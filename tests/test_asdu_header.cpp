/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_asdu_header.cpp
 * @brief Unit tests for the data unit identifier and IOA codec
 *        (src/codec/asdu_header.hpp).
 *
 * Reference bytes come from two kinds of sources, named at every test:
 *   - "capture": tests/data/lib60870_capture.txt, produced by an independent
 *     implementation (lib60870-C v2.4.1, used only as external test peer);
 *   - "layout": derived by hand from the bit layouts documented in
 *     asdu_header.hpp, which follow docs/03 (citing the Wireshark dissector).
 */
#include "codec/apci.hpp"
#include "codec/asdu_header.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <gtest/gtest.h>

using namespace iec104::codec;

namespace {

// General interrogation C_IC_NA_1 (100), COT 6 activation, ORG 0, CA 1,
// IOA 0, QOI 20 (station interrogation). Source: layout.
const std::uint8_t kGeneralInterrogation[] = {
    0x68, 0x0E, 0x00, 0x00, 0x00, 0x00, // APCI, N(S) = N(R) = 0
    0x64, 0x01, 0x06, 0x00, 0x01, 0x00, // type 100, VSQ 1, COT 6, ORG 0, CA 1
    0x00, 0x00, 0x00,                   // IOA 0
    0x14};                              // QOI 20 (station interrogation)

// The same command as sent by a peer: originator address 3, N(R) = 2.
// Source: capture, line "SEND: 68 0e 00 00 04 00 64 01 06 03 01 00 00 00 00 14".
const std::uint8_t kCapturedGeneralInterrogation[] = {
    0x68, 0x0E, 0x00, 0x00, 0x04, 0x00, 0x64, 0x01, 0x06, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x14};

// Interrogated single points with SQ = 1: type 1, 8 objects, COT 20, ORG 3,
// CA 1, start IOA 300 (0x012C).
// Source: capture, line "RCVD: 68 15 0a 00 02 00 01 88 14 03 01 00 2c 01 00 ...".
const std::uint8_t kCapturedSequenceHeader[] = {0x01, 0x88, 0x14, 0x03, 0x01, 0x00};

} // namespace

// Rule: a complete I-frame splits into APCI, 6-octet header (IEC 104
// profile) and a 3-octet IOA; IOA 0 addresses the station.
// Source: layout.
TEST(AsduHeader, DecodeGeneralInterrogation) {
    Apci apci{};
    ASSERT_EQ(decode_apci(kGeneralInterrogation, sizeof kGeneralInterrogation, apci), IEC104_OK);
    ASSERT_EQ(apci.format, FrameFormat::I);
    ASSERT_EQ(apci.asdu_length, 10U);

    const std::uint8_t* asdu = kGeneralInterrogation + kApciLength;
    AsduHeader h{};
    std::size_t consumed = 0;
    ASSERT_EQ(decode_asdu_header(asdu, apci.asdu_length, kIec104Profile, h, consumed), IEC104_OK);
    EXPECT_EQ(consumed, 6U);
    EXPECT_EQ(h.type_id, 100U);
    EXPECT_FALSE(h.sequence);
    EXPECT_EQ(h.num_objects, 1U);
    EXPECT_EQ(h.cause, 6U);
    EXPECT_FALSE(h.negative);
    EXPECT_FALSE(h.test);
    EXPECT_EQ(h.originator, 0U);
    EXPECT_EQ(h.common_address, 1U);

    std::uint32_t ioa = 99;
    std::size_t ioa_len = 0;
    ASSERT_EQ(
        decode_ioa(asdu + consumed, apci.asdu_length - consumed, kIec104Profile, ioa, ioa_len),
        IEC104_OK);
    EXPECT_EQ(ioa, 0U);
    EXPECT_EQ(ioa_len, 3U);
}

