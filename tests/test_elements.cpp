/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_elements.cpp
 * @brief Unit tests for the information elements (src/codec/elements.hpp).
 *
 * Reference octets come from tests/data/lib60870_asdu_reference.txt
 * ("reference"; encoded by lib60870-C v2.4.1, checked with Wireshark 3.4.10)
 * and name the type line they were taken from. Rules about reserved bits and
 * ranges are checked with octets derived from the layouts in elements.hpp
 * ("layout").
 */
#include "codec/elements.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>

using namespace iec104::codec;

// Rule: SIQ bit 0 = SPI, upper bits = quality.
// Source: reference, M_SP_NA_1 SQ0 (0x90 = off, IV BL) and SQ1 (0x21 = on, SB).
TEST(Elements, Siq) {
    Siq e = decode_siq(0x90);
    EXPECT_FALSE(e.on);
    EXPECT_EQ(e.quality, kQualityInvalid | kQualityBlocked);
    EXPECT_EQ(encode_siq(e), 0x90U);

    e = decode_siq(0x21);
    EXPECT_TRUE(e.on);
    EXPECT_EQ(e.quality, kQualitySubstituted);
    EXPECT_EQ(encode_siq(e), 0x21U);

    // Quality must not overlap the SPI bit.
    EXPECT_FALSE(is_valid(Siq{false, 0x01}));
}

// Rule: DIQ bits 0..1 = DPI.
// Source: reference, M_DP_NA_1 SQ0 (0x83 = indeterminate, IV).
TEST(Elements, Diq) {
    const Diq e = decode_diq(0x83);
    EXPECT_EQ(e.state, DoublePoint::Indeterminate);
    EXPECT_EQ(e.quality, kQualityInvalid);
    EXPECT_EQ(encode_diq(e), 0x83U);
    EXPECT_FALSE(is_valid(Diq{DoublePoint::On, 0x02}));
}

// Rule: VTI value is a 7-bit two's complement, bit 7 = transient.
// Source: reference, M_ST_NA_1 (0xC0 = -64 transient, 0x3F = 63, 0x7F = -1).
TEST(Elements, Vti) {
    Vti e = decode_vti(0xC0);
    EXPECT_EQ(e.value, -64);
    EXPECT_TRUE(e.transient);
    EXPECT_EQ(encode_vti(e), 0xC0U);
    EXPECT_EQ(decode_vti(0x3F).value, 63);
    EXPECT_EQ(decode_vti(0x7F).value, -1);
    EXPECT_EQ(encode_vti(Vti{-1, false}), 0x7FU);

    EXPECT_TRUE(is_valid(Vti{-64, false}));
    EXPECT_FALSE(is_valid(Vti{64, false}));
    EXPECT_FALSE(is_valid(Vti{-65, false}));
}

// Rule: BCR = signed 32-bit counter + sequence/CY/CA/IV octet.
// Source: reference, M_IT_NA_1 SQ0 (40 E2 01 00 27 = 123456, seq 7, CY;
// FE FF FF FF DF = -2, seq 31, CA, IV).
TEST(Elements, Bcr) {
    const std::array<std::uint8_t, 5> a{0x40, 0xE2, 0x01, 0x00, 0x27};
    Bcr e = decode_bcr(a.data());
    EXPECT_EQ(e.counter, 123456);
    EXPECT_EQ(e.sequence, 7U);
    EXPECT_TRUE(e.carry);
    EXPECT_FALSE(e.adjusted);
    EXPECT_FALSE(e.invalid);

    const std::array<std::uint8_t, 5> b{0xFE, 0xFF, 0xFF, 0xFF, 0xDF};
    e = decode_bcr(b.data());
    EXPECT_EQ(e.counter, -2);
    EXPECT_EQ(e.sequence, 31U);
    EXPECT_TRUE(e.adjusted);
    EXPECT_TRUE(e.invalid);
    std::array<std::uint8_t, 5> out{};
    encode_bcr(e, out.data());
    EXPECT_EQ(out, b);

    e.sequence = 32;
    EXPECT_FALSE(is_valid(e));
}

// Rule: SEP bits 0..1 = event state, EI = 0x08.
// Source: reference, M_EP_TD_1 (0x89 = off, EI, IV; lib60870 encoding only).
TEST(Elements, Sep) {
    const Sep e = decode_sep(0x89);
    EXPECT_EQ(e.state, EventState::Off);
    EXPECT_EQ(e.quality, kQualityElapsedTimeInvalid | kQualityInvalid);
    EXPECT_EQ(encode_sep(e), 0x89U);
}

// Rule: SCO/DCO/RCO = state bits, QU in bits 2..6, S/E in bit 7.
// Source: reference, C_SC_NA_1 (0x81), C_DC_NA_1 (0x09), C_RC_NA_1 (0x06),
// C_RC_TA_1 (0x0D).
TEST(Elements, Commands) {
    const Sco sco = decode_sco(0x81);
    EXPECT_TRUE(sco.on);
    EXPECT_EQ(sco.qu, 0U);
    EXPECT_TRUE(sco.select);
    EXPECT_EQ(encode_sco(sco), 0x81U);

    const Dco dco = decode_dco(0x09);
    EXPECT_EQ(dco.state, 1U);
    EXPECT_EQ(dco.qu, 2U);
    EXPECT_FALSE(dco.select);
    EXPECT_EQ(encode_dco(dco), 0x09U);

    const Rco rco = decode_rco(0x06);
    EXPECT_EQ(rco.state, 2U);
    EXPECT_EQ(rco.qu, 1U);
    EXPECT_EQ(encode_rco(rco), 0x06U);
    EXPECT_EQ(encode_rco(Rco{1, 3, false}), 0x0DU);

    EXPECT_FALSE(is_valid(Sco{true, false, 32, false}));
    EXPECT_FALSE(is_valid(Dco{4, 0, false}));
    EXPECT_FALSE(is_valid(Rco{0, 32, false}));
}

