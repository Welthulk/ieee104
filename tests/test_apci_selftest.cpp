/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_apci_selftest.cpp
 * @brief Self-test without network: client and server state machines run against each other.
 *
 * Requirement F10 (docs/04_Analyse.md): the library tests itself until the
 * integrated test against the peer of the customer takes place. This file is
 * the network-free part: complete ASDUs built by the codec travel through
 * two APCI connections coupled in memory (tests/apci_pair.hpp), with
 * simulated time, through a whole connection life cycle.
 */
#include "apci_pair.hpp"
#include "codec/asdu.hpp"

#include <array>
#include <gtest/gtest.h>

using namespace iec104;
using iec104::test::Pair;

namespace {

// Builds one M_ME_NB_1 ASDU (scaled value) with the given IOA and value.
std::array<std::uint8_t, 32> measurement(std::uint32_t ioa, std::int16_t value, std::size_t& len) {
    codec::AsduHeader h{};
    h.type_id = 11;
    h.cause = 3; // spontaneous
    h.common_address = 1;
    codec::InformationObject o{};
    o.ioa = ioa;
    o.sva = value;
    std::array<std::uint8_t, 32> out{};
    EXPECT_EQ(codec::encode_asdu(h, &o, 1, codec::kIec104Profile, out.data(), out.size(), len),
              IEC104_OK);
    return out;
}

// Decodes the ASDU of an AsduReceived event and returns its scaled value.
std::int16_t value_of(const apci::Event& e) {
    std::array<codec::InformationObject, 1> objects{};
    codec::DecodedAsdu d{};
    EXPECT_EQ(codec::decode_asdu(e.asdu.data(), e.asdu_length, codec::kIec104Profile,
                                 objects.data(), objects.size(), d),
              IEC104_OK);
    return objects[0].sva;
}

} // namespace

// Scenario: start, 1000 measurements server -> client and 100 client ->
// server with k-limited bursts, idle time with link tests, stop, restart,
// more data, local close. Checks that every ASDU arrives once, in order,
// unchanged, and that no side closes unexpectedly.
TEST(ApciSelfTest, FullLifeCycle) {
    Pair p;
    p.start();
    ASSERT_EQ(p.client.conn.state(), apci::State::Started);

    // Server sends 1000 values as fast as the window allows.
    std::int16_t next_value = 0;
    while (next_value < 1000) {
        std::size_t len = 0;
        const auto asdu = measurement(100, next_value, len);
        const iec104_status st = p.server.conn.send_asdu(asdu.data(), len, p.now);
        if (st == IEC104_ERR_WOULD_BLOCK) {
            p.advance(10); // let acknowledgements come back (w or t2)
            continue;
        }
        ASSERT_EQ(st, IEC104_OK);
        ++next_value;
        p.pump();
    }
    // Client sends 100 values the other way.
    for (std::int16_t v = 0; v < 100; ++v) {
        std::size_t len = 0;
        const auto asdu = measurement(200, v, len);
        ASSERT_EQ(p.client.conn.send_asdu(asdu.data(), len, p.now), IEC104_OK);
        p.pump();
    }

    // Idle for one minute: t2 acknowledges the rest, t3 tests the link.
    p.advance(60000);
    EXPECT_EQ(p.server.conn.unacknowledged(), 0U);
    EXPECT_EQ(p.client.conn.unacknowledged(), 0U);

    // Every value arrived once and in order.
    std::int16_t expected = 0;
    for (const apci::Event& e : p.client.events) {
        if (e.kind == apci::EventKind::AsduReceived) {
            EXPECT_EQ(value_of(e), expected++);
        }
    }
    EXPECT_EQ(expected, 1000);
    EXPECT_EQ(p.server.asdus(), 100U);

    // Stop and start again; data flows again afterwards.
    ASSERT_EQ(p.client.conn.stop(p.now), IEC104_OK);
    p.pump();
    EXPECT_EQ(p.client.conn.state(), apci::State::Stopped);
    EXPECT_EQ(p.server.conn.state(), apci::State::Stopped);
    std::size_t len = 0;
    const auto asdu = measurement(100, 4711, len);
    EXPECT_EQ(p.server.conn.send_asdu(asdu.data(), len, p.now), IEC104_ERR_WRONG_STATE);
    p.start();
    ASSERT_EQ(p.server.conn.send_asdu(asdu.data(), len, p.now), IEC104_OK);
    p.pump();
    EXPECT_EQ(p.client.asdus(), 1001U);

    EXPECT_EQ(p.client.close_reason(), apci::CloseReason::None);
    EXPECT_EQ(p.server.close_reason(), apci::CloseReason::None);

    p.client.conn.close();
    p.client.collect();
    EXPECT_EQ(p.client.close_reason(), apci::CloseReason::LocalRequest);
}
