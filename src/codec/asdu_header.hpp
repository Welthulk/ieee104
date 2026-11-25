// ASDU data unit identifier (type id, VSQ, COT, common address) and the
// information object address (IOA).
//
// Field lengths are parameters (AsduProfile). The IEC 104 profile is fixed at
// COT 2, CA 2, IOA 3 octets; other values exist only for non-conforming
// peers (docs/02_Konzept, "ASDU-Profil").
#pragma once

#include <cstddef>
#include <cstdint>

#include "iec104/iec104.h"

namespace iec104::codec {

struct AsduProfile {
    std::uint8_t cot_length{2}; // 1..2 (2 = with originator address)
    std::uint8_t ca_length{2};  // 1..2
    std::uint8_t ioa_length{3}; // 1..3
};

inline constexpr AsduProfile kIec104Profile{};

inline constexpr std::uint8_t kVsqSequenceBit = 0x80U;
inline constexpr std::uint8_t kMaxObjects = 0x7FU;   // 127
inline constexpr std::uint8_t kCotCauseMask = 0x3FU; // causes 0..63
inline constexpr std::uint8_t kCotNegativeBit = 0x40U;
inline constexpr std::uint8_t kCotTestBit = 0x80U;
inline constexpr std::uint16_t kBroadcastCa = 0xFFFFU; // with ca_length 2

struct AsduHeader {
    std::uint8_t type_id{0};
    bool sequence{false};          // SQ bit of VSQ
    std::uint8_t num_objects{0};   // 0..127
    std::uint8_t cause{0};         // COT 0..63
    bool negative{false};          // P/N bit
    bool test{false};              // T bit
    std::uint8_t originator{0};    // only with cot_length 2
    std::uint16_t common_address{0};
};

bool is_valid(const AsduProfile& profile) noexcept;

// 2 + cot_length + ca_length (6 in the IEC 104 profile).
std::size_t header_length(const AsduProfile& profile) noexcept;

iec104_status encode_asdu_header(const AsduHeader& header, const AsduProfile& profile,
                                 std::uint8_t* out, std::size_t out_size,
                                 std::size_t& written) noexcept;

// Decodes the header only, objects are not interpreted. Type id and cause are
// not checked against tables here (that is TASK-001).
iec104_status decode_asdu_header(const std::uint8_t* data, std::size_t size,
                                 const AsduProfile& profile, AsduHeader& out,
                                 std::size_t& consumed) noexcept;

iec104_status encode_ioa(std::uint32_t ioa, const AsduProfile& profile, std::uint8_t* out,
                         std::size_t out_size, std::size_t& written) noexcept;

iec104_status decode_ioa(const std::uint8_t* data, std::size_t size, const AsduProfile& profile,
                         std::uint32_t& ioa, std::size_t& consumed) noexcept;

} // namespace iec104::codec
