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
 * Rules for this header (see docs/02_Technik_und_Quellen.md, "Sprache und Architektur"):
 *   - no C++ types, no STL types, no exceptions crossing this boundary;
 *   - every error is reported as an ::iec104_status return code;
 *   - every exported symbol carries ::IEC104_API.
 *
 * Contents:
 *   - version and status helpers;
 *   - connection parameters (::iec104_link_config) and callbacks;
 *   - server (controlled station) and client (controlling station) handles
 *     that move ASDUs over TCP.
 *
 * Data crosses this interface as values (::iec104_object, table of types
 * there) or, for every other type, as raw ASDU octets. The server answers
 * the requests of the standard by itself (interrogation, counter
 * interrogation, read, test) from its process image and passes commands
 * and clock synchronization to the application (on_command).
 *
 * Threading: a handle is used by one thread at a time. All work, including
 * the callbacks, happens inside the *_poll() call of that handle.
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
#define IEC104_VERSION_MINOR 9
/** @brief Patch version of this header. Fixes without API change increment it. */
#define IEC104_VERSION_PATCH 3

/**
 * @brief Default TCP port of an IEC 60870-5-104 connection without TLS.
 *
 * The controlled station (server) listens on this port, the controlling
 * station (client) connects to it. Source: docs/02_Technik_und_Quellen.md, section "APCI"
 * (port 2404).
 * TODO(norm): port definition, clause unknown.
 */
#define IEC104_DEFAULT_PORT 2404

/**
 * @brief Default TCP port of an IEC 60870-5-104 connection secured with TLS.
 *
 * IANA registers TCP 19998 as service "iec-104-sec". IEC 62351-3 leaves the
 * port to the application protocol. Source: docs/02_Technik_und_Quellen.md, section "Sicherheit".
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
    /** A system call on a socket failed (e.g. port in use, connection refused). */
    IEC104_ERR_IO = -9,
    /** The peer closed the connection, or it was closed locally. */
    IEC104_ERR_CLOSED = -10,
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

/* ------------------------------------------------------------------------- */
/* Connection parameters and events                                          */
/* ------------------------------------------------------------------------- */

/**
 * @brief Parameters of every connection of a server or client.
 *
 * Initialise with iec104_link_config_init(), which sets the values of the
 * standard, then change what the peer requires. Times are milliseconds.
 *
 * Ranges: t0..t2 1..255 s, t3 1 s .. 48 h or 0 = off (only with tolerant = 1),
 * k and w 1..32767. t2 < t1 and w <= 2/3 k are recommended; violations are
 * accepted and logged as warnings (iec104_server_set_log(),
 * iec104_client_set_log()). The field lengths of the ASDU are 2/2/3 in
 * IEC 104; other lengths are a deliberate setting for peers outside the
 * standard and are accepted also in strict mode.
 */
typedef struct iec104_link_config {
    uint32_t t0_ms; /**< Time-out of connection establishment, default 30000. */
    uint32_t t1_ms; /**< Time-out of send or test APDUs, default 15000. */
    uint32_t t2_ms; /**< Acknowledge at the latest after t2 without own data, default 10000. */
    uint32_t t3_ms; /**< Test frame after t3 idle time, default 20000. */
    uint16_t k;     /**< Max. unacknowledged sent I-frames, default 12. */
    uint16_t w;     /**< Acknowledge at the latest after w received I-frames, default 8. */
    int tolerant;   /**< 0 = strict IEC 104 behaviour (default), 1 = tolerate known deviations. */
    uint8_t cot_length;      /**< Octets of the cause of transmission, 1..2, default 2. */
    uint8_t ca_length;       /**< Octets of the common address, 1..2, default 2. */
    uint8_t ioa_length;      /**< Octets of the information object address, 1..3, default 3. */
    uint8_t max_asdu_length; /**< Largest ASDU this side sends, 1..249, default 249. */
    uint8_t originator;      /**< Originator address written into sent ASDUs, default 0. */
} iec104_link_config;

/** @brief Fills @p config with the default values of the standard. Does nothing for NULL. */
IEC104_API void iec104_link_config_init(iec104_link_config* config);

/** @brief State of one connection, as reported to the state callback. */
/* NOLINTNEXTLINE(cppcoreguidelines-use-enum-class) */
typedef enum iec104_link_state {
    IEC104_LINK_CONNECTING = 0,    /**< Client: TCP connect in progress. */
    IEC104_LINK_STOPPED = 1,       /**< Connected, data transfer stopped. */
    IEC104_LINK_START_PENDING = 2, /**< Client: STARTDT act sent. */
    IEC104_LINK_STARTED = 3,       /**< Data transfer started: ASDUs flow. */
    IEC104_LINK_STOP_PENDING = 4,  /**< STOPDT act sent or received, acknowledgements open. */
    IEC104_LINK_CLOSED = 5         /**< Closed; see ::iec104_close_reason. */
} iec104_link_state;

/** @brief Why a connection was closed. */
/* NOLINTNEXTLINE(cppcoreguidelines-use-enum-class) */
typedef enum iec104_close_reason {
    IEC104_CLOSE_NONE = 0,              /**< Not closed. */
    IEC104_CLOSE_LOCAL = 1,             /**< Closed by this side (close or destroy). */
    IEC104_CLOSE_PEER = 2,              /**< The peer closed the TCP connection. */
    IEC104_CLOSE_CONNECT_FAILED = 3,    /**< TCP connect refused or unreachable. */
    IEC104_CLOSE_T0_TIMEOUT = 4,        /**< TCP connect or TLS handshake not complete within t0. */
    IEC104_CLOSE_T1_TIMEOUT = 5,        /**< Frame not acknowledged/confirmed within t1. */
    IEC104_CLOSE_SEQUENCE_ERROR = 6,    /**< Received N(S) not as expected. */
    IEC104_CLOSE_ACK_OUT_OF_WINDOW = 7, /**< Received N(R) acknowledges frames never sent. */
    IEC104_CLOSE_MALFORMED = 8,         /**< The byte stream is not a valid APDU sequence. */
    IEC104_CLOSE_UNEXPECTED_FRAME = 9,  /**< Frame not allowed in the current state or role. */
    IEC104_CLOSE_SOCKET_ERROR = 10,     /**< A socket operation failed. */
    IEC104_CLOSE_TLS = 11               /**< TLS handshake failed or TLS error (see on_security). */
} iec104_close_reason;

/** @brief Short English name of a link state. Never NULL. */
IEC104_API const char* iec104_link_state_string(iec104_link_state state);
/** @brief Short English name of a close reason. Never NULL. */
IEC104_API const char* iec104_close_reason_string(iec104_close_reason reason);

