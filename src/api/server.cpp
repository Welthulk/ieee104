/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file server.cpp
 * @brief C API of the server (controlled station), see iec104.h.
 *
 * A server owns one listening socket, any number of connections (links),
 * the process image and one application-layer session per connection.
 * Every accepted connection gets an id (1, 2, ...) that the callbacks and
 * the send functions use. Connections start in STOPPED; the controlling
 * station starts data transfer with STARTDT, which the state machine
 * confirms by itself. Requests (interrogation, commands, ...) are answered
 * by the session (src/session/server_session.hpp).
 *
 * Spontaneous data (iec104_server_update(), iec104_server_send() to all)
 * goes through the event buffer of a redundancy group
 * (src/session/redundancy_group.hpp): every connection belongs to one group,
 * only the active connection of a group receives the events, and an event
 * stays buffered until the peer has acknowledged it. Without groups added by
 * the application, every connection is a group of its own.
 *
 * Layer: API.
 */
#include "api/link_common.hpp"
#include "api/values.hpp"
#include "session/command_table.hpp"
#include "session/process_image.hpp"
#include "session/redundancy_group.hpp"
#include "session/server_session.hpp"

#include <array>
#include <cstring>
#include <deque>
#include <map>
#include <new>
#include <poll.h>
#include <string>

using namespace iec104;

namespace {

constexpr std::uint8_t kCotPeriodic = 1;
constexpr std::uint8_t kCotBackground = 2;

/** @brief Application layer of one connection plus what its unacknowledged I-frames carry. */
struct Peer {
    explicit Peer(session::ServerSession s) noexcept : session(std::move(s)) {}
    session::ServerSession session;
    // One entry per sent, unacknowledged I-frame, oldest first: the id of
    // the buffered event it carried, 0 for anything else. Acknowledgements
    // (N(R)) are counted, so every I-frame of the link must be recorded.
    std::deque<std::uint64_t> in_flight;
    session::RedundancyGroup* group{nullptr};
    std::uint32_t group_id{0};
};

/** @brief Sends an ASDU on @p link and records the I-frame for @p peer. */
iec104_status transmit(transport::Link& link, Peer& peer, const std::uint8_t* asdu,
                       std::size_t length) noexcept {
    const iec104_status st = link.send_asdu(asdu, length, transport::monotonic_ms());
    if (st == IEC104_OK) {
        peer.in_flight.push_back(0);
    }
    return st;
}

/** @brief Lets a session send over a link; marks buffered events as sent. */
class LinkSender : public session::AsduSender {
  public:
    LinkSender(transport::Link& link, Peer& peer) noexcept : link_(link), peer_(peer) {}
    bool can_send() override { return link_.can_send(); }
    iec104_status send(const std::uint8_t* asdu, std::size_t length) override {
        return transmit(link_, peer_, asdu, length);
    }
    void sent_event(std::uint64_t id) override {
        if (!peer_.in_flight.empty()) {
            peer_.in_flight.back() = id; // the I-frame just recorded by send()
        }
        if (peer_.group != nullptr) {
            peer_.group->sent(id);
        }
    }

  private:
    transport::Link& link_;
    Peer& peer_;
};

/** @brief IP address of a peer address "ip:port" / "[ipv6]:port" (IPv4-mapped as IPv4). */
std::string address_of(const std::string& peer) {
    std::string ip = peer.substr(0, peer.rfind(':'));
    if (ip.size() >= 2U && ip.front() == '[' && ip.back() == ']') {
        ip = ip.substr(1, ip.size() - 2U);
    }
    const std::string mapped = "::ffff:";
    if (ip.compare(0, mapped.size(), mapped) == 0 && ip.find('.') != std::string::npos) {
        ip = ip.substr(mapped.size());
    }
    return ip;
}

} // namespace

struct iec104_server : api::CallbackSink, session::CommandHandler, session::SessionObserver {
    iec104_server(const apci::ApciConfig& c, const iec104_link_config& link,
                  const iec104_callbacks* cb) noexcept
        : api::CallbackSink(cb), config(c), profile(api::to_profile(link)),
          originator(link.originator) {}

