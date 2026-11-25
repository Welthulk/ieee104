/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file link_common.cpp
 * @brief Conversions and poll loop shared by server and client (see link_common.hpp).
 *
 * Layer: API.
 */
#include "api/link_common.hpp"

#include <algorithm>
#include <cerrno>
#include <poll.h>

namespace iec104::api {

apci::ApciConfig to_apci(const iec104_link_config& c, apci::Role role) noexcept {
    apci::ApciConfig a{};
    a.role = role;
    a.t0_ms = c.t0_ms;
    a.t1_ms = c.t1_ms;
    a.t2_ms = c.t2_ms;
    a.t3_ms = c.t3_ms;
    a.k = c.k;
    a.w = c.w;
    a.strict = c.tolerant == 0;
    a.plausibility = apci::PlausibilityCheck::Warn;
    return a;
}

iec104_link_state to_c(apci::State state) noexcept {
    switch (state) {
    case apci::State::Idle:
        return IEC104_LINK_CONNECTING;
    case apci::State::Stopped:
        return IEC104_LINK_STOPPED;
    case apci::State::StartPending:
        return IEC104_LINK_START_PENDING;
    case apci::State::Started:
        return IEC104_LINK_STARTED;
    case apci::State::StopPending:
        return IEC104_LINK_STOP_PENDING;
    case apci::State::Closed:
        return IEC104_LINK_CLOSED;
    }
    return IEC104_LINK_CLOSED;
}

iec104_close_reason to_c(transport::LinkCloseReason reason, apci::CloseReason protocol) noexcept {
    switch (reason) {
    case transport::LinkCloseReason::None:
        return IEC104_CLOSE_NONE;
    case transport::LinkCloseReason::PeerClosed:
        return IEC104_CLOSE_PEER;
    case transport::LinkCloseReason::ConnectFailed:
        return IEC104_CLOSE_CONNECT_FAILED;
    case transport::LinkCloseReason::T0Timeout:
        return IEC104_CLOSE_T0_TIMEOUT;
    case transport::LinkCloseReason::SocketError:
        return IEC104_CLOSE_SOCKET_ERROR;
    case transport::LinkCloseReason::Protocol:
        break;
    }
    switch (protocol) {
    case apci::CloseReason::T1Timeout:
        return IEC104_CLOSE_T1_TIMEOUT;
    case apci::CloseReason::SequenceError:
        return IEC104_CLOSE_SEQUENCE_ERROR;
    case apci::CloseReason::AckOutOfWindow:
        return IEC104_CLOSE_ACK_OUT_OF_WINDOW;
    case apci::CloseReason::MalformedFrame:
        return IEC104_CLOSE_MALFORMED;
    case apci::CloseReason::UnexpectedFrame:
        return IEC104_CLOSE_UNEXPECTED_FRAME;
    case apci::CloseReason::OutputOverflow:
        return IEC104_CLOSE_SOCKET_ERROR;
    case apci::CloseReason::LocalRequest:
    case apci::CloseReason::None:
        break;
    }
    return IEC104_CLOSE_LOCAL;
}

CallbackSink::CallbackSink(const iec104_callbacks* callbacks) noexcept {
    if (callbacks != nullptr) {
        callbacks_ = *callbacks;
    }
}

void CallbackSink::on_asdu(std::uint32_t id, const std::uint8_t* asdu, std::size_t length) {
    if (callbacks_.on_asdu != nullptr) {
        callbacks_.on_asdu(callbacks_.user, id, asdu, length);
    }
}

void CallbackSink::on_state(std::uint32_t id, apci::State state) {
    // Closed is reported once, with its reason, by on_closed().
    if (callbacks_.on_state != nullptr && state != apci::State::Closed) {
        callbacks_.on_state(callbacks_.user, id, to_c(state), IEC104_CLOSE_NONE);
    }
}

void CallbackSink::on_closed(std::uint32_t id, transport::LinkCloseReason reason,
                             apci::CloseReason protocol_reason) {
    if (callbacks_.on_state != nullptr) {
        callbacks_.on_state(callbacks_.user, id, IEC104_LINK_CLOSED, to_c(reason, protocol_reason));
    }
}

iec104_status poll_links(std::vector<std::unique_ptr<transport::Link>>& links,
                         const transport::Socket* listener, int timeout_ms,
                         transport::LinkSink& sink, short& listener_revents) noexcept {
    std::vector<pollfd> fds;
    fds.reserve(links.size() + 1U);
    if (listener != nullptr && listener->valid()) {
        fds.push_back(pollfd{listener->fd(), POLLIN, 0});
    }
    const std::size_t first_link = fds.size();

    // Wake up for the earliest timer of any link.
    const apci::TimeMs now = transport::monotonic_ms();
    apci::TimeMs deadline = apci::kNoDeadline;
    for (const auto& link : links) {
        fds.push_back(pollfd{link->fd(), link->poll_events(), 0});
        deadline = std::min(deadline, link->next_deadline());
    }
    int wait = timeout_ms < 0 ? 0 : timeout_ms;
    if (deadline != apci::kNoDeadline) {
        const apci::TimeMs until = deadline > now ? deadline - now : 0U;
        wait = static_cast<int>(std::min<apci::TimeMs>(static_cast<apci::TimeMs>(wait), until));
    }

    const int rc = ::poll(fds.data(), fds.size(), wait);
    if (rc < 0 && errno != EINTR) {
        return IEC104_ERR_IO;
    }

    listener_revents = first_link > 0U ? fds[0].revents : 0;
    const apci::TimeMs after = transport::monotonic_ms();
    // Links are serviced even without socket events: their timers run.
    for (std::size_t i = 0; i < links.size(); ++i) {
        const short revents = rc > 0 ? fds[first_link + i].revents : 0;
        links[i]->service(revents, after, sink);
    }
    links.erase(
        std::remove_if(links.begin(), links.end(),
                       [](const std::unique_ptr<transport::Link>& l) { return l->finished(); }),
        links.end());
    return IEC104_OK;
}

} // namespace iec104::api
