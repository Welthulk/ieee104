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

#include <algorithm>
#include <cstring>
#include <poll.h>
#include <utility>

namespace iec104::transport {

iec104_status Link::init_accepted(std::uint32_t id, const apci::ApciConfig& config, Socket socket,
                                  apci::TimeMs now, std::shared_ptr<TlsContext> tls) noexcept {
    const iec104_status st = conn_.init(config);
    if (st != IEC104_OK) {
        return st;
    }
    id_ = id;
    socket_ = std::move(socket);
    tls_context_ = std::move(tls);
    // t0 also limits the TLS handshake of an accepted connection (docs/05, E9).
    setup_deadline_ = now + config.t0_ms;
    return start_tls_or_open(now);
}

iec104_status Link::init_connecting(std::uint32_t id, const apci::ApciConfig& config, Socket socket,
                                    apci::TimeMs now, std::shared_ptr<TlsContext> tls) noexcept {
    const iec104_status st = conn_.init(config);
    if (st != IEC104_OK) {
        return st;
    }
    id_ = id;
    socket_ = std::move(socket);
    tls_context_ = std::move(tls);
    phase_ = Phase::Connecting;
    // t0: time-out of connection establishment (docs/05, interoperability
    // list), including the TLS handshake (E9).
    setup_deadline_ = now + config.t0_ms;
    return IEC104_OK;
}

iec104_status Link::start_tls_or_open(apci::TimeMs now) noexcept {
    if (tls_context_ == nullptr) {
        phase_ = Phase::Open;
        return conn_.open(now);
    }
    phase_ = Phase::Handshaking;
    return tls_context_->create_session(socket_, tls_);
}

short Link::poll_events() const noexcept {
    switch (phase_) {
    case Phase::Connecting:
        return POLLOUT; // writable = connect finished (successfully or not)
    case Phase::Handshaking:
        return tls_->poll_events();
    case Phase::Open:
        break;
    }
    const bool tls_wants_write = tls_ != nullptr && (tls_->poll_events() & POLLOUT) != 0;
    return static_cast<short>(POLLIN | (pending_fill_ > 0U || tls_wants_write ? POLLOUT : 0));
}

apci::TimeMs Link::next_deadline() const noexcept {
    if (closed()) {
        return 0; // service() at once to report the close
    }
    if (phase_ != Phase::Open) {
        return setup_deadline_;
    }
    return std::min(conn_.next_deadline(), next_renewal_);
}

bool Link::tls_info(TlsInfo& out) const {
    if (tls_ == nullptr || phase_ != Phase::Open || closed()) {
        return false;
    }
    out = tls_->info();
    return true;
}

void Link::service(short revents, apci::TimeMs now, LinkSink& sink) noexcept {
    if (reported_closed_) {
        return;
    }
    if (close_reason_ != LinkCloseReason::None) {
        finish(close_reason_, sink); // recorded earlier, e.g. by a failed write in send_asdu()
        return;
    }
    if (phase_ == Phase::Connecting && !continue_connect(revents, now, sink)) {
        return; // still connecting, or closed
    }
    bool just_opened = false;
    if (phase_ == Phase::Handshaking) {
        if (!continue_handshake(now, sink)) {
            return; // still running, or closed
        }
        just_opened = true;
    }
    // TLS: data that arrived with the end of the handshake is already
    // buffered inside the TLS session, and a renegotiation may need a
    // writable socket to go on reading.
    const bool readable = (revents & (POLLIN | POLLHUP | POLLERR)) != 0;
    if (readable || (tls_ != nullptr && (just_opened || (revents & POLLOUT) != 0))) {
        read_input(now, sink);
        if (reported_closed_) {
            return;
        }
    }
    if (tls_ != nullptr && now >= next_renewal_) {
        renew_keys(now, sink);
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

bool Link::continue_connect(short revents, apci::TimeMs now, LinkSink& sink) noexcept {
    if ((revents & (POLLOUT | POLLERR | POLLHUP)) == 0) {
        if (now >= setup_deadline_) {
            finish(LinkCloseReason::T0Timeout, sink);
        }
        return false;
    }
    if (finish_connect(socket_) != IEC104_OK) {
        finish(LinkCloseReason::ConnectFailed, sink);
        return false;
    }
    if (start_tls_or_open(now) != IEC104_OK) {
        sink.on_tls(id_, TlsEvent::HandshakeFailed, "cannot create TLS session");
        finish(LinkCloseReason::TlsError, sink);
        return false;
    }
    return true;
}

bool Link::continue_handshake(apci::TimeMs now, LinkSink& sink) noexcept {
    const iec104_status st = tls_->handshake();
    if (st == IEC104_ERR_WOULD_BLOCK) {
        if (now >= setup_deadline_) {
            sink.on_tls(id_, TlsEvent::HandshakeFailed, "handshake not complete within t0");
            finish(LinkCloseReason::T0Timeout, sink);
        }
        return false;
    }
    if (st != IEC104_OK) {
        sink.on_tls(id_,
                    tls_->failure() == TlsFailure::Certificate ? TlsEvent::CertificateRejected
                                                               : TlsEvent::HandshakeFailed,
                    tls_->error());
        finish(LinkCloseReason::TlsError, sink);
        return false;
    }
    phase_ = Phase::Open;
    const TlsInfo info = tls_->info();
    std::string text = info.version + ", " + info.cipher;
    if (!info.peer_subject.empty()) {
        text += ", peer " + info.peer_subject;
    }
    if (info.resumed) {
        text += ", resumed";
    }
    sink.on_tls(id_, TlsEvent::Established, text);
    const std::uint32_t interval = tls_context_->renewal_interval_ms();
    next_renewal_ = interval > 0U ? now + interval : apci::kNoDeadline;
    (void)conn_.open(now);
    return true;
}

void Link::renew_keys(apci::TimeMs now, LinkSink& sink) noexcept {
    next_renewal_ = now + tls_context_->renewal_interval_ms();
    const iec104_status st = tls_->renew();
    if (st == IEC104_OK) {
        sink.on_tls(id_, TlsEvent::Renewal,
                    tls_->info().version == "TLSv1.3" ? "key update" : "renegotiation");
    } else if (st == IEC104_ERR_IO) {
        sink.on_tls(id_, TlsEvent::Error, tls_->error());
        close_reason_ = LinkCloseReason::TlsError;
    } else if (st == IEC104_ERR_UNSUPPORTED) {
        next_renewal_ = apci::kNoDeadline; // renegotiation switched off: do not try again
    }
}

iec104_status Link::io_read(std::uint8_t* buf, std::size_t size, std::size_t& n) noexcept {
    return tls_ != nullptr ? tls_->read(buf, size, n) : read_some(socket_, buf, size, n);
}

iec104_status Link::io_write(const std::uint8_t* data, std::size_t size, std::size_t& n) noexcept {
    if (tls_ == nullptr) {
        return write_some(socket_, data, size, n);
    }
    const iec104_status st = tls_->write(data, size, n);
    return st == IEC104_ERR_WOULD_BLOCK ? IEC104_OK : st; // nothing written yet, as with TCP
}

LinkCloseReason Link::io_failure(iec104_status st) const noexcept {
    if (st == IEC104_ERR_CLOSED) {
        return LinkCloseReason::PeerClosed;
    }
    return tls_ != nullptr ? LinkCloseReason::TlsError : LinkCloseReason::SocketError;
}

void Link::read_input(apci::TimeMs now, LinkSink& sink) noexcept {
    std::array<std::uint8_t, 4096> buf{};
    for (;;) {
        std::size_t n = 0;
        const iec104_status st = io_read(buf.data(), buf.size(), n);
        if (st == IEC104_ERR_WOULD_BLOCK) {
            return;
        }
        if (st != IEC104_OK) {
            // Deliver what the state machine produced before the peer left.
            dispatch_events(sink);
            const LinkCloseReason reason = io_failure(st);
            if (reason == LinkCloseReason::TlsError) {
                sink.on_tls(id_, TlsEvent::Error, tls_->error());
            }
            finish(reason, sink);
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
            sink.on_acknowledged(id_, e.count); // frees events of the redundancy group
            break;
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
    const iec104_status st = io_write(pending_.data(), pending_fill_, written);
    if (st != IEC104_OK) {
        close_reason_ = io_failure(st);
        return;
    }
    std::memmove(pending_.data(), pending_.data() + written, pending_fill_ - written);
    pending_fill_ -= written;
}

iec104_status Link::send_asdu(const std::uint8_t* asdu, std::size_t length,
                              apci::TimeMs now) noexcept {
    if (closed() || phase_ != Phase::Open) {
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
    // Orderly TLS end (close_notify) unless the connection is already broken.
    if (tls_ != nullptr && phase_ == Phase::Open && reason != LinkCloseReason::PeerClosed &&
        reason != LinkCloseReason::SocketError && reason != LinkCloseReason::TlsError) {
        tls_->shutdown();
    }
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
