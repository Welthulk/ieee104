/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file client.cpp
 * @brief C API of the client (controlling station), see iec104.h.
 *
 * A client has at most one connection (link id 1). After
 * iec104_client_connect() the TCP connect runs in the background of
 * iec104_client_poll(); when it completes the state callback reports
 * STOPPED, and the application sends STARTDT with iec104_client_start().
 *
 * Layer: API.
 */
#include "api/link_common.hpp"
#include "api/values.hpp"
#include "session/command_table.hpp"
#include "session/command_tracker.hpp"

#include <array>
#include <new>
#include <string>

using namespace iec104;

namespace {
constexpr std::uint32_t kClientLinkId = 1;
}

struct iec104_client : api::CallbackSink {
    iec104_client(const apci::ApciConfig& c, const iec104_link_config& link,
                  const iec104_callbacks* cb) noexcept
        : api::CallbackSink(cb), config(c), profile(api::to_profile(link)),
          originator(link.originator) {}

    apci::ApciConfig config;
    codec::AsduProfile profile;
    std::uint8_t originator;
    // A vector of at most one entry, so that the shared poll loop can be used.
    std::vector<std::unique_ptr<transport::Link>> links;

    transport::Link* link() noexcept { return links.empty() ? nullptr : links.front().get(); }

    session::CommandTracker tracker;
    session::CommandTimeouts timeouts;
    iec104_link_stats counters{};   // command and answer counters of the client
    iec104_link_stats last_stats{}; // all counters of the last closed connection
    bool had_connection{false};

    void on_asdu(std::uint32_t id, const std::uint8_t* asdu, std::size_t length) override {
        api::CallbackSink::on_asdu(id, asdu, length);
        deliver_objects(id, asdu, length, profile);
        codec::AsduHeader header{};
        std::size_t header_length = 0;
        if (codec::decode_asdu_header(asdu, length, profile, header, header_length) == IEC104_OK &&
            header.negative) {
            ++counters.negative_answers;
            logger().write(IEC104_LOG_WARNING, id,
                           "negative answer: type " + std::to_string(header.type_id) + ", cause " +
                               std::to_string(header.cause));
        }
        if (tracker.open() == 0U) {
            return;
        }
        // Answers to own commands: confirmation, termination, causes 44..47.
        std::array<codec::InformationObject, codec::kMaxObjects> objects{};
        codec::DecodedAsdu d{};
        if (codec::decode_asdu(asdu, length, profile, objects.data(), objects.size(), d) !=
                IEC104_OK ||
            d.type == nullptr) {
            return;
        }
        std::vector<session::CommandOutcome> outcomes;
        std::vector<session::PendingCommand> to_execute;
        const std::uint64_t now = transport::monotonic_ms();
        tracker.on_asdu(d.header, objects.data(), d.object_count, now, timeouts, outcomes,
                        to_execute);
        for (const session::PendingCommand& exec : to_execute) {
            // Second step of select_execute(): the execute of the selected command.
            const iec104_status st =
                send_object(exec.common_address, IEC104_COT_ACTIVATION, exec.object, exec.type);
            if (st == IEC104_OK) {
                tracker.add(exec.common_address, exec.type, exec.object, false, now, timeouts);
            } else {
                outcomes.push_back(session::CommandOutcome{
                    exec.common_address, exec.type, exec.object, session::CommandResult::Rejected});
            }
        }
        report(outcomes);
    }

    void on_closed(std::uint32_t id, transport::LinkCloseReason reason,
                   apci::CloseReason protocol_reason) override {
        api::CallbackSink::on_closed(id, reason, protocol_reason);
        if (link() != nullptr) {
            last_stats = stats_of(*link());
        }
        std::vector<session::CommandOutcome> outcomes;
        tracker.connection_lost(outcomes);
        report(outcomes);
    }

    // All counters of @p l plus the client's own.
    [[nodiscard]] iec104_link_stats stats_of(const transport::Link& l) const noexcept {
        iec104_link_stats s = counters;
        api::copy_link_stats(l, s);
        return s;
    }

    // Counts, logs and passes finished commands to on_command_result.
    void report(const std::vector<session::CommandOutcome>& outcomes) {
        for (const session::CommandOutcome& o : outcomes) {
            switch (o.result) {
            case session::CommandResult::Confirmed:
            case session::CommandResult::Terminated:
            case session::CommandResult::Selected:
                ++counters.commands_accepted;
                break;
            case session::CommandResult::Rejected:
                ++counters.commands_rejected;
                break;
            case session::CommandResult::NoConfirmation:
            case session::CommandResult::NoTermination:
                ++counters.commands_discarded;
                break;
            case session::CommandResult::ConnectionLost:
                break;
            }
            logger().write(IEC104_LOG_INFO, kClientLinkId,
                           "command type " + std::to_string(o.type) + " ioa " +
                               std::to_string(o.object.ioa) + ": " +
                               iec104_command_result_string(to_c(o.result)));
        }
        if (callbacks().on_command_result == nullptr) {
            return;
        }
        for (const session::CommandOutcome& o : outcomes) {
            iec104_object c{};
            if (api::from_codec(o.type, o.object, c)) {
                callbacks().on_command_result(callbacks().user, o.common_address, &c,
                                              to_c(o.result));
            }
        }
    }

