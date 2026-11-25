/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_asdu.cpp
 * @brief ASDU codec: round trip of every type, splitting, negative cases.
 *
 * These tests use values chosen in the test (not reference bytes); they check
 * the codec's own rules: losslessness for every table entry with SQ = 0 and
 * SQ = 1, the length and address rules, strict/tolerant, splitting by
 * max_asdu_length. Reference bytes of a foreign implementation are tested in
 * test_asdu_reference.cpp.
 */
#include "codec/asdu.hpp"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

using namespace iec104::codec;

namespace {

using Buffer = std::array<std::uint8_t, 256>;

AsduProfile tolerant() {
    AsduProfile p{};
    p.strict = false;
    return p;
}

// An object whose members all hold in-range, non-default values that differ
// per @p seed, so that a member mix-up in the codec changes the bytes.
InformationObject sample_object(std::uint32_t ioa, unsigned seed) {
    const auto s8 = static_cast<std::uint8_t>(seed);
    InformationObject o{};
    o.ioa = ioa;
    o.siq = Siq{(seed & 1U) != 0U, kQualityBlocked};
    o.diq = Diq{DoublePoint::On, kQualityInvalid};
    o.vti = Vti{static_cast<std::int8_t>(-20 + static_cast<int>(seed % 40U)), true};
    o.qds = kQualityOverflow | kQualityNotTopical;
    o.bsi = 0x01020304U + seed;
    o.nva = static_cast<std::int16_t>(-1000 - static_cast<int>(seed));
    o.sva = static_cast<std::int16_t>(2000 + static_cast<int>(seed));
    o.r32 = 3.25F + static_cast<float>(seed);
    o.bcr = Bcr{-123456 - static_cast<std::int32_t>(seed), static_cast<std::uint8_t>(seed % 32U),
                true, false, true};
    o.scd = Scd{static_cast<std::uint16_t>(0xA500U + seed), 0x00FFU};
    o.sep = Sep{EventState::Off, kQualityElapsedTimeInvalid};
    o.spe = kSpeGeneralStart | kSpeEarthCurrent;
    o.oci = kOciPhaseL2;
    o.qdp = kQualitySubstituted;
    o.sco = Sco{true, false, 2, true};
    o.dco = Dco{2, 1, false};
    o.rco = Rco{1, 3, true};
    o.qos = Qos{static_cast<std::uint8_t>(seed % 128U), true};
    o.coi = Coi{2, true};
    o.qoi = 21;
    o.qcc = Qcc{5, 1};
    o.qrp = 2;
    o.fbp = kFixedTestBitPattern;
    o.tsc = static_cast<std::uint16_t>(0x1000U + seed);
    o.qpm = Qpm{2, true, false};
    o.qpa = s8;
    o.cp16 = static_cast<std::uint16_t>(100U + seed);
    o.cp24 = Cp24Time2a{static_cast<std::uint16_t>(1000U + seed), 42, false, false};
    o.cp56.milliseconds = static_cast<std::uint16_t>(30000U + seed);
    o.cp56.minute = 30;
    o.cp56.hour = 12;
    o.cp56.day_of_month = 24;
    o.cp56.day_of_week = 2;
    o.cp56.month = 12;
    o.cp56.year = 26;
    o.cp56.summer_time = (seed & 1U) != 0U;
    return o;
}

AsduHeader header_for(std::uint8_t type_id, bool sequence) {
    AsduHeader h{};
    h.type_id = type_id;
    h.sequence = sequence;
    h.cause = 3;
    h.originator = 7;
    h.common_address = 0x1234;
    return h;
}

// Three objects with consecutive addresses (valid for SQ = 0 and SQ = 1).
std::vector<InformationObject> three_objects() {
    return {sample_object(1000, 1), sample_object(1001, 2), sample_object(1002, 3)};
}

} // namespace

