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
 *   iec104_selftest_client --host 127.0.0.1 --port 2404 --idle 25
 *   iec104_selftest_client --host 10.0.0.5 --foreign --ca 7 --idle 60
 *   iec104_selftest_client --path 127.0.0.1:2404 --path 127.0.0.1:2405 --fault-every 4
 *
 * Every step is reported with ok / FAILED:
 *
 *   step                         self-test server              --foreign
 *   connect, STARTDT             yes                           yes
 *   end of initialization        type 70 after STARTDT         -
 *   general interrogation        ACTCON, 3 points, ACTTERM     ACTCON, ACTTERM, points counted
 *   group interrogation 2        only IOA 300                  -
 *   clock synchronization        positive ACTCON               ACTCON (positive or negative)
 *   counter interrogation        ACTCON, counter, ACTTERM      ACTCON, ACTTERM, counters counted
 *   set point commands           M x executed (direct point)   -
 *   execute without select       rejected (SBO point)          -
 *   select and execute           executed, IOA 300 = 1         -
 *   selection expires            selected, execute rejected    -
 *   command with time tag (58)   executed                      -
 *   late command (60 s old)      discarded: no confirmation    -
 *   unknown command IOA          rejected (cause 47)           -
 *   unknown station              negative, cause 46            -
 *   spontaneous values           N values in order             -
 *   switch-over (--switchover)   STOPDT old, STARTDT standby,  same, series of --series-ioa
 *                                the series continues          (periodic allowed)
 *   idle period, STOPDT          yes                           yes
 *
 * With --path (two or more, docs/05_Normfragen_und_Festlegungen.md, E11)
 * the client holds one path to each address instead; the stations must send
 * the same series (self-test servers with --broadcast). Steps: all paths
 * started, then the series while every --fault-every seconds the leading path
 * is disturbed (closed or silent, alternately; with --seed a random path with
 * a random fault, so that following paths fail as well), and
 * the series must arrive without a gap; repetitions are allowed.
 *
 * Exit code: 0 = all steps passed, 1 = a step failed, 2 = usage error.
 */
#include "selftest_common.hpp"

#include <cstdlib>
#include <ctime>
#include <functional>
#include <vector>

namespace {

struct Options {
    selftest::CommonOptions common;
    unsigned long count{10};
    unsigned long expect{100};
    unsigned long idle_s{0};
    unsigned long timeout_s{20};
    unsigned long ca{selftest::kStation};
    bool foreign{false};
    bool switchover{false}; // second connection as standby, switch in the middle of the series
    std::uint32_t series_ioa{selftest::kMeasurementIoa}; // IOA of the series (--series-ioa)
    std::vector<iec104_path_config> paths;               // --path, several: path redundancy
    unsigned long fault_every_s{0};                      // --fault-every
    unsigned long seed{0};                               // --seed, 0 = alternate
};

/** @brief One received object with the parts of its ASDU the checks need. */
struct Received {
    iec104_asdu_info info;
    iec104_object object;
};

/** @brief Final result of one command, as reported by on_command_result. */
struct Result {
    std::uint8_t type;
    std::uint32_t ioa;
    std::uint8_t select;
    iec104_command_result result;
};

struct Client {
    Options opt;
    std::vector<Result> results;
    iec104_client* handle{nullptr};
    iec104_link_state state{IEC104_LINK_CONNECTING};
    iec104_close_reason close_reason{IEC104_CLOSE_NONE};
    std::vector<Received> received;
    Client* other{nullptr};    // the other connection of a switch-over test, polled as well
    unsigned long switches{0}; // switch-overs of the leading path (--path)
};

std::uint64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (static_cast<std::uint64_t>(ts.tv_sec) * 1000U) +
           (static_cast<std::uint64_t>(ts.tv_nsec) / 1000000U);
}

