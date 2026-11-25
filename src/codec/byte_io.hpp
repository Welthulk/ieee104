/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file byte_io.hpp
 * @brief Little-endian read and write helpers for the codec layer.
 *
 * IEC 60870-5-104 transmits every multi-octet value least significant octet
 * first (called "mode 1" in interoperability lists; source: docs/03, ASDU
 * section). These helpers are the only place in the codec that assembles or
 * splits integers, so byte order is handled in exactly one spot.
 *
 * Layer: codec (pure functions, no I/O, no allocation). Internal header, not
 * part of the public API. The caller guarantees that the pointers are valid for
 * the given number of octets; the helpers do no bounds checking themselves.
 *
 * TODO(norm): transmission order of multi-octet values, clause unknown.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace iec104::codec::detail {

/**
 * @brief Writes a 16-bit value as two octets, low octet first.
 *
 * Example: 0x1234 -> 34 12
 *
 * @param p Destination, at least 2 writable octets.
 * @param v Value to write.
 */
inline void put_u16_le(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFFU);
    p[1] = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
}

/**
 * @brief Reads a 16-bit value from two octets, low octet first.
 *
 * Example: 34 12 -> 0x1234
 *
 * @param p Source, at least 2 readable octets.
 * @return The assembled value.
 */
inline std::uint16_t get_u16_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(static_cast<unsigned>(p[0]) |
                                      (static_cast<unsigned>(p[1]) << 8U));
}

/**
 * @brief Writes the lowest @p n octets of @p v, low octet first.
 *
 * Used for fields whose length depends on the ASDU profile (common address
 * 1..2 octets, information object address 1..3 octets). Higher octets of
 * @p v are silently dropped; range checks belong to the caller.
 *
 * Example: v = 0x001267, n = 3 -> 67 12 00
 *
 * @param p Destination, at least @p n writable octets.
 * @param v Value to write.
 * @param n Number of octets, 1..4.
 */
inline void put_uint_le(std::uint8_t* p, std::uint32_t v, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        p[i] = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU);
    }
}

/**
 * @brief Reads @p n octets, low octet first, into an unsigned 32-bit value.
 *
 * Example: 67 12 00, n = 3 -> 0x001267 (4711)
 *
 * @param p Source, at least @p n readable octets.
 * @param n Number of octets, 1..4.
 * @return The assembled value; octets above @p n are zero.
 */
inline std::uint32_t get_uint_le(const std::uint8_t* p, std::size_t n) noexcept {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        v |= static_cast<std::uint32_t>(p[i]) << (8U * i);
    }
    return v;
}

} // namespace iec104::codec::detail
