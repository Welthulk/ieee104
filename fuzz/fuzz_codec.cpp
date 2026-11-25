/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file fuzz_codec.cpp
 * @brief libFuzzer target for the codec layer (APCI, ASDU header, ASDUs, CP56Time2a).
 *
 * The fuzzer feeds arbitrary bytes into the decoders. Besides crashes and
 * sanitizer findings (ASan/UBSan are enabled in the 'fuzz' preset) it checks
 * these invariants and aborts if one is violated:
 *   - APCI: whatever decodes successfully re-encodes to the identical 6 octets;
 *   - ASDU header (IEC 104 profile): whatever decodes successfully behind an
 *     I-frame re-encodes to the identical octets;
 *   - complete ASDU (decoding is lossless, see elements.hpp): whatever decodes
 *     successfully re-encodes to the identical octets, unless an element value
 *     lies outside its range (e.g. minute 63 in a time tag), in which case the
 *     encoder must report IEC104_ERR_OUT_OF_RANGE and nothing else. Checked
 *     behind an I-frame (strict IEC 104 profile) and on the raw input, where
 *     the first octet selects the profile (see select_profile());
 *   - CP56Time2a: a decoded, valid time tag re-encodes to the identical octets.
 *
 * Build:  cmake --preset fuzz && cmake --build --preset fuzz
 * Seeds:  tools/make_fuzz_seeds.sh (APDUs and ASDUs from tests/data/)
 * Run:    ./build/fuzz/fuzz_codec -max_total_time=300 fuzz/corpus
 */
#include "codec/apci.hpp"
#include "codec/asdu.hpp"
#include "codec/asdu_header.hpp"
#include "codec/cp56time2a.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

using namespace iec104::codec;

namespace {

// Profile for the raw-input ASDU check, chosen by the first input octet.
// The seeds produced by tools/make_fuzz_seeds.sh use the same numbering.
AsduProfile select_profile(std::uint8_t selector) {
    AsduProfile p{};
    switch (selector % 3U) {
    case 0: // strict IEC 104 profile (default)
        break;
    case 1: // tolerant IEC 104 profile (CP24 types, empty ASDUs)
        p.strict = false;
        break;
    default: // tolerant, COT 1 / CA 1 / IOA 2 octets
        p = AsduProfile{1, 1, 2};
        p.strict = false;
        break;
    }
    return p;
}

// Decodes one ASDU and checks the round-trip invariant (see file comment).
void check_asdu(const std::uint8_t* asdu, std::size_t size, const AsduProfile& profile) {
    std::array<InformationObject, kMaxObjects> objects{};
    DecodedAsdu d{};
    if (decode_asdu(asdu, size, profile, objects.data(), objects.size(), d) != IEC104_OK ||
        d.type == nullptr || d.object_count == 0U) {
        return; // rejected input, private type or empty ASDU: nothing to re-encode
    }
    std::array<std::uint8_t, kMaxAsduLength> out{};
    std::size_t written = 0;
    const iec104_status st = encode_asdu(d.header, objects.data(), d.object_count, profile,
                                         out.data(), out.size(), written);
    if (st == IEC104_ERR_OUT_OF_RANGE) {
        return; // a received value outside its range; decoding keeps it, encoding refuses it
    }
    if (st != IEC104_OK || written != size || std::memcmp(out.data(), asdu, size) != 0) {
        std::abort();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // APCI: decode -> encode must give back the input octets.
    Apci apci{};
    if (decode_apci(data, size, apci) == IEC104_OK) {
        std::array<std::uint8_t, kApciLength> re{};
        iec104_status st = IEC104_ERR_INTERNAL;
        switch (apci.format) {
        case FrameFormat::I:
            st = encode_i_header(apci.send_seq, apci.recv_seq, apci.asdu_length, re.data(),
                                 re.size());
            break;
        case FrameFormat::S:
            st = encode_s_frame(apci.recv_seq, re.data(), re.size());
            break;
        case FrameFormat::U:
            st = encode_u_frame(apci.u_function, re.data(), re.size());
            break;
        }
        if (st != IEC104_OK || std::memcmp(re.data(), data, kApciLength) != 0) {
            std::abort();
        }

        // ASDU header and complete ASDU behind an I-frame. apci.asdu_length is
        // bounded by the length octet, which decode_apci() checked against 'size'.
        if (apci.format == FrameFormat::I) {
            const std::uint8_t* asdu = data + kApciLength;
            AsduHeader h{};
            std::size_t consumed = 0;
            if (decode_asdu_header(asdu, apci.asdu_length, kIec104Profile, h, consumed) ==
                IEC104_OK) {
                std::array<std::uint8_t, 6> hb{};
                std::size_t written = 0;
                if (encode_asdu_header(h, kIec104Profile, hb.data(), hb.size(), written) !=
                        IEC104_OK ||
                    written != consumed || std::memcmp(hb.data(), asdu, written) != 0) {
                    std::abort();
                }
            }
            check_asdu(asdu, apci.asdu_length, kIec104Profile);
        }
    }

    // Complete ASDU on the raw input: first octet selects the profile, the
    // rest (at most the largest ASDU) is the ASDU.
    if (size >= 2U) {
        check_asdu(data + 1, std::min(size - 1U, kMaxAsduLength), select_profile(data[0]));
    }

    // CP56Time2a on the raw input (independent of the APCI result).
    Cp56Time2a t{};
    if (decode_cp56time2a(data, size, t) == IEC104_OK && validate(t) == IEC104_OK) {
        std::array<std::uint8_t, kCp56Time2aLength> tb{};
        if (encode_cp56time2a(t, tb.data(), tb.size()) != IEC104_OK ||
            std::memcmp(tb.data(), data, kCp56Time2aLength) != 0) {
            std::abort();
        }
    }
    return 0;
}
