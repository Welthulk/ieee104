/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file selftest_common.hpp
 * @brief Shared code of the self-test programs: data points, options, output.
 *
 * The self-test programs exercise the library over real TCP. They use only
 * the public C API (iec104.h).
 *
 *   iec104_selftest_server  (controlled station, common address 1)
 *     process image:  IOA 100 scaled value (11)        group 1
 *                     IOA 101 short float (13)          group 1
 *                     IOA 300 single point (1)          group 2
 *                     IOA 200 integrated totals (15)    counter group 1
 *     after STARTDT:  end of initialization (70), then N spontaneous values
 *                     of IOA 100 (11, cause 3): 0, 1, 2, ...
 *     commands:       set point scaled (49) at IOA 5000 accepted;
 *                     single command (45) at IOA 6000 accepted, switches
 *                     IOA 300 and reports it spontaneously;
 *                     clock synchronization accepted; everything else rejected
 *
 *   iec104_selftest_client  (controlling station)
 *     checks every sequence against these points (see selftest_client.cpp);
 *     with --foreign only the sequences, for any IEC 104 server.
 */
#pragma once

#include "iec104/iec104.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace selftest {

inline constexpr std::uint16_t kStation = 1;
inline constexpr std::uint32_t kMeasurementIoa = 100; // type 11, spontaneous series
inline constexpr std::uint32_t kFloatIoa = 101;       // type 13
inline constexpr std::uint32_t kCounterIoa = 200;     // type 15
inline constexpr std::uint32_t kSinglePointIoa = 300; // type 1
inline constexpr std::uint32_t kSetPointIoa = 5000;   // commands type 49
inline constexpr std::uint32_t kSwitchIoa = 6000;     // commands type 45, switches IOA 300
inline constexpr double kFloatValue = 49.95;
inline constexpr double kCounterValue = 123456;

/** @brief Options both programs understand. */
struct CommonOptions {
    std::string host;
    std::uint16_t port{IEC104_DEFAULT_PORT};
    iec104_link_config link{};
    bool verbose{false};
};

/** @brief Prints the options both programs understand (part of --help). */
inline void print_common_help() {
    std::printf("  --port N         TCP port (default %d; server: 0 = any free port)\n"
                "  --t0 S --t1 S --t2 S --t3 S\n"
                "                   timers in seconds (default 30 15 10 20; t3 0 = off, needs "
                "--tolerant)\n"
                "  --k N --w N      window sizes (default 12 and 8)\n"
                "  --tolerant       accept known deviations of peers (default: strict)\n"
                "  --verbose        print every state change and object\n"
                "  --help           this text\n",
                IEC104_DEFAULT_PORT);
}

/** @brief Reads an unsigned number or exits with a message. */
inline unsigned long parse_number(const char* option, const char* text, unsigned long max) {
    char* end = nullptr;
    const unsigned long v = std::strtoul(text != nullptr ? text : "", &end, 10);
    if (text == nullptr || *text == '\0' || *end != '\0' || v > max) {
        std::fprintf(stderr, "invalid value for %s: %s\n", option, text != nullptr ? text : "");
        std::exit(2);
    }
    return v;
}

/**
 * @brief Handles one common option at argv[i].
 * @return true if it was a common option (i is advanced past its value).
 */
inline bool parse_common(int argc, char** argv, int& i, CommonOptions& o) {
    const std::string arg = argv[i];
    auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    constexpr unsigned long kMaxSeconds = 48UL * 3600UL;
    if (arg == "--host") {
        const char* v = value();
        o.host = v != nullptr ? v : "";
    } else if (arg == "--port") {
        o.port = static_cast<std::uint16_t>(parse_number("--port", value(), 65535));
    } else if (arg == "--t0") {
        o.link.t0_ms = static_cast<std::uint32_t>(parse_number("--t0", value(), 255) * 1000UL);
    } else if (arg == "--t1") {
        o.link.t1_ms = static_cast<std::uint32_t>(parse_number("--t1", value(), 255) * 1000UL);
    } else if (arg == "--t2") {
        o.link.t2_ms = static_cast<std::uint32_t>(parse_number("--t2", value(), 255) * 1000UL);
    } else if (arg == "--t3") {
        o.link.t3_ms =
            static_cast<std::uint32_t>(parse_number("--t3", value(), kMaxSeconds) * 1000UL);
    } else if (arg == "--k") {
        o.link.k = static_cast<std::uint16_t>(parse_number("--k", value(), 32767));
    } else if (arg == "--w") {
        o.link.w = static_cast<std::uint16_t>(parse_number("--w", value(), 32767));
    } else if (arg == "--tolerant") {
        o.link.tolerant = 1;
    } else if (arg == "--verbose") {
        o.verbose = true;
    } else {
        return false;
    }
    return true;
}

/** @brief Prints an object in one line (for --verbose). */
inline void print_object(const char* prefix, const iec104_asdu_info* info, const iec104_object* o) {
    std::printf("%stype %u cause %u%s%s ca %u ioa %u value %g\n", prefix, info->type, info->cause,
                info->negative != 0U ? " negative" : "", info->test != 0U ? " test" : "",
                info->common_address, o->ioa, o->value);
}

/** @brief Prints one line of the report: "  label ...... text". */
inline void report_line(const char* label, const std::string& text) {
    std::string dots(label);
    dots.push_back(' ');
    while (dots.size() < 38U) {
        dots.push_back('.');
    }
    std::printf("  %s %s\n", dots.c_str(), text.c_str());
}

} // namespace selftest