    apci::ApciConfig config;
    codec::AsduProfile profile;
    std::uint8_t originator;
    transport::Socket listener;
    std::shared_ptr<transport::TlsContext> tls; // NULL = plain TCP; for new connections
    std::vector<std::unique_ptr<transport::Link>> links;
    std::map<std::uint32_t, Peer> peers;
    session::ProcessImage image;
    session::CommandTable commands;
    session::SessionOptions options;
    std::uint32_t next_id{1};
    std::uint32_t current_connection{0}; // connection of the request being handled

    // Redundancy groups by id. Groups added by the application live as long
    // as the server; without them every connection gets a group of its own
    // (same id space), which ends with the connection.
    std::map<std::uint32_t, std::unique_ptr<session::RedundancyGroup>> groups;
    bool configured_groups{false};
    std::uint32_t next_group{1};
    session::GroupConfig own_group; // settings of a connection's own group (server options)
    std::map<std::uint32_t, bool> overflowing; // group id -> buffer full reported

    transport::Link* find(std::uint32_t id) noexcept {
        for (auto& link : links) {
            if (link->id() == id) {
                return link.get();
            }
        }
        return nullptr;
    }

    // Accepts all pending connections.
    void accept_pending() noexcept {
        for (;;) {
            transport::Socket s;
            if (transport::accept_tcp(listener, s) != IEC104_OK) {
                return;
            }
            const std::string from = transport::peer_address(s);
            const std::uint32_t group = assign_group(address_of(from));
            if (group == 0U) {
                // Not a controlling station of any group (also an address allow-list).
                logger().write(IEC104_LOG_WARNING, 0,
                               "connection from " + from +
                                   " rejected: not in any redundancy group");
                continue; // the socket closes here
            }
            auto link = std::make_unique<transport::Link>();
            if (link->init_accepted(next_id, config, std::move(s), transport::monotonic_ms(),
                                    tls) != IEC104_OK) {
                drop_own_group(group);
                continue;
            }
            link->set_trace(this);
            logger().write(IEC104_LOG_INFO, next_id,
                           "connection accepted from " + from +
                               (configured_groups ? ", group " + std::to_string(group) : ""));
            Peer& peer = peers.emplace(next_id, Peer(session::ServerSession(profile, originator)))
                             .first->second;
            peer.group = groups.at(group).get();
            peer.group_id = group;
            peer.group->join(next_id);
            ++next_id;
            links.push_back(std::move(link));
        }
    }

    // Sends what the sessions have queued, as far as the windows allow.
    void pump_all() noexcept {
        for (auto& link : links) {
            const auto it = peers.find(link->id());
            if (it != peers.end() && !link->closed()) {
                LinkSender sender(*link, it->second);
                it->second.session.pump(sender);
            }
        }
    }

    // --- Redundancy groups ----------------------------------------------------

    // Group of a new connection from @p address; 0 = reject.
    std::uint32_t assign_group(const std::string& address) {
        if (!configured_groups) {
            const std::uint32_t id = next_group++;
            groups.emplace(id, std::make_unique<session::RedundancyGroup>(own_group));
            return id;
        }
        // An explicit address match wins over a group open to any address.
        std::uint32_t any = 0;
        for (const auto& [id, group] : groups) {
            if (group->config().peers.empty()) {
                any = any != 0U ? any : id;
            } else if (group->accepts(address)) {
                return id;
            }
        }
        return any;
    }

    // Removes the own group of a connection that is gone.
    void drop_own_group(std::uint32_t group) {
        if (!configured_groups) {
            groups.erase(group);
            overflowing.erase(group);
        }
    }

    // Hands the buffered events of @p group to its active connection.
    void deliver(std::uint32_t group_id, session::RedundancyGroup& group) {
        const std::uint32_t active = group.active();
        const auto it = peers.find(active);
        if (it == peers.end()) {
            return;
        }
        const std::vector<const session::BufferedEvent*> events = group.queue_on_active();
        for (const session::BufferedEvent* e : events) {
            it->second.session.enqueue_event(e->asdu.data(), e->asdu.size(), e->id);
        }
        logger().write(IEC104_LOG_INFO, active,
                       "group " + std::to_string(group_id) + ": active, " +
                           std::to_string(events.size()) + " buffered events");
    }

