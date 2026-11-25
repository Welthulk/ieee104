/**
 * @file test_apci.cpp
 * @brief Unit tests for the APCI codec (src/codec/apci.hpp).
 *
 * Reference bytes come from two kinds of sources, named at every test:
 *   - "capture": tests/data/lib60870_capture.txt, produced by an independent
 *     implementation (lib60870-C v2.4.1, used only as external test peer);
 *   - "layout": derived by hand from the bit layouts documented in apci.hpp,
 *     which follow docs/03 (citing the Wireshark IEC 104 dissector).
 */
#include "codec/apci.hpp"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>

using namespace iec104::codec;
using Bytes6 = std::array<std::uint8_t, 6>;

// Rule: each U-function is one specific first control octet, the other three
// control octets are zero, and decoding returns the same function.
// Source: STARTDT act/con from the capture (lines "SEND: 68 04 07 ..." and
// "RCVD: 68 04 0b ..."); STOPDT and TESTFR from the function octets listed in
// docs/03 (Wireshark dissector masks).
TEST(Apci, EncodeUFrames) {
    struct Case {
        UFunction f;
        Bytes6 expected;
    };
    const Case cases[] = {
        {UFunction::StartDtAct, {0x68, 0x04, 0x07, 0x00, 0x00, 0x00}},
        {UFunction::StartDtCon, {0x68, 0x04, 0x0B, 0x00, 0x00, 0x00}},
        {UFunction::StopDtAct, {0x68, 0x04, 0x13, 0x00, 0x00, 0x00}},
        {UFunction::StopDtCon, {0x68, 0x04, 0x23, 0x00, 0x00, 0x00}},
        {UFunction::TestFrAct, {0x68, 0x04, 0x43, 0x00, 0x00, 0x00}},
        {UFunction::TestFrCon, {0x68, 0x04, 0x83, 0x00, 0x00, 0x00}},
    };
    for (const auto& c : cases) {
        Bytes6 out{};
        ASSERT_EQ(encode_u_frame(c.f, out.data(), out.size()), IEC104_OK);
        EXPECT_EQ(out, c.expected);

        Apci apci{};
        ASSERT_EQ(decode_apci(out.data(), out.size(), apci), IEC104_OK);
        EXPECT_EQ(apci.format, FrameFormat::U);
        EXPECT_EQ(apci.u_function, c.f);
    }
}

// Rule: UFunction::None is not a function and must never be put on the wire.
// Source: apci.hpp (API contract).
TEST(Apci, EncodeUFrameRejectsNone) {
    Bytes6 out{};
    EXPECT_EQ(encode_u_frame(UFunction::None, out.data(), out.size()), IEC104_ERR_INVALID_ARGUMENT);
}

