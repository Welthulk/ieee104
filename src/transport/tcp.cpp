/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file tcp.cpp
 * @brief Implementation of the non-blocking TCP helpers (Linux).
 *
 * Layer: transport (I/O).
 */
#include "transport/tcp.hpp"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace iec104::transport {
namespace {

/** @brief Frees a getaddrinfo() result when leaving the scope. */
struct AddrInfo {
    addrinfo* list{nullptr};
    AddrInfo() = default;
    AddrInfo(const AddrInfo&) = delete;
    AddrInfo& operator=(const AddrInfo&) = delete;
    AddrInfo(AddrInfo&&) = delete;
    AddrInfo& operator=(AddrInfo&&) = delete;
    ~AddrInfo() {
        if (list != nullptr) {
            freeaddrinfo(list);
        }
    }
};

/** @brief Resolves host and port; passive = for bind(). */
bool resolve(const char* host, std::uint16_t port, bool passive, AddrInfo& out) noexcept {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC; // IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV | (passive ? AI_PASSIVE : 0);
    const std::string service = std::to_string(port);
    const char* node = (host != nullptr && host[0] != '\0') ? host : nullptr;
    return getaddrinfo(node, service.c_str(), &hints, &out.list) == 0 && out.list != nullptr;
}

/**
 * @brief Socket options of every IEC 104 connection.
 *
 * TCP_NODELAY: APDUs are small and latency matters (an S-frame or a
 * confirmation must not wait for Nagle's algorithm).
 */
void set_connection_options(int fd) noexcept {
    const int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}

} // namespace

void Socket::reset(int fd) noexcept {
    if (fd_ >= 0) {
        (void)::close(fd_);
    }
    fd_ = fd;
}

iec104_status listen_tcp(const char* host, std::uint16_t port, Socket& out) noexcept {
    AddrInfo ai;
    if (!resolve(host, port, true, ai)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    for (const addrinfo* a = ai.list; a != nullptr; a = a->ai_next) {
        Socket s(
            ::socket(a->ai_family, a->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, a->ai_protocol));
        if (!s.valid()) {
            continue;
        }
        // Allows an immediate restart of a server on the same port while old
        // connections are still in TIME_WAIT.
        const int one = 1;
        (void)setsockopt(s.fd(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        if (::bind(s.fd(), a->ai_addr, a->ai_addrlen) == 0 && ::listen(s.fd(), 16) == 0) {
            out = std::move(s);
            return IEC104_OK;
        }
    }
    return IEC104_ERR_IO;
}

iec104_status accept_tcp(const Socket& listener, Socket& out) noexcept {
    const int fd = ::accept4(listener.fd(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? IEC104_ERR_WOULD_BLOCK
                                                                           : IEC104_ERR_IO;
    }
    set_connection_options(fd);
    out.reset(fd);
    return IEC104_OK;
}

iec104_status connect_tcp(const char* host, std::uint16_t port, Socket& out) noexcept {
    if (host == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    AddrInfo ai;
    if (!resolve(host, port, false, ai)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    // The first address is used. Trying further addresses would need a
    // second connect round after a failure; not needed for version 1.
    const addrinfo* a = ai.list;
    Socket s(::socket(a->ai_family, a->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, a->ai_protocol));
    if (!s.valid()) {
        return IEC104_ERR_IO;
    }
    set_connection_options(s.fd());
    if (::connect(s.fd(), a->ai_addr, a->ai_addrlen) != 0 && errno != EINPROGRESS) {
        return IEC104_ERR_IO;
    }
    out = std::move(s);
    return IEC104_OK;
}

iec104_status finish_connect(const Socket& socket) noexcept {
    int error = 0;
    socklen_t len = sizeof error;
    if (getsockopt(socket.fd(), SOL_SOCKET, SO_ERROR, &error, &len) != 0 || error != 0) {
        return IEC104_ERR_IO;
    }
    return IEC104_OK;
}

std::string peer_address(const Socket& socket) {
    sockaddr_storage addr{};
    socklen_t len = sizeof addr;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): socket API takes sockaddr*
    if (getpeername(socket.fd(), reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return {};
    }
    std::array<char, INET6_ADDRSTRLEN> text{};
    if (addr.ss_family == AF_INET) {
        sockaddr_in in{};
        std::memcpy(&in, &addr, sizeof in);
        (void)inet_ntop(AF_INET, &in.sin_addr, text.data(), text.size());
        return std::string(text.data()) + ":" + std::to_string(ntohs(in.sin_port));
    }
    if (addr.ss_family == AF_INET6) {
        sockaddr_in6 in6{};
        std::memcpy(&in6, &addr, sizeof in6);
        (void)inet_ntop(AF_INET6, &in6.sin6_addr, text.data(), text.size());
        return "[" + std::string(text.data()) + "]:" + std::to_string(ntohs(in6.sin6_port));
    }
    return {};
}

std::uint16_t local_port(const Socket& socket) noexcept {
    sockaddr_storage addr{};
    socklen_t len = sizeof addr;
    // The socket API takes the generic sockaddr*; the cast is the documented usage.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (getsockname(socket.fd(), reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return 0;
    }
    // Copy into the family-specific struct instead of casting the storage.
    if (addr.ss_family == AF_INET) {
        sockaddr_in in{};
        std::memcpy(&in, &addr, sizeof in);
        return ntohs(in.sin_port);
    }
    if (addr.ss_family == AF_INET6) {
        sockaddr_in6 in6{};
        std::memcpy(&in6, &addr, sizeof in6);
        return ntohs(in6.sin6_port);
    }
    return 0;
}

iec104_status read_some(const Socket& socket, std::uint8_t* buf, std::size_t size,
                        std::size_t& read) noexcept {
    return read_some(socket.fd(), buf, size, read);
}

iec104_status read_some(int fd, std::uint8_t* buf, std::size_t size, std::size_t& read) noexcept {
    const ssize_t n = ::recv(fd, buf, size, 0);
    if (n > 0) {
        read = static_cast<std::size_t>(n);
        return IEC104_OK;
    }
    if (n == 0) {
        return IEC104_ERR_CLOSED;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return IEC104_ERR_WOULD_BLOCK;
    }
    return errno == ECONNRESET ? IEC104_ERR_CLOSED : IEC104_ERR_IO;
}

iec104_status write_some(const Socket& socket, const std::uint8_t* data, std::size_t size,
                         std::size_t& written) noexcept {
    return write_some(socket.fd(), data, size, written);
}

iec104_status write_some(int fd, const std::uint8_t* data, std::size_t size,
                         std::size_t& written) noexcept {
    written = 0;
    if (size == 0U) {
        return IEC104_OK;
    }
    const ssize_t n = ::send(fd, data, size, MSG_NOSIGNAL);
    if (n >= 0) {
        written = static_cast<std::size_t>(n);
        return IEC104_OK;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return IEC104_OK;
    }
    return (errno == EPIPE || errno == ECONNRESET) ? IEC104_ERR_CLOSED : IEC104_ERR_IO;
}

std::uint64_t monotonic_ms() noexcept {
    timespec ts{};
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (static_cast<std::uint64_t>(ts.tv_sec) * 1000U) +
           (static_cast<std::uint64_t>(ts.tv_nsec) / 1000000U);
}

std::int64_t utc_ms() noexcept {
    timespec ts{};
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    return (static_cast<std::int64_t>(ts.tv_sec) * 1000) +
           (static_cast<std::int64_t>(ts.tv_nsec) / 1000000);
}

} // namespace iec104::transport
