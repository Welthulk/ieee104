/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file server.cpp
 * @brief C API of the server (controlled station), see iec104.h.
 *
 * A server owns one listening socket, any number of connections (links),
 * the process image and one application-layer session per connection.
 * Every accepted connection gets an id (1, 2, ...) that the callbacks and
 * the send functions use. Connections start in STOPPED; the controlling
 * station starts data transfer with STARTDT, which the state machine
 * confirms by itself. Requests (interrogation, commands, ...) are answered
 * by the session (src/session/server_session.hpp).
 *
 * Layer: API.
 */
#include "api/link_common.hpp"
#include "api/values.hpp"
#include "session/command_table.hpp"
#include "session/process_image.hpp"
#include "session/server_session.hpp"

#include <array>
#include <map>
#include <new>
#include <poll.h>

using namespace iec104;

namespace {

/** @brief Lets a session send over a TCP link. */
class LinkSender : public session::AsduSender {
  public:
    explicit LinkSender(transport::Link& link) noexcept : link_(link) {}
    bool can_send() override { return link_.can_send(); }
    iec104_status send(const std::uint8_t* asdu, std::size_t length) override {
        return link_.send_asdu(asdu, length, transport::monotonic_ms());
    }

  private:
    transport::Link& link_;
};

} // namespace

struct iec104_server : api::CallbackSink, session::CommandHandler {
    iec104_server(const apci::ApciConfig& c, const iec104_link_config& link,
                  const iec104_callbacks* cb) noexcept
        : api::CallbackSink(cb), config(c), profile(api::to_profile(link)),
          originator(link.originator) {}

    apci::ApciConfig config;
    codec::AsduProfile profile;
    std::uint8_t originator;
    transport::Socket listener;
    std::vector<std::unique_ptr<transport::Link>> links;
    std::map<std::uint32_t, session::ServerSession> sessions;
    session::ProcessImage image;
    session::CommandTable commands;
    session::SessionOptions options;
    std::uint32_t next_id{1};
    std::uint32_t current_connection{0}; // connection of the request being handled

    transport::Link* find(std::uint32_t id) noexcept {
        for (auto& link : links) {
            if (link->id() == id) {
                return link.get();
            }
        }
        return nullptr;
    }

    // Accepts all pending connections.
    void accept_pending() noexcept {
        for (;;) {
            transport::Socket s;
            if (transport::accept_tcp(listener, s) != IEC104_OK) {
                return;
            }
            auto link = std::make_unique<transport::Link>();
            if (link->init_accepted(next_id, config, std::move(s), transport::monotonic_ms()) ==
                IEC104_OK) {
                sessions.emplace(next_id, session::ServerSession(profile, originator));
                ++next_id;
                links.push_back(std::move(link));
            }
        }
    }

    // Sends what the sessions have queued, as far as the windows allow.
    void pump_all() noexcept {
        for (auto& link : links) {
            const auto it = sessions.find(link->id());
            if (it != sessions.end() && !link->closed()) {
                LinkSender sender(*link);
                it->second.pump(sender);
            }
        }
    }

    // --- LinkSink: events of the links --------------------------------------

    void on_asdu(std::uint32_t id, const std::uint8_t* asdu, std::size_t length) override {
        api::CallbackSink::on_asdu(id, asdu, length);
        const auto it = sessions.find(id);
        current_connection = id;
        const session::StationContext station{
            &image, &commands, &options,
            session::SessionClock{transport::monotonic_ms(), transport::utc_ms()}};
        const bool handled =
            it != sessions.end() && it->second.handle(asdu, length, station, *this);
        current_connection = 0;
        if (!handled) {
            deliver_objects(id, asdu, length, profile);
        }
    }

    void on_state(std::uint32_t id, apci::State state) override {
        api::CallbackSink::on_state(id, state);
        const auto it = sessions.find(id);
        if (state == apci::State::Started && it != sessions.end()) {
            it->second.on_started(image, options);
        }
    }

    void on_closed(std::uint32_t id, transport::LinkCloseReason reason,
                   apci::CloseReason protocol_reason) override {
        api::CallbackSink::on_closed(id, reason, protocol_reason);
        sessions.erase(id);
    }

    // --- CommandHandler: the application decides --------------------------