void on_security(void* user, std::uint32_t id, iec104_security_event event, const char* detail) {
    const auto* self = static_cast<const Client*>(user);
    if (self->opt.common.verbose || event != IEC104_SECURITY_KEY_RENEWAL) {
        selftest::print_security(id, event, detail);
    }
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

void on_object(void* user, std::uint32_t /*id*/, const iec104_asdu_info* info,
               const iec104_object* object) {
    auto* c = static_cast<Client*>(user);
    if (c->opt.common.verbose) {
        selftest::print_object("received ", info, object);
    }
    c->received.push_back(Received{*info, *object});
}

void on_command_result(void* user, std::uint16_t /*ca*/, const iec104_object* command,
                       iec104_command_result result) {
    auto* c = static_cast<Client*>(user);
    if (c->opt.common.verbose) {
        std::printf("command type %u ioa %u%s: %s\n", command->type, command->ioa,
                    command->select != 0U ? " (select)" : "", iec104_command_result_string(result));
    }
    c->results.push_back(Result{command->type, command->ioa, command->select, result});
}

/** @brief Polls until @p done, the connection closes, or @p ms have passed. */
bool wait_for(Client& c, std::uint64_t ms, const std::function<bool()>& done) {
    const std::uint64_t end = now_ms() + ms;
    while (!done() && c.state != IEC104_LINK_CLOSED && now_ms() < end) {
        (void)iec104_client_poll(c.handle, 10);
        if (c.other != nullptr && c.other->handle != nullptr) {
            (void)iec104_client_poll(c.other->handle, 0); // the standby answers TESTFR
        }
    }
    return done();
}

/**
 * @brief Values of the series IOA (--series-ioa, default 100) in order of arrival.
 *
 * Self-test server: cause spontaneous. Foreign server (--foreign): also
 * cause periodic, which some servers buffer for their redundancy group.
 */
std::vector<double> series(const Client& c) {
    std::vector<double> v;
    for (const Received& r : c.received) {
        const bool cause = r.info.cause == IEC104_COT_SPONTANEOUS ||
                           (c.opt.foreign && r.info.cause == IEC104_COT_PERIODIC);
        if (cause && r.object.ioa == c.opt.series_ioa) {
            v.push_back(r.object.value);
        }
    }
    return v;
}

/**
 * @brief The series received over two connections one after the other.
 *
 * Events not acknowledged before the switch-over may arrive again on the
 * second connection; such repetitions are skipped.
 * @param repeated Receives the number of skipped repetitions.
 * @return Length of the gap-free series 0..n-1, or -1 if it has a gap or is out of order.
 */
long merged_series(const Client& first, const Client& second, unsigned long& repeated) {
    std::vector<double> all = series(first);
    const std::vector<double> more = series(second);
    repeated = 0;
    for (const double v : more) {
        if (!all.empty() && v <= all.back()) {
            ++repeated;
            continue;
        }
        all.push_back(v);
    }
    // Self-test server: 0, 1, 2, ...; foreign server: any start, then +1.
    const double start = first.opt.foreign && !all.empty() ? all.front() : 0.0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i] != start + static_cast<double>(i)) {
            return -1;
        }
    }
    return static_cast<long>(all.size());
}

/** @brief Index of the first object at or after @p from with type and cause, or -1. */
long find(const Client& c, std::size_t from, std::uint8_t type, std::uint8_t cause) {
    for (std::size_t i = from; i < c.received.size(); ++i) {
        const Received& r = c.received[i];
        if (r.info.type == type && r.info.cause == cause) {
            return static_cast<long>(i);
        }
    }
    return -1;
}

/** @brief Number of objects at or after @p from with the given cause. */
std::size_t count_cause(const Client& c, std::size_t from, std::uint8_t cause) {
    std::size_t n = 0;
    for (std::size_t i = from; i < c.received.size(); ++i) {
        n += c.received[i].info.cause == cause ? 1U : 0U;
    }
    return n;
}

/** @brief Sends one command and waits for its result. @return the result, or -1 on timeout. */
int command_result(Client& c, std::uint16_t ca, const iec104_object& cmd, bool select_execute,
                   std::uint64_t wait_ms) {
    const std::size_t before = c.results.size();
    const iec104_status st = select_execute ? iec104_client_select_execute(c.handle, ca, &cmd)
                                            : iec104_client_send_command(c.handle, ca, &cmd);
    if (st != IEC104_OK) {
        return -1;
    }
    if (!wait_for(c, wait_ms, [&] { return c.results.size() > before; })) {
        return -1;
    }
    return static_cast<int>(c.results.back().result);
}

std::string result_text(int r) {
    return r < 0 ? std::string("no result")
                 : iec104_command_result_string(static_cast<iec104_command_result>(r));
}

/**
 * @brief The command steps against the self-test server (select before operate,
 *        direct execute, time tag, maximum delay, unknown IOA).
 */
