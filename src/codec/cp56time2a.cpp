#include "codec/cp56time2a.hpp"

#include "codec/byte_io.hpp"

namespace iec104::codec {

iec104_status validate(const Cp56Time2a& t) noexcept {
    if (t.milliseconds > 59999U || t.minute > 59U || t.hour > 23U || t.day_of_month < 1U ||
        t.day_of_month > 31U || t.day_of_week > 7U || t.month < 1U || t.month > 12U ||
        t.year > 99U) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    return IEC104_OK;
}

iec104_status encode_cp56time2a(const Cp56Time2a& t, std::uint8_t* out,
                                std::size_t out_size) noexcept {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < kCp56Time2aLength) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    const iec104_status st = validate(t);
    if (st != IEC104_OK) {
        return st;
    }
    detail::put_u16_le(out, t.milliseconds);
    out[2] = static_cast<std::uint8_t>((t.minute & 0x3FU) | (t.res1 ? 0x40U : 0U) |
                                       (t.invalid ? 0x80U : 0U));
    out[3] = static_cast<std::uint8_t>((t.hour & 0x1FU) | (t.summer_time ? 0x80U : 0U));
    out[4] = static_cast<std::uint8_t>((t.day_of_month & 0x1FU) |
                                       ((t.day_of_week & 0x07U) << 5U));
    out[5] = static_cast<std::uint8_t>(t.month & 0x0FU);
    out[6] = static_cast<std::uint8_t>(t.year & 0x7FU);
    return IEC104_OK;
}

iec104_status decode_cp56time2a(const std::uint8_t* data, std::size_t size,
                                Cp56Time2a& out) noexcept {
    if (data == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (size < kCp56Time2aLength) {
        return IEC104_ERR_NEED_MORE_DATA;
    }
    Cp56Time2a t{};
    t.milliseconds = detail::get_u16_le(data);
    t.minute = static_cast<std::uint8_t>(data[2] & 0x3FU);
    t.res1 = (data[2] & 0x40U) != 0U;
    t.invalid = (data[2] & 0x80U) != 0U;
    t.hour = static_cast<std::uint8_t>(data[3] & 0x1FU);
    t.summer_time = (data[3] & 0x80U) != 0U;
    t.day_of_month = static_cast<std::uint8_t>(data[4] & 0x1FU);
    t.day_of_week = static_cast<std::uint8_t>((data[4] >> 5U) & 0x07U);
    t.month = static_cast<std::uint8_t>(data[5] & 0x0FU);
    t.year = static_cast<std::uint8_t>(data[6] & 0x7FU);
    out = t;
    return IEC104_OK;
}

} // namespace iec104::codec