    // Takes the queued events of @p connection back into its group.
    void withdraw(std::uint32_t connection) {
        const auto it = peers.find(connection);
        if (it != peers.end()) {
            it->second.session.drop_events();
        }
    }

    // Puts an event into every group (spontaneous data to all connections).
    iec104_status publish(const std::uint8_t* asdu, std::size_t length, std::uint8_t cause) {
        if (groups.empty()) {
            return IEC104_ERR_WRONG_STATE; // nobody to send to, nothing to keep
        }
        // Cyclic and background data are not buffered: only the active connections get them.
        // TODO(norm): clause 10, which causes are buffered.
        if (cause == kCotPeriodic || cause == kCotBackground) {
            bool sent = false;
            for (const auto& [id, group] : groups) {
                const auto it = peers.find(group->active());
                if (it != peers.end()) {
                    sent = it->second.session.enqueue(asdu, length) == IEC104_OK || sent;
                }
            }
            return sent ? IEC104_OK : IEC104_ERR_WRONG_STATE;
        }
        bool rejected = false;
        for (const auto& [id, group] : groups) {
            rejected = add_event(id, *group, asdu, length) != IEC104_OK || rejected;
        }
        return rejected ? IEC104_ERR_WOULD_BLOCK : IEC104_OK;
    }

    // Puts an event into one group and queues it on the group's active connection.
    iec104_status add_event(std::uint32_t id, session::RedundancyGroup& group,
                            const std::uint8_t* asdu, std::size_t length) {
        const session::RedundancyGroup::Added a = group.add(asdu, length);
        if (a.dropped && a.dropped_queued_on != 0U) {
            const auto it = peers.find(a.dropped_queued_on);
            if (it != peers.end()) {
                it->second.session.drop_event(a.dropped_id);
            }
        }
        if (a.status != IEC104_OK || a.dropped) {
            report_overflow(id, group);
        }
        if (a.status != IEC104_OK) {
            return a.status;
        }
        const auto it = peers.find(group.active());
        if (it != peers.end()) {
            for (const session::BufferedEvent* e : group.queue_on_active()) {
                it->second.session.enqueue_event(e->asdu.data(), e->asdu.size(), e->id);
            }
        }
        return IEC104_OK;
    }

    void report_overflow(std::uint32_t id, const session::RedundancyGroup& group) {
        bool& reported = overflowing[id];
        if (!reported) {
            reported = true;
            logger().write(IEC104_LOG_WARNING, group.active(),
                           "group " + std::to_string(id) + ": event buffer full (" +
                               std::to_string(group.config().capacity) + "), " +
                               (group.config().overflow == session::Overflow::DropOldest
                                    ? "oldest events dropped"
                                    : "new events rejected"));
        }
    }

    void check_overflow_end(std::uint32_t id, const session::RedundancyGroup& group) {
        const auto it = overflowing.find(id);
        if (it != overflowing.end() && it->second &&
            group.stats().buffered < group.config().capacity) {
            it->second = false;
            const session::GroupStats st = group.stats();
            logger().write(IEC104_LOG_INFO, group.active(),
                           "group " + std::to_string(id) + ": event buffer has room again (" +
                               std::to_string(st.dropped + st.rejected) + " events lost so far)");
        }
    }

    // --- LinkSink: events of the links --------------------------------------

    void on_asdu(std::uint32_t id, const std::uint8_t* asdu, std::size_t length) override {
        api::CallbackSink::on_asdu(id, asdu, length);
        const auto it = peers.find(id);
        current_connection = id;
        const session::StationContext station{
            &image, &commands, &options,
            session::SessionClock{transport::monotonic_ms(), transport::utc_ms()}, this};
        const bool handled =
            it != peers.end() && it->second.session.handle(asdu, length, station, *this);
        current_connection = 0;
        if (!handled) {
            deliver_objects(id, asdu, length, profile);
        }
    }