// Rule: as DecodeGeneralInterrogation, plus the originator address in the
// second COT octet, checked against a peer's frame in both directions.
// Source: capture (kCapturedGeneralInterrogation).
TEST(AsduHeader, GeneralInterrogationFromCapture) {
    Apci apci{};
    ASSERT_EQ(
        decode_apci(kCapturedGeneralInterrogation, sizeof kCapturedGeneralInterrogation, apci),
        IEC104_OK);
    ASSERT_EQ(apci.asdu_length, 10U);

    const std::uint8_t* asdu = kCapturedGeneralInterrogation + kApciLength;
    AsduHeader h{};
    std::size_t consumed = 0;
    ASSERT_EQ(decode_asdu_header(asdu, apci.asdu_length, kIec104Profile, h, consumed), IEC104_OK);
    EXPECT_EQ(h.type_id, 100U);
    EXPECT_EQ(h.num_objects, 1U);
    EXPECT_EQ(h.cause, 6U);
    EXPECT_EQ(h.originator, 3U);
    EXPECT_EQ(h.common_address, 1U);

    std::array<std::uint8_t, 6> re{};
    std::size_t written = 0;
    ASSERT_EQ(encode_asdu_header(h, kIec104Profile, re.data(), re.size(), written), IEC104_OK);
    EXPECT_EQ(written, consumed);
    EXPECT_TRUE(std::equal(re.begin(), re.end(), asdu));
}

// Rule: the SQ bit is bit 7 of the VSQ octet, the number of objects bits 0..6.
// Source: capture (kCapturedSequenceHeader).
TEST(AsduHeader, SequenceBitFromCapture) {
    AsduHeader h{};
    std::size_t consumed = 0;
    ASSERT_EQ(decode_asdu_header(kCapturedSequenceHeader, sizeof kCapturedSequenceHeader,
                                 kIec104Profile, h, consumed),
              IEC104_OK);
    EXPECT_EQ(h.type_id, 1U);
    EXPECT_TRUE(h.sequence);
    EXPECT_EQ(h.num_objects, 8U);
    EXPECT_EQ(h.cause, 20U);
    EXPECT_EQ(h.originator, 3U);
    EXPECT_EQ(h.common_address, 1U);
}

// Rule: every flag and every field at its maximum survives a round trip and
// lands at the documented bit position (SQ 0x80, P/N 0x40, T 0x80).
// Source: layout (type 13, SQ + 127 -> FF, T + P/N + cause 47 -> EF).
TEST(AsduHeader, EncodeRoundTripAllFlags) {
    AsduHeader h{};
    h.type_id = 13;
    h.sequence = true;
    h.num_objects = 127;
    h.cause = 47;
    h.negative = true;
    h.test = true;
    h.originator = 0xAB;
    h.common_address = 0xFFFF;

    std::array<std::uint8_t, 6> buf{};
    std::size_t written = 0;
    ASSERT_EQ(encode_asdu_header(h, kIec104Profile, buf.data(), buf.size(), written), IEC104_OK);
    EXPECT_EQ(written, 6U);
    EXPECT_EQ(buf, (std::array<std::uint8_t, 6>{0x0D, 0xFF, 0xEF, 0xAB, 0xFF, 0xFF}));

    AsduHeader d{};
    std::size_t consumed = 0;
    ASSERT_EQ(decode_asdu_header(buf.data(), buf.size(), kIec104Profile, d, consumed), IEC104_OK);
    EXPECT_EQ(d.type_id, h.type_id);
    EXPECT_EQ(d.sequence, h.sequence);
    EXPECT_EQ(d.num_objects, h.num_objects);
    EXPECT_EQ(d.cause, h.cause);
    EXPECT_EQ(d.negative, h.negative);
    EXPECT_EQ(d.test, h.test);
    EXPECT_EQ(d.originator, h.originator);
    EXPECT_EQ(d.common_address, h.common_address);
}

