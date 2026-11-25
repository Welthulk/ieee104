/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file asdu.cpp
 * @brief Implementation of the ASDU codec.
 *
 * Decoding and encoding walk the element list of the type table entry; the
 * two switch statements below are the only place that maps an element kind
 * to a member of InformationObject.
 *
 * Layer: codec (pure functions, no I/O, no allocation).
 */
#include "codec/asdu.hpp"

#include "codec/byte_io.hpp"

#include <algorithm>

namespace iec104::codec {
namespace {

/**
 * @brief Finds the table entry for @p type_id and applies the strict rules.
 *
 * Shared by decoding and encoding so that both accept the same set of types.
 *
 * @return IEC104_OK and @p type set, or IEC104_ERR_UNSUPPORTED if the type is
 *         unknown or a CP24Time2a type in strict mode. Private types are not
 *         handled here (the caller checks them first).
 */
iec104_status resolve_type(std::uint8_t type_id, const AsduProfile& profile,
                           const TypeInfo*& type) noexcept {
    const TypeInfo* t = find_type(type_id);
    if (t == nullptr) {
        return IEC104_ERR_UNSUPPORTED;
    }
    // The IEC 101 types with CP24Time2a are not part of the IEC 104 type set
    // according to docs/03 (no verbatim proof from the norm text, N4).
    if (profile.strict && t->time_tag == TimeTag::Cp24) {
        return IEC104_ERR_UNSUPPORTED;
    }
    type = t;
    return IEC104_OK;
}

/** @brief Reads one element at @p in into the matching member of @p obj. */
void decode_element(ElementKind kind, const std::uint8_t* in, InformationObject& obj) noexcept {
    switch (kind) {
    case ElementKind::Siq:
        obj.siq = decode_siq(in[0]);
        break;
    case ElementKind::Diq:
        obj.diq = decode_diq(in[0]);
        break;
    case ElementKind::Vti:
        obj.vti = decode_vti(in[0]);
        break;
    case ElementKind::Qds:
        obj.qds = in[0];
        break;
    case ElementKind::Bsi:
        obj.bsi = detail::get_uint_le(in, 4U);
        break;
    case ElementKind::Nva:
        obj.nva = static_cast<std::int16_t>(detail::get_u16_le(in));
        break;
    case ElementKind::Sva:
        obj.sva = static_cast<std::int16_t>(detail::get_u16_le(in));
        break;
    case ElementKind::R32:
        obj.r32 = decode_r32(in);
        break;
    case ElementKind::Bcr:
        obj.bcr = decode_bcr(in);
        break;
    case ElementKind::Scd:
        obj.scd = decode_scd(in);
        break;
    case ElementKind::Sep:
        obj.sep = decode_sep(in[0]);
        break;
    case ElementKind::Spe:
        obj.spe = in[0];
        break;
    case ElementKind::Oci:
        obj.oci = in[0];
        break;
    case ElementKind::Qdp:
        obj.qdp = in[0];
        break;
    case ElementKind::Sco:
        obj.sco = decode_sco(in[0]);
        break;
    case ElementKind::Dco:
        obj.dco = decode_dco(in[0]);
        break;
    case ElementKind::Rco:
        obj.rco = decode_rco(in[0]);
        break;
    case ElementKind::Qos:
        obj.qos = decode_qos(in[0]);
        break;
    case ElementKind::Coi:
        obj.coi = decode_coi(in[0]);
        break;
    case ElementKind::Qoi:
        obj.qoi = in[0];
        break;
    case ElementKind::Qcc:
        obj.qcc = decode_qcc(in[0]);
        break;
    case ElementKind::Qrp:
        obj.qrp = in[0];
        break;
    case ElementKind::Qpm:
        obj.qpm = decode_qpm(in[0]);
        break;
    case ElementKind::Qpa:
        obj.qpa = in[0];
        break;
    case ElementKind::Fbp:
        obj.fbp = detail::get_u16_le(in);
        break;
    case ElementKind::Tsc:
        obj.tsc = detail::get_u16_le(in);
        break;
    case ElementKind::Cp16:
        obj.cp16 = detail::get_u16_le(in);
        break;
    case ElementKind::Cp24:
        obj.cp24 = decode_cp24time2a(in);
        break;
    case ElementKind::Cp56:
        // Cannot fail: the pointer is valid and the length was checked for
        // the whole ASDU before the first element was read.
        (void)decode_cp56time2a(in, kCp56Time2aLength, obj.cp56);
        break;
    }
}

/**
 * @brief Writes a one-octet element after its range check.
 *
 * @return IEC104_OK, or IEC104_ERR_OUT_OF_RANGE (nothing written) if @p valid is false.
 */
iec104_status put_octet(bool valid, std::uint8_t octet, std::uint8_t* out) noexcept {
    if (!valid) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    out[0] = octet;
    return IEC104_OK;
}

/**
 * @brief Writes one element from the matching member of @p obj to @p out.
 *
 * @return IEC104_OK, or IEC104_ERR_OUT_OF_RANGE if the member holds a value
 *         that does not fit the element (nothing is truncated).
 */
iec104_status encode_element(ElementKind kind, const InformationObject& obj,
                             std::uint8_t* out) noexcept {
    switch (kind) {
    case ElementKind::Siq:
        return put_octet(is_valid(obj.siq), encode_siq(obj.siq), out);
    case ElementKind::Diq:
        return put_octet(is_valid(obj.diq), encode_diq(obj.diq), out);
    case ElementKind::Vti:
        return put_octet(is_valid(obj.vti), encode_vti(obj.vti), out);
    case ElementKind::Qds:
        return put_octet(true, obj.qds, out);
    case ElementKind::Bsi:
        detail::put_uint_le(out, obj.bsi, 4U);
        return IEC104_OK;
    case ElementKind::Nva:
        detail::put_u16_le(out, static_cast<std::uint16_t>(obj.nva));
        return IEC104_OK;
    case ElementKind::Sva:
        detail::put_u16_le(out, static_cast<std::uint16_t>(obj.sva));
        return IEC104_OK;
    case ElementKind::R32:
        encode_r32(obj.r32, out);
        return IEC104_OK;
    case ElementKind::Bcr:
        if (!is_valid(obj.bcr)) {
            return IEC104_ERR_OUT_OF_RANGE;
        }
        encode_bcr(obj.bcr, out);
        return IEC104_OK;
    case ElementKind::Scd:
        encode_scd(obj.scd, out);
        return IEC104_OK;
    case ElementKind::Sep:
        return put_octet(is_valid(obj.sep), encode_sep(obj.sep), out);
    case ElementKind::Spe:
        return put_octet(true, obj.spe, out);
    case ElementKind::Oci:
        return put_octet(true, obj.oci, out);
    case ElementKind::Qdp:
        return put_octet(true, obj.qdp, out);
    case ElementKind::Sco:
        return put_octet(is_valid(obj.sco), encode_sco(obj.sco), out);
    case ElementKind::Dco:
        return put_octet(is_valid(obj.dco), encode_dco(obj.dco), out);
    case ElementKind::Rco:
        return put_octet(is_valid(obj.rco), encode_rco(obj.rco), out);
    case ElementKind::Qos:
        return put_octet(is_valid(obj.qos), encode_qos(obj.qos), out);
    case ElementKind::Coi:
        return put_octet(is_valid(obj.coi), encode_coi(obj.coi), out);
    case ElementKind::Qoi:
        return put_octet(true, obj.qoi, out);
    case ElementKind::Qcc:
        return put_octet(is_valid(obj.qcc), encode_qcc(obj.qcc), out);
    case ElementKind::Qrp:
        return put_octet(true, obj.qrp, out);
    case ElementKind::Qpm:
        return put_octet(is_valid(obj.qpm), encode_qpm(obj.qpm), out);
    case ElementKind::Qpa:
        return put_octet(true, obj.qpa, out);
    case ElementKind::Fbp:
        detail::put_u16_le(out, obj.fbp);
        return IEC104_OK;
    case ElementKind::Tsc:
        detail::put_u16_le(out, obj.tsc);
        return IEC104_OK;
    case ElementKind::Cp16:
        // TODO(norm): range of CP16Time2a, clause unknown; every value is written.
        detail::put_u16_le(out, obj.cp16);
        return IEC104_OK;
    case ElementKind::Cp24:
        if (!is_valid(obj.cp24)) {
            return IEC104_ERR_OUT_OF_RANGE;
        }
        encode_cp24time2a(obj.cp24, out);
        return IEC104_OK;
    case ElementKind::Cp56:
        return encode_cp56time2a(obj.cp56, out, kCp56Time2aLength);
    }
    return IEC104_ERR_INTERNAL; // unreachable for valid enum values
}

/**
 * @brief Checks the addresses of an SQ = 1 object list.
 *
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT if objects[i].ioa is not
 *         objects[0].ioa + i; IEC104_ERR_OUT_OF_RANGE if the last address does
 *         not fit into ioa_length octets.
 */
iec104_status check_sequence_addresses(const InformationObject* objects, std::size_t count,
                                       const AsduProfile& profile) noexcept {
    const std::uint64_t first = objects[0].ioa;
    for (std::size_t i = 1; i < count; ++i) {
        if (std::uint64_t{objects[i].ioa} != first + i) {
            return IEC104_ERR_INVALID_ARGUMENT;
        }
    }
    if (first + (count - 1U) > max_ioa(profile)) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    return IEC104_OK;
}

} // namespace

std::size_t asdu_length(const TypeInfo& type, bool sequence, std::size_t count,
                        const AsduProfile& profile) noexcept {
    const std::size_t ioa = profile.ioa_length;
    const std::size_t obj = object_length(type);
    // SQ = 1 carries one IOA for the whole sequence, SQ = 0 one per object.
    const std::size_t objects = sequence ? ioa + (count * obj) : count * (ioa + obj);
    return header_length(profile) + objects;
}

iec104_status decode_asdu(const std::uint8_t* data, std::size_t size, const AsduProfile& profile,
                          InformationObject* objects, std::size_t capacity,
                          DecodedAsdu& out) noexcept {
    if (data == nullptr || (objects == nullptr && capacity > 0U) || !is_valid(profile)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }

    DecodedAsdu result{};
    std::size_t pos = 0;
    // The caller hands over a complete ASDU, so a missing header is a broken
    // frame, not a reason to wait for more data.
    if (decode_asdu_header(data, size, profile, result.header, pos) != IEC104_OK) {
        return IEC104_ERR_MALFORMED;
    }
    result.object_data = data + pos;
    result.object_data_size = size - pos;

    if (is_private_type(result.header.type_id)) {
        out = result; // raw octets only, see DecodedAsdu::object_data
        return IEC104_OK;
    }
    const iec104_status st = resolve_type(result.header.type_id, profile, result.type);
    if (st != IEC104_OK) {
        return st;
    }

    const TypeInfo& type = *result.type;
    const std::size_t count = result.header.num_objects;
    if (count == 0U) {
        // TODO(norm): ASDU without objects, clause unknown (docs/04, N9).
        if (profile.strict || result.object_data_size != 0U) {
            return IEC104_ERR_MALFORMED;
        }
        out = result;
        return IEC104_OK;
    }
    if (size != asdu_length(type, result.header.sequence, count, profile)) {
        return IEC104_ERR_MALFORMED;
    }
    if (capacity < count) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }

