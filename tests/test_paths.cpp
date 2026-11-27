/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_paths.cpp
 * @brief Client with several paths over TCP (public C API only).
 *
 * Two or three servers on 127.0.0.1 carry the same process (same station,
 * same points); one client holds a path to each. The rules are those of
 * docs/05_Normfragen_und_Festlegungen.md, E11 and E12.
 */
#include "iec104/iec104.h"

#include <cstdio>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr uint16_t kStation = 1;
constexpr uint32_t kIoa = 100;

// What the application of the client sees.
struct App {
    std::vector<iec104_link_state> states; // on_state of connection 1
    std::vector<uint32_t> connections;     // id of every on_object
    std::vector<double> values;            // spontaneous and interrogated values of kIoa
    std::vector<iec104_command_result> results;
    std::vector<iec104_path_switch> switches;
    std::vector<std::string> reasons;
    std::vector<std::string> log;

    static void on_state(void* user, uint32_t /*connection*/, iec104_link_state state,
                         iec104_close_reason /*reason*/) {
        static_cast<App*>(user)->states.push_back(state);
    }
    static void on_object(void* user, uint32_t connection, const iec104_asdu_info* info,
                          const iec104_object* object) {
        auto* app = static_cast<App*>(user);
        app->connections.push_back(connection);
        if (object->ioa == kIoa && (info->cause == IEC104_COT_SPONTANEOUS ||
                                    info->cause == IEC104_COT_INTERROGATED_STATION)) {
            app->values.push_back(object->value);
        }
    }
    static void on_result(void* user, uint16_t /*ca*/, const iec104_object* /*command*/,
                          iec104_command_result result) {
        static_cast<App*>(user)->results.push_back(result);
    }
    static void on_switch(void* user, const iec104_path_switch* e) {
        auto* app = static_cast<App*>(user);
        app->switches.push_back(*e);
        app->reasons.emplace_back(e->reason);
    }
    static void on_log(void* user, iec104_log_level level, uint32_t connection,
                       const char* message) {
        static_cast<App*>(user)->log.push_back(std::string(iec104_log_level_string(level)) + " " +
                                               std::to_string(connection) + " " + message);
    }
    [[nodiscard]] bool logged(const std::string& part) const {
        for (const std::string& l : log) {
            if (l.find(part) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

// A station: server with one measured value and one command point.
struct Station {
    iec104_server* server{nullptr};
    int commands{0};
    int clock_syncs{0};

    static int on_command(void* user, uint32_t /*connection*/, const iec104_asdu_info* /*info*/,
                          const iec104_object* command) {
        auto* s = static_cast<Station*>(user);
        if (command->type == 103) {
            ++s->clock_syncs;
        } else {
            ++s->commands;
        }
        return 1;
    }

    Station() {
        iec104_callbacks cb{};
        cb.user = this;
        cb.on_command = on_command;
        EXPECT_EQ(iec104_server_create(nullptr, &cb, &server), IEC104_OK);
        iec104_object point{};
        point.type = 13; // M_ME_NC_1
        point.ioa = kIoa;
        EXPECT_EQ(iec104_server_add_point(server, kStation, &point, 1), IEC104_OK);
        EXPECT_EQ(iec104_server_add_command(server, kStation, 200, 45, IEC104_COMMAND_DIRECT, 0),
                  IEC104_OK);
        EXPECT_EQ(iec104_server_listen(server, "127.0.0.1", 0), IEC104_OK);
    }
    Station(const Station&) = delete;
    Station& operator=(const Station&) = delete;
    Station(Station&&) = delete;
    Station& operator=(Station&&) = delete;
    ~Station() { iec104_server_destroy(server); }

    void update(double value) const {
        iec104_object o{};
        o.type = 13;
        o.ioa = kIoa;
        o.value = value;
        EXPECT_EQ(iec104_server_update(server, kStation, &o), IEC104_OK);
    }
    [[nodiscard]] uint64_t requests() const {
        iec104_link_stats s{};
        uint64_t n = 0;
        for (uint32_t id = 1; id < 20; ++id) {
            if (iec104_server_get_stats(server, id, &s) == IEC104_OK) {
                n += s.requests;
            }
        }
        return n;
    }
};

// Client with one path per station.
struct Fixture {
    std::vector<std::unique_ptr<Station>> stations;
    App app;
    iec104_client* client{nullptr};

    explicit Fixture(std::size_t count, const iec104_link_config* link = nullptr,
                     const iec104_path_options* options = nullptr) {
        for (std::size_t i = 0; i < count; ++i) {
            stations.push_back(std::make_unique<Station>());
        }
        iec104_callbacks cb{};
        cb.user = &app;
        cb.on_state = App::on_state;
        cb.on_object = App::on_object;
        cb.on_command_result = App::on_result;
        EXPECT_EQ(iec104_client_create(link, &cb, &client), IEC104_OK);
        iec104_client_set_log(client, IEC104_LOG_DEBUG, App::on_log, &app);
        iec104_client_set_path_callback(client, App::on_switch, &app);
        std::vector<iec104_path_config> paths(count);
        for (std::size_t i = 0; i < count; ++i) {
            std::snprintf(paths[i].host, sizeof paths[i].host, "127.0.0.1");
            paths[i].port = iec104_server_port(stations[i]->server);
        }
        iec104_path_options o{};
        iec104_path_options_init(&o);
        o.reconnect_interval_ms = 200;
        EXPECT_EQ(iec104_client_connect_paths(client, paths.data(), count,
                                              options != nullptr ? options : &o),
                  IEC104_OK);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;
    ~Fixture() { iec104_client_destroy(client); }

    bool run_until(const std::function<bool()>& done, int rounds = 5000) {
        for (int i = 0; i < rounds && !done(); ++i) {
            for (const auto& s : stations) {
                (void)iec104_server_poll(s->server, 0);
            }
            (void)iec104_client_poll(client, 1);
        }
        return done();
    }
    void run_for(int rounds) {
        (void)run_until([] { return false; }, rounds);
    }
    [[nodiscard]] iec104_path_info info(uint32_t path) const {
        iec104_path_info i{};
        EXPECT_EQ(iec104_client_get_path_info(client, path, &i), IEC104_OK);
        return i;
    }
    // All paths started, one of them leading.
    bool all_started() {
        return run_until([&] {
            for (uint32_t p = 1; p <= stations.size(); ++p) {
                if (info(p).state != IEC104_LINK_STARTED) {
                    return false;
                }
            }
            return iec104_client_leading_path(client) != 0U;
        });
    }
    void start() {
        ASSERT_EQ(iec104_client_start(client), IEC104_OK);
        ASSERT_TRUE(all_started());
    }
};

} // namespace

// Rule (E11): all paths are started; the first started one leads, the others
// follow. The application sees one logical connection (id 1) and every
// object once, although every station sends it.
TEST(Paths, OnePathDeliveredOthersFollow) {
    Fixture f(2);
    f.start();
    EXPECT_EQ(iec104_client_state(f.client), IEC104_LINK_STARTED);
    ASSERT_FALSE(f.app.states.empty());
    EXPECT_EQ(f.app.states.back(), IEC104_LINK_STARTED);
    const uint32_t leading = iec104_client_leading_path(f.client);
    const uint32_t other = 3U - leading;
    EXPECT_EQ(f.info(leading).leading, 1);
    EXPECT_EQ(f.info(other).leading, 0);
    EXPECT_TRUE(f.app.logged("path " + std::to_string(leading) + " leading: first started path"));
    EXPECT_TRUE(f.app.logged("path " + std::to_string(other) + " started, following"));
    ASSERT_EQ(f.app.switches.size(), 1U);
    EXPECT_EQ(f.app.switches[0].leading, leading);
    EXPECT_EQ(f.app.switches[0].previous, 0U);

    f.app.values.clear();
    f.app.connections.clear();
    for (const auto& s : f.stations) {
        s->update(1.5);
    }
    ASSERT_TRUE(f.run_until([&] { return f.info(other).followed >= 1U; }));
    f.run_for(50);
    EXPECT_EQ(f.app.values, std::vector<double>{1.5});
    for (const uint32_t id : f.app.connections) {
        EXPECT_EQ(id, 1U);
    }
}

// Rule (E11, R3, R8): when the leading path is closed, the other one takes
// over at once; the logical state stays STARTED; the switch is logged with
// its reason and reported to the path callback; a general interrogation runs
// on the new path; the lost path is connected again and follows.
TEST(Paths, SwitchOverOnClose) {
    Fixture f(2);
    f.start();
    const uint32_t leading = iec104_client_leading_path(f.client);
    const uint32_t other = 3U - leading;
    const uint64_t requests_before = f.stations[other - 1U]->requests();
    const std::size_t states_before = f.app.states.size();

    ASSERT_EQ(iec104_client_disturb_path(f.client, leading, IEC104_PATH_CLOSE), IEC104_OK);
    ASSERT_TRUE(f.run_until([&] { return iec104_client_leading_path(f.client) == other; }));
    EXPECT_EQ(f.app.states.size(), states_before); // still STARTED: nothing reported
    EXPECT_EQ(iec104_client_state(f.client), IEC104_LINK_STARTED);
    ASSERT_EQ(f.app.switches.size(), 2U);
    EXPECT_EQ(f.app.switches[1].leading, other);
    EXPECT_EQ(f.app.switches[1].previous, leading);
    // The end of initialization both stations sent is known already: suppressed.
    EXPECT_EQ(f.app.reasons[1], "path " + std::to_string(other) + " leading: path " +
                                    std::to_string(leading) + " closed: closed locally" +
                                    "; replayed 0, suppressed 1, dropped 0");
    EXPECT_TRUE(f.app.logged("warning " + std::to_string(other) + " path " + std::to_string(other) +
                             " leading: path " + std::to_string(leading) + " closed"));
    // General interrogation on the new path.
    ASSERT_TRUE(f.run_until([&] { return f.stations[other - 1U]->requests() > requests_before; }));
    // The lost path comes back and follows.
    ASSERT_TRUE(f.run_until([&] { return f.info(leading).state == IEC104_LINK_STARTED; }));
    EXPECT_EQ(iec104_client_leading_path(f.client), other); // no switch back
    EXPECT_GE(f.info(leading).connects, 2U);
    EXPECT_EQ(f.info(leading).last_close, IEC104_CLOSE_LOCAL);
}

// Rule (E11, replay): a silent leading path is detected only after t3 + t1.
// What the following path received meanwhile is delivered afterwards, so
// that the application misses no value; values delivered before the failure
// are not delivered again.
TEST(Paths, SilentPathReplay) {
    iec104_link_config link{};
    iec104_link_config_init(&link);
    link.t1_ms = 2000;
    link.t2_ms = 1000;
    link.t3_ms = 1000;
    Fixture f(2, &link);
    f.start();
    const uint32_t leading = iec104_client_leading_path(f.client);
    const uint32_t other = 3U - leading;

    // Delivered normally.
    for (const auto& s : f.stations) {
        s->update(1.0);
    }
    ASSERT_TRUE(f.run_until([&] { return f.info(other).followed >= 1U && !f.app.values.empty(); }));
    f.app.values.clear();

    // The leading path goes silent; three new values reach only the follower.
    ASSERT_EQ(iec104_client_disturb_path(f.client, leading, IEC104_PATH_SILENT), IEC104_OK);
    EXPECT_EQ(f.info(leading).silent, 1);
    for (const double v : {2.0, 3.0, 4.0}) {
        for (const auto& s : f.stations) {
            s->update(v);
        }
        f.run_for(20);
    }
    EXPECT_TRUE(f.app.values.empty()); // nothing delivered while the leading path is silent

    ASSERT_TRUE(f.run_until([&] { return iec104_client_leading_path(f.client) == other; }));
    EXPECT_EQ(f.info(leading).last_close, IEC104_CLOSE_T1_TIMEOUT);
    ASSERT_EQ(f.app.switches.size(), 2U);
    EXPECT_EQ(f.app.switches[1].replayed, 3U);   // 2, 3, 4
    EXPECT_EQ(f.app.switches[1].suppressed, 2U); // end of initialization and 1: delivered already
    ASSERT_GE(f.app.values.size(), 3U);
    EXPECT_EQ(f.app.values[0], 2.0);
    EXPECT_EQ(f.app.values[1], 3.0);
    EXPECT_EQ(f.app.values[2], 4.0);
    EXPECT_EQ(f.info(other).replayed, 3U);
}

// Rule (R4, D6, E12): without a started path the logical state is CONNECTING,
// an open command ends with CONNECTION_LOST, and the client keeps trying;
// the first path back leads again.
TEST(Paths, AllLostAndBack) {
    Fixture f(2);
    f.start();
    iec104_object cmd{};
    cmd.type = 45;
    cmd.ioa = 200;
    cmd.value = 1;
    iec104_client_options o{};
    iec104_client_options_init(&o);
    o.expect_termination = 1;
    o.termination_timeout_ms = 60000;
    ASSERT_EQ(iec104_client_set_options(f.client, &o), IEC104_OK);

    // Close both paths in one go, before the command is answered.
    ASSERT_EQ(iec104_client_send_command(f.client, kStation, &cmd), IEC104_OK);
    ASSERT_EQ(iec104_client_disturb_path(f.client, 1, IEC104_PATH_CLOSE), IEC104_OK);
    ASSERT_EQ(iec104_client_disturb_path(f.client, 2, IEC104_PATH_CLOSE), IEC104_OK);
    ASSERT_TRUE(f.run_until([&] { return iec104_client_leading_path(f.client) == 0U; }));
    EXPECT_EQ(iec104_client_state(f.client), IEC104_LINK_CONNECTING);
    EXPECT_EQ(f.app.states.back(), IEC104_LINK_CONNECTING);
    EXPECT_TRUE(f.app.logged("all paths lost"));
    EXPECT_EQ(f.app.switches.back().leading, 0U);
    ASSERT_EQ(f.app.results.size(), 1U);
    EXPECT_EQ(f.app.results[0], IEC104_COMMAND_CONNECTION_LOST);
    EXPECT_EQ(iec104_client_send_command(f.client, kStation, &cmd), IEC104_ERR_WRONG_STATE);

    ASSERT_TRUE(f.all_started());
    EXPECT_EQ(f.app.states.back(), IEC104_LINK_STARTED);
    EXPECT_EQ(iec104_client_state(f.client), IEC104_LINK_STARTED);
}

// Rule (E12, R7): commands go over the leading path only; the clock goes to
// every started path.
TEST(Paths, CommandsOverLeadingPathClockToAll) {
    Fixture f(2);
    f.start();
    const uint32_t leading = iec104_client_leading_path(f.client);
    iec104_object cmd{};
    cmd.type = 45;
    cmd.ioa = 200;
    cmd.value = 1;
    ASSERT_EQ(iec104_client_send_command(f.client, kStation, &cmd), IEC104_OK);
    iec104_time t{};
    t.year = 2026;
    t.month = 11;
    t.day = 27;
    ASSERT_EQ(iec104_client_clock_sync(f.client, kStation, &t), IEC104_OK);
    ASSERT_TRUE(f.run_until([&] {
        return f.app.results.size() == 1U && f.stations[0]->clock_syncs == 1 &&
               f.stations[1]->clock_syncs == 1;
    }));
    EXPECT_EQ(f.app.results[0], IEC104_COMMAND_TERMINATED);
    EXPECT_EQ(f.stations[leading - 1U]->commands, 1);
    EXPECT_EQ(f.stations[2U - leading]->commands, 0);
}

// Rule (D7): three paths; the next started path after the lost one takes over.
TEST(Paths, ThreePaths) {
    Fixture f(3);
    f.start();
    const uint32_t first = iec104_client_leading_path(f.client);
    ASSERT_EQ(iec104_client_disturb_path(f.client, first, IEC104_PATH_CLOSE), IEC104_OK);
    const uint32_t expected = first % 3U + 1U;
    ASSERT_TRUE(f.run_until([&] {
        const uint32_t l = iec104_client_leading_path(f.client);
        return l != 0U && l != first;
    }));
    EXPECT_EQ(iec104_client_leading_path(f.client), expected);
}

// Rule (R5, option): with prefer_first_path, path 1 leads again when it is back.
TEST(Paths, PreferFirstPath) {
    iec104_path_options o{};
    iec104_path_options_init(&o);
    o.reconnect_interval_ms = 200;
    o.prefer_first_path = 1;
    Fixture f(2, nullptr, &o);
    f.start();
    ASSERT_TRUE(f.run_until([&] { return iec104_client_leading_path(f.client) == 1U; }));
    ASSERT_EQ(iec104_client_disturb_path(f.client, 1, IEC104_PATH_CLOSE), IEC104_OK);
    ASSERT_TRUE(f.run_until([&] { return iec104_client_leading_path(f.client) == 2U; }));
    ASSERT_TRUE(f.run_until([&] { return iec104_client_leading_path(f.client) == 1U; }));
    EXPECT_TRUE(f.app.logged("preferred path 1 is back"));
}

// Rule (D6): only iec104_client_close() ends a client with paths; CLOSED is
// reported once; afterwards the client can connect again.
TEST(Paths, CloseEndsAllPaths) {
    Fixture f(2);
    f.start();
    iec104_client_close(f.client);
    EXPECT_EQ(iec104_client_state(f.client), IEC104_LINK_CLOSED);
    ASSERT_TRUE(f.run_until([&] { return f.app.states.back() == IEC104_LINK_CLOSED; }));
    f.run_for(400); // longer than the reconnect interval: nothing reconnects
    EXPECT_EQ(iec104_client_state(f.client), IEC104_LINK_CLOSED);
    long closed = 0;
    for (const iec104_link_state s : f.app.states) {
        closed += s == IEC104_LINK_CLOSED ? 1 : 0;
    }
    EXPECT_EQ(closed, 1);
    EXPECT_EQ(iec104_client_leading_path(f.client), 0U);
    iec104_path_info i{};
    EXPECT_EQ(iec104_client_get_path_info(f.client, 1, &i), IEC104_ERR_WRONG_STATE);
    // One connection again, as before.
    ASSERT_EQ(
        iec104_client_connect(f.client, "127.0.0.1", iec104_server_port(f.stations[0]->server)),
        IEC104_OK);
    ASSERT_TRUE(f.run_until([&] { return iec104_client_state(f.client) == IEC104_LINK_STOPPED; }));
}

// Rule: arguments are checked; a client has either one connection or paths.
TEST(Paths, Arguments) {
    iec104_client* c = nullptr;
    ASSERT_EQ(iec104_client_create(nullptr, nullptr, &c), IEC104_OK);
    iec104_path_config p[IEC104_MAX_PATHS + 1]{};
    EXPECT_EQ(iec104_client_connect_paths(c, p, 1, nullptr), IEC104_ERR_INVALID_ARGUMENT); // ""
    std::snprintf(p[0].host, sizeof p[0].host, "127.0.0.1");
    p[0].port = 1;
    EXPECT_EQ(iec104_client_connect_paths(c, p, 0, nullptr), IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(iec104_client_connect_paths(c, p, IEC104_MAX_PATHS + 1, nullptr),
              IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(iec104_client_connect_paths(nullptr, p, 1, nullptr), IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(iec104_client_disturb_path(c, 1, IEC104_PATH_CLOSE), IEC104_ERR_WRONG_STATE);
    EXPECT_EQ(iec104_client_leading_path(c), 0U);

    iec104_path_options o{};
    iec104_path_options_init(&o);
    EXPECT_EQ(o.reconnect_interval_ms, 5000U);
    EXPECT_EQ(o.replay_window_ms, 0U);
    EXPECT_EQ(o.replay_max_objects, 10000U);
    EXPECT_EQ(o.replay_tolerance_ms, 500U);
    EXPECT_EQ(o.interrogation_after_switch, 1);
    EXPECT_EQ(o.prefer_first_path, 0);

    ASSERT_EQ(iec104_client_connect_paths(c, p, 1, nullptr), IEC104_OK); // refused port: retried
    EXPECT_EQ(iec104_client_connect(c, "127.0.0.1", 1), IEC104_ERR_WRONG_STATE);
    EXPECT_EQ(iec104_client_connect_paths(c, p, 1, nullptr), IEC104_ERR_WRONG_STATE);
    iec104_path_info i{};
    EXPECT_EQ(iec104_client_get_path_info(c, 2, &i), IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(iec104_client_disturb_path(c, 2, IEC104_PATH_CLOSE), IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(iec104_client_disturb_path(c, 1, static_cast<iec104_path_fault>(9)),
              IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(iec104_client_get_path_info(c, 1, &i), IEC104_OK);
    EXPECT_STREQ(i.host, "127.0.0.1");
    EXPECT_EQ(i.leading, 0);
    iec104_client_destroy(c);
}

// Rule: the configuration file sets paths and their options; "path" may be
// repeated up to IEC104_MAX_PATHS times; IPv6 addresses stand in brackets.
TEST(Paths, ConfigFile) {
    const std::string path = testing::TempDir() + "paths.conf";
    {
        std::FILE* f = std::fopen(path.c_str(), "w");
        ASSERT_NE(f, nullptr);
        std::fputs("path = 192.0.2.10:2404\n"
                   "path = [2001:db8::1]:2405 station-b.example AB:CD\n"
                   "path_reconnect = 2.5\n"
                   "path_replay_window = 60\n"
                   "path_replay_tolerance = 0.25\n"
                   "path_replay_max = 500\n"
                   "path_interrogation = no\n"
                   "path_prefer_first = yes\n",
                   f);
        std::fclose(f);
    }
    iec104_config c{};
    iec104_config_init(&c);
    EXPECT_EQ(c.path_count, 0U);
    EXPECT_EQ(c.path_options.reconnect_interval_ms, 5000U);
    int line = -1;
    ASSERT_EQ(iec104_config_load(path.c_str(), &c, &line), IEC104_OK);
    ASSERT_EQ(c.path_count, 2U);
    EXPECT_STREQ(c.paths[0].host, "192.0.2.10");
    EXPECT_EQ(c.paths[0].port, 2404U);
    EXPECT_STREQ(c.paths[0].tls_peer_name, "");
    EXPECT_STREQ(c.paths[1].host, "2001:db8::1");
    EXPECT_EQ(c.paths[1].port, 2405U);
    EXPECT_STREQ(c.paths[1].tls_peer_name, "station-b.example");
    EXPECT_STREQ(c.paths[1].tls_peer_fingerprint, "AB:CD");
    EXPECT_EQ(c.path_options.reconnect_interval_ms, 2500U);
    EXPECT_EQ(c.path_options.replay_window_ms, 60000U);
    EXPECT_EQ(c.path_options.replay_tolerance_ms, 250U);
    EXPECT_EQ(c.path_options.replay_max_objects, 500U);
    EXPECT_EQ(c.path_options.interrogation_after_switch, 0);
    EXPECT_EQ(c.path_options.prefer_first_path, 1);

    for (const char* bad : {"path = 192.0.2.10\n", "path = 2001:db8::1:2404\n",
                            "path = host:99999\n", "path = a:1 b c d\n"}) {
        std::FILE* f = std::fopen(path.c_str(), "w");
        ASSERT_NE(f, nullptr);
        std::fputs(bad, f);
        std::fclose(f);
        EXPECT_NE(iec104_config_load(path.c_str(), &c, &line), IEC104_OK) << bad;
        EXPECT_EQ(line, 1);
    }
    std::FILE* f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs("path = a:1\npath = a:2\npath = a:3\npath = a:4\npath = a:5\n", f);
    std::fclose(f);
    iec104_config_init(&c);
    EXPECT_EQ(iec104_config_load(path.c_str(), &c, &line), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(line, 5);
}
