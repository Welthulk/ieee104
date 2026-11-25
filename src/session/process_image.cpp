/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file process_image.cpp
 * @brief Implementation of the process image.
 *
 * Layer: session (no I/O).
 */
#include "session/process_image.hpp"

#include <algorithm>

namespace iec104::session {
namespace {
constexpr std::uint8_t kIntegratedTotals = 15;
} // namespace

std::uint8_t ProcessImage::base_type(std::uint8_t type) noexcept {
    switch (type) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 9:
    case 11:
    case 13:
    case 15:
    case 21:
        return type;
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
        // The time-tagged monitor types 30..37 correspond in order to the
        // odd types 1..15 (docs/02_Technik_und_Quellen.md, "Typkennungen").
        return static_cast<std::uint8_t>(((type - 30U) * 2U) + 1U);
    default:
        return 0;
    }
}

iec104_status ProcessImage::set(std::uint16_t common_address, std::uint8_t type,
                                const codec::InformationObject& value, std::uint16_t groups) {
    const std::uint8_t base = base_type(type);
    if (base == 0U) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    Point p{};
    p.common_address = common_address;
    p.type = base;
    p.value = value;
    p.groups = groups;
    points_[{common_address, value.ioa}] = p;
    return IEC104_OK;
}

iec104_status ProcessImage::update(std::uint16_t common_address, std::uint8_t type,
                                   const codec::InformationObject& value) {
    const auto it = points_.find({common_address, value.ioa});
    if (it == points_.end() || base_type(type) != it->second.type) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    it->second.value = value;
    return IEC104_OK;
}

const Point* ProcessImage::find(std::uint16_t common_address, std::uint32_t ioa) const {
    const auto it = points_.find({common_address, ioa});
    return it != points_.end() ? &it->second : nullptr;
}

bool ProcessImage::has_station(std::uint16_t common_address) const {
    // Keys are sorted by common address first, so the first key not below
    // (address, 0) tells whether the station exists.
    const auto it = points_.lower_bound({common_address, 0U});
    return it != points_.end() && it->first.first == common_address;
}

std::vector<std::uint16_t> ProcessImage::stations() const {
    std::vector<std::uint16_t> result;
    for (const auto& entry : points_) {
        if (result.empty() || result.back() != entry.first.first) {
            result.push_back(entry.first.first);
        }
    }
    return result;
}

std::vector<Point> ProcessImage::select(std::uint16_t common_address, bool counters,
                                        unsigned group) const {
    std::vector<Point> result;
    for (auto it = points_.lower_bound({common_address, 0U});
         it != points_.end() && it->first.first == common_address; ++it) {
        const Point& p = it->second;
        if ((p.type == kIntegratedTotals) != counters) {
            continue;
        }
        if (group != 0U && (p.groups & (1U << (group - 1U))) == 0U) {
            continue;
        }
        result.push_back(p);
    }
    // Grouping by type lets one ASDU carry many points of the same type.
    std::stable_sort(result.begin(), result.end(),
                     [](const Point& a, const Point& b) { return a.type < b.type; });
    return result;
}

} // namespace iec104::session
