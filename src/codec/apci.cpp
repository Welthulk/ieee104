/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file apci.cpp
 * @brief Implementation of APCI encoding and decoding.
 *
 * See apci.hpp for the frame layouts and the meaning of every function. This
 * file only contains the bit manipulation; it keeps no state.
 *
 * Layer: codec (pure functions, no I/O, no allocation).
 */
#include "codec/apci.hpp"

#include "codec/byte_io.hpp"

namespace iec104::codec {
namespace {

/// Bit 0 of the first control octet: 0 = I-format, 1 = S- or U-format.
constexpr std::uint8_t kFormatBitI = 0x01U;
/// Bits 1..0 of the first control octet: 01 = S-format, 11 = U-format.
constexpr std::uint8_t kFormatMaskSU = 0x03U;
constexpr std::uint8_t kFormatS = 0x01U;

/**
 * @brief Writes a 15-bit sequence number into two control octets.
 *
 * The number is shifted left by one bit because bit 0 of the first octet is
 * reserved: in octets 3/4 it is the I-format marker, in octets 5/6 it must be 0.
 *
 * Example: 2 -> 04 00
 */
void put_sequence(std::uint8_t* p, std::uint16_t seq) noexcept {
    detail::put_u16_le(p, static_cast<std::uint16_t>(seq << 1U));
}

/**
 * @brief Reads a 15-bit sequence number from two control octets.
 *
 * Inverse of put_sequence(): drops the reserved bit 0. Whether that bit was
 * zero is checked by the caller, because its meaning depends on the position.
 */
std::uint16_t get_sequence(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(detail::get_u16_le(p) >> 1U);
}

/**
 * @brief True if @p octet is exactly one of the six U-format functions.
 *
 * Comparing against the enumerators (instead of testing single bits) rejects
 * octets with two or more function bits set, which the standard does not
 * allow in one frame (docs/03, design consequences: never tolerated).
 */
bool is_valid_u_function(std::uint8_t octet) noexcept {
    switch (static_cast<UFunction>(octet)) {
    case UFunction::StartDtAct:
    case UFunction::StartDtCon:
    case UFunction::StopDtAct:
    case UFunction::StopDtCon:
    case UFunction::TestFrAct:
    case UFunction::TestFrCon:
        return true;
    case UFunction::None:
        return false;
    }
    // Any octet that is not an enumerator (e.g. 0x47 = STARTDT act + TESTFR act).
    return false;
}

} // namespace

iec104_status peek_apdu_length(const std::uint8_t* data, std::size_t size,
                               std::size_t& apdu_length) noexcept {
    if (data == nullptr && size != 0U) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (size < 2U) {
        return IEC104_ERR_NEED_MORE_DATA;
    }
    if (data[0] != kStartByte) {
        return IEC104_ERR_MALFORMED;
    }
    // The length octet counts the control field plus the ASDU, so it can never
    // be smaller than the control field itself.
    const std::size_t length = data[1];
    if (length < kControlFieldLength || length > kMaxLengthField) {
        return IEC104_ERR_MALFORMED;
    }
    apdu_length = length + 2U;
    return IEC104_OK;
}

iec104_status encode_i_header(std::uint16_t send_seq, std::uint16_t recv_seq,
                              std::size_t asdu_length, std::uint8_t* out,
                              std::size_t out_size) noexcept {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < kApciLength) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    // An I-frame without ASDU would be indistinguishable from a malformed
    // frame for the receiver, so asdu_length 0 is rejected here as well.
    if (send_seq >= kSequenceModulo || recv_seq >= kSequenceModulo || asdu_length == 0U ||
        asdu_length > kMaxAsduLength) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    out[0] = kStartByte;
    out[1] = static_cast<std::uint8_t>(asdu_length + kControlFieldLength);
    put_sequence(&out[2], send_seq); // bit 0 stays 0: I-format marker
    put_sequence(&out[4], recv_seq);
    return IEC104_OK;
}

iec104_status encode_s_frame(std::uint16_t recv_seq, std::uint8_t* out,
                             std::size_t out_size) noexcept {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < kApciLength) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    if (recv_seq >= kSequenceModulo) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    out[0] = kStartByte;
    out[1] = static_cast<std::uint8_t>(kControlFieldLength);
    out[2] = kFormatS;
    out[3] = 0x00U;
    put_sequence(&out[4], recv_seq);
    return IEC104_OK;
}

iec104_status encode_u_frame(UFunction function, std::uint8_t* out, std::size_t out_size) noexcept {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < kApciLength) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    if (!is_valid_u_function(static_cast<std::uint8_t>(function))) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    out[0] = kStartByte;
    out[1] = static_cast<std::uint8_t>(kControlFieldLength);
    out[2] = static_cast<std::uint8_t>(function);
    out[3] = 0x00U;
    out[4] = 0x00U;
    out[5] = 0x00U;
    return IEC104_OK;
}

iec104_status decode_apci(const std::uint8_t* data, std::size_t size, Apci& out) noexcept {
    std::size_t apdu_length = 0;
    const iec104_status st = peek_apdu_length(data, size, apdu_length);
    if (st != IEC104_OK) {
        return st;
    }
    if (size < apdu_length) {
        return IEC104_ERR_NEED_MORE_DATA;
    }
    const std::size_t length = data[1];
    const std::uint8_t c1 = data[2];

    // Decode into a local copy so that 'out' stays untouched on every error path.
    Apci result{};
    if ((c1 & kFormatBitI) == 0U) {
        // I-format. Bit 0 of octet 5 is reserved (shifted N(R)) and must be 0.
        if ((data[4] & 0x01U) != 0U || length == kControlFieldLength) {
            return IEC104_ERR_MALFORMED;
        }
        result.format = FrameFormat::I;
        result.send_seq = get_sequence(&data[2]);
        result.recv_seq = get_sequence(&data[4]);
        result.asdu_length = length - kControlFieldLength;
    } else if ((c1 & kFormatMaskSU) == kFormatS) {
        // S-format. All bits except the format marker in octets 3/4 are reserved.
        if (c1 != kFormatS || data[3] != 0U || (data[4] & 0x01U) != 0U ||
            length != kControlFieldLength) {
            return IEC104_ERR_MALFORMED;
        }
        result.format = FrameFormat::S;
        result.recv_seq = get_sequence(&data[4]);
    } else {
        // U-format. Octets 4..6 carry nothing and must be zero.
        if (!is_valid_u_function(c1) || data[3] != 0U || data[4] != 0U || data[5] != 0U ||
            length != kControlFieldLength) {
            return IEC104_ERR_MALFORMED;
        }
        result.format = FrameFormat::U;
        result.u_function = static_cast<UFunction>(c1);
    }
    out = result;
    return IEC104_OK;
}

} // namespace iec104::codec
