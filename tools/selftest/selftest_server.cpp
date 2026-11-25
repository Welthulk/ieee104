/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file selftest_server.cpp
 * @brief iec104_selftest_server: controlled station for the self-test (see selftest_common.hpp).
 *
 * Example:
 *   iec104_selftest_server --port 2404 --count 100 --interval 10
 *   iec104_selftest_server --port 0 --port-file /tmp/port --once     (for scripts)
 *
 * Exit code with --once: 0 if the first connection received all values and
 * closed regularly, 1 otherwise. Without --once the server runs until
 * --duration has passed or it is stopped with Ctrl-C (exit code 0).
 */
#include "selftest_common.hpp"

#include <csignal>
#include <ctime>
#include <map>

namespace {

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int /*sig*/) {
    g_stop = 1;
}

struct Options {
    selftest::CommonOptions common;
    unsigned long count{100};
    unsigned long interval_ms{10};
    unsigned long duration_s{0};
    bool once{false};
    std::string port_file;
};

/** @brief Progress of one connection. */
struct Peer {
    bool started{false};
    unsigned long sent{0};
    std::uint64_t next_send_ms{0};
    unsigned long requests{0}; // interrogations, counter interrogations, reads, tests
    unsigned long commands{0}; // commands that reached the application (incl. selects)
    unsigned long clock_syncs{0};
};

struct Server {
    Options opt;
    iec104_server* handle{nullptr};
    std::map<std::uint32_t, Peer> peers;
    bool switch_state{false};
    bool finished_once{false};
    bool once_passed{false};
};

std::uint64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (static_cast<std::uint64_t>(ts.tv_sec) * 1000U) +
           (static_cast<std::uint64_t>(ts.tv_nsec) / 1000000U);
}

iec104_object make(std::uint8_t type, std::uint32_t ioa, double value) {
    iec104_object o{};
    o.type = type;
    o.ioa = ioa;
    o.value = value;
    return o;
}

void on_security(void* user, std::uint32_t id, iec104_security_event event, const char* detail) {
    const auto* self = static_cast<const Server*>(user);
    if (self->opt.common.verbose || event != IEC104_SECURITY_KEY_RENEWAL) {
        selftest::print_security(id, event, detail);
    }
}

void on_state(void* user, std::uint32_t id, iec104_link_state state, iec104_close_reason reason) {
    auto* s = static_cast<Server*>(user);
    Peer& p = s->peers[id];
    if (s->opt.common.verbose || state == IEC104_LINK_STARTED || state == IEC104_LINK_CLOSED) {
        std::printf("connection %u: %s%s%s\n", id, iec104_link_state_string(state),
                    state == IEC104_LINK_CLOSED ? ", " : "",
                    state == IEC104_LINK_CLOSED ? iec104_close_reason_string(reason) : "");
    }
    if (state == IEC104_LINK_STARTED) {
        p.started = true;
    } else if (state == IEC104_LINK_STOPPED || state == IEC104_LINK_STOP_PENDING) {
        p.started = false;
    } else if (state == IEC104_LINK_CLOSED) {
        selftest::report_line("measured values sent",
                              std::to_string(p.sent) + "/" + std::to_string(s->opt.count));
        selftest::report_line("requests answered (100-107)", std::to_string(p.requests));
        selftest::report_line("commands passed to the application", std::to_string(p.commands));
        selftest::report_line("clock synchronizations", std::to_string(p.clock_syncs));
        const bool passed =
            p.sent == s->opt.count && (reason == IEC104_CLOSE_PEER || reason == IEC104_CLOSE_LOCAL);
        if (s->opt.common.stats) {
            iec104_link_stats st{};
            if (iec104_server_get_stats(s->handle, id, &st) == IEC104_OK) {
                selftest::print_stats(st);
            }
        }
        std::printf("connection %u result: %s\n", id, passed ? "PASSED" : "FAILED");
        if (s->opt.once && !s->finished_once) {
            s->finished_once = true;
            s->once_passed = passed;
        }
        s->peers.erase(id);
    }
}

