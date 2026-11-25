/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file process_image.hpp
 * @brief The data points of a controlled station, as needed to answer requests.
 *
 * A controlled station answers an interrogation with the current value of
 * every data point (or of the points of one group), a counter interrogation
 * with its counters, and a read command with one point. The process image
 * keeps these values. It is filled and updated by the application; the
 * session layer reads it.
 *
 * A point is identified by common address (station) and IOA. It is stored
 * with its type without time tag (e.g. 13 for a point that changes as 36),
 * because an interrogation reports the current state, not an event
 * (docs/05_Normfragen_und_Festlegungen.md, N8).
 * TODO(norm): types used in the answer to an interrogation, clause 7.
 *
 * Layer: session (no I/O).
 */
#pragma once

#include "codec/asdu.hpp"
#include "iec104/iec104.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace iec104::session {

/** @brief One data point of the image. */
struct Point {
    std::uint16_t common_address{0};
    std::uint8_t type{0};             ///< Type without time tag: 1, 3, 5, 7, 9, 11, 13, 15, 21.
    codec::InformationObject value{}; ///< Current value; value.ioa is the address.
    std::uint16_t groups{0};          ///< Bit n = interrogation group n + 1 (counters: group 1..4).
};

/** @brief Data points of one or more stations (see file comment). */
class ProcessImage {
  public:
    /**
     * @brief Type without time tag for a monitor type: 30 -> 1, 31 -> 3, .. 37 -> 15.
     * @return The base type, or 0 if @p type is not a point type of the image.
     */
    static std::uint8_t base_type(std::uint8_t type) noexcept;

    /**
     * @brief Adds or replaces a point.
     * @return IEC104_OK, or IEC104_ERR_INVALID_ARGUMENT if @p type is not a point type.
     */
    iec104_status set(std::uint16_t common_address, std::uint8_t type,
                      const codec::InformationObject& value, std::uint16_t groups);

    /**
     * @brief Updates the value of an existing point.
     * @param type Type of the update; its base type must be the type of the point.
     * @return IEC104_OK, or IEC104_ERR_INVALID_ARGUMENT for an unknown point or wrong type.
     */
    iec104_status update(std::uint16_t common_address, std::uint8_t type,
                         const codec::InformationObject& value);

    /** @brief The point at (@p common_address, @p ioa), or NULL. */
    [[nodiscard]] const Point* find(std::uint16_t common_address, std::uint32_t ioa) const;

    /** @brief True if at least one point has @p common_address. */
    [[nodiscard]] bool has_station(std::uint16_t common_address) const;

    /** @brief True if the image has no points at all. */
    [[nodiscard]] bool empty() const { return points_.empty(); }

    /** @brief All common addresses of the image, ascending. */
    [[nodiscard]] std::vector<std::uint16_t> stations() const;

    /**
     * @brief Points of one station for an interrogation, sorted by type, then IOA.
     *
     * @param counters false: all points except counters (type 15); true: only counters.
     * @param group    0 = all points; 1..16 = only points of that group.
     */
    [[nodiscard]] std::vector<Point> select(std::uint16_t common_address, bool counters,
                                            unsigned group) const;

  private:
    std::map<std::pair<std::uint16_t, std::uint32_t>, Point> points_;
};

} // namespace iec104::session
