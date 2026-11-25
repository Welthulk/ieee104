/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_session.cpp
 * @brief Application layer of the controlled station (src/session) without network.
 *
 * Requests are built with the codec, handed to a ServerSession, and the
 * answers are collected by a test sender that can simulate a full window.
 * The sequences follow docs/02_Technik_und_Quellen.md ("Übertragungsursachen")
 * and the usual reading of clause 7 (docs/05_Normfragen_und_Festlegungen.md, N8).
 */
#include "codec/asdu.hpp"
#include "session/process_image.hpp"
#include "session/server_session.hpp"

#include <array>
#include <gtest/gtest.h>
#include <vector>

using namespace iec104;
using namespace iec104::session;

namespace {

/** @brief Collects sent ASDUs; can refuse after a number of sends (full window). */
class TestSender : public AsduSender {
  public:
    std::vector<std::vector<std::uint8_t>> sent;
    std::size_t budget{1000000}; // sends allowed before the window is "full"

    bool can_send() override { return budget > 0U; }
    iec104_status send(const std::uint8_t* asdu, std::size_t length) override {
        if (budget == 0U) {
            return IEC104_ERR_WOULD_BLOCK;
        }
        --budget;
        sent.emplace_back(asdu, asdu + length);
        return IEC104_OK;
    }
};

/** @brief Records commands and answers with a fixed decision. */
class TestHandler : public CommandHandler {
  public:
    bool accept{true};
    std::vector<codec::InformationObject> commands;
    bool on_command(const codec::AsduHeader& /*header*/,
                    const codec::InformationObject& object) override {
        commands.push_back(object);
        return accept;
    }
};

/** @brief A decoded answer. */
struct Answer {
    codec::AsduHeader header;
    std::vector<codec::InformationObject> objects;
};

Answer decode(const std::vector<std::uint8_t>& asdu) {
    Answer a{};
    std::array<codec::InformationObject, codec::kMaxObjects> objects{};
    codec::DecodedAsdu d{};
    EXPECT_EQ(codec::decode_asdu(asdu.data(), asdu.size(), codec::kIec104Profile, objects.data(),
                                 objects.size(), d),
              IEC104_OK);
    a.header = d.header;
    a.objects.assign(objects.begin(), objects.begin() + static_cast<long>(d.object_count));
    return a;
}

std::vector<Answer> decode_all(const TestSender& s) {
    std::vector<Answer> result;
    for (const auto& asdu : s.sent) {
        result.push_back(decode(asdu));
    }
    return result;
}

/** @brief Encodes a one-object request. */
std::vector<std::uint8_t> request(std::uint8_t type, std::uint8_t cause, std::uint16_t ca,
                                  const codec::InformationObject& o) {
    codec::AsduHeader h{};
    h.type_id = type;
    h.cause = cause;
    h.originator = 7;
    h.common_address = ca;
    std::array<std::uint8_t, codec::kMaxAsduLength> buf{};
    std::size_t n = 0;
    EXPECT_EQ(codec::encode_asdu(h, &o, 1, codec::kIec104Profile, buf.data(), buf.size(), n),
              IEC104_OK);
    return {buf.begin(), buf.begin() + static_cast<long>(n)};
}

codec::InformationObject scaled(std::uint32_t ioa, std::int16_t v) {
    codec::InformationObject o{};
    o.ioa = ioa;
    o.sva = v;
    return o;
}

/** @brief Image of station 1: two scaled values (group 1), a single point (group 2), a counter. */
ProcessImage sample_image() {
    ProcessImage image;
    EXPECT_EQ(image.set(1, 11, scaled(100, 10), 0x0001U), IEC104_OK);
    EXPECT_EQ(image.set(1, 35, scaled(101, 20), 0x0001U), IEC104_OK); // stored as type 11
    codec::InformationObject sp{};
    sp.ioa = 300;
    sp.siq.on = true;
    EXPECT_EQ(image.set(1, 1, sp, 0x0002U), IEC104_OK);
    codec::InformationObject c{};
    c.ioa = 200;
    c.bcr.counter = 4711;
    EXPECT_EQ(image.set(1, 15, c, 0x0001U), IEC104_OK);
    return image;
}

struct Fixture {
    ProcessImage image = sample_image();
    CommandTable commands;
    TestHandler handler;
    SessionOptions options;
    // 2026-11-25 11:01:05.931 UTC on the wall clock, 1 s on the monotonic clock.
    SessionClock clock{1000, 1795604465931LL};

