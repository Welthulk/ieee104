/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file tcp.hpp
 * @brief Thin wrappers around non-blocking POSIX TCP sockets (Linux).
 *
 * IEC 60870-5-104 runs over plain TCP; the controlled station listens (port
 * 2404 by default, docs/05: interoperability list "Portnumber 2404"), the
 * controlling station connects. This file hides the socket system calls
 * behind a few functions that never block and report errors as
 * iec104_status, so that the layers above contain no system calls.
 *
 * All sockets are non-blocking. Address resolution uses getaddrinfo(), so
 * host names, IPv4 and IPv6 addresses work.
 *
 * Layer: transport (I/O, Linux only in version 1, docs/05_Normfragen_und_Festlegungen.md, E4).
 */
#pragma once

#include "iec104/iec104.h"

#include <cstddef>
#include <cstdint>

namespace iec104::transport {

/**
 * @brief Owner of one socket file descriptor; closes it on destruction.
 *
 * Move-only, so that a descriptor is never closed twice.
 */
class Socket {
  public:
    Socket() noexcept = default;
    explicit Socket(int fd) noexcept : fd_(fd) {}
    ~Socket() { reset(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : fd_(other.release()) {}
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    /** @brief The descriptor, or -1. */
    [[nodiscard]] int fd() const noexcept { return fd_; }
    /** @brief True if a descriptor is held. */
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    /** @brief Closes the held descriptor (if any) and takes @p fd. */
    void reset(int fd = -1) noexcept;
    /** @brief Gives up ownership without closing. */
    int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

  private:
    int fd_{-1};
};

/**
 * @brief Opens a listening socket.
 *
 * @param host NULL or "" for all local addresses, else an address or name to bind to.
 * @param port TCP port; 0 lets the system choose a free port (see local_port()).
 * @param out  Receives the socket on success.
 * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT if @p host cannot be
 *         resolved; IEC104_ERR_IO if socket(), bind() or listen() fails
 *         (e.g. port in use).
 */
iec104_status listen_tcp(const char* host, std::uint16_t port, Socket& out) noexcept;

/**
 * @brief Accepts one pending connection, if any.
 *
 * @param listener Listening socket.
 * @param out      Receives the new, non-blocking socket.
 * @return IEC104_OK; IEC104_ERR_WOULD_BLOCK if no connection is pending;
 *         IEC104_ERR_IO on other errors.
 */
iec104_status accept_tcp(const Socket& listener, Socket& out) noexcept;

/**
 * @brief Starts a non-blocking connect.
 *
 * Completion is signalled by the socket becoming writable; then
 * finish_connect() tells whether it succeeded.
 *
 * @return IEC104_OK when the connect is complete or in progress;
 *         IEC104_ERR_INVALID_ARGUMENT if @p host is NULL or cannot be
 *         resolved; IEC104_ERR_IO if the connect fails at once.
 */
iec104_status connect_tcp(const char* host, std::uint16_t port, Socket& out) noexcept;

/**
 * @brief Result of a non-blocking connect once the socket is writable.
 * @return IEC104_OK if connected, IEC104_ERR_IO if the connect failed.
 */
iec104_status finish_connect(const Socket& socket) noexcept;

/** @brief Local port of a bound socket, 0 on error. */
std::uint16_t local_port(const Socket& socket) noexcept;

/**
 * @brief Reads what is available.
 *
 * @param read Receives the number of bytes read on IEC104_OK.
 * @return IEC104_OK (read > 0); IEC104_ERR_WOULD_BLOCK if nothing is
 *         available; IEC104_ERR_CLOSED if the peer closed the connection;
 *         IEC104_ERR_IO on errors.
 */
iec104_status read_some(const Socket& socket, std::uint8_t* buf, std::size_t size,
                        std::size_t& read) noexcept;

/**
 * @brief Writes as much as the socket accepts now.
 *
 * Never raises SIGPIPE (MSG_NOSIGNAL).
 *
 * @param written Receives the number of bytes written on IEC104_OK (may be 0).
 * @return IEC104_OK; IEC104_ERR_CLOSED if the peer is gone; IEC104_ERR_IO on errors.
 */
iec104_status write_some(const Socket& socket, const std::uint8_t* data, std::size_t size,
                         std::size_t& written) noexcept;

/** @brief Monotonic time in milliseconds (CLOCK_MONOTONIC), the clock of the APCI layer. */
std::uint64_t monotonic_ms() noexcept;

} // namespace iec104::transport
