/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_redundancy.cpp
 * @brief Redundancy groups and event buffer: the group alone (no I/O) and
 *        server with two client handles over TCP (public C API).
 *
 * Rules: docs/05_Normfragen_und_Festlegungen.md, N7 and E10; public sources
 * in docs/02_Technik_und_Quellen.md, section "Redundanz".
 */
#include "iec104/iec104.h"
#include "session/redundancy_group.hpp"

#include <chrono>
#include <cstring>
#include <functional>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using iec104::session::BufferedEvent;
using iec104::session::GroupConfig;
using iec104::session::Overflow;
using iec104::session::RedundancyGroup;

namespace {

std::vector<std::uint8_t> event(std::uint8_t n) {
    return {13, 1, 3, 0, 1, 0, n, 0, 0};
}

std::uint64_t add(RedundancyGroup& g, std::uint8_t n) {
    const auto e = event(n);
    return g.add(e.data(), e.size()).id;
}

std::vector<std::uint8_t> marks(const std::vector<const BufferedEvent*>& events) {
    std::vector<std::uint8_t> out;
    for (const BufferedEvent* e : events) {
        out.push_back(e->asdu[6]);
    }
    return out;
}

} // namespace

// --- The group alone ---------------------------------------------------------

// N7: without an active connection events wait in the buffer; the first
// started connection gets them in order; acknowledged events leave.
TEST(RedundancyGroup, BuffersUntilAcknowledged) {
    RedundancyGroup g(GroupConfig{});
    g.join(1);
    const std::uint64_t a = add(g, 1);
    const std::uint64_t b = add(g, 2);
    add(g, 3);
    EXPECT_TRUE(g.queue_on_active().empty()); // no active connection
    EXPECT_EQ(g.start(1), 0U);
    EXPECT_EQ(marks(g.queue_on_active()), (std::vector<std::uint8_t>{1, 2, 3}));
    EXPECT_TRUE(g.queue_on_active().empty()); // all queued already
    g.sent(a);
    g.sent(b);
    EXPECT_EQ(g.stats().in_flight, 2U);
    g.acknowledged(a);
    EXPECT_EQ(g.stats().buffered, 2U);
    EXPECT_EQ(g.stats().events, 3U);
}

// N7: a stop or break makes queued and sent events pending again; the next
// active connection gets them in the original order.
TEST(RedundancyGroup, RevertsOnStopAndBreak) {
    RedundancyGroup g(GroupConfig{});
    g.join(1);
    g.join(2);
    g.start(1);
    const std::uint64_t a = add(g, 1);
    add(g, 2);
    EXPECT_EQ(marks(g.queue_on_active()), (std::vector<std::uint8_t>{1, 2}));
    g.sent(a);
    EXPECT_EQ(g.leave(1), 0U); // break of the active connection, nobody else started
    EXPECT_EQ(g.active(), 0U);
    g.acknowledged(a); // a late acknowledgement of the old connection does not count
    EXPECT_EQ(g.stats().buffered, 2U);
    EXPECT_EQ(g.stats().resent, 1U);
    g.start(2);
    EXPECT_EQ(marks(g.queue_on_active()), (std::vector<std::uint8_t>{1, 2}));
}

// N7/E10: STARTDT on a second connection takes over; stopping the active one
// hands over to another started member.
TEST(RedundancyGroup, TakeOverAndHandOver) {
    RedundancyGroup g(GroupConfig{});
    g.join(1);
    g.join(2);
    g.start(1);
    add(g, 1);
    g.queue_on_active();
    EXPECT_EQ(g.start(2), 1U); // previous active returned for withdrawal
    EXPECT_EQ(g.active(), 2U);
    EXPECT_EQ(marks(g.queue_on_active()), (std::vector<std::uint8_t>{1}));
    EXPECT_EQ(g.stop(2), 1U); // connection 1 is still started
    EXPECT_EQ(g.stats().switchovers, 2U);
    EXPECT_EQ(g.stop(1), 0U);
}

