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

#include <array>
#include <new>

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

    void on_asdu(std::uint32_t id, const std::uint8_t* asdu, std::size_t length) override {
        api::CallbackSink::on_asdu(id, asdu, length);
        deliver_objects(id, asdu, length, profile);
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
    client->links.push_back(std::move(link));
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
    return api::poll_links(client->links, nullptr, timeout_ms, *client, unused);
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
    const codec::TypeInfo* type = codec::find_type(command->type);
    if (type == nullptr || type->direction != codec::Direction::Control) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    codec::InformationObject o{};
    const iec104_status st = api::to_codec(*command, o);
    if (st != IEC104_OK) {
        return st;
    }
    return client->send_object(common_address, IEC104_COT_ACTIVATION, o, command->type);
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

void iec104_client_destroy(iec104_client* client) {
    delete client; // NOLINT(cppcoreguidelines-owning-memory): ends the ownership of the C caller
}

} // extern "C"
