/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file fuzz_codec.cpp
 * @brief libFuzzer target for the codec layer (APCI, ASDU header, CP56Time2a).
 *
 * The fuzzer feeds arbitrary bytes into the decoders. Besides crashes and
 * sanitizer findings (ASan/UBSan are enabled in the 'fuzz' preset) it checks
 * these invariants and aborts if one is violated:
 *   - APCI: whatever decodes successfully re-encodes to the identical 6 octets;
 *   - ASDU header (IEC 104 profile): whatever decodes successfully behind an
 *     I-frame re-encodes to the identical octets;
 *   - CP56Time2a: a decoded, valid time tag survives encode -> decode
 *     unchanged. Bytes are not compared, because RES2..RES4 are dropped on
 *     purpose (see cp56time2a.hpp).
 *
 * Build:  cmake --preset fuzz && cmake --build --preset fuzz
 * Run:    ./build/fuzz/fuzz_codec -max_total_time=300 fuzz/corpus
 * Seeds:  the APDUs of tests/data/lib60870_capture.txt are good starting inputs.
 */
#include "codec/apci.hpp"
#include "codec/asdu_header.hpp"
#include "codec/cp56time2a.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

using namespace iec104::codec;

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

        // ASDU header behind an I-frame. apci.asdu_length is bounded by the
        // length octet, which decode_apci() checked against 'size'.
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
        }
    }

    // CP56Time2a on the raw input (independent of the APCI result).
    Cp56Time2a t{};
    if (decode_cp56time2a(data, size, t) == IEC104_OK && validate(t) == IEC104_OK) {
        std::array<std::uint8_t, kCp56Time2aLength> tb{};
        Cp56Time2a back{};
        if (encode_cp56time2a(t, tb.data(), tb.size()) != IEC104_OK ||
            decode_cp56time2a(tb.data(), tb.size(), back) != IEC104_OK || !(back == t)) {
            std::abort();
        }
    }
    return 0;
}