// E10: a full buffer drops the oldest event (and says where it was queued)
// or rejects the new one.
TEST(RedundancyGroup, Overflow) {
    GroupConfig c;
    c.capacity = 3;
    RedundancyGroup drop(c);
    drop.join(7);
    drop.start(7);
    const std::uint64_t first = add(drop, 1);
    drop.queue_on_active();
    add(drop, 2);
    add(drop, 3);
    const auto e = event(4);
    const RedundancyGroup::Added r = drop.add(e.data(), e.size());
    EXPECT_EQ(r.status, IEC104_OK);
    EXPECT_TRUE(r.dropped);
    EXPECT_EQ(r.dropped_id, first);
    EXPECT_EQ(r.dropped_queued_on, 7U);
    EXPECT_EQ(drop.stats().dropped, 1U);
    EXPECT_EQ(drop.stats().buffered, 3U);

    c.overflow = Overflow::Reject;
    RedundancyGroup reject(c);
    add(reject, 1);
    add(reject, 2);
    add(reject, 3);
    EXPECT_EQ(reject.add(e.data(), e.size()).status, IEC104_ERR_WOULD_BLOCK);
    EXPECT_EQ(reject.stats().rejected, 1U);
    EXPECT_EQ(reject.stats().buffered, 3U);
}

TEST(RedundancyGroup, Addresses) {
    GroupConfig c;
    EXPECT_TRUE(RedundancyGroup(c).accepts("192.0.2.1")); // empty list: any
    c.peers = {"192.0.2.1", "2001:db8::1"};
    const RedundancyGroup g(c);
    EXPECT_TRUE(g.accepts("192.0.2.1"));
    EXPECT_TRUE(g.accepts("2001:db8::1"));
    EXPECT_FALSE(g.accepts("192.0.2.2"));
}

// --- Server with two client handles over TCP --------------------------------

namespace {

struct ClientSeen {
    iec104_link_state state{IEC104_LINK_CONNECTING};
    iec104_close_reason reason{IEC104_CLOSE_NONE};
    std::vector<int> values; // spontaneous values of IOA 100, in order of arrival