template <typename Step> void commands(Client& c, Step& step, std::uint16_t ca) {
    // Short time limits, so that the expected missing answer (late command) is quick.
    iec104_client_options options;
    iec104_client_options_init(&options);
    options.confirmation_timeout_ms = 3000;
    options.termination_timeout_ms = 3000;
    (void)iec104_client_set_options(c.handle, &options);
    const std::uint64_t wait_ms = 5000;

    // Set points at the direct point 5000: each confirmed and terminated.
    const std::size_t before = c.results.size();
    unsigned long sent = 0;
    (void)wait_for(c, c.opt.timeout_s * 1000U, [&] {
        while (sent < c.opt.count) {
            iec104_object sp{};
            sp.type = 49;
            sp.ioa = selftest::kSetPointIoa;
            sp.value = static_cast<double>(sent);
            if (iec104_client_send_command(c.handle, ca, &sp) != IEC104_OK) {
                break; // window k full: retry after the next poll
            }
            ++sent;
        }
        return c.results.size() >= before + c.opt.count;
    });
    std::size_t done = 0;
    for (std::size_t i = before; i < c.results.size(); ++i) {
        done += c.results[i].result == IEC104_COMMAND_TERMINATED ? 1U : 0U;
    }
    step("set point commands (direct)", done == c.opt.count,
         std::to_string(done) + "/" + std::to_string(c.opt.count) + " executed");

    iec104_object sc{};
    sc.type = 45;
    sc.ioa = selftest::kSwitchIoa;
    sc.value = 1;

    // Select before operate: an execute without selection is rejected.
    int r = command_result(c, ca, sc, false, wait_ms);
    step("execute without select (rejected)", r == IEC104_COMMAND_REJECTED, result_text(r));

    // Select and execute in one call; the switch reports IOA 300 = 1.
    const std::size_t mark = c.received.size();
    r = command_result(c, ca, sc, true, wait_ms);
    const bool feedback = wait_for(c, wait_ms, [&] {
        const long i = find(c, mark, 1, IEC104_COT_SPONTANEOUS);
        return i >= 0 && c.received[static_cast<std::size_t>(i)].object.value == 1.0;
    });
    step("select and execute with feedback", r == IEC104_COMMAND_TERMINATED && feedback,
         result_text(r));

    // A selection is valid for 2 s only.
    iec104_object sel = sc;
    sel.select = 1;
    r = command_result(c, ca, sel, false, wait_ms);
    (void)wait_for(c, selftest::kSelectTimeoutMs + 500U, [] { return false; });
    const int late_exec = command_result(c, ca, sc, false, wait_ms);
    step("selection expires after 2 s",
         r == IEC104_COMMAND_SELECTED && late_exec == IEC104_COMMAND_REJECTED,
         result_text(r) + ", then " + result_text(late_exec));

    // Command with time tag: current time is accepted, a time tag 60 s old is
    // discarded by the server without answer.
    iec104_object timed{};
    timed.type = 58;
    timed.ioa = selftest::kTimedIoa;
    timed.value = 1;
    const std::int64_t now_ms = static_cast<std::int64_t>(std::time(nullptr)) * 1000;
    (void)iec104_time_from_unix_ms(now_ms, &timed.time);
    r = command_result(c, ca, timed, false, wait_ms);
    step("command with time tag (type 58)", r == IEC104_COMMAND_TERMINATED, result_text(r));
    (void)iec104_time_from_unix_ms(now_ms - 60000, &timed.time);
    r = command_result(c, ca, timed, false, wait_ms);
    step("late command discarded (max. delay)", r == IEC104_COMMAND_NO_CONFIRMATION,
         result_text(r));

    // An IOA without command point: negative answer with cause 47.
    iec104_object bad{};
    bad.type = 49;
    bad.ioa = 9999;
    r = command_result(c, ca, bad, false, wait_ms);
    step("unknown command IOA (cause 47)", r == IEC104_COMMAND_REJECTED, result_text(r));
}

void print_help() {
    std::printf("iec104_selftest_client - IEC 60870-5-104 controlling station for self-tests\n\n"
                "usage: iec104_selftest_client [options]\n\n"
                "  --host ADDR      server address (default 127.0.0.1)\n"
                "  --foreign        test any IEC 104 server: only the sequences, not the\n"
                "                   data points of the self-test server\n"
                "  --ca N           common address of the station (default 1)\n"
                "  --count N        set-point commands to send (default 10)\n"
                "  --expect N       spontaneous values to expect (default 100)\n"
                "  --idle S         idle seconds before STOPDT, to see the link test\n"
                "                   (default 0; choose more than t3)\n"
                "  --timeout S      limit for each step in seconds (default 20)\n"
                "  --switchover     open a second connection as standby and switch to it\n"
                "                   while the series runs (server with --broadcast)\n"
                "  --series-ioa N   IOA of the series for --switchover (default 100); with\n"
                "                   --foreign any start value, spontaneous or periodic\n"
                "  --path HOST:PORT one path of a client with path redundancy; give it two\n"
                "                   to four times (servers with --broadcast)\n"
                "  --fault-every S  with --path: disturb the leading path every S seconds\n"
                "                   (default 0 = never)\n"
                "  --seed N         with --fault-every: random faults and paths from seed N\n"
                "                   (default 0 = close and silence the leading path in turn)\n");
    selftest::print_common_help();
}

