/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file iec104.cpp
 * @brief Implementation of the general C API functions (version, status texts).
 *
 * Layer: API. This file only contains functions that do not belong to a
 * protocol layer. It must not throw and must not use RTTI, like the rest of the
 * library (built with -fno-exceptions -fno-rtti).
 */
#include "iec104/iec104.h"

/*
 * The build system passes the project version (CMakeLists.txt, project()) as
 * IEC104_VERSION_STRING. The fallback only exists so that the file still
 * compiles when built outside CMake, e.g. by an IDE indexer.
 */
#ifndef IEC104_VERSION_STRING
#define IEC104_VERSION_STRING "0.0.0"
#endif

extern "C" {

const char* iec104_version_string(void) {
    return IEC104_VERSION_STRING;
}

const char* iec104_status_string(iec104_status status) {
    // A switch without 'default' lets the compiler warn when a new status code
    // is added to the enum but not to this table.
    switch (status) {
    case IEC104_OK:
        return "ok";
    case IEC104_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case IEC104_ERR_BUFFER_TOO_SMALL:
        return "buffer too small";
    case IEC104_ERR_NEED_MORE_DATA:
        return "need more data";
    case IEC104_ERR_MALFORMED:
        return "malformed frame";
    case IEC104_ERR_OUT_OF_RANGE:
        return "value out of range";
    case IEC104_ERR_UNSUPPORTED:
        return "unsupported";
    case IEC104_ERR_WOULD_BLOCK:
        return "would block";
    case IEC104_ERR_WRONG_STATE:
        return "not allowed in this state";
    case IEC104_ERR_INTERNAL:
        return "internal error";
    }
    // Reached for integers that are not a declared enumerator (C callers can
    // pass any int).
    return "unknown status";
}

} // extern "C"
