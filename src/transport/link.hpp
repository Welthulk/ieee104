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
 * the connection set-up of the client with time-out t0, which the sans-I/O
 * state machine does not know about.
 *
 * Layer: transport (I/O).
 */
#pragma once

#include "apci/connection.hpp"
#include "transport/tcp.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace iec104::transport {

/** @brief Why a link was closed, beyond the reasons of the state machine. */
enum class LinkCloseReason : std::uint8_t {
    None,          ///< Open.
    Protocol,      ///< The state machine closed; see apci::CloseReason.
    PeerClosed,    ///< The peer closed the TCP connection.
    ConnectFailed, ///< The TCP connect failed (refused, unreachable).
    T0Timeout,     ///< The TCP connect did not complete within t0.
    SocketError,   ///< A read or write failed.
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
};

/** @brief A TCP connection plus its state machine (see file comment). */
class Link {
  public:
    /**
     * @brief Sets up a link for an accepted socket (server side, already connected).
     * @return Status of apci::Connection::init().
     */
    iec104_status init_accepted(std::uint32_t id, const apci::ApciConfig& config, Socket socket,
                                apci::TimeMs now) noexcept;

    /**
     * @brief Sets up a link for a socket with a connect in progress (client side).
     * @return Status of apci::Connection::init().
     */
    iec104_status init_connecting(std::uint32_t id, const apci::ApciConfig& config, Socket socket,
                                  apci::TimeMs now) noexcept;

    /** @brief Events for poll(): POLLIN always, POLLOUT while connecting or output is pending. */
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
    /** @brief Closes locally; reported as closed on the next service(). */
    void close() noexcept { conn_.close(); }

    /** @brief Earliest time service() must run even without socket activity. */
    [[nodiscard]] apci::TimeMs next_deadline() const noexcept;

    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }
    [[nodiscard]] int fd() const noexcept { return socket_.fd(); }
    [[nodiscard]] bool closed() const noexcept { return close_reason_ != LinkCloseReason::None; }
    /** @brief True once the close has been reported to the sink; the link can be removed. */
    [[nodiscard]] bool finished() const noexcept { return reported_closed_; }
    [[nodiscard]] bool connecting() const noexcept { return connecting_; }
    [[nodiscard]] apci::State state() const noexcept { return conn_.state(); }
    [[nodiscard]] bool can_send() const noexcept { return conn_.can_send(); }

  private:
    void read_input(apci::TimeMs now, LinkSink& sink) noexcept;
    void dispatch_events(LinkSink& sink) noexcept;
    void flush_output() noexcept;
    void finish(LinkCloseReason reason, LinkSink& sink) noexcept;

    std::uint32_t id_{0};
    Socket socket_;
    apci::Connection conn_;
    bool connecting_{false};
    apci::TimeMs connect_deadline_{0};
    LinkCloseReason close_reason_{LinkCloseReason::None};
    apci::CloseReason protocol_reason_{apci::CloseReason::None};
    bool reported_closed_{false};

    // Bytes taken from the state machine but not yet accepted by the socket.
    std::array<std::uint8_t, 8192> pending_{};
    std::size_t pending_fill_{0};
};

} // namespace iec104::transport