/** @brief Creates the client handle of @p c with all options; prints the error. */
iec104_status create_handle(Client& c) {
    iec104_callbacks cb{};
    cb.user = &c;
    cb.on_state = on_state;
    cb.on_security = on_security;
    cb.on_object = on_object;
    cb.on_command_result = on_command_result;
    iec104_status st = iec104_client_create(&c.opt.common.link, &cb, &c.handle);
    if (st != IEC104_OK) {
        std::fprintf(stderr, "invalid configuration: %s\n", iec104_status_string(st));
        return st;
    }
    (void)iec104_client_set_options(c.handle, &c.opt.common.client);
    if (c.opt.common.log_level >= 0) {
        iec104_client_set_log(c.handle, static_cast<iec104_log_level>(c.opt.common.log_level),
                              selftest::print_log, nullptr);
    }
    if (c.opt.common.use_tls &&
        (st = iec104_client_set_tls(c.handle, &c.opt.common.tls)) != IEC104_OK) {
        std::fprintf(stderr, "TLS settings: %s (details with --log error)\n",
                     iec104_status_string(st));
        iec104_client_destroy(c.handle);
        c.handle = nullptr;
    }
    return st;
}

void on_path(void* user, const iec104_path_switch* e) {
    auto* c = static_cast<Client*>(user);
    if (e->previous != 0U) {
        ++c->switches;
    }
    std::printf("path switch: %s\n", e->reason);
    std::fflush(stdout);
}

/** @brief "host:port" or "[v6]:port" -> path; false if malformed. */
bool parse_path(const char* text, iec104_path_config& out) {
    const std::string t = text != nullptr ? text : "";
    const std::size_t colon = t.rfind(':');
    if (colon == std::string::npos || colon == 0U || colon + 1U >= t.size()) {
        return false;
    }
    std::string host = t.substr(0, colon);
    if (host.size() > 2U && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2U);
    }
    if (host.size() >= sizeof out.host) {
        return false;
    }
    out = iec104_path_config{};
    std::snprintf(out.host, sizeof out.host, "%s", host.c_str());
    out.port = static_cast<std::uint16_t>(
        selftest::parse_number("--path", t.substr(colon + 1U).c_str(), 65535));
    return true;
}

/**
 * @brief Values of the series received over all paths, merged.
 * @param repeated Receives the number of repetitions (value not above the last one).
 * @return Length of the gap-free series 0..n-1, or -1 if a value is missing.
 */
long path_series(const Client& c, unsigned long& repeated) {
    repeated = 0;
    long next = 0;
    for (const double v : series(c)) {
        if (v < static_cast<double>(next)) {
            ++repeated;
        } else if (v == static_cast<double>(next)) {
            ++next;
        } else {
            return -1; // gap
        }
    }
    return next;
}

