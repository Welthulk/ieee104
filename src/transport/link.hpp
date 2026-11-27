/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file link.hpp
 * @brief One TCP connection with its APCI state machine.
 *
 * A Link joins the two halves that are kept apart below this layer:
 *
 *   Socket (tcp.hpp)  <--bytes-->  Link  <--bytes, events-->  apci::Connection
 *
 * It reads from the socket into the state machine, writes the state
 * machine's output to the socket, drives the timers with the monotonic
 * clock, and hands the events to a sink (the C API layer). It also covers
 * the connection set-up, which the sans-I/O state machine does not know
 * about:
 *
 * | Phase       | Ends when                        | Time limit |
 * |-------------|----------------------------------|------------|
 * | Connecting  | TCP connect complete (client)    | t0         |
 * | Handshaking | TLS handshake complete (if TLS)  | t0 (E9)    |
 * | Open        | close                            | t1..t3     |
 *
 * With TLS (tls.hpp) every octet goes through the TLS session instead of
 * the socket; the state machine sees the same plain APDUs either way.
 *
 * Layer: transport (I/O).
 */
#pragma once

#include "apci/connection.hpp"
#include "transport/tcp.hpp"
#include "transport/tls.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace iec104::transport {

/** @brief Why a link was closed, beyond the reasons of the state machine. */
enum class LinkCloseReason : std::uint8_t {
    None,          ///< Open.
    Protocol,      ///< The state machine closed; see apci::CloseReason.
    PeerClosed,    ///< The peer closed the TCP connection.
    ConnectFailed, ///< The TCP connect failed (refused, unreachable).
    T0Timeout,     ///< TCP connect or TLS handshake not complete within t0.
    SocketError,   ///< A read or write failed.
    TlsError,      ///< TLS handshake failed or TLS error on the open connection.
};

/** @brief Security events of a TLS link (reported with a text). */
enum class TlsEvent : std::uint8_t {
    Established,         ///< Handshake complete; text: version, suite, peer, resumed.
    HandshakeFailed,     ///< Handshake failed or did not complete within t0.
    CertificateRejected, ///< The peer certificate was rejected; text: reason.
    Error,               ///< TLS error on the open connection; text: reason.
    Renewal,             ///< Key renewal started by this side, or a renegotiation completed.
};

/** @brief Receiver of what happens on a link (implemented by the API layer). */
class LinkSink {
  public:
    LinkSink() = default;
    LinkSink(const LinkSink&) = delete;
    LinkSink& operator=(const LinkSink&) = delete;
    LinkSink(LinkSink&&) = delete;
    LinkSink& operator=(LinkSink&&) = delete;
    virtual ~LinkSink() = default;
    /** @brief An ASDU arrived on link @p id. */
    virtual void on_asdu(std::uint32_t id, const std::uint8_t* asdu, std::size_t length) = 0;
    /** @brief The state of link @p id changed. */
    virtual void on_state(std::uint32_t id, apci::State state) = 0;
    /** @brief Link @p id is closed; it is removed after this call. */
    virtual void on_closed(std::uint32_t id, LinkCloseReason reason,
                           apci::CloseReason protocol_reason) = 0;
    /** @brief A complete APDU was sent or received on link @p id (for a raw data trace). */
    virtual void on_frame(std::uint32_t /*id*/, bool /*outgoing*/, const std::uint8_t* /*apdu*/,
                          std::size_t /*length*/) {}
    /** @brief The peer acknowledged the @p count oldest unacknowledged I-frames of link @p id. */
    virtual void on_acknowledged(std::uint32_t /*id*/, std::uint16_t /*count*/) {}
    /** @brief A security event on TLS link @p id. */
    virtual void on_tls(std::uint32_t /*id*/, TlsEvent /*event*/, const std::string& /*text*/) {}
};

/** @brief A TCP connection plus its state machine (see file comment). */
class Link {
  public:
    /**
     * @brief Sets up a link for an accepted socket (server side, already connected).
     * @param tls TLS context of the server, NULL for plain TCP.
     * @return Status of apci::Connection::init() or of the TLS session.
     */
    iec104_status init_accepted(std::uint32_t id, const apci::ApciConfig& config, Socket socket,
                                apci::TimeMs now,
                                std::shared_ptr<TlsContext> tls = nullptr) noexcept;

    /**
     * @brief Sets up a link for a socket with a connect in progress (client side).
     * @param tls TLS context of the client, NULL for plain TCP.
     * @return Status of apci::Connection::init().
     */
    iec104_status init_connecting(std::uint32_t id, const apci::ApciConfig& config, Socket socket,
                                  apci::TimeMs now,
                                  std::shared_ptr<TlsContext> tls = nullptr) noexcept;

    /** @brief Events for poll(): depend on the phase and on what TLS waits for. */
    [[nodiscard]] short poll_events() const noexcept;

    /** @brief Handles the poll() result for this link and runs the timers. */
    void service(short revents, apci::TimeMs now, LinkSink& sink) noexcept;