// Counts the requests the library answers by itself (they do not reach on_command).
void on_asdu(void* user, std::uint32_t id, const std::uint8_t* asdu, std::size_t length) {
    auto* s = static_cast<Server*>(user);
    if (length > 0U && asdu[0] >= 100U && asdu[0] <= 107U && asdu[0] != 103U && asdu[0] != 105U) {
        ++s->peers[id].requests;
    }
}

void on_object(void* user, std::uint32_t id, const iec104_asdu_info* info,
               const iec104_object* object) {
    auto* s = static_cast<Server*>(user);
    if (s->opt.common.verbose) {
        std::printf("connection %u: ", id);
        selftest::print_object("received ", info, object);
    }
}

int on_command(void* user, std::uint32_t id, const iec104_asdu_info* info,
               const iec104_object* command) {
    auto* s = static_cast<Server*>(user);
    Peer& p = s->peers[id];
    if (s->opt.common.verbose) {
        std::printf("connection %u: ", id);
        selftest::print_object("command ", info, command);
    }
    if (info->type == 103U) {
        ++p.clock_syncs;
        if (s->opt.common.verbose) {
            std::printf("  clock %04u-%02u-%02u %02u:%02u:%02u.%03u\n", command->time.year,
                        command->time.month, command->time.day, command->time.hour,
                        command->time.minute, command->time.second, command->time.millisecond);
        }
        return 1;
    }
    // The library has already checked the command points (IOA, type, select
    // before operate, maximum delay); this application accepts what arrives.
    const bool switching = info->type == 45U && command->ioa == selftest::kSwitchIoa;
    ++p.commands;
    if (switching && command->select == 0U) {
        // Execute: the switch follows the command and reports its new state.
        s->switch_state = command->value != 0.0;
        const iec104_object point = make(1, selftest::kSinglePointIoa, s->switch_state ? 1.0 : 0.0);
        (void)iec104_server_update(s->handle, selftest::kStation, &point);
    }
    return 1;
}

// Sends the next measured values that are due on every started connection.
void send_due_values(Server& s) {
    const std::uint64_t now = now_ms();
    for (auto& [id, p] : s.peers) {
        while (p.started && p.sent < s.opt.count && now >= p.next_send_ms) {
            const iec104_object v =
                make(11, selftest::kMeasurementIoa, static_cast<double>(p.sent));
            if (iec104_server_send(s.handle, id, selftest::kStation, IEC104_COT_SPONTANEOUS, &v,
                                   1) != IEC104_OK) {
                break; // queue full: try again after the next poll
            }
            ++p.sent;
            p.next_send_ms = now + s.opt.interval_ms;
            if (s.opt.interval_ms > 0U) {
                break;
            }
        }
    }
}

void add_points(Server& s) {
    iec104_object m = make(11, selftest::kMeasurementIoa, 0);
    iec104_object f = make(13, selftest::kFloatIoa, selftest::kFloatValue);
    iec104_object sp = make(1, selftest::kSinglePointIoa, 0);
    iec104_object c = make(15, selftest::kCounterIoa, selftest::kCounterValue);
    (void)iec104_server_add_point(s.handle, selftest::kStation, &m, 0x0001U);
    (void)iec104_server_add_point(s.handle, selftest::kStation, &f, 0x0001U);
    (void)iec104_server_add_point(s.handle, selftest::kStation, &sp, 0x0002U);
    (void)iec104_server_add_point(s.handle, selftest::kStation, &c, 0x0001U);
}

void print_help() {
    std::printf("iec104_selftest_server - IEC 60870-5-104 controlled station for self-tests\n\n"
                "usage: iec104_selftest_server [options]\n\n"
                "  --host ADDR      local address to listen on (default: all)\n"
                "  --count N        spontaneous values per connection (default 100)\n"
                "  --interval MS    pause between two values in ms (default 10, 0 = burst)\n"
                "  --duration S     stop after S seconds (default 0 = run until Ctrl-C)\n"
                "  --once           exit after the first connection has closed; exit code\n"
                "                   0 = passed, 1 = failed\n"
                "  --port-file F    write the port actually used to file F (for --port 0)\n");
    selftest::print_common_help();
}

} // namespace

