/**
 * @file asdu_header.hpp
 * @brief ASDU data unit identifier and information object address (IOA).
 *
 * An ASDU (application service data unit) starts with the data unit
 * identifier:
 *
 *   type id (1) | VSQ (1) | COT (cot_length) | common address (ca_length)
 *
 * followed by one or more information objects, each introduced by an IOA of
 * ioa_length octets. In the IEC 60870-5-104 profile the lengths are fixed at
 * COT 2, CA 2, IOA 3 octets (6-octet header). IEC 60870-5-101 allows other
 * lengths, and some peers use them over TCP as well; the lengths are therefore
 * parameters (AsduProfile), with the IEC 104 values as default
 * (docs/02, "ASDU-Profil").
 *
 * This file only converts the header fields. Which cause of transmission is
 * allowed for which type id (standard, clause 9.5) and the information objects
 * themselves are part of TASK-001.
 *
 * Sources: docs/03, ASDU section (field layout and bit masks, citing the
 * Wireshark IEC 104 dissector); reference bytes in
 * tests/data/lib60870_capture.txt.
 * TODO(norm): data unit identifier layout, clause unknown.
 *
 * Layer: codec (pure functions, no I/O, no allocation).
 */
#pragma once

#include "iec104/iec104.h"

#include <cstddef>
#include <cstdint>

