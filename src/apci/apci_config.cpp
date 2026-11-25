/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file apci_config.cpp
 * @brief Range and plausibility checks of the APCI parameters.
 *
 * Layer: apci (pure functions, no I/O).
 */
#include "apci/apci_config.hpp"

namespace iec104::apci {
namespace {

bool in_timer_range(std::uint32_t ms) noexcept {
    return ms >= kMinTimer && ms <= kMaxTimer;
}

} // namespace

iec104_status validate(const ApciConfig& config, ConfigWarnings& warnings) noexcept {
    warnings = ConfigWarnings{};

    if (!in_timer_range(config.t0_ms) || !in_timer_range(config.t1_ms) ||
        !in_timer_range(config.t2_ms)) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    // t3 = 0 switches the idle test off; only tolerated outside strict mode.
    const bool t3_off = config.t3_ms == 0U;
    if ((t3_off && config.strict) ||
        (!t3_off && (config.t3_ms < kMinTimer || config.t3_ms > kMaxT3))) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    if (config.k < 1U || config.k > kMaxWindow || config.w < 1U || config.w > kMaxWindow) {
        return IEC104_ERR_OUT_OF_RANGE;
    }

    if (config.plausibility == PlausibilityCheck::Off) {
        return IEC104_OK;
    }
    ConfigWarnings found{};
    found.t2_not_below_t1 = config.t2_ms >= config.t1_ms;
    // w <= 2/3 k, in integers: 3 w <= 2 k (no rounding question).
    found.w_above_two_thirds_k = 3U * config.w > 2U * config.k;
    if (!found.t2_not_below_t1 && !found.w_above_two_thirds_k) {
        return IEC104_OK;
    }
    if (config.plausibility == PlausibilityCheck::Error) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    warnings = found;
    return IEC104_OK;
}

} // namespace iec104::apci
