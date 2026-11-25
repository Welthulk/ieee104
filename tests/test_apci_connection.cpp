/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_apci_connection.cpp
 * @brief Rules R1..R12 of the APCI state machine (src/apci/connection.cpp), simulated time.
 *
 * Each test names the rule it checks; the sources of the rules are listed in
 * connection.cpp. Frames of the peer are either produced by a second
 * Connection (tests/apci_pair.hpp) or written by hand when the test needs a
 * frame a correct peer would never send. Hand-written frames follow the
 * layouts in src/codec/apci.hpp; STARTDT/TESTFR octets also appear in
 * tests/data/lib60870_capture.txt.
 */
#include "apci_pair.hpp"
#include "codec/apci.hpp"

#include <array>
#include <gtest/gtest.h>
#include <vector>

using namespace iec104;
using namespace iec104::apci;
using iec104::test::Pair;

namespace {

// A small ASDU (M_SP_NA_1, 1 object); the APCI layer does not look inside.
const std::array<std::uint8_t, 10> kAsdu{0x01, 0x01, 0x03, 0x00, 0x01,
                                         0x00, 0x64, 0x00, 0x00, 0x01};

ApciConfig tolerant() {
    ApciConfig c{};
    c.strict = false;
    return c;
}

// Feeds hand-written bytes into one connection and collects its events.
void feed(test::Side& side, const std::vector<std::uint8_t>& bytes, TimeMs now) {
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const std::size_t n = side.conn.receive(bytes.data() + pos, bytes.size() - pos, now);
        pos += n;
        side.collect();
        if (n == 0U) {
            break;
        }
    }
}

// Takes the pending output of one side as a byte vector.
std::vector<std::uint8_t> output(test::Side& side) {
    std::array<std::uint8_t, 4096> buf{};
    const std::size_t n = side.conn.take_output(buf.data(), buf.size());
    return {buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n)};
}

// An I-frame with the given counters and kAsdu.
std::vector<std::uint8_t> i_frame(std::uint16_t ns, std::uint16_t nr) {
    std::array<std::uint8_t, codec::kApciLength> h{};
    EXPECT_EQ(codec::encode_i_header(ns, nr, kAsdu.size(), h.data(), h.size()), IEC104_OK);
    std::vector<std::uint8_t> f(h.begin(), h.end());
    f.insert(f.end(), kAsdu.begin(), kAsdu.end());
    return f;
}

std::vector<std::uint8_t> s_frame(std::uint16_t nr) {
    std::array<std::uint8_t, codec::kApciLength> f{};
    EXPECT_EQ(codec::encode_s_frame(nr, f.data(), f.size()), IEC104_OK);
    return {f.begin(), f.end()};
}

} // namespace

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// Rule: defaults and ranges of t0..t3, k, w (docs/03 timer table), t3 = 0
// only tolerant (docs/07_Normfragen_und_Festlegungen.md, E3), plausibility as warning/error/off
// (E2).
TEST(ApciConfig, RangesAndPlausibility) {
    ConfigWarnings w{};
    ApciConfig c{};
    EXPECT_EQ(validate(c, w), IEC104_OK);
    EXPECT_FALSE(w.t2_not_below_t1 || w.w_above_two_thirds_k);

    c.t1_ms = 256U * kSecond;
    EXPECT_EQ(validate(c, w), IEC104_ERR_OUT_OF_RANGE);
    c = ApciConfig{};
    c.t2_ms = 999;
    EXPECT_EQ(validate(c, w), IEC104_ERR_OUT_OF_RANGE);
    c = ApciConfig{};
    c.k = 0;
    EXPECT_EQ(validate(c, w), IEC104_ERR_OUT_OF_RANGE);
    c = ApciConfig{};
    c.t3_ms = kMaxT3 + 1U;
    EXPECT_EQ(validate(c, w), IEC104_ERR_OUT_OF_RANGE);

    c = ApciConfig{};
    c.t3_ms = 0;
    EXPECT_EQ(validate(c, w), IEC104_ERR_OUT_OF_RANGE);
    c.strict = false;
    EXPECT_EQ(validate(c, w), IEC104_OK);

    // t1 = 250 s, t2 = 240 s is fine; t2 = t1 and w > 2/3 k are warnings.
    c = ApciConfig{};
    c.t1_ms = 250U * kSecond;
    c.t2_ms = 240U * kSecond;
    EXPECT_EQ(validate(c, w), IEC104_OK);
    EXPECT_FALSE(w.t2_not_below_t1);
    c.t2_ms = c.t1_ms;
    c.w = 9; // 3 * 9 > 2 * 12
    EXPECT_EQ(validate(c, w), IEC104_OK);
    EXPECT_TRUE(w.t2_not_below_t1);
    EXPECT_TRUE(w.w_above_two_thirds_k);
    c.plausibility = PlausibilityCheck::Error;
    EXPECT_EQ(validate(c, w), IEC104_ERR_INVALID_ARGUMENT);
    c.plausibility = PlausibilityCheck::Off;
    EXPECT_EQ(validate(c, w), IEC104_OK);
    EXPECT_FALSE(w.t2_not_below_t1);
}

