/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_tcp_loopback.cpp
 * @brief Server and client of the C API over real TCP on the loopback interface.
 *
 * Only the public C API (iec104.h) is used, as an application would. The
 * server listens on port 0 (chosen by the system), so parallel test runs do
 * not collide.
 */
#include "iec104/iec104.h"

#include <array>
#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include <vector>

namespace {

// Records what the callbacks report.
struct Recorder {
    std::vector<std::vector<std::uint8_t>> asdus;
    std::vector<iec104_link_state> states;
    iec104_close_reason close_reason{IEC104_CLOSE_NONE};
    std::uint32_t last_connection{0};

    iec104_callbacks callbacks() {
        iec104_callbacks cb{};
        cb.user = this;
        cb.on_asdu = [](void* user, std::uint32_t connection, const std::uint8_t* asdu,
                        std::size_t length) {
            auto* self = static_cast<Recorder*>(user);
            self->last_connection = connection;
            self->asdus.emplace_back(asdu, asdu + length);
        };
        cb.on_state = [](void* user, std::uint32_t connection, iec104_link_state state,
                         iec104_close_reason reason) {
            auto* self = static_cast<Recorder*>(user);
            self->last_connection = connection;
            self->states.push_back(state);
            if (state == IEC104_LINK_CLOSED) {
                self->close_reason = reason;
            }
        };
        return cb;
    }
    [[nodiscard]] bool saw(iec104_link_state s) const {
        for (const auto x : states) {
            if (x == s) {
                return true;
            }
        }
        return false;
    }
};

// Polls both sides until @p done or about 5 s have passed.
bool run_until(iec104_server* server, iec104_client* client, const std::function<bool()>& done) {
    for (int i = 0; i < 5000 && !done(); ++i) {
        if (server != nullptr) {
            (void)iec104_server_poll(server, 1);
        }
        if (client != nullptr) {
            (void)iec104_client_poll(client, 1);
        }
    }
    return done();
}

// An M_ME_NB_1 ASDU with the value in the last octets; content is not checked by the transport.
std::array<std::uint8_t, 12> asdu_with(std::uint16_t value) {
    return {0x0B,
            0x01,
            0x03,
            0x00,
            0x01,
            0x00,
            0x64,
            0x00,
            0x00,
            static_cast<std::uint8_t>(value & 0xFFU),
            static_cast<std::uint8_t>(value >> 8U),
            0x00};
}

} // namespace

// Rule: a client connects, starts data transfer, ASDUs flow in both
// directions in order, STOPDT and close are reported on both sides.
// Source: TASK-003 goal; APCI rules see docs/05_Quellen_APCI.md.
TEST(TcpLoopback, ConnectStartExchangeStopClose) {
    Recorder srv;
    Recorder cli;
    iec104_callbacks scb = srv.callbacks();
    iec104_callbacks ccb = cli.callbacks();
    iec104_server* server = nullptr;
    iec104_client* client = nullptr;
    ASSERT_EQ(iec104_server_create(nullptr, &scb, &server), IEC104_OK);
    ASSERT_EQ(iec104_server_listen(server, "127.0.0.1", 0), IEC104_OK);
    const std::uint16_t port = iec104_server_port(server);
    ASSERT_NE(port, 0U);

    ASSERT_EQ(iec104_client_create(nullptr, &ccb, &client), IEC104_OK);
    ASSERT_EQ(iec104_client_connect(client, "127.0.0.1", port), IEC104_OK);
    ASSERT_TRUE(run_until(server, client, [&] { return cli.saw(IEC104_LINK_STOPPED); }));
    EXPECT_EQ(iec104_client_send_asdu(client, asdu_with(0).data(), 12), IEC104_ERR_WRONG_STATE);

    ASSERT_EQ(iec104_client_start(client), IEC104_OK);
    ASSERT_TRUE(run_until(server, client, [&] {
        return cli.saw(IEC104_LINK_STARTED) && srv.saw(IEC104_LINK_STARTED);
    }));
    EXPECT_EQ(iec104_server_connection_count(server), 1U);

    // Server -> client: 200 ASDUs, more than k, so the window must open again.
    std::uint16_t sent = 0;
    ASSERT_TRUE(run_until(server, client, [&] {
        while (sent < 200U) {
            const auto a = asdu_with(sent);
            const iec104_status st = iec104_server_send_asdu(server, 0, a.data(), a.size());
            if (st != IEC104_OK) {
                break; // window full: poll and retry
            }
            ++sent;
        }
        return cli.asdus.size() == 200U;
    }));
    for (std::uint16_t i = 0; i < 200U; ++i) {
        const auto expected = asdu_with(i);
        EXPECT_EQ(cli.asdus[i], std::vector<std::uint8_t>(expected.begin(), expected.end()));
    }

    // Client -> server.
    for (std::uint16_t i = 0; i < 5U; ++i) {
        ASSERT_EQ(iec104_client_send_asdu(client, asdu_with(i).data(), 12), IEC104_OK);
    }
    ASSERT_TRUE(run_until(server, client, [&] { return srv.asdus.size() == 5U; }));
    EXPECT_EQ(srv.last_connection, 1U);

    ASSERT_EQ(iec104_client_stop(client), IEC104_OK);
    ASSERT_TRUE(run_until(server, client,
                          [&] { return iec104_client_state(client) == IEC104_LINK_STOPPED; }));

    iec104_client_close(client);
    ASSERT_TRUE(run_until(server, client, [&] {
        return cli.saw(IEC104_LINK_CLOSED) && srv.saw(IEC104_LINK_CLOSED);
    }));
    EXPECT_EQ(cli.close_reason, IEC104_CLOSE_LOCAL);
    EXPECT_EQ(srv.close_reason, IEC104_CLOSE_PEER);
    EXPECT_EQ(iec104_server_connection_count(server), 0U);

    iec104_client_destroy(client);
    iec104_server_destroy(server);
}