    StationContext station() { return StationContext{&image, &commands, &options, clock}; }
    ServerSession session{codec::kIec104Profile, 0};
    TestSender sender;

    std::vector<Answer> run(const std::vector<std::uint8_t>& asdu) {
        EXPECT_TRUE(session.handle(asdu.data(), asdu.size(), station(), handler));
        session.pump(sender);
        return decode_all(sender);
    }
};

codec::InformationObject qoi(std::uint8_t value) {
    codec::InformationObject o{};
    o.qoi = value;
    return o;
}

} // namespace

// Rule: station interrogation = ACTCON, all points except counters with
// cause 20 (one ASDU per type), ACTTERM; the request's originator is kept.
TEST(ServerSession, StationInterrogation) {
    Fixture f;
    const auto a = f.run(request(100, 6, 1, qoi(20)));
    ASSERT_EQ(a.size(), 4U);
    EXPECT_EQ(a[0].header.type_id, 100U);
    EXPECT_EQ(a[0].header.cause, 7U);
    EXPECT_FALSE(a[0].header.negative);
    EXPECT_EQ(a[0].header.originator, 7U);
    EXPECT_EQ(a[1].header.type_id, 1U); // types in ascending order
    EXPECT_EQ(a[1].header.cause, 20U);
    EXPECT_EQ(a[2].header.type_id, 11U);
    ASSERT_EQ(a[2].objects.size(), 2U); // both scaled values in one ASDU
    EXPECT_EQ(a[2].objects[1].ioa, 101U);
    EXPECT_EQ(a[3].header.cause, 10U);
}

// Rule: group interrogation n answers only the points of group n, with cause 20 + n.
TEST(ServerSession, GroupInterrogation) {
    Fixture f;
    const auto a = f.run(request(100, 6, 1, qoi(22)));
    ASSERT_EQ(a.size(), 3U);
    EXPECT_EQ(a[1].header.cause, 22U);
    ASSERT_EQ(a[1].objects.size(), 1U);
    EXPECT_EQ(a[1].objects[0].ioa, 300U);
}

// Rule: a long interrogation stays between ACTCON and ACTTERM and loses no
// point, also when the window allows only a few ASDUs per round.
TEST(ServerSession, LongInterrogationWithFullWindow) {
    Fixture f;
    for (std::uint32_t i = 0; i < 500U; ++i) {
        ASSERT_EQ(f.image.set(1, 11, scaled(1000 + i, static_cast<std::int16_t>(i)), 1), IEC104_OK);
    }
    const auto req = request(100, 6, 1, qoi(20));
    ASSERT_TRUE(f.session.handle(req.data(), req.size(), f.station(), f.handler));
    for (int round = 0; round < 1000 && f.session.queued() > 0U; ++round) {
        f.sender.budget = 3;
        f.session.pump(f.sender);
    }
    const auto a = decode_all(f.sender);
    ASSERT_GE(a.size(), 3U);
    EXPECT_EQ(a.front().header.cause, 7U);
    EXPECT_EQ(a.back().header.cause, 10U);
    std::size_t points = 0;
    for (std::size_t i = 1; i + 1 < a.size(); ++i) {
        EXPECT_EQ(a[i].header.cause, 20U);
        points += a[i].objects.size();
    }
    EXPECT_EQ(points, 503U); // 500 + 2 scaled + 1 single point
}

// Rule: counter interrogation answers the counters with cause 37 (general) or
// 37 + group; freeze/reset requests are confirmed without data.
TEST(ServerSession, CounterInterrogation) {
    Fixture f;
    codec::InformationObject read{};
    read.qcc = codec::Qcc{5, 0};
    auto a = f.run(request(101, 6, 1, read));
    ASSERT_EQ(a.size(), 3U);
    EXPECT_EQ(a[1].header.type_id, 15U);
    EXPECT_EQ(a[1].header.cause, 37U);
    EXPECT_EQ(a[1].objects[0].bcr.counter, 4711);

    Fixture g;
    codec::InformationObject freeze{};
    freeze.qcc = codec::Qcc{5, 1};
    a = g.run(request(101, 6, 1, freeze));
    ASSERT_EQ(a.size(), 2U);
    EXPECT_EQ(a[0].header.cause, 7U);
    EXPECT_EQ(a[1].header.cause, 10U);
}

