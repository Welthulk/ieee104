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

#include "api/values.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <poll.h>
#include <utility>

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
    case transport::LinkCloseReason::TlsError:
        return IEC104_CLOSE_TLS;
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

void CallbackSink::on_frame(std::uint32_t id, bool outgoing, const std::uint8_t* apdu,
                            std::size_t length) {
    if (logger_.enabled(IEC104_LOG_TRACE)) {
        logger_.write(IEC104_LOG_TRACE, id,
                      std::string(outgoing ? "sent " : "received ") + Logger::hex(apdu, length));
    }
}

void CallbackSink::on_tls(std::uint32_t id, transport::TlsEvent event, const std::string& text) {
    iec104_security_event e = IEC104_SECURITY_TLS_ERROR;
    iec104_log_level level = IEC104_LOG_ERROR;
    switch (event) {
    case transport::TlsEvent::Established:
        e = IEC104_SECURITY_TLS_ESTABLISHED;
        level = IEC104_LOG_INFO;
        break;
    case transport::TlsEvent::HandshakeFailed:
        e = IEC104_SECURITY_HANDSHAKE_FAILED;
        break;
    case transport::TlsEvent::CertificateRejected:
        e = IEC104_SECURITY_CERTIFICATE_REJECTED;
        break;
    case transport::TlsEvent::Error:
        e = IEC104_SECURITY_TLS_ERROR;
        break;
    case transport::TlsEvent::Renewal:
        e = IEC104_SECURITY_KEY_RENEWAL;
        level = IEC104_LOG_DEBUG;
        break;
    }
    logger_.write(level, id, std::string(iec104_security_event_string(e)) + ": " + text);
    if (callbacks_.on_security != nullptr) {
        callbacks_.on_security(callbacks_.user, id, e, text.c_str());
    }
}

namespace {

// Copies a C string field of bounded size (the field may lack the NUL).
template <std::size_t N>
std::string field(const char (&text)[N]) { // NOLINT(*-avoid-c-arrays): field of a C struct
    const auto* begin = static_cast<const char*>(text);
    const auto* end = static_cast<const char*>(std::memchr(begin, '\0', N));
    return {begin, end != nullptr ? static_cast<std::size_t>(end - begin) : N};
}

template <std::size_t N>
void put(char (&out)[N], // NOLINT(*-avoid-c-arrays): field of a C struct
         const std::string& text) noexcept {
    const std::size_t n = std::min(text.size(), N - 1U);
    std::memcpy(static_cast<char*>(out), text.data(), n);
    out[n] = '\0';
}

transport::TlsSettings to_settings(const iec104_tls_config& c) {
    transport::TlsSettings t;
    t.ca_file = field(c.ca_file);
    t.certificate_file = field(c.certificate_file);
    t.private_key_file = field(c.private_key_file);
    t.key_password = field(c.key_password);
    t.crl_file = field(c.crl_file);
    t.peer_name = field(c.peer_name);
    t.peer_fingerprint = field(c.peer_fingerprint);
    t.cipher_list = field(c.cipher_list);
    t.cipher_suites = field(c.cipher_suites);
    t.min_version = c.min_version;
    t.max_version = c.max_version;
    t.verify_peer = c.verify_peer != 0;
    t.renegotiation = c.renegotiation != 0;
    t.renewal_interval_ms =
        c.renewal_interval_s > UINT32_MAX / 1000U ? UINT32_MAX : c.renewal_interval_s * 1000U;
    t.session_resumption = c.session_resumption != 0;
    t.session_lifetime_s = c.session_lifetime_s;
    return t;
}

} // namespace

iec104_status make_tls_context(transport::TlsRole role, const iec104_tls_config* config,
                               const Logger& logger, std::shared_ptr<transport::TlsContext>& out) {
    if (config == nullptr) {
        out.reset();
        logger.write(IEC104_LOG_INFO, 0, "TLS off for new connections");
        return IEC104_OK;
    }
    std::shared_ptr<transport::TlsContext> ctx;
    std::string error;
    const iec104_status st = transport::create_tls_context(role, to_settings(*config), ctx, error);
    if (st != IEC104_OK) {
        logger.write(IEC104_LOG_ERROR, 0, "TLS settings: " + error);
        return st;
    }
    if (config->verify_peer == 0) {
        logger.write(IEC104_LOG_WARNING, 0, "TLS: peer is not verified (verify_peer = 0)");
    }
    out = std::move(ctx);
    logger.write(IEC104_LOG_INFO, 0, "TLS on for new connections");
    return IEC104_OK;
}

