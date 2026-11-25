/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file config.cpp
 * @brief Configuration file: `key = value` lines -> iec104_config (see iec104.h).
 *
 * The file only fills the structs; the ranges of the standard are checked
 * when a server or client is created with them.
 *
 * Layer: API.
 */
#include "iec104/iec104.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) {
        return {};
    }
    return s.substr(b, s.find_last_not_of(ws) - b + 1U);
}

/** @brief Whole number in [lo, hi]. */
iec104_status parse_uint(const std::string& text, unsigned long hi, unsigned long& out) {
    char* end = nullptr;
    errno = 0;
    const unsigned long v = std::strtoul(text.c_str(), &end, 10);
    if (text.empty() || text[0] == '-' || *end != '\0' || errno != 0) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (v > hi) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    out = v;
    return IEC104_OK;
}

/** @brief Seconds (fractions allowed) -> milliseconds, at most 48 h. */
iec104_status parse_seconds(const std::string& text, std::uint32_t& ms) {
    char* end = nullptr;
    const double s = std::strtod(text.c_str(), &end);
    if (text.empty() || *end != '\0' || !std::isfinite(s)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (s < 0.0 || s > 48.0 * 3600.0) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    ms = static_cast<std::uint32_t>(std::lround(s * 1000.0));
    return IEC104_OK;
}

/** @brief 0/1, no/yes, false/true, off/on. */
iec104_status parse_flag(const std::string& text, int& out) {
    if (text == "1" || text == "yes" || text == "true" || text == "on") {
        out = 1;
        return IEC104_OK;
    }
    if (text == "0" || text == "no" || text == "false" || text == "off") {
        out = 0;
        return IEC104_OK;
    }
    return IEC104_ERR_INVALID_ARGUMENT;
}

template <typename T> iec104_status parse_small(const std::string& text, unsigned long hi, T& out) {
    unsigned long v = 0;
    const iec104_status st = parse_uint(text, hi, v);
    if (st == IEC104_OK) {
        out = static_cast<T>(v);
    }
    return st;
}

/** @brief Applies one key; IEC104_ERR_INVALID_ARGUMENT for an unknown key. */
iec104_status apply(const std::string& key, const std::string& value, iec104_config& c) {
    iec104_link_config& l = c.link;
    if (key == "host") {
        if (value.size() >= sizeof c.host) {
            return IEC104_ERR_OUT_OF_RANGE;
        }
        std::memcpy(static_cast<char*>(c.host), value.c_str(), value.size() + 1U);
        return IEC104_OK;
    }
    if (key == "port") {
        return parse_small(value, 65535, c.port);
    }
    if (key == "t0") {
        return parse_seconds(value, l.t0_ms);
    }
    if (key == "t1") {
        return parse_seconds(value, l.t1_ms);
    }
    if (key == "t2") {
        return parse_seconds(value, l.t2_ms);
    }
    if (key == "t3") {
        return parse_seconds(value, l.t3_ms);
    }
    if (key == "k") {
        return parse_small(value, 65535, l.k);
    }
    if (key == "w") {
        return parse_small(value, 65535, l.w);
    }
    if (key == "tolerant") {
        return parse_flag(value, l.tolerant);
    }
    if (key == "cot_length") {
        return parse_small(value, 255, l.cot_length);
    }
    if (key == "ca_length") {
        return parse_small(value, 255, l.ca_length);
    }
    if (key == "ioa_length") {
        return parse_small(value, 255, l.ioa_length);
    }
    if (key == "max_asdu_length") {
        return parse_small(value, 255, l.max_asdu_length);
    }
    if (key == "originator") {
        return parse_small(value, 255, l.originator);
    }
    if (key == "send_end_of_init") {
        return parse_flag(value, c.server.send_end_of_init);
    }
    if (key == "command_termination") {
        return parse_flag(value, c.server.command_termination);
    }
    if (key == "cause_of_init") {
        return parse_small(value, 127, c.server.cause_of_init);
    }
    if (key == "max_command_delay") {
        return parse_seconds(value, c.server.max_command_delay_ms);
    }
    if (key == "confirmation_timeout") {
        return parse_seconds(value, c.client.confirmation_timeout_ms);
    }
    if (key == "termination_timeout") {
        return parse_seconds(value, c.client.termination_timeout_ms);
    }
    if (key == "expect_termination") {
        return parse_flag(value, c.client.expect_termination);
    }
    return IEC104_ERR_INVALID_ARGUMENT;
}

} // namespace

extern "C" {

void iec104_config_init(iec104_config* config) {
    if (config == nullptr) {
        return;
    }
    *config = iec104_config{};
    iec104_link_config_init(&config->link);
    iec104_server_options_init(&config->server);
    iec104_client_options_init(&config->client);
    config->port = IEC104_DEFAULT_PORT;
}

iec104_status iec104_config_load(const char* path, iec104_config* config, int* error_line) {
    if (error_line != nullptr) {
        *error_line = 0;
    }
    if (path == nullptr || config == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    std::ifstream in(path);
    if (!in) {
        return IEC104_ERR_IO;
    }
    // Work on a copy, so that a broken file leaves the caller's config unchanged.
    iec104_config c = *config;
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        const std::size_t comment = line.find_first_of("#;");
        const std::string text = trim(line.substr(0, comment));
        if (text.empty()) {
            continue;
        }
        const std::size_t eq = text.find('=');
        iec104_status st = IEC104_ERR_INVALID_ARGUMENT;
        if (eq != std::string::npos) {
            st = apply(trim(text.substr(0, eq)), trim(text.substr(eq + 1U)), c);
        }
        if (st != IEC104_OK) {
            if (error_line != nullptr) {
                *error_line = number;
            }
            return st;
        }
    }
    *config = c;
    return IEC104_OK;
}

} // extern "C"
