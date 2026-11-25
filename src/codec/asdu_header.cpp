/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file asdu_header.cpp
 * @brief Implementation of the data unit identifier and IOA codec.
 *
 * See asdu_header.hpp for the layout. The functions work for every valid
 * AsduProfile, not only for the IEC 104 profile.
 *
 * Layer: codec (pure functions, no I/O, no allocation).
 */
#include "codec/asdu_header.hpp"

#include "codec/byte_io.hpp"

namespace iec104::codec {
namespace {

/**
 * @brief Largest unsigned value that fits into @p octets octets.
 *
 * Example: 1 -> 0xFF, 2 -> 0xFFFF, 3 -> 0xFFFFFF. The 4-octet case is handled
 * separately because shifting a 32-bit value by 32 bits is undefined.
 */
std::uint32_t max_value(std::size_t octets) noexcept {
    return octets >= 4U ? 0xFFFFFFFFU : ((1U << (8U * octets)) - 1U);
}

} // namespace

bool is_valid(const AsduProfile& profile) noexcept {
    return profile.cot_length >= 1U && profile.cot_length <= 2U && profile.ca_length >= 1U &&
           profile.ca_length <= 2U && profile.ioa_length >= 1U && profile.ioa_length <= 3U &&
           profile.max_asdu_length >= 1U && profile.max_asdu_length <= kMaxAsduLength;
}

std::size_t header_length(const AsduProfile& profile) noexcept {
    // type id + VSQ + COT + CA
    return 2U + static_cast<std::size_t>(profile.cot_length) +
           static_cast<std::size_t>(profile.ca_length);
}

iec104_status encode_asdu_header(const AsduHeader& header, const AsduProfile& profile,
                                 std::uint8_t* out, std::size_t out_size,
                                 std::size_t& written) noexcept {
    if (out == nullptr || !is_valid(profile)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    const std::size_t len = header_length(profile);
    if (out_size < len) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    // Values that would not survive the bit packing are rejected instead of
    // being truncated, so that a caller error never turns into wrong data on
    // the wire. An originator address needs the second COT octet.
    if (header.num_objects > kMaxObjects || header.cause > kCotCauseMask ||
        (profile.cot_length == 1U && header.originator != 0U) ||
        header.common_address > max_value(profile.ca_length)) {
        return IEC104_ERR_OUT_OF_RANGE;
    }

    std::size_t pos = 0;
    out[pos++] = header.type_id;
    out[pos++] =
        static_cast<std::uint8_t>((header.sequence ? kVsqSequenceBit : 0U) | header.num_objects);
    out[pos++] = static_cast<std::uint8_t>(header.cause | (header.negative ? kCotNegativeBit : 0U) |
                                           (header.test ? kCotTestBit : 0U));
    if (profile.cot_length == 2U) {
        out[pos++] = header.originator;
    }
    detail::put_uint_le(&out[pos], header.common_address, profile.ca_length);
    pos += profile.ca_length;

    written = pos;
    return IEC104_OK;
}

iec104_status decode_asdu_header(const std::uint8_t* data, std::size_t size,
                                 const AsduProfile& profile, AsduHeader& out,
                                 std::size_t& consumed) noexcept {
    if (data == nullptr || !is_valid(profile)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    const std::size_t len = header_length(profile);
    if (size < len) {
        return IEC104_ERR_NEED_MORE_DATA;
    }

    // Decode into a local copy so that 'out' stays untouched on error.
    AsduHeader h{};
    std::size_t pos = 0;
    h.type_id = data[pos++];
    h.sequence = (data[pos] & kVsqSequenceBit) != 0U;
    h.num_objects = static_cast<std::uint8_t>(data[pos++] & kMaxObjects);
    h.cause = static_cast<std::uint8_t>(data[pos] & kCotCauseMask);
    h.negative = (data[pos] & kCotNegativeBit) != 0U;
    h.test = (data[pos] & kCotTestBit) != 0U;
    ++pos;
    if (profile.cot_length == 2U) {
        h.originator = data[pos++];
    }
    h.common_address =
        static_cast<std::uint16_t>(detail::get_uint_le(&data[pos], profile.ca_length));
    pos += profile.ca_length;

    out = h;
    consumed = pos;
    return IEC104_OK;
}

std::uint32_t max_ioa(const AsduProfile& profile) noexcept {
    return max_value(profile.ioa_length);
}

iec104_status encode_ioa(std::uint32_t ioa, const AsduProfile& profile, std::uint8_t* out,
                         std::size_t out_size, std::size_t& written) noexcept {
    if (out == nullptr || !is_valid(profile)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < profile.ioa_length) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    if (ioa > max_value(profile.ioa_length)) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    detail::put_uint_le(out, ioa, profile.ioa_length);
    written = profile.ioa_length;
    return IEC104_OK;
}

iec104_status decode_ioa(const std::uint8_t* data, std::size_t size, const AsduProfile& profile,
                         std::uint32_t& ioa, std::size_t& consumed) noexcept {
    if (data == nullptr || !is_valid(profile)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (size < profile.ioa_length) {
        return IEC104_ERR_NEED_MORE_DATA;
    }
    ioa = detail::get_uint_le(data, profile.ioa_length);
    consumed = profile.ioa_length;
    return IEC104_OK;
}

} // namespace iec104::codec
