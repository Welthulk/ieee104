/**
 * @file apci.hpp
 * @brief APCI encoding and decoding (start octet, length, control field).
 *
 * Every IEC 60870-5-104 APDU starts with a 6-octet APCI (application protocol
 * control information):
 *
 *   octet 1:    start octet 0x68
 *   octet 2:    length of the rest of the APDU (control field + ASDU), 4..253
 *   octets 3-6: control field, 4 octets; its first bits select the frame format
 *
 * There are three frame formats:
 *   - I-format: carries an ASDU and the send/receive sequence numbers N(S), N(R)
 *   - S-format: acknowledges received I-frames with N(R), no ASDU
 *   - U-format: link control (STARTDT, STOPDT, TESTFR, each act/con), no ASDU
 *
 * This file only converts between bytes and the Apci struct. It does not keep
 * state: sequence number checks against the window, k/w and the timers belong to
 * the apci state machine (TASK-002).
 *
 * Sources: docs/03 (research report), section on the APCI, which cites the
 * Wireshark IEC 104 dissector; reference bytes in tests/data/lib60870_capture.txt.
 * TODO(norm): APCI frame formats, clause unknown.
 *
 * Layer: codec (pure functions, no I/O, no allocation).
 */
#pragma once

#include "iec104/iec104.h"

#include <cstddef>
#include <cstdint>

