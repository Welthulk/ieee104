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
#define IEC104_VERSION_MINOR 1
/** @brief Patch version of this header. Fixes without API change increment it. */
#define IEC104_VERSION_PATCH 0

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
 * accepted (the library reports them nowhere yet). The field lengths of the
 * ASDU deviate from 2/2/3 only for peers outside the standard.
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
    IEC104_CLOSE_T0_TIMEOUT = 4,        /**< TCP connect not complete within t0. */
    IEC104_CLOSE_T1_TIMEOUT = 5,        /**< Frame not acknowledged/confirmed within t1. */
    IEC104_CLOSE_SEQUENCE_ERROR = 6,    /**< Received N(S) not as expected. */
    IEC104_CLOSE_ACK_OUT_OF_WINDOW = 7, /**< Received N(R) acknowledges frames never sent. */
    IEC104_CLOSE_MALFORMED = 8,         /**< The byte stream is not a valid APDU sequence. */
    IEC104_CLOSE_UNEXPECTED_FRAME = 9,  /**< Frame not allowed in the current state or role. */
    IEC104_CLOSE_SOCKET_ERROR = 10      /**< A socket operation failed. */
} iec104_close_reason;

/** @brief Short English name of a link state. Never NULL. */
IEC104_API const char* iec104_link_state_string(iec104_link_state state);
/** @brief Short English name of a close reason. Never NULL. */
IEC104_API const char* iec104_close_reason_string(iec104_close_reason reason);

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

/**
 * @brief One information object as a value.
 *
 * Which fields are used depends on @ref type:
 *
 * | type                         | value                      | other fields |
 * |------------------------------|----------------------------|-------------------------------------|
 * | 1, 30 single point           | 0 off, 1 on                | quality | | 3, 31 double point |
 * 0..3 (1 off, 2 on)         | quality                             | | 5, 32 step position |
 * -64..63                    | quality, flags TRANSIENT            | | 7, 33 bitstring | - | bits,
 * quality                       | | 9, 34 normalized             | -1.0 .. 1.0 - 2^-15        |
 * quality                             | | 11, 35 scaled                | -32768..32767 | quality |
 * | 13, 36 short float           | float                      | quality | | 15, 37 integrated
 * totals     | counter, 32 bit signed     | qualifier = sequence 0..31,         | | | | flags
 * CARRY/ADJUSTED, quality IV    | | 21 normalized, no quality    | -1.0 .. 1.0 - 2^-15        | - |
 * | 45, 58 single command        | 0 off, 1 on                | qualifier = QU 0..31, select | |
 * 46, 59 double command        | 1 off, 2 on                | qualifier = QU, select              |
 * | 47, 60 regulating step       | 1 lower, 2 higher          | qualifier = QU, select | | 48, 61
 * set point normalized  | -1.0 .. 1.0 - 2^-15        | qualifier = QL 0..127, select       | | 49,
 * 62 set point scaled      | -32768..32767              | qualifier = QL, select              | |
 * 50, 63 set point float       | float                      | qualifier = QL, select              |
 * | 51, 64 bitstring command     | -                          | bits | | 70 end of initialization
 * | -                          | qualifier = cause 0..127, flags     | | 100 interrogation | - |
 * qualifier = QOI (20 station, 21..36 group 1..16) | | 101 counter interrogation    | - | qualifier
 * = RQT (1..4 group, 5 all), bits = FRZ 0..3 | | 102 read                     | - | - | | 103 clock
 * synchronization    | -                          | time                                | | 105
 * reset process            | -                          | qualifier = QRP                     | |
 * 107 test with time tag       | test sequence counter      | time                                |
 *
 * Types 30..37, 58..64 and 107 carry @ref time as time tag. Other types
 * (e.g. CP24Time2a types, parameters, protection events) are available as
 * raw ASDUs (on_asdu, *_send_asdu).
 */
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
} iec104_callbacks;

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
 * @param timeout_ms Maximum time to wait for activity; 0 = do not wait.
 *                   The call returns earlier when a timer is due.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL; IEC104_ERR_IO if poll() fails.
 */
IEC104_API iec104_status iec104_server_poll(iec104_server* server, int timeout_ms);

/**
 * @brief Sends an ASDU on one connection, or on all started connections.
 *
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

/** @brief Behaviour of the server's application layer. */
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
} iec104_server_options;

/** @brief Fills @p options with the defaults. Does nothing for NULL. */
IEC104_API void iec104_server_options_init(iec104_server_options* options);

/** @brief Applies options. @return IEC104_OK or IEC104_ERR_INVALID_ARGUMENT for NULL. */
IEC104_API iec104_status iec104_server_set_options(iec104_server* server,
                                                   const iec104_server_options* options);

/**
 * @brief Adds a data point to the server's process image (or replaces it).
 *
 * The image answers interrogation (type 100) and counter interrogation
 * (type 101) and read commands (type 102). A point is identified by common
 * address and IOA. Points with a time tag type (30..37) are stored as their
 * type without time tag and answered as such in an interrogation.
 *
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
 * Connections that are not started do not receive it (no buffering yet).
 *
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT if the point is unknown or
 *         the type does not fit; IEC104_ERR_OUT_OF_RANGE for a bad value;
 *         IEC104_ERR_WOULD_BLOCK if the send queue of a connection is full.
 */
IEC104_API iec104_status iec104_server_update(iec104_server* server, uint16_t common_address,
                                              const iec104_object* object);

/**
 * @brief Sends objects of one type with a chosen cause (e.g. periodic, cause 1).
 *
 * Long lists are split into several ASDUs. Objects are not stored in the image.
 *
 * @param connection Connection id, or 0 = all started connections.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL, count 0, mixed or
 *         unsupported types; IEC104_ERR_OUT_OF_RANGE for a bad value;
 *         IEC104_ERR_WRONG_STATE if no started connection matches;
 *         IEC104_ERR_WOULD_BLOCK if a send queue is full.
 */
IEC104_API iec104_status iec104_server_send(iec104_server* server, uint32_t connection,
                                            uint16_t common_address, uint8_t cause,
                                            const iec104_object* objects, size_t count);

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
 * is a second call with select = 0.
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

/** @brief Closes the connection and frees the handle. NULL is ignored. */
IEC104_API void iec104_client_destroy(iec104_client* client);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IEC104_IEC104_H */
