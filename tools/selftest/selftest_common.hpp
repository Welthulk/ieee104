/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file selftest_common.hpp
 * @brief Shared code of the self-test programs: options, ASDUs, output.
 *
 * The self-test programs exercise the library over real TCP:
 *
 *   iec104_selftest_server  (controlled station)
 *     - sends N spontaneous measured values M_ME_NB_1 (IOA 100, values
 *       0..N-1, cause 3) on every connection after STARTDT;
 *     - confirms every set-point command C_SE_NB_1 it receives by sending
 *       it back with cause 7 (activation confirmation).
 *
 *   iec104_selftest_client  (controlling station)
 *     - connects, sends STARTDT, sends M set-point commands C_SE_NB_1
 *       (IOA 5000, values 0..M-1, cause 6);
 *     - expects N measured values in order and M confirmations;
 *     - stays idle for a while (link test with TESTFR after t3), sends
 *       STOPDT, closes, and prints a report.
 *
 * Both use the public C API (iec104.h) for the connection. ASDUs are built
 * and checked with the internal codec, because the public functions for
 * ASDU values come with the session layer.
 */
#pragma once

#include "codec/asdu.hpp"
#include "iec104/iec104.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace selftest {

/** @brief Type and address of the values the server sends. */
inline constexpr std::uint8_t kMeasurementType = 11; // M_ME_NB_1, scaled value
inline constexpr std::uint32_t kMeasurementIoa = 100;
/** @brief Type and address of the commands the client sends. */
inline constexpr std::uint8_t kCommandType = 49; // C_SE_NB_1, set point scaled value
inline constexpr std::uint32_t kCommandIoa = 5000;
inline constexpr std::uint16_t kCommonAddress = 1;
inline constexpr std::uint8_t kCauseSpontaneous = 3;
inline constexpr std::uint8_t kCauseActivation = 6;
inline constexpr std::uint8_t kCauseActivationCon = 7;

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
                "  --verbose        print every state change and ASDU\n"
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

/** @brief Builds a one-object ASDU with a scaled value; returns its length (0 on error). */
inline std::size_t build_asdu(std::uint8_t type, std::uint8_t cause, std::uint32_t ioa,
                              std::int16_t value, std::uint8_t* out, std::size_t size) {
    iec104::codec::AsduHeader h{};
    h.type_id = type;
    h.cause = cause;
    h.common_address = kCommonAddress;
    iec104::codec::InformationObject o{};
    o.ioa = ioa;
    o.sva = value;
    std::size_t len = 0;
    return iec104::codec::encode_asdu(h, &o, 1, iec104::codec::kIec104Profile, out, size, len) ==
                   IEC104_OK
               ? len
               : 0U;
}

/** @brief The parts of a received one-object ASDU the self-test checks. */
struct Received {
    bool ok{false};
    std::uint8_t type{0};
    std::uint8_t cause{0};
    std::uint32_t ioa{0};
    std::int16_t value{0};
};

/** @brief Decodes a received ASDU of the self-test (types 11 and 49). */
inline Received parse_asdu(const std::uint8_t* asdu, std::size_t length) {
    Received r{};
    iec104::codec::InformationObject objects[1]{};
    iec104::codec::DecodedAsdu d{};
    if (iec104::codec::decode_asdu(asdu, length, iec104::codec::kIec104Profile, objects, 1, d) !=
            IEC104_OK ||
        d.object_count != 1U) {
        return r;
    }
    r.ok = true;
    r.type = d.header.type_id;
    r.cause = d.header.cause;
    r.ioa = objects[0].ioa;
    r.value = objects[0].sva;
    return r;
}

/** @brief Prints one line of the report: "  label ...... text". */
inline void report_line(const char* label, const std::string& text) {
    std::string dots(label);
    dots.push_back(' ');
    while (dots.size() < 34U) {
        dots.push_back('.');
    }
    std::printf("  %s %s\n", dots.c_str(), text.c_str());
}

} // namespace selftest
