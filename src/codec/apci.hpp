// APCI: start octet 0x68, length octet and four control octets.
// Pure functions on byte buffers, no I/O, no allocation.
//
// Sources: docs/03_Recherchebericht, section "Sechs Oktette APCI".
#pragma once

#include <cstddef>
#include <cstdint>

#include "iec104/iec104.h"

namespace iec104::codec {

inline constexpr std::uint8_t kStartByte = 0x68U;
inline constexpr std::size_t kApciLength = 6U;          // start + length + 4 control octets
inline constexpr std::size_t kControlFieldLength = 4U;
inline constexpr std::size_t kMaxLengthField = 253U;    // control octets + ASDU
inline constexpr std::size_t kMaxApduLength = kMaxLengthField + 2U;            // 255
inline constexpr std::size_t kMaxAsduLength = kMaxLengthField - kControlFieldLength; // 249
inline constexpr std::uint16_t kSequenceModulo = 32768U; // N(S), N(R): 15 bit

enum class FrameFormat : std::uint8_t { I, S, U };

// U-format function octet (control octet 1). Exactly one function per frame.
enum class UFunction : std::uint8_t {
    None = 0x00U,
    StartDtAct = 0x07U,
    StartDtCon = 0x0BU,
    StopDtAct = 0x13U,
    StopDtCon = 0x23U,
    TestFrAct = 0x43U,
    TestFrCon = 0x83U,
};

struct Apci {
    FrameFormat format{FrameFormat::U};
    std::uint16_t send_seq{0};          // N(S), I-format only
    std::uint16_t recv_seq{0};          // N(R), I- and S-format
    UFunction u_function{UFunction::None}; // U-format only
    std::size_t asdu_length{0};         // I-format only, 1..249
};

// Stream framing: from the first two octets, determine the total APDU length
// (2 + length field). Returns NEED_MORE_DATA if fewer than two octets are
// available, MALFORMED on a wrong start octet or a length outside 4..253.
iec104_status peek_apdu_length(const std::uint8_t* data, std::size_t size,
                               std::size_t& apdu_length) noexcept;

// Writes the 6-octet APCI of an I-frame. The ASDU (asdu_length octets) must
// follow directly in the output stream. out_size >= 6.
iec104_status encode_i_header(std::uint16_t send_seq, std::uint16_t recv_seq,
                              std::size_t asdu_length, std::uint8_t* out,
                              std::size_t out_size) noexcept;

// Writes a complete S-frame (6 octets).
iec104_status encode_s_frame(std::uint16_t recv_seq, std::uint8_t* out,
                             std::size_t out_size) noexcept;

// Writes a complete U-frame (6 octets).
iec104_status encode_u_frame(UFunction function, std::uint8_t* out, std::size_t out_size) noexcept;

// Decodes the APCI of a complete APDU. `size` must cover the whole APDU
// (2 + length field), otherwise NEED_MORE_DATA. Strict: reserved bits and
// unused octets must be zero, U-frames must carry exactly one function.
iec104_status decode_apci(const std::uint8_t* data, std::size_t size, Apci& out) noexcept;

} // namespace iec104::codec