// ---------------------------------------------------------------------------
// Start, stop, test frames
// ---------------------------------------------------------------------------

// Rule R2: the client sends STARTDT act (68 04 07 00 00 00), the server
// confirms (68 04 0B 00 00 00); both are then Started.
// Source of the octets: tests/data/lib60870_capture.txt, first two lines.
TEST(ApciConnection, StartDataTransfer) {
    Pair p;
    ASSERT_EQ(p.client.conn.start(p.now), IEC104_OK);
    EXPECT_EQ(p.client.conn.state(), State::StartPending);
    p.pump();
    EXPECT_EQ(p.client.sent_bytes, (std::vector<std::uint8_t>{0x68, 0x04, 0x07, 0x00, 0x00, 0x00}));
    EXPECT_EQ(p.server.sent_bytes, (std::vector<std::uint8_t>{0x68, 0x04, 0x0B, 0x00, 0x00, 0x00}));
    EXPECT_EQ(p.client.conn.state(), State::Started);
    EXPECT_EQ(p.server.conn.state(), State::Started);
}

// Rule R2: only the controlling station sends STARTDT/STOPDT act; a
// controlled station that receives a STARTDT con, or a controlling station
// that receives STARTDT act, closes.
TEST(ApciConnection, StartStopRoles) {
    Pair p;
    EXPECT_EQ(p.server.conn.start(p.now), IEC104_ERR_WRONG_STATE);
    EXPECT_EQ(p.client.conn.stop(p.now), IEC104_ERR_WRONG_STATE); // not started yet

    feed(p.client, {0x68, 0x04, 0x07, 0x00, 0x00, 0x00}, p.now); // STARTDT act to the client
    EXPECT_EQ(p.client.close_reason(), CloseReason::UnexpectedFrame);
}

// Rule R1: no data before STARTDT; send_asdu() refuses, and a strict server
// closes on an I-frame while stopped, a tolerant one accepts it.
TEST(ApciConnection, NoDataBeforeStart) {
    Pair p;
    EXPECT_EQ(p.client.conn.send_asdu(kAsdu.data(), kAsdu.size(), p.now), IEC104_ERR_WRONG_STATE);
    feed(p.server, i_frame(0, 0), p.now);
    EXPECT_EQ(p.server.close_reason(), CloseReason::UnexpectedFrame);

    Pair t(tolerant(), tolerant());
    feed(t.server, i_frame(0, 0), t.now);
    EXPECT_EQ(t.server.close_reason(), CloseReason::None);
    EXPECT_EQ(t.server.asdus(), 1U);
}

// Rule R3: TESTFR act is answered in every state, also before STARTDT.
// Source of the octets: layout in apci.hpp (TESTFR act 0x43, con 0x83).
TEST(ApciConnection, TestFrameAnsweredWhileStopped) {
    Pair p;
    feed(p.server, {0x68, 0x04, 0x43, 0x00, 0x00, 0x00}, p.now);
    EXPECT_EQ(output(p.server), (std::vector<std::uint8_t>{0x68, 0x04, 0x83, 0x00, 0x00, 0x00}));
    EXPECT_EQ(p.server.conn.state(), State::Stopped);
}

