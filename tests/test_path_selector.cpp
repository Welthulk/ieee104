/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_path_selector.cpp
 * @brief Path redundancy of the controlling station (src/session/path_selector.hpp).
 *
 * Rules: docs/05_Normfragen_und_Festlegungen.md, E11 and E12. Every test is a
 * sequence of events with fixed times; values are short floats (type 13) of
 * station 1, so that each delivered object can be recognised by IOA and value.
 */
#include "session/path_selector.hpp"

#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

using iec104::session::PathAction;
using iec104::session::PathActionKind;
using iec104::session::PathSelector;
using iec104::session::PathSettings;
namespace codec = iec104::codec;

namespace {

codec::AsduHeader header(std::uint8_t type = 13, std::uint8_t cause = 3) {
    codec::AsduHeader h{};
    h.type_id = type;
    h.cause = cause;
    h.common_address = 1;
    h.num_objects = 1;
    return h;
}

codec::InformationObject value(std::uint32_t ioa, float v) {
    codec::InformationObject o{};
    o.ioa = ioa;
    o.r32 = v;
    return o;
}

// (IOA, value) of every Deliver action, in order.
std::vector<std::pair<std::uint32_t, float>> delivered(const std::vector<PathAction>& actions) {
    std::vector<std::pair<std::uint32_t, float>> out;
    for (const PathAction& a : actions) {
        if (a.kind != PathActionKind::Deliver) {
            continue;
        }
        codec::InformationObject o{};
        codec::DecodedAsdu d{};
        EXPECT_EQ(codec::decode_asdu(a.asdu.data(), a.asdu.size(), codec::AsduProfile{}, &o, 1, d),
                  IEC104_OK);
        out.emplace_back(o.ioa, o.r32);
    }
    return out;
}

std::size_t count(const std::vector<PathAction>& actions, PathActionKind kind) {
    std::size_t n = 0;
    for (const PathAction& a : actions) {
        n += a.kind == kind ? 1U : 0U;
    }
    return n;
}

const PathAction* find(const std::vector<PathAction>& actions, PathActionKind kind) {
    for (const PathAction& a : actions) {
        if (a.kind == kind) {
            return &a;
        }
    }
    return nullptr;
}

// Two started paths, path 0 leading.
struct Two {
    PathSettings settings;
    PathSelector sel;
    std::vector<PathAction> out;
    explicit Two(const PathSettings& s = PathSettings{})
        : settings(s), sel(2, s, codec::AsduProfile{}) {
        sel.started(0, 0, out);
        sel.started(1, 10, out);
        out.clear();
    }
    // The same value on both paths, path 1 arriving @p lag ms later.
    void both(std::uint32_t ioa, float v, std::uint64_t t, std::uint64_t lag = 20) {
        EXPECT_TRUE(sel.received(0, header(), value(ioa, v), t));
        EXPECT_FALSE(sel.received(1, header(), value(ioa, v), t + lag));
    }
};

} // namespace

// Rule (E11): the first path that reaches "started" is leading; only its
// objects are delivered; a later started path follows and is buffered.
TEST(PathSelector, FirstStartedLeadsOthersFollow) {
    PathSelector sel(2, PathSettings{}, codec::AsduProfile{});
    std::vector<PathAction> out;
    EXPECT_EQ(sel.leading(), PathSelector::kNoPath);
    sel.started(1, 0, out);
    ASSERT_EQ(sel.leading(), 1U);
    ASSERT_EQ(out.size(), 1U); // Switched, no interrogation for the very first start
    EXPECT_EQ(out[0].kind, PathActionKind::Switched);
    EXPECT_EQ(out[0].previous, PathSelector::kNoPath);
    out.clear();
    sel.started(0, 5, out);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(sel.leading(), 1U);
    EXPECT_TRUE(sel.received(1, header(), value(100, 1.0F), 10));
    EXPECT_FALSE(sel.received(0, header(), value(100, 1.0F), 12));
    EXPECT_EQ(sel.buffered(0), 1U);
    EXPECT_EQ(sel.buffered(1), 0U);
    // A path that is not started delivers nothing and keeps nothing.
    PathSelector idle(2, PathSettings{}, codec::AsduProfile{});
    EXPECT_FALSE(idle.received(0, header(), value(100, 1.0F), 0));
    EXPECT_EQ(idle.buffered(0), 0U);
}

