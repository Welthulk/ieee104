/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_values.cpp
 * @brief Value API: iec104_object <-> codec, time conversion (src/api/values.hpp).
 *
 * The mapping per type is the table at iec104_object in iec104.h; NVA =
 * raw / 32768 (docs/02_Technik_und_Quellen.md, "Informationselemente").
 */
#include "api/values.hpp"
#include "iec104/iec104.h"

#include <gtest/gtest.h>

using namespace iec104;

namespace {

iec104_object make(std::uint8_t type, double value) {
    iec104_object o{};
    o.type = type;
    o.ioa = 4711;
    o.value = value;
    o.time = iec104_time{2026, 11, 25, 11, 1, 5, 931, 0, 0, 0};
    return o;
}

// C -> codec -> C gives the same object.
void expect_round_trip(const iec104_object& in) {
    codec::InformationObject c{};
    ASSERT_EQ(api::to_codec(in, c), IEC104_OK) << "type " << int{in.type};
    iec104_object out{};
    ASSERT_TRUE(api::from_codec(in.type, c, out));
    EXPECT_EQ(out.ioa, in.ioa);
    EXPECT_DOUBLE_EQ(out.value, in.value) << "type " << int{in.type};
    EXPECT_EQ(out.quality, in.quality);
    EXPECT_EQ(out.qualifier, in.qualifier);
    EXPECT_EQ(out.select, in.select);
    EXPECT_EQ(out.bits, in.bits);
}

} // namespace

// Rule: every type of the value API maps both ways without loss for in-range values.
TEST(Values, RoundTrip) {
    for (const int t : {1, 30, 45, 58}) {
        expect_round_trip(make(static_cast<std::uint8_t>(t), 1));
    }
    expect_round_trip(make(3, 2));
    iec104_object st = make(5, -64);
    st.flags = IEC104_FLAG_TRANSIENT;
    expect_round_trip(st);
    expect_round_trip(make(9, 0.5));
    expect_round_trip(make(21, -1.0));
    expect_round_trip(make(11, -32768));
    expect_round_trip(make(13, 49.5));
    iec104_object it = make(15, -123456);
    it.qualifier = 31;
    it.flags = IEC104_FLAG_CARRY | IEC104_FLAG_ADJUSTED;
    it.quality = IEC104_QUALITY_INVALID;
    expect_round_trip(it);
    iec104_object bo = make(7, 0);
    bo.bits = 0xDEADBEEFU;
    bo.quality = IEC104_QUALITY_BLOCKED;
    expect_round_trip(bo);
    iec104_object cmd = make(49, 1234);
    cmd.qualifier = 5;
    cmd.select = 1;
    expect_round_trip(cmd);
    iec104_object ci = make(101, 0);
    ci.qualifier = 5;
    ci.bits = 2;
    expect_round_trip(ci);
    expect_round_trip(make(107, 0x1234));
}

// Rule: values outside the range of the type are rejected, never truncated.
TEST(Values, RangeChecks) {
    codec::InformationObject c{};
    EXPECT_EQ(api::to_codec(make(1, 2), c), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(api::to_codec(make(1, 0.5), c), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(api::to_codec(make(11, 32768), c), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(api::to_codec(make(9, 1.0), c), IEC104_ERR_OUT_OF_RANGE); // 32768 does not fit
    EXPECT_EQ(api::to_codec(make(5, 64), c), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(api::to_codec(make(22, 0), c), IEC104_ERR_INVALID_ARGUMENT); // unsupported type
    iec104_object t = make(30, 1);
    t.time.year = 1999;
    EXPECT_EQ(api::to_codec(t, c), IEC104_ERR_OUT_OF_RANGE);
}

// Rule: NVA = value * 32768, rounded to the nearest raw value.
TEST(Values, NormalizedRounding) {
    codec::InformationObject c{};
    ASSERT_EQ(api::to_codec(make(9, 0.1), c), IEC104_OK);
    EXPECT_EQ(c.nva, 3277); // 3276.8 rounded
    ASSERT_EQ(api::to_codec(make(9, -1.0), c), IEC104_OK);
    EXPECT_EQ(c.nva, -32768);
}

// Rule: Unix time <-> calendar time in UTC; 2026-11-25 is a Wednesday (3).
TEST(Values, UnixTime) {
    iec104_time t{};
    // 2026-11-25 11:01:05.931 UTC = 1795604465931 ms after 1970-01-01.
    ASSERT_EQ(iec104_time_from_unix_ms(1795604465931LL, &t), IEC104_OK);
    EXPECT_EQ(t.year, 2026U);
    EXPECT_EQ(t.month, 11U);
    EXPECT_EQ(t.day, 25U);
    EXPECT_EQ(t.hour, 11U);
    EXPECT_EQ(t.minute, 1U);
    EXPECT_EQ(t.second, 5U);
    EXPECT_EQ(t.millisecond, 931U);
    EXPECT_EQ(t.day_of_week, 3U);
    EXPECT_EQ(iec104_time_to_unix_ms(&t), 1795604465931LL);

    EXPECT_EQ(iec104_time_from_unix_ms(0, &t), IEC104_ERR_OUT_OF_RANGE); // 1970: before 2000
    EXPECT_EQ(iec104_time_from_unix_ms(0, nullptr), IEC104_ERR_INVALID_ARGUMENT);
    // 2000-02-29 (leap day) survives the round trip.
    iec104_time leap{2000, 2, 29, 23, 59, 59, 999, 0, 0, 0};
    iec104_time back{};
    ASSERT_EQ(iec104_time_from_unix_ms(iec104_time_to_unix_ms(&leap), &back), IEC104_OK);
    EXPECT_EQ(back.month, 2U);
    EXPECT_EQ(back.day, 29U);
}
