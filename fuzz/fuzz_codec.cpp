// libFuzzer target for the codec layer.
// Build:  cmake --preset fuzz && cmake --build --preset fuzz
// Run:    ./build/fuzz/fuzz_codec -max_total_time=600 fuzz/corpus
//
// Invariant checked: whatever decodes successfully must re-encode to the
// identical bytes (APCI, ASDU header with the IEC 104 profile, CP56Time2a
// apart from the ignored reserved bits).
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "codec/apci.hpp"
#include "codec/asdu_header.hpp"
#include "codec/cp56time2a.hpp"

using namespace iec104::codec;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // APCI
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

        // ASDU header behind an I-frame
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

    // CP56Time2a on the raw input
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
