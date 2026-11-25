/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file selftest_client.cpp
 * @brief iec104_selftest_client: controlling station for the self-test (see selftest_common.hpp).
 *
 * Example:
 *   iec104_selftest_client --host 127.0.0.1 --port 2404 --expect 100 --count 10 --idle 25
 *
 * Steps, each reported with ok / FAILED:
 *   1. TCP connect (t0)            4. M commands confirmed (cause 7)
 *   2. STARTDT act/con             5. idle period: link test with TESTFR after t3
 *   3. N measured values in order  6. STOPDT act/con, close
 *
 * Exit code: 0 = all steps passed, 1 = a step failed, 2 = usage error.
 * With --expect 0 the client does not wait for measured values, which allows
 * a test against any IEC 104 server that only has to accept STARTDT/STOPDT.
 */
#include "selftest_common.hpp"

#include <ctime>
#include <functional>

namespace {

struct Options {
    selftest::CommonOptions common;
    unsigned long count{10};
    unsigned long expect{100};
    unsigned long idle_s{0};
    unsigned long timeout_s{60};
};

struct Client {
    Options opt;
    iec104_client* handle{nullptr};
    iec104_link_state state{IEC104_LINK_CONNECTING};
    iec104_close_reason close_reason{IEC104_CLOSE_NONE};
    unsigned long values{0};
    bool values_in_order{true};
    unsigned long confirmations{0};
    bool confirmations_in_order{true};
    unsigned long other_asdus{0};
};

std::uint64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000U +
           static_cast<std::uint64_t>(ts.tv_nsec) / 1000000U;
}

void on_state(void* user, std::uint32_t /*id*/, iec104_link_state state,
              iec104_close_reason reason) {
    auto* c = static_cast<Client*>(user);
    c->state = state;
    if (state == IEC104_LINK_CLOSED) {
        c->close_reason = reason;
    }
    if (c->opt.common.verbose) {
        std::printf("state: %s%s%s\n", iec104_link_state_string(state),
                    state == IEC104_LINK_CLOSED ? ", " : "",
                    state == IEC104_LINK_CLOSED ? iec104_close_reason_string(reason) : "");
    }
}

void on_asdu(void* user, std::uint32_t /*id*/, const std::uint8_t* asdu, std::size_t length) {
    auto* c = static_cast<Client*>(user);
    const selftest::Received r = selftest::parse_asdu(asdu, length);
    if (c->opt.common.verbose) {
        std::printf("received type %u cause %u ioa %u value %d\n", r.type, r.cause, r.ioa, r.value);
    }
    if (r.ok && r.type == selftest::kMeasurementType && r.ioa == selftest::kMeasurementIoa) {
        if (r.value != static_cast<std::int16_t>(c->values)) {
            c->values_in_order = false;
        }
        ++c->values;
    } else if (r.ok && r.type == selftest::kCommandType &&
               r.cause == selftest::kCauseActivationCon) {
        if (r.value != static_cast<std::int16_t>(c->confirmations)) {
            c->confirmations_in_order = false;
        }
        ++c->confirmations;
    } else {
        ++c->other_asdus;
    }
}

/** @brief Polls until @p done, the connection closes, or @p ms have passed. */
bool wait_for(Client& c, std::uint64_t ms, const std::function<bool()>& done) {
    const std::uint64_t end = now_ms() + ms;
    while (!done() && c.state != IEC104_LINK_CLOSED && now_ms() < end) {
        (void)iec104_client_poll(c.handle, 10);
    }
    return done();
}

void print_help() {
    std::printf("iec104_selftest_client - IEC 60870-5-104 controlling station for self-tests\n\n"
                "usage: iec104_selftest_client [options]\n\n"
                "  --host ADDR      server address (default 127.0.0.1)\n"
                "  --count N        set-point commands to send (default 10, 0 = none)\n"
                "  --expect N       measured values to expect (default 100, 0 = none)\n"
                "  --idle S         idle seconds before STOPDT, to see the link test\n"
                "                   (default 0; choose more than t3)\n"
                "  --timeout S      limit for each step in seconds (default 60)\n");
    selftest::print_common_help();
}

std::string ratio(unsigned long have, unsigned long want, bool in_order) {
    std::string s = std::to_string(have) + "/" + std::to_string(want);
    if (have > 0U) {
        s += in_order ? ", in order" : ", NOT in order";
    }
    return s;
}

} // namespace

