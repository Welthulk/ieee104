#include "codec/apci.hpp"

#include "codec/byte_io.hpp"

namespace iec104::codec {
namespace {

void put_sequence(std::uint8_t* p, std::uint16_t seq) noexcept {
    // 15-bit counter shifted left by one; bit 0 of the first octet is the
    // format bit and stays 0.
    detail::put_u16_le(p, static_cast<std::uint16_t>(seq << 1U));
}

std::uint16_t get_sequence(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(detail::get_u16_le(p) >> 1U);
}

bool is_valid_u_function(std::uint8_t octet) noexcept {
    switch (static_cast<UFunction>(octet)) {
    case UFunction::StartDtAct:
    case UFunction::StartDtCon:
    case UFunction::StopDtAct:
    case UFunction::StopDtCon:
    case UFunction::TestFrAct:
    case UFunction::TestFrCon:
        return true;
    case UFunction::None:
        return false;
    }
    return false;
}

} // namespace

iec104_status peek_apdu_length(const std::uint8_t* data, std::size_t size,
                               std::size_t& apdu_length) noexcept {
    if (data == nullptr && size != 0U) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (size < 2U) {
        return IEC104_ERR_NEED_MORE_DATA;
    }
    if (data[0] != kStartByte) {
        return IEC104_ERR_MALFORMED;
    }
    const std::size_t length = data[1];
    if (length < kControlFieldLength || length > kMaxLengthField) {
        return IEC104_ERR_MALFORMED;
    }
    apdu_length = length + 2U;
    return IEC104_OK;
}

iec104_status encode_i_header(std::uint16_t send_seq, std::uint16_t recv_seq,
                              std::size_t asdu_length, std::uint8_t* out,
                              std::size_t out_size) noexcept {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < kApciLength) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    if (send_seq >= kSequenceModulo || recv_seq >= kSequenceModulo || asdu_length == 0U ||
        asdu_length > kMaxAsduLength) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    out[0] = kStartByte;
    out[1] = static_cast<std::uint8_t>(asdu_length + kControlFieldLength);
    put_sequence(&out[2], send_seq);
    put_sequence(&out[4], recv_seq);
    return IEC104_OK;
}

iec104_status encode_s_frame(std::uint16_t recv_seq, std::uint8_t* out,
                             std::size_t out_size) noexcept {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < kApciLength) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    if (recv_seq >= kSequenceModulo) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    out[0] = kStartByte;
    out[1] = static_cast<std::uint8_t>(kControlFieldLength);
    out[2] = 0x01U;
    out[3] = 0x00U;
    put_sequence(&out[4], recv_seq);
    return IEC104_OK;
}

iec104_status encode_u_frame(UFunction function, std::uint8_t* out, std::size_t out_size) noexcept {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < kApciLength) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    if (!is_valid_u_function(static_cast<std::uint8_t>(function))) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    out[0] = kStartByte;
    out[1] = static_cast<std::uint8_t>(kControlFieldLength);
    out[2] = static_cast<std::uint8_t>(function);
    out[3] = 0x00U;
    out[4] = 0x00U;
    out[5] = 0x00U;
    return IEC104_OK;
}

iec104_status decode_apci(const std::uint8_t* data, std::size_t size, Apci& out) noexcept {
    std::size_t apdu_length = 0;
    const iec104_status st = peek_apdu_length(data, size, apdu_length);
    if (st != IEC104_OK) {
        return st;
    }
    if (size < apdu_length) {
        return IEC104_ERR_NEED_MORE_DATA;
    }
    const std::size_t length = data[1];
    const std::uint8_t c1 = data[2];

    Apci result{};
    if ((c1 & 0x01U) == 0U) {
        // I-format: bit 0 of octet 3 must be 0, ASDU must be present.
        if ((data[4] & 0x01U) != 0U || length == kControlFieldLength) {
            return IEC104_ERR_MALFORMED;
        }
        result.format = FrameFormat::I;
        result.send_seq = get_sequence(&data[2]);
        result.recv_seq = get_sequence(&data[4]);
        result.asdu_length = length - kControlFieldLength;
    } else if ((c1 & 0x03U) == 0x01U) {
        // S-format: octet 1 = 0x01, octet 2 = 0, no ASDU.
        if (c1 != 0x01U || data[3] != 0U || (data[4] & 0x01U) != 0U ||
            length != kControlFieldLength) {
            return IEC104_ERR_MALFORMED;
        }
        result.format = FrameFormat::S;
        result.recv_seq = get_sequence(&data[4]);
    } else {
        // U-format: exactly one function, octets 2..4 zero, no ASDU.
        if (!is_valid_u_function(c1) || data[3] != 0U || data[4] != 0U || data[5] != 0U ||
            length != kControlFieldLength) {
            return IEC104_ERR_MALFORMED;
        }
        result.format = FrameFormat::U;
        result.u_function = static_cast<UFunction>(c1);
    }
    out = result;
    return IEC104_OK;
}

} // namespace iec104::codec