// Rule (E11): leading path lost -> the other takes over; what it received
// after the counterpart of the last delivered object is replayed in order;
// then an interrogation on the new path.
TEST(PathSelector, SilentFailureReplaysTheGap) {
    Two t;
    t.both(100, 1.0F, 1000);
    t.both(101, 5.0F, 1100);
    // Path 0 fails silently: only path 1 still receives.
    EXPECT_FALSE(t.sel.received(1, header(), value(100, 2.0F), 2000));
    EXPECT_FALSE(t.sel.received(1, header(), value(101, 6.0F), 3000));
    EXPECT_FALSE(t.sel.received(1, header(), value(100, 3.0F), 4000));
    t.sel.lost(0, "closed: t1 timeout", 36000, t.out);
    EXPECT_EQ(t.sel.leading(), 1U);
    const PathAction* sw = find(t.out, PathActionKind::Switched);
    ASSERT_NE(sw, nullptr);
    EXPECT_EQ(sw->path, 1U);
    EXPECT_EQ(sw->previous, 0U);
    EXPECT_EQ(sw->reason, "path 1 closed: t1 timeout");
    EXPECT_EQ(sw->replayed, 3U);
    EXPECT_EQ(sw->suppressed, 2U);                           // the two values both paths had
    EXPECT_EQ(t.out.front().kind, PathActionKind::Switched); // reported before the replay
    using V = std::vector<std::pair<std::uint32_t, float>>;
    EXPECT_EQ(delivered(t.out), (V{{100, 2.0F}, {101, 6.0F}, {100, 3.0F}}));
    ASSERT_EQ(t.out.back().kind, PathActionKind::Interrogate);
    EXPECT_EQ(t.out.back().path, 1U);
    EXPECT_EQ(t.sel.counters(1).replayed, 3U);
    EXPECT_EQ(t.sel.buffered(1), 0U);
    // From now on path 1 delivers directly.
    EXPECT_TRUE(t.sel.received(1, header(), value(100, 4.0F), 37000));
}

// Rule (E11): a value that returns to an earlier one (a-b-a) is matched with
// the occurrence closest in time to the last delivery, so that the trailing
// change is not lost and the earlier ones are not repeated.
TEST(PathSelector, RepeatedValueMatchesClosestInTime) {
    Two t;
    t.both(100, 1.0F, 1000);
    t.both(100, 2.0F, 2000);
    t.both(100, 1.0F, 3000);                             // last delivered: 1.0 at 3000
    t.sel.received(1, header(), value(100, 2.0F), 4000); // only on path 1
    t.sel.lost(0, "closed by peer", 5000, t.out);
    using V = std::vector<std::pair<std::uint32_t, float>>;
    EXPECT_EQ(delivered(t.out), (V{{100, 2.0F}}));
    EXPECT_EQ(find(t.out, PathActionKind::Switched)->suppressed, 3U);
}

// Rule (E11): arrival times of both paths may differ by the tolerance; the
// time tag of the objects is not compared (each station sets its own).
TEST(PathSelector, TimeTagsDifferValuesMatch) {
    PathSettings s;
    s.replay_tolerance_ms = 500;
    Two t(s);
    codec::InformationObject a = value(200, 7.5F);
    a.cp56.milliseconds = 1000;
    codec::InformationObject b = a;
    b.cp56.milliseconds = 1003; // the other station stamps 3 ms later
    EXPECT_TRUE(t.sel.received(0, header(36), a, 1000));
    EXPECT_FALSE(t.sel.received(1, header(36), b, 1400)); // 400 ms later
    t.sel.received(1, header(36), value(200, 8.0F), 2000);
    t.sel.lost(0, "closed by peer", 3000, t.out);
    using V = std::vector<std::pair<std::uint32_t, float>>;
    EXPECT_EQ(delivered(t.out), (V{{200, 8.0F}}));
}

// Rule (E11): no counterpart within the tolerance -> everything from (last
// delivery - tolerance) on; a point never delivered -> everything.
TEST(PathSelector, WithoutCounterpartFromLastDelivery) {
    PathSettings s;
    s.replay_tolerance_ms = 500;
    Two t(s);
    EXPECT_TRUE(t.sel.received(0, header(), value(100, 1.0F), 5000));
    // Path 1 had another value before (the stations differ), then changes.
    t.sel.received(1, header(), value(100, 9.0F), 1000); // older than 5000 - 500: dropped
    t.sel.received(1, header(), value(100, 9.5F), 4700); // within the tolerance: kept
    t.sel.received(1, header(), value(100, 2.0F), 6000);
    t.sel.received(1, header(), value(300, 4.0F), 1500); // point never delivered: kept
    t.sel.lost(0, "closed by peer", 7000, t.out);
    using V = std::vector<std::pair<std::uint32_t, float>>;
    EXPECT_EQ(delivered(t.out), (V{{100, 9.5F}, {100, 2.0F}, {300, 4.0F}}));
}

