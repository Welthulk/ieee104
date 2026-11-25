/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file values.cpp
 * @brief Implementation of the value conversions (see values.hpp) and the time functions of
 *        the C API.
 *
 * Layer: API.
 */
#include "api/values.hpp"

#include <cmath>

namespace iec104::api {
namespace {

/** @brief Groups of types that share one mapping (table at iec104_object). */
enum class Kind : std::uint8_t {
    None,
    SinglePoint,          // 1, 30
    DoublePoint,          // 3, 31
    StepPosition,         // 5, 32
    Bitstring,            // 7, 33
    Normalized,           // 9, 34
    Scaled,               // 11, 35
    ShortFloat,           // 13, 36
    Totals,               // 15, 37
    NormalizedNoQds,      // 21
    SingleCommand,        // 45, 58
    DoubleCommand,        // 46, 59
    StepCommand,          // 47, 60
    SetNormalized,        // 48, 61
    SetScaled,            // 49, 62
    SetFloat,             // 50, 63
    BitCommand,           // 51, 64
    EndOfInit,            // 70
    Interrogation,        // 100
    CounterInterrogation, // 101
    Read,                 // 102
    ClockSync,            // 103
    ResetProcess,         // 105
    TestWithTime,         // 107
};

Kind kind_of(std::uint8_t type) noexcept {
    switch (type) {
    case 1:
    case 30:
        return Kind::SinglePoint;
    case 3:
    case 31:
        return Kind::DoublePoint;
    case 5:
    case 32:
        return Kind::StepPosition;
    case 7:
    case 33:
        return Kind::Bitstring;
    case 9:
    case 34:
        return Kind::Normalized;
    case 11:
    case 35:
        return Kind::Scaled;
    case 13:
    case 36:
        return Kind::ShortFloat;
    case 15:
    case 37:
        return Kind::Totals;
    case 21:
        return Kind::NormalizedNoQds;
    case 45:
    case 58:
        return Kind::SingleCommand;
    case 46:
    case 59:
        return Kind::DoubleCommand;
    case 47:
    case 60:
        return Kind::StepCommand;
    case 48:
    case 61:
        return Kind::SetNormalized;
    case 49:
    case 62:
        return Kind::SetScaled;
    case 50:
    case 63:
        return Kind::SetFloat;
    case 51:
    case 64:
        return Kind::BitCommand;
    case 70:
        return Kind::EndOfInit;
    case 100:
        return Kind::Interrogation;
    case 101:
        return Kind::CounterInterrogation;
    case 102:
        return Kind::Read;
    case 103:
        return Kind::ClockSync;
    case 105:
        return Kind::ResetProcess;
    case 107:
        return Kind::TestWithTime;
    default:
        return Kind::None;
    }
}

/** @brief True for the kinds of the monitor direction (including end of initialization). */
bool is_monitor(Kind kind) noexcept {
    return kind <= Kind::NormalizedNoQds || kind == Kind::EndOfInit;
}

/** @brief Types whose objects end with a CP56Time2a time tag (30..37, 58..64, 107). */
bool has_time_tag(std::uint8_t type) noexcept {
    return (type >= 30U && type <= 37U) || (type >= 58U && type <= 64U) || type == 107U;
}

/** @brief A double that must be a whole number in [lo, hi]. */
bool to_int(double v, double lo, double hi, long& out) noexcept {
    if (!std::isfinite(v) || v != std::floor(v) || v < lo || v > hi) {
        return false;
    }
    out = static_cast<long>(v);
    return true;
}

/**
 * @brief Fraction -> NVA raw value (value * 32768, docs/02_Technik_und_Quellen.md,
 *        "Informationselemente").
 *
 * The largest representable fraction is 1 - 2^-15; rounding to the nearest
 * raw value keeps decimal fractions like 0.1 as close as possible.
 */
bool to_nva(double v, std::int16_t& out) noexcept {
    if (!std::isfinite(v)) {
        return false;
    }
    const double raw = std::round(v * 32768.0);
    if (raw < -32768.0 || raw > 32767.0) {
        return false;
    }
    out = static_cast<std::int16_t>(raw);
    return true;
}

double from_nva(std::int16_t raw) noexcept {
    return static_cast<double>(raw) / 32768.0;
}

/** @brief Monitor-direction kinds (and end of initialization), see to_codec(). */
iec104_status monitor_to_codec(Kind kind, const iec104_object& in,
                               codec::InformationObject& o) noexcept {
    long n = 0;
    const iec104_status range = IEC104_ERR_OUT_OF_RANGE;
    switch (kind) {
    case Kind::None:
        return IEC104_ERR_INVALID_ARGUMENT;
    case Kind::SinglePoint:
        if (!to_int(in.value, 0, 1, n)) {
            return range;
        }
        o.siq = codec::Siq{n == 1, in.quality};
        break;
    case Kind::DoublePoint:
        if (!to_int(in.value, 0, 3, n)) {
            return range;
        }
        o.diq = codec::Diq{static_cast<codec::DoublePoint>(n), in.quality};
        break;
    case Kind::StepPosition:
        if (!to_int(in.value, -64, 63, n)) {
            return range;
        }
        o.vti = codec::Vti{static_cast<std::int8_t>(n), (in.flags & IEC104_FLAG_TRANSIENT) != 0U};
        o.qds = in.quality;
        break;
    case Kind::Bitstring:
        o.bsi = in.bits;
        o.qds = in.quality;
        break;
    case Kind::Normalized:
        if (!to_nva(in.value, o.nva)) {
            return range;
        }
        o.qds = in.quality;
        break;
    case Kind::NormalizedNoQds:
        if (!to_nva(in.value, o.nva)) {
            return range;
        }
        break;
    case Kind::Scaled:
        if (!to_int(in.value, -32768, 32767, n)) {
            return range;
        }
        o.sva = static_cast<std::int16_t>(n);
        o.qds = in.quality;
        break;
    case Kind::ShortFloat:
        o.r32 = static_cast<float>(in.value);
        o.qds = in.quality;
        break;
    case Kind::Totals:
        if (!to_int(in.value, -2147483648.0, 2147483647.0, n)) {
            return range;
        }
        o.bcr.counter = static_cast<std::int32_t>(n);
        o.bcr.sequence = in.qualifier;
        o.bcr.carry = (in.flags & IEC104_FLAG_CARRY) != 0U;
        o.bcr.adjusted = (in.flags & IEC104_FLAG_ADJUSTED) != 0U;
        o.bcr.invalid = (in.quality & IEC104_QUALITY_INVALID) != 0U;
        break;
    case Kind::EndOfInit:
        o.coi = codec::Coi{in.qualifier, (in.flags & IEC104_FLAG_PARAMETERS_CHANGED) != 0U};
        break;
    default:
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    return IEC104_OK;
}

/** @brief Control-direction kinds (commands and system commands), see to_codec(). */
iec104_status control_to_codec(Kind kind, const iec104_object& in,
                               codec::InformationObject& o) noexcept {
    long n = 0;
    const iec104_status range = IEC104_ERR_OUT_OF_RANGE;
    switch (kind) {
    case Kind::SingleCommand:
        if (!to_int(in.value, 0, 1, n)) {
            return range;
        }
        o.sco = codec::Sco{n == 1, false, in.qualifier, in.select != 0U};
        break;
    case Kind::DoubleCommand:
        if (!to_int(in.value, 0, 3, n)) {
            return range;
        }
        o.dco = codec::Dco{static_cast<std::uint8_t>(n), in.qualifier, in.select != 0U};
        break;
    case Kind::StepCommand:
        if (!to_int(in.value, 0, 3, n)) {
            return range;
        }
        o.rco = codec::Rco{static_cast<std::uint8_t>(n), in.qualifier, in.select != 0U};
        break;
    case Kind::SetNormalized:
        if (!to_nva(in.value, o.nva)) {
            return range;
        }
        o.qos = codec::Qos{in.qualifier, in.select != 0U};
        break;
    case Kind::SetScaled:
        if (!to_int(in.value, -32768, 32767, n)) {
            return range;
        }
        o.sva = static_cast<std::int16_t>(n);
        o.qos = codec::Qos{in.qualifier, in.select != 0U};
        break;
    case Kind::SetFloat:
        o.r32 = static_cast<float>(in.value);
        o.qos = codec::Qos{in.qualifier, in.select != 0U};
        break;
    case Kind::BitCommand:
        o.bsi = in.bits;
        break;
    case Kind::Interrogation:
        o.qoi = in.qualifier;
        break;
    case Kind::CounterInterrogation:
        if (in.bits > 3U) {
            return range;
        }
        o.qcc = codec::Qcc{in.qualifier, static_cast<std::uint8_t>(in.bits)};
        break;
    case Kind::Read:
        break;
    case Kind::ClockSync:
        if (to_cp56(in.time, o.cp56) != IEC104_OK) {
            return range;
        }
        break;
    case Kind::ResetProcess:
        o.qrp = in.qualifier;
        break;
    case Kind::TestWithTime:
        if (!to_int(in.value, 0, 65535, n)) {
            return range;
        }
        o.tsc = static_cast<std::uint16_t>(n);
        break;
    default:
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    return IEC104_OK;
}

} // namespace

bool value_type_supported(std::uint8_t type) noexcept {
    return kind_of(type) != Kind::None;
}

iec104_status to_cp56(const iec104_time& in, codec::Cp56Time2a& out) noexcept {
    if (in.year < 2000U || in.year > 2099U || in.second > 59U || in.millisecond > 999U) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    codec::Cp56Time2a t{};
    t.milliseconds = static_cast<std::uint16_t>((in.second * 1000U) + in.millisecond);
    t.minute = in.minute;
    t.hour = in.hour;
    t.day_of_month = in.day;
    t.day_of_week = in.day_of_week;
    t.month = in.month;
    t.year = static_cast<std::uint8_t>(in.year - 2000U);
    t.invalid = in.invalid != 0U;
    t.summer_time = in.summer_time != 0U;
    if (codec::validate(t) != IEC104_OK) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    out = t;
    return IEC104_OK;
}

iec104_time from_cp56(const codec::Cp56Time2a& in) noexcept {
    iec104_time t{};
    t.year = static_cast<std::uint16_t>(2000U + in.year);
    t.month = in.month;
    t.day = in.day_of_month;
    t.hour = in.hour;
    t.minute = in.minute;
    t.second = static_cast<std::uint8_t>(in.milliseconds / 1000U);
    t.millisecond = static_cast<std::uint16_t>(in.milliseconds % 1000U);
    t.day_of_week = in.day_of_week;
    t.invalid = in.invalid ? 1U : 0U;
    t.summer_time = in.summer_time ? 1U : 0U;
    return t;
}

iec104_status to_codec(const iec104_object& in, codec::InformationObject& out) noexcept {
    const Kind kind = kind_of(in.type);
    if (kind == Kind::None) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    codec::InformationObject o{};
    o.ioa = in.ioa;
    const iec104_status st =
        is_monitor(kind) ? monitor_to_codec(kind, in, o) : control_to_codec(kind, in, o);
    if (st != IEC104_OK) {
        return st;
    }
    if (has_time_tag(in.type) && to_cp56(in.time, o.cp56) != IEC104_OK) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    out = o;
    return IEC104_OK;
}

namespace {

/** @brief Monitor-direction kinds, see from_codec(). */
void monitor_from_codec(Kind kind, const codec::InformationObject& in, iec104_object& o) noexcept {
    switch (kind) {
    case Kind::SinglePoint:
        o.value = in.siq.on ? 1.0 : 0.0;
        o.quality = in.siq.quality;
        break;
    case Kind::DoublePoint:
        o.value = static_cast<double>(in.diq.state);
        o.quality = in.diq.quality;
        break;
    case Kind::StepPosition:
        o.value = in.vti.value;
        o.flags = in.vti.transient ? IEC104_FLAG_TRANSIENT : 0U;
        o.quality = in.qds;
        break;
    case Kind::Bitstring:
        o.bits = in.bsi;
        o.quality = in.qds;
        break;
    case Kind::Normalized:
        o.value = from_nva(in.nva);
        o.quality = in.qds;
        break;
    case Kind::NormalizedNoQds:
        o.value = from_nva(in.nva);
        break;
    case Kind::Scaled:
        o.value = in.sva;
        o.quality = in.qds;
        break;
    case Kind::ShortFloat:
        o.value = static_cast<double>(in.r32);
        o.quality = in.qds;
        break;
    case Kind::Totals:
        o.value = in.bcr.counter;
        o.qualifier = in.bcr.sequence;
        o.flags = static_cast<std::uint8_t>((in.bcr.carry ? IEC104_FLAG_CARRY : 0U) |
                                            (in.bcr.adjusted ? IEC104_FLAG_ADJUSTED : 0U));
        o.quality = in.bcr.invalid ? IEC104_QUALITY_INVALID : 0U;
        break;
    case Kind::EndOfInit:
        o.qualifier = in.coi.cause;
        o.flags = in.coi.parameters_changed ? IEC104_FLAG_PARAMETERS_CHANGED : 0U;
        break;
    default:
        break;
    }
}

/** @brief Control-direction kinds, see from_codec(). */
void control_from_codec(Kind kind, const codec::InformationObject& in, iec104_object& o) noexcept {
    switch (kind) {
    case Kind::SingleCommand:
        o.value = in.sco.on ? 1.0 : 0.0;
        o.qualifier = in.sco.qu;
        o.select = in.sco.select ? 1U : 0U;
        break;
    case Kind::DoubleCommand:
        o.value = in.dco.state;
        o.qualifier = in.dco.qu;
        o.select = in.dco.select ? 1U : 0U;
        break;
    case Kind::StepCommand:
        o.value = in.rco.state;
        o.qualifier = in.rco.qu;
        o.select = in.rco.select ? 1U : 0U;
        break;
    case Kind::SetNormalized:
        o.value = from_nva(in.nva);
        o.qualifier = in.qos.ql;
        o.select = in.qos.select ? 1U : 0U;
        break;
    case Kind::SetScaled:
        o.value = in.sva;
        o.qualifier = in.qos.ql;
        o.select = in.qos.select ? 1U : 0U;
        break;
    case Kind::SetFloat:
        o.value = static_cast<double>(in.r32);
        o.qualifier = in.qos.ql;
        o.select = in.qos.select ? 1U : 0U;
        break;
    case Kind::BitCommand:
        o.bits = in.bsi;
        break;
    case Kind::Interrogation:
        o.qualifier = in.qoi;
        break;
    case Kind::CounterInterrogation:
        o.qualifier = in.qcc.rqt;
        o.bits = in.qcc.frz;
        break;
    case Kind::Read:
        break;
    case Kind::ClockSync:
        o.time = from_cp56(in.cp56);
        break;
    case Kind::ResetProcess:
        o.qualifier = in.qrp;
        break;
    case Kind::TestWithTime:
        o.value = in.tsc;
        break;
    default:
        break;
    }
}

} // namespace

bool from_codec(std::uint8_t type, const codec::InformationObject& in,
                iec104_object& out) noexcept {
    const Kind kind = kind_of(type);
    if (kind == Kind::None) {
        return false;
    }
    iec104_object o{};
    o.type = type;
    o.ioa = in.ioa;
    if (is_monitor(kind)) {
        monitor_from_codec(kind, in, o);
    } else {
        control_from_codec(kind, in, o);
    }
    if (has_time_tag(type)) {
        o.time = from_cp56(in.cp56);
    }
    out = o;
    return true;
}

codec::AsduProfile to_profile(const iec104_link_config& c) noexcept {
    codec::AsduProfile p{};
    p.cot_length = c.cot_length;
    p.ca_length = c.ca_length;
    p.ioa_length = c.ioa_length;
    p.max_asdu_length = c.max_asdu_length;
    p.strict = c.tolerant == 0;
    return p;
}

iec104_asdu_info to_info(const codec::AsduHeader& h) noexcept {
    iec104_asdu_info i{};
    i.type = h.type_id;
    i.cause = h.cause;
    i.negative = h.negative ? 1U : 0U;
    i.test = h.test ? 1U : 0U;
    i.originator = h.originator;
    i.common_address = h.common_address;
    return i;
}

} // namespace iec104::api

// ---------------------------------------------------------------------------
// C API: time (the calendar arithmetic lives in the codec, cp56time2a.hpp)
// ---------------------------------------------------------------------------

extern "C" {

iec104_status iec104_time_from_unix_ms(int64_t unix_ms, iec104_time* out) {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    iec104::codec::Cp56Time2a t{};
    const iec104_status st = iec104::codec::from_unix_ms(unix_ms, t);
    if (st != IEC104_OK) {
        return st;
    }
    *out = iec104::api::from_cp56(t);
    return IEC104_OK;
}

int64_t iec104_time_to_unix_ms(const iec104_time* time) {
    if (time == nullptr || time->year < 2000U) {
        return 0;
    }
    iec104::codec::Cp56Time2a t{};
    t.year = static_cast<std::uint8_t>(time->year - 2000U);
    t.month = time->month;
    t.day_of_month = time->day;
    t.hour = time->hour;
    t.minute = time->minute;
    t.milliseconds = static_cast<std::uint16_t>((time->second * 1000U) + time->millisecond);
    return iec104::codec::to_unix_ms(t);
}

} // extern "C"
