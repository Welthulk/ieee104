/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_operation.cpp
 * @brief Operation: configuration file, statistics and logging (public C API only).
 */
#include "iec104/iec104.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

std::string write_file(const std::string& name, const std::string& text) {
    const std::string path = testing::TempDir() + name;
    std::ofstream(path) << text;
    return path;
}

struct Log {
    std::vector<std::string> lines;
    static void fn(void* user, iec104_log_level level, uint32_t connection, const char* message) {
        static_cast<Log*>(user)->lines.push_back(std::string(iec104_log_level_string(level)) + " " +
                                                 std::to_string(connection) + " " + message);
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

bool run_until(iec104_server* s, iec104_client* c, const std::function<bool()>& done) {
    for (int i = 0; i < 5000 && !done(); ++i) {
        (void)iec104_server_poll(s, 1);
        (void)iec104_client_poll(c, 1);
    }
    return done();
}

} // namespace

// Rule: every key of the file sets its field; times are seconds; comments and
// blank lines are ignored; keys not in the file keep their value.
TEST(ConfigFile, AllKeys) {
    const std::string path = write_file("all.conf", R"(# comment
host = 192.0.2.10
port = 2405
t0 = 15
t1 = 250        ; long timers of some peers
t2 = 240
t3 = 0.5
k = 16
w = 10
tolerant = yes
cot_length = 1
ca_length = 1
ioa_length = 2
max_asdu_length = 200
originator = 9

send_end_of_init = no
command_termination = 0
cause_of_init = 2
max_command_delay = 10
confirmation_timeout = 3.5
termination_timeout = 20
expect_termination = off
)");
    iec104_config c{};
    iec104_config_init(&c);
    int line = -1;
    ASSERT_EQ(iec104_config_load(path.c_str(), &c, &line), IEC104_OK);
    EXPECT_EQ(line, 0);
    EXPECT_STREQ(c.host, "192.0.2.10");
    EXPECT_EQ(c.port, 2405U);
    EXPECT_EQ(c.link.t0_ms, 15000U);
    EXPECT_EQ(c.link.t1_ms, 250000U);
    EXPECT_EQ(c.link.t3_ms, 500U);
    EXPECT_EQ(c.link.k, 16U);
    EXPECT_EQ(c.link.w, 10U);
    EXPECT_EQ(c.link.tolerant, 1);
    EXPECT_EQ(c.link.cot_length, 1U);
    EXPECT_EQ(c.link.ioa_length, 2U);
    EXPECT_EQ(c.link.max_asdu_length, 200U);
    EXPECT_EQ(c.link.originator, 9U);
    EXPECT_EQ(c.server.send_end_of_init, 0);
    EXPECT_EQ(c.server.command_termination, 0);
    EXPECT_EQ(c.server.cause_of_init, 2U);
    EXPECT_EQ(c.server.max_command_delay_ms, 10000U);
    EXPECT_EQ(c.client.confirmation_timeout_ms, 3500U);
    EXPECT_EQ(c.client.termination_timeout_ms, 20000U);
    EXPECT_EQ(c.client.expect_termination, 0);
}

// Rule: an error names its line and leaves the configuration unchanged.
TEST(ConfigFile, Errors) {
    iec104_config c{};
    iec104_config_init(&c);
    int line = 0;
    std::string path = write_file("unknown.conf", "t1 = 20\nunknown_key = 1\n");
    EXPECT_EQ(iec104_config_load(path.c_str(), &c, &line), IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(line, 2);
    EXPECT_EQ(c.link.t1_ms, 15000U); // unchanged although line 1 was valid

    path = write_file("range.conf", "\n\nport = 70000\n");
    EXPECT_EQ(iec104_config_load(path.c_str(), &c, &line), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(line, 3);

    path = write_file("syntax.conf", "t1 15\n");
    EXPECT_EQ(iec104_config_load(path.c_str(), &c, &line), IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(line, 1);

    path = write_file("value.conf", "tolerant = maybe\n");
    EXPECT_EQ(iec104_config_load(path.c_str(), &c, &line), IEC104_ERR_INVALID_ARGUMENT);

    EXPECT_EQ(iec104_config_load("/nonexistent/iec104.conf", &c, &line), IEC104_ERR_IO);
    EXPECT_EQ(iec104_config_load(nullptr, &c, &line), IEC104_ERR_INVALID_ARGUMENT);
}

// Rule: counters and log messages of server and client follow the traffic:
// accepted connection, state changes, trace of every APDU, request and
// command counters on both sides.
TEST(Operation, StatisticsAndLogging) {
    Log server_log;
    Log client_log;
    iec104_server* server = nullptr;
    iec104_client* client = nullptr;
    ASSERT_EQ(iec104_server_create(nullptr, nullptr, &server), IEC104_OK);
    iec104_server_set_log(server, IEC104_LOG_TRACE, Log::fn, &server_log);
    iec104_object point{};
    point.type = 11;
    point.ioa = 100;
    ASSERT_EQ(iec104_server_add_point(server, 1, &point, 1), IEC104_OK);
    ASSERT_EQ(iec104_server_listen(server, "127.0.0.1", 0), IEC104_OK);

    ASSERT_EQ(iec104_client_create(nullptr, nullptr, &client), IEC104_OK);
    iec104_client_set_log(client, IEC104_LOG_TRACE, Log::fn, &client_log);
    iec104_link_stats cs{};
    EXPECT_EQ(iec104_client_get_stats(client, &cs), IEC104_ERR_WRONG_STATE); // never connected
    ASSERT_EQ(iec104_client_connect(client, "127.0.0.1", iec104_server_port(server)), IEC104_OK);
    ASSERT_TRUE(run_until(server, client,
                          [&] { return iec104_client_state(client) == IEC104_LINK_STOPPED; }));
    ASSERT_EQ(iec104_client_start(client), IEC104_OK);
    ASSERT_TRUE(run_until(server, client,
                          [&] { return iec104_client_state(client) == IEC104_LINK_STARTED; }));
    ASSERT_EQ(iec104_client_interrogation(client, 1, 20), IEC104_OK);
    iec104_object cmd{};
    cmd.type = 45; // no on_command callback: the server rejects it
    cmd.ioa = 1;
    cmd.value = 1;
    ASSERT_EQ(iec104_client_send_command(client, 1, &cmd), IEC104_OK);
    ASSERT_TRUE(run_until(server, client, [&] {
        return iec104_client_get_stats(client, &cs) == IEC104_OK && cs.commands_rejected == 1U;
    }));

    iec104_link_stats ss{};
    ASSERT_EQ(iec104_server_get_stats(server, 1, &ss), IEC104_OK);
    EXPECT_EQ(ss.requests, 1U);
    EXPECT_EQ(ss.commands_rejected, 1U);
    EXPECT_EQ(ss.negative_answers, 1U);
    EXPECT_GE(ss.i_frames_received, 2U);
    EXPECT_GE(ss.i_frames_sent, 4U);     // end of init, ACTCON, data, ACTTERM, negative ACTCON
    EXPECT_EQ(ss.u_frames_received, 1U); // STARTDT act
    EXPECT_EQ(ss.bytes_sent, cs.bytes_received);
    EXPECT_EQ(cs.negative_answers, 1U);
    EXPECT_EQ(iec104_server_get_stats(server, 99, &ss), IEC104_ERR_WRONG_STATE);

    EXPECT_TRUE(server_log.contains("info 1 connection accepted from 127.0.0.1:"));
    EXPECT_TRUE(server_log.contains("info 1 state started"));
    EXPECT_TRUE(server_log.contains("warning 1 negative answer to type 45, cause 7"));
    EXPECT_TRUE(client_log.contains("trace 1 sent 68 04 07 00 00 00")); // STARTDT act
    EXPECT_TRUE(client_log.contains("info 1 command type 45 ioa 1: rejected"));

    // After close the client still reports the counters of the last connection.
    iec104_client_close(client);
    // The state is CLOSED at once; the close is reported on the next poll.
    ASSERT_TRUE(run_until(server, client,
                          [&] { return client_log.contains("info 1 closed: closed locally"); }));
    iec104_link_stats after{};
    ASSERT_EQ(iec104_client_get_stats(client, &after), IEC104_OK);
    EXPECT_EQ(after.u_frames_sent, cs.u_frames_sent);

    iec104_client_destroy(client);
    iec104_server_destroy(server);
}

// Rule: violated recommendations of the configuration are reported as
// warnings when the log is switched on (E2: warning, not error).
TEST(Operation, ConfigWarningsAreLogged) {
    iec104_link_config c{};
    iec104_link_config_init(&c);
    c.t2_ms = c.t1_ms;
    iec104_server* server = nullptr;
    ASSERT_EQ(iec104_server_create(&c, nullptr, &server), IEC104_OK);
    Log log;
    iec104_server_set_log(server, IEC104_LOG_WARNING, Log::fn, &log);
    EXPECT_TRUE(log.contains("warning 0 configuration: t2 is not below t1"));
    iec104_server_destroy(server);
}
