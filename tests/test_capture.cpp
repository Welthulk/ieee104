/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_capture.cpp
 * @brief Replays a capture of a foreign implementation through the codec.
 *
 * tests/data/lib60870_capture.txt holds every APDU exchanged between the
 * lib60870-C v2.4.1 example client and server (external test peer only, not
 * linked). This test checks that the codec accepts all of them and produces
 * byte-identical APCIs, ASDU headers and complete ASDUs when re-encoding the
 * decoded values.
 * New frames added to the capture are covered automatically.
 *
 * tests/data/lib60870_tls_capture.txt holds the APDUs of this library's
 * client with the lib60870 TLS example server; frames marked "deviation"
 * are known errors of the peer and must be rejected by the strict codec.
 */
#include "codec/apci.hpp"
#include "codec/asdu.hpp"
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
    bool deviation = false;          // marked as known error of the peer
};

// Reads all "SEND:" and "RCVD:" lines. Lines starting with '#' are comments;
// "# deviation" after the octets marks a known error of the peer.
std::vector<CapturedApdu> load_capture(const std::string& name = "lib60870_capture.txt") {
    std::vector<CapturedApdu> result;
    std::ifstream in(std::string(IEC104_TEST_DATA_DIR) + "/" + name);
    std::string text;
    int line = 0;
    while (std::getline(in, text)) {
        ++line;
        if (text.rfind("SEND:", 0) != 0 && text.rfind("RCVD:", 0) != 0) {
            continue;
        }
        CapturedApdu apdu;
        apdu.line = line;
        apdu.deviation = text.find("# deviation") != std::string::npos;
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

namespace {

// Rule: every APDU of a foreign implementation is framed, decoded and
// re-encoded to identical bytes (APCI for all formats, ASDU header for I-frames).
// Source: capture (whole file).
void check_round_trip(const std::vector<CapturedApdu>& apdus) {
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

        // Complete ASDU (strict IEC 104 profile): decode and re-encode.
        std::array<InformationObject, 127> objects{};
        DecodedAsdu d{};
        if (c.deviation) {
            // Known error of the peer: rejected, not misread.
            EXPECT_NE(decode_asdu(asdu, apci.asdu_length, kIec104Profile, objects.data(),
                                  objects.size(), d),
                      IEC104_OK);
            continue;
        }
        ASSERT_EQ(
            decode_asdu(asdu, apci.asdu_length, kIec104Profile, objects.data(), objects.size(), d),
            IEC104_OK);
        std::array<std::uint8_t, kMaxAsduLength> ab{};
        std::size_t asdu_written = 0;
        ASSERT_EQ(encode_asdu(d.header, objects.data(), d.object_count, kIec104Profile, ab.data(),
                              ab.size(), asdu_written),
                  IEC104_OK);
        ASSERT_EQ(asdu_written, apci.asdu_length);
        EXPECT_TRUE(
            std::equal(ab.begin(), ab.begin() + static_cast<std::ptrdiff_t>(asdu_written), asdu));
    }
}

} // namespace

TEST(Capture, EveryApduRoundTrips) {
    const std::vector<CapturedApdu> apdus = load_capture();
    ASSERT_GE(apdus.size(), 10U) << "capture file missing or truncated";
    check_round_trip(apdus);
}

// Rule: the APDUs of a foreign server over TLS (decrypted) round-trip as
// well; its malformed termination (type 0, VSQ 0) is rejected by the strict
// codec. Source: tests/data/lib60870_tls_capture.txt.
TEST(Capture, ForeignTlsServer) {
    const std::vector<CapturedApdu> apdus = load_capture("lib60870_tls_capture.txt");
    ASSERT_GE(apdus.size(), 20U) << "capture file missing or truncated";
    EXPECT_EQ(std::count_if(apdus.begin(), apdus.end(),
                            [](const CapturedApdu& c) { return c.deviation; }),
              1);
    check_round_trip(apdus);
}
