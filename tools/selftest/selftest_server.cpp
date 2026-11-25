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
 * Exit code with --once: 0 if the connection sent all values and every
 * received command was in order, 1 otherwise. Without --once the server runs
 * until --duration has passed or it is stopped with Ctrl-C (exit code 0).
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
    unsigned long commands{0};
    bool commands_in_order{true};
    std::uint64_t next_send_ms{0};
};

struct Server {
    Options opt;
    iec104_server* handle{nullptr};
    std::map<std::uint32_t, Peer> peers;
    bool finished_once{false};
    bool once_passed{false};
};

std::uint64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000U +
           static_cast<std::uint64_t>(ts.tv_nsec) / 1000000U;
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
        selftest::report_line("commands received and confirmed",
                              std::to_string(p.commands) +
                                  (p.commands_in_order ? ", in order" : ", NOT in order"));
        const bool passed = p.sent == s->opt.count && p.commands_in_order &&
                            (reason == IEC104_CLOSE_PEER || reason == IEC104_CLOSE_LOCAL);
        std::printf("connection %u result: %s\n", id, passed ? "PASSED" : "FAILED");
        if (s->opt.once && !s->finished_once) {
            s->finished_once = true;
            s->once_passed = passed;
        }
        s->peers.erase(id);
    }
}

void on_asdu(void* user, std::uint32_t id, const std::uint8_t* asdu, std::size_t length) {
    auto* s = static_cast<Server*>(user);
    Peer& p = s->peers[id];
    const selftest::Received r = selftest::parse_asdu(asdu, length);
    if (s->opt.common.verbose) {
        std::printf("connection %u: received type %u cause %u ioa %u value %d\n", id, r.type,
                    r.cause, r.ioa, r.value);
    }
    if (!r.ok || r.type != selftest::kCommandType || r.cause != selftest::kCauseActivation) {
        return; // not part of the self-test
    }
    if (r.value != static_cast<std::int16_t>(p.commands) || r.ioa != selftest::kCommandIoa) {
        p.commands_in_order = false;
    }
    ++p.commands;
    // Confirm: same command, cause 7 (activation confirmation).
    std::uint8_t out[32];
    const std::size_t len = selftest::build_asdu(
        selftest::kCommandType, selftest::kCauseActivationCon, r.ioa, r.value, out, sizeof out);
    if (len == 0U || iec104_server_send_asdu(s->handle, id, out, len) != IEC104_OK) {
        // A full window drops the confirmation; the client then reports it missing.
        p.commands_in_order = false;
    }
}

// Sends the next measured values that are due on every started connection.
void send_due_values(Server& s) {
    const std::uint64_t now = now_ms();
    for (auto& [id, p] : s.peers) {
        while (p.started && p.sent < s.opt.count && now >= p.next_send_ms) {
            std::uint8_t out[32];
            const std::size_t len = selftest::build_asdu(
                selftest::kMeasurementType, selftest::kCauseSpontaneous, selftest::kMeasurementIoa,
                static_cast<std::int16_t>(p.sent), out, sizeof out);
            if (iec104_server_send_asdu(s.handle, id, out, len) != IEC104_OK) {
                break; // window k full: try again after the next poll
            }
            ++p.sent;
            p.next_send_ms = now + s.opt.interval_ms;
            if (s.opt.interval_ms > 0U) {
                break;
            }
        }
    }
}

void print_help() {
    std::printf("iec104_selftest_server - IEC 60870-5-104 controlled station for self-tests\n\n"
                "usage: iec104_selftest_server [options]\n\n"
                "  --host ADDR      local address to listen on (default: all)\n"
                "  --count N        measured values to send per connection (default 100)\n"
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
    iec104_link_config_init(&s.opt.common.link);
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
    iec104_status st = iec104_server_create(&s.opt.common.link, &cb, &s.handle);
    if (st != IEC104_OK) {
        std::fprintf(stderr, "invalid configuration: %s\n", iec104_status_string(st));
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
    const std::uint64_t end = s.opt.duration_s > 0U ? now_ms() + s.opt.duration_s * 1000U : 0U;
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