/* ------------------------------------------------------------------------- */
/* TLS (IEC 62351-3)                                                         */
/* ------------------------------------------------------------------------- */

/** @brief Size of the path fields of ::iec104_tls_config, including the terminating NUL. */
#define IEC104_TLS_PATH_SIZE 512

/** @brief TLS protocol versions. */
/* NOLINTNEXTLINE(cppcoreguidelines-use-enum-class) */
typedef enum iec104_tls_version {
    IEC104_TLS_1_2 = 0x0303, /**< TLS 1.2 (RFC 5246). */
    IEC104_TLS_1_3 = 0x0304  /**< TLS 1.3 (RFC 8446). */
} iec104_tls_version;

/**
 * @brief TLS settings of a server or client.
 *
 * Strings are NUL-terminated; "" means "not set". Files are PEM. The
 * defaults (iec104_tls_config_init()) follow the TLS profile of IEC 62351-3
 * as far as it is known (docs/05_Normfragen_und_Festlegungen.md, N12):
 * mutual authentication, TLS 1.2 and 1.3, for TLS 1.2 only ECDHE suites
 * with AEAD.
 */
typedef struct iec104_tls_config {
    char ca_file[IEC104_TLS_PATH_SIZE];          /**< Trusted CA certificates; required. */
    char certificate_file[IEC104_TLS_PATH_SIZE]; /**< Own certificate, then intermediates. */
    char private_key_file[IEC104_TLS_PATH_SIZE]; /**< Private key of the own certificate. */
    char key_password[128];                      /**< Password of an encrypted key, "" = none. */
    char crl_file[IEC104_TLS_PATH_SIZE];         /**< Revocation lists, "" = no CRL check. */
    /** Expected DNS name or IP address in the peer certificate, "" = no name check. */
    char peer_name[256];
    /** Expected SHA-256 fingerprint of the peer certificate (hex, colons optional), "" = any. */
    char peer_fingerprint[128];
    char cipher_list[512];   /**< TLS 1.2 suites (OpenSSL syntax), "" = default. */
    char cipher_suites[256]; /**< TLS 1.3 suites (OpenSSL syntax), "" = default. */
    uint16_t min_version;    /**< ::iec104_tls_version; default 1.2. */
    uint16_t max_version;    /**< ::iec104_tls_version; default 1.3. */
    /**
     * 1 (default): the peer must present a valid certificate (server: client
     * certificate required). 0 only for tests; IEC 62351-3 requires mutual
     * authentication.
     */
    int verify_peer;
    /** 1 (default): TLS 1.2 renegotiation allowed in both directions (always RFC 5746). */
    int renegotiation;
    /** Key renewal by this side every n seconds: key update (1.3), renegotiation (1.2); 0 = off. */
    uint32_t renewal_interval_s;
    int session_resumption;      /**< 1 (default): sessions can be resumed. */
    uint32_t session_lifetime_s; /**< Lifetime of a resumable session; default 3600. */
} iec104_tls_config;

/** @brief Defaults (see ::iec104_tls_config); all files empty. Does nothing for NULL. */
IEC104_API void iec104_tls_config_init(iec104_tls_config* config);

/** @brief 1 if the library was built with TLS support, else 0. */
IEC104_API int iec104_tls_available(void);

/** @brief Security events of TLS connections (see on_security). */
/* NOLINTNEXTLINE(cppcoreguidelines-use-enum-class) */
typedef enum iec104_security_event {
    /** Handshake complete; detail: version, suite, peer subject, "resumed". */
    IEC104_SECURITY_TLS_ESTABLISHED = 0,
    /** Handshake failed or not complete within t0; detail: reason. */
    IEC104_SECURITY_HANDSHAKE_FAILED = 1,
    /** Peer certificate rejected (untrusted, expired, revoked, name, fingerprint); detail: reason.
     */
    IEC104_SECURITY_CERTIFICATE_REJECTED = 2,
    /** TLS error on an open connection, e.g. an alert of the peer; detail: reason. */
    IEC104_SECURITY_TLS_ERROR = 3,
    /** Key renewal: this side started one ("key update", "renegotiation"), or a TLS 1.2
     *  renegotiation by either side completed ("renegotiation complete"). */
    IEC104_SECURITY_KEY_RENEWAL = 4
} iec104_security_event;

/** @brief Short English name of a security event. Never NULL. */
IEC104_API const char* iec104_security_event_string(iec104_security_event event);

/** @brief Details of an open TLS connection. */
typedef struct iec104_tls_info {
    char version[16];          /**< e.g. "TLSv1.3". */
    char cipher[64];           /**< e.g. "TLS_AES_128_GCM_SHA256". */
    char peer_subject[256];    /**< Subject of the peer certificate (RFC 2253), "" if none. */
    char peer_fingerprint[96]; /**< SHA-256 of the peer certificate, "AB:CD:...". */
    int resumed;               /**< 1 if an earlier session was resumed. */
} iec104_tls_info;

/* ------------------------------------------------------------------------- */
/* Values: time, information objects, ASDU information                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief Date and time as carried by CP56Time2a (7 octets).
 *
 * The standard does not say whether the time is UTC or local time; that is
 * agreed with the peer (interoperability list). This struct carries no time
 * zone. CP56Time2a transmits the year as 0..99; the library maps it to
 * 2000..2099.
 */
typedef struct iec104_time {
    uint16_t year;        /**< 2000..2099. */
    uint8_t month;        /**< 1..12. */
    uint8_t day;          /**< 1..31. */
    uint8_t hour;         /**< 0..23. */
    uint8_t minute;       /**< 0..59. */
    uint8_t second;       /**< 0..59. */
    uint16_t millisecond; /**< 0..999. */
    uint8_t day_of_week;  /**< 1..7, 0 = not used. */
    uint8_t invalid;      /**< 1 = IV, the time is not valid. */
    uint8_t summer_time;  /**< 1 = SU, summer (daylight saving) time. */
} iec104_time;

/**
 * @brief Converts milliseconds since 1970-01-01 00:00 UTC into an iec104_time (UTC).
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL; IEC104_ERR_OUT_OF_RANGE
 *         outside 2000..2099.
 */
IEC104_API iec104_status iec104_time_from_unix_ms(int64_t unix_ms, iec104_time* out);

/** @brief Converts an iec104_time taken as UTC into milliseconds since 1970.
 *  0 for NULL or a year before 2000. */
IEC104_API int64_t iec104_time_to_unix_ms(const iec104_time* time);

