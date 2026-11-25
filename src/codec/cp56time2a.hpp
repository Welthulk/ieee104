/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file cp56time2a.hpp
 * @brief CP56Time2a: the 7-octet time tag of IEC 60870-5-101/104.
 *
 * CP56Time2a carries date and time with millisecond resolution. The IEC 104
 * time-tagged types (30-40, 58-64, 107) use it. Whether the 3-octet CP24Time2a
 * types of IEC 101 are excluded from IEC 104 is not proven by the standard text
 * yet (docs/05_Normfragen_und_Festlegungen.md, N4).
 *
 * Layout (octets 1..7, little endian where a field spans two octets):
 *
 *   octet 1-2:  milliseconds incl. seconds, 0..59999
 *   octet 3:    bit 7 IV (invalid) | bit 6 RES1 | bits 5..0 minute 0..59
 *   octet 4:    bit 7 SU (summer time) | bits 6..5 RES2 | bits 4..0 hour 0..23
 *   octet 5:    bits 7..5 day of week 1..7 (0 = not used) | bits 4..0 day of month 1..31
 *   octet 6:    bits 7..4 RES3 | bits 3..0 month 1..12
 *   octet 7:    bit 7 RES4 | bits 6..0 year 0..99
 *
 * Example: 2B 17 01 0B 19 0B 1A = 2026-11-25 11:01:05.931, day of week not
 * used, IV = 0, SU = 0 (time tag of a C_TS_TA_1 test command in
 * tests/data/lib60870_capture.txt).
 *
 * The standard does NOT define whether the time is UTC or local time; that is
 * an item of the interoperability list. This type therefore carries no time
 * zone. Interpretation belongs to the connection profile (time base, see
 * docs/02_Technik_und_Quellen.md, "Zeit").
 *
 * Sources: docs/02_Technik_und_Quellen.md, section "Informationselemente" (citing the Wireshark
 * IEC 104 dissector).
 * TODO(norm): CP56Time2a definition, clause unknown (IEC 60870-5 series).
 *
 * Layer: codec (pure functions, no I/O, no allocation).
 */
#pragma once

#include "iec104/iec104.h"

#include <cstddef>
#include <cstdint>

namespace iec104::codec {

/** @brief Length of a CP56Time2a time tag in octets. */
inline constexpr std::size_t kCp56Time2aLength = 7U;

/**
 * @brief Decoded CP56Time2a time tag.
 *
 * Plain field values, no time zone and no calendar arithmetic. A
 * default-constructed value is 00:00:00.000 on day 1, month 1, year 0.
 */
struct Cp56Time2a {
    std::uint16_t milliseconds{0}; ///< Seconds * 1000 + milliseconds, 0..59999.
    std::uint8_t minute{0};        ///< 0..59.
    std::uint8_t hour{0};          ///< 0..23.
    std::uint8_t day_of_month{1};  ///< 1..31.
    std::uint8_t day_of_week{
        0}; ///< 1..7, 0 = not used. TODO(norm): numbering of weekdays, clause unknown.
    std::uint8_t month{1};   ///< 1..12.
    std::uint8_t year{0};    ///< 0..99, the century is not transmitted.
    bool invalid{false};     ///< IV: the time is not valid (e.g. clock not synchronised).
    bool summer_time{false}; ///< SU: the time is summer (daylight saving) time.
    /*
     * Reserved bits, kept as received so that decoding and re-encoding gives
     * identical octets (lossless decoding, see elements.hpp). A sender that
     * does not set them leaves them 0. Some implementations give RES1 a
     * meaning of their own; this library attaches none.
     * TODO(norm): meaning of the reserved bits, clause unknown.
     */
    bool res1{false};     ///< RES1, minute octet bit 6.
    std::uint8_t res2{0}; ///< RES2, hour octet bits 5..6, 0..3.
    std::uint8_t res3{0}; ///< RES3, month octet bits 4..7, 0..15.
    bool res4{false};     ///< RES4, year octet bit 7.

