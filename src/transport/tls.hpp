/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file tls.hpp
 * @brief TLS backend interface: secures the byte stream of a link.
 *
 * IEC 60870-5-104 itself has no security; IEC 62351-3 adds a TLS profile
 * underneath it (docs/02_Technik_und_Quellen.md, section "Sicherheit"). The
 * library does not implement TLS, it uses an external library behind this
 * interface:
 *
 *   Link  --plain bytes-->  TlsSession  --TLS records-->  socket
 *
 * A TlsContext holds what all connections of a server or client share
 * (certificates, keys, CA, CRL, versions, suites, session cache) and creates
 * one TlsSession per connection. All calls are non-blocking and report
 * "try again later" as IEC104_ERR_WOULD_BLOCK together with the socket event
 * the backend waits for (poll_events()).
 *
 * Backends: OpenSSL 3.x (tls_openssl.cpp); without OpenSSL a stub that
 * reports IEC104_ERR_UNSUPPORTED (tls_none.cpp, CMake option IEC104_TLS=OFF).
 *
 * Layer: transport (I/O).
 */
#pragma once

#include "iec104/iec104.h"
#include "transport/tcp.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace iec104::transport {

/** @brief Side of the TLS handshake: the client (controlling station) starts it. */
enum class TlsRole : std::uint8_t { Client, Server };

/** @brief TLS protocol versions (wire values). */
constexpr std::uint16_t kTls12 = 0x0303;
constexpr std::uint16_t kTls13 = 0x0304;

/** @brief Settings of a TlsContext (see iec104_tls_config for the meaning). */
struct TlsSettings {
    std::string ca_file;
    std::string certificate_file;
    std::string private_key_file;
    std::string key_password;
    std::string crl_file;
    std::string peer_name;
    std::string peer_fingerprint; ///< SHA-256, hex; colons and case are ignored.
    std::string cipher_list;      ///< TLS 1.2 (OpenSSL syntax); "" = default of the profile.
    std::string cipher_suites;    ///< TLS 1.3 (OpenSSL syntax); "" = default of the profile.
    std::uint16_t min_version{kTls12};
    std::uint16_t max_version{kTls13};
    bool verify_peer{true};
    bool renegotiation{true};
    std::uint32_t renewal_interval_ms{0}; ///< 0 = no key renewal by this side.
    bool session_resumption{true};
    std::uint32_t session_lifetime_s{3600};
};

/** @brief What is known about an established TLS connection. */
struct TlsInfo {
    std::string version;          ///< e.g. "TLSv1.3".
    std::string cipher;           ///< e.g. "TLS_AES_256_GCM_SHA384".
    std::string peer_subject;     ///< Subject of the peer certificate, "" if none.
    std::string peer_fingerprint; ///< SHA-256 of the peer certificate, "AB:CD:..."; "" if none.
    bool resumed{false};          ///< The handshake resumed an earlier session.
};

/** @brief Kind of the last failure of a TlsSession. */
enum class TlsFailure : std::uint8_t {
    None,
    Handshake,   ///< Handshake failed: versions, suites, alert of the peer, not TLS at all.
    Certificate, ///< The peer certificate was rejected (chain, validity, CRL, name, fingerprint).
    Protocol,    ///< Error on an established connection (record layer, alert).
};

/** @brief One TLS connection over a connected, non-blocking socket. */
class TlsSession {
  public:
    TlsSession() = default;
    TlsSession(const TlsSession&) = delete;
    TlsSession& operator=(const TlsSession&) = delete;
    TlsSession(TlsSession&&) = delete;
    TlsSession& operator=(TlsSession&&) = delete;
    virtual ~TlsSession() = default;

    /**
     * @brief Continues the handshake.
     * @return IEC104_OK when it is complete; IEC104_ERR_WOULD_BLOCK while it
     *         runs; IEC104_ERR_IO if it failed (see failure(), error()).
     */
    virtual iec104_status handshake() noexcept = 0;

    /** @brief As read_some(): OK, WOULD_BLOCK, CLOSED (peer ended TLS or TCP), IO. */
    virtual iec104_status read(std::uint8_t* buf, std::size_t size, std::size_t& read) noexcept = 0;

    /**
     * @brief Writes as much as possible.
     *
     * After IEC104_ERR_WOULD_BLOCK the next call must pass the same octets
     * again (more may follow them).
     * @return OK (written > 0 unless size is 0), WOULD_BLOCK, CLOSED, IO.
     */
    virtual iec104_status write(const std::uint8_t* data, std::size_t size,
                                std::size_t& written) noexcept = 0;

    /** @brief Socket events the last call waits for (POLLIN, POLLOUT). */
    [[nodiscard]] virtual short poll_events() const noexcept = 0;

    /**
     * @brief Starts a key renewal: key update (TLS 1.3) or renegotiation (TLS 1.2).
     * @return IEC104_OK when started; IEC104_ERR_UNSUPPORTED if renegotiation
     *         is switched off; IEC104_ERR_IO on failure.
     */
    virtual iec104_status renew() noexcept = 0;

    /** @brief Sends close_notify once (best effort, never blocks). */
    virtual void shutdown() noexcept = 0;

    /**
     * @brief Completed TLS 1.2 handshakes of this connection: 1 after the
     *        first, +1 for every renegotiation (by either side).
     */
    [[nodiscard]] virtual std::uint32_t handshakes() const noexcept = 0;

    /** @brief Version, suite, peer certificate; valid after the handshake. */
    [[nodiscard]] virtual TlsInfo info() const = 0;

    /** @brief Kind of the last failure. */
    [[nodiscard]] virtual TlsFailure failure() const noexcept = 0;

    /** @brief Text of the last failure, e.g. "certificate has expired". */
    [[nodiscard]] virtual const std::string& error() const noexcept = 0;
};

/** @brief Settings shared by all TLS connections of one server or client. */
class TlsContext {
  public:
    TlsContext() = default;
    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;
    TlsContext(TlsContext&&) = delete;
    TlsContext& operator=(TlsContext&&) = delete;
    virtual ~TlsContext() = default;

    /**
     * @brief Creates the TLS session of a new connection.
     *
     * The socket stays owned by the caller and must outlive the session.
     * @return IEC104_OK; IEC104_ERR_INTERNAL if the backend fails.
     */
    virtual iec104_status create_session(const Socket& socket,
                                         std::unique_ptr<TlsSession>& out) noexcept = 0;

    /** @brief Interval of the key renewal by this side, 0 = none. */
    [[nodiscard]] virtual std::uint32_t renewal_interval_ms() const noexcept = 0;
};

/**
 * @brief Creates a context: loads CA, certificate, key and CRL, applies versions and suites.
 *
 * @param error Receives a description of the failure (file, reason).
 * @return IEC104_OK; IEC104_ERR_IO if a file cannot be read or parsed;
 *         IEC104_ERR_INVALID_ARGUMENT for inconsistent settings (key does not
 *         match certificate, unknown suites, versions); IEC104_ERR_UNSUPPORTED
 *         if the library was built without TLS.
 */
iec104_status create_tls_context(TlsRole role, const TlsSettings& settings,
                                 std::shared_ptr<TlsContext>& out, std::string& error);

/** @brief True if the library was built with a TLS backend. */
bool tls_available() noexcept;

/**
 * @brief Normalizes a SHA-256 fingerprint to "AB:CD:...".
 * @return The normalized text, "" if @p text is not 32 octets in hex.
 */
std::string normalize_fingerprint(const std::string& text);

} // namespace iec104::transport