// Rule R12: STOPDT. The client acknowledges what it has received, the server
// confirms only after its own frames are acknowledged.
TEST(ApciConnection, StopWaitsForAcknowledgement) {
    Pair p;
    p.start();
    // Server sends one frame; the client has not acknowledged it yet (w = 8, t2 = 10 s).
    ASSERT_EQ(p.server.conn.send_asdu(kAsdu.data(), kAsdu.size(), p.now), IEC104_OK);
    p.pump();
    EXPECT_EQ(p.server.conn.unacknowledged(), 1U);

    // STOPDT: the client sends S-frame (N(R) = 1) and STOPDT act in this order.
    p.client.sent_bytes.clear();
    ASSERT_EQ(p.client.conn.stop(p.now), IEC104_OK);
    p.pump();
    EXPECT_EQ(p.client.sent_bytes, (std::vector<std::uint8_t>{0x68, 0x04, 0x01, 0x00, 0x02, 0x00,
                                                              0x68, 0x04, 0x13, 0x00, 0x00, 0x00}));
    EXPECT_EQ(p.server.conn.state(), State::Stopped);
    EXPECT_EQ(p.client.conn.state(), State::Stopped);
}

// Rule R12, server side: STOPDT act arrives while own frames are open; the
// server stays StopPending until the acknowledgement arrives.
TEST(ApciConnection, ServerStopPendingUntilAcknowledged) {
    Pair p;
    p.start();
    ASSERT_EQ(p.server.conn.send_asdu(kAsdu.data(), kAsdu.size(), p.now), IEC104_OK);
    (void)output(p.server); // frame lost in transit: the client never sees it
    feed(p.server, {0x68, 0x04, 0x13, 0x00, 0x00, 0x00}, p.now); // STOPDT act
    EXPECT_EQ(p.server.conn.state(), State::StopPending);
    EXPECT_TRUE(output(p.server).empty()); // no con yet
    feed(p.server, s_frame(1), p.now);
    EXPECT_EQ(output(p.server), (std::vector<std::uint8_t>{0x68, 0x04, 0x23, 0x00, 0x00, 0x00}));
    EXPECT_EQ(p.server.conn.state(), State::Stopped);
}

// ---------------------------------------------------------------------------
// Sequence numbers and acknowledgement
// ---------------------------------------------------------------------------

// Rule R5: an I-frame with N(S) other than V(R) closes the connection.
TEST(ApciConnection, SequenceGapCloses) {
    Pair p;
    p.start();
    feed(p.client, i_frame(1, 0), p.now); // expected N(S) = 0
    EXPECT_EQ(p.client.close_reason(), CloseReason::SequenceError);
}

// Rule R6: N(R) beyond the frames sent closes the connection; an N(R) equal
// to the current acknowledgement is fine (nothing new acknowledged).
TEST(ApciConnection, AcknowledgeOutOfWindowCloses) {
    Pair p;
    p.start();
    feed(p.client, s_frame(0), p.now);
    EXPECT_EQ(p.client.close_reason(), CloseReason::None);
    feed(p.client, s_frame(1), p.now); // client has sent nothing
    EXPECT_EQ(p.client.close_reason(), CloseReason::AckOutOfWindow);
}

// Rule R7: at most k unacknowledged I-frames; after an acknowledgement
// sending resumes. Rule R6: acknowledgement is cumulative.
TEST(ApciConnection, WindowK) {
    ApciConfig c{};
    c.k = 3;
    c.w = 2;
    Pair p(c, c);
    p.start();
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(p.client.conn.send_asdu(kAsdu.data(), kAsdu.size(), p.now), IEC104_OK);
    }
    EXPECT_EQ(p.client.conn.send_asdu(kAsdu.data(), kAsdu.size(), p.now), IEC104_ERR_WOULD_BLOCK);
    EXPECT_FALSE(p.client.conn.can_send());
    (void)output(p.client);            // lost: no acknowledgement can come
    feed(p.client, s_frame(3), p.now); // one cumulative acknowledgement for all three
    EXPECT_EQ(p.client.conn.unacknowledged(), 0U);
    EXPECT_TRUE(p.client.conn.can_send());
}

// Rule R8: after w received I-frames an S-frame is sent at once, with N(R) = w.
TEST(ApciConnection, AcknowledgeAfterW) {
    Pair p;
    p.start();
    for (std::uint16_t i = 0; i < 7; ++i) {
        feed(p.client, i_frame(i, 0), p.now);
    }
    EXPECT_TRUE(output(p.client).empty()); // w = 8 not reached
    feed(p.client, i_frame(7, 0), p.now);
    EXPECT_EQ(output(p.client), s_frame(8));
}