    /** @brief Field-wise equality, including IV, SU and the reserved bits. */
    friend bool operator==(const Cp56Time2a& a, const Cp56Time2a& b) noexcept {
        return a.milliseconds == b.milliseconds && a.minute == b.minute && a.hour == b.hour &&
               a.day_of_month == b.day_of_month && a.day_of_week == b.day_of_week &&
               a.month == b.month && a.year == b.year && a.invalid == b.invalid &&
               a.summer_time == b.summer_time && a.res1 == b.res1 && a.res2 == b.res2 &&
               a.res3 == b.res3 && a.res4 == b.res4;
    }
    /** @brief Negation of operator==. */
    friend bool operator!=(const Cp56Time2a& a, const Cp56Time2a& b) noexcept { return !(a == b); }
};

/**
 * @brief Checks every field against its range.
 *
 * Only the ranges listed at the struct members are checked (including the
 * widths of RES2 and RES3). Calendar validity
 * (e.g. 31 February) and the consistency of the day of week are not checked,
 * because the year is only known modulo 100.
 *
 * @param t Time tag to check.
 * @return IEC104_OK if every field is in range.
 * @return IEC104_ERR_OUT_OF_RANGE otherwise.
 */
iec104_status validate(const Cp56Time2a& t) noexcept;

/**
 * @brief Encodes a time tag into 7 octets.
 *
 * Encoding is strict: out-of-range fields are rejected, never truncated.
 *
 * @param t        Time tag to encode.
 * @param out      Destination buffer.
 * @param out_size Size of @p out, at least 7.
 * @return IEC104_OK on success.
 * @return IEC104_ERR_INVALID_ARGUMENT if @p out is NULL.
 * @return IEC104_ERR_BUFFER_TOO_SMALL if @p out_size is less than 7.
 * @return IEC104_ERR_OUT_OF_RANGE if validate() fails.
 */
iec104_status encode_cp56time2a(const Cp56Time2a& t, std::uint8_t* out,
                                std::size_t out_size) noexcept;

/**
 * @brief Decodes 7 octets into a time tag.
 *
 * Decoding is tolerant: field values are taken as received without range
 * check, so that a peer's time tag can still be inspected and logged. Callers
 * that need strict handling call validate() on the result. The reserved bits
 * RES1..RES4 are kept, so encode_cp56time2a() reproduces the input octets
 * whenever validate() succeeds.
 *
 * @param data Start of the time tag. Must not be NULL.
 * @param size Number of octets available at @p data.
 * @param out  Receives the decoded time tag on success. Unchanged on error.
 * @return IEC104_OK on success.
 * @return IEC104_ERR_INVALID_ARGUMENT if @p data is NULL.
 * @return IEC104_ERR_NEED_MORE_DATA if @p size is less than 7.
 */
iec104_status decode_cp56time2a(const std::uint8_t* data, std::size_t size,
                                Cp56Time2a& out) noexcept;

/**
 * @brief Milliseconds since 1970-01-01 00:00 for a time tag read as UTC, year 2000 + yy.
 *
 * The time tag itself carries no time zone (see file comment); reading it as
 * UTC is the default time base of the library (docs/05_Normfragen_und_Festlegungen.md,
 * N13). Fields are used as they are; call validate() first for untrusted input.
 *
 * Example: 2026-11-25 11:01:05.931 -> 1795604465931.
 */
std::int64_t to_unix_ms(const Cp56Time2a& t) noexcept;

/**
 * @brief Time tag (UTC, IV = 0, SU = 0) for milliseconds since 1970-01-01 00:00.
 *
 * The day of week is set (1 = Monday .. 7 = Sunday).
 *
 * @return IEC104_OK, or IEC104_ERR_OUT_OF_RANGE outside the years 2000..2099.
 */
iec104_status from_unix_ms(std::int64_t unix_ms, Cp56Time2a& out) noexcept;

} // namespace iec104::codec