/** @brief Quality bits (QDS and the quality part of SIQ/DIQ). */
#define IEC104_QUALITY_OVERFLOW 0x01U    /**< OV: value beyond the range of the representation. */
#define IEC104_QUALITY_BLOCKED 0x10U     /**< BL: blocked for transmission. */
#define IEC104_QUALITY_SUBSTITUTED 0x20U /**< SB: substituted (manually or by another source). */
#define IEC104_QUALITY_NOT_TOPICAL 0x40U /**< NT: not updated in time. */
#define IEC104_QUALITY_INVALID 0x80U     /**< IV: not valid. */

/** @brief Flags in iec104_object::flags (meaning depends on the type, see there). */
#define IEC104_FLAG_TRANSIENT 0x01U /**< Step position (5, 32): equipment is moving. */
#define IEC104_FLAG_CARRY 0x01U     /**< Integrated totals (15, 37): counter overflow (CY). */
#define IEC104_FLAG_ADJUSTED 0x02U  /**< Integrated totals (15, 37): counter adjusted (CA). */
#define IEC104_FLAG_PARAMETERS_CHANGED                                                             \
    0x01U /**< End of initialization (70): after parameter change. */

/* The table is kept out of automatic reformatting: re-wrapping destroys it. */
/* clang-format off */
/**
 * @brief One information object as a value.
 *
 * Which fields are used depends on iec104_object::type:
 *
 * | type                       | value                  | other fields                                      |
 * |----------------------------|------------------------|---------------------------------------------------|
 * | 1, 30 single point         | 0 off, 1 on            | quality                                           |
 * | 3, 31 double point         | 0..3 (1 off, 2 on)     | quality                                           |
 * | 5, 32 step position        | -64..63                | quality, flags TRANSIENT                          |
 * | 7, 33 bitstring            | -                      | bits, quality                                     |
 * | 9, 34 normalized           | -1.0 .. 1.0 - 2^-15    | quality                                           |
 * | 11, 35 scaled              | -32768..32767          | quality                                           |
 * | 13, 36 short float         | float                  | quality                                           |
 * | 15, 37 integrated totals   | counter, 32 bit signed | qualifier = sequence 0..31, flags CARRY/ADJUSTED, quality IV |
 * | 21 normalized, no quality  | -1.0 .. 1.0 - 2^-15    | -                                                 |
 * | 45, 58 single command      | 0 off, 1 on            | qualifier = QU 0..31, select                      |
 * | 46, 59 double command      | 1 off, 2 on            | qualifier = QU, select                            |
 * | 47, 60 regulating step     | 1 lower, 2 higher      | qualifier = QU, select                            |
 * | 48, 61 set point normalized| -1.0 .. 1.0 - 2^-15    | qualifier = QL 0..127, select                     |
 * | 49, 62 set point scaled    | -32768..32767          | qualifier = QL, select                            |
 * | 50, 63 set point float     | float                  | qualifier = QL, select                            |
 * | 51, 64 bitstring command   | -                      | bits                                              |
 * | 70 end of initialization   | -                      | qualifier = cause 0..127, flags                   |
 * | 100 interrogation          | -                      | qualifier = QOI (20 station, 21..36 group 1..16)  |
 * | 101 counter interrogation  | -                      | qualifier = RQT (1..4 group, 5 all), bits = FRZ 0..3 |
 * | 102 read                   | -                      | - (sent with cause 5, see iec104_client_read())   |
 * | 103 clock synchronization  | -                      | time                                              |
 * | 105 reset process          | -                      | qualifier = QRP                                   |
 * | 107 test with time tag     | test sequence counter  | time                                              |
 *
 * Types 30..37, 58..64 and 107 carry iec104_object::time as time tag. Other
 * types (e.g. CP24Time2a types, parameters, protection events) are available
 * as raw ASDUs (on_asdu, *_send_asdu).
 */
/* clang-format on */
typedef struct iec104_object {
    uint8_t type;      /**< Type identification, e.g. 13 = M_ME_NC_1. */
    uint32_t ioa;      /**< Information object address, 0..16777215. */
    double value;      /**< Value, see table. */
    uint32_t bits;     /**< Bitstring (7, 33, 51, 64) or FRZ (101). */
    uint8_t quality;   /**< IEC104_QUALITY_* bits. */
    uint8_t flags;     /**< IEC104_FLAG_* bits. */
    uint8_t qualifier; /**< QU, QL, QOI, RQT, QRP, sequence number or cause, see table. */
    uint8_t select;    /**< Commands: 1 = select, 0 = execute. */
    iec104_time time;  /**< Time tag or clock value, see table. */
} iec104_object;

/** @brief Causes of transmission used by the library (bits 0..5 of the COT). */
/* NOLINTNEXTLINE(cppcoreguidelines-use-enum-class) */
typedef enum iec104_cause {
    IEC104_COT_PERIODIC = 1,
    IEC104_COT_BACKGROUND = 2,
    IEC104_COT_SPONTANEOUS = 3,
    IEC104_COT_INITIALIZED = 4,
    IEC104_COT_REQUEST = 5,
    IEC104_COT_ACTIVATION = 6,
    IEC104_COT_ACTIVATION_CON = 7,
    IEC104_COT_DEACTIVATION = 8,
    IEC104_COT_DEACTIVATION_CON = 9,
    IEC104_COT_ACTIVATION_TERMINATION = 10,
    IEC104_COT_INTERROGATED_STATION = 20, /**< 21..36 = interrogated by group 1..16. */
    IEC104_COT_REQUESTED_COUNTERS = 37,   /**< 38..41 = counter group 1..4. */
    IEC104_COT_UNKNOWN_TYPE = 44,
    IEC104_COT_UNKNOWN_CAUSE = 45,
    IEC104_COT_UNKNOWN_COMMON_ADDRESS = 46,
    IEC104_COT_UNKNOWN_IOA = 47
} iec104_cause;

/** @brief The data unit identifier of a received ASDU. */
typedef struct iec104_asdu_info {
    uint8_t type;            /**< Type identification. */
    uint8_t cause;           /**< Cause of transmission, 0..63 (see ::iec104_cause). */
    uint8_t negative;        /**< 1 = negative confirmation (P/N bit). */
    uint8_t test;            /**< 1 = test (T bit). */
    uint8_t originator;      /**< Originator address. */
    uint16_t common_address; /**< Common address (station). */
} iec104_asdu_info;

/**
 * @brief Name of a type identification as in the standard, e.g. "M_SP_NA_1" for 1.
 *
 * Covers the types of the library's type table (see the interoperability
 * list). Intended for logs and test tools.
 *
 * @return Pointer to a static, NUL-terminated string. Never NULL. Types
 *         outside the table (unknown, private) yield "unknown type".
 */
IEC104_API const char* iec104_type_name(uint8_t type);