    static void on_state(void* user, uint32_t, iec104_link_state s, iec104_close_reason r) {
        auto* c = static_cast<ClientSeen*>(user);
        c->state = s;
        c->reason = r;
    }
    static void on_object(void* user, uint32_t, const iec104_asdu_info* info,
                          const iec104_object* o) {
        if (info->cause == IEC104_COT_SPONTANEOUS && o->ioa == 100U) {
            static_cast<ClientSeen*>(user)->values.push_back(static_cast<int>(o->value));
        }
    }
};

struct Log {
    std::vector<std::string> lines;
    static void fn(void* user, iec104_log_level, uint32_t, const char* message) {
        static_cast<Log*>(user)->lines.emplace_back(message);
    }
    [[nodiscard]] bool contains(const std::string& part) const {
        for (const auto& l : lines) {
            if (l.find(part) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

class Redundancy : public testing::Test {
  protected:
    void TearDown() override {
        for (iec104_client*& c : clients_) {
            iec104_client_destroy(c);
        }
        iec104_server_destroy(server_);
    }

    void create_server() {
        ASSERT_EQ(iec104_server_create(nullptr, nullptr, &server_), IEC104_OK);
        iec104_server_set_log(server_, IEC104_LOG_INFO, Log::fn, &log_);
        iec104_object p{};
        p.type = 13;
        p.ioa = 100;
        ASSERT_EQ(iec104_server_add_point(server_, 1, &p, 1), IEC104_OK);
    }
    void listen() { ASSERT_EQ(iec104_server_listen(server_, "127.0.0.1", 0), IEC104_OK); }

    /** @brief Connects client @p i (0 or 1) and waits for STOPPED. */
    void connect(int i, uint32_t t2_ms = 0) {
        iec104_link_config link{};
        iec104_link_config_init(&link);
        if (t2_ms != 0U) {
            link.t2_ms = t2_ms; // acknowledge late: events stay unacknowledged
        }
        iec104_callbacks cb{};
        cb.user = &seen_[i];
        cb.on_state = ClientSeen::on_state;
        cb.on_object = ClientSeen::on_object;
        ASSERT_EQ(iec104_client_create(&link, &cb, &clients_[i]), IEC104_OK);
        ASSERT_EQ(iec104_client_connect(clients_[i], "127.0.0.1", iec104_server_port(server_)),
                  IEC104_OK);
        ASSERT_TRUE(run_until([&] { return seen_[i].state == IEC104_LINK_STOPPED; }));
    }
    void start(int i) {
        ASSERT_EQ(iec104_client_start(clients_[i]), IEC104_OK);
        ASSERT_TRUE(run_until([&] { return seen_[i].state == IEC104_LINK_STARTED; }));
    }
    void stop(int i) {
        ASSERT_EQ(iec104_client_stop(clients_[i]), IEC104_OK);
        ASSERT_TRUE(run_until([&] { return seen_[i].state == IEC104_LINK_STOPPED; }));
    }
    /** @brief Spontaneous values @p from .. @p to (inclusive) at IOA 100. */
    void update(int from, int to, iec104_status expected = IEC104_OK) {
        for (int v = from; v <= to; ++v) {
            iec104_object p{};
            p.type = 13;
            p.ioa = 100;
            p.value = v;
            EXPECT_EQ(iec104_server_update(server_, 1, &p), expected) << v;
        }
    }
    bool run_until(const std::function<bool()>& done, int ms = 5000) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!done() && std::chrono::steady_clock::now() < end) {
            (void)iec104_server_poll(server_, 1);
            for (iec104_client* c : clients_) {
                if (c != nullptr) {
                    (void)iec104_client_poll(c, 0);
                }
            }
        }
        return done();
    }
    void settle(int ms = 200) {
        run_until([] { return false; }, ms);
    }
    iec104_group_stats group_stats(uint32_t group) {
        iec104_group_stats s{};
        EXPECT_EQ(iec104_server_get_group_stats(server_, group, &s), IEC104_OK);
        return s;
    }
    uint32_t add_group(const std::vector<std::string>& peers, uint32_t size = 1000,
                       iec104_overflow overflow = IEC104_OVERFLOW_DROP_OLDEST) {
        iec104_group_config g{};
        iec104_group_config_init(&g);
        for (std::size_t i = 0; i < peers.size(); ++i) {
            std::snprintf(g.peers[i], sizeof g.peers[i], "%s", peers[i].c_str());
        }
        g.buffer_size = size;
        g.overflow = overflow;
        uint32_t id = 0;
        EXPECT_EQ(iec104_server_add_group(server_, &g, &id), IEC104_OK);
        return id;
    }

    iec104_server* server_{nullptr};
    iec104_client* clients_[2]{nullptr, nullptr};
    ClientSeen seen_[2];
    Log log_;
};

std::vector<int> range(int from, int to) {
    std::vector<int> v;
    for (int i = from; i <= to; ++i) {
        v.push_back(i);
    }
    return v;
}

} // namespace

// E10: without groups every connection buffers its events while it is stopped.
TEST_F(Redundancy, OwnGroupBuffersWhileStopped) {
    create_server();
    listen();
    connect(0);
    update(1, 3);
    settle();
    EXPECT_TRUE(seen_[0].values.empty());
    start(0);
    ASSERT_TRUE(run_until([&] { return seen_[0].values.size() == 3U; }));
    EXPECT_EQ(seen_[0].values, range(1, 3));
    update(4, 5);
    ASSERT_TRUE(run_until([&] { return seen_[0].values.size() == 5U; }));
}

// N7: switch-over with STOPDT on the old and STARTDT on the new connection;
// events in between wait in the group's buffer; nothing is lost or reordered.
TEST_F(Redundancy, SwitchOverWithStopAndStart) {
    create_server();
    const uint32_t g = add_group({"127.0.0.1"});
    listen();
    connect(0);
    connect(1);
    EXPECT_EQ(iec104_server_connection_group(server_, 1), g);
    EXPECT_EQ(iec104_server_connection_group(server_, 2), g);
    start(0);
    update(1, 5);
    ASSERT_TRUE(run_until([&] { return seen_[0].values.size() == 5U; }));
    stop(0); // all acknowledged before STOPDT con
    update(6, 8);
    settle();
    EXPECT_EQ(group_stats(g).buffered, 3U);
    EXPECT_EQ(group_stats(g).active_connection, 0U);
    start(1);
    ASSERT_TRUE(run_until([&] { return seen_[1].values.size() == 3U; }));
    EXPECT_EQ(seen_[1].values, range(6, 8));
    EXPECT_EQ(seen_[0].values, range(1, 5));
    update(9, 9);
    ASSERT_TRUE(run_until([&] { return seen_[1].values.size() == 4U; }));
    settle();
    EXPECT_EQ(seen_[0].values.size(), 5U); // the standby connection gets nothing
    const iec104_group_stats s = group_stats(g);
    EXPECT_EQ(s.active_connection, 2U);
    EXPECT_EQ(s.connections, 2U);
    EXPECT_EQ(s.events, 9U);
    EXPECT_EQ(s.resent, 0U);
}

// N7: events sent but not acknowledged when the active connection breaks are
// sent again on the next connection.
TEST_F(Redundancy, BreakResendsUnacknowledged) {
    create_server();
    const uint32_t g = add_group({});
    listen();
    connect(0, 60000); // acknowledges only after 8 frames or 60 s
    connect(1);
    start(0);
    update(1, 3);
    ASSERT_TRUE(run_until([&] { return seen_[0].values.size() == 3U; }));
    EXPECT_EQ(group_stats(g).in_flight, 3U);
    iec104_client_destroy(clients_[0]); // break without acknowledgement
    clients_[0] = nullptr;
    ASSERT_TRUE(run_until([&] { return group_stats(g).connections == 1U; }));
    start(1);
    ASSERT_TRUE(run_until([&] { return seen_[1].values.size() == 3U; }));
    EXPECT_EQ(seen_[1].values, range(1, 3));
    EXPECT_EQ(group_stats(g).resent, 3U);
    settle(300);
}

// E10: STARTDT on a second connection takes over without STOPDT on the old one.
TEST_F(Redundancy, StartOnSecondConnectionTakesOver) {
    create_server();
    const uint32_t g = add_group({});
    listen();
    connect(0);
    connect(1);
    start(0);
    update(1, 2);
    ASSERT_TRUE(run_until([&] { return seen_[0].values.size() == 2U; }));
    start(1);
    update(3, 4);
    ASSERT_TRUE(run_until([&] { return seen_[1].values.size() >= 2U; }));
    settle();
    EXPECT_EQ(seen_[0].values, range(1, 2));
    EXPECT_EQ(seen_[1].values.back(), 4);
    EXPECT_EQ(group_stats(g).active_connection, 2U);
    EXPECT_EQ(group_stats(g).switchovers, 1U);
    EXPECT_TRUE(log_.contains("takes over from connection 1"));
    // The old one stops: the active connection stays.
    stop(0);
    EXPECT_EQ(group_stats(g).active_connection, 2U);
}

// E10: full buffer: drop the oldest (default) or reject new events.
TEST_F(Redundancy, BufferOverflow) {
    create_server();
    const uint32_t drop = add_group({"127.0.0.1"}, 5);
    const uint32_t reject = add_group({"192.0.2.1"}, 5, IEC104_OVERFLOW_REJECT);
    listen();
    connect(0, 1000);                     // t2 = 1 s: acknowledges soon
    update(1, 5);                         // both groups full
    update(6, 8, IEC104_ERR_WOULD_BLOCK); // rejected by the second group
    EXPECT_EQ(group_stats(drop).dropped, 3U);
    EXPECT_EQ(group_stats(reject).rejected, 3U);
    EXPECT_TRUE(log_.contains("event buffer full"));
    start(0);
    ASSERT_TRUE(run_until([&] { return seen_[0].values.size() == 5U; }));
    EXPECT_EQ(seen_[0].values, range(4, 8));                                  // the newest five
    ASSERT_TRUE(run_until([&] { return group_stats(drop).buffered == 0U; })); // acknowledged
}

// E10: a connection joins the group that lists its address, else the group
// open to any address, else it is rejected.
TEST_F(Redundancy, AssignmentByAddress) {
    create_server();
    const uint32_t any = add_group({});
    const uint32_t own = add_group({"192.0.2.1", "127.0.0.1"});
    listen();
    connect(0);
    EXPECT_EQ(iec104_server_connection_group(server_, 1), own);
    EXPECT_NE(any, own);
    iec104_server_destroy(server_);
    server_ = nullptr;
    iec104_client_destroy(clients_[0]);
    clients_[0] = nullptr;
    seen_[0] = ClientSeen{};

    create_server();
    add_group({"192.0.2.1"});
    listen();
    iec104_callbacks cb{};
    cb.user = &seen_[0];
    cb.on_state = ClientSeen::on_state;
    ASSERT_EQ(iec104_client_create(nullptr, &cb, &clients_[0]), IEC104_OK);
    ASSERT_EQ(iec104_client_connect(clients_[0], "127.0.0.1", iec104_server_port(server_)),
              IEC104_OK);
    ASSERT_TRUE(run_until([&] { return seen_[0].state == IEC104_LINK_CLOSED; }));
    EXPECT_EQ(iec104_server_connection_count(server_), 0U);
    EXPECT_TRUE(log_.contains("not in any redundancy group"));
}

// E10: cyclic data is not buffered; it only goes to active connections.
TEST_F(Redundancy, CyclicDataNotBuffered) {
    create_server();
    listen();
    connect(0);
    iec104_object p{};
    p.type = 13;
    p.ioa = 100;
    EXPECT_EQ(iec104_server_send(server_, 0, 1, IEC104_COT_PERIODIC, &p, 1),
              IEC104_ERR_WRONG_STATE);
    start(0);
    EXPECT_EQ(iec104_server_send(server_, 0, 1, IEC104_COT_PERIODIC, &p, 1), IEC104_OK);
    EXPECT_EQ(group_stats(iec104_server_connection_group(server_, 1)).events, 0U);
}

TEST_F(Redundancy, SettingsErrors) {
    create_server();
    iec104_group_config g{};
    iec104_group_config_init(&g);
    EXPECT_EQ(g.buffer_size, 1000U);
    g.buffer_size = 0;
    EXPECT_EQ(iec104_server_add_group(server_, &g, nullptr), IEC104_ERR_OUT_OF_RANGE);
    iec104_group_config_init(&g);
    std::memset(g.peers[0], 'x', sizeof g.peers[0]); // no NUL
    EXPECT_EQ(iec104_server_add_group(server_, &g, nullptr), IEC104_ERR_INVALID_ARGUMENT);
    iec104_server_options o{};
    iec104_server_options_init(&o);
    o.event_buffer_size = IEC104_MAX_EVENT_BUFFER + 1U;
    EXPECT_EQ(iec104_server_set_options(server_, &o), IEC104_ERR_OUT_OF_RANGE);
    listen();
    iec104_group_config_init(&g);
    EXPECT_EQ(iec104_server_add_group(server_, &g, nullptr), IEC104_ERR_WRONG_STATE);
    iec104_group_stats s{};
    EXPECT_EQ(iec104_server_get_group_stats(server_, 99, &s), IEC104_ERR_WRONG_STATE);
}