// Rule: with a non-IEC-104 profile (COT 1, CA 1, IOA 2) the header shrinks to
// 4 octets, there is no originator octet, and values that do not fit the
// shorter fields are rejected.
// Source: layout; profile option from docs/02 ("ASDU-Profil").
TEST(AsduHeader, ShortProfile) {
    const AsduProfile p{1, 1, 2};
    EXPECT_TRUE(is_valid(p));
    EXPECT_EQ(header_length(p), 4U);

    AsduHeader h{};
    h.type_id = 1;
    h.num_objects = 1;
    h.cause = 3;
    h.common_address = 200;
    std::array<std::uint8_t, 4> buf{};
    std::size_t written = 0;
    ASSERT_EQ(encode_asdu_header(h, p, buf.data(), buf.size(), written), IEC104_OK);
    EXPECT_EQ(buf, (std::array<std::uint8_t, 4>{0x01, 0x01, 0x03, 0xC8}));

    h.common_address = 256; // does not fit into one octet
    EXPECT_EQ(encode_asdu_header(h, p, buf.data(), buf.size(), written), IEC104_ERR_OUT_OF_RANGE);
    h.common_address = 1;
    h.originator = 1; // no originator octet with cot_length 1
    EXPECT_EQ(encode_asdu_header(h, p, buf.data(), buf.size(), written), IEC104_ERR_OUT_OF_RANGE);
}

// Rule: invalid profiles, short buffers and out-of-range header fields are
// reported with the documented status codes.
// Source: asdu_header.hpp (API contract).
TEST(AsduHeader, RejectsInvalidInput) {
    AsduHeader h{};
    std::array<std::uint8_t, 6> buf{};
    std::size_t n = 0;
    EXPECT_FALSE(is_valid(AsduProfile{0, 2, 3}));
    EXPECT_FALSE(is_valid(AsduProfile{2, 3, 3}));
    EXPECT_FALSE(is_valid(AsduProfile{2, 2, 4}));
    EXPECT_EQ(encode_asdu_header(h, AsduProfile{2, 2, 4}, buf.data(), buf.size(), n),
              IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(encode_asdu_header(h, kIec104Profile, buf.data(), 5, n), IEC104_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(decode_asdu_header(buf.data(), 5, kIec104Profile, h, n), IEC104_ERR_NEED_MORE_DATA);

    h.num_objects = 128;
    EXPECT_EQ(encode_asdu_header(h, kIec104Profile, buf.data(), buf.size(), n),
              IEC104_ERR_OUT_OF_RANGE);
    h.num_objects = 1;
    h.cause = 64;
    EXPECT_EQ(encode_asdu_header(h, kIec104Profile, buf.data(), buf.size(), n),
              IEC104_ERR_OUT_OF_RANGE);
}

// Rule: a 3-octet IOA covers 0..0xFFFFFF little endian; larger values are
// rejected; decoding needs all three octets.
// Source: layout (4711 = 0x1267 -> 67 12 00).
TEST(AsduHeader, IoaRanges) {
    std::array<std::uint8_t, 3> buf{};
    std::size_t n = 0;
    ASSERT_EQ(encode_ioa(0xFFFFFF, kIec104Profile, buf.data(), buf.size(), n), IEC104_OK);
    EXPECT_EQ(buf, (std::array<std::uint8_t, 3>{0xFF, 0xFF, 0xFF}));
    EXPECT_EQ(encode_ioa(0x1000000, kIec104Profile, buf.data(), buf.size(), n),
              IEC104_ERR_OUT_OF_RANGE);

    ASSERT_EQ(encode_ioa(4711, kIec104Profile, buf.data(), buf.size(), n), IEC104_OK);
    EXPECT_EQ(buf, (std::array<std::uint8_t, 3>{0x67, 0x12, 0x00}));
    std::uint32_t ioa = 0;
    ASSERT_EQ(decode_ioa(buf.data(), buf.size(), kIec104Profile, ioa, n), IEC104_OK);
    EXPECT_EQ(ioa, 4711U);
    EXPECT_EQ(decode_ioa(buf.data(), 2, kIec104Profile, ioa, n), IEC104_ERR_NEED_MORE_DATA);
}