/**
 * @brief Decodes a complete ASDU into its data unit identifier and objects.
 *
 * For applications and test tools that see raw ASDUs, e.g. in on_asdu or in
 * a frame trace (the octets behind the 6 octets of the APCI). The decoding is
 * the same the library uses for its own callbacks.
 *
 * @param config   Field lengths and strict/tolerant handling; NULL = values of
 *                 the standard (iec104_link_config_init()).
 * @param asdu     The ASDU octets.
 * @param length   Number of octets at @p asdu.
 * @param info     Receives the data unit identifier. Filled whenever the
 *                 ASDU itself is well formed, also for IEC104_ERR_UNSUPPORTED.
 * @param objects  Caller's array for the objects; may be NULL if @p capacity is 0.
 * @param capacity Number of entries in @p objects. 127 is always enough.
 * @param count    Receives the number of objects in the ASDU (also for
 *                 IEC104_ERR_UNSUPPORTED, then without writing @p objects).
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL pointers or an
 *         invalid @p config; IEC104_ERR_MALFORMED if the octets do not form
 *         an ASDU (length does not match the objects, header too short);
 *         IEC104_ERR_UNSUPPORTED if the type is not in the table or has no
 *         mapping to ::iec104_object (see the table there);
 *         IEC104_ERR_BUFFER_TOO_SMALL if @p capacity is less than the number
 *         of objects.
 */
IEC104_API iec104_status iec104_decode_asdu(const iec104_link_config* config, const uint8_t* asdu,
                                            size_t length, iec104_asdu_info* info,
                                            iec104_object* objects, size_t capacity, size_t* count);

/** @brief Final result of a command sent by the client (see on_command_result). */
/* NOLINTNEXTLINE(cppcoreguidelines-use-enum-class) */
typedef enum iec104_command_result {
    IEC104_COMMAND_CONFIRMED = 0,       /**< Positive confirmation; no termination expected. */
    IEC104_COMMAND_TERMINATED = 1,      /**< Positive confirmation and termination: executed. */
    IEC104_COMMAND_SELECTED = 2,        /**< Select confirmed positively. */
    IEC104_COMMAND_REJECTED = 3,        /**< Negative answer (also causes 44..47). */
    IEC104_COMMAND_NO_CONFIRMATION = 4, /**< No confirmation within the confirmation time. */
    IEC104_COMMAND_NO_TERMINATION = 5,  /**< Confirmed, but no termination in time. */
    IEC104_COMMAND_CONNECTION_LOST = 6  /**< Connection closed before the result was known. */
} iec104_command_result;

/** @brief Short English name of a command result. Never NULL. */
IEC104_API const char* iec104_command_result_string(iec104_command_result result);

/**
 * @brief Callbacks of a server or client. Every pointer may be NULL.
 *
 * They are called from inside iec104_server_poll() / iec104_client_poll().
 * Calling the send, start and stop functions of the same handle from a
 * callback is allowed; destroying the handle is not.
 */
typedef struct iec104_callbacks {
    void* user; /**< Passed unchanged to every callback. */
    /**
     * @brief An ASDU was received.
     * @param connection Id of the connection (client: always 1).
     * @param asdu       The ASDU octets, valid only during the call.
     * @param length     1..249.
     */
    void (*on_asdu)(void* user, uint32_t connection, const uint8_t* asdu, size_t length);
    /**
     * @brief The state of a connection changed.
     * @param reason IEC104_CLOSE_NONE unless @p state is IEC104_LINK_CLOSED.
     */
    void (*on_state)(void* user, uint32_t connection, iec104_link_state state,
                     iec104_close_reason reason);
    /**
     * @brief An information object was received (types of the table at ::iec104_object).
     *
     * Called once per object, after on_asdu for the same ASDU. On the client
     * this includes the confirmations of own requests (cause 7, 10, negative).
     * On the server it is called for data in monitor direction only; requests
     * are answered by the library and commands go to on_command.
     */
    void (*on_object)(void* user, uint32_t connection, const iec104_asdu_info* info,
                      const iec104_object* object);
    /**
     * @brief Server only: a command or clock synchronization arrived (types 45..51, 58..64,
     *        103, 105).
     *
     * The library answers with a positive confirmation (cause 7) if the
     * callback returns 1, with a negative one if it returns 0 or the callback
     * is NULL, and with an activation termination (cause 10) after a positive
     * confirmation of a command (see iec104_server_options).
     */
    int (*on_command)(void* user, uint32_t connection, const iec104_asdu_info* info,
                      const iec104_object* command);
    /**
     * @brief Client only: a command sent with iec104_client_send_command() or
     *        iec104_client_select_execute() has its final result.
     * @param command The command as sent (for select_execute: the execute).
     */
    void (*on_command_result)(void* user, uint16_t common_address, const iec104_object* command,
                              iec104_command_result result);
    /**
     * @brief A security event of a TLS connection (see ::iec104_security_event).
     *
     * Failures are followed by the close of the connection with
     * IEC104_CLOSE_TLS or IEC104_CLOSE_T0_TIMEOUT.
     * @param detail Text, valid only during the call. Never NULL.
     */
    void (*on_security)(void* user, uint32_t connection, iec104_security_event event,
                        const char* detail);
} iec104_callbacks;

/* ------------------------------------------------------------------------- */
/* Logging and statistics                                                    */
/* ------------------------------------------------------------------------- */

/** @brief Log levels, from most to least important. */
/* NOLINTNEXTLINE(cppcoreguidelines-use-enum-class) */
typedef enum iec104_log_level {
    IEC104_LOG_ERROR = 0,   /**< Connection lost because of an error, protocol violation. */
    IEC104_LOG_WARNING = 1, /**< Unusual: negative answers, discarded commands, config warnings. */
    IEC104_LOG_INFO = 2,    /**< Connections, start/stop, command results. */
    IEC104_LOG_DEBUG = 3,   /**< Every state change, received requests. */
    IEC104_LOG_TRACE = 4    /**< Every APDU as hex dump. */
} iec104_log_level;

/**
 * @brief Receives one log message (English, one line, without newline).
 * @param user The pointer given to iec104_server_set_log() / iec104_client_set_log().
 * @param level Level of the message.
 * @param connection Connection id, 0 for messages that concern no connection.
 * @param message The text, valid only during the call.
 */
typedef void (*iec104_log_fn)(void* user, iec104_log_level level, uint32_t connection,
                              const char* message);

/** @brief Short name of a log level ("error", "warning", ...). Never NULL. */
IEC104_API const char* iec104_log_level_string(iec104_log_level level);