int main(int argc, char** argv) {
    Client c;
    c.opt.common.host = "127.0.0.1";
    iec104_link_config_init(&c.opt.common.link);
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (selftest::parse_common(argc, argv, i, c.opt.common)) {
            continue;
        }
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (arg == "--count") {
            c.opt.count = selftest::parse_number("--count", value(), 32767);
        } else if (arg == "--expect") {
            c.opt.expect = selftest::parse_number("--expect", value(), 32767);
        } else if (arg == "--idle") {
            c.opt.idle_s = selftest::parse_number("--idle", value(), 172800);
        } else if (arg == "--timeout") {
            c.opt.timeout_s = selftest::parse_number("--timeout", value(), 86400);
        } else if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        } else {
            std::fprintf(stderr, "unknown option %s (see --help)\n", arg.c_str());
            return 2;
        }
    }

    iec104_callbacks cb{};
    cb.user = &c;
    cb.on_asdu = on_asdu;
    cb.on_state = on_state;
    iec104_status st = iec104_client_create(&c.opt.common.link, &cb, &c.handle);
    if (st != IEC104_OK) {
        std::fprintf(stderr, "invalid configuration: %s\n", iec104_status_string(st));
        return 2;
    }
    std::printf("iec104_selftest_client: %s:%u\n", c.opt.common.host.c_str(), c.opt.common.port);
    const std::uint64_t step_ms = c.opt.timeout_s * 1000U;
    bool passed = true;
    auto step = [&](const char* label, bool ok, const std::string& detail = "") {
        selftest::report_line(label, ok ? (detail.empty() ? "ok" : detail)
                                        : "FAILED" + (detail.empty() ? "" : " (" + detail + ")"));
        passed = passed && ok;
        std::fflush(stdout);
        return ok;
    };
    auto closed_detail = [&]() {
        return c.state == IEC104_LINK_CLOSED ? iec104_close_reason_string(c.close_reason)
                                             : "timeout";
    };

    // 1. Connect.
    st = iec104_client_connect(c.handle, c.opt.common.host.c_str(), c.opt.common.port);
    bool ok =
        st == IEC104_OK && wait_for(c, step_ms, [&] { return c.state == IEC104_LINK_STOPPED; });
    const std::string connect_detail =
        ok ? "" : (st != IEC104_OK ? iec104_status_string(st) : closed_detail());
    if (!step("connect", ok, connect_detail)) {
        iec104_client_destroy(c.handle);
        std::printf("result: FAILED\n");
        return 1;
    }

    // 2. Start data transfer.
    ok = iec104_client_start(c.handle) == IEC104_OK &&
         wait_for(c, step_ms, [&] { return c.state == IEC104_LINK_STARTED; });
    if (step("start data transfer (STARTDT)", ok, ok ? "" : closed_detail())) {
        // 3. + 4. Send commands while values arrive.
        unsigned long sent = 0;
        const bool all = wait_for(c, step_ms, [&] {
            while (sent < c.opt.count) {
                std::uint8_t out[32];
                const std::size_t len = selftest::build_asdu(
                    selftest::kCommandType, selftest::kCauseActivation, selftest::kCommandIoa,
                    static_cast<std::int16_t>(sent), out, sizeof out);
                if (iec104_client_send_asdu(c.handle, out, len) != IEC104_OK) {
                    break; // window k full: retry after the next poll
                }
                ++sent;
            }
            return c.values >= c.opt.expect && c.confirmations >= c.opt.count;
        });
        (void)all;
        step("measured values received", c.values == c.opt.expect && c.values_in_order,
             ratio(c.values, c.opt.expect, c.values_in_order));
        step("commands confirmed", c.confirmations == c.opt.count && c.confirmations_in_order,
             ratio(c.confirmations, c.opt.count, c.confirmations_in_order));

        // 5. Idle: the link stays up only if both sides answer TESTFR within t1.
        if (c.opt.idle_s > 0U) {
            (void)wait_for(c, c.opt.idle_s * 1000U, [] { return false; });
            step("idle period (link test)", c.state == IEC104_LINK_STARTED,
                 c.state == IEC104_LINK_STARTED ? std::to_string(c.opt.idle_s) + " s, ok"
                                                : closed_detail());
        }

        // 6. Stop data transfer.
        ok = c.state == IEC104_LINK_STARTED && iec104_client_stop(c.handle) == IEC104_OK &&
             wait_for(c, step_ms, [&] { return c.state == IEC104_LINK_STOPPED; });
        step("stop data transfer (STOPDT)", ok, ok ? "" : closed_detail());
    }

    iec104_client_close(c.handle);
    (void)wait_for(c, 1000, [&] { return c.state == IEC104_LINK_CLOSED; });
    iec104_client_destroy(c.handle);
    if (c.other_asdus > 0U) {
        selftest::report_line("other ASDUs received (ignored)", std::to_string(c.other_asdus));
    }
    std::printf("result: %s\n", passed ? "PASSED" : "FAILED");
    return passed ? 0 : 1;
}
