/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file tls.cpp
 * @brief Parts of the TLS interface that do not depend on a backend (see tls.hpp).
 *
 * Layer: transport.
 */
#include "transport/tls.hpp"

#include <cctype>

namespace iec104::transport {

std::string normalize_fingerprint(const std::string& text) {
    constexpr std::size_t kOctets = 32; // SHA-256
    std::string hex;
    for (const char c : text) {
        if (c == ':' || c == ' ') {
            continue;
        }
        if (std::isxdigit(static_cast<unsigned char>(c)) == 0) {
            return {};
        }
        hex += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (hex.size() != 2U * kOctets) {
        return {};
    }
    std::string out;
    for (std::size_t i = 0; i < hex.size(); i += 2U) {
        if (i > 0U) {
            out += ':';
        }
        out += hex.substr(i, 2U);
    }
    return out;
}

} // namespace iec104::transport