    void on_state(std::uint32_t id, apci::State state) override {
        api::CallbackSink::on_state(id, state);
        const auto it = peers.find(id);
        if (it == peers.end() || it->second.group == nullptr) {
            return;
        }
        Peer& peer = it->second;
        session::RedundancyGroup& group = *peer.group;
        if (state == apci::State::Started) {
            peer.session.on_started(image, commands, options); // end of initialization first
            const std::uint32_t previous = group.start(id);
            if (previous != 0U) {
                withdraw(previous);
                logger().write(IEC104_LOG_WARNING, id,
                               "group " + std::to_string(peer.group_id) +
                                   ": takes over from connection " + std::to_string(previous) +
                                   " (STARTDT without STOPDT on the old connection)");
            }
            deliver(peer.group_id, group);
        } else if (state == apci::State::Stopped && group.active() == id) {
            withdraw(id);
            if (group.stop(id) != 0U) {
                deliver(peer.group_id, group);
            }
        }
    }

    void on_acknowledged(std::uint32_t id, std::uint16_t count) override {
        const auto it = peers.find(id);
        if (it == peers.end()) {
            return;
        }
        Peer& peer = it->second;
        for (std::uint16_t i = 0; i < count && !peer.in_flight.empty(); ++i) {
            const std::uint64_t event = peer.in_flight.front();
            peer.in_flight.pop_front();
            if (event != 0U && peer.group != nullptr) {
                peer.group->acknowledged(event);
            }
        }
        if (peer.group != nullptr) {
            check_overflow_end(peer.group_id, *peer.group);
        }
    }

    void on_closed(std::uint32_t id, transport::LinkCloseReason reason,
                   apci::CloseReason protocol_reason) override {
        api::CallbackSink::on_closed(id, reason, protocol_reason);
        const auto it = peers.find(id);
        if (it == peers.end()) {
            return;
        }
        const std::uint32_t group_id = it->second.group_id;
        session::RedundancyGroup* group = it->second.group;
        peers.erase(it); // its queued events are gone with the session
        if (group != nullptr) {
            const bool was_active = group->active() == id;
            const std::uint32_t now_active = group->leave(id);
            if (was_active && now_active != 0U) {
                deliver(group_id, *group);
            }
            if (!configured_groups) {
                drop_own_group(group_id);
            }
        }
    }

    // --- CommandHandler: the application decides --------------------------

    bool on_command(const codec::AsduHeader& header,
                    const codec::InformationObject& object) override {
        if (callbacks().on_command == nullptr) {
            return false; // no handler: every command is rejected
        }
        iec104_object c{};
        if (!api::from_codec(header.type_id, object, c)) {
            return false;
        }
        const iec104_asdu_info info = api::to_info(header);
        const bool accepted =
            callbacks().on_command(callbacks().user, current_connection, &info, &c) != 0;
        logger().write(IEC104_LOG_INFO, current_connection,
                       "command type " + std::to_string(header.type_id) + " ioa " +
                           std::to_string(object.ioa) + (accepted ? ": accepted" : ": rejected"));
        return accepted;
    }

    // --- SessionObserver: notable events for the log ----------------------

    void negative_answer(const std::uint8_t* asdu, std::size_t length,
                         std::uint8_t cause) override {
        if (logger().enabled(IEC104_LOG_WARNING) && length > 0U) {
            logger().write(IEC104_LOG_WARNING, current_connection,
                           "negative answer to type " + std::to_string(asdu[0]) + ", cause " +
                               std::to_string(cause));
        }
    }

    void discarded_command(const codec::AsduHeader& header,
                           const codec::InformationObject& object) override {
        logger().write(IEC104_LOG_WARNING, current_connection,
                       "command type " + std::to_string(header.type_id) + " ioa " +
                           std::to_string(object.ioa) +
                           " discarded: older than the maximum command delay");
    }

    // Queues @p asdu on one started connection, or (connection 0) as event for all groups.
    iec104_status enqueue(std::uint32_t connection, const std::uint8_t* asdu, std::size_t length,
                          std::uint8_t cause) {
        iec104_status st = IEC104_ERR_WRONG_STATE;
        if (connection == 0U) {
            st = publish(asdu, length, cause);
        } else {
            const transport::Link* link = find(connection);
            const auto it = peers.find(connection);
            if (link != nullptr && link->state() == apci::State::Started && it != peers.end()) {
                st = it->second.session.enqueue(asdu, length);
            }
        }
        pump_all();
        return st;
    }
};