// Rule: read (cause 5) answers the point with cause 5; an unknown IOA gets a
// negative answer with cause 47.
TEST(ServerSession, Read) {
    Fixture f;
    codec::InformationObject o{};
    o.ioa = 100;
    auto a = f.run(request(102, 5, 1, o));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.type_id, 11U);
    EXPECT_EQ(a[0].header.cause, 5U);
    EXPECT_EQ(a[0].objects[0].sva, 10);

    Fixture g;
    o.ioa = 999;
    a = g.run(request(102, 5, 1, o));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 47U);
    EXPECT_TRUE(a[0].header.negative);
}

// Rule: unknown type -> 44, unknown station -> 46, wrong cause -> 45, all
// negative; the broadcast address is accepted for interrogation.
TEST(ServerSession, NegativeAnswers) {
    Fixture f;
    // Type 22 is unassigned; the ASDU is answered by mirroring it.
    const std::vector<std::uint8_t> unknown{22, 0x01, 6, 0, 1, 0, 0, 0, 0, 0};
    ASSERT_TRUE(f.session.handle(unknown.data(), unknown.size(), f.station(), f.handler));
    f.session.pump(f.sender);
    // The answer has the unknown type as well, so it is checked octet by octet.
    ASSERT_EQ(f.sender.sent.size(), 1U);
    EXPECT_EQ(f.sender.sent[0][0], 22U);
    EXPECT_EQ(f.sender.sent[0][2], 44U | 0x40U); // cause 44, P/N = 1

    Fixture g;
    auto a = g.run(request(100, 6, 99, qoi(20)));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 46U);
    EXPECT_TRUE(a[0].header.negative);

    Fixture h;
    a = h.run(request(100, 3, 1, qoi(20)));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 45U);
    EXPECT_TRUE(a[0].header.negative);

    Fixture b;
    a = b.run(request(100, 6, 0xFFFF, qoi(20)));
    ASSERT_GE(a.size(), 2U);
    EXPECT_EQ(a[0].header.common_address, 1U); // answered with the station's own address
}

// Rule: accepted execute command -> positive ACTCON and ACTTERM; rejected ->
// negative ACTCON only; select -> ACTCON only; deactivation -> DEACTCON.
TEST(ServerSession, Commands) {
    codec::InformationObject sc{};
    sc.ioa = 6000;
    sc.sco.on = true;

    Fixture f;
    auto a = f.run(request(45, 6, 1, sc));
    ASSERT_EQ(a.size(), 2U);
    EXPECT_EQ(a[0].header.cause, 7U);
    EXPECT_FALSE(a[0].header.negative);
    EXPECT_EQ(a[1].header.cause, 10U);
    ASSERT_EQ(f.handler.commands.size(), 1U);
    EXPECT_TRUE(f.handler.commands[0].sco.on);

    Fixture r;
    r.handler.accept = false;
    a = r.run(request(45, 6, 1, sc));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_TRUE(a[0].header.negative);

    Fixture s;
    sc.sco.select = true;
    a = s.run(request(45, 6, 1, sc));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 7U);

    Fixture d;
    a = d.run(request(45, 8, 1, sc));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 9U);

    Fixture t;
    t.options.command_termination = false;
    sc.sco.select = false;
    a = t.run(request(45, 6, 1, sc));
    EXPECT_EQ(a.size(), 1U);
}

// Rule: clock synchronization goes to the application and is confirmed; the
// test command with time tag is mirrored with cause 7.
TEST(ServerSession, ClockSyncAndTest) {
    Fixture f;
    codec::InformationObject cs{};
    cs.cp56.hour = 11;
    cs.cp56.day_of_month = 25;
    cs.cp56.month = 11;
    cs.cp56.year = 26;
    auto a = f.run(request(103, 6, 1, cs));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 7U);
    ASSERT_EQ(f.handler.commands.size(), 1U);
    EXPECT_EQ(f.handler.commands[0].cp56.hour, 11U);

    Fixture t;
    codec::InformationObject ts{};
    ts.tsc = 0x1234;
    ts.cp56.day_of_month = 1;
    ts.cp56.month = 1;
    a = t.run(request(107, 6, 1, ts));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 7U);
    EXPECT_EQ(a[0].objects[0].tsc, 0x1234U);
}