// Rule R9: without own data the acknowledgement follows when t2 expires;
// an own I-frame acknowledges with its N(R) and stops t2.
TEST(ApciConnection, AcknowledgeAfterT2OrPiggyBack) {
    Pair p;
    p.start();
    feed(p.client, i_frame(0, 0), p.now);
    p.client.conn.tick(p.now + 9999);
    EXPECT_TRUE(output(p.client).empty());
    p.client.conn.tick(p.now + 10000);
    EXPECT_EQ(output(p.client), s_frame(1));

    feed(p.client, i_frame(1, 0), p.now + 10000);
    ASSERT_EQ(p.client.conn.send_asdu(kAsdu.data(), kAsdu.size(), p.now + 10001), IEC104_OK);
    const std::vector<std::uint8_t> sent = output(p.client);
    EXPECT_EQ(std::vector<std::uint8_t>(sent.begin(), sent.begin() + 6),
              (std::vector<std::uint8_t>{0x68, 0x0E, 0x00, 0x00, 0x04, 0x00})); // N(S) 0, N(R) 2
    p.client.conn.tick(p.now + 30000);
    EXPECT_TRUE(output(p.client).empty()); // t2 was stopped by the I-frame
}

// Rule R4: N(S) and N(R) wrap from 32767 to 0 without error.
TEST(ApciConnection, SequenceWrap) {
    ApciConfig c{};
    c.w = 1; // acknowledge every frame, keeps the test simple
    Pair p(c, c);
    p.start();
    const std::size_t total = codec::kSequenceModulo + 5U;
    for (std::size_t i = 0; i < total; ++i) {
        ASSERT_EQ(p.server.conn.send_asdu(kAsdu.data(), kAsdu.size(), p.now), IEC104_OK) << i;
        p.pump();
    }
    p.client.events.clear(); // keep memory low; counting below uses the server side
    EXPECT_EQ(p.server.conn.unacknowledged(), 0U);
    EXPECT_EQ(p.client.close_reason(), CloseReason::None);
    EXPECT_EQ(p.server.close_reason(), CloseReason::None);
}

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------

// Rule R10: an I-frame not acknowledged within t1 closes the connection.
TEST(ApciConnection, T1ForIFrames) {
    Pair p;
    p.start();
    ASSERT_EQ(p.client.conn.send_asdu(kAsdu.data(), kAsdu.size(), p.now), IEC104_OK);
    (void)output(p.client); // lost
    EXPECT_EQ(p.client.conn.next_deadline(), p.now + 15000);
    p.client.conn.tick(p.now + 14999);
    EXPECT_EQ(p.client.conn.state(), State::Started);
    p.client.conn.tick(p.now + 15000);
    p.client.collect();
    EXPECT_EQ(p.client.close_reason(), CloseReason::T1Timeout);
}

// Rule R10: a STARTDT act without confirmation within t1 closes the connection.
TEST(ApciConnection, T1ForStartDt) {
    Pair p;
    ASSERT_EQ(p.client.conn.start(p.now), IEC104_OK);
    (void)output(p.client); // lost
    p.client.conn.tick(p.now + 15000);
    p.client.collect();
    EXPECT_EQ(p.client.close_reason(), CloseReason::T1Timeout);
}

// Rule R11: after t3 without received frame a TESTFR act is sent; the
// confirmation stops t1. Without confirmation t1 closes.
TEST(ApciConnection, T3IdleTest) {
    Pair p;
    p.start();
    p.client.sent_bytes.clear();
    p.advance(20000); // t3 = 20 s: both sides test, both answer
    EXPECT_EQ(p.client.close_reason(), CloseReason::None);
    EXPECT_EQ(p.server.close_reason(), CloseReason::None);
    const std::vector<std::uint8_t> testfr{0x68, 0x04, 0x43, 0x00, 0x00, 0x00};
    EXPECT_TRUE(std::search(p.client.sent_bytes.begin(), p.client.sent_bytes.end(), testfr.begin(),
                            testfr.end()) != p.client.sent_bytes.end());

    // Peer silent: the test is not confirmed and t1 closes.
    Pair q;
    q.start();
    q.client.conn.tick(q.now + 20000);
    (void)output(q.client);
    q.client.conn.tick(q.now + 20000 + 15000);
    q.client.collect();
    EXPECT_EQ(q.client.close_reason(), CloseReason::T1Timeout);
}

