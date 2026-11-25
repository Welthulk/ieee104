/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file command_table.cpp
 * @brief Implementation of the command table.
 *
 * Layer: session (no I/O).
 */
#include "session/command_table.hpp"

namespace iec104::session {

bool is_command_type(std::uint8_t type) noexcept {
    return (type >= 45U && type <= 51U) || (type >= 58U && type <= 64U);
}

bool is_select(std::uint8_t type, const codec::InformationObject& object) noexcept {
    switch (type) {
    case 45:
    case 58:
        return object.sco.select;
    case 46:
    case 59:
        return object.dco.select;
    case 47:
    case 60:
        return object.rco.select;
    case 48:
    case 49:
    case 50:
    case 61:
    case 62:
    case 63:
        return object.qos.select;
    default:
        return false;
    }
}

iec104_status CommandTable::set(std::uint16_t common_address, std::uint32_t ioa,
                                const CommandPoint& point) {
    if (!is_command_type(point.type)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    // Bitstring commands (51, 64) carry no S/E bit and cannot be selected.
    const bool bitstring = point.type == 51U || point.type == 64U;
    if (bitstring && point.model == CommandModel::SelectExecute) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    points_[{common_address, ioa}] = point;
    return IEC104_OK;
}

const CommandPoint* CommandTable::find(std::uint16_t common_address, std::uint32_t ioa) const {
    const auto it = points_.find({common_address, ioa});
    return it != points_.end() ? &it->second : nullptr;
}

} // namespace iec104::session