/** @brief Counters of one connection since it was opened. */
typedef struct iec104_link_stats {
    uint64_t i_frames_sent;     /**< I-frames (ASDUs) sent. */
    uint64_t i_frames_received; /**< I-frames (ASDUs) received. */
    uint64_t s_frames_sent;     /**< S-frames sent. */
    uint64_t s_frames_received; /**< S-frames received. */
    uint64_t u_frames_sent;     /**< U-frames (STARTDT, STOPDT, TESTFR) sent. */
    uint64_t u_frames_received; /**< U-frames received. */
    uint64_t bytes_sent;        /**< Octets of all sent APDUs. */
    uint64_t bytes_received;    /**< Octets of all received APDUs. */
    uint64_t requests;          /**< Server: interrogations, reads, tests answered. Client: 0. */
    uint64_t
        commands_accepted; /**< Server: accepted by on_command. Client: confirmed or executed. */
    uint64_t commands_rejected;  /**< Server: rejected by on_command. Client: negative answer. */
    uint64_t commands_discarded; /**< Server: older than the maximum command delay.
                                      Client: no confirmation or termination in time. */
    uint64_t negative_answers;   /**< Server: sent. Client: received. */
    uint32_t send_queue;         /**< Server: ASDUs waiting for the window. Client: 0. */
} iec104_link_stats;

/* ------------------------------------------------------------------------- */
/* Server (controlled station)                                               */
/* ------------------------------------------------------------------------- */

/** @brief Opaque server handle. */
typedef struct iec104_server iec104_server;

/**
 * @brief Creates a server. It does not listen yet.
 *
 * @param config    Connection parameters (copied). NULL = defaults.
 * @param callbacks Callbacks (copied). NULL = none.
 * @param out       Receives the handle.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT if @p out is NULL;
 *         IEC104_ERR_OUT_OF_RANGE or IEC104_ERR_INVALID_ARGUMENT if the
 *         configuration is invalid.
 */
IEC104_API iec104_status iec104_server_create(const iec104_link_config* config,
                                              const iec104_callbacks* callbacks,
                                              iec104_server** out);

/**
 * @brief Starts listening.
 *
 * @param server The server.
 * @param address Local address to bind to; NULL or "" = all addresses.
 * @param port    TCP port; IEC104_DEFAULT_PORT is the standard, 0 = any free port.
 * @return IEC104_OK; IEC104_ERR_WRONG_STATE if already listening;
 *         IEC104_ERR_INVALID_ARGUMENT if the address is unknown;
 *         IEC104_ERR_IO if the port cannot be bound (e.g. in use).
 */
IEC104_API iec104_status iec104_server_listen(iec104_server* server, const char* address,
                                              uint16_t port);

/** @brief The port the server listens on (useful after port 0), 0 if not listening. */
IEC104_API uint16_t iec104_server_port(const iec104_server* server);

/**
 * @brief Does all pending work: accept, receive, timers, send, callbacks.
 *
 * @param server The server.
 * @param timeout_ms Maximum time to wait for activity; 0 = do not wait.
 *                   The call returns earlier when a timer is due.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL; IEC104_ERR_IO if poll() fails.
 */
IEC104_API iec104_status iec104_server_poll(iec104_server* server, int timeout_ms);

/**
 * @brief Sends an ASDU on one connection, or on all started connections.
 *
 * @param server The server.
 * @param asdu Complete ASDU (data unit identifier and objects), encoded by the caller.
 * @param length Octets of @p asdu, 1..249.
 * @param connection Connection id from a callback, or 0 = all started connections.
 * @return IEC104_OK (for 0: sent on at least one connection);
 *         IEC104_ERR_INVALID_ARGUMENT for NULL or a bad length;
 *         IEC104_ERR_WRONG_STATE if the connection is unknown or not started
 *         (for 0: no started connection); IEC104_ERR_WOULD_BLOCK if the window k
 *         is full (for 0: on at least one connection); poll and retry.
 */
IEC104_API iec104_status iec104_server_send_asdu(iec104_server* server, uint32_t connection,
                                                 const uint8_t* asdu, size_t length);

/** @brief Number of open connections. */
IEC104_API size_t iec104_server_connection_count(const iec104_server* server);

/** @brief What happens when an event arrives at a full event buffer. */
/* NOLINTNEXTLINE(cppcoreguidelines-use-enum-class) */
typedef enum iec104_overflow {
    IEC104_OVERFLOW_DROP_OLDEST = 0, /**< The oldest event is dropped (default). */
    IEC104_OVERFLOW_REJECT = 1       /**< The new event is rejected: IEC104_ERR_WOULD_BLOCK. */
} iec104_overflow;

/** @brief Behaviour of the server's application layer (iec104_server_set_options()). */
typedef struct iec104_server_options {
    /** 1 (default) = send end of initialization (type 70, cause 4) on the first STARTDT
     *  of every connection after the server was created. */
    int send_end_of_init;
    /** 1 (default) = send activation termination (cause 10) after an accepted command. */
    int command_termination;
    /** Cause of initialization sent with type 70, default 0 (local power switch on). */
    uint8_t cause_of_init;
    /** Maximum age in ms of a command with time tag (58..64, 107) against the server's
     *  UTC clock; older commands are discarded without answer. 0 (default) = no check. */
    uint32_t max_command_delay_ms;
    /** Event buffer of a connection without configured redundancy groups (see
     *  iec104_server_add_group()): events while the connection is stopped, and
     *  sent but unacknowledged ones. Default 1000; set before listen. */
    uint32_t event_buffer_size;
    /** Behaviour of that buffer when it is full; default IEC104_OVERFLOW_DROP_OLDEST. */
    iec104_overflow overflow;
} iec104_server_options;

/** @brief Fills @p options with the defaults. Does nothing for NULL. */
IEC104_API void iec104_server_options_init(iec104_server_options* options);

/**
 * @brief Applies options.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL; IEC104_ERR_OUT_OF_RANGE
 *         for cause_of_init above 127 or an event buffer size outside
 *         1..IEC104_MAX_EVENT_BUFFER. On error nothing is changed.
 */
IEC104_API iec104_status iec104_server_set_options(iec104_server* server,
                                                   const iec104_server_options* options);

/* Redundancy groups (edition 2, clause 10) -------------------------------- */

/** @brief Most addresses of controlling stations in one group. */
#define IEC104_GROUP_MAX_PEERS 8
/** @brief Size of an address text in ::iec104_group_config, including the NUL. */
#define IEC104_ADDRESS_SIZE 64
/** @brief Largest event buffer of a group. */
#define IEC104_MAX_EVENT_BUFFER 1000000