extern "C" {

iec104_status iec104_server_create(const iec104_link_config* config,
                                   const iec104_callbacks* callbacks, iec104_server** out) {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    iec104_link_config c{};
    iec104_link_config_init(&c);
    if (config != nullptr) {
        c = *config;
    }
    const apci::ApciConfig a = api::to_apci(c, apci::Role::Controlled);
    apci::ConfigWarnings warnings{};
    const iec104_status st = apci::validate(a, warnings);
    if (st != IEC104_OK) {
        return st;
    }
    if (!codec::is_valid(api::to_profile(c))) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    // The handle is owned by the C caller until iec104_server_destroy().
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out = new (std::nothrow) iec104_server(a, c, callbacks);
    return *out != nullptr ? IEC104_OK : IEC104_ERR_INTERNAL;
}

iec104_status iec104_server_listen(iec104_server* server, const char* address, uint16_t port) {
    if (server == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (server->listener.valid()) {
        return IEC104_ERR_WRONG_STATE;
    }
    return transport::listen_tcp(address, port, server->listener);
}

uint16_t iec104_server_port(const iec104_server* server) {
    return (server != nullptr && server->listener.valid()) ? transport::local_port(server->listener)
                                                           : 0U;
}

iec104_status iec104_server_poll(iec104_server* server, int timeout_ms) {
    if (server == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    short listener_revents = 0;
    const iec104_status st =
        api::poll_links(server->links, &server->listener, timeout_ms, *server, listener_revents);
    if (st == IEC104_OK && (listener_revents & POLLIN) != 0) {
        server->accept_pending();
    }
    // Answers queued while handling the received frames go out now.
    server->pump_all();
    return st;
}

iec104_status iec104_server_send_asdu(iec104_server* server, uint32_t connection,
                                      const uint8_t* asdu, size_t length) {
    if (server == nullptr || asdu == nullptr || length == 0U || length > codec::kMaxAsduLength) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (connection != 0U) {
        transport::Link* link = server->find(connection);
        const auto it = server->peers.find(connection);
        return link != nullptr && it != server->peers.end()
                   ? transmit(*link, it->second, asdu, length)
                   : IEC104_ERR_WRONG_STATE;
    }
    // Broadcast to every started connection (raw ASDUs are not buffered).
    bool sent = false;
    bool blocked = false;
    for (auto& link : server->links) {
        const auto it = server->peers.find(link->id());
        if (link->state() != apci::State::Started || it == server->peers.end()) {
            continue;
        }
        const iec104_status st = transmit(*link, it->second, asdu, length);
        sent = sent || st == IEC104_OK;
        blocked = blocked || st == IEC104_ERR_WOULD_BLOCK;
    }
    if (blocked) {
        return IEC104_ERR_WOULD_BLOCK;
    }
    return sent ? IEC104_OK : IEC104_ERR_WRONG_STATE;
}

size_t iec104_server_connection_count(const iec104_server* server) {
    return server != nullptr ? server->links.size() : 0U;
}

void iec104_server_options_init(iec104_server_options* options) {
    if (options != nullptr) {
        options->send_end_of_init = 1;
        options->command_termination = 1;
        options->cause_of_init = 0;
        options->max_command_delay_ms = 0;
        const session::GroupConfig defaults{};
        options->event_buffer_size = static_cast<uint32_t>(defaults.capacity);
        options->overflow = IEC104_OVERFLOW_DROP_OLDEST;
    }
}

iec104_status iec104_server_set_options(iec104_server* server,
                                        const iec104_server_options* options) {
    if (server == nullptr || options == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    // Check everything first: on error nothing is applied. COI is 7 bit
    // (bit 8 is "parameters changed", not settable here).
    if (options->cause_of_init > 127U || options->event_buffer_size == 0U ||
        options->event_buffer_size > IEC104_MAX_EVENT_BUFFER) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    server->options.send_end_of_init = options->send_end_of_init != 0;
    server->options.command_termination = options->command_termination != 0;
    server->options.cause_of_init = options->cause_of_init;
    server->options.max_command_delay_ms = options->max_command_delay_ms;
    server->own_group.capacity = options->event_buffer_size;
    server->own_group.overflow = options->overflow == IEC104_OVERFLOW_REJECT
                                     ? session::Overflow::Reject
                                     : session::Overflow::DropOldest;
    return IEC104_OK;
}

void iec104_group_config_init(iec104_group_config* config) {
    if (config != nullptr) {
        *config = iec104_group_config{};
        config->buffer_size = static_cast<uint32_t>(session::GroupConfig{}.capacity);
        config->overflow = IEC104_OVERFLOW_DROP_OLDEST;
    }
}

iec104_status iec104_server_add_group(iec104_server* server, const iec104_group_config* config,
                                      uint32_t* group) {
    if (server == nullptr || config == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (server->listener.valid() || !server->links.empty()) {
        return IEC104_ERR_WRONG_STATE;
    }
    if (config->buffer_size == 0U || config->buffer_size > IEC104_MAX_EVENT_BUFFER) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    session::GroupConfig g;
    g.capacity = config->buffer_size;
    g.overflow = config->overflow == IEC104_OVERFLOW_REJECT ? session::Overflow::Reject
                                                            : session::Overflow::DropOldest;
    for (const auto& peer : config->peers) { // NOLINT(*-array-to-pointer-decay): C struct
        const auto* text = static_cast<const char*>(peer);
        const auto* end = static_cast<const char*>(std::memchr(text, '\0', sizeof peer));
        if (end == nullptr) {
            return IEC104_ERR_INVALID_ARGUMENT;
        }
        if (end != text) {
            g.peers.emplace_back(text, end);
        }
    }
    const std::uint32_t id = server->next_group++;
    server->groups.emplace(id, std::make_unique<session::RedundancyGroup>(std::move(g)));
    server->configured_groups = true;
    if (group != nullptr) {
        *group = id;
    }
    return IEC104_OK;
}

iec104_status iec104_server_get_group_stats(const iec104_server* server, uint32_t group,
                                            iec104_group_stats* out) {
    if (server == nullptr || out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    const auto it = server->groups.find(group);
    if (it == server->groups.end()) {
        return IEC104_ERR_WRONG_STATE;
    }
    const session::GroupStats g = it->second->stats();
    *out = iec104_group_stats{};
    out->active_connection = g.active;
    out->connections = static_cast<uint32_t>(g.connections);
    out->buffered = static_cast<uint32_t>(g.buffered);
    out->in_flight = static_cast<uint32_t>(g.in_flight);
    out->events = g.events;
    out->dropped = g.dropped;
    out->rejected = g.rejected;
    out->resent = g.resent;
    out->switchovers = g.switchovers;
    return IEC104_OK;
}

uint32_t iec104_server_connection_group(const iec104_server* server, uint32_t connection) {
    if (server == nullptr) {
        return 0;
    }
    const auto it = server->peers.find(connection);
    return it != server->peers.end() ? it->second.group_id : 0U;
}

iec104_status iec104_server_add_command(iec104_server* server, uint16_t common_address,
                                        uint32_t ioa, uint8_t type, iec104_command_model model,
                                        uint32_t select_timeout_ms) {
    if (server == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    session::CommandPoint point{};
    point.type = type;
    point.model = model == IEC104_COMMAND_SELECT_EXECUTE ? session::CommandModel::SelectExecute
                                                         : session::CommandModel::Direct;
    point.select_timeout_ms = select_timeout_ms;
    return server->commands.set(common_address, ioa, point);
}

iec104_status iec104_server_add_point(iec104_server* server, uint16_t common_address,
                                      const iec104_object* point, uint16_t groups) {
    if (server == nullptr || point == nullptr ||
        session::ProcessImage::base_type(point->type) == 0U) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    codec::InformationObject o{};
    const iec104_status st = api::to_codec(*point, o);
    if (st != IEC104_OK) {
        return st;
    }
    return server->image.set(common_address, point->type, o, groups);
}

iec104_status iec104_server_update(iec104_server* server, uint16_t common_address,
                                   const iec104_object* object) {
    if (server == nullptr || object == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    codec::InformationObject o{};
    iec104_status st = api::to_codec(*object, o);
    if (st != IEC104_OK) {
        return st;
    }
    st = server->image.update(common_address, object->type, o);
    if (st != IEC104_OK) {
        return st;
    }
    codec::AsduHeader h{};
    h.type_id = object->type;
    h.cause = IEC104_COT_SPONTANEOUS;
    h.originator = server->originator;
    h.common_address = common_address;
    std::array<std::uint8_t, codec::kMaxAsduLength> buf{};
    std::size_t n = 0;
    st = codec::encode_asdu(h, &o, 1, server->profile, buf.data(), buf.size(), n);
    if (st != IEC104_OK) {
        return st;
    }
    st = server->enqueue(0, buf.data(), n, IEC104_COT_SPONTANEOUS);
    // No started connection is not an error for an update: the image holds the value.
    return st == IEC104_ERR_WRONG_STATE ? IEC104_OK : st;
}

iec104_status iec104_server_send(iec104_server* server, uint32_t connection,
                                 uint16_t common_address, uint8_t cause,
                                 const iec104_object* objects, size_t count) {
    if (server == nullptr || objects == nullptr || count == 0U) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    std::vector<codec::InformationObject> list(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (objects[i].type != objects[0].type) {
            return IEC104_ERR_INVALID_ARGUMENT; // one type per ASDU
        }
        const iec104_status st = api::to_codec(objects[i], list[i]);
        if (st != IEC104_OK) {
            return st;
        }
    }
    codec::AsduHeader h{};
    h.type_id = objects[0].type;
    h.cause = cause;
    h.originator = server->originator;
    h.common_address = common_address;
    const codec::InformationObject* next = list.data();
    std::size_t remaining = count;
    iec104_status result = IEC104_OK;
    while (remaining > 0U) {
        std::array<std::uint8_t, codec::kMaxAsduLength> buf{};
        std::size_t n = 0;
        std::size_t used = 0;
        const iec104_status st = codec::encode_asdu_fitting(h, next, remaining, server->profile,
                                                            buf.data(), buf.size(), n, used);
        if (st != IEC104_OK) {
            return st;
        }
        result = server->enqueue(connection, buf.data(), n, cause);
        if (result != IEC104_OK) {
            return result;
        }
        next += used;
        remaining -= used;
    }
    return result;
}

void iec104_server_set_log(iec104_server* server, iec104_log_level max_level, iec104_log_fn fn,
                           void* user) {
    if (server == nullptr) {
        return;
    }
    server->logger().set(max_level, fn, user);
    api::log_config_warnings(server->config, server->logger());
}

iec104_status iec104_server_get_stats(const iec104_server* server, uint32_t connection,
                                      iec104_link_stats* out) {
    if (server == nullptr || out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    for (const auto& link : server->links) {
        if (link->id() != connection) {
            continue;
        }
        iec104_link_stats s{};
        api::copy_link_stats(*link, s);
        const auto it = server->peers.find(connection);
        if (it != server->peers.end()) {
            const session::SessionStats& ss = it->second.session.stats();
            s.requests = ss.requests;
            s.commands_accepted = ss.commands_accepted;
            s.commands_rejected = ss.commands_rejected;
            s.commands_discarded = ss.commands_discarded;
            s.negative_answers = ss.negative_answers;
            s.send_queue = static_cast<uint32_t>(it->second.session.queued());
        }
        *out = s;
        return IEC104_OK;
    }
    return IEC104_ERR_WRONG_STATE;
}

iec104_status iec104_server_set_tls(iec104_server* server, const iec104_tls_config* config) {
    if (server == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    return api::make_tls_context(transport::TlsRole::Server, config, server->logger(), server->tls);
}

iec104_status iec104_server_get_tls_info(const iec104_server* server, uint32_t connection,
                                         iec104_tls_info* out) {
    if (server == nullptr || out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    for (const auto& link : server->links) {
        if (link->id() == connection) {
            return api::copy_tls_info(*link, *out) ? IEC104_OK : IEC104_ERR_WRONG_STATE;
        }
    }
    return IEC104_ERR_WRONG_STATE;
}

void iec104_server_destroy(iec104_server* server) {
    // Ends the ownership of the C caller; sockets close in their destructors.
    delete server; // NOLINT(cppcoreguidelines-owning-memory)
}

} // extern "C"