// Rule: every table entry encodes and decodes losslessly with SQ = 0 and
// SQ = 1: encode -> decode -> encode gives identical octets, the length is
// what asdu_length() predicts, and the decoded addresses are the given ones.
// Source: type table (type_table.cpp); values from sample_object().
TEST(Asdu, EveryTypeRoundTripsWithSq0AndSq1) {
    std::size_t type_count = 0;
    const TypeInfo* types = all_types(type_count);
    const std::vector<InformationObject> objects = three_objects();

    for (std::size_t t = 0; t < type_count; ++t) {
        for (const bool sq : {false, true}) {
            SCOPED_TRACE(std::string(types[t].name) + (sq ? " SQ1" : " SQ0"));
            const AsduHeader h = header_for(types[t].id, sq);
            Buffer first{};
            std::size_t first_len = 0;
            ASSERT_EQ(encode_asdu(h, objects.data(), objects.size(), tolerant(), first.data(),
                                  first.size(), first_len),
                      IEC104_OK);
            EXPECT_EQ(first_len, asdu_length(types[t], sq, objects.size(), tolerant()));

            std::array<InformationObject, 3> decoded{};
            DecodedAsdu d{};
            ASSERT_EQ(
                decode_asdu(first.data(), first_len, tolerant(), decoded.data(), decoded.size(), d),
                IEC104_OK);
            EXPECT_EQ(d.type, &types[t]);
            EXPECT_EQ(d.header.cause, 3U);
            EXPECT_EQ(d.header.common_address, 0x1234U);
            ASSERT_EQ(d.object_count, 3U);
            for (std::size_t i = 0; i < 3U; ++i) {
                EXPECT_EQ(decoded[i].ioa, objects[i].ioa);
            }

            Buffer second{};
            std::size_t second_len = 0;
            ASSERT_EQ(encode_asdu(d.header, decoded.data(), d.object_count, tolerant(),
                                  second.data(), second.size(), second_len),
                      IEC104_OK);
            ASSERT_EQ(second_len, first_len);
            EXPECT_EQ(first, second);
        }
    }
}

// Rule: SQ = 0 carries an IOA per object, SQ = 1 one IOA for all objects.
// Source: VSQ layout in asdu_header.hpp; bytes derived by hand (layout):
// M_SP_NA_1, cause 3, originator 7, CA 0x1234, IOA 1000 = E8 03 00.
TEST(Asdu, SequenceLayoutKnownBytes) {
    std::vector<InformationObject> objects = three_objects();
    for (InformationObject& o : objects) {
        o.siq = Siq{true, 0};
    }
    Buffer out{};
    std::size_t len = 0;

    ASSERT_EQ(encode_asdu(header_for(1, false), objects.data(), 2, kIec104Profile, out.data(),
                          out.size(), len),
              IEC104_OK);
    const std::vector<std::uint8_t> sq0{0x01, 0x02, 0x03, 0x07, 0x34, 0x12, 0xE8,
                                        0x03, 0x00, 0x01, 0xE9, 0x03, 0x00, 0x01};
    EXPECT_EQ(std::vector<std::uint8_t>(out.begin(), out.begin() + static_cast<long>(len)), sq0);

    ASSERT_EQ(encode_asdu(header_for(1, true), objects.data(), 2, kIec104Profile, out.data(),
                          out.size(), len),
              IEC104_OK);
    const std::vector<std::uint8_t> sq1{0x01, 0x82, 0x03, 0x07, 0x34, 0x12,
                                        0xE8, 0x03, 0x00, 0x01, 0x01};
    EXPECT_EQ(std::vector<std::uint8_t>(out.begin(), out.begin() + static_cast<long>(len)), sq1);
}

// Rule: the input must contain exactly the objects the VSQ announces.
// Source: length rule of asdu.hpp; bytes derived from SequenceLayoutKnownBytes.
TEST(Asdu, LengthMustMatchObjectCount) {
    const std::vector<std::uint8_t> good{0x01, 0x02, 0x03, 0x07, 0x34, 0x12, 0xE8,
                                         0x03, 0x00, 0x01, 0xE9, 0x03, 0x00, 0x01};
    std::array<InformationObject, 127> objects{};
    DecodedAsdu d{};
    ASSERT_EQ(
        decode_asdu(good.data(), good.size(), kIec104Profile, objects.data(), objects.size(), d),
        IEC104_OK);

    // One octet missing, one octet too much.
    EXPECT_EQ(decode_asdu(good.data(), good.size() - 1, kIec104Profile, objects.data(),
                          objects.size(), d),
              IEC104_ERR_MALFORMED);
    std::vector<std::uint8_t> longer = good;
    longer.push_back(0x00);
    EXPECT_EQ(decode_asdu(longer.data(), longer.size(), kIec104Profile, objects.data(),
                          objects.size(), d),
              IEC104_ERR_MALFORMED);

    // VSQ announces 3 objects, 2 are present.
    std::vector<std::uint8_t> wrong_count = good;
    wrong_count[1] = 0x03;
    EXPECT_EQ(decode_asdu(wrong_count.data(), wrong_count.size(), kIec104Profile, objects.data(),
                          objects.size(), d),
              IEC104_ERR_MALFORMED);

    // Shorter than the data unit identifier.
    EXPECT_EQ(decode_asdu(good.data(), 5, kIec104Profile, objects.data(), objects.size(), d),
              IEC104_ERR_MALFORMED);
}