// Rule: a refused connect is reported as CLOSED with CONNECT_FAILED.
// Source: TASK-003; the port is taken from a server that is destroyed again.
TEST(TcpLoopback, ConnectRefused) {
    iec104_server* server = nullptr;
    ASSERT_EQ(iec104_server_create(nullptr, nullptr, &server), IEC104_OK);
    ASSERT_EQ(iec104_server_listen(server, "127.0.0.1", 0), IEC104_OK);
    const std::uint16_t port = iec104_server_port(server);
    iec104_server_destroy(server); // nobody listens on this port any more

    Recorder cli;
    iec104_callbacks ccb = cli.callbacks();
    iec104_client* client = nullptr;
    ASSERT_EQ(iec104_client_create(nullptr, &ccb, &client), IEC104_OK);
    ASSERT_EQ(iec104_client_connect(client, "127.0.0.1", port), IEC104_OK);
    ASSERT_TRUE(run_until(nullptr, client, [&] { return cli.saw(IEC104_LINK_CLOSED); }));
    EXPECT_EQ(cli.close_reason, IEC104_CLOSE_CONNECT_FAILED);
    iec104_client_destroy(client);
}

// Rule: invalid arguments and configurations are rejected.
// Source: iec104.h contracts; t3 = 0 needs tolerant mode (docs/04 W6).
TEST(TcpLoopback, ArgumentChecks) {
    iec104_link_config c{};
    iec104_link_config_init(&c);
    EXPECT_EQ(c.t1_ms, 15000U);
    EXPECT_EQ(c.k, 12U);
    c.t3_ms = 0;
    iec104_server* server = nullptr;
    EXPECT_EQ(iec104_server_create(&c, nullptr, &server), IEC104_ERR_OUT_OF_RANGE);
    c.tolerant = 1;
    ASSERT_EQ(iec104_server_create(&c, nullptr, &server), IEC104_OK);
    EXPECT_EQ(iec104_server_create(&c, nullptr, nullptr), IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(iec104_server_listen(server, "no-such-host.invalid", 0), IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(iec104_server_send_asdu(server, 0, nullptr, 1), IEC104_ERR_INVALID_ARGUMENT);
    std::array<std::uint8_t, 12> a = asdu_with(1);
    EXPECT_EQ(iec104_server_send_asdu(server, 0, a.data(), a.size()), IEC104_ERR_WRONG_STATE);
    iec104_server_destroy(server);
    EXPECT_STREQ(iec104_close_reason_string(IEC104_CLOSE_T1_TIMEOUT),
                 "t1 timeout (no acknowledgement)");
    EXPECT_STREQ(iec104_link_state_string(IEC104_LINK_STARTED), "started");
}
