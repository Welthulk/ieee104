/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file cp56time2a.cpp
 * @brief Implementation of the CP56Time2a codec.
 *
 * See cp56time2a.hpp for the bit layout. Encoding is strict, decoding is
 * tolerant; validate() connects the two.
 *
 * Layer: codec (pure functions, no I/O, no allocation).
 */
#include "codec/cp56time2a.hpp"

#include "codec/byte_io.hpp"

namespace iec104::codec {
namespace {

// Masks for the octets 3..7 of the time tag (index 2..6 in the buffer).
constexpr std::uint8_t kMinuteMask = 0x3FU;
constexpr std::uint8_t kRes1Bit = 0x40U;
constexpr std::uint8_t kInvalidBit = 0x80U;
constexpr std::uint8_t kHourMask = 0x1FU;
constexpr std::uint8_t kSummerTimeBit = 0x80U;
constexpr std::uint8_t kDayOfMonthMask = 0x1FU;
constexpr unsigned kDayOfWeekShift = 5U;
constexpr std::uint8_t kDayOfWeekMask = 0x07U;
constexpr std::uint8_t kMonthMask = 0x0FU;
constexpr std::uint8_t kYearMask = 0x7FU;
constexpr unsigned kRes2Shift = 5U;
constexpr std::uint8_t kRes2Mask = 0x03U; // after shifting
constexpr unsigned kRes3Shift = 4U;
constexpr std::uint8_t kRes3Mask = 0x0FU; // after shifting
constexpr std::uint8_t kRes4Bit = 0x80U;

} // namespace

iec104_status validate(const Cp56Time2a& t) noexcept {
    if (t.milliseconds > 59999U || t.minute > 59U || t.hour > 23U || t.day_of_month < 1U ||
        t.day_of_month > 31U || t.day_of_week > 7U || t.month < 1U || t.month > 12U ||
        t.year > 99U || t.res2 > kRes2Mask || t.res3 > kRes3Mask) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    return IEC104_OK;
}

iec104_status encode_cp56time2a(const Cp56Time2a& t, std::uint8_t* out,
                                std::size_t out_size) noexcept {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < kCp56Time2aLength) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    const iec104_status st = validate(t);
    if (st != IEC104_OK) {
        return st;
    }
    // After validate() every field fits its mask; the masks only document the
    // bit positions. Reserved bits are written as given (0 unless decoded).
    detail::put_u16_le(out, t.milliseconds);
    out[2] = static_cast<std::uint8_t>((t.minute & kMinuteMask) | (t.res1 ? kRes1Bit : 0U) |
                                       (t.invalid ? kInvalidBit : 0U));
    out[3] = static_cast<std::uint8_t>((t.hour & kHourMask) |
                                       ((static_cast<unsigned>(t.res2) & kRes2Mask) << kRes2Shift) |
                                       (t.summer_time ? kSummerTimeBit : 0U));
    out[4] = static_cast<std::uint8_t>((t.day_of_month & kDayOfMonthMask) |
                                       ((t.day_of_week & kDayOfWeekMask) << kDayOfWeekShift));
    out[5] = static_cast<std::uint8_t>((t.month & kMonthMask) |
                                       ((static_cast<unsigned>(t.res3) & kRes3Mask) << kRes3Shift));
    out[6] = static_cast<std::uint8_t>((t.year & kYearMask) | (t.res4 ? kRes4Bit : 0U));
    return IEC104_OK;
}

iec104_status decode_cp56time2a(const std::uint8_t* data, std::size_t size,
                                Cp56Time2a& out) noexcept {
    if (data == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (size < kCp56Time2aLength) {
        return IEC104_ERR_NEED_MORE_DATA;
    }
    // No range check here on purpose (tolerant decoding, see header).
    Cp56Time2a t{};
    t.milliseconds = detail::get_u16_le(data);
    t.minute = static_cast<std::uint8_t>(data[2] & kMinuteMask);
    t.res1 = (data[2] & kRes1Bit) != 0U;
    t.invalid = (data[2] & kInvalidBit) != 0U;
    t.hour = static_cast<std::uint8_t>(data[3] & kHourMask);
    t.summer_time = (data[3] & kSummerTimeBit) != 0U;
    t.day_of_month = static_cast<std::uint8_t>(data[4] & kDayOfMonthMask);
    t.day_of_week = static_cast<std::uint8_t>((data[4] >> kDayOfWeekShift) & kDayOfWeekMask);
    t.res2 = static_cast<std::uint8_t>((data[3] >> kRes2Shift) & kRes2Mask);
    t.month = static_cast<std::uint8_t>(data[5] & kMonthMask);
    t.res3 = static_cast<std::uint8_t>((data[5] >> kRes3Shift) & kRes3Mask);
    t.year = static_cast<std::uint8_t>(data[6] & kYearMask);
    t.res4 = (data[6] & kRes4Bit) != 0U;
    out = t;
    return IEC104_OK;
}

} // namespace iec104::codec
