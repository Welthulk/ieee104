/**
 * @file test_cp56time2a.cpp
 * @brief Unit tests for the CP56Time2a codec (src/codec/cp56time2a.hpp).
 *
 * Reference bytes come from two kinds of sources, named at every test:
 *   - "capture": tests/data/lib60870_capture.txt, produced by an independent
 *     implementation (lib60870-C v2.4.1, used only as external test peer);
 *   - "layout": derived by hand from the bit layout documented in
 *     cp56time2a.hpp, which follows docs/03 (citing the Wireshark dissector).
 */
#include "codec/cp56time2a.hpp"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>

using namespace iec104::codec;
using Bytes7 = std::array<std::uint8_t, 7>;

namespace {

// 2026-11-25 09:48:30.123, day of week 3, IV = 0, SU = 0. Arbitrary values
// that exercise every field with a non-zero value.
Cp56Time2a sample() {
    Cp56Time2a t{};
    t.milliseconds = 30123;
    t.minute = 48;
    t.hour = 9;
    t.day_of_month = 25;
    t.day_of_week = 3;
    t.month = 11;
    t.year = 26;
    return t;
}

} // namespace

// Rule: field positions as documented in cp56time2a.hpp.
// Source: layout (30123 = 0x75AB -> AB 75; 48 -> 30; 9 -> 09;
// day 25 + day of week 3 << 5 -> 79; 11 -> 0B; 26 -> 1A).
TEST(Cp56Time2a, EncodeKnownBytes) {
    Bytes7 out{};
    ASSERT_EQ(encode_cp56time2a(sample(), out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out, (Bytes7{0xAB, 0x75, 0x30, 0x09, 0x79, 0x0B, 0x1A}));
}

// Rule: as EncodeKnownBytes, checked against a peer's time tag in both
// directions (day of week not used = 0).
// Source: capture, time tag of the C_TS_TA_1 test command
// "... 38 49 2b 17 01 0b 19 0b 1a" = 2026-11-25 11:01:05.931.
TEST(Cp56Time2a, TimeTagFromCapture) {
    const Bytes7 captured{0x2B, 0x17, 0x01, 0x0B, 0x19, 0x0B, 0x1A};
    Cp56Time2a d{};
    ASSERT_EQ(decode_cp56time2a(captured.data(), captured.size(), d), IEC104_OK);
    EXPECT_EQ(d.milliseconds, 5931U);
    EXPECT_EQ(d.minute, 1U);
    EXPECT_EQ(d.hour, 11U);
    EXPECT_EQ(d.day_of_month, 25U);
    EXPECT_EQ(d.day_of_week, 0U);
    EXPECT_EQ(d.month, 11U);
    EXPECT_EQ(d.year, 26U);
    EXPECT_FALSE(d.invalid);
    EXPECT_FALSE(d.summer_time);
    EXPECT_FALSE(d.res1);
    EXPECT_EQ(validate(d), IEC104_OK);

    Bytes7 out{};
    ASSERT_EQ(encode_cp56time2a(d, out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out, captured);
}

// Rule: IV (minute octet bit 7), RES1 (bit 6) and SU (hour octet bit 7) are
// encoded at their positions and survive a round trip.
// Source: layout.
TEST(Cp56Time2a, FlagsRoundTrip) {
    Cp56Time2a t = sample();
    t.invalid = true;
    t.summer_time = true;
    t.res1 = true;
    Bytes7 out{};
    ASSERT_EQ(encode_cp56time2a(t, out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out[2], 0x30U | 0x40U | 0x80U);
    EXPECT_EQ(out[3], 0x09U | 0x80U);

    Cp56Time2a d{};
    ASSERT_EQ(decode_cp56time2a(out.data(), out.size(), d), IEC104_OK);
    EXPECT_EQ(d, t);
}

// Rule: RES2..RES4 are ignored on decoding (tolerant decoding).
// Source: layout; reserved bits set by hand on the EncodeKnownBytes octets.
TEST(Cp56Time2a, DecodeIgnoresReservedBits) {
    // RES2 (hour bits 5-6), RES3 (month bits 4-7), RES4 (year bit 7) set
    const Bytes7 in{0xAB, 0x75, 0x30, 0x09 | 0x60, 0x79, 0x0B | 0xF0, 0x1A | 0x80};
    Cp56Time2a d{};
    ASSERT_EQ(decode_cp56time2a(in.data(), in.size(), d), IEC104_OK);
    EXPECT_EQ(d, sample());
}

// Rule: validate() rejects each field just above (or below) its range, and
// encoding refuses an invalid value instead of truncating it.
// Source: ranges from cp56time2a.hpp (docs/03).
TEST(Cp56Time2a, ValidateRanges) {
    EXPECT_EQ(validate(sample()), IEC104_OK);

    Cp56Time2a t = sample();
    t.milliseconds = 60000;
    EXPECT_EQ(validate(t), IEC104_ERR_OUT_OF_RANGE);
    t = sample();
    t.minute = 60;
    EXPECT_EQ(validate(t), IEC104_ERR_OUT_OF_RANGE);
    t = sample();
    t.hour = 24;
    EXPECT_EQ(validate(t), IEC104_ERR_OUT_OF_RANGE);
    t = sample();
    t.day_of_month = 0;
    EXPECT_EQ(validate(t), IEC104_ERR_OUT_OF_RANGE);
    t = sample();
    t.month = 13;
    EXPECT_EQ(validate(t), IEC104_ERR_OUT_OF_RANGE);
    t = sample();
    t.year = 100;
    EXPECT_EQ(validate(t), IEC104_ERR_OUT_OF_RANGE);

    Bytes7 out{};
    EXPECT_EQ(encode_cp56time2a(t, out.data(), out.size()), IEC104_ERR_OUT_OF_RANGE);
}

// Rule: decoding accepts out-of-range field values so they can be inspected;
// only validate() reports them.
// Source: constructed by hand (minute 63, month 0).
TEST(Cp56Time2a, DecodeIsTolerant) {
    const Bytes7 in{0x00, 0x00, 0x3F, 0x00, 0x01, 0x00, 0x00};
    Cp56Time2a d{};
    ASSERT_EQ(decode_cp56time2a(in.data(), in.size(), d), IEC104_OK);
    EXPECT_EQ(d.minute, 63U);
    EXPECT_EQ(d.month, 0U);
    EXPECT_EQ(validate(d), IEC104_ERR_OUT_OF_RANGE);
}

// Rule: buffer size and NULL checks return the documented status codes.
// Source: cp56time2a.hpp (API contract).
TEST(Cp56Time2a, BufferChecks) {
    Bytes7 out{};
    EXPECT_EQ(encode_cp56time2a(sample(), out.data(), 6), IEC104_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(encode_cp56time2a(sample(), nullptr, 7), IEC104_ERR_INVALID_ARGUMENT);
    Cp56Time2a d{};
    EXPECT_EQ(decode_cp56time2a(out.data(), 6, d), IEC104_ERR_NEED_MORE_DATA);
}