/**
 * @brief Settings of a redundancy group.
 *
 * The connections of one controlling station form a group. Only one of them
 * transfers data (the one started last with STARTDT); the others stand by in
 * STOPDT. Spontaneous data goes into the group's event buffer and stays
 * there until the active connection has acknowledged it. After a break or a
 * switch-over, unacknowledged events are sent again on the next active
 * connection (they may arrive twice, but are not lost).
 */
typedef struct iec104_group_config {
    /** IP addresses of the controlling station ("192.0.2.10", "2001:db8::1");
     *  all empty = any address (only one such group is used). */
    /* NOLINTNEXTLINE(*-avoid-c-arrays): C header */
    char peers[IEC104_GROUP_MAX_PEERS][IEC104_ADDRESS_SIZE];
    uint32_t buffer_size;     /**< Events in the buffer, default 1000. */
    iec104_overflow overflow; /**< Default IEC104_OVERFLOW_DROP_OLDEST. */
} iec104_group_config;

/** @brief Defaults: any address, 1000 events, drop oldest. Does nothing for NULL. */
IEC104_API void iec104_group_config_init(iec104_group_config* config);

/**
 * @brief Adds a redundancy group (before iec104_server_listen()).
 *
 * With groups, a new connection joins the group that lists its address, or
 * else the group open to any address; without such a group it is rejected
 * at once (logged as warning). Without any group, every connection is a
 * group of its own (buffer from ::iec104_server_options).
 *
 * @param server The server.
 * @param config Settings of the group.
 * @param group Receives the id of the group (1, 2, ...); may be NULL.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL or an address
 *         without NUL; IEC104_ERR_OUT_OF_RANGE for a buffer size outside
 *         1..IEC104_MAX_EVENT_BUFFER; IEC104_ERR_WRONG_STATE after listen.
 */
IEC104_API iec104_status iec104_server_add_group(iec104_server* server,
                                                 const iec104_group_config* config,
                                                 uint32_t* group);

/** @brief State and counters of a redundancy group. */
typedef struct iec104_group_stats {
    uint32_t active_connection; /**< Connection that transfers data, 0 = none. */
    uint32_t connections;       /**< Connections in the group. */
    uint32_t buffered;          /**< Events in the buffer. */
    uint32_t in_flight;         /**< ... of them sent, not yet acknowledged. */
    uint64_t events;            /**< Events put into the buffer. */
    uint64_t dropped;           /**< Events dropped because the buffer was full. */
    uint64_t rejected;          /**< Events rejected because the buffer was full. */
    uint64_t resent;            /**< Sent events queued again after a stop or break. */
    uint64_t switchovers;       /**< Changes of the active connection to another one. */
} iec104_group_stats;

/**
 * @brief Counters of a group.
 * @return IEC104_OK; IEC104_ERR_WRONG_STATE if the group does not exist (a
 *         connection's own group ends with the connection).
 */
IEC104_API iec104_status iec104_server_get_group_stats(const iec104_server* server, uint32_t group,
                                                       iec104_group_stats* out);

/** @brief Group of a connection, 0 if the connection does not exist. */
IEC104_API uint32_t iec104_server_connection_group(const iec104_server* server,
                                                   uint32_t connection);

/**
 * @brief Adds a data point to the server's process image (or replaces it).
 *
 * The image answers interrogation (type 100) and counter interrogation
 * (type 101) and read commands (type 102). A point is identified by common
 * address and IOA. Points with a time tag type (30..37) are stored as their
 * type without time tag and answered as such in an interrogation.
 *
 * @param server The server.
 * @param common_address Station address, 1..65534 (the common address must be known
 *                       to the server; unknown addresses are answered with cause 46).
 * @param point          Initial value; type 1, 3, 5, 7, 9, 11, 13, 15, 21 or 30..37.
 * @param groups         Interrogation groups the point belongs to: bit 0 = group 1 ..
 *                       bit 15 = group 16; for counters bits 0..3 = counter group 1..4.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL or an unsupported type;
 *         IEC104_ERR_OUT_OF_RANGE for a value outside its range.
 */
IEC104_API iec104_status iec104_server_add_point(iec104_server* server, uint16_t common_address,
                                                 const iec104_object* point, uint16_t groups);

/** @brief How a command point is operated. */
/* NOLINTNEXTLINE(cppcoreguidelines-use-enum-class) */
typedef enum iec104_command_model {
    IEC104_COMMAND_DIRECT = 0,        /**< Execute directly; a select is rejected. */
    IEC104_COMMAND_SELECT_EXECUTE = 1 /**< Select first, then execute the same command. */
} iec104_command_model;

/**
 * @brief Defines a command point (or replaces it).
 *
 * Without any command point the application decides about every command in
 * on_command. As soon as one point exists, commands are checked against the
 * points first:
 *   - unknown IOA: negative answer with cause 47;
 *   - other type than defined (e.g. 45 for a point defined as 58): negative ACTCON;
 *   - direct point: a select is rejected;
 *   - select-execute point: an execute is only passed to on_command if the
 *     same command (state and qualifier) was selected on the same connection
 *     less than @p select_timeout_ms ago; a deactivation (cause 8) cancels
 *     the selection.
 *
 * Stations: a common address is known if it has data points or command
 * points. Requests to other addresses are answered with cause 46. Only a
 * server without any point accepts every address (the application manages
 * its stations). Known stations get the end of initialization and answer a
 * broadcast interrogation.
 *
 * @param server The server.
 * @param common_address Station of the command point.
 * @param ioa Information object address of the command point.
 * @param model Direct execute or select before operate.
 * @param select_timeout_ms Validity of a selection (select before operate), ms.
 * @param type Command type 45..51 or 58..64 the point accepts.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL, a type that is not a
 *         command, or select-execute for a bitstring command (51, 64).
 */
IEC104_API iec104_status iec104_server_add_command(iec104_server* server, uint16_t common_address,
                                                   uint32_t ioa, uint8_t type,
                                                   iec104_command_model model,
                                                   uint32_t select_timeout_ms);

/**
 * @brief Updates a point of the image and sends it spontaneously (cause 3).
 *
 * The object may carry the time tag variant of the point's type (e.g. 36
 * for a point added as 13); it is sent as given, the image keeps the value.
 * The event goes into the buffer of every redundancy group (see
 * iec104_server_add_group()) and stays there until the active connection
 * has acknowledged it, also while no connection of the group is started.
 * Without any group (no iec104_server_add_group() and no open connection)
 * the event is not buffered; only the image keeps the value.
 *
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT if the point is unknown or
 *         the type does not fit; IEC104_ERR_OUT_OF_RANGE for a bad value;
 *         IEC104_ERR_WOULD_BLOCK if a group buffer is full and set to
 *         IEC104_OVERFLOW_REJECT.
 */
IEC104_API iec104_status iec104_server_update(iec104_server* server, uint16_t common_address,
                                              const iec104_object* object);