// Rule: t3 = 0 (tolerant) switches the idle test off.
TEST(ApciConnection, T3Off) {
    ApciConfig c = tolerant();
    c.t3_ms = 0;
    Pair p(c, c);
    p.start();
    EXPECT_EQ(p.client.conn.next_deadline(), kNoDeadline);
    p.client.conn.tick(p.now + 1000000);
    EXPECT_TRUE(output(p.client).empty());
}

// ---------------------------------------------------------------------------
// Byte stream
// ---------------------------------------------------------------------------

// Rule: TCP may split frames anywhere; frames delivered byte by byte are
// processed like whole frames.
TEST(ApciConnection, ByteByByte) {
    Pair p;
    p.start();
    std::vector<std::uint8_t> stream = i_frame(0, 0);
    const std::vector<std::uint8_t> second = i_frame(1, 0);
    stream.insert(stream.end(), second.begin(), second.end());
    for (const std::uint8_t b : stream) {
        EXPECT_EQ(p.client.conn.receive(&b, 1, p.now), 1U);
        p.client.collect();
    }
    EXPECT_EQ(p.client.asdus(), 2U);
}

// Rule: a byte stream that is not an APDU sequence closes the connection
// (no resynchronisation, see codec::peek_apdu_length()).
TEST(ApciConnection, MalformedStreamCloses) {
    Pair p;
    feed(p.server, {0x69, 0x04, 0x07, 0x00, 0x00, 0x00}, p.now);
    EXPECT_EQ(p.server.close_reason(), CloseReason::MalformedFrame);
    EXPECT_EQ(p.server.conn.state(), State::Closed);
}

// ---------------------------------------------------------------------------
// Rules from public evidence (docs/05_Quellen_APCI.md)
// ---------------------------------------------------------------------------

// Rule R6 (amendment 1, 5.1): N(R) acknowledges all frames with N(S) < N(R).
TEST(ApciConnection, AcknowledgeBelowNr) {
    Pair p;
    p.start();
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(p.client.conn.send_asdu(kAsdu.data(), kAsdu.size(), p.now), IEC104_OK);
    }
    (void)output(p.client);
    feed(p.client, s_frame(2), p.now); // acknowledges N(S) 0 and 1, not 2
    EXPECT_EQ(p.client.conn.unacknowledged(), 1U);
}

// Rule R1 (figure 17): a stopped controlled station closes on an S-frame;
// tolerant mode accepts it.
TEST(ApciConnection, StoppedServerClosesOnSFrame) {
    Pair p;
    feed(p.server, s_frame(0), p.now);
    EXPECT_EQ(p.server.close_reason(), CloseReason::UnexpectedFrame);

    Pair t(tolerant(), tolerant());
    feed(t.server, s_frame(0), t.now);
    EXPECT_EQ(t.server.close_reason(), CloseReason::None);
}

// Rule R12 (figure 17): after STOPDT act with open frames, an I-frame from
// the controlling station closes the controlled station.
TEST(ApciConnection, ServerStopPendingClosesOnIFrame) {
    Pair p;
    p.start();
    ASSERT_EQ(p.server.conn.send_asdu(kAsdu.data(), kAsdu.size(), p.now), IEC104_OK);
    (void)output(p.server);
    feed(p.server, {0x68, 0x04, 0x13, 0x00, 0x00, 0x00}, p.now); // STOPDT act
    ASSERT_EQ(p.server.conn.state(), State::StopPending);
    feed(p.server, i_frame(0, 1), p.now);
    EXPECT_EQ(p.server.close_reason(), CloseReason::UnexpectedFrame);
}

// Rule R12 (own decision): frames that arrive after the own STOPDT act are
// acknowledged at once, so the peer can confirm without waiting for t2.
TEST(ApciConnection, ClientAcknowledgesAtOnceWhileStopping) {
    Pair p;
    p.start();
    ASSERT_EQ(p.client.conn.stop(p.now), IEC104_OK);
    (void)output(p.client);               // STOPDT act on its way
    feed(p.client, i_frame(0, 0), p.now); // server had sent this before it saw the act
    EXPECT_EQ(output(p.client), s_frame(1));
    EXPECT_EQ(p.client.asdus(), 1U);
}