namespace iec104::codec {

/**
 * @brief Field lengths of the data unit identifier and the IOA.
 *
 * Default-constructed, the profile has the IEC 104 values (2, 2, 3). Other
 * values only exist for peers outside the standard and must be configured
 * explicitly.
 */
struct AsduProfile {
    std::uint8_t cot_length{2}; ///< 1..2 octets. 2 = cause octet + originator address octet.
    std::uint8_t ca_length{2};  ///< 1..2 octets.
    std::uint8_t ioa_length{3}; ///< 1..3 octets.
};

/** @brief The IEC 60870-5-104 profile: COT 2, CA 2, IOA 3 octets. */
inline constexpr AsduProfile kIec104Profile{};

/**
 * @brief Bits of the VSQ octet (variable structure qualifier).
 *
 *   bit:   7  | 6 5 4 3 2 1 0
 *          SQ | number of objects (0..127)
 *
 * SQ = 0: every information object has its own IOA.
 * SQ = 1: one IOA for the first object, the following objects have
 *         consecutive addresses and carry no IOA.
 *
 * Example: 01 88 = type 1, SQ = 1, 8 objects (tests/data/lib60870_capture.txt).
 */
inline constexpr std::uint8_t kVsqSequenceBit = 0x80U;
/** @brief Mask and maximum of the number of objects in the VSQ octet (127). */
inline constexpr std::uint8_t kMaxObjects = 0x7FU;

/**
 * @brief Bits of the first COT octet (cause of transmission).
 *
 *   bit:   7 | 6   | 5 4 3 2 1 0
 *          T | P/N | cause (0..63)
 *
 * T = test, P/N = negative confirmation. With cot_length 2 the second octet
 * is the originator address (0 = not used).
 *
 * Example: 06 03 = cause 6 (activation), positive, no test, originator 3
 * (tests/data/lib60870_capture.txt).
 */
inline constexpr std::uint8_t kCotCauseMask = 0x3FU;
/** @brief P/N bit of the first COT octet: 1 = negative confirmation. */
inline constexpr std::uint8_t kCotNegativeBit = 0x40U;
/** @brief T bit of the first COT octet: 1 = test, the receiver must not act on the data. */
inline constexpr std::uint8_t kCotTestBit = 0x80U;

/**
 * @brief Broadcast common address with a 2-octet CA (all stations).
 *
 * Not used by the codec yet. Which types may be sent to the broadcast address
 * is open (docs/04_Analyse.md, N10).
 * TODO(norm): broadcast common address, clause unknown.
 */
inline constexpr std::uint16_t kBroadcastCa = 0xFFFFU;

/** @brief Decoded data unit identifier. */
struct AsduHeader {
    std::uint8_t type_id{0};         ///< Type identification, 1..255 (0 is not defined).
    bool sequence{false};            ///< SQ bit of the VSQ octet.
    std::uint8_t num_objects{0};     ///< Number of information objects, 0..127.
    std::uint8_t cause{0};           ///< Cause of transmission, 0..63.
    bool negative{false};            ///< P/N bit: negative confirmation.
    bool test{false};                ///< T bit: test frame.
    std::uint8_t originator{0};      ///< Originator address; only with cot_length 2, else 0.
    std::uint16_t common_address{0}; ///< Common address (station), width per ca_length.
};

/**
 * @brief Checks that all lengths of @p profile are inside their allowed ranges.
 *
 * @param profile Profile to check.
 * @return true if cot_length is 1..2, ca_length 1..2 and ioa_length 1..3.
 */
bool is_valid(const AsduProfile& profile) noexcept;

/**
 * @brief Length of the data unit identifier for @p profile.
 *
 * @param profile A profile; the result is only meaningful if is_valid() holds.
 * @return 2 + cot_length + ca_length, i.e. 6 in the IEC 104 profile.
 */
std::size_t header_length(const AsduProfile& profile) noexcept;

/**
 * @brief Encodes the data unit identifier.
 *
 * Example (IEC 104 profile): type 100, 1 object, cause 6, originator 3, CA 1
 * -> 64 01 06 03 01 00 (tests/data/lib60870_capture.txt).
 *
 * @param header   Fields to encode.
 * @param profile  Field lengths.
 * @param out      Destination buffer.
 * @param out_size Size of @p out, at least header_length(profile).
 * @param written  Receives the number of octets written on success.
 * @return IEC104_OK on success.
 * @return IEC104_ERR_INVALID_ARGUMENT if @p out is NULL or @p profile is invalid.
 * @return IEC104_ERR_BUFFER_TOO_SMALL if @p out_size is too small.
 * @return IEC104_ERR_OUT_OF_RANGE if num_objects exceeds 127, cause exceeds 63,
 *         an originator other than 0 is given with cot_length 1, or the common
 *         address does not fit into ca_length octets.
 */
iec104_status encode_asdu_header(const AsduHeader& header, const AsduProfile& profile,
                                 std::uint8_t* out, std::size_t out_size,
                                 std::size_t& written) noexcept;

/**
 * @brief Decodes the data unit identifier.
 *
 * Only the header is decoded; the information objects are not interpreted.
 * Type id and cause are not checked against tables here (TASK-001), and a VSQ
 * with 0 objects is accepted (docs/04_Analyse.md, N9).
 *
 * @param data     Start of the ASDU. Must not be NULL.
 * @param size     Number of octets available at @p data.
 * @param profile  Field lengths.
 * @param out      Receives the decoded header on success. Unchanged on error.
 * @param consumed Receives header_length(profile) on success.
 * @return IEC104_OK on success.
 * @return IEC104_ERR_INVALID_ARGUMENT if @p data is NULL or @p profile is invalid.
 * @return IEC104_ERR_NEED_MORE_DATA if @p size is smaller than the header.
 */
iec104_status decode_asdu_header(const std::uint8_t* data, std::size_t size,
                                 const AsduProfile& profile, AsduHeader& out,
                                 std::size_t& consumed) noexcept;

/**
 * @brief Encodes an information object address (IOA).
 *
 * IOA 0 is a valid value; it addresses the station as a whole, e.g. for the
 * general interrogation (docs/03).
 *
 * Example (3 octets): 4711 -> 67 12 00
 *
 * @param ioa      Address, 0 .. 2^(8 * ioa_length) - 1.
 * @param profile  Field lengths (only ioa_length is used).
 * @param out      Destination buffer.
 * @param out_size Size of @p out, at least ioa_length.
 * @param written  Receives ioa_length on success.
 * @return IEC104_OK on success.
 * @return IEC104_ERR_INVALID_ARGUMENT if @p out is NULL or @p profile is invalid.
 * @return IEC104_ERR_BUFFER_TOO_SMALL if @p out_size is too small.
 * @return IEC104_ERR_OUT_OF_RANGE if @p ioa does not fit into ioa_length octets.
 */
iec104_status encode_ioa(std::uint32_t ioa, const AsduProfile& profile, std::uint8_t* out,
                         std::size_t out_size, std::size_t& written) noexcept;

/**
 * @brief Decodes an information object address (IOA).
 *
 * @param data     Start of the IOA. Must not be NULL.
 * @param size     Number of octets available at @p data.
 * @param profile  Field lengths (only ioa_length is used).
 * @param ioa      Receives the address on success.
 * @param consumed Receives ioa_length on success.
 * @return IEC104_OK on success.
 * @return IEC104_ERR_INVALID_ARGUMENT if @p data is NULL or @p profile is invalid.
 * @return IEC104_ERR_NEED_MORE_DATA if @p size is smaller than ioa_length.
 */
iec104_status decode_ioa(const std::uint8_t* data, std::size_t size, const AsduProfile& profile,
                         std::uint32_t& ioa, std::size_t& consumed) noexcept;

} // namespace iec104::codec
