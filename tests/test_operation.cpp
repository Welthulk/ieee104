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

// Rule: TLS keys fill iec104_config.tls; relative file names are relative to
// the directory of the configuration file, absolute ones stay.
TEST(ConfigFile, TlsKeys) {
    const std::string path = write_file("tls.conf", R"(tls = yes
port = 19998
tls_ca = pki/ca.pem
tls_certificate = /etc/pki/server.pem
tls_key = pki/server.key
tls_key_password = secret
tls_crl = pki/crl.pem
tls_peer_name = station1.example
tls_peer_fingerprint = AB:CD
tls_min_version = 1.3
tls_max_version = 1.3
tls_ciphers = ECDHE-ECDSA-AES128-GCM-SHA256
tls_ciphersuites = TLS_AES_256_GCM_SHA384
tls_verify_peer = 1
tls_renegotiation = off
tls_session_resumption = no
tls_renewal_interval = 3600
tls_session_lifetime = 600
)");
    iec104_config c{};
    iec104_config_init(&c);
    EXPECT_EQ(c.use_tls, 0);
    EXPECT_EQ(c.tls.min_version, IEC104_TLS_1_2);
    int line = -1;
    ASSERT_EQ(iec104_config_load(path.c_str(), &c, &line), IEC104_OK) << line;
    const std::string dir = testing::TempDir(); // ends with '/'
    EXPECT_EQ(c.use_tls, 1);
    EXPECT_EQ(c.port, 19998U);
    EXPECT_EQ(std::string(c.tls.ca_file), dir.substr(0, dir.size() - 1U) + "/pki/ca.pem");
    EXPECT_STREQ(c.tls.certificate_file, "/etc/pki/server.pem");
    EXPECT_EQ(std::string(c.tls.private_key_file),
              dir.substr(0, dir.size() - 1U) + "/pki/server.key");
    EXPECT_STREQ(c.tls.key_password, "secret");
    EXPECT_STREQ(c.tls.peer_name, "station1.example");
    EXPECT_STREQ(c.tls.peer_fingerprint, "AB:CD");
    EXPECT_EQ(c.tls.min_version, IEC104_TLS_1_3);
    EXPECT_EQ(c.tls.max_version, IEC104_TLS_1_3);
    EXPECT_STREQ(c.tls.cipher_list, "ECDHE-ECDSA-AES128-GCM-SHA256");
    EXPECT_STREQ(c.tls.cipher_suites, "TLS_AES_256_GCM_SHA384");
    EXPECT_EQ(c.tls.renegotiation, 0);
    EXPECT_EQ(c.tls.session_resumption, 0);
    EXPECT_EQ(c.tls.renewal_interval_s, 3600U);
    EXPECT_EQ(c.tls.session_lifetime_s, 600U);

    const std::string bad = write_file("tls_bad.conf", "tls_min_version = 1.1\n");
    EXPECT_EQ(iec104_config_load(bad.c_str(), &c, &line), IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(line, 1);
}

// Rule: every "group" line adds a redundancy group; the buffer settings of
// the file apply to all groups, also when they stand below the groups.
TEST(ConfigFile, RedundancyGroups) {
    const std::string path = write_file("groups.conf", R"(group = 192.0.2.10, 192.0.2.11
group = 2001:db8::1 2001:db8::2
group = *
event_buffer_size = 5000
overflow = reject
)");
    iec104_config c{};
    iec104_config_init(&c);
    EXPECT_EQ(c.server.event_buffer_size, 1000U);
    EXPECT_EQ(c.server.overflow, IEC104_OVERFLOW_DROP_OLDEST);
    int line = -1;
    ASSERT_EQ(iec104_config_load(path.c_str(), &c, &line), IEC104_OK) << line;
    ASSERT_EQ(c.group_count, 3U);
    EXPECT_STREQ(c.groups[0].peers[0], "192.0.2.10");
    EXPECT_STREQ(c.groups[0].peers[1], "192.0.2.11");
    EXPECT_STREQ(c.groups[0].peers[2], "");
    EXPECT_STREQ(c.groups[1].peers[1], "2001:db8::2");
    EXPECT_STREQ(c.groups[2].peers[0], ""); // any address
    for (std::uint32_t i = 0; i < c.group_count; ++i) {
        EXPECT_EQ(c.groups[i].buffer_size, 5000U);
        EXPECT_EQ(c.groups[i].overflow, IEC104_OVERFLOW_REJECT);
    }
    EXPECT_EQ(c.server.event_buffer_size, 5000U);

    const std::string bad = write_file("groups_bad.conf", "group =\n");
    EXPECT_EQ(iec104_config_load(bad.c_str(), &c, &line), IEC104_ERR_INVALID_ARGUMENT);
    const std::string range = write_file("groups_range.conf", "event_buffer_size = 2000000\n");
    EXPECT_EQ(iec104_config_load(range.c_str(), &c, &line), IEC104_ERR_OUT_OF_RANGE);
}

namespace {

struct Received {
    std::vector<std::pair<iec104_asdu_info, iec104_object>> objects;
    iec104_link_state state{IEC104_LINK_CONNECTING};
    static void on_object(void* user, uint32_t, const iec104_asdu_info* info,
                          const iec104_object* o) {
        static_cast<Received*>(user)->objects.emplace_back(*info, *o);
    }
    static void on_state(void* user, uint32_t, iec104_link_state s, iec104_close_reason) {
        static_cast<Received*>(user)->state = s;
    }
};

} // namespace

// Rule: a read command (type 102) is a request with cause 5; the station
// answers with the object itself (its type, cause 5) or negatively with
// cause 47. iec104_client_send_command() with type 102 sends the same.
TEST(ClientRead, RequestAndAnswer) {
    iec104_server* s = nullptr;
    ASSERT_EQ(iec104_server_create(nullptr, nullptr, &s), IEC104_OK);
    iec104_object p{};
    p.type = 13;
    p.ioa = 100;
    p.value = 2.5;
    ASSERT_EQ(iec104_server_add_point(s, 1, &p, 1), IEC104_OK);
    ASSERT_EQ(iec104_server_listen(s, "127.0.0.1", 0), IEC104_OK);
    Received r;
    iec104_callbacks cb{};
    cb.user = &r;
    cb.on_object = Received::on_object;
    cb.on_state = Received::on_state;
    iec104_client* c = nullptr;
    ASSERT_EQ(iec104_client_create(nullptr, &cb, &c), IEC104_OK);
    ASSERT_EQ(iec104_client_connect(c, "127.0.0.1", iec104_server_port(s)), IEC104_OK);
    ASSERT_TRUE(run_until(s, c, [&] { return r.state == IEC104_LINK_STOPPED; }));
    ASSERT_EQ(iec104_client_start(c), IEC104_OK);
    ASSERT_TRUE(run_until(s, c, [&] { return r.state == IEC104_LINK_STARTED; }));
    r.objects.clear();

    ASSERT_EQ(iec104_client_read(c, 1, 100), IEC104_OK);
    ASSERT_TRUE(run_until(s, c, [&] { return !r.objects.empty(); }));
    EXPECT_EQ(r.objects[0].first.type, 13U);
    EXPECT_EQ(r.objects[0].first.cause, IEC104_COT_REQUEST);
    EXPECT_EQ(r.objects[0].first.negative, 0U);
    EXPECT_DOUBLE_EQ(r.objects[0].second.value, 2.5);

    r.objects.clear();
    iec104_object read{};
    read.type = 102;
    read.ioa = 999;
    ASSERT_EQ(iec104_client_send_command(c, 1, &read), IEC104_OK);
    ASSERT_TRUE(run_until(s, c, [&] { return !r.objects.empty(); }));
    EXPECT_EQ(r.objects[0].first.type, 102U);
    EXPECT_EQ(r.objects[0].first.cause, IEC104_COT_UNKNOWN_IOA);
    EXPECT_EQ(r.objects[0].first.negative, 1U);

    iec104_client_destroy(c);
    iec104_server_destroy(s);
}

// Rule: server options are checked before anything is applied; the cause of
// initialization has 7 bits. A rejected call leaves the previous options in
// force (seen in the end of initialization after STARTDT).
TEST(ServerOptions, RangeCheckedBeforeApplying) {
    iec104_server* s = nullptr;
    ASSERT_EQ(iec104_server_create(nullptr, nullptr, &s), IEC104_OK);
    iec104_server_options o{};
    iec104_server_options_init(&o);
    o.cause_of_init = 128;
    EXPECT_EQ(iec104_server_set_options(s, &o), IEC104_ERR_OUT_OF_RANGE);
    o.cause_of_init = 5;
    EXPECT_EQ(iec104_server_set_options(s, &o), IEC104_OK);
    o.cause_of_init = 9;     // would change the COI ...
    o.event_buffer_size = 0; // ... but this value is invalid
    EXPECT_EQ(iec104_server_set_options(s, &o), IEC104_ERR_OUT_OF_RANGE);

    iec104_object p{};
    p.type = 13;
    p.ioa = 100;
    ASSERT_EQ(iec104_server_add_point(s, 1, &p, 1), IEC104_OK);
    ASSERT_EQ(iec104_server_listen(s, "127.0.0.1", 0), IEC104_OK);
    Received r;
    iec104_callbacks cb{};
    cb.user = &r;
    cb.on_object = Received::on_object;
    cb.on_state = Received::on_state;
    iec104_client* c = nullptr;
    ASSERT_EQ(iec104_client_create(nullptr, &cb, &c), IEC104_OK);
    ASSERT_EQ(iec104_client_connect(c, "127.0.0.1", iec104_server_port(s)), IEC104_OK);
    ASSERT_TRUE(run_until(s, c, [&] { return r.state == IEC104_LINK_STOPPED; }));
    ASSERT_EQ(iec104_client_start(c), IEC104_OK);
    ASSERT_TRUE(run_until(s, c, [&] { return !r.objects.empty(); }));
    EXPECT_EQ(r.objects[0].first.type, 70U);
    EXPECT_EQ(r.objects[0].second.qualifier, 5U); // COI of the last accepted call
    iec104_client_destroy(c);
    iec104_server_destroy(s);
}