    bool on_command(const codec::AsduHeader& header,
                    const codec::InformationObject& object) override {
        if (callbacks().on_command == nullptr) {
            return false; // no handler: every command is rejected
        }
        iec104_object c{};
        if (!api::from_codec(header.type_id, object, c)) {
            return false;
        }
        const iec104_asdu_info info = api::to_info(header);
        return callbacks().on_command(callbacks().user, current_connection, &info, &c) != 0;
    }

    // Queues @p asdu on one or all started connections.
    iec104_status enqueue(std::uint32_t connection, const std::uint8_t* asdu, std::size_t length) {
        bool any = false;
        bool full = false;
        for (auto& link : links) {
            if ((connection != 0U && link->id() != connection) ||
                link->state() != apci::State::Started) {
                continue;
            }
            const auto it = sessions.find(link->id());
            if (it == sessions.end()) {
                continue;
            }
            const iec104_status st = it->second.enqueue(asdu, length);
            any = any || st == IEC104_OK;
            full = full || st == IEC104_ERR_WOULD_BLOCK;
        }
        pump_all();
        if (full) {
            return IEC104_ERR_WOULD_BLOCK;
        }
        return any ? IEC104_OK : IEC104_ERR_WRONG_STATE;
    }
};

extern "C" {

iec104_status iec104_server_create(const iec104_link_config* config,
                                   const iec104_callbacks* callbacks, iec104_server** out) {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    iec104_link_config c{};
    iec104_link_config_init(&c);
    if (config != nullptr) {
        c = *config;
    }
    const apci::ApciConfig a = api::to_apci(c, apci::Role::Controlled);
    apci::ConfigWarnings warnings{};
    const iec104_status st = apci::validate(a, warnings);
    if (st != IEC104_OK) {
        return st;
    }
    if (!codec::is_valid(api::to_profile(c))) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    // The handle is owned by the C caller until iec104_server_destroy().
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out = new (std::nothrow) iec104_server(a, c, callbacks);
    return *out != nullptr ? IEC104_OK : IEC104_ERR_INTERNAL;
}

iec104_status iec104_server_listen(iec104_server* server, const char* address, uint16_t port) {
    if (server == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (server->listener.valid()) {
        return IEC104_ERR_WRONG_STATE;
    }
    return transport::listen_tcp(address, port, server->listener);
}

uint16_t iec104_server_port(const iec104_server* server) {
    return (server != nullptr && server->listener.valid()) ? transport::local_port(server->listener)
                                                           : 0U;
}

iec104_status iec104_server_poll(iec104_server* server, int timeout_ms) {
    if (server == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    short listener_revents = 0;
    const iec104_status st =
        api::poll_links(server->links, &server->listener, timeout_ms, *server, listener_revents);
    if (st == IEC104_OK && (listener_revents & POLLIN) != 0) {
        server->accept_pending();
    }
    // Answers queued while handling the received frames go out now.
    server->pump_all();
    return st;
}

iec104_status iec104_server_send_asdu(iec104_server* server, uint32_t connection,
                                      const uint8_t* asdu, size_t length) {
    if (server == nullptr || asdu == nullptr || length == 0U || length > codec::kMaxAsduLength) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    const apci::TimeMs now = transport::monotonic_ms();
    if (connection != 0U) {
        transport::Link* link = server->find(connection);
        return link != nullptr ? link->send_asdu(asdu, length, now) : IEC104_ERR_WRONG_STATE;
    }
    // Broadcast to every started connection.
    bool sent = false;
    bool blocked = false;
    for (auto& link : server->links) {
        if (link->state() != apci::State::Started) {
            continue;
        }
        const iec104_status st = link->send_asdu(asdu, length, now);
        sent = sent || st == IEC104_OK;
        blocked = blocked || st == IEC104_ERR_WOULD_BLOCK;
    }
    if (blocked) {
        return IEC104_ERR_WOULD_BLOCK;
    }
    return sent ? IEC104_OK : IEC104_ERR_WRONG_STATE;
}

size_t iec104_server_connection_count(const iec104_server* server) {
    return server != nullptr ? server->links.size() : 0U;
}

void iec104_server_options_init(iec104_server_options* options) {
    if (options != nullptr) {
        options->send_end_of_init = 1;
        options->command_termination = 1;
        options->cause_of_init = 0;
        options->max_command_delay_ms = 0;
    }
}

iec104_status iec104_server_set_options(iec104_server* server,
                                        const iec104_server_options* options) {
    if (server == nullptr || options == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    server->options.send_end_of_init = options->send_end_of_init != 0;
    server->options.command_termination = options->command_termination != 0;
    server->options.cause_of_init = options->cause_of_init;
    server->options.max_command_delay_ms = options->max_command_delay_ms;
    return IEC104_OK;
}

iec104_status iec104_server_add_command(iec104_server* server, uint16_t common_address,
                                        uint32_t ioa, uint8_t type, iec104_command_model model,
                                        uint32_t select_timeout_ms) {
    if (server == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    session::CommandPoint point{};
    point.type = type;
    point.model = model == IEC104_COMMAND_SELECT_EXECUTE ? session::CommandModel::SelectExecute
                                                         : session::CommandModel::Direct;
    point.select_timeout_ms = select_timeout_ms;
    return server->commands.set(common_address, ioa, point);
}

iec104_status iec104_server_add_point(iec104_server* server, uint16_t common_address,
                                      const iec104_object* point, uint16_t groups) {
    if (server == nullptr || point == nullptr ||
        session::ProcessImage::base_type(point->type) == 0U) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    codec::InformationObject o{};
    const iec104_status st = api::to_codec(*point, o);
    if (st != IEC104_OK) {
        return st;
    }
    return server->image.set(common_address, point->type, o, groups);
}

iec104_status iec104_server_update(iec104_server* server, uint16_t common_address,
                                   const iec104_object* object) {
    if (server == nullptr || object == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    codec::InformationObject o{};
    iec104_status st = api::to_codec(*object, o);
    if (st != IEC104_OK) {
        return st;
    }
    st = server->image.update(common_address, object->type, o);
    if (st != IEC104_OK) {
        return st;
    }
    codec::AsduHeader h{};
    h.type_id = object->type;
    h.cause = IEC104_COT_SPONTANEOUS;
    h.originator = server->originator;
    h.common_address = common_address;
    std::array<std::uint8_t, codec::kMaxAsduLength> buf{};
    std::size_t n = 0;
    st = codec::encode_asdu(h, &o, 1, server->profile, buf.data(), buf.size(), n);
    if (st != IEC104_OK) {
        return st;
    }
    st = server->enqueue(0, buf.data(), n);
    // No started connection is not an error for an update: the image holds the value.
    return st == IEC104_ERR_WRONG_STATE ? IEC104_OK : st;
}

iec104_status iec104_server_send(iec104_server* server, uint32_t connection,
                                 uint16_t common_address, uint8_t cause,
                                 const iec104_object* objects, size_t count) {
    if (server == nullptr || objects == nullptr || count == 0U) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    std::vector<codec::InformationObject> list(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (objects[i].type != objects[0].type) {
            return IEC104_ERR_INVALID_ARGUMENT; // one type per ASDU
        }
        const iec104_status st = api::to_codec(objects[i], list[i]);
        if (st != IEC104_OK) {
            return st;
        }
    }
    codec::AsduHeader h{};
    h.type_id = objects[0].type;
    h.cause = cause;
    h.originator = server->originator;
    h.common_address = common_address;
    const codec::InformationObject* next = list.data();
    std::size_t remaining = count;
    iec104_status result = IEC104_OK;
    while (remaining > 0U) {
        std::array<std::uint8_t, codec::kMaxAsduLength> buf{};
        std::size_t n = 0;
        std::size_t used = 0;
        const iec104_status st = codec::encode_asdu_fitting(h, next, remaining, server->profile,
                                                            buf.data(), buf.size(), n, used);
        if (st != IEC104_OK) {
            return st;
        }
        result = server->enqueue(connection, buf.data(), n);
        if (result != IEC104_OK) {
            return result;
        }
        next += used;
        remaining -= used;
    }
    return result;
}

void iec104_server_destroy(iec104_server* server) {
    // Ends the ownership of the C caller; sockets close in their destructors.
    delete server; // NOLINT(cppcoreguidelines-owning-memory)
}

} // extern "C"