    static iec104_command_result to_c(session::CommandResult r) noexcept {
        switch (r) {
        case session::CommandResult::Confirmed:
            return IEC104_COMMAND_CONFIRMED;
        case session::CommandResult::Terminated:
            return IEC104_COMMAND_TERMINATED;
        case session::CommandResult::Selected:
            return IEC104_COMMAND_SELECTED;
        case session::CommandResult::Rejected:
            return IEC104_COMMAND_REJECTED;
        case session::CommandResult::NoConfirmation:
            return IEC104_COMMAND_NO_CONFIRMATION;
        case session::CommandResult::NoTermination:
            return IEC104_COMMAND_NO_TERMINATION;
        case session::CommandResult::ConnectionLost:
            return IEC104_COMMAND_CONNECTION_LOST;
        }
        return IEC104_COMMAND_REJECTED;
    }

    // Sends a command and starts following it.
    iec104_status send_tracked(std::uint16_t common_address, const iec104_object& command,
                               bool then_execute) {
        const codec::TypeInfo* type = codec::find_type(command.type);
        if (type == nullptr || type->direction != codec::Direction::Control) {
            return IEC104_ERR_INVALID_ARGUMENT;
        }
        codec::InformationObject o{};
        const iec104_status st = api::to_codec(command, o);
        if (st != IEC104_OK) {
            return st;
        }
        const iec104_status sent =
            send_object(common_address, IEC104_COT_ACTIVATION, o, command.type);
        if (sent == IEC104_OK) {
            tracker.add(common_address, command.type, o, then_execute, transport::monotonic_ms(),
                        timeouts);
        }
        return sent;
    }

    // Encodes one object with cause activation and sends it.
    iec104_status send_object(std::uint16_t common_address, std::uint8_t cause,
                              const codec::InformationObject& object, std::uint8_t type) {
        transport::Link* l = link();
        if (l == nullptr) {
            return IEC104_ERR_WRONG_STATE;
        }
        codec::AsduHeader h{};
        h.type_id = type;
        h.cause = cause;
        h.originator = originator;
        h.common_address = common_address;
        std::array<std::uint8_t, codec::kMaxAsduLength> buf{};
        std::size_t n = 0;
        const iec104_status st =
            codec::encode_asdu(h, &object, 1, profile, buf.data(), buf.size(), n);
        if (st != IEC104_OK) {
            return st;
        }
        return l->send_asdu(buf.data(), n, transport::monotonic_ms());
    }
};

extern "C" {

iec104_status iec104_client_create(const iec104_link_config* config,
                                   const iec104_callbacks* callbacks, iec104_client** out) {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    iec104_link_config c{};
    iec104_link_config_init(&c);
    if (config != nullptr) {
        c = *config;
    }
    const apci::ApciConfig a = api::to_apci(c, apci::Role::Controlling);
    apci::ConfigWarnings warnings{};
    const iec104_status st = apci::validate(a, warnings);
    if (st != IEC104_OK) {
        return st;
    }
    if (!codec::is_valid(api::to_profile(c))) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    // The handle is owned by the C caller until iec104_client_destroy().
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out = new (std::nothrow) iec104_client(a, c, callbacks);
    return *out != nullptr ? IEC104_OK : IEC104_ERR_INTERNAL;
}

iec104_status iec104_client_connect(iec104_client* client, const char* host, uint16_t port) {
    if (client == nullptr || host == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (client->link() != nullptr) {
        return IEC104_ERR_WRONG_STATE;
    }
    transport::Socket s;
    const iec104_status st = transport::connect_tcp(host, port, s);
    if (st != IEC104_OK) {
        return st;
    }
    auto link = std::make_unique<transport::Link>();
    const iec104_status init = link->init_connecting(kClientLinkId, client->config, std::move(s),
                                                     transport::monotonic_ms());
    if (init != IEC104_OK) {
        return init;
    }
    link->set_trace(client);
    client->logger().write(IEC104_LOG_INFO, kClientLinkId,
                           "connecting to " + std::string(host) + ":" + std::to_string(port));
    client->links.push_back(std::move(link));
    client->had_connection = true;
    client->counters = iec104_link_stats{};
    return IEC104_OK;
}

iec104_status iec104_client_start(iec104_client* client) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    transport::Link* link = client->link();
    return link != nullptr ? link->start(transport::monotonic_ms()) : IEC104_ERR_WRONG_STATE;
}

iec104_status iec104_client_stop(iec104_client* client) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    transport::Link* link = client->link();
    return link != nullptr ? link->stop(transport::monotonic_ms()) : IEC104_ERR_WRONG_STATE;
}