// Rule (E11): the buffer keeps objects for the window and at most the given
// number; what was dropped is counted and reported with the switch-over.
TEST(PathSelector, WindowAndSizeLimitTheBuffer) {
    PathSettings s;
    s.replay_window_ms = 10000;
    s.replay_max_objects = 3;
    Two t(s);
    for (std::uint32_t i = 0; i < 5; ++i) {
        t.sel.received(1, header(), value(400 + i, 1.0F), 1000 + i);
    }
    EXPECT_EQ(t.sel.buffered(1), 3U); // two dropped for size
    t.sel.tick(20000);                // all older than the window
    EXPECT_EQ(t.sel.buffered(1), 0U);
    t.sel.received(1, header(), value(500, 1.0F), 20001);
    t.sel.lost(0, "closed by peer", 20002, t.out);
    const PathAction* sw = find(t.out, PathActionKind::Switched);
    ASSERT_NE(sw, nullptr);
    EXPECT_EQ(sw->dropped, 5U);
    EXPECT_EQ(sw->replayed, 1U);
    EXPECT_EQ(t.sel.counters(1).dropped, 5U);
}

// Rule (E11, D6): no started path -> AllLost; the next path that starts is
// leading and interrogates; a returning path follows (no switch back).
TEST(PathSelector, AllLostAndReturn) {
    Two t;
    t.sel.lost(1, "closed by peer", 100, t.out); // follower: nothing to report
    EXPECT_TRUE(t.out.empty());
    t.sel.lost(0, "closed: t1 timeout", 200, t.out);
    ASSERT_EQ(t.out.size(), 1U);
    EXPECT_EQ(t.out[0].kind, PathActionKind::AllLost);
    EXPECT_EQ(t.out[0].reason, "path 1 closed: t1 timeout, no other path started");
    EXPECT_EQ(t.sel.leading(), PathSelector::kNoPath);
    t.out.clear();
    t.sel.started(1, 5000, t.out);
    EXPECT_EQ(t.sel.leading(), 1U);
    EXPECT_EQ(count(t.out, PathActionKind::Switched), 1U);
    EXPECT_EQ(count(t.out, PathActionKind::Interrogate), 1U);
    t.out.clear();
    t.sel.started(0, 6000, t.out); // back, but path 1 stays leading
    EXPECT_TRUE(t.out.empty());
    EXPECT_EQ(t.sel.leading(), 1U);
}

// Option prefer_first_path: path 1 (index 0) takes over again when it is back.
TEST(PathSelector, PreferFirstPath) {
    PathSettings s;
    s.prefer_first_path = true;
    Two t(s);
    t.sel.lost(0, "closed by peer", 100, t.out);
    EXPECT_EQ(t.sel.leading(), 1U);
    t.out.clear();
    t.sel.started(0, 5000, t.out);
    EXPECT_EQ(t.sel.leading(), 0U);
    const PathAction* sw = find(t.out, PathActionKind::Switched);
    ASSERT_NE(sw, nullptr);
    EXPECT_EQ(sw->reason, "preferred path 1 is back");
    EXPECT_EQ(sw->previous, 1U);
}

// Rule (E11): with three paths the next started one in order takes over;
// interrogation after a switch-over can be switched off.
TEST(PathSelector, ThreePathsAndNoInterrogation) {
    PathSettings s;
    s.interrogation_after_switch = false;
    PathSelector sel(3, s, codec::AsduProfile{});
    std::vector<PathAction> out;
    sel.started(0, 0, out);
    sel.started(1, 0, out);
    sel.started(2, 0, out);
    sel.lost(1, "closed by peer", 10, out); // follower
    out.clear();
    sel.lost(0, "closed by peer", 20, out);
    EXPECT_EQ(sel.leading(), 2U);
    EXPECT_EQ(count(out, PathActionKind::Interrogate), 0U);
    EXPECT_EQ(sel.counters(2).became_leading, 1U);
}

// Objects of control direction and types outside the table are not buffered;
// the leading path delivers them unchanged.
TEST(PathSelector, OnlyMonitorObjectsAreBuffered) {
    Two t;
    codec::InformationObject cmd{};
    cmd.ioa = 6000;
    EXPECT_FALSE(t.sel.received(1, header(45, 7), cmd, 100));
    EXPECT_EQ(t.sel.buffered(1), 0U);
    EXPECT_TRUE(t.sel.received(0, header(45, 7), cmd, 100));
}