// Rule: end of initialization (type 70, cause 4) once per connection and station.
TEST(ServerSession, EndOfInitialization) {
    Fixture f;
    f.session.on_started(f.image, f.commands, f.options);
    f.session.on_started(f.image, f.commands, f.options); // second STARTDT: nothing more
    f.session.pump(f.sender);
    const auto a = decode_all(f.sender);
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.type_id, 70U);
    EXPECT_EQ(a[0].header.cause, 4U);

    Fixture off;
    off.options.send_end_of_init = false;
    off.session.on_started(off.image, off.commands, off.options);
    EXPECT_EQ(off.session.queued(), 0U);
}

// Rule: data in monitor direction is not handled by the session but passed on.
TEST(ServerSession, MonitorDataIsPassedOn) {
    Fixture f;
    const auto asdu = request(11, 3, 1, scaled(100, 1));
    EXPECT_FALSE(f.session.handle(asdu.data(), asdu.size(), f.station(), f.handler));
}

// Rule: the image stores time-tagged types as their base type and rejects
// updates of unknown points or with a type that does not fit.
TEST(ProcessImage, BaseTypesAndUpdates) {
    EXPECT_EQ(ProcessImage::base_type(30), 1U);
    EXPECT_EQ(ProcessImage::base_type(36), 13U);
    EXPECT_EQ(ProcessImage::base_type(37), 15U);
    EXPECT_EQ(ProcessImage::base_type(45), 0U);
    ProcessImage image = sample_image();
    EXPECT_EQ(image.find(1, 101)->type, 11U);
    EXPECT_EQ(image.update(1, 35, scaled(101, 5)), IEC104_OK);
    EXPECT_EQ(image.update(1, 13, scaled(101, 5)), IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(image.update(1, 11, scaled(999, 5)), IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_TRUE(image.has_station(1));
    EXPECT_FALSE(image.has_station(2));
}

// ---------------------------------------------------------------------------
// Command points: select before operate, direct execute, maximum delay
// ---------------------------------------------------------------------------

namespace {

/** @brief Single command for IOA 6000, select or execute. */
codec::InformationObject switch_on(bool select) {
    codec::InformationObject o{};
    o.ioa = 6000;
    o.sco.on = true;
    o.sco.select = select;
    return o;
}

/** @brief Fixture with a select-execute point (IOA 6000, 2 s) and a direct point (IOA 5000). */
struct CommandFixture : Fixture {
    CommandFixture() {
        EXPECT_EQ(commands.set(1, 6000, CommandPoint{45, CommandModel::SelectExecute, 2000}),
                  IEC104_OK);
        EXPECT_EQ(commands.set(1, 5000, CommandPoint{49, CommandModel::Direct, 0}), IEC104_OK);
        EXPECT_EQ(commands.set(1, 6001, CommandPoint{58, CommandModel::Direct, 0}), IEC104_OK);
    }
    /** @brief Handles one request and returns only the answers it produced. */
    std::vector<Answer> step(const std::vector<std::uint8_t>& asdu) {
        sender.sent.clear();
        return run(asdu);
    }
};

/** @brief Time tag @p seconds before the fixture's wall clock (2026-11-25 11:01:05.931). */
codec::Cp56Time2a time_before(int seconds) {
    codec::Cp56Time2a t{};
    EXPECT_EQ(codec::from_unix_ms(1795604465931LL - (seconds * 1000LL), t), IEC104_OK);
    return t;
}

} // namespace

// Rule: on a select-execute point an execute without selection is rejected;
// select (ACTCON) followed by the same execute (ACTCON, ACTTERM) succeeds,
// and the selection is used up.
TEST(ServerCommands, SelectBeforeOperate) {
    CommandFixture f;
    auto a = f.step(request(45, 6, 1, switch_on(false)));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_TRUE(a[0].header.negative);
    EXPECT_TRUE(f.handler.commands.empty()); // the application is not asked

    a = f.step(request(45, 6, 1, switch_on(true)));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 7U);
    EXPECT_FALSE(a[0].header.negative);
    a = f.step(request(45, 6, 1, switch_on(false)));
    ASSERT_EQ(a.size(), 2U);
    EXPECT_FALSE(a[0].header.negative);
    EXPECT_EQ(a[1].header.cause, 10U);
    EXPECT_EQ(f.handler.commands.size(), 2U); // select and execute

    a = f.step(request(45, 6, 1, switch_on(false))); // selection already used
    ASSERT_EQ(a.size(), 1U);
    EXPECT_TRUE(a[0].header.negative);
}

// Rule: the execute must repeat the selected command and come in time.
TEST(ServerCommands, SelectionMismatchAndTimeout) {
    CommandFixture f;
    (void)f.step(request(45, 6, 1, switch_on(true)));
    codec::InformationObject off = switch_on(false);
    off.sco.on = false; // different state than selected
    auto a = f.step(request(45, 6, 1, off));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_TRUE(a[0].header.negative);

    (void)f.step(request(45, 6, 1, switch_on(true)));
    f.clock.monotonic_ms += 2001; // selection valid for 2 s
    a = f.step(request(45, 6, 1, switch_on(false)));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_TRUE(a[0].header.negative);
}

// Rule: deactivation (cause 8) cancels a selection with positive DEACTCON (9);
// without selection the DEACTCON is negative.
TEST(ServerCommands, Deactivation) {
    CommandFixture f;
    (void)f.step(request(45, 6, 1, switch_on(true)));
    auto a = f.step(request(45, 8, 1, switch_on(true)));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 9U);
    EXPECT_FALSE(a[0].header.negative);
    a = f.step(request(45, 6, 1, switch_on(false))); // nothing selected any more
    EXPECT_TRUE(a[0].header.negative);
    a = f.step(request(45, 8, 1, switch_on(true)));
    EXPECT_EQ(a[0].header.cause, 9U);
    EXPECT_TRUE(a[0].header.negative);
}

