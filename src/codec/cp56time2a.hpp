// CP56Time2a: 7-octet time tag.
//
// Layout (octets 1..7):
//   1-2  milliseconds incl. seconds, 0..59999, little endian
//   3    bits 0-5 minute, bit 6 RES1, bit 7 IV (invalid)
//   4    bits 0-4 hour,   bits 5-6 RES2, bit 7 SU (summer time)
//   5    bits 0-4 day of month 1..31, bits 5-7 day of week 1..7 (0 = not used)
//   6    bits 0-3 month 1..12, bits 4-7 RES3
//   7    bits 0-6 year 0..99,  bit 7 RES4
//
// The standard does NOT define whether the time is UTC or local time. This
// type therefore carries no time zone; interpretation belongs to the
// connection profile (TimeBase, see docs/02_Konzept, section "Zeit").
#pragma once

#include <cstddef>
#include <cstdint>

#include "iec104/iec104.h"

namespace iec104::codec {

inline constexpr std::size_t kCp56Time2aLength = 7U;

struct Cp56Time2a {
    std::uint16_t milliseconds{0}; // 0..59999 (seconds * 1000 + ms)
    std::uint8_t minute{0};        // 0..59
    std::uint8_t hour{0};          // 0..23
    std::uint8_t day_of_month{1};  // 1..31
    std::uint8_t day_of_week{0};   // 1..7, 0 = not used
    std::uint8_t month{1};         // 1..12
    std::uint8_t year{0};          // 0..99
    bool invalid{false};           // IV
    bool summer_time{false};       // SU
    bool res1{false};              // RES1 bit of the minute octet, kept as is

    friend bool operator==(const Cp56Time2a& a, const Cp56Time2a& b) noexcept {
        return a.milliseconds == b.milliseconds && a.minute == b.minute && a.hour == b.hour &&
               a.day_of_month == b.day_of_month && a.day_of_week == b.day_of_week &&
               a.month == b.month && a.year == b.year && a.invalid == b.invalid &&
               a.summer_time == b.summer_time && a.res1 == b.res1;
    }
    friend bool operator!=(const Cp56Time2a& a, const Cp56Time2a& b) noexcept {
        return !(a == b);
    }
};

// Range check of all fields. Does not check calendar validity (e.g. 31.02.).
iec104_status validate(const Cp56Time2a& t) noexcept;

// Encodes 7 octets. Returns OUT_OF_RANGE if validate() fails.
iec104_status encode_cp56time2a(const Cp56Time2a& t, std::uint8_t* out,
                                std::size_t out_size) noexcept;

// Decodes 7 octets without range check (tolerant). Reserved bits RES2..RES4
// are ignored. Call validate() for strict handling.
iec104_status decode_cp56time2a(const std::uint8_t* data, std::size_t size,
                                Cp56Time2a& out) noexcept;

} // namespace iec104::codec