int main(int argc, char** argv) {
    Server s;
    // Self-test default; a configuration file or later options may change it.
    s.opt.common.server.max_command_delay_ms = selftest::kMaxCommandDelayMs;
    selftest::load_config_option(argc, argv, s.opt.common);
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (selftest::parse_common(argc, argv, i, s.opt.common)) {
            continue;
        }
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--count") {
            s.opt.count = selftest::parse_number("--count", value(), 32767);
        } else if (arg == "--interval") {
            s.opt.interval_ms = selftest::parse_number("--interval", value(), 3600000);
        } else if (arg == "--duration") {
            s.opt.duration_s = selftest::parse_number("--duration", value(), 31536000);
        } else if (arg == "--once") {
            s.opt.once = true;
        } else if (arg == "--port-file") {
            const char* v = value();
            s.opt.port_file = v != nullptr ? v : "";
        } else if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        } else {
            std::fprintf(stderr, "unknown option %s (see --help)\n", arg.c_str());
            return 2;
        }
    }

    iec104_callbacks cb{};
    cb.user = &s;
    cb.on_asdu = on_asdu;
    cb.on_state = on_state;
    cb.on_security = on_security;
    cb.on_object = on_object;
    cb.on_command = on_command;
    iec104_status st = iec104_server_create(&s.opt.common.link, &cb, &s.handle);
    if (st != IEC104_OK) {
        std::fprintf(stderr, "invalid configuration: %s\n", iec104_status_string(st));
        return 2;
    }
    add_points(s);
    (void)iec104_server_add_command(s.handle, selftest::kStation, selftest::kSetPointIoa, 49,
                                    IEC104_COMMAND_DIRECT, 0);
    (void)iec104_server_add_command(s.handle, selftest::kStation, selftest::kSwitchIoa, 45,
                                    IEC104_COMMAND_SELECT_EXECUTE, selftest::kSelectTimeoutMs);
    (void)iec104_server_add_command(s.handle, selftest::kStation, selftest::kTimedIoa, 58,
                                    IEC104_COMMAND_DIRECT, 0);
    (void)iec104_server_set_options(s.handle, &s.opt.common.server);
    if (s.opt.common.log_level >= 0) {
        iec104_server_set_log(s.handle, static_cast<iec104_log_level>(s.opt.common.log_level),
                              selftest::print_log, nullptr);
    }
    if (s.opt.common.use_tls &&
        (st = iec104_server_set_tls(s.handle, &s.opt.common.tls)) != IEC104_OK) {
        std::fprintf(stderr, "TLS settings: %s (details with --log error)\n",
                     iec104_status_string(st));
        iec104_server_destroy(s.handle);
        return 2;
    }
    st = iec104_server_listen(s.handle, s.opt.common.host.c_str(), s.opt.common.port);
    if (st != IEC104_OK) {
        std::fprintf(stderr, "cannot listen on port %u: %s\n", s.opt.common.port,
                     iec104_status_string(st));
        iec104_server_destroy(s.handle);
        return 2;
    }
    const unsigned port = iec104_server_port(s.handle);
    std::printf("iec104_selftest_server listening on port %u\n", port);
    std::fflush(stdout);
    if (!s.opt.port_file.empty()) {
        if (FILE* f = std::fopen(s.opt.port_file.c_str(), "w")) {
            std::fprintf(f, "%u\n", port);
            std::fclose(f);
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    const std::uint64_t end = s.opt.duration_s > 0U ? now_ms() + (s.opt.duration_s * 1000U) : 0U;
    while (g_stop == 0 && !s.finished_once && (end == 0U || now_ms() < end)) {
        send_due_values(s);
        (void)iec104_server_poll(s.handle, 5);
        std::fflush(stdout);
    }
    iec104_server_destroy(s.handle);
    if (s.opt.once) {
        return s.finished_once && s.once_passed ? 0 : 1;
    }
    return 0;
}