// Rule: S-frame = 01 00 followed by N(R) shifted left by one bit.
// Source: layout (N(R) = 1 -> 02 00).
TEST(Apci, EncodeSFrame) {
    Bytes6 out{};
    ASSERT_EQ(encode_s_frame(1, out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out, (Bytes6{0x68, 0x04, 0x01, 0x00, 0x02, 0x00}));

    Apci apci{};
    ASSERT_EQ(decode_apci(out.data(), out.size(), apci), IEC104_OK);
    EXPECT_EQ(apci.format, FrameFormat::S);
    EXPECT_EQ(apci.recv_seq, 1U);
}

// Rule: same as EncodeSFrame, checked against a peer's frame.
// Source: capture, client acknowledgement "SEND: 68 04 01 00 14 00" (N(R) = 10).
TEST(Apci, SFrameFromCapture) {
    const Bytes6 captured{0x68, 0x04, 0x01, 0x00, 0x14, 0x00};
    Apci apci{};
    ASSERT_EQ(decode_apci(captured.data(), captured.size(), apci), IEC104_OK);
    EXPECT_EQ(apci.format, FrameFormat::S);
    EXPECT_EQ(apci.recv_seq, 10U);

    Bytes6 out{};
    ASSERT_EQ(encode_s_frame(10, out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out, captured);
}

// Rule: N(S) and N(R) survive an encode/decode round trip over the full
// 15-bit range, including the octet boundaries (127/128, 255/256) and the
// largest value 32767.
// Source: layout.
TEST(Apci, IFrameSequenceNumbersRoundTrip) {
    const std::uint16_t values[] = {0, 1, 127, 128, 255, 256, 16383, 32766, 32767};
    for (std::uint16_t ns : values) {
        for (std::uint16_t nr : values) {
            std::array<std::uint8_t, 7> buf{};
            ASSERT_EQ(encode_i_header(ns, nr, 1, buf.data(), buf.size()), IEC104_OK);
            Apci apci{};
            ASSERT_EQ(decode_apci(buf.data(), buf.size(), apci), IEC104_OK);
            EXPECT_EQ(apci.format, FrameFormat::I);
            EXPECT_EQ(apci.send_seq, ns);
            EXPECT_EQ(apci.recv_seq, nr);
            EXPECT_EQ(apci.asdu_length, 1U);
        }
    }
}

// Rule: I-frame control field = N(S) << 1, N(R) << 1, little endian; length
// octet = ASDU length + 4.
// Source: layout (N(S) = 2, N(R) = 3, ASDU 10 octets -> 0E 04 00 06 00).
TEST(Apci, IFrameKnownBytes) {
    Bytes6 out{};
    ASSERT_EQ(encode_i_header(2, 3, 10, out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out, (Bytes6{0x68, 0x0E, 0x04, 0x00, 0x06, 0x00}));
}

// Rule: as IFrameKnownBytes, checked against a peer's frames.
// Source: capture, general interrogation "SEND: 68 0e 00 00 04 00 ..."
// (N(S) = 0, N(R) = 2, ASDU 10 octets) and test command confirmation
// "RCVD: 68 16 14 00 04 00 ..." (N(S) = 10, N(R) = 2, ASDU 18 octets).
TEST(Apci, IFrameHeaderFromCapture) {
    Bytes6 out{};
    ASSERT_EQ(encode_i_header(0, 2, 10, out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out, (Bytes6{0x68, 0x0E, 0x00, 0x00, 0x04, 0x00}));
    ASSERT_EQ(encode_i_header(10, 2, 18, out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out, (Bytes6{0x68, 0x16, 0x14, 0x00, 0x04, 0x00}));
}

// Rule: values outside the 15-bit sequence range and ASDU lengths outside
// 1..249 are rejected, never truncated; 249 is the largest allowed ASDU
// (length octet 253). Also covers buffer and NULL checks of encode_s_frame.
// Source: limits from docs/03 (APDU at most 255 octets).
TEST(Apci, EncodeRejectsOutOfRange) {
    Bytes6 out{};
    EXPECT_EQ(encode_i_header(32768, 0, 1, out.data(), out.size()), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(encode_i_header(0, 32768, 1, out.data(), out.size()), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(encode_i_header(0, 0, 0, out.data(), out.size()), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(encode_i_header(0, 0, kMaxAsduLength + 1U, out.data(), out.size()),
              IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(encode_i_header(0, 0, kMaxAsduLength, out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out[1], 253U);
    EXPECT_EQ(encode_s_frame(32768, out.data(), out.size()), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(encode_s_frame(0, out.data(), 5), IEC104_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(encode_s_frame(0, nullptr, 6), IEC104_ERR_INVALID_ARGUMENT);
}

// Rule: stream framing needs two octets; the start octet must be 0x68 and the
// length octet 4..253; total length = 2 + length octet.
// Source: docs/03 (APCI section).
TEST(Apci, PeekLength) {
    std::size_t len = 0;
    const std::uint8_t ok[] = {0x68, 0x0E};
    EXPECT_EQ(peek_apdu_length(ok, 1, len), IEC104_ERR_NEED_MORE_DATA);
    ASSERT_EQ(peek_apdu_length(ok, 2, len), IEC104_OK);
    EXPECT_EQ(len, 16U);

    const std::uint8_t bad_start[] = {0x69, 0x04};
    EXPECT_EQ(peek_apdu_length(bad_start, 2, len), IEC104_ERR_MALFORMED);
    const std::uint8_t too_short[] = {0x68, 0x03};
    EXPECT_EQ(peek_apdu_length(too_short, 2, len), IEC104_ERR_MALFORMED);
    const std::uint8_t too_long[] = {0x68, 0xFE};
    EXPECT_EQ(peek_apdu_length(too_long, 2, len), IEC104_ERR_MALFORMED);
}

// Rule: decoding is strict (see decode_apci() in apci.hpp): incomplete
// APDUs need more data; I-frames need an ASDU and a clear reserved bit;
// S- and U-frames have no ASDU and zero reserved octets; a U-frame carries
// exactly one function.
// Source: layout; malformed inputs constructed by hand.
TEST(Apci, DecodeRejectsMalformed) {
    Apci apci{};
    // incomplete APDU
    const std::uint8_t partial[] = {0x68, 0x0E, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(decode_apci(partial, sizeof partial, apci), IEC104_ERR_NEED_MORE_DATA);
    // I-frame without ASDU
    const std::uint8_t i_empty[] = {0x68, 0x04, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(decode_apci(i_empty, sizeof i_empty, apci), IEC104_ERR_MALFORMED);
    // S-frame with non-zero octet 2
    const std::uint8_t s_bad[] = {0x68, 0x04, 0x01, 0x01, 0x00, 0x00};
    EXPECT_EQ(decode_apci(s_bad, sizeof s_bad, apci), IEC104_ERR_MALFORMED);
    // U-frame with two functions (STARTDT act + TESTFR act)
    const std::uint8_t u_two[] = {0x68, 0x04, 0x47, 0x00, 0x00, 0x00};
    EXPECT_EQ(decode_apci(u_two, sizeof u_two, apci), IEC104_ERR_MALFORMED);
    // U-frame with trailing ASDU
    const std::uint8_t u_long[] = {0x68, 0x05, 0x07, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(decode_apci(u_long, sizeof u_long, apci), IEC104_ERR_MALFORMED);
    // I-frame with LSB of N(R) set
    const std::uint8_t i_bad_nr[] = {0x68, 0x05, 0x00, 0x00, 0x01, 0x00, 0x00};
    EXPECT_EQ(decode_apci(i_bad_nr, sizeof i_bad_nr, apci), IEC104_ERR_MALFORMED);
}
