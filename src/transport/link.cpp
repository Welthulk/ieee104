/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file link.cpp
 * @brief Implementation of a TCP connection with its APCI state machine.
 *
 * Layer: transport (I/O).
 */
#include "transport/link.hpp"

#include <cstring>
#include <poll.h>
#include <utility>

namespace iec104::transport {

iec104_status Link::init_accepted(std::uint32_t id, const apci::ApciConfig& config, Socket socket,
                                  apci::TimeMs now) noexcept {
    const iec104_status st = conn_.init(config);
    if (st != IEC104_OK) {
        return st;
    }
    id_ = id;
    socket_ = std::move(socket);
    connecting_ = false;
    return conn_.open(now);
}

iec104_status Link::init_connecting(std::uint32_t id, const apci::ApciConfig& config, Socket socket,
                                    apci::TimeMs now) noexcept {
    const iec104_status st = conn_.init(config);
    if (st != IEC104_OK) {
        return st;
    }
    id_ = id;
    socket_ = std::move(socket);
    connecting_ = true;
    // t0: time-out of connection establishment (docs/05, interoperability list).
    connect_deadline_ = now + config.t0_ms;
    return IEC104_OK;
}

short Link::poll_events() const noexcept {
    if (connecting_) {
        return POLLOUT; // writable = connect finished (successfully or not)
    }
    return static_cast<short>(POLLIN | (pending_fill_ > 0U ? POLLOUT : 0));
}

apci::TimeMs Link::next_deadline() const noexcept {
    if (closed()) {
        return 0; // service() at once to report the close
    }
    return connecting_ ? connect_deadline_ : conn_.next_deadline();
}

void Link::service(short revents, apci::TimeMs now, LinkSink& sink) noexcept {
    if (reported_closed_) {
        return;
    }
    if (close_reason_ != LinkCloseReason::None) {
        finish(close_reason_, sink); // recorded earlier, e.g. by a failed write in send_asdu()
        return;
    }
    if (connecting_) {
        if ((revents & (POLLOUT | POLLERR | POLLHUP)) != 0) {
            if (finish_connect(socket_) != IEC104_OK) {
                finish(LinkCloseReason::ConnectFailed, sink);
                return;
            }
            connecting_ = false;
            (void)conn_.open(now);
        } else if (now >= connect_deadline_) {
            finish(LinkCloseReason::T0Timeout, sink);
            return;
        } else {
            return;
        }
    }
    if ((revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        read_input(now, sink);
        if (reported_closed_) {
            return;
        }
    }
    conn_.tick(now);
    dispatch_events(sink);
    flush_output();
    // A close of the state machine or a failed write ends the link. Output
    // queued before a protocol close (rare) has been flushed above.
    if (conn_.state() == apci::State::Closed && close_reason_ == LinkCloseReason::None) {
        close_reason_ = LinkCloseReason::Protocol;
    }
    if (close_reason_ != LinkCloseReason::None) {
        finish(close_reason_, sink);
    }
}

void Link::read_input(apci::TimeMs now, LinkSink& sink) noexcept {
    std::array<std::uint8_t, 4096> buf{};
    for (;;) {
        std::size_t n = 0;
        const iec104_status st = read_some(socket_, buf.data(), buf.size(), n);
        if (st == IEC104_ERR_WOULD_BLOCK) {
            return;
        }
        if (st != IEC104_OK) {
            // Deliver what the state machine produced before the peer left.
            dispatch_events(sink);
            finish(st == IEC104_ERR_CLOSED ? LinkCloseReason::PeerClosed
                                           : LinkCloseReason::SocketError,
                   sink);
            return;
        }
        // receive() stops when its event queue is full; empty it and go on.
        std::size_t pos = 0;
        while (pos < n && conn_.state() != apci::State::Closed) {
            pos += conn_.receive(buf.data() + pos, n - pos, now);
            dispatch_events(sink);
        }
        if (conn_.state() == apci::State::Closed) {
            return;
        }
    }
}

void Link::dispatch_events(LinkSink& sink) noexcept {
    apci::Event e{};
    while (conn_.next_event(e)) {
        switch (e.kind) {
        case apci::EventKind::AsduReceived:
            sink.on_asdu(id_, e.asdu.data(), e.asdu_length);
            break;
        case apci::EventKind::StateChanged:
            sink.on_state(id_, e.state);
            break;
        case apci::EventKind::Acknowledged:
            break; // will feed the event buffer of redundancy groups
        case apci::EventKind::CloseRequested:
            protocol_reason_ = e.reason;
            break;
        }
    }
}

void Link::flush_output() noexcept {
    pending_fill_ +=
        conn_.take_output(pending_.data() + pending_fill_, pending_.size() - pending_fill_);
    std::size_t written = 0;
    const iec104_status st = write_some(socket_, pending_.data(), pending_fill_, written);
    if (st != IEC104_OK) {
        close_reason_ =
            st == IEC104_ERR_CLOSED ? LinkCloseReason::PeerClosed : LinkCloseReason::SocketError;
        return;
    }
    std::memmove(pending_.data(), pending_.data() + written, pending_fill_ - written);
    pending_fill_ -= written;
}

iec104_status Link::send_asdu(const std::uint8_t* asdu, std::size_t length,
                              apci::TimeMs now) noexcept {
    if (closed() || connecting_) {
        return IEC104_ERR_WRONG_STATE;
    }
    const iec104_status st = conn_.send_asdu(asdu, length, now);
    if (st == IEC104_OK) {
        flush_output(); // low latency: do not wait for the next poll round
    }
    return st;
}

void Link::finish(LinkCloseReason reason, LinkSink& sink) noexcept {
    if (reported_closed_) {
        return;
    }
    reported_closed_ = true;
    close_reason_ = reason;
    conn_.close(); // no further output from the state machine
    socket_.reset();
    sink.on_closed(id_, reason, protocol_reason_);
}

void Link::trace_thunk(void* context, bool outgoing, const std::uint8_t* apdu,
                       std::size_t length) noexcept {
    // context is the Link itself (set in set_trace(); links live in unique_ptr,
    // so the address stays valid).
    const auto* link = static_cast<const Link*>(context);
    if (link->trace_sink_ != nullptr) {
        link->trace_sink_->on_frame(link->id_, outgoing, apdu, length);
    }
}

} // namespace iec104::transport