/**
 * @brief Sends objects of one type with a chosen cause (e.g. periodic, cause 1).
 *
 * Long lists are split into several ASDUs. Objects are not stored in the image.
 *
 * | connection | cause | goes to |
 * |---|---|---|
 * | id | any | that connection, if started |
 * | 0 | 1 periodic, 2 background | the active connection of every group, not buffered |
 * | 0 | other | the buffer of every group, like iec104_server_update() |
 *
 * @param server The server.
 * @param common_address Station of the objects.
 * @param cause Cause of transmission, e.g. IEC104_COT_PERIODIC.
 * @param objects Objects of one type.
 * @param count Number of @p objects.
 * @param connection Connection id, or 0 = all (see table).
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL, count 0, mixed or
 *         unsupported types; IEC104_ERR_OUT_OF_RANGE for a bad value;
 *         IEC104_ERR_WRONG_STATE if the connection is not started, or for
 *         cause 1/2 if no connection is active (also if there is no group at
 *         all); IEC104_ERR_WOULD_BLOCK if the send queue of the connection is
 *         full, or a group buffer is full and set to IEC104_OVERFLOW_REJECT.
 */
IEC104_API iec104_status iec104_server_send(iec104_server* server, uint32_t connection,
                                            uint16_t common_address, uint8_t cause,
                                            const iec104_object* objects, size_t count);

/**
 * @brief Sets the log callback; messages up to @p max_level are passed on.
 *
 * Configuration warnings (t2 >= t1, w > 2/3 k) are reported at once.
 * @p fn NULL switches logging off.
 */
IEC104_API void iec104_server_set_log(iec104_server* server, iec104_log_level max_level,
                                      iec104_log_fn fn, void* user);

/**
 * @brief Counters of one connection.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL; IEC104_ERR_WRONG_STATE
 *         if the connection does not exist (any more).
 */
IEC104_API iec104_status iec104_server_get_stats(const iec104_server* server, uint32_t connection,
                                                 iec104_link_stats* out);

/**
 * @brief Switches TLS on for connections accepted from now on.
 *
 * Loads CA, certificate, key and CRL at once. Can be called again at any
 * time, e.g. to change the certificate: open connections keep their TLS
 * session, new ones use the new settings. NULL switches TLS off for new
 * connections. Errors are also logged with their reason.
 *
 * @return IEC104_OK; IEC104_ERR_IO if a file cannot be read;
 *         IEC104_ERR_INVALID_ARGUMENT for inconsistent settings (key does not
 *         match, no usable suite, no certificate); IEC104_ERR_UNSUPPORTED
 *         without TLS support. On error the previous settings stay.
 */
IEC104_API iec104_status iec104_server_set_tls(iec104_server* server,
                                               const iec104_tls_config* config);

/**
 * @brief TLS details of an open connection.
 * @return IEC104_OK; IEC104_ERR_WRONG_STATE if the connection does not
 *         exist, is not TLS or the handshake is not complete.
 */
IEC104_API iec104_status iec104_server_get_tls_info(const iec104_server* server,
                                                    uint32_t connection, iec104_tls_info* out);

/** @brief Closes all connections and the listening socket and frees the handle. NULL is ignored. */
IEC104_API void iec104_server_destroy(iec104_server* server);

/* ------------------------------------------------------------------------- */
/* Client (controlling station)                                              */
/* ------------------------------------------------------------------------- */

/** @brief Opaque client handle. One client = one connection. */
typedef struct iec104_client iec104_client;

/** @brief Creates a client. Arguments as for iec104_server_create(). */
IEC104_API iec104_status iec104_client_create(const iec104_link_config* config,
                                              const iec104_callbacks* callbacks,
                                              iec104_client** out);

/**
 * @brief Starts connecting; completion is reported by the state callback (STOPPED).
 *
 * @param client The client.
 * @param host Name or address of the server.
 * @param port TCP port, e.g. IEC104_DEFAULT_PORT.
 * @return IEC104_OK; IEC104_ERR_WRONG_STATE if a connection exists;
 *         IEC104_ERR_INVALID_ARGUMENT if @p host is NULL or unknown;
 *         IEC104_ERR_IO if the connect fails at once.
 */
IEC104_API iec104_status iec104_client_connect(iec104_client* client, const char* host,
                                               uint16_t port);

/** @brief Sends STARTDT act. @return IEC104_OK or IEC104_ERR_WRONG_STATE if not STOPPED. */
IEC104_API iec104_status iec104_client_start(iec104_client* client);
/** @brief Sends STOPDT act. @return IEC104_OK or IEC104_ERR_WRONG_STATE if not STARTED. */
IEC104_API iec104_status iec104_client_stop(iec104_client* client);

/** @brief As iec104_server_poll(). */
IEC104_API iec104_status iec104_client_poll(iec104_client* client, int timeout_ms);

/**
 * @brief Sends an ASDU.
 * @return IEC104_OK; IEC104_ERR_WRONG_STATE if not STARTED;
 *         IEC104_ERR_WOULD_BLOCK if the window k is full (poll and retry).
 */
IEC104_API iec104_status iec104_client_send_asdu(iec104_client* client, const uint8_t* asdu,
                                                 size_t length);

/** @brief Time limits of the client for commands. */
typedef struct iec104_client_options {
    uint32_t confirmation_timeout_ms; /**< Activation -> confirmation, default 10000. */
    uint32_t termination_timeout_ms;  /**< Confirmation -> termination, default 10000. */
    /** 1 (default) = an executed command (45..64) is finished by its termination (cause 10);
     *  0 = the positive confirmation is final (for peers that send no termination). */
    int expect_termination;
} iec104_client_options;

/** @brief Fills @p options with the defaults. Does nothing for NULL. */
IEC104_API void iec104_client_options_init(iec104_client_options* options);

/** @brief Applies options. @return IEC104_OK or IEC104_ERR_INVALID_ARGUMENT for NULL. */
IEC104_API iec104_status iec104_client_set_options(iec104_client* client,
                                                   const iec104_client_options* options);

/**
 * @brief Sends one object with cause activation (6), e.g. a command or a set point.
 *
 * The answers (confirmation, cause 7, and termination, cause 10) arrive via
 * on_object with the same type; a negative answer has info->negative = 1.
 * In addition the library follows the command and reports its final result
 * once via on_command_result (see iec104_client_options for the time limits).
 * A command with select = 1 ends with IEC104_COMMAND_SELECTED; the execute
 * is a second call with select = 0. Type 102 (read) is sent with cause 5
 * and not followed (see iec104_client_read()).
 *
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL or an unsupported
 *         type; IEC104_ERR_OUT_OF_RANGE for a bad value; IEC104_ERR_WRONG_STATE
 *         if not STARTED; IEC104_ERR_WOULD_BLOCK if the window k is full.
 */
