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
 * A server owns one listening socket and any number of connections (links).
 * Every accepted connection gets an id (1, 2, ...) that the callbacks and
 * iec104_server_send_asdu() use. Connections start in STOPPED; the
 * controlling station starts data transfer with STARTDT, which the state
 * machine confirms by itself.
 *
 * Layer: API.
 */
#include "api/link_common.hpp"

#include <new>
#include <poll.h>

using namespace iec104;

struct iec104_server : api::CallbackSink {
    iec104_server(const apci::ApciConfig& c, const iec104_callbacks* cb) noexcept
        : api::CallbackSink(cb), config(c) {}

    apci::ApciConfig config;
    transport::Socket listener;
    std::vector<std::unique_ptr<transport::Link>> links;
    std::uint32_t next_id{1};

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
                ++next_id;
                links.push_back(std::move(link));
            }
        }
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
    // The handle is owned by the C caller until iec104_server_destroy().
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out = new (std::nothrow) iec104_server(a, callbacks);
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

void iec104_server_destroy(iec104_server* server) {
    // Ends the ownership of the C caller; sockets close in their destructors.
    delete server; // NOLINT(cppcoreguidelines-owning-memory)
}

} // extern "C"