iec104_status iec104_client_poll(iec104_client* client, int timeout_ms) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    short unused = 0;
    const iec104_status st = api::poll_links(client->links, nullptr, timeout_ms, *client, unused);
    // Time limits of open commands.
    std::vector<session::CommandOutcome> outcomes;
    client->tracker.tick(transport::monotonic_ms(), outcomes);
    client->report(outcomes);
    return st;
}

iec104_status iec104_client_send_asdu(iec104_client* client, const uint8_t* asdu, size_t length) {
    if (client == nullptr || asdu == nullptr || length == 0U || length > codec::kMaxAsduLength) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    transport::Link* link = client->link();
    return link != nullptr ? link->send_asdu(asdu, length, transport::monotonic_ms())
                           : IEC104_ERR_WRONG_STATE;
}

iec104_status iec104_client_send_command(iec104_client* client, uint16_t common_address,
                                         const iec104_object* command) {
    if (client == nullptr || command == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    return client->send_tracked(common_address, *command, false);
}

iec104_status iec104_client_select_execute(iec104_client* client, uint16_t common_address,
                                           const iec104_object* command) {
    if (client == nullptr || command == nullptr || !session::is_command_type(command->type) ||
        command->type == 51U || command->type == 64U) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    iec104_object select = *command;
    select.select = 1;
    return client->send_tracked(common_address, select, true);
}

void iec104_client_options_init(iec104_client_options* options) {
    if (options != nullptr) {
        const session::CommandTimeouts defaults{};
        options->confirmation_timeout_ms = defaults.confirmation_ms;
        options->termination_timeout_ms = defaults.termination_ms;
        options->expect_termination = defaults.expect_termination ? 1 : 0;
    }
}

iec104_status iec104_client_set_options(iec104_client* client,
                                        const iec104_client_options* options) {
    if (client == nullptr || options == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    client->timeouts.confirmation_ms = options->confirmation_timeout_ms;
    client->timeouts.termination_ms = options->termination_timeout_ms;
    client->timeouts.expect_termination = options->expect_termination != 0;
    return IEC104_OK;
}

iec104_status iec104_client_interrogation(iec104_client* client, uint16_t common_address,
                                          uint8_t qoi) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    codec::InformationObject o{}; // IOA 0: the request concerns the whole station
    o.qoi = qoi;
    return client->send_object(common_address, IEC104_COT_ACTIVATION, o, 100);
}

iec104_status iec104_client_counter_interrogation(iec104_client* client, uint16_t common_address,
                                                  uint8_t rqt, uint8_t frz) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    codec::InformationObject o{};
    o.qcc = codec::Qcc{rqt, frz};
    return client->send_object(common_address, IEC104_COT_ACTIVATION, o, 101);
}

iec104_status iec104_client_clock_sync(iec104_client* client, uint16_t common_address,
                                       const iec104_time* time) {
    if (client == nullptr || time == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    codec::InformationObject o{};
    const iec104_status st = api::to_cp56(*time, o.cp56);
    if (st != IEC104_OK) {
        return st;
    }
    return client->send_object(common_address, IEC104_COT_ACTIVATION, o, 103);
}

iec104_link_state iec104_client_state(const iec104_client* client) {
    if (client == nullptr || client->links.empty()) {
        return IEC104_LINK_CLOSED;
    }
    const transport::Link& link = *client->links.front();
    if (link.closed()) {
        return IEC104_LINK_CLOSED;
    }
    return link.connecting() ? IEC104_LINK_CONNECTING : api::to_c(link.state());
}

void iec104_client_close(iec104_client* client) {
    if (client != nullptr && client->link() != nullptr) {
        client->link()->close();
    }
}

void iec104_client_set_log(iec104_client* client, iec104_log_level max_level, iec104_log_fn fn,
                           void* user) {
    if (client == nullptr) {
        return;
    }
    client->logger().set(max_level, fn, user);
    api::log_config_warnings(client->config, client->logger());
}

iec104_status iec104_client_get_stats(const iec104_client* client, iec104_link_stats* out) {
    if (client == nullptr || out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (!client->links.empty()) {
        *out = client->stats_of(*client->links.front());
        return IEC104_OK;
    }
    if (!client->had_connection) {
        return IEC104_ERR_WRONG_STATE;
    }
    *out = client->last_stats;
    return IEC104_OK;
}

void iec104_client_destroy(iec104_client* client) {
    delete client; // NOLINT(cppcoreguidelines-owning-memory): ends the ownership of the C caller
}

} // extern "C"
