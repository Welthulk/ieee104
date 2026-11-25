/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file asdu.hpp
 * @brief Complete ASDUs: data unit identifier plus information objects.
 *
 * An ASDU is the payload of an I-frame:
 *
 *   data unit identifier (asdu_header.hpp) | information object 1 | ... | object n
 *
 * The VSQ octet of the identifier says how the objects are laid out:
 *
 *   SQ = 0:  IOA | elements | IOA | elements | ...     (every object has its own IOA)
 *   SQ = 1:  IOA | elements | elements | ...           (one IOA, addresses count up by 1)
 *
 * Example (reference data, M_ME_NB_1, SQ = 0, 2 objects):
 *
 *   0b 02 03 03 01 00  64 00 00 67 12 00  65 00 00 00 80 01
 *   type 11, 2 objects, cause 3 (spontaneous), originator 3, CA 1;
 *   IOA 100: SVA 4711, QDS 0; IOA 101: SVA -32768, QDS OV
 *
 * The elements of an object come from the type table (type_table.hpp); this
 * file has no per-type code. Decoding writes into an array the caller
 * provides, encoding reads from one; nothing is allocated.
 *
 * Strict and tolerant profile (AsduProfile::strict, docs/02_Technik_und_Quellen.md, "ASDU-Kopf und
 * ASDU-Profil"):
 *   - IEC 101 types with CP24Time2a: strict -> IEC104_ERR_UNSUPPORTED,
 *     tolerant -> decoded and encoded like any other type;
 *   - ASDU with 0 objects: strict -> IEC104_ERR_MALFORMED, tolerant ->
 *     accepted if no object octets follow (docs/05_Normfragen_und_Festlegungen.md, N9).
 * Types 136..255 are private: decode_asdu() hands their object octets to the
 * caller unchanged (DecodedAsdu::object_data) and does not interpret them.
 *
 * The codec handles SQ = 1 for every type in the table; whether SQ = 1 is
 * admissible for a type is not checked.
 * TODO(norm): admissible types for SQ = 1, clause unknown.
 *
 * Layer: codec (pure functions, no I/O, no allocation).
 */
#pragma once

#include "codec/asdu_header.hpp"
#include "codec/cp56time2a.hpp"
#include "codec/elements.hpp"
#include "codec/type_table.hpp"
#include "iec104/iec104.h"

#include <cstddef>
#include <cstdint>

namespace iec104::codec {

/**
 * @brief One information object: address and the values of its elements.
 *
 * The struct has one member per element kind. Which members are meaningful
 * depends on the type identification: exactly those listed in the type table
 * entry (TypeInfo::elements). All other members are ignored on encoding and
 * left at their default on decoding.
 *
 * Example: for M_ME_TE_1 (35) the elements are SVA, QDS, CP56Time2a, so
 * ioa, sva, qds and cp56 are used.
 *
 * NVA is kept as its raw signed 16-bit value; the represented fraction is
 * nva / 32768 (docs/02_Technik_und_Quellen.md), e.g. 0x4000 = 0.5.
 */
struct InformationObject {
    std::uint32_t ioa{0}; ///< Information object address, width per AsduProfile::ioa_length.

    // Process information (monitor direction; NVA, SVA, R32, BSI also in commands).
    Siq siq{};            ///< SIQ: single point.
    Diq diq{};            ///< DIQ: double point.
    Vti vti{};            ///< VTI: step position.
    std::uint8_t qds{0};  ///< QDS: quality descriptor, kQuality* bits.
    std::uint32_t bsi{0}; ///< BSI: 32-bit bitstring.
    std::int16_t nva{0};  ///< NVA: normalized value, raw.
    std::int16_t sva{0};  ///< SVA: scaled value.
    float r32{0.0F};      ///< R32: short floating point value.
    Bcr bcr{};            ///< BCR: binary counter reading.
    Scd scd{};            ///< SCD: packed single points with change detection.
    Sep sep{};            ///< SEP: single event of protection equipment.
    std::uint8_t spe{0};  ///< SPE: start events, kSpe* bits.
    std::uint8_t oci{0};  ///< OCI: output circuit information, kOci* bits.
    std::uint8_t qdp{0};  ///< QDP: quality descriptor for protection events.

    // Commands.
    Sco sco{}; ///< SCO: single command.
    Dco dco{}; ///< DCO: double command.
    Rco rco{}; ///< RCO: regulating step command.
    Qos qos{}; ///< QOS: qualifier of set-point command.

    // System information and parameters.
    Coi coi{};            ///< COI: cause of initialization.
    std::uint8_t qoi{0};  ///< QOI: qualifier of interrogation (kQoiStation = 20).
    Qcc qcc{};            ///< QCC: qualifier of counter interrogation.
    std::uint8_t qrp{0};  ///< QRP: qualifier of reset process command.
    std::uint16_t fbp{0}; ///< FBP: fixed test bit pattern (kFixedTestBitPattern).
    std::uint16_t tsc{0}; ///< TSC: test sequence counter.
    Qpm qpm{};            ///< QPM: qualifier of parameter of measured values.
    std::uint8_t qpa{0};  ///< QPA: qualifier of parameter activation.

    // Times.
    std::uint16_t cp16{0}; ///< CP16Time2a: elapsed or delay time in milliseconds.
    Cp24Time2a cp24{};     ///< CP24Time2a time tag (IEC 101 types, tolerant profile only).
    Cp56Time2a cp56{};     ///< CP56Time2a: time tag, or the clock value of C_CS_NA_1.
};

/** @brief Result of decode_asdu(), besides the objects in the caller's array. */
struct DecodedAsdu {
    AsduHeader header{};           ///< Decoded data unit identifier.
    const TypeInfo* type{nullptr}; ///< Table entry of the type; NULL for private types.
    std::size_t object_count{0};   ///< Number of objects written to the caller's array.
    /**
     * @brief The octets after the data unit identifier, as received.
     *
     * Points into the input buffer and is only valid as long as that buffer
     * is. For private types this is the only way to get at the content.
     */
    const std::uint8_t* object_data{nullptr};
    std::size_t object_data_size{0}; ///< Number of octets at object_data.
};

/**
 * @brief Number of octets an ASDU with @p count objects of @p type occupies.
 *
 * Example: M_SP_NA_1, SQ = 1, 3 objects, IEC 104 profile -> 6 + 3 + 3 * 1 = 12.
 *
 * @param type     Table entry of the type.
 * @param sequence SQ bit.
 * @param count    Number of objects.
 * @param profile  Field lengths (must be valid).
 * @return Length in octets.
 */
std::size_t asdu_length(const TypeInfo& type, bool sequence, std::size_t count,
                        const AsduProfile& profile) noexcept;

/**
 * @brief Decodes a complete ASDU.
 *
 * @p data must hold exactly one ASDU, e.g. the octets behind the APCI of an
 * I-frame (Apci::asdu_length octets). The length must match the number of
 * objects exactly.
 *
 * @param data     Start of the ASDU. Must not be NULL.
 * @param size     Length of the ASDU in octets.
 * @param profile  Field lengths and strict/tolerant handling.
 * @param objects  Caller's array for the decoded objects; may be NULL if
 *                 @p capacity is 0. On error its content is unspecified.
 * @param capacity Number of entries in @p objects. 127 is always enough.
 * @param out      Receives header, type and object count on success.
 *                 Unchanged on error.
 * @return IEC104_OK on success (also for private types, with object_count 0).
 * @return IEC104_ERR_INVALID_ARGUMENT if @p data is NULL, @p objects is NULL
 *         with a capacity > 0, or @p profile is invalid.
 * @return IEC104_ERR_UNSUPPORTED if the type is not in the table and not
 *         private, or is an IEC 101 CP24Time2a type in strict mode.
 * @return IEC104_ERR_MALFORMED if the ASDU is shorter than its header, the
 *         length does not match the number of objects, the number of
 *         objects is 0 in strict mode, or an address of an SQ = 1 sequence
 *         does not fit into ioa_length octets.
 * @return IEC104_ERR_BUFFER_TOO_SMALL if @p capacity is less than the number
 *         of objects.
 */
iec104_status decode_asdu(const std::uint8_t* data, std::size_t size, const AsduProfile& profile,
                          InformationObject* objects, std::size_t capacity,
                          DecodedAsdu& out) noexcept;

/**
 * @brief Encodes one ASDU with all given objects.
 *
 * With SQ = 1 (header.sequence) the addresses must be consecutive:
 * objects[i].ioa == objects[0].ioa + i; only the first IOA is written.
 *
 * @param header   Type, SQ, cause, P/N, T, originator, common address.
 *                 header.num_objects is ignored; @p count is written instead.
 * @param objects  The objects to encode. Must not be NULL.
 * @param count    Number of objects, 1..127.
 * @param profile  Field lengths, maximum ASDU length, strict/tolerant.
 * @param out      Destination buffer. Must not be NULL.
 * @param out_size Size of @p out.
 * @param written  Receives the ASDU length on success.
 * @return IEC104_OK on success.
 * @return IEC104_ERR_INVALID_ARGUMENT if a pointer is NULL, @p count is 0,
 *         @p profile is invalid, or the addresses of an SQ = 1 ASDU are not
 *         consecutive.
 * @return IEC104_ERR_UNSUPPORTED if the type is private, not in the table,
 *         or an IEC 101 CP24Time2a type in strict mode.
 * @return IEC104_ERR_OUT_OF_RANGE if @p count exceeds 127, the ASDU would be
 *         longer than profile.max_asdu_length (use encode_asdu_fitting()),
 *         a header field or an IOA does not fit, or an element value is out
 *         of range.
 * @return IEC104_ERR_BUFFER_TOO_SMALL if @p out_size is less than the ASDU
 *         length.
 */
iec104_status encode_asdu(const AsduHeader& header, const InformationObject* objects,
                          std::size_t count, const AsduProfile& profile, std::uint8_t* out,
                          std::size_t out_size, std::size_t& written) noexcept;

/**
 * @brief Encodes as many objects as fit into one ASDU; used to split long lists.
 *
 * Takes objects from the front of @p objects until the next one would exceed
 * profile.max_asdu_length or 127 objects. The caller sends the ASDU,
 * advances by @p objects_encoded and calls again until all are sent:
 *
 * @code
 *   while (count > 0) {
 *       st = encode_asdu_fitting(h, objects, count, profile, buf, sizeof buf, len, used);
 *       if (st != IEC104_OK) break;
 *       send(buf, len);
 *       objects += used;
 *       count -= used;
 *   }
 * @endcode
 *
 * With SQ = 1 every ASDU starts with the IOA of its first object, so a
 * consecutive list stays consecutive across the split.
 *
 * @param header          As for encode_asdu().
 * @param objects         Remaining objects. Must not be NULL.
 * @param count           Number of remaining objects, at least 1.
 * @param profile         As for encode_asdu().
 * @param out             Destination buffer. Must not be NULL.
 * @param out_size        Size of @p out; profile.max_asdu_length is always enough.
 * @param written         Receives the ASDU length on success.
 * @param objects_encoded Receives the number of objects in this ASDU on success.
 * @return The codes of encode_asdu(); additionally IEC104_ERR_OUT_OF_RANGE if
 *         not even one object fits into profile.max_asdu_length.
 */
iec104_status encode_asdu_fitting(const AsduHeader& header, const InformationObject* objects,
                                  std::size_t count, const AsduProfile& profile, std::uint8_t* out,
                                  std::size_t out_size, std::size_t& written,
                                  std::size_t& objects_encoded) noexcept;

} // namespace iec104::codec