/** @brief The path-redundancy test (--path). @return exit code. */
int run_paths(Client& c) {
    if (create_handle(c) != IEC104_OK) {
        return 2;
    }
    iec104_client_set_path_callback(c.handle, on_path, &c);
    iec104_path_options options{};
    iec104_path_options_init(&options);
    options.reconnect_interval_ms = 1000;
    std::printf("iec104_selftest_client: %zu paths, fault every %lu s%s\n", c.opt.paths.size(),
                c.opt.fault_every_s, c.opt.seed != 0U ? ", random" : "");
    bool passed = true;
    auto step = [&](const char* label, bool ok, const std::string& detail = "") {
        std::string text = ok ? (detail.empty() ? "ok" : detail) : "FAILED";
        if (!ok && !detail.empty()) {
            text += " (" + detail + ")";
        }
        selftest::report_line(label, text);
        passed = passed && ok;
        std::fflush(stdout);
        return ok;
    };
    const std::uint64_t step_ms = c.opt.timeout_s * 1000U;
    auto all_started = [&] {
        for (std::uint32_t p = 1; p <= c.opt.paths.size(); ++p) {
            iec104_path_info i{};
            if (iec104_client_get_path_info(c.handle, p, &i) != IEC104_OK ||
                i.state != IEC104_LINK_STARTED) {
                return false;
            }
        }
        return true;
    };
    const iec104_status st =
        iec104_client_connect_paths(c.handle, c.opt.paths.data(), c.opt.paths.size(), &options);
    bool ok = st == IEC104_OK && iec104_client_start(c.handle) == IEC104_OK &&
              wait_for(c, step_ms, all_started);
    step("all paths started", ok,
         st != IEC104_OK ? iec104_status_string(st)
                         : "leading path " + std::to_string(iec104_client_leading_path(c.handle)));

    // The series while paths fail.
    std::srand(static_cast<unsigned>(c.opt.seed));
    unsigned long faults = 0;
    unsigned long leading_faults = 0; // faults that hit the leading path (only those switch)
    unsigned long repeated = 0;
    const std::uint64_t fault_ms = c.opt.fault_every_s * 1000U;
    std::uint64_t next_fault = now_ms() + fault_ms;
    const std::uint64_t end = now_ms() + step_ms + (c.opt.expect * 50U);
    while (ok && now_ms() < end) {
        const long n = path_series(c, repeated);
        if (n < 0 || n >= static_cast<long>(c.opt.expect)) {
            break;
        }
        (void)iec104_client_poll(c.handle, 10);
        if (fault_ms == 0U || now_ms() < next_fault) {
            continue;
        }
        next_fault = now_ms() + fault_ms;
        std::uint32_t path = iec104_client_leading_path(c.handle);
        bool silent = faults % 2U == 1U;
        if (c.opt.seed != 0U) {
            silent = std::rand() % 2 == 1; // NOLINT(cert-msc30-c,cert-msc50-cpp): test pattern
            path = static_cast<std::uint32_t>(std::rand()) % // NOLINT(cert-msc30-c,cert-msc50-cpp)
                       static_cast<std::uint32_t>(c.opt.paths.size()) +
                   1U;
        }
        const bool hits_leading = path == iec104_client_leading_path(c.handle);
        if (path != 0U && iec104_client_disturb_path(c.handle, path,
                                                     silent ? IEC104_PATH_SILENT
                                                            : IEC104_PATH_CLOSE) == IEC104_OK) {
            ++faults;
            leading_faults += hits_leading ? 1U : 0U;
            std::printf("fault: path %u %s\n", path, silent ? "silent" : "closed");
            std::fflush(stdout);
        }
    }
    const long n = path_series(c, repeated);
    step("series over path faults", n == static_cast<long>(c.opt.expect),
         n < 0 ? "gap" : std::to_string(n) + "/" + std::to_string(c.opt.expect) + ", in order");
    step("repetitions around a switch-over", true, std::to_string(repeated));
    step("faults, switch-overs",
         fault_ms == 0U || (faults > 0U && (leading_faults == 0U || c.switches > 0U)),
         std::to_string(faults) + " faults (" + std::to_string(leading_faults) +
             " on the leading path), " + std::to_string(c.switches) + " switch-overs");
    for (std::uint32_t p = 1; p <= c.opt.paths.size(); ++p) {
        iec104_path_info i{};
        if (iec104_client_get_path_info(c.handle, p, &i) == IEC104_OK) {
            std::printf("path %u: %s:%u %s, connects %llu, leading %llu times, replayed %llu, "
                        "suppressed %llu, dropped %llu\n",
                        p, i.host, i.port, iec104_link_state_string(i.state),
                        static_cast<unsigned long long>(i.connects),
                        static_cast<unsigned long long>(i.became_leading),
                        static_cast<unsigned long long>(i.replayed),
                        static_cast<unsigned long long>(i.suppressed),
                        static_cast<unsigned long long>(i.dropped));
        }
    }
    iec104_client_close(c.handle);
    (void)wait_for(c, 2000, [&] {
        return iec104_client_state(c.handle) == IEC104_LINK_CLOSED && c.state == IEC104_LINK_CLOSED;
    });
    iec104_client_destroy(c.handle);
    std::printf("result: %s\n", passed ? "PASSED" : "FAILED");
    return passed ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    Client c;
    Client standby; // second connection of the switch-over test
    c.opt.common.host = "127.0.0.1";
    selftest::load_config_option(argc, argv, c.opt.common);
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
        } else if (arg == "--ca") {
            c.opt.ca = selftest::parse_number("--ca", value(), 65535);
        } else if (arg == "--foreign") {
            c.opt.foreign = true;
        } else if (arg == "--switchover") {
            c.opt.switchover = true;
        } else if (arg == "--series-ioa") {
            c.opt.series_ioa = static_cast<std::uint32_t>(
                selftest::parse_number("--series-ioa", value(), 16777215));
        } else if (arg == "--path") {
            iec104_path_config p{};
            if (!parse_path(value(), p) || c.opt.paths.size() >= IEC104_MAX_PATHS) {
                std::fprintf(stderr, "--path: HOST:PORT, at most %d times\n", IEC104_MAX_PATHS);
                return 2;
            }
            c.opt.paths.push_back(p);
        } else if (arg == "--fault-every") {
            c.opt.fault_every_s = selftest::parse_number("--fault-every", value(), 3600);
        } else if (arg == "--seed") {
            c.opt.seed = selftest::parse_number("--seed", value(), 1000000);
        } else if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        } else {
            std::fprintf(stderr, "unknown option %s (see --help)\n", arg.c_str());
            return 2;
        }
    }
    if (c.opt.paths.size() == 1U) {
        std::fprintf(stderr, "--path: give at least two paths\n");
        return 2;
    }
    if (!c.opt.paths.empty()) {
        return run_paths(c);
    }

    iec104_status st = create_handle(c);
    if (st != IEC104_OK) {
        return 2;
    }
    const auto ca = static_cast<std::uint16_t>(c.opt.ca);
    const std::uint64_t step_ms = c.opt.timeout_s * 1000U;
    std::printf("iec104_selftest_client: %s:%u, station %u%s\n", c.opt.common.host.c_str(),
                c.opt.common.port, ca, c.opt.foreign ? ", foreign server" : "");

    bool passed = true;
    auto step = [&](const char* label, bool ok, const std::string& detail = "") {
        std::string text = ok ? (detail.empty() ? "ok" : detail) : "FAILED";
        if (!ok && !detail.empty()) {
            text += " (" + detail + ")";
        }
        selftest::report_line(label, text);
        passed = passed && ok;
        std::fflush(stdout);
        return ok;
    };
    auto closed_detail = [&]() -> std::string {
        return c.state == IEC104_LINK_CLOSED ? iec104_close_reason_string(c.close_reason)
                                             : "timeout";
    };
    // Waits for ACTCON (and optionally ACTTERM) of a request sent after index @p from.
    auto confirmed = [&](std::size_t from, std::uint8_t type, bool termination) {
        return wait_for(c, step_ms, [&] {
            return find(c, from, type, IEC104_COT_ACTIVATION_CON) >= 0 &&
                   (!termination || find(c, from, type, IEC104_COT_ACTIVATION_TERMINATION) >= 0);
        });
    };
    auto positive = [&](std::size_t from, std::uint8_t type) {
        const long i = find(c, from, type, IEC104_COT_ACTIVATION_CON);
        return i >= 0 && c.received[static_cast<std::size_t>(i)].info.negative == 0U;
    };
    auto value_at = [&](long index) { return c.received[static_cast<std::size_t>(index)].object; };
    // A foreign server may reject a request it does not implement (causes 44..47).
    auto rejection = [&](std::size_t from, std::uint8_t type) -> long {
        for (std::uint8_t cause = IEC104_COT_UNKNOWN_TYPE; cause <= IEC104_COT_UNKNOWN_IOA;
             ++cause) {
            if (find(c, from, type, cause) >= 0) {
                return cause;
            }
        }
        return -1;
    };
    auto answered = [&](std::size_t from, std::uint8_t type, bool termination) {
        return wait_for(c, step_ms, [&] {
            return rejection(from, type) >= 0 ||
                   (find(c, from, type, IEC104_COT_ACTIVATION_CON) >= 0 &&
                    (!termination || !positive(from, type) ||
                     find(c, from, type, IEC104_COT_ACTIVATION_TERMINATION) >= 0));
        });
    };

    // Connect and start.
    st = iec104_client_connect(c.handle, c.opt.common.host.c_str(), c.opt.common.port);
    bool ok =
        st == IEC104_OK && wait_for(c, step_ms, [&] { return c.state == IEC104_LINK_STOPPED; });
    if (!step("connect", ok,
              ok ? "" : (st != IEC104_OK ? iec104_status_string(st) : closed_detail()))) {
        iec104_client_destroy(c.handle);
        std::printf("result: FAILED\n");
        return 1;
    }
    if (c.opt.common.use_tls) {
        iec104_tls_info tls{};
        (void)iec104_client_get_tls_info(c.handle, &tls);
        step("TLS", tls.version[0] != '\0',
             std::string(tls.version) + ", " + tls.cipher + ", peer " + tls.peer_subject);
    }
    if (c.opt.switchover) {
        // Standby: connected, data transfer stopped, supervised with TESTFR.
        standby.opt = c.opt;
        ok = create_handle(standby) == IEC104_OK &&
             iec104_client_connect(standby.handle, c.opt.common.host.c_str(), c.opt.common.port) ==
                 IEC104_OK;
        c.other = &standby;
        standby.other = &c;
        ok = ok && wait_for(standby, step_ms, [&] { return standby.state == IEC104_LINK_STOPPED; });
        step("standby connection (stopped)", ok);
    }
    ok = iec104_client_start(c.handle) == IEC104_OK &&
         wait_for(c, step_ms, [&] { return c.state == IEC104_LINK_STARTED; });
    if (step("start data transfer (STARTDT)", ok, ok ? "" : closed_detail())) {
        if (!c.opt.foreign) {
            ok = wait_for(c, 2000, [&] { return find(c, 0, 70, IEC104_COT_INITIALIZED) >= 0; });
            step("end of initialization (type 70)", ok);
        }

        // General interrogation.
        std::size_t mark = c.received.size();
        ok = iec104_client_interrogation(c.handle, ca, 20) == IEC104_OK &&
             confirmed(mark, 100, true);
        const std::size_t points = count_cause(c, mark, IEC104_COT_INTERROGATED_STATION);
        if (c.opt.foreign) {
            // Say what is missing, so that a deviation of the peer is visible.
            std::string detail = std::to_string(points) + " objects";
            if (find(c, mark, 100, IEC104_COT_ACTIVATION_CON) < 0) {
                detail += ", no confirmation (ACTCON)";
            } else if (!positive(mark, 100)) {
                detail += ", negative confirmation";
            } else if (find(c, mark, 100, IEC104_COT_ACTIVATION_TERMINATION) < 0) {
                detail += ", no termination (ACTTERM type 100)";
            }
            step("general interrogation", ok && positive(mark, 100), detail);
        } else {
            step("general interrogation", ok && positive(mark, 100) && points == 3U,
                 std::to_string(points) + "/3 objects");
            mark = c.received.size();
            ok = iec104_client_interrogation(c.handle, ca, 22) == IEC104_OK &&
                 confirmed(mark, 100, true);
            const long sp = find(c, mark, 1, 22);
            const std::size_t in_group = count_cause(c, mark, 22);
            step("group interrogation 2",
                 ok && in_group == 1U && sp >= 0 && value_at(sp).ioa == selftest::kSinglePointIoa,
                 std::to_string(in_group) + "/1 objects");
        }

        // Clock synchronization with the current UTC time.
        iec104_time now{};
        (void)iec104_time_from_unix_ms(static_cast<std::int64_t>(std::time(nullptr)) * 1000, &now);
        mark = c.received.size();
        if (c.opt.foreign) {
            ok = iec104_client_clock_sync(c.handle, ca, &now) == IEC104_OK &&
                 answered(mark, 103, false);
            const long r = rejection(mark, 103);
            step("clock synchronization", ok,
                 r >= 0 ? "not supported (cause " + std::to_string(r) + ")"
                        : (positive(mark, 103) ? "confirmed" : "negative confirmation"));
        } else {
            ok = iec104_client_clock_sync(c.handle, ca, &now) == IEC104_OK &&
                 confirmed(mark, 103, false);
            step("clock synchronization", ok && positive(mark, 103));
        }

        // Counter interrogation (general request, read).
        mark = c.received.size();
        if (c.opt.foreign) {
            ok = iec104_client_counter_interrogation(c.handle, ca, 5, 0) == IEC104_OK &&
                 answered(mark, 101, true);
            const long r = rejection(mark, 101);
            step("counter interrogation", ok,
                 r >= 0 ? "not supported (cause " + std::to_string(r) + ")"
                        : std::to_string(count_cause(c, mark, IEC104_COT_REQUESTED_COUNTERS)) +
                              " counters");
        } else {
            ok = iec104_client_counter_interrogation(c.handle, ca, 5, 0) == IEC104_OK &&
                 confirmed(mark, 101, true);
            const long counter = find(c, mark, 15, IEC104_COT_REQUESTED_COUNTERS);
            step("counter interrogation", ok && positive(mark, 101) && counter >= 0 &&
                                              value_at(counter).value == selftest::kCounterValue);
        }

        if (!c.opt.foreign) {
            // Commands: every result comes from the library's command tracking.
            commands(c, step, ca);

            // An unknown station is answered with cause 46.
            mark = c.received.size();
            ok = iec104_client_interrogation(c.handle, 99, 20) == IEC104_OK &&
                 wait_for(c, step_ms, [&] {
                     return find(c, mark, 100, IEC104_COT_UNKNOWN_COMMON_ADDRESS) >= 0;
                 });
            step("unknown station (cause 46)", ok);

            if (!c.opt.switchover) {
                // Spontaneous values of IOA 100 in order.
                auto spontaneous = [&](bool& in_order) {
                    unsigned long n = 0;
                    in_order = true;
                    for (const Received& r : c.received) {
                        if (r.info.cause == IEC104_COT_SPONTANEOUS &&
                            r.object.ioa == selftest::kMeasurementIoa) {
                            in_order = in_order && r.object.value == static_cast<double>(n);
                            ++n;
                        }
                    }
                    return n;
                };
                bool in_order = true;
                ok = wait_for(c, step_ms, [&] { return spontaneous(in_order) >= c.opt.expect; });
                const unsigned long values = spontaneous(in_order);
                step("spontaneous values", ok && in_order && values == c.opt.expect,
                     std::to_string(values) + "/" + std::to_string(c.opt.expect) +
                         (in_order ? ", in order" : ", NOT in order"));
            }
        }

        if (c.opt.switchover) {
            // Switch while the series is running: STOPDT on the first
            // connection, STARTDT on the standby; the series must go on
            // without a gap (self-test server with --broadcast, or a
            // foreign server whose series is buffered by its redundancy group).
            // Self-test server: the series has --expect values in all and must not
            // be complete yet. Foreign server: --expect values after the switch.
            ok = wait_for(c, step_ms, [&] { return !series(c).empty(); }) &&
                 (c.opt.foreign || series(c).size() < c.opt.expect) &&
                 iec104_client_stop(c.handle) == IEC104_OK &&
                 wait_for(c, step_ms, [&] { return c.state == IEC104_LINK_STOPPED; }) &&
                 iec104_client_start(standby.handle) == IEC104_OK &&
                 wait_for(standby, step_ms, [&] { return standby.state == IEC104_LINK_STARTED; });
            const auto before = static_cast<long>(series(c).size());
            step("switch-over (STOPDT, STARTDT standby)", ok,
                 std::to_string(before) + " values before");
            unsigned long repeated = 0;
            const long expect = static_cast<long>(c.opt.expect) + (c.opt.foreign ? before : 0L);
            ok = ok && wait_for(standby, step_ms,
                                [&] { return merged_series(c, standby, repeated) >= expect; });
            const long n = merged_series(c, standby, repeated);
            step(c.opt.foreign ? "series after switch-over" : "spontaneous values",
                 ok && n >= expect && (c.opt.foreign || n == expect),
                 n < 0 ? "gap or NOT in order"
                       : std::to_string(n) + "/" + std::to_string(expect) + ", in order" +
                             (repeated > 0U ? ", " + std::to_string(repeated) + " repeated" : ""));
        }

        // After a switch-over the standby carries the data.
        Client& m = c.opt.switchover && standby.state == IEC104_LINK_STARTED ? standby : c;

        // Idle: the link stays up only if both sides answer TESTFR within t1.
        if (c.opt.idle_s > 0U) {
            (void)wait_for(m, c.opt.idle_s * 1000U, [] { return false; });
            step("idle period (link test)", m.state == IEC104_LINK_STARTED,
                 m.state == IEC104_LINK_STARTED  ? std::to_string(c.opt.idle_s) + " s, ok"
                 : m.state == IEC104_LINK_CLOSED ? iec104_close_reason_string(m.close_reason)
                                                 : "timeout");
            if (c.opt.switchover) {
                step("idle connection still open",
                     (&m == &c ? standby : c).state == IEC104_LINK_STOPPED);
            }
        }

        ok = m.state == IEC104_LINK_STARTED && iec104_client_stop(m.handle) == IEC104_OK &&
             wait_for(m, step_ms, [&] { return m.state == IEC104_LINK_STOPPED; });
        step("stop data transfer (STOPDT)", ok,
             ok || m.state != IEC104_LINK_CLOSED ? "" : iec104_close_reason_string(m.close_reason));
    }

    c.other = nullptr;
    standby.other = nullptr;
    for (Client* k : {&standby, &c}) {
        if (k->handle == nullptr) {
            continue;
        }
        iec104_client_close(k->handle);
        (void)wait_for(*k, 1000, [&] { return k->state == IEC104_LINK_CLOSED; });
    }
    iec104_link_stats stats{};
    if (c.opt.common.stats && iec104_client_get_stats(c.handle, &stats) == IEC104_OK) {
        selftest::print_stats(stats);
    }
    iec104_client_destroy(standby.handle);
    iec104_client_destroy(c.handle);
    std::printf("result: %s\n", passed ? "PASSED" : "FAILED");
    return passed ? 0 : 1;
}
