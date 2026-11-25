/**
 * @file test_capture.cpp
 * @brief Replays a capture of a foreign implementation through the codec.
 *
 * tests/data/lib60870_capture.txt holds every APDU exchanged between the
 * lib60870-C v2.4.1 example client and server (external test peer only, not
 * linked). This test checks that the codec accepts all of them and produces
 * byte-identical APCIs and ASDU headers when re-encoding the decoded values.
 * New frames added to the capture are covered automatically.
 */
#include "codec/apci.hpp"
#include "codec/asdu_header.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

using namespace iec104::codec;

namespace {

struct CapturedApdu {
    int line = 0;                    // line number in the capture file, for messages
    std::vector<std::uint8_t> bytes; // the complete APDU
};

// Reads all "SEND:" and "RCVD:" lines. Lines starting with '#' are comments.
std::vector<CapturedApdu> load_capture() {
    std::vector<CapturedApdu> result;
    std::ifstream in(IEC104_TEST_DATA_DIR "/lib60870_capture.txt");
    std::string text;
    int line = 0;
    while (std::getline(in, text)) {
        ++line;
        if (text.rfind("SEND:", 0) != 0 && text.rfind("RCVD:", 0) != 0) {
            continue;
        }
        CapturedApdu apdu;
        apdu.line = line;
        std::istringstream hex(text.substr(5));
        unsigned value = 0;
        while (hex >> std::hex >> value) {
            apdu.bytes.push_back(static_cast<std::uint8_t>(value));
        }
        result.push_back(apdu);
    }
    return result;
}

} // namespace

// Rule: every APDU of a foreign implementation is framed, decoded and
// re-encoded to identical bytes (APCI for all formats, ASDU header for I-frames).
// Source: capture (whole file).
TEST(Capture, EveryApduRoundTrips) {
    const std::vector<CapturedApdu> apdus = load_capture();
    ASSERT_GE(apdus.size(), 10U) << "capture file missing or truncated";

    for (const CapturedApdu& c : apdus) {
        SCOPED_TRACE("capture line " + std::to_string(c.line));
        const std::uint8_t* data = c.bytes.data();
        const std::size_t size = c.bytes.size();

        std::size_t apdu_length = 0;
        ASSERT_EQ(peek_apdu_length(data, size, apdu_length), IEC104_OK);
        EXPECT_EQ(apdu_length, size);

        Apci apci{};
        ASSERT_EQ(decode_apci(data, size, apci), IEC104_OK);

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
        ASSERT_EQ(st, IEC104_OK);
        EXPECT_TRUE(std::equal(re.begin(), re.end(), data));

        if (apci.format != FrameFormat::I) {
            continue;
        }
        const std::uint8_t* asdu = data + kApciLength;
        AsduHeader h{};
        std::size_t consumed = 0;
        ASSERT_EQ(decode_asdu_header(asdu, apci.asdu_length, kIec104Profile, h, consumed),
                  IEC104_OK);
        std::array<std::uint8_t, 6> hb{};
        std::size_t written = 0;
        ASSERT_EQ(encode_asdu_header(h, kIec104Profile, hb.data(), hb.size(), written), IEC104_OK);
        EXPECT_EQ(written, consumed);
        EXPECT_TRUE(
            std::equal(hb.begin(), hb.begin() + static_cast<std::ptrdiff_t>(written), asdu));
    }
}
