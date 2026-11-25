/*
 * iec104.h - public C API of the IEC 60870-5-104 library.
 *
 * Rules for this header (see docs/02_Konzept_IEC104_Library.md):
 *   - plain C11, usable from C, C++ and Rust (bindgen)
 *   - no STL types, no exceptions crossing this boundary
 *   - all errors are reported as iec104_status return codes
 *
 * Status: skeleton. Only version and status helpers exist so far.
 * Connection handles, configuration and callbacks follow with the
 * apci/session/transport layers.
 */
#ifndef IEC104_IEC104_H
#define IEC104_IEC104_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  ifdef IEC104_BUILDING_LIBRARY
#    define IEC104_API __declspec(dllexport)
#  else
#    define IEC104_API
#  endif
#else
#  define IEC104_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define IEC104_VERSION_MAJOR 0
#define IEC104_VERSION_MINOR 1
#define IEC104_VERSION_PATCH 0

/* Default TCP ports (IEC 60870-5-104, IANA "iec-104-sec" for TLS). */
#define IEC104_DEFAULT_PORT     2404
#define IEC104_DEFAULT_TLS_PORT 19998

/** Return codes. 0 is success, negative values are errors. */
/* C header: 'enum class' does not exist in C. */
/* NOLINTNEXTLINE(cppcoreguidelines-use-enum-class) */
typedef enum iec104_status {
    IEC104_OK                   = 0,
    IEC104_ERR_INVALID_ARGUMENT = -1, /* null pointer, invalid profile, ... */
    IEC104_ERR_BUFFER_TOO_SMALL = -2, /* output buffer cannot hold the result */
    IEC104_ERR_NEED_MORE_DATA   = -3, /* input incomplete, read more bytes */
    IEC104_ERR_MALFORMED        = -4, /* input violates the frame structure */
    IEC104_ERR_OUT_OF_RANGE     = -5, /* value outside the range allowed by the standard */
    IEC104_ERR_UNSUPPORTED      = -6, /* valid, but not supported (yet) */
    IEC104_ERR_INTERNAL         = -99
} iec104_status;

/** Library version as "major.minor.patch". Never NULL. */
IEC104_API const char* iec104_version_string(void);

/** Short English description of a status code. Never NULL. */
IEC104_API const char* iec104_status_string(iec104_status status);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IEC104_IEC104_H */