// Rule: a direct point rejects a select and executes directly; an unknown
// command IOA is answered with cause 47; a type other than the point's is rejected.
TEST(ServerCommands, DirectUnknownAndWrongType) {
    CommandFixture f;
    codec::InformationObject sp{};
    sp.ioa = 5000;
    sp.sva = 42;
    sp.qos.select = true;
    auto a = f.step(request(49, 6, 1, sp));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_TRUE(a[0].header.negative);
    sp.qos.select = false;
    a = f.step(request(49, 6, 1, sp));
    ASSERT_EQ(a.size(), 2U);
    EXPECT_FALSE(a[0].header.negative);

    sp.ioa = 9999;
    a = f.step(request(49, 6, 1, sp));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 47U);
    EXPECT_TRUE(a[0].header.negative);

    codec::InformationObject sc{};
    sc.ioa = 6001; // defined as 58 (with time tag), sent as 45
    sc.sco.on = true;
    a = f.step(request(45, 6, 1, sc));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 7U);
    EXPECT_TRUE(a[0].header.negative);
}

// Rule (amendment 1, N6): with a maximum delay, a time-tagged command older
// than the maximum, or with an invalid time tag, is discarded without answer;
// a command in time is executed; without maximum nothing is checked.
TEST(ServerCommands, MaximumCommandDelay) {
    CommandFixture f;
    f.options.max_command_delay_ms = 10000;
    codec::InformationObject sc{};
    sc.ioa = 6001;
    sc.sco.on = true;

    sc.cp56 = time_before(5);
    auto a = f.step(request(58, 6, 1, sc));
    EXPECT_EQ(a.size(), 2U);

    sc.cp56 = time_before(11);
    a = f.step(request(58, 6, 1, sc));
    EXPECT_TRUE(a.empty());

    sc.cp56 = time_before(0);
    sc.cp56.invalid = true;
    a = f.step(request(58, 6, 1, sc));
    EXPECT_TRUE(a.empty());

    f.options.max_command_delay_ms = 0;
    sc.cp56 = time_before(3600);
    sc.cp56.invalid = false;
    a = f.step(request(58, 6, 1, sc));
    EXPECT_EQ(a.size(), 2U);
}