    /** @brief Sends an ASDU (see apci::Connection::send_asdu()). */
    iec104_status send_asdu(const std::uint8_t* asdu, std::size_t length,
                            apci::TimeMs now) noexcept;

    /** @brief STARTDT act (client). */
    iec104_status start(apci::TimeMs now) noexcept { return conn_.start(now); }
    /** @brief STOPDT act (client). */
    iec104_status stop(apci::TimeMs now) noexcept { return conn_.stop(now); }
    /**
     * @brief Closes locally; reported as closed on the next service().
     *
     * Also while the TCP connect or the TLS handshake still runs: the link
     * then ends at once instead of waiting for the connect or t0.
     */
    void close() noexcept;

    /**
     * @brief Test aid: a silent link neither reads nor writes any more.
     *
     * The socket stays open and the timers keep running, so the state
     * machine sees what a broken network path looks like: no answers, no
     * acknowledgements, and finally t1. Used to test path redundancy.
     */
    void set_silent(bool silent) noexcept { silent_ = silent; }
    /** @brief True while the link is silent (see set_silent()). */
    [[nodiscard]] bool silent() const noexcept { return silent_; }

    /** @brief Earliest time service() must run even without socket activity. */
    [[nodiscard]] apci::TimeMs next_deadline() const noexcept;

    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }
    [[nodiscard]] int fd() const noexcept { return socket_.fd(); }
    [[nodiscard]] bool closed() const noexcept { return close_reason_ != LinkCloseReason::None; }
    /** @brief True once the close has been reported to the sink; the link can be removed. */
    [[nodiscard]] bool finished() const noexcept { return reported_closed_; }
    /** @brief True until the connection is open (TCP connect and TLS handshake done). */
    [[nodiscard]] bool connecting() const noexcept { return phase_ != Phase::Open; }
    /** @brief True if the link runs over TLS. */
    [[nodiscard]] bool secured() const noexcept { return tls_ != nullptr; }
    /** @brief TLS details of an open TLS link; false for plain TCP or before the handshake. */
    bool tls_info(TlsInfo& out) const;
    [[nodiscard]] apci::State state() const noexcept { return conn_.state(); }
    [[nodiscard]] bool can_send() const noexcept { return conn_.can_send(); }
    /** @brief Frame and octet counters of the state machine. */
    [[nodiscard]] const apci::ConnectionStats& stats() const noexcept { return conn_.stats(); }
    /** @brief Address of the peer ("ip:port"), "" while connecting. */
    [[nodiscard]] std::string peer() const { return peer_address(socket_); }

    /** @brief Passes every complete APDU of this link to @p sink->on_frame() (NULL = off). */
    void set_trace(LinkSink* sink) noexcept {
        trace_sink_ = sink;
        conn_.set_trace(sink != nullptr ? &Link::trace_thunk : nullptr, this);
    }

  private:
    enum class Phase : std::uint8_t { Connecting, Handshaking, Open };

    iec104_status start_tls_or_open(apci::TimeMs now) noexcept;
    bool continue_connect(short revents, apci::TimeMs now, LinkSink& sink) noexcept;
    bool continue_handshake(apci::TimeMs now, LinkSink& sink) noexcept;
    void renew_keys(apci::TimeMs now, LinkSink& sink) noexcept;
    iec104_status io_read(std::uint8_t* buf, std::size_t size, std::size_t& n) noexcept;
    iec104_status io_write(const std::uint8_t* data, std::size_t size, std::size_t& n) noexcept;
    [[nodiscard]] LinkCloseReason io_failure(iec104_status st) const noexcept;
    void read_input(apci::TimeMs now, LinkSink& sink) noexcept;
    void dispatch_events(LinkSink& sink) noexcept;
    void flush_output() noexcept;
    void finish(LinkCloseReason reason, LinkSink& sink) noexcept;
    static void trace_thunk(void* context, bool outgoing, const std::uint8_t* apdu,
                            std::size_t length) noexcept;

    std::uint32_t id_{0};
    Socket socket_;
    apci::Connection conn_;
    Phase phase_{Phase::Connecting};
    apci::TimeMs setup_deadline_{0}; // end of connect and handshake (t0)
    // TLS: the context outlives the session (declared first, destroyed last).
    std::shared_ptr<TlsContext> tls_context_;
    std::unique_ptr<TlsSession> tls_;
    apci::TimeMs next_renewal_{apci::kNoDeadline};
    std::uint32_t handshakes_seen_{0}; // TLS 1.2: completed handshakes already reported
    LinkCloseReason close_reason_{LinkCloseReason::None};
    apci::CloseReason protocol_reason_{apci::CloseReason::None};
    bool reported_closed_{false};
    bool silent_{false}; // test aid, see set_silent()
    LinkSink* trace_sink_{nullptr};

    // Bytes taken from the state machine but not yet accepted by the socket.
    std::array<std::uint8_t, 8192> pending_{};
    std::size_t pending_fill_{0};
};

} // namespace iec104::transport
