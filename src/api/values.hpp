/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file values.hpp
 * @brief Conversion between the C value types (iec104_object, iec104_time) and the codec.
 *
 * The C API describes an information object with one generic struct whose
 * fields are used per type (table at iec104_object in iec104.h). The codec
 * has one member per information element. This file maps between the two,
 * checks ranges, and converts time stamps.
 *
 * Layer: API (internal header).
 */
#pragma once

#include "codec/asdu.hpp"
#include "iec104/iec104.h"

#include <cstdint>

namespace iec104::api {

/** @brief True if the value API supports @p type (table at iec104_object). */
bool value_type_supported(std::uint8_t type) noexcept;

/**
 * @brief C object -> codec object, for the type in @p in.type.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for an unsupported type;
 *         IEC104_ERR_OUT_OF_RANGE for a value outside the range of the type.
 */
iec104_status to_codec(const iec104_object& in, codec::InformationObject& out) noexcept;

/**
 * @brief Codec object of type @p type -> C object.
 * @return false for a type the value API does not support.
 */
bool from_codec(std::uint8_t type, const codec::InformationObject& in, iec104_object& out) noexcept;

/** @brief iec104_time -> CP56Time2a. @return IEC104_ERR_OUT_OF_RANGE for invalid fields. */
iec104_status to_cp56(const iec104_time& in, codec::Cp56Time2a& out) noexcept;

/** @brief CP56Time2a -> iec104_time (year 2000 + yy). */
iec104_time from_cp56(const codec::Cp56Time2a& in) noexcept;

/** @brief ASDU profile from the C link configuration (strict = !tolerant). */
codec::AsduProfile to_profile(const iec104_link_config& c) noexcept;

/** @brief Data unit identifier -> C ASDU information. */
iec104_asdu_info to_info(const codec::AsduHeader& h) noexcept;

} // namespace iec104::api
