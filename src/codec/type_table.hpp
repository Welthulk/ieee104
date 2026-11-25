/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file type_table.hpp
 * @brief Table of the type identifications: what an information object of each type contains.
 *
 * The type identification (first octet of an ASDU) determines the structure
 * of every information object in it. This table lists, per type, the
 * information elements in wire order, whether the object carries a time tag,
 * and the direction. The ASDU codec (asdu.hpp) is driven entirely by this
 * table; it contains no per-type code.
 *
 * Scope of version 1: the IEC 104 types 1-21 (odd), 20, 21, 30-40, 45-51,
 * 58-64, 70, 100-107, 110-113, and the IEC 101 types with CP24Time2a
 * (2-16 even, 17-19), which only the tolerant profile accepts.
 * Not in scope: file transfer 120-127, security types of IEC TS 60870-5-7.
 * Types 136-255 are private (docs/02_Technik_und_Quellen.md); they are not interpreted but handed
 * to the caller as raw octets.
 *
 * Sources: type groups and element lists from docs/02_Technik_und_Quellen.md ("Typkennungen",
 * element lengths citing the Wireshark dissector); names and
 * element order confirmed by tests/data/lib60870_asdu_reference.txt.
 * TODO(norm): type identification table, clause unknown.
 *
 * Layer: codec (pure data and lookups, no I/O, no allocation).
 */
#pragma once

#include "codec/elements.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace iec104::codec {

/** @brief Direction a type is defined for. */
enum class Direction : std::uint8_t {
    Monitor, ///< Monitor direction: controlled station -> controlling station (process data).
    Control, ///< Control direction: commands, system commands, parameters.
};

/** @brief Time tag at the end of every information object of a type. */
enum class TimeTag : std::uint8_t {
    None, ///< No time tag.
    Cp24, ///< CP24Time2a, IEC 101 only (rejected in the strict IEC 104 profile).
    Cp56, ///< CP56Time2a.
};

/** @brief Maximum number of elements in one information object (M_EP_TE_1: SPE QDP CP16 CP56). */
inline constexpr std::size_t kMaxElements = 4U;

/**
 * @brief Description of one type identification.
 *
 * Example: M_ME_TE_1 (35) = { SVA, QDS, CP56 }, time tag CP56, monitor
 * direction; one object is IOA + 2 + 1 + 7 octets.
 */
struct TypeInfo {
    std::uint8_t id;            ///< Type identification.
    const char* name;           ///< Name as in the standard and in Wireshark, e.g. "M_SP_NA_1".
    Direction direction;        ///< Direction the type is defined for.
    TimeTag time_tag;           ///< Time tag; when not None it is the last element.
    std::uint8_t element_count; ///< Number of used entries in @ref elements (0 for C_RD_NA_1).
    std::array<ElementKind, kMaxElements> elements; ///< Elements in wire order, time tag included.
};

/**
 * @brief Looks up a type identification.
 *
 * @param type_id Type identification octet.
 * @return Pointer to the static table entry, or NULL if the type is not in
 *         the table (unknown, private, or out of scope).
 */
const TypeInfo* find_type(std::uint8_t type_id) noexcept;

/**
 * @brief All table entries, ordered by type identification.
 *
 * For tests and diagnostics (e.g. listing the supported types).
 *
 * @param count Receives the number of entries.
 * @return Pointer to the first entry. Never NULL.
 */
const TypeInfo* all_types(std::size_t& count) noexcept;

/**
 * @brief Length of the elements of one information object, without IOA.
 *
 * Example: M_SP_TB_1 (30) -> 1 (SIQ) + 7 (CP56Time2a) = 8.
 *
 * @param type A table entry.
 * @return Sum of the element lengths.
 */
std::size_t object_length(const TypeInfo& type) noexcept;

/** @brief First type identification of the private range (docs/02_Technik_und_Quellen.md: 136-255).
 */
inline constexpr std::uint8_t kFirstPrivateType = 136U;

/**
 * @brief Tells whether a type identification is in the private range 136..255.
 *
 * Private types are defined by the parties of a project, not by the
 * standard. The codec does not interpret them.
 *
 * @param type_id Type identification octet.
 * @return true for 136..255.
 */
constexpr bool is_private_type(std::uint8_t type_id) noexcept {
    return type_id >= kFirstPrivateType;
}

/**
 * @brief Tells whether a cause of transmission is allowed for a type.
 *
 * The matrix is defined in clause 9.5 (docs/05_Normfragen_und_Festlegungen.md,
 * N3). The table behind this function is empty, so every combination is
 * allowed; filling it in changes only this function and its table.
 * TODO(norm): 9.5, matrix of type identification and cause of transmission.
 *
 * @param type_id Type identification.
 * @param cause   Cause of transmission, 0..63.
 * @return true if the combination is allowed (currently always).
 */
bool is_cause_allowed(std::uint8_t type_id, std::uint8_t cause) noexcept;

} // namespace iec104::codec
