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
 * Contents:
 *   - version and status helpers;
 *   - connection parameters (::iec104_link_config) and callbacks;
 *   - server (controlled station) and client (controlling station) handles
 *     that move ASDUs over TCP.
 *
 * ASDUs cross this interface as octets (type id, VSQ, COT, CA, objects).
 * Functions to build and read ASDUs as values follow with the session layer.
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
 * accepted (the library reports them nowhere yet).
 */
typedef struct iec104_link_config {
    uint32_t t0_ms; /**< Time-out of connection establishment, default 30000. */
    uint32_t t1_ms; /**< Time-out of send or test APDUs, default 15000. */
    uint32_t t2_ms; /**< Acknowledge at the latest after t2 without own data, default 10000. */
    uint32_t t3_ms; /**< Test frame after t3 idle time, default 20000. */
    uint16_t k;     /**< Max. unacknowledged sent I-frames, default 12. */
    uint16_t w;     /**< Acknowledge at the latest after w received I-frames, default 8. */
    int tolerant;   /**< 0 = strict IEC 104 behaviour (default), 1 = tolerate known deviations. */
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