// Rule: the reserved bit 1 of SCO is kept (lossless decoding).
// Source: layout (SCO bit 1 = RES); octet chosen for the test.
TEST(Elements, ScoReservedBitIsKept) {
    const Sco e = decode_sco(0x03);
    EXPECT_TRUE(e.on);
    EXPECT_TRUE(e.reserved);
    EXPECT_EQ(encode_sco(e), 0x03U);
}

// Rule: QOS = QL bits 0..6, S/E bit 7.
// Source: reference, C_SE_NA_1 (0x80 = QL 0, select).
TEST(Elements, Qos) {
    const Qos e = decode_qos(0x80);
    EXPECT_EQ(e.ql, 0U);
    EXPECT_TRUE(e.select);
    EXPECT_EQ(encode_qos(Qos{127, false}), 0x7FU);
    EXPECT_FALSE(is_valid(Qos{128, false}));
}

// Rule: COI, QCC, QPM bit fields.
// Source: reference, M_EI_NA_1 (COI 0x81), C_CI_NA_1 (QCC 0x85), P_ME_NB_1
// (QPM 0x43), P_ME_NC_1 (QPM 0x84).
TEST(Elements, SystemQualifiers) {
    const Coi coi = decode_coi(0x81);
    EXPECT_EQ(coi.cause, 1U);
    EXPECT_TRUE(coi.parameters_changed);
    EXPECT_EQ(encode_coi(coi), 0x81U);

    const Qcc qcc = decode_qcc(0x85);
    EXPECT_EQ(qcc.rqt, 5U);
    EXPECT_EQ(qcc.frz, 2U);
    EXPECT_EQ(encode_qcc(qcc), 0x85U);
    EXPECT_FALSE(is_valid(Qcc{64, 0}));
    EXPECT_FALSE(is_valid(Qcc{1, 4}));

    const Qpm nb = decode_qpm(0x43);
    EXPECT_EQ(nb.kpa, 3U);
    EXPECT_TRUE(nb.local_change);
    EXPECT_FALSE(nb.not_in_operation);
    const Qpm nc = decode_qpm(0x84);
    EXPECT_EQ(nc.kpa, 4U);
    EXPECT_TRUE(nc.not_in_operation);
    EXPECT_EQ(encode_qpm(nc), 0x84U);
    EXPECT_FALSE(is_valid(Qpm{64, false, false}));
}

// Rule: CP24Time2a = milliseconds + minute octet as in CP56Time2a.
// Source: reference, M_SP_TA_1 (2B 17 01 = 5931 ms, minute 1).
TEST(Elements, Cp24Time2a) {
    const std::array<std::uint8_t, 3> in{0x2B, 0x17, 0x01};
    const Cp24Time2a e = decode_cp24time2a(in.data());
    EXPECT_EQ(e.milliseconds, 5931U);
    EXPECT_EQ(e.minute, 1U);
    EXPECT_FALSE(e.invalid);
    std::array<std::uint8_t, 3> out{};
    encode_cp24time2a(e, out.data());
    EXPECT_EQ(out, in);

    // IV and RES1 survive the round trip (layout).
    const std::array<std::uint8_t, 3> flags{0x00, 0x00, 0xC5};
    const Cp24Time2a f = decode_cp24time2a(flags.data());
    EXPECT_TRUE(f.invalid);
    EXPECT_TRUE(f.res1);
    EXPECT_EQ(f.minute, 5U);
    encode_cp24time2a(f, out.data());
    EXPECT_EQ(out, flags);

    EXPECT_FALSE(is_valid(Cp24Time2a{60000, 0, false, false}));
    EXPECT_FALSE(is_valid(Cp24Time2a{0, 60, false, false}));
}

// Rule: R32 is IEEE 754 single precision, little endian; the bit pattern is
// kept exactly (NaN payload, negative zero).
// Source: reference, M_ME_NC_1 (00 00 C0 3F = 1.5, 00 40 66 C3 = -230.25);
// NaN octets chosen for the test.
TEST(Elements, R32) {
    const std::array<std::uint8_t, 4> a{0x00, 0x00, 0xC0, 0x3F};
    EXPECT_FLOAT_EQ(decode_r32(a.data()), 1.5F);
    std::array<std::uint8_t, 4> out{};
    encode_r32(-230.25F, out.data());
    EXPECT_EQ(out, (std::array<std::uint8_t, 4>{0x00, 0x40, 0x66, 0xC3}));

    const std::array<std::uint8_t, 4> nan{0x01, 0x00, 0xC0, 0x7F};
    const float f = decode_r32(nan.data());
    EXPECT_TRUE(std::isnan(f));
    encode_r32(f, out.data());
    EXPECT_EQ(out, nan);
}

// Rule: element lengths as listed in elements.hpp (sum check against the
// reference ASDUs is done in test_asdu_reference.cpp).
// Source: docs/03 (element lengths citing Wireshark).
TEST(Elements, Lengths) {
    EXPECT_EQ(element_length(ElementKind::Siq), 1U);
    EXPECT_EQ(element_length(ElementKind::Nva), 2U);
    EXPECT_EQ(element_length(ElementKind::Cp24), 3U);
    EXPECT_EQ(element_length(ElementKind::R32), 4U);
    EXPECT_EQ(element_length(ElementKind::Bcr), 5U);
    EXPECT_EQ(element_length(ElementKind::Cp56), 7U);
    EXPECT_STREQ(element_name(ElementKind::Qds), "QDS");
}