namespace iec104::codec {

/** @brief First octet of every APDU. A different value means the stream is out of sync. */
inline constexpr std::uint8_t kStartByte = 0x68U;

/** @brief Length of the APCI: start octet + length octet + 4 control octets. */
inline constexpr std::size_t kApciLength = 6U;

/** @brief Length of the control field, also the minimum value of the length octet. */
inline constexpr std::size_t kControlFieldLength = 4U;

/** @brief Maximum value of the length octet (control field + ASDU). Source: docs/03. */
inline constexpr std::size_t kMaxLengthField = 253U;

/** @brief Maximum length of a complete APDU on the wire: 2 + 253 = 255 octets. */
inline constexpr std::size_t kMaxApduLength = kMaxLengthField + 2U;

/** @brief Maximum ASDU length: 253 - 4 = 249 octets. Peers may agree on less. */
inline constexpr std::size_t kMaxAsduLength = kMaxLengthField - kControlFieldLength;

/**
 * @brief Sequence numbers N(S) and N(R) are 15 bits wide and count modulo 32768.
 *
 * Valid values are 0..32767. The wrap-around itself is handled by the state
 * machine, the codec only rejects values that do not fit into 15 bits.
 */
inline constexpr std::uint16_t kSequenceModulo = 32768U;

/** @brief The three APCI frame formats. */
enum class FrameFormat : std::uint8_t {
    I, ///< Information transfer: sequence numbers + ASDU.
    S, ///< Supervisory: acknowledgement with N(R) only.
    U  ///< Unnumbered: link control functions.
};

/**
 * @brief U-format functions, stored as the complete first control octet.
 *
 * The first control octet of a U-frame has bits 0 and 1 set (format marker)
 * and exactly one of the six function bits:
 *
 *   bit:   7      6      5      4      3       2       1  0
 *        TESTFR TESTFR STOPDT STOPDT STARTDT STARTDT   1  1
 *         con    act    con    act    con     act
 *
 * Example: STARTDT act -> 68 04 07 00 00 00, STARTDT con -> 68 04 0B 00 00 00
 * (both from tests/data/lib60870_capture.txt).
 */
enum class UFunction : std::uint8_t {
    None = 0x00U,       ///< No function. Only used as "not a U-frame"; never valid on the wire.
    StartDtAct = 0x07U, ///< Start data transfer, activation (sent by the controlling station).
    StartDtCon = 0x0BU, ///< Start data transfer, confirmation.
    StopDtAct = 0x13U,  ///< Stop data transfer, activation.
    StopDtCon = 0x23U,  ///< Stop data transfer, confirmation.
    TestFrAct = 0x43U,  ///< Test frame, activation (link supervision).
    TestFrCon = 0x83U,  ///< Test frame, confirmation.
};

/**
 * @brief Decoded APCI of one APDU.
 *
 * Only the fields that belong to the decoded format are meaningful; the others
 * keep their default values.
 */
struct Apci {
    FrameFormat format{FrameFormat::U};    ///< Frame format.
    std::uint16_t send_seq{0};             ///< N(S), 0..32767. I-format only.
    std::uint16_t recv_seq{0};             ///< N(R), 0..32767. I- and S-format.
    UFunction u_function{UFunction::None}; ///< Function. U-format only.
    std::size_t asdu_length{0}; ///< Length of the ASDU after the APCI, 1..249. I-format only.
};

/**
 * @brief Determines the total length of the next APDU in a byte stream.
 *
 * TCP delivers a stream without message boundaries. The first two octets of an
 * APDU (start octet and length octet) are enough to know how many octets the
 * complete APDU has: 2 + length octet. The transport layer calls this function
 * to cut the stream into APDUs before decoding them.
 *
 * Example: 68 0E ... -> apdu_length = 16
 *
 * @param data        Start of the received bytes. May be NULL only if @p size is 0.
 * @param size        Number of bytes available at @p data.
 * @param apdu_length Receives the total APDU length (6..255) on success. Unchanged otherwise.
 * @return IEC104_OK on success.
 * @return IEC104_ERR_INVALID_ARGUMENT if @p data is NULL and @p size is not 0.
 * @return IEC104_ERR_NEED_MORE_DATA if fewer than two octets are available.
 * @return IEC104_ERR_MALFORMED if the start octet is not 0x68 or the length octet
 *         is outside 4..253. The stream cannot be resynchronised; the caller
 *         should close the connection.
 */
iec104_status peek_apdu_length(const std::uint8_t* data, std::size_t size,
                               std::size_t& apdu_length) noexcept;

/**
 * @brief Writes the 6-octet APCI of an I-frame.
 *
 * I-format control field (sequence numbers shifted left by one bit, little endian):
 *
 *   octet 3:  N(S) bits 0..6  | 0      <- bit 0 = 0 marks the I-format
 *   octet 4:  N(S) bits 7..14
 *   octet 5:  N(R) bits 0..6  | 0
 *   octet 6:  N(R) bits 7..14
 *
 * Example: N(S) = 0, N(R) = 2, ASDU length 10 -> 68 0E 00 00 04 00
 * (from tests/data/lib60870_capture.txt, general interrogation).
 *
 * The ASDU itself is not written; it must follow directly after these
 * 6 octets in the output stream.
 *
 * @param send_seq    N(S), 0..32767.
 * @param recv_seq    N(R), 0..32767.
 * @param asdu_length Length of the ASDU that will follow, 1..249.
 * @param out         Destination buffer.
 * @param out_size    Size of @p out, at least 6.
 * @return IEC104_OK on success.
 * @return IEC104_ERR_INVALID_ARGUMENT if @p out is NULL.
 * @return IEC104_ERR_BUFFER_TOO_SMALL if @p out_size is less than 6.
 * @return IEC104_ERR_OUT_OF_RANGE if a sequence number exceeds 32767 or
 *         @p asdu_length is 0 or greater than 249.
 */
iec104_status encode_i_header(std::uint16_t send_seq, std::uint16_t recv_seq,
                              std::size_t asdu_length, std::uint8_t* out,
                              std::size_t out_size) noexcept;

/**
 * @brief Writes a complete S-frame (6 octets).
 *
 * S-format control field:
 *
 *   octet 3:  0 0 0 0 0 0 0 1   <- bits 1..0 = 01 mark the S-format
 *   octet 4:  0
 *   octet 5:  N(R) bits 0..6  | 0
 *   octet 6:  N(R) bits 7..14
 *
 * Example: N(R) = 10 -> 68 04 01 00 14 00 (from tests/data/lib60870_capture.txt).
 *
 * @param recv_seq N(R), 0..32767: number of the next expected I-frame. It
 *                 acknowledges all I-frames with a lower N(S).
 * @param out      Destination buffer.
 * @param out_size Size of @p out, at least 6.
 * @return IEC104_OK on success.
 * @return IEC104_ERR_INVALID_ARGUMENT if @p out is NULL.
 * @return IEC104_ERR_BUFFER_TOO_SMALL if @p out_size is less than 6.
 * @return IEC104_ERR_OUT_OF_RANGE if @p recv_seq exceeds 32767.
 */
iec104_status encode_s_frame(std::uint16_t recv_seq, std::uint8_t* out,
                             std::size_t out_size) noexcept;

/**
 * @brief Writes a complete U-frame (6 octets).
 *
 * U-format control field: octet 3 is the function octet (see ::UFunction),
 * octets 4..6 are zero.
 *
 * Example: TESTFR act -> 68 04 43 00 00 00
 *
 * @param function One of the six functions; UFunction::None is rejected.
 * @param out      Destination buffer.
 * @param out_size Size of @p out, at least 6.
 * @return IEC104_OK on success.
 * @return IEC104_ERR_INVALID_ARGUMENT if @p out is NULL or @p function is not
 *         one of the six defined functions.
 * @return IEC104_ERR_BUFFER_TOO_SMALL if @p out_size is less than 6.
 */
iec104_status encode_u_frame(UFunction function, std::uint8_t* out, std::size_t out_size) noexcept;

/**
 * @brief Decodes the APCI of one complete APDU.
 *
 * The check is strict on purpose: a malformed APCI means the peer and this
 * library disagree on the frame structure, and continuing would risk
 * misinterpreting data (docs/03, design consequences: a length above 253 and
 * more than one U-function bit are never tolerated). Checked are:
 *   - start octet 0x68 and length octet 4..253;
 *   - I-format: bit 0 of octet 5 is 0 and an ASDU is present (length > 4);
 *   - S-format: octet 3 is exactly 0x01, octet 4 is 0, bit 0 of octet 5 is 0,
 *     no ASDU;
 *   - U-format: exactly one function bit, octets 4..6 are 0, no ASDU.
 *
 * @param data Start of the APDU. Must not be NULL.
 * @param size Number of bytes available; must cover the whole APDU
 *             (2 + length octet). Additional bytes after the APDU are ignored.
 * @param out  Receives the decoded APCI on success. Unchanged on error.
 * @return IEC104_OK on success.
 * @return IEC104_ERR_INVALID_ARGUMENT if @p data is NULL and @p size is not 0.
 * @return IEC104_ERR_NEED_MORE_DATA if @p size is smaller than the APDU.
 * @return IEC104_ERR_MALFORMED if any of the checks above fails.
 */
iec104_status decode_apci(const std::uint8_t* data, std::size_t size, Apci& out) noexcept;

} // namespace iec104::codec
