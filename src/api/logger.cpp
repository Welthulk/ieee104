/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file logger.cpp
 * @brief Hex dump for the trace level of the logger.
 *
 * Layer: API.
 */
#include "api/logger.hpp"

#include <string_view>

namespace iec104::api {

std::string Logger::hex(const std::uint8_t* data, std::size_t length) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    text.reserve(length * 3U);
    for (std::size_t i = 0; i < length; ++i) {
        if (i > 0U) {
            text.push_back(' ');
        }
        text.push_back(kDigits[data[i] >> 4U]);
        text.push_back(kDigits[data[i] & 0x0FU]);
    }
    return text;
}

} // namespace iec104::api
