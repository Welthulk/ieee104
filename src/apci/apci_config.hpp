/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file apci_config.hpp
 * @brief Parameters of the APCI layer: timers t0..t3, window sizes k and w.
 *
 * IEC 60870-5-104 controls a connection with four timers and two counters:
 *
 *   t0  time-out of connection establishment (TCP connect)
 *   t1  time-out of send or test APDUs: an I-frame or a U-frame "act" that is
 *       not acknowledged / confirmed within t1 closes the connection
 *   t2  time-out for acknowledges when there is no own data to send: a
 *       received I-frame is acknowledged with an S-frame at the latest after t2
 *   t3  time-out for sending test frames in case of a long idle state
 *   k   maximum number of sent, unacknowledged I-frames; the sender stops at k
 *   w   the receiver acknowledges at the latest after w received I-frames
 *
 * Defaults and ranges (source: docs/02_Technik_und_Quellen.md, section "Verbindung und Timer",
 * citing the interoperability list of the standard as reproduced in public
 * documentation): t0 30 s, t1 15 s, t2 10 s, t3 20 s, each 1..255 s
 * (t3 recommended 1 s .. 48 h); k 12, w 8, each 1..32767; t2 < t1 and
 * w <= 2/3 k are recommendations.
 * TODO(norm): timer and k/w definitions, clause unknown (IEC 60870-5-104 clause 5/9).
 *
 * Deviations accepted on purpose (docs/02_Technik_und_Quellen.md, "Verbindung und Timer"):
 *   - t3 = 0 switches the idle test off; some peers require it. Only allowed
 *     with strict = false (docs/05_Normfragen_und_Festlegungen.md, E3).
 *   - violations of t2 < t1 and w <= 2/3 k are reported as warnings by
 *     default, because some peers use e.g. t1 = 250 s, t2 = 240 s (allowed)
 *     or other combinations; whether they are errors is configurable (E2).
 *
 * Times are milliseconds so that tests with simulated time can use any
 * value; the allowed range is still whole seconds as above.
 *
 * Layer: apci (pure data and checks, no I/O).
 */
#pragma once

#include "iec104/iec104.h"

#include <cstdint>

namespace iec104::apci {

/** @brief Role of this side of the connection. */
enum class Role : std::uint8_t {
    Controlling, ///< Controlling station (client): opens TCP, sends STARTDT act.
    Controlled,  ///< Controlled station (server): accepts TCP, answers STARTDT act.
};

/** @brief How violations of the recommendations t2 < t1 and w <= 2/3 k are handled. */
enum class PlausibilityCheck : std::uint8_t {
    Warn,  ///< Accepted, reported in ConfigWarnings (default, docs/02_Technik_und_Quellen.md).
    Error, ///< Rejected with IEC104_ERR_INVALID_ARGUMENT.
    Off,   ///< Not checked, not reported.
};

/** @brief Milliseconds per second, for readable defaults. */
inline constexpr std::uint32_t kSecond = 1000U;
/** @brief Smallest t0, t1, t2 and non-zero t3: 1 s. */
inline constexpr std::uint32_t kMinTimer = 1U * kSecond;
/** @brief Largest t0, t1, t2: 255 s. */
inline constexpr std::uint32_t kMaxTimer = 255U * kSecond;
/** @brief Largest t3: 48 h. */
inline constexpr std::uint32_t kMaxT3 = 48U * 3600U * kSecond;
/** @brief Largest k and w: 32767, half the sequence number space. */
inline constexpr std::uint16_t kMaxWindow = 32767U;

/** @brief Parameters of one connection. Default-constructed = values of the standard. */
struct ApciConfig {
    Role role{Role::Controlling};       ///< Client or server side.
    std::uint32_t t0_ms{30U * kSecond}; ///< Connection establishment (used by the transport).
    std::uint32_t t1_ms{15U * kSecond}; ///< Send or test APDU time-out.
    std::uint32_t t2_ms{10U * kSecond}; ///< Acknowledge time-out without own data.
    std::uint32_t t3_ms{20U * kSecond}; ///< Idle time before a test frame; 0 = off (tolerant only).
    std::uint16_t k{12};                ///< Max. unacknowledged sent I-frames.
    std::uint16_t w{8};                 ///< Acknowledge after at most w received I-frames.
    bool strict{true};                  ///< Strict (default) or tolerant handling of deviations.
    PlausibilityCheck plausibility{PlausibilityCheck::Warn}; ///< See PlausibilityCheck.
};

/** @brief Recommendations a valid configuration does not meet (only with PlausibilityCheck::Warn).
 */
struct ConfigWarnings {
    bool t2_not_below_t1{false};      ///< t2 >= t1.
    bool w_above_two_thirds_k{false}; ///< 3 * w > 2 * k.
};

/**
 * @brief Checks a configuration.
 *
 * @param config   Configuration to check.
 * @param warnings Receives the violated recommendations (all false unless
 *                 config.plausibility is Warn and a recommendation is violated).
 * @return IEC104_OK if the configuration can be used (warnings possible).
 * @return IEC104_ERR_OUT_OF_RANGE if a timer, k or w is outside its range,
 *         or t3 = 0 in strict mode.
 * @return IEC104_ERR_INVALID_ARGUMENT if a recommendation is violated and
 *         config.plausibility is Error.
 */
iec104_status validate(const ApciConfig& config, ConfigWarnings& warnings) noexcept;

} // namespace iec104::apci