// Rule: the command table refuses non-command types and select-execute for
// bitstring commands, which have no S/E bit.
TEST(ServerCommands, TableChecks) {
    CommandTable t;
    EXPECT_EQ(t.set(1, 1, CommandPoint{13, CommandModel::Direct, 0}), IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(t.set(1, 1, CommandPoint{51, CommandModel::SelectExecute, 1000}),
              IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(t.set(1, 1, CommandPoint{64, CommandModel::Direct, 0}), IEC104_OK);
    EXPECT_NE(t.find(1, 1), nullptr);
    EXPECT_EQ(t.find(2, 1), nullptr);
}

// Rule (N10): a station is known by its data points or by its command
// points; a station with command points only accepts commands, an unknown
// station is answered with cause 46.
TEST(ServerCommands, StationWithCommandPointsOnly) {
    CommandFixture f;
    ASSERT_EQ(f.commands.set(7, 100, CommandPoint{45, CommandModel::Direct, 0}), IEC104_OK);
    codec::InformationObject sc{};
    sc.ioa = 100;
    sc.sco.on = true;
    auto a = f.step(request(45, 6, 7, sc)); // station 7 has no data points
    ASSERT_EQ(a.size(), 2U);
    EXPECT_EQ(a[0].header.cause, 7U);
    EXPECT_FALSE(a[0].header.negative);
    EXPECT_EQ(a[1].header.cause, 10U);
    a = f.step(request(45, 6, 8, sc)); // station 8: neither data nor command points
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 46U);
    EXPECT_TRUE(a[0].header.negative);
}

// Rule (N10, E10): stations with command points only are stations too: they
// get the end of initialization and answer a broadcast interrogation.
TEST(ServerCommands, CommandOnlyStationInInitAndBroadcast) {
    CommandFixture f;
    ASSERT_EQ(f.commands.set(7, 100, CommandPoint{45, CommandModel::Direct, 0}), IEC104_OK);
    f.session.on_started(f.image, f.commands, f.options);
    f.session.pump(f.sender);
    auto a = decode_all(f.sender);
    ASSERT_EQ(a.size(), 2U);
    EXPECT_EQ(a[0].header.common_address, 1U);
    EXPECT_EQ(a[1].header.common_address, 7U);
    EXPECT_EQ(a[1].header.type_id, 70U);

    a = f.step(request(100, 6, 65535, qoi(20)));
    std::vector<std::uint16_t> confirmed;
    for (const Answer& x : a) {
        if (x.header.type_id == 100U && x.header.cause == 7U) {
            confirmed.push_back(x.header.common_address);
        }
    }
    EXPECT_EQ(confirmed, (std::vector<std::uint16_t>{1, 7}));
}

// Rule: with an empty process image but command points, only the stations of
// the command points are known; other addresses get cause 46.
TEST(ServerCommands, EmptyImageWithCommandPoints) {
    Fixture f;
    f.image = ProcessImage{};
    ASSERT_EQ(f.commands.set(7, 100, CommandPoint{45, CommandModel::Direct, 0}), IEC104_OK);
    auto a = f.run(request(100, 6, 7, qoi(20)));
    ASSERT_EQ(a.size(), 2U); // ACTCON, ACTTERM (no data points)
    EXPECT_FALSE(a[0].header.negative);
    f.sender.sent.clear();
    a = f.run(request(100, 6, 8, qoi(20)));
    ASSERT_EQ(a.size(), 1U);
    EXPECT_EQ(a[0].header.cause, 46U);
}

// Rule: without any points the application manages its stations: every
// address is accepted, a broadcast interrogation is answered once with the
// broadcast address.
TEST(ServerSession, BroadcastWithoutPoints) {
    Fixture f;
    f.image = ProcessImage{};
    const auto a = f.run(request(100, 6, 65535, qoi(20)));
    ASSERT_EQ(a.size(), 2U);
    EXPECT_EQ(a[0].header.cause, 7U);
    EXPECT_EQ(a[0].header.common_address, 65535U);
    EXPECT_EQ(a[1].header.cause, 10U);
}
