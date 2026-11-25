#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "codec/cp56time2a.hpp"

using namespace iec104::codec;
using Bytes7 = std::array<std::uint8_t, 7>;

namespace {
Cp56Time2a sample() {
    // Wednesday 2026-11-25 09:48:30.123
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

TEST(Cp56Time2a, EncodeKnownBytes) {
    Bytes7 out{};
    ASSERT_EQ(encode_cp56time2a(sample(), out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out, (Bytes7{0xAB, 0x75, 0x30, 0x09, 0x79, 0x0B, 0x1A}));
}

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

TEST(Cp56Time2a, DecodeIgnoresReservedBits) {
    // RES2 (hour bits 5-6), RES3 (month bits 4-7), RES4 (year bit 7) set
    const Bytes7 in{0xAB, 0x75, 0x30, 0x09 | 0x60, 0x79, 0x0B | 0xF0, 0x1A | 0x80};
    Cp56Time2a d{};
    ASSERT_EQ(decode_cp56time2a(in.data(), in.size(), d), IEC104_OK);
    EXPECT_EQ(d, sample());
}

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

TEST(Cp56Time2a, DecodeIsTolerant) {
    // month 0 and minute 63 decode, but fail validation
    const Bytes7 in{0x00, 0x00, 0x3F, 0x00, 0x01, 0x00, 0x00};
    Cp56Time2a d{};
    ASSERT_EQ(decode_cp56time2a(in.data(), in.size(), d), IEC104_OK);
    EXPECT_EQ(d.minute, 63U);
    EXPECT_EQ(d.month, 0U);
    EXPECT_EQ(validate(d), IEC104_ERR_OUT_OF_RANGE);
}

TEST(Cp56Time2a, BufferChecks) {
    Bytes7 out{};
    EXPECT_EQ(encode_cp56time2a(sample(), out.data(), 6), IEC104_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(encode_cp56time2a(sample(), nullptr, 7), IEC104_ERR_INVALID_ARGUMENT);
    Cp56Time2a d{};
    EXPECT_EQ(decode_cp56time2a(out.data(), 6, d), IEC104_ERR_NEED_MORE_DATA);
}
