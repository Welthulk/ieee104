/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file command_table.hpp
 * @brief Command points of a controlled station: which commands exist and how they run.
 *
 * A command point is identified by common address and IOA and fixes:
 *   - the type identification the point accepts (with or without time tag,
 *     e.g. 45 or 58; some peers send only time-tagged commands);
 *   - the command model: direct execute, or select before operate (the
 *     controlling station first selects, then executes the same command);
 *   - for select before operate, how long a selection stays valid.
 *
 * The model per point is a parameter of the station (intern/02: "Befehlsmodell
 * je IOA"). An empty table means that the application decides about every
 * command itself (behaviour without command points).
 * TODO(norm): select and execute procedure, clause 7 (docs/05_Normfragen_und_Festlegungen.md, N8).
 *
 * Layer: session (no I/O).
 */
#pragma once

#include "codec/asdu.hpp"
#include "iec104/iec104.h"

#include <cstdint>
#include <map>
#include <utility>

namespace iec104::session {

/** @brief How a command point is operated. */
enum class CommandModel : std::uint8_t {
    Direct,        ///< Execute directly; a select is rejected.
    SelectExecute, ///< Select first, then execute the same command within the timeout.
};

/** @brief One command point. */
struct CommandPoint {
    std::uint8_t type{0}; ///< Accepted type identification (45..51, 58..64).
    CommandModel model{CommandModel::Direct};
    std::uint32_t select_timeout_ms{0}; ///< Validity of a selection (select before operate).
};

/** @brief Command points of one or more stations. */
class CommandTable {
  public:
    /**
     * @brief Adds or replaces a command point.
     * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT if @p point.type is not a
     *         command type, or select before operate is asked for a bitstring
     *         command (types 51 and 64 have no select bit).
     */
    iec104_status set(std::uint16_t common_address, std::uint32_t ioa, const CommandPoint& point);

    /** @brief The point at (@p common_address, @p ioa), or NULL. */
    [[nodiscard]] const CommandPoint* find(std::uint16_t common_address, std::uint32_t ioa) const;

    /** @brief True if no command point is defined. */
    [[nodiscard]] bool empty() const { return points_.empty(); }

  private:
    std::map<std::pair<std::uint16_t, std::uint32_t>, CommandPoint> points_;
};

/** @brief True for the command types 45..51 and 58..64. */
bool is_command_type(std::uint8_t type) noexcept;

/** @brief S/E bit of a command object: true = select; false for types without S/E (51, 64). */
bool is_select(std::uint8_t type, const codec::InformationObject& object) noexcept;

} // namespace iec104::session