bool copy_tls_info(const transport::Link& link, iec104_tls_info& out) noexcept {
    transport::TlsInfo info;
    if (!link.tls_info(info)) {
        return false;
    }
    out = iec104_tls_info{};
    put(out.version, info.version);
    put(out.cipher, info.cipher);
    put(out.peer_subject, info.peer_subject);
    put(out.peer_fingerprint, info.peer_fingerprint);
    out.resumed = info.resumed ? 1 : 0;
    return true;
}

void log_config_warnings(const apci::ApciConfig& config, const Logger& logger) {
    apci::ConfigWarnings w{};
    if (apci::validate(config, w) != IEC104_OK) {
        return;
    }
    if (w.t2_not_below_t1) {
        logger.write(IEC104_LOG_WARNING, 0, "configuration: t2 is not below t1");
    }
    if (w.w_above_two_thirds_k) {
        logger.write(IEC104_LOG_WARNING, 0, "configuration: w is above 2/3 of k");
    }
}

void copy_link_stats(const transport::Link& link, iec104_link_stats& out) noexcept {
    const apci::ConnectionStats& s = link.stats();
    out.i_frames_sent = s.i_sent;
    out.i_frames_received = s.i_received;
    out.s_frames_sent = s.s_sent;
    out.s_frames_received = s.s_received;
    out.u_frames_sent = s.u_sent;
    out.u_frames_received = s.u_received;
    out.bytes_sent = s.bytes_sent;
    out.bytes_received = s.bytes_received;
}

void CallbackSink::on_state(std::uint32_t id, apci::State state) {
    // Closed is reported once, with its reason, by on_closed().
    if (state != apci::State::Closed) {
        const bool important = state == apci::State::Started || state == apci::State::Stopped;
        logger_.write(important ? IEC104_LOG_INFO : IEC104_LOG_DEBUG, id,
                      std::string("state ") + iec104_link_state_string(to_c(state)));
    }
    if (callbacks_.on_state != nullptr && state != apci::State::Closed) {
        callbacks_.on_state(callbacks_.user, id, to_c(state), IEC104_CLOSE_NONE);
    }
}

void CallbackSink::on_closed(std::uint32_t id, transport::LinkCloseReason reason,
                             apci::CloseReason protocol_reason) {
    const iec104_close_reason c = to_c(reason, protocol_reason);
    // A regular end is information; everything else means the link failed.
    const bool regular = c == IEC104_CLOSE_LOCAL || c == IEC104_CLOSE_PEER;
    logger_.write(regular ? IEC104_LOG_INFO : IEC104_LOG_ERROR, id,
                  std::string("closed: ") + iec104_close_reason_string(c));
    if (callbacks_.on_state != nullptr) {
        callbacks_.on_state(callbacks_.user, id, IEC104_LINK_CLOSED, to_c(reason, protocol_reason));
    }
}

void CallbackSink::deliver_objects(std::uint32_t id, const std::uint8_t* asdu, std::size_t length,
                                   const codec::AsduProfile& profile) const {
    if (callbacks_.on_object == nullptr) {
        return;
    }
    std::array<codec::InformationObject, codec::kMaxObjects> objects{};
    codec::DecodedAsdu d{};
    if (codec::decode_asdu(asdu, length, profile, objects.data(), objects.size(), d) != IEC104_OK ||
        d.type == nullptr || !value_type_supported(d.header.type_id)) {
        return;
    }
    const iec104_asdu_info info = to_info(d.header);
    for (std::size_t i = 0; i < d.object_count; ++i) {
        iec104_object object{};
        if (from_codec(d.header.type_id, objects[i], object)) {
            callbacks_.on_object(callbacks_.user, id, &info, &object);
        }
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
