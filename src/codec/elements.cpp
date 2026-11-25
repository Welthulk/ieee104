/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file elements.cpp
 * @brief Implementation of the information element codec.
 *
 * See elements.hpp for the bit layouts and the design rules (lossless
 * decoding, range check on encoding, fixed lengths).
 *
 * Layer: codec (pure functions, no I/O, no allocation).
 */
#include "codec/elements.hpp"

#include "codec/byte_io.hpp"

#include <cstring>

namespace iec104::codec {
namespace {

// Bits shared by several elements (see the tables in elements.hpp).
constexpr std::uint8_t kBit0 = 0x01U;
constexpr std::uint8_t kBit1 = 0x02U;
constexpr std::uint8_t kBit6 = 0x40U;
constexpr std::uint8_t kBit7 = 0x80U;
constexpr std::uint8_t kTwoBitMask = 0x03U;   // DPI, ES, DCS, RCS, FRZ
constexpr std::uint8_t kSixBitMask = 0x3FU;   // RQT, KPA
constexpr std::uint8_t kSevenBitMask = 0x7FU; // VTI value, QL, COI cause

// Qualifier of command (QU) in SCO, DCO, RCO: bits 2..6.
constexpr unsigned kQuShift = 2U;
constexpr std::uint8_t kQuMax = 31U;

// FRZ of QCC: bits 6..7.
constexpr unsigned kFrzShift = 6U;

// BCR, fifth octet: SQ bits 0..4, CY, CA, IV.
constexpr std::uint8_t kBcrSequenceMask = 0x1FU;
constexpr std::uint8_t kBcrCarry = 0x20U;
constexpr std::uint8_t kBcrAdjusted = 0x40U;
constexpr std::uint8_t kBcrInvalid = 0x80U;

// CP24Time2a minute octet: identical to octet 3 of CP56Time2a.
constexpr std::uint8_t kMinuteMask = 0x3FU;

/** @brief All bits of an octet except those of @p mask (unsigned, no int promotion). */
constexpr std::uint8_t inverted(std::uint8_t mask) noexcept {
    return static_cast<std::uint8_t>(0xFFU ^ mask);
}

/** @brief @p value masked with @p mask and moved to bit position @p shift, as unsigned. */
constexpr unsigned field(std::uint8_t value, std::uint8_t mask, unsigned shift) noexcept {
    return (static_cast<unsigned>(value) & mask) << shift;
}

/** @brief Octet with bit 7 set if @p flag is true. */
constexpr std::uint8_t bit7_if(bool flag) noexcept {
    return flag ? kBit7 : 0U;
}

} // namespace

// ---------------------------------------------------------------------------
// Element kinds
// ---------------------------------------------------------------------------

std::size_t element_length(ElementKind kind) noexcept {
    switch (kind) {
    case ElementKind::Siq:
    case ElementKind::Diq:
    case ElementKind::Vti:
    case ElementKind::Qds:
    case ElementKind::Sep:
    case ElementKind::Spe:
    case ElementKind::Oci:
    case ElementKind::Qdp:
    case ElementKind::Sco:
    case ElementKind::Dco:
    case ElementKind::Rco:
    case ElementKind::Qos:
    case ElementKind::Coi:
    case ElementKind::Qoi:
    case ElementKind::Qcc:
    case ElementKind::Qrp:
    case ElementKind::Qpm:
    case ElementKind::Qpa:
        return 1U;
    case ElementKind::Nva:
    case ElementKind::Sva:
    case ElementKind::Fbp:
    case ElementKind::Tsc:
    case ElementKind::Cp16:
        return 2U;
    case ElementKind::Cp24:
        return kCp24Time2aLength;
    case ElementKind::Bsi:
    case ElementKind::R32:
        return 4U;
    case ElementKind::Scd:
        return kScdLength;
    case ElementKind::Bcr:
        return kBcrLength;
    case ElementKind::Cp56:
        return kCp56Time2aLength;
    }
    return 0U; // unreachable for valid enum values
}

const char* element_name(ElementKind kind) noexcept {
    switch (kind) {
    case ElementKind::Siq:
        return "SIQ";
    case ElementKind::Diq:
        return "DIQ";
    case ElementKind::Vti:
        return "VTI";
    case ElementKind::Qds:
        return "QDS";
    case ElementKind::Bsi:
        return "BSI";
    case ElementKind::Nva:
        return "NVA";
    case ElementKind::Sva:
        return "SVA";
    case ElementKind::R32:
        return "R32";
    case ElementKind::Bcr:
        return "BCR";
    case ElementKind::Scd:
        return "SCD";
    case ElementKind::Sep:
        return "SEP";
    case ElementKind::Spe:
        return "SPE";
    case ElementKind::Oci:
        return "OCI";
    case ElementKind::Qdp:
        return "QDP";
    case ElementKind::Sco:
        return "SCO";
    case ElementKind::Dco:
        return "DCO";
    case ElementKind::Rco:
        return "RCO";
    case ElementKind::Qos:
        return "QOS";
    case ElementKind::Coi:
        return "COI";
    case ElementKind::Qoi:
        return "QOI";
    case ElementKind::Qcc:
        return "QCC";
    case ElementKind::Qrp:
        return "QRP";
    case ElementKind::Qpm:
        return "QPM";
    case ElementKind::Qpa:
        return "QPA";
    case ElementKind::Fbp:
        return "FBP";
    case ElementKind::Tsc:
        return "TSC";
    case ElementKind::Cp16:
        return "CP16Time2a";
    case ElementKind::Cp24:
        return "CP24Time2a";
    case ElementKind::Cp56:
        return "CP56Time2a";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Monitor direction
// ---------------------------------------------------------------------------

bool is_valid(const Siq& e) noexcept {
    return (e.quality & kBit0) == 0U;
}

std::uint8_t encode_siq(const Siq& e) noexcept {
    return static_cast<std::uint8_t>((e.quality & inverted(kBit0)) | (e.on ? kBit0 : 0U));
}

Siq decode_siq(std::uint8_t octet) noexcept {
    Siq e{};
    e.on = (octet & kBit0) != 0U;
    e.quality = static_cast<std::uint8_t>(octet & inverted(kBit0));
    return e;
}

bool is_valid(const Diq& e) noexcept {
    return static_cast<std::uint8_t>(e.state) <= kTwoBitMask && (e.quality & kTwoBitMask) == 0U;
}

std::uint8_t encode_diq(const Diq& e) noexcept {
    return static_cast<std::uint8_t>((e.quality & inverted(kTwoBitMask)) |
                                     (static_cast<std::uint8_t>(e.state) & kTwoBitMask));
}

Diq decode_diq(std::uint8_t octet) noexcept {
    Diq e{};
    e.state = static_cast<DoublePoint>(octet & kTwoBitMask);
    e.quality = static_cast<std::uint8_t>(octet & inverted(kTwoBitMask));
    return e;
}

bool is_valid(const Vti& e) noexcept {
    return e.value >= -64 && e.value <= 63;
}

std::uint8_t encode_vti(const Vti& e) noexcept {
    // Two's complement in 7 bits: the low 7 bits of the 8-bit two's
    // complement are exactly the 7-bit representation for -64..63.
    const auto raw = static_cast<std::uint8_t>(e.value);
    return static_cast<std::uint8_t>((raw & kSevenBitMask) | bit7_if(e.transient));
}

Vti decode_vti(std::uint8_t octet) noexcept {
    Vti e{};
    // Bit 6 is the sign of the 7-bit value; subtracting 128 when it is set
    // sign-extends without relying on implementation-defined shifts.
    const int raw = octet & kSevenBitMask;
    e.value = static_cast<std::int8_t>((raw & kBit6) != 0U ? raw - 128 : raw);
    e.transient = (octet & kBit7) != 0U;
    return e;
}

bool is_valid(const Bcr& e) noexcept {
    return e.sequence <= kBcrSequenceMask;
}

void encode_bcr(const Bcr& e, std::uint8_t* out) noexcept {
    detail::put_uint_le(out, static_cast<std::uint32_t>(e.counter), 4U);
    out[4] = static_cast<std::uint8_t>(
        (e.sequence & kBcrSequenceMask) | (e.carry ? kBcrCarry : 0U) |
        (e.adjusted ? kBcrAdjusted : 0U) | (e.invalid ? kBcrInvalid : 0U));
}

Bcr decode_bcr(const std::uint8_t* in) noexcept {
    Bcr e{};
    e.counter = static_cast<std::int32_t>(detail::get_uint_le(in, 4U));
    e.sequence = static_cast<std::uint8_t>(in[4] & kBcrSequenceMask);
    e.carry = (in[4] & kBcrCarry) != 0U;
    e.adjusted = (in[4] & kBcrAdjusted) != 0U;
    e.invalid = (in[4] & kBcrInvalid) != 0U;
    return e;
}

void encode_scd(const Scd& e, std::uint8_t* out) noexcept {
    detail::put_u16_le(out, e.status);
    detail::put_u16_le(out + 2, e.change);
}

Scd decode_scd(const std::uint8_t* in) noexcept {
    Scd e{};
    e.status = detail::get_u16_le(in);
    e.change = detail::get_u16_le(in + 2);
    return e;
}

bool is_valid(const Sep& e) noexcept {
    return static_cast<std::uint8_t>(e.state) <= kTwoBitMask && (e.quality & kTwoBitMask) == 0U;
}

std::uint8_t encode_sep(const Sep& e) noexcept {
    return static_cast<std::uint8_t>((e.quality & inverted(kTwoBitMask)) |
                                     (static_cast<std::uint8_t>(e.state) & kTwoBitMask));
}

Sep decode_sep(std::uint8_t octet) noexcept {
    Sep e{};
    e.state = static_cast<EventState>(octet & kTwoBitMask);
    e.quality = static_cast<std::uint8_t>(octet & inverted(kTwoBitMask));
    return e;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool is_valid(const Sco& e) noexcept {
    return e.qu <= kQuMax;
}

std::uint8_t encode_sco(const Sco& e) noexcept {
    return static_cast<std::uint8_t>((e.on ? kBit0 : 0U) | (e.reserved ? kBit1 : 0U) |
                                     field(e.qu, kQuMax, kQuShift) | bit7_if(e.select));
}

Sco decode_sco(std::uint8_t octet) noexcept {
    Sco e{};
    e.on = (octet & kBit0) != 0U;
    e.reserved = (octet & kBit1) != 0U;
    e.qu = static_cast<std::uint8_t>((octet >> kQuShift) & kQuMax);
    e.select = (octet & kBit7) != 0U;
    return e;
}

bool is_valid(const Dco& e) noexcept {
    return e.state <= kTwoBitMask && e.qu <= kQuMax;
}

std::uint8_t encode_dco(const Dco& e) noexcept {
    return static_cast<std::uint8_t>((e.state & kTwoBitMask) | field(e.qu, kQuMax, kQuShift) |
                                     bit7_if(e.select));
}

Dco decode_dco(std::uint8_t octet) noexcept {
    Dco e{};
    e.state = static_cast<std::uint8_t>(octet & kTwoBitMask);
    e.qu = static_cast<std::uint8_t>((octet >> kQuShift) & kQuMax);
    e.select = (octet & kBit7) != 0U;
    return e;
}

bool is_valid(const Rco& e) noexcept {
    return e.state <= kTwoBitMask && e.qu <= kQuMax;
}

std::uint8_t encode_rco(const Rco& e) noexcept {
    return static_cast<std::uint8_t>((e.state & kTwoBitMask) | field(e.qu, kQuMax, kQuShift) |
                                     bit7_if(e.select));
}

Rco decode_rco(std::uint8_t octet) noexcept {
    Rco e{};
    e.state = static_cast<std::uint8_t>(octet & kTwoBitMask);
    e.qu = static_cast<std::uint8_t>((octet >> kQuShift) & kQuMax);
    e.select = (octet & kBit7) != 0U;
    return e;
}

bool is_valid(const Qos& e) noexcept {
    return e.ql <= kSevenBitMask;
}

std::uint8_t encode_qos(const Qos& e) noexcept {
    return static_cast<std::uint8_t>((e.ql & kSevenBitMask) | bit7_if(e.select));
}

Qos decode_qos(std::uint8_t octet) noexcept {
    Qos e{};
    e.ql = static_cast<std::uint8_t>(octet & kSevenBitMask);
    e.select = (octet & kBit7) != 0U;
    return e;
}

// ---------------------------------------------------------------------------
// System information and parameters
// ---------------------------------------------------------------------------

bool is_valid(const Coi& e) noexcept {
    return e.cause <= kSevenBitMask;
}

std::uint8_t encode_coi(const Coi& e) noexcept {
    return static_cast<std::uint8_t>((e.cause & kSevenBitMask) | bit7_if(e.parameters_changed));
}

Coi decode_coi(std::uint8_t octet) noexcept {
    Coi e{};
    e.cause = static_cast<std::uint8_t>(octet & kSevenBitMask);
    e.parameters_changed = (octet & kBit7) != 0U;
    return e;
}

bool is_valid(const Qcc& e) noexcept {
    return e.rqt <= kSixBitMask && e.frz <= kTwoBitMask;
}

std::uint8_t encode_qcc(const Qcc& e) noexcept {
    return static_cast<std::uint8_t>((e.rqt & kSixBitMask) | field(e.frz, kTwoBitMask, kFrzShift));
}

Qcc decode_qcc(std::uint8_t octet) noexcept {
    Qcc e{};
    e.rqt = static_cast<std::uint8_t>(octet & kSixBitMask);
    e.frz = static_cast<std::uint8_t>((octet >> kFrzShift) & kTwoBitMask);
    return e;
}

bool is_valid(const Qpm& e) noexcept {
    return e.kpa <= kSixBitMask;
}

std::uint8_t encode_qpm(const Qpm& e) noexcept {
    return static_cast<std::uint8_t>((e.kpa & kSixBitMask) | (e.local_change ? kBit6 : 0U) |
                                     bit7_if(e.not_in_operation));
}

Qpm decode_qpm(std::uint8_t octet) noexcept {
    Qpm e{};
    e.kpa = static_cast<std::uint8_t>(octet & kSixBitMask);
    e.local_change = (octet & kBit6) != 0U;
    e.not_in_operation = (octet & kBit7) != 0U;
    return e;
}

// ---------------------------------------------------------------------------
// Time and floating point
// ---------------------------------------------------------------------------

bool is_valid(const Cp24Time2a& e) noexcept {
    return e.milliseconds <= 59999U && e.minute <= 59U;
}

void encode_cp24time2a(const Cp24Time2a& e, std::uint8_t* out) noexcept {
    detail::put_u16_le(out, e.milliseconds);
    out[2] = static_cast<std::uint8_t>((e.minute & kMinuteMask) | (e.res1 ? kBit6 : 0U) |
                                       bit7_if(e.invalid));
}

Cp24Time2a decode_cp24time2a(const std::uint8_t* in) noexcept {
    Cp24Time2a e{};
    e.milliseconds = detail::get_u16_le(in);
    e.minute = static_cast<std::uint8_t>(in[2] & kMinuteMask);
    e.res1 = (in[2] & kBit6) != 0U;
    e.invalid = (in[2] & kBit7) != 0U;
    return e;
}

void encode_r32(float value, std::uint8_t* out) noexcept {
    // memcpy is the portable way to get at the bit pattern of a float
    // (no aliasing violation, no change of NaN payloads).
    static_assert(sizeof(float) == 4U, "R32 needs a 32-bit IEEE 754 float");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    detail::put_uint_le(out, bits, 4U);
}

float decode_r32(const std::uint8_t* in) noexcept {
    const std::uint32_t bits = detail::get_uint_le(in, 4U);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

} // namespace iec104::codec
