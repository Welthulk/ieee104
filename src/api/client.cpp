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

#include <new>

using namespace iec104;

namespace {
constexpr std::uint32_t kClientLinkId = 1;
}

struct iec104_client : api::CallbackSink {
    iec104_client(const apci::ApciConfig& c, const iec104_callbacks* cb) noexcept
        : api::CallbackSink(cb), config(c) {}

    apci::ApciConfig config;
    // A vector of at most one entry, so that the shared poll loop can be used.
    std::vector<std::unique_ptr<transport::Link>> links;

    transport::Link* link() noexcept { return links.empty() ? nullptr : links.front().get(); }
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
    // The handle is owned by the C caller until iec104_client_destroy().
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out = new (std::nothrow) iec104_client(a, callbacks);
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
