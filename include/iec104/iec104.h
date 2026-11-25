/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: Apache-2.0
 * Author: Udo Schmitz (SOPTIM AG)
 */
/**
 * @file iec104.h
 * @brief Public C API of the IEC 60870-5-104 library.
 *
 * This is the only header a user of the library includes. It is plain C11 so
 * that the library can be used from C, C++ and any language with a C foreign
 * function interface (for example Rust via bindgen).
 *
 * Rules for this header (see docs/02_Konzept_IEC104_Library.md, "Sprachwahl"):
 *   - no C++ types, no STL types, no exceptions crossing this boundary;
 *   - every error is reported as an ::iec104_status return code;
 *   - every exported symbol carries ::IEC104_API.
 *
 * Status: skeleton. Only the version and status helpers exist so far.
 * Connection handles, configuration and callbacks follow together with the
 * apci, session and transport layers.
 */
#ifndef IEC104_IEC104_H
#define IEC104_IEC104_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Marks a function as part of the exported library interface.
 *
 * The library is built with hidden symbol visibility, so only functions that
 * carry this macro are visible to users of the shared library. Version 1
 * targets Linux only; the Windows branch is kept minimal on purpose.
 */
#ifdef _WIN32
#ifdef IEC104_BUILDING_LIBRARY
#define IEC104_API __declspec(dllexport)
#else
#define IEC104_API
#endif
#else
#define IEC104_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Major version of this header. Incompatible API changes increment it. */
#define IEC104_VERSION_MAJOR 0
/** @brief Minor version of this header. Compatible additions increment it. */
#define IEC104_VERSION_MINOR 1
/** @brief Patch version of this header. Fixes without API change increment it. */
#define IEC104_VERSION_PATCH 0

/**
 * @brief Default TCP port of an IEC 60870-5-104 connection without TLS.
 *
 * The controlled station (server) listens on this port, the controlling
 * station (client) connects to it. Source: docs/03, section on the APCI
 * defaults (port 2404).
 * TODO(norm): port definition, clause unknown.
 */
#define IEC104_DEFAULT_PORT 2404

/**
 * @brief Default TCP port of an IEC 60870-5-104 connection secured with TLS.
 *
 * IANA registers TCP 19998 as service "iec-104-sec". IEC 62351-3 leaves the
 * port to the application protocol. Source: docs/03, section "Sicherheit".
 */
#define IEC104_DEFAULT_TLS_PORT 19998

/**
 * @brief Result of every library function that can fail.
 *
 * 0 means success, negative values are errors. The values are part of the
 * stable API and never change; new codes are only ever added.
 */
/* C header: 'enum class' does not exist in C. */
/* NOLINTNEXTLINE(cppcoreguidelines-use-enum-class) */
typedef enum iec104_status {
    /** Success. */
    IEC104_OK = 0,
    /** A pointer argument is NULL or a parameter set (e.g. an ASDU profile) is invalid. */
    IEC104_ERR_INVALID_ARGUMENT = -1,
    /** The caller's output buffer cannot hold the result. Nothing usable was written. */
    IEC104_ERR_BUFFER_TOO_SMALL = -2,
    /** The input ends before the structure is complete. Read more bytes and retry. */
    IEC104_ERR_NEED_MORE_DATA = -3,
    /** The input violates the frame structure (wrong start octet, length, reserved bits). */
    IEC104_ERR_MALFORMED = -4,
    /** A value lies outside the range allowed by the standard or by the active profile. */
    IEC104_ERR_OUT_OF_RANGE = -5,
    /** The input is well formed but not supported (yet), e.g. a type rejected in strict mode. */
    IEC104_ERR_UNSUPPORTED = -6,
    /**
     * The call cannot be served now but may succeed later, e.g. sending while
     * k I-frames are unacknowledged or the output buffer is full. Retry after
     * the peer has acknowledged or the output has been taken.
     */
    IEC104_ERR_WOULD_BLOCK = -7,
    /** The call is not allowed in the current connection state, e.g. sending data before STARTDT.
     */
    IEC104_ERR_WRONG_STATE = -8,
    /** An internal invariant was violated. Indicates a bug in the library. */
    IEC104_ERR_INTERNAL = -99
} iec104_status;

/**
 * @brief Returns the library version as "major.minor.patch".
 *
 * The string is the version the library binary was built with. Compare it with
 * the IEC104_VERSION_* macros to detect a header/library mismatch.
 *
 * @return Pointer to a static, NUL-terminated string. Never NULL.
 */
IEC104_API const char* iec104_version_string(void);

/**
 * @brief Returns a short English description of a status code.
 *
 * Intended for log messages. The text is not part of the stable API and may
 * change between versions; compare status codes, not strings.
 *
 * @param status Any value, including values that are not a known status code.
 * @return Pointer to a static, NUL-terminated string. Never NULL. Unknown values
 *         yield "unknown status".
 */
IEC104_API const char* iec104_status_string(iec104_status status);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IEC104_IEC104_H */
