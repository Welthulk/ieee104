// Little-endian helpers. IEC 60870-5-104 transmits all multi-octet values
// least significant octet first ("mode 1").
#pragma once

#include <cstddef>
#include <cstdint>

namespace iec104::codec::detail {

inline void put_u16_le(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFFU);
    p[1] = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
}

inline std::uint16_t get_u16_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(static_cast<unsigned>(p[0]) |
                                      (static_cast<unsigned>(p[1]) << 8U));
}

// Writes the lowest `n` octets of `v` (n = 1..4).
inline void put_uint_le(std::uint8_t* p, std::uint32_t v, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        p[i] = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU);
    }
}

// Reads `n` octets (n = 1..4).
inline std::uint32_t get_uint_le(const std::uint8_t* p, std::size_t n) noexcept {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        v |= static_cast<std::uint32_t>(p[i]) << (8U * i);
    }
    return v;
}

} // namespace iec104::codec::detail
