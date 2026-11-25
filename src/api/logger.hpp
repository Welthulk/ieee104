/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file logger.hpp
 * @brief Log output of a server or client handle to the application's callback.
 *
 * Messages are built only if their level is enabled, so logging costs
 * nothing when it is switched off.
 *
 * Layer: API (internal header).
 */
#pragma once

#include "iec104/iec104.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace iec104::api {

/** @brief Log callback with level filter. */
class Logger {
  public:
    /** @brief Sets the callback (NULL = off) and the highest level passed on. */
    void set(iec104_log_level max_level, iec104_log_fn fn, void* user) noexcept {
        max_level_ = max_level;
        fn_ = fn;
        user_ = user;
    }

    /** @brief True if a message of @p level would be passed on. */
    [[nodiscard]] bool enabled(iec104_log_level level) const noexcept {
        return fn_ != nullptr && level <= max_level_;
    }

    /** @brief Passes @p message on if @p level is enabled. */
    void write(iec104_log_level level, std::uint32_t connection, const std::string& message) const {
        if (enabled(level)) {
            fn_(user_, level, connection, message.c_str());
        }
    }

    /** @brief Octets as hex, e.g. "68 04 07 00 00 00". */
    static std::string hex(const std::uint8_t* data, std::size_t length);

  private:
    iec104_log_level max_level_{IEC104_LOG_ERROR};
    iec104_log_fn fn_{nullptr};
    void* user_{nullptr};
};

} // namespace iec104::api