    std::uint32_t first_ioa = 0;
    for (std::size_t i = 0; i < count; ++i) {
        InformationObject obj{};
        std::size_t n = 0;
        if (!result.header.sequence || i == 0U) {
            // Bounds are guaranteed by the length check above.
            (void)decode_ioa(data + pos, size - pos, profile, obj.ioa, n);
            pos += n;
            first_ioa = obj.ioa;
        } else {
            // SQ = 1: the address of object i is the first address plus i.
            // An address beyond ioa_length octets cannot exist on the wire.
            const std::uint64_t ioa = std::uint64_t{first_ioa} + i;
            if (ioa > max_ioa(profile)) {
                return IEC104_ERR_MALFORMED;
            }
            obj.ioa = static_cast<std::uint32_t>(ioa);
        }
        for (std::size_t e = 0; e < type.element_count; ++e) {
            decode_element(type.elements[e], data + pos, obj);
            pos += element_length(type.elements[e]);
        }
        objects[i] = obj;
    }

    result.object_count = count;
    out = result;
    return IEC104_OK;
}

iec104_status encode_asdu(const AsduHeader& header, const InformationObject* objects,
                          std::size_t count, const AsduProfile& profile, std::uint8_t* out,
                          std::size_t out_size, std::size_t& written) noexcept {
    if (objects == nullptr || out == nullptr || count == 0U || !is_valid(profile)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (is_private_type(header.type_id)) {
        return IEC104_ERR_UNSUPPORTED;
    }
    const TypeInfo* type = nullptr;
    iec104_status st = resolve_type(header.type_id, profile, type);
    if (st != IEC104_OK) {
        return st;
    }
    if (count > kMaxObjects) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    if (header.sequence) {
        st = check_sequence_addresses(objects, count, profile);
        if (st != IEC104_OK) {
            return st;
        }
    }
    const std::size_t length = asdu_length(*type, header.sequence, count, profile);
    if (length > profile.max_asdu_length) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    if (out_size < length) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }

    AsduHeader h = header;
    h.num_objects = static_cast<std::uint8_t>(count);
    std::size_t pos = 0;
    st = encode_asdu_header(h, profile, out, out_size, pos);
    if (st != IEC104_OK) {
        return st;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const InformationObject& obj = objects[i];
        if (!header.sequence || i == 0U) {
            std::size_t n = 0;
            st = encode_ioa(obj.ioa, profile, out + pos, out_size - pos, n);
            if (st != IEC104_OK) {
                return st;
            }
            pos += n;
        }
        for (std::size_t e = 0; e < type->element_count; ++e) {
            st = encode_element(type->elements[e], obj, out + pos);
            if (st != IEC104_OK) {
                return st;
            }
            pos += element_length(type->elements[e]);
        }
    }
    written = pos;
    return IEC104_OK;
}

