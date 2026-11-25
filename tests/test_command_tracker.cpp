/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_command_tracker.cpp
 * @brief Command results of the controlling station (src/session/command_tracker.hpp).
 *
 * Answers are written as headers and objects by hand; time is simulated.
 * Sequence activation -> confirmation -> termination: usual reading of
 * clause 7 (docs/05_Normfragen_und_Festlegungen.md, N8).
 */
#include "session/command_tracker.hpp"

#include <gtest/gtest.h>
#include <vector>

using namespace iec104;
using namespace iec104::session;

namespace {

codec::InformationObject single(std::uint32_t ioa, bool select) {
    codec::InformationObject o{};
    o.ioa = ioa;
    o.sco.on = true;
    o.sco.select = select;
    return o;
}

codec::AsduHeader answer(std::uint8_t type, std::uint8_t cause, bool negative = false) {
    codec::AsduHeader h{};
    h.type_id = type;
    h.cause = cause;
    h.negative = negative;
    h.common_address = 1;
    return h;
}

struct Fixture {
    CommandTracker tracker;
    CommandTimeouts timeouts;
    std::vector<CommandOutcome> outcomes;
    std::vector<PendingCommand> to_execute;
    std::uint64_t now{1000};

    void receive(const codec::AsduHeader& h, const codec::InformationObject& o) {
        tracker.on_asdu(h, &o, 1, now, timeouts, outcomes, to_execute);
    }
};

} // namespace

// Rule: an executed command ends with its termination; without expected
// termination the positive confirmation is final.
TEST(CommandTracker, ConfirmedAndTerminated) {
    Fixture f;
    f.tracker.add(1, 45, single(6000, false), false, f.now, f.timeouts);
    f.receive(answer(45, 7), single(6000, false));
    EXPECT_TRUE(f.outcomes.empty());
    f.receive(answer(45, 10), single(6000, false));
    ASSERT_EQ(f.outcomes.size(), 1U);
    EXPECT_EQ(f.outcomes[0].result, CommandResult::Terminated);

    Fixture g;
    g.timeouts.expect_termination = false;
    g.tracker.add(1, 49, codec::InformationObject{}, false, g.now, g.timeouts);
    g.receive(answer(49, 7), codec::InformationObject{});
    ASSERT_EQ(g.outcomes.size(), 1U);
    EXPECT_EQ(g.outcomes[0].result, CommandResult::Confirmed);
}

// Rule: negative confirmation and causes 44..47 reject the command.
TEST(CommandTracker, Rejected) {
    Fixture f;
    f.tracker.add(1, 45, single(6000, false), false, f.now, f.timeouts);
    f.receive(answer(45, 7, true), single(6000, false));
    ASSERT_EQ(f.outcomes.size(), 1U);
    EXPECT_EQ(f.outcomes[0].result, CommandResult::Rejected);

    f.tracker.add(1, 45, single(9999, false), false, f.now, f.timeouts);
    f.receive(answer(45, 47, true), single(9999, false));
    ASSERT_EQ(f.outcomes.size(), 2U);
    EXPECT_EQ(f.outcomes[1].result, CommandResult::Rejected);
    EXPECT_EQ(f.tracker.open(), 0U);
}

// Rule: a separate select ends with Selected; select_execute hands back the
// execute (S/E = 0) after the positive confirmation of the select.
TEST(CommandTracker, SelectAndAutomaticExecute) {
    Fixture f;
    f.tracker.add(1, 45, single(6000, true), false, f.now, f.timeouts);
    f.receive(answer(45, 7), single(6000, true));
    ASSERT_EQ(f.outcomes.size(), 1U);
    EXPECT_EQ(f.outcomes[0].result, CommandResult::Selected);

    Fixture g;
    g.tracker.add(1, 45, single(6000, true), true, g.now, g.timeouts);
    g.receive(answer(45, 7), single(6000, true));
    EXPECT_TRUE(g.outcomes.empty());
    ASSERT_EQ(g.to_execute.size(), 1U);
    EXPECT_FALSE(g.to_execute[0].object.sco.select);
    EXPECT_TRUE(g.to_execute[0].object.sco.on);
    EXPECT_EQ(g.tracker.open(), 0U); // the caller adds the execute after sending it
}

// Rule: separate time limits for confirmation and termination.
TEST(CommandTracker, Timeouts) {
    Fixture f;
    f.tracker.add(1, 45, single(6000, false), false, f.now, f.timeouts);
    f.tracker.tick(f.now + 9999, f.outcomes);
    EXPECT_TRUE(f.outcomes.empty());
    f.tracker.tick(f.now + 10000, f.outcomes);
    ASSERT_EQ(f.outcomes.size(), 1U);
    EXPECT_EQ(f.outcomes[0].result, CommandResult::NoConfirmation);

    Fixture g;
    g.timeouts.termination_ms = 3000;
    g.tracker.add(1, 45, single(6000, false), false, g.now, g.timeouts);
    g.now += 500;
    g.receive(answer(45, 7), single(6000, false));
    g.tracker.tick(g.now + 2999, g.outcomes);
    EXPECT_TRUE(g.outcomes.empty());
    g.tracker.tick(g.now + 3000, g.outcomes);
    ASSERT_EQ(g.outcomes.size(), 1U);
    EXPECT_EQ(g.outcomes[0].result, CommandResult::NoTermination);
}

// Rule: requests that are not commands (e.g. clock synchronization) end with
// their confirmation; answers are matched by IOA, also out of order.
TEST(CommandTracker, NonCommandsAndMatching) {
    Fixture f;
    f.tracker.add(1, 103, codec::InformationObject{}, false, f.now, f.timeouts);
    f.receive(answer(103, 7), codec::InformationObject{});
    ASSERT_EQ(f.outcomes.size(), 1U);
    EXPECT_EQ(f.outcomes[0].result, CommandResult::Confirmed);

    Fixture g;
    g.tracker.add(1, 45, single(1, false), false, g.now, g.timeouts);
    g.tracker.add(1, 45, single(2, false), false, g.now, g.timeouts);
    g.receive(answer(45, 7, true), single(2, false));
    ASSERT_EQ(g.outcomes.size(), 1U);
    EXPECT_EQ(g.outcomes[0].object.ioa, 2U);
    EXPECT_EQ(g.tracker.open(), 1U);
}

// Rule: a closed connection ends every open command.
TEST(CommandTracker, ConnectionLost) {
    Fixture f;
    f.tracker.add(1, 45, single(1, false), false, f.now, f.timeouts);
    f.tracker.add(1, 46, codec::InformationObject{}, false, f.now, f.timeouts);
    f.tracker.connection_lost(f.outcomes);
    ASSERT_EQ(f.outcomes.size(), 2U);
    EXPECT_EQ(f.outcomes[0].result, CommandResult::ConnectionLost);
    EXPECT_EQ(f.tracker.open(), 0U);
}