IEC104_API iec104_status iec104_client_send_command(iec104_client* client, uint16_t common_address,
                                                    const iec104_object* command);

/**
 * @brief Select before operate in one call: sends the select, and after its
 *        positive confirmation the execute of the same command.
 *
 * @p command->select is ignored. The final result (of the execute, or the
 * rejection of the select) arrives via on_command_result.
 *
 * @return As iec104_client_send_command(); additionally
 *         IEC104_ERR_INVALID_ARGUMENT for types without select (51, 64) and
 *         non-command types.
 */
IEC104_API iec104_status iec104_client_select_execute(iec104_client* client,
                                                      uint16_t common_address,
                                                      const iec104_object* command);

/**
 * @brief Read command (type 102, cause 5 "request") for one information object.
 *
 * The station answers with the object itself (its type, cause 5) via
 * on_object, or negatively with cause 47 (unknown IOA) or 46 (unknown
 * station). A read has no confirmation and is not followed by
 * on_command_result. iec104_client_send_command() with type 102 does the same.
 * @param client The client.
 * @param common_address Station of the object.
 * @param ioa Information object address to read.
 * @return As iec104_client_send_command().
 */
IEC104_API iec104_status iec104_client_read(iec104_client* client, uint16_t common_address,
                                            uint32_t ioa);

/** @brief General (qoi 20) or group (21..36) interrogation; results via on_object. */
IEC104_API iec104_status iec104_client_interrogation(iec104_client* client, uint16_t common_address,
                                                     uint8_t qoi);

/** @brief Counter interrogation: rqt 1..4 = group, 5 = all; frz 0 = read .. 3 = reset. */
IEC104_API iec104_status iec104_client_counter_interrogation(iec104_client* client,
                                                             uint16_t common_address, uint8_t rqt,
                                                             uint8_t frz);

/** @brief Clock synchronization command (type 103) with @p time. */
IEC104_API iec104_status iec104_client_clock_sync(iec104_client* client, uint16_t common_address,
                                                  const iec104_time* time);

/** @brief Current state; IEC104_LINK_CLOSED before connect and after close. */
IEC104_API iec104_link_state iec104_client_state(const iec104_client* client);

/** @brief Closes the connection (reported as IEC104_CLOSE_LOCAL on the next poll). */
IEC104_API void iec104_client_close(iec104_client* client);

/** @brief As iec104_server_set_log(). */
IEC104_API void iec104_client_set_log(iec104_client* client, iec104_log_level max_level,
                                      iec104_log_fn fn, void* user);

/**
 * @brief Counters of the current (or last) connection.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL; IEC104_ERR_WRONG_STATE
 *         before the first connect.
 */
IEC104_API iec104_status iec104_client_get_stats(const iec104_client* client,
                                                 iec104_link_stats* out);

/**
 * @brief Uses TLS for the next connect (see iec104_server_set_tls()).
 *
 * A session of the previous connection is offered for resumption unless
 * the settings changed in between.
 */
IEC104_API iec104_status iec104_client_set_tls(iec104_client* client,
                                               const iec104_tls_config* config);

/** @brief TLS details of the open connection (see iec104_server_get_tls_info()). */
IEC104_API iec104_status iec104_client_get_tls_info(const iec104_client* client,
                                                    iec104_tls_info* out);

/* ------------------------------------------------------------------------- */
/* Configuration file                                                        */
/* ------------------------------------------------------------------------- */

/** @brief Most redundancy groups in a configuration file. */
#define IEC104_CONFIG_MAX_GROUPS 8

/** @brief Everything a configuration file can set. */
typedef struct iec104_config {
    iec104_link_config link;      /**< Timers, windows, ASDU profile. */
    iec104_server_options server; /**< Server behaviour. */
    iec104_client_options client; /**< Client time limits. */
    char host[256];               /**< Address: server = bind address, client = peer. */
    uint16_t port;                /**< TCP port. */
    int use_tls;                  /**< 1: apply @p tls with *_set_tls(). */
    iec104_tls_config tls;        /**< TLS settings. */
    /** Redundancy groups of a server, for iec104_server_add_group(). */
    iec104_group_config groups[IEC104_CONFIG_MAX_GROUPS];
    uint32_t group_count; /**< Used entries of @p groups. */
} iec104_config;

/** @brief Defaults of all parts (standard values, host "", port 2404). Does nothing for NULL. */
IEC104_API void iec104_config_init(iec104_config* config);

/**
 * @brief Reads a configuration file into @p config (keys not in the file keep their value).
 *
 * Format: one `key = value` per line; `#` or `;` starts a comment. Times in
 * seconds (fractions allowed). Keys:
 *
 * | key | field |
 * |-----|-------|
 * | host, port | host, port |
 * | t0, t1, t2, t3 | link timers (s) |
 * | k, w, tolerant | link |
 * | cot_length, ca_length, ioa_length, max_asdu_length, originator | link (ASDU profile) |
 * | send_end_of_init, command_termination, cause_of_init | server |
 * | max_command_delay | server (s) |
 * | event_buffer_size, overflow ("drop_oldest", "reject") | server; also for the groups |
 * | group | one redundancy group per line: addresses separated by blanks or commas, "*" = any |
 * | confirmation_timeout, termination_timeout, expect_termination | client (s) |
 * | tls | use_tls |
 * | tls_ca, tls_certificate, tls_key, tls_crl | tls (files) |
 * | tls_key_password, tls_peer_name, tls_peer_fingerprint | tls |
 * | tls_min_version, tls_max_version | tls ("1.2", "1.3") |
 * | tls_ciphers, tls_ciphersuites | tls (cipher_list, cipher_suites) |
 * | tls_verify_peer, tls_renegotiation, tls_session_resumption | tls |
 * | tls_renewal_interval, tls_session_lifetime | tls (s) |
 *
 * Relative file names are relative to the directory of the configuration file.
 *
 * @param path Configuration file.
 * @param config Receives the values; keys not in the file keep their value.
 * @param error_line Receives the line number of the first error (0 if none); may be NULL.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL, an unknown key or
 *         a malformed line; IEC104_ERR_OUT_OF_RANGE for a value outside its
 *         type; IEC104_ERR_IO if the file cannot be read.
 */
IEC104_API iec104_status iec104_config_load(const char* path, iec104_config* config,
                                            int* error_line);

/** @brief Closes the connection and frees the handle. NULL is ignored. */
IEC104_API void iec104_client_destroy(iec104_client* client);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IEC104_IEC104_H */