iec104_status encode_asdu_fitting(const AsduHeader& header, const InformationObject* objects,
                                  std::size_t count, const AsduProfile& profile, std::uint8_t* out,
                                  std::size_t out_size, std::size_t& written,
                                  std::size_t& objects_encoded) noexcept {
    if (objects == nullptr || out == nullptr || count == 0U || !is_valid(profile)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (is_private_type(header.type_id)) {
        return IEC104_ERR_UNSUPPORTED;
    }
    const TypeInfo* type = nullptr;
    const iec104_status st = resolve_type(header.type_id, profile, type);
    if (st != IEC104_OK) {
        return st;
    }

    // Octets every ASDU needs, and octets each further object adds.
    const std::size_t fixed = header_length(profile) + (header.sequence ? profile.ioa_length : 0U);
    const std::size_t per_object =
        object_length(*type) + (header.sequence ? 0U : profile.ioa_length);
    if (fixed + per_object > profile.max_asdu_length) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    std::size_t fit = std::min<std::size_t>(count, kMaxObjects);
    if (per_object > 0U) { // C_RD_NA_1 with SQ = 1 has 0 octets per further object
        fit = std::min(fit, (profile.max_asdu_length - fixed) / per_object);
    }

    std::size_t length = 0;
    const iec104_status enc = encode_asdu(header, objects, fit, profile, out, out_size, length);
    if (enc != IEC104_OK) {
        return enc;
    }
    written = length;
    objects_encoded = fit;
    return IEC104_OK;
}

} // namespace iec104::codec