// Rule: the caller's array must hold all objects; too small is reported, not truncated.
// Source: decode_asdu() contract.
TEST(Asdu, DecodeCapacityTooSmall) {
    const std::vector<std::uint8_t> two{0x01, 0x02, 0x03, 0x07, 0x34, 0x12, 0xE8,
                                        0x03, 0x00, 0x01, 0xE9, 0x03, 0x00, 0x01};
    std::array<InformationObject, 1> objects{};
    DecodedAsdu d{};
    EXPECT_EQ(
        decode_asdu(two.data(), two.size(), kIec104Profile, objects.data(), objects.size(), d),
        IEC104_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(decode_asdu(two.data(), two.size(), kIec104Profile, nullptr, 1, d),
              IEC104_ERR_INVALID_ARGUMENT);
}

// Rule: a type that is neither in the table nor private is unsupported
// (the session answers with cause 44, docs/02_Technik_und_Quellen.md). Types 22..29 are unassigned.
// Source: type table; header bytes derived by hand.
TEST(Asdu, UnknownTypeIsUnsupported) {
    const std::vector<std::uint8_t> asdu{0x16, 0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00};
    std::array<InformationObject, 1> objects{};
    DecodedAsdu d{};
    EXPECT_EQ(
        decode_asdu(asdu.data(), asdu.size(), kIec104Profile, objects.data(), objects.size(), d),
        IEC104_ERR_UNSUPPORTED);

    const InformationObject o = sample_object(1, 1);
    Buffer out{};
    std::size_t len = 0;
    EXPECT_EQ(
        encode_asdu(header_for(22, false), &o, 1, kIec104Profile, out.data(), out.size(), len),
        IEC104_ERR_UNSUPPORTED);
}

// Rule: strict mode rejects CP24Time2a types on encoding too
// (docs/05_Normfragen_und_Festlegungen.md, N4). Source: type table (M_SP_TA_1 = 2).
TEST(Asdu, StrictRejectsCp24OnEncode) {
    const InformationObject o = sample_object(1, 1);
    Buffer out{};
    std::size_t len = 0;
    EXPECT_EQ(encode_asdu(header_for(2, false), &o, 1, kIec104Profile, out.data(), out.size(), len),
              IEC104_ERR_UNSUPPORTED);
    EXPECT_EQ(encode_asdu(header_for(2, false), &o, 1, tolerant(), out.data(), out.size(), len),
              IEC104_OK);
}

// Rule: an ASDU with 0 objects is malformed in strict mode and accepted in
// tolerant mode if nothing follows the header (docs/05_Normfragen_und_Festlegungen.md, N9).
// Source: header bytes derived by hand (M_SP_NA_1, VSQ 0).
TEST(Asdu, ZeroObjects) {
    const std::vector<std::uint8_t> empty{0x01, 0x00, 0x03, 0x00, 0x01, 0x00};
    std::array<InformationObject, 1> objects{};
    DecodedAsdu d{};
    EXPECT_EQ(
        decode_asdu(empty.data(), empty.size(), kIec104Profile, objects.data(), objects.size(), d),
        IEC104_ERR_MALFORMED);
    ASSERT_EQ(
        decode_asdu(empty.data(), empty.size(), tolerant(), objects.data(), objects.size(), d),
        IEC104_OK);
    EXPECT_EQ(d.object_count, 0U);

    std::vector<std::uint8_t> trailing = empty;
    trailing.push_back(0x01);
    EXPECT_EQ(decode_asdu(trailing.data(), trailing.size(), tolerant(), objects.data(),
                          objects.size(), d),
              IEC104_ERR_MALFORMED);

    Buffer out{};
    std::size_t len = 0;
    EXPECT_EQ(encode_asdu(header_for(1, false), objects.data(), 0, tolerant(), out.data(),
                          out.size(), len),
              IEC104_ERR_INVALID_ARGUMENT);
}

// Rule: private types 136..255 are passed through as raw octets (asdu.hpp).
// Source: private range from docs/02_Technik_und_Quellen.md; bytes chosen for the test.
TEST(Asdu, PrivateTypeIsRaw) {
    const std::vector<std::uint8_t> asdu{0xC9, 0x01, 0x03, 0x00, 0x01, 0x00, 0xAA, 0xBB, 0xCC};
    DecodedAsdu d{};
    ASSERT_EQ(decode_asdu(asdu.data(), asdu.size(), kIec104Profile, nullptr, 0, d), IEC104_OK);
    EXPECT_EQ(d.type, nullptr);
    EXPECT_EQ(d.header.type_id, 0xC9U);
    EXPECT_EQ(d.object_count, 0U);
    ASSERT_EQ(d.object_data_size, 3U);
    EXPECT_EQ(d.object_data, asdu.data() + 6);

    const InformationObject o{};
    Buffer out{};
    std::size_t len = 0;
    EXPECT_EQ(
        encode_asdu(header_for(0xC9, false), &o, 1, kIec104Profile, out.data(), out.size(), len),
        IEC104_ERR_UNSUPPORTED);
}

// Rule: with SQ = 1 the addresses must be consecutive, and the last one must
// fit into ioa_length octets.
// Source: SQ semantics in asdu_header.hpp.
TEST(Asdu, SequenceAddressRules) {
    std::vector<InformationObject> objects = three_objects();
    objects[2].ioa = 2000; // gap
    Buffer out{};
    std::size_t len = 0;
    EXPECT_EQ(encode_asdu(header_for(1, true), objects.data(), objects.size(), kIec104Profile,
                          out.data(), out.size(), len),
              IEC104_ERR_INVALID_ARGUMENT);

    objects = three_objects();
    objects[0].ioa = 0xFFFFFE;
    objects[1].ioa = 0xFFFFFF;
    objects[2].ioa = 0x1000000; // does not fit 3 octets
    EXPECT_EQ(encode_asdu(header_for(1, true), objects.data(), objects.size(), kIec104Profile,
                          out.data(), out.size(), len),
              IEC104_ERR_OUT_OF_RANGE);

    // Decoding: SQ = 1, 2 objects starting at the largest address.
    const std::vector<std::uint8_t> overflow{0x01, 0x82, 0x03, 0x00, 0x01, 0x00,
                                             0xFF, 0xFF, 0xFF, 0x01, 0x00};
    std::array<InformationObject, 2> decoded{};
    DecodedAsdu d{};
    EXPECT_EQ(decode_asdu(overflow.data(), overflow.size(), kIec104Profile, decoded.data(),
                          decoded.size(), d),
              IEC104_ERR_MALFORMED);
}

// Rule: encoding rejects values that do not fit their element instead of truncating.
// Source: ranges in elements.hpp and cp56time2a.hpp.
TEST(Asdu, ElementOutOfRangeIsRejected) {
    Buffer out{};
    std::size_t len = 0;
    InformationObject o = sample_object(1, 1);
    o.vti.value = 64; // VTI is -64..63
    EXPECT_EQ(encode_asdu(header_for(5, false), &o, 1, kIec104Profile, out.data(), out.size(), len),
              IEC104_ERR_OUT_OF_RANGE);

    o = sample_object(1, 1);
    o.cp56.minute = 60;
    EXPECT_EQ(
        encode_asdu(header_for(30, false), &o, 1, kIec104Profile, out.data(), out.size(), len),
        IEC104_ERR_OUT_OF_RANGE);

    o = sample_object(1, 1);
    o.sco.qu = 32;
    EXPECT_EQ(
        encode_asdu(header_for(45, false), &o, 1, kIec104Profile, out.data(), out.size(), len),
        IEC104_ERR_OUT_OF_RANGE);

    o = sample_object(0x1000000, 1); // IOA does not fit 3 octets
    EXPECT_EQ(encode_asdu(header_for(1, false), &o, 1, kIec104Profile, out.data(), out.size(), len),
              IEC104_ERR_OUT_OF_RANGE);
}

// Rule: the output buffer and the profile maximum are separate limits.
// Source: encode_asdu() contract; M_ME_NC_1 with 1 object = 6 + 3 + 5 = 14 octets.
TEST(Asdu, OutputAndProfileLimits) {
    const InformationObject o = sample_object(1, 1);
    Buffer out{};
    std::size_t len = 0;
    EXPECT_EQ(encode_asdu(header_for(13, false), &o, 1, kIec104Profile, out.data(), 13, len),
              IEC104_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(encode_asdu(header_for(13, false), &o, 1, kIec104Profile, out.data(), 14, len),
              IEC104_OK);

    AsduProfile small{};
    small.max_asdu_length = 13;
    EXPECT_EQ(encode_asdu(header_for(13, false), &o, 1, small, out.data(), out.size(), len),
              IEC104_ERR_OUT_OF_RANGE);

    small.max_asdu_length = 0;
    EXPECT_FALSE(is_valid(small));
    small.max_asdu_length = 250;
    EXPECT_FALSE(is_valid(small));
}

// Rule: encode_asdu_fitting() splits a long list so that every ASDU stays
// within max_asdu_length and 127 objects; together the parts contain every
// object exactly once, in order.
// Source: encode_asdu_fitting() contract. M_ME_NC_1 SQ = 0: 6 + n * 8 octets, so 249
// allows 30 objects per ASDU; SQ = 1: 6 + 3 + n * 5, so 48 objects.
TEST(Asdu, SplitByMaxAsduLength) {
    std::vector<InformationObject> list;
    for (std::uint32_t i = 0; i < 100U; ++i) {
        list.push_back(sample_object(5000 + i, i));
    }
    for (const bool sq : {false, true}) {
        SCOPED_TRACE(sq ? "SQ1" : "SQ0");
        const InformationObject* next = list.data();
        std::size_t remaining = list.size();
        std::vector<std::size_t> parts;
        std::uint32_t expected_ioa = 5000;
        while (remaining > 0U) {
            Buffer out{};
            std::size_t len = 0;
            std::size_t used = 0;
            ASSERT_EQ(encode_asdu_fitting(header_for(13, sq), next, remaining, kIec104Profile,
                                          out.data(), out.size(), len, used),
                      IEC104_OK);
            EXPECT_LE(len, kIec104Profile.max_asdu_length);
            parts.push_back(used);

            std::array<InformationObject, 127> decoded{};
            DecodedAsdu d{};
            ASSERT_EQ(
                decode_asdu(out.data(), len, kIec104Profile, decoded.data(), decoded.size(), d),
                IEC104_OK);
            ASSERT_EQ(d.object_count, used);
            for (std::size_t i = 0; i < used; ++i) {
                EXPECT_EQ(decoded[i].ioa, expected_ioa++);
            }
            next += used;
            remaining -= used;
        }
        const std::vector<std::size_t> expected =
            sq ? std::vector<std::size_t>{48, 48, 4} : std::vector<std::size_t>{30, 30, 30, 10};
        EXPECT_EQ(parts, expected);
    }
}

// Rule: a smaller profile maximum gives smaller parts; if not even one object
// fits, the encoder reports it instead of looping forever.
// Source: encode_asdu_fitting() contract; M_ME_NC_1 SQ = 0 needs 14 octets for one object.
TEST(Asdu, SplitSmallMaximum) {
    const std::vector<InformationObject> list = three_objects();
    AsduProfile p{};
    p.max_asdu_length = 22; // 6 + 2 * 8
    Buffer out{};
    std::size_t len = 0;
    std::size_t used = 0;
    ASSERT_EQ(encode_asdu_fitting(header_for(13, false), list.data(), list.size(), p, out.data(),
                                  out.size(), len, used),
              IEC104_OK);
    EXPECT_EQ(used, 2U);
    EXPECT_EQ(len, 22U);

    p.max_asdu_length = 13;
    EXPECT_EQ(encode_asdu_fitting(header_for(13, false), list.data(), list.size(), p, out.data(),
                                  out.size(), len, used),
              IEC104_ERR_OUT_OF_RANGE);
}

// Rule: at most 127 objects per ASDU (7-bit count in the VSQ), also when they
// would fit by length (C_RD_NA_1 with SQ = 1 needs no octets per object).
// Source: VSQ layout in asdu_header.hpp.
TEST(Asdu, SplitAt127Objects) {
    std::vector<InformationObject> list;
    for (std::uint32_t i = 0; i < 200U; ++i) {
        list.push_back(sample_object(i, i));
    }
    Buffer out{};
    std::size_t len = 0;
    std::size_t used = 0;
    ASSERT_EQ(encode_asdu_fitting(header_for(102, true), list.data(), list.size(), kIec104Profile,
                                  out.data(), out.size(), len, used),
              IEC104_OK);
    EXPECT_EQ(used, 127U);
    EXPECT_EQ(len, 9U); // header + one IOA
    EXPECT_EQ(encode_asdu(header_for(102, true), list.data(), 128, kIec104Profile, out.data(),
                          out.size(), len),
              IEC104_ERR_OUT_OF_RANGE);
}
