/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file client.cpp
 * @brief C API of the client (controlling station), see iec104.h.
 *
 * A client has one connection (link id 1) or, with
 * iec104_client_connect_paths(), up to four paths (link id = path number).
 * After the connect call the TCP connect runs in the background of
 * iec104_client_poll(); when it completes the state callback reports
 * STOPPED, and the application sends STARTDT with iec104_client_start().
 *
 * With several paths (docs/05_Normfragen_und_Festlegungen.md, E11, E12) the
 * session::PathSelector decides which path is delivered; this file does the
 * I/O around it:
 *
 *   link event of path p  -->  PathSelector (started, lost, received)
 *                         -->  actions: deliver, switched, interrogate, all lost
 *
 * The application sees one logical connection with id 1. Lost paths are
 * connected again after reconnect_interval_ms (in iec104_client_poll(), never
 * from inside a link callback, because the poll loop walks the link list).
 *
 * Layer: API.
 */
#include "api/link_common.hpp"
#include "api/values.hpp"
#include "session/command_table.hpp"
#include "session/command_tracker.hpp"
#include "session/path_selector.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <set>
#include <string>

using namespace iec104;

namespace {
constexpr std::uint32_t kClientLinkId = 1;
constexpr std::uint8_t kReadType = 102;      // C_RD_NA_1
constexpr std::uint8_t kInterrogation = 100; // C_IC_NA_1
constexpr std::uint8_t kClockSync = 103;     // C_CS_NA_1
constexpr std::uint8_t kQoiStation = 20;     // general interrogation

// Copies @p text into a fixed C string field (truncated, always terminated).
template <std::size_t N>
void put(char (&out)[N], // NOLINT(*-avoid-c-arrays): field of a C struct
         const std::string& text) noexcept {
    const std::size_t n = std::min(text.size(), N - 1U);
    std::memcpy(static_cast<char*>(out), text.data(), n);
    out[n] = '\0';
}

// A terminated C string field as text.
template <std::size_t N>
std::string text_of(const char (&field)[N]) { // NOLINT(*-avoid-c-arrays): field of a C struct
    const auto* begin = static_cast<const char*>(field);
    const auto* end = static_cast<const char*>(std::memchr(begin, '\0', N));
    return {begin, end != nullptr ? static_cast<std::size_t>(end - begin) : N};
}

// One path of a client with several paths.
struct PathSlot {
    iec104_path_config address{};
    std::shared_ptr<transport::TlsContext> tls; // NULL = plain TCP
    bool waiting{false};                        // no link; connect again at reconnect_at
    std::uint64_t reconnect_at{0};
    iec104_close_reason last_close{IEC104_CLOSE_NONE};
    std::uint64_t connects{0};
};

// Order of the logical state while no path leads: the most advanced path counts.
int progress(iec104_link_state s) noexcept {
    switch (s) {
    case IEC104_LINK_STARTED:
        return 4;
    case IEC104_LINK_START_PENDING:
        return 3;
    case IEC104_LINK_STOP_PENDING:
        return 2;
    case IEC104_LINK_STOPPED:
        return 1;
    case IEC104_LINK_CONNECTING:
    case IEC104_LINK_CLOSED:
        break;
    }
    return 0;
}
} // namespace

struct iec104_client : api::CallbackSink {
    iec104_client(const apci::ApciConfig& c, const iec104_link_config& link,
                  const iec104_callbacks* cb) noexcept
        : api::CallbackSink(cb), config(c), profile(api::to_profile(link)),
          originator(link.originator) {}

    apci::ApciConfig config;
    codec::AsduProfile profile;
    std::uint8_t originator;
    std::shared_ptr<transport::TlsContext> tls;    // NULL = plain TCP; keeps the session cache
    std::unique_ptr<iec104_tls_config> tls_config; // as set, for the TLS settings of the paths
    // One connection: at most one entry. Several paths: one per connected path.
    std::vector<std::unique_ptr<transport::Link>> links;

    // Several paths (empty = one connection, iec104_client_connect()).
    std::vector<PathSlot> paths;
    iec104_path_options path_options{};
    std::unique_ptr<session::PathSelector> selector;
    std::vector<session::PathAction> actions; // of the selector, not yet carried out
    std::set<std::uint16_t> stations;         // common addresses seen, for the interrogation
    bool want_started{false};                 // start every path as soon as it is connected
    bool closing{false};                      // iec104_client_close() with paths
    bool ever_led{false};
    iec104_link_state reported{IEC104_LINK_CLOSED}; // logical state last reported
    iec104_path_fn path_fn{nullptr};
    void* path_user{nullptr};

    [[nodiscard]] bool multi() const noexcept { return !paths.empty(); }

    // The link of path number @p id (1..), NULL if it has none.
    [[nodiscard]] transport::Link* find(std::uint32_t id) const noexcept {
        for (const auto& l : links) {
            if (l->id() == id && !l->closed()) {
                return l.get();
            }
        }
        return nullptr;
    }

    // The link commands go over: the only one, or the one of the leading path.
    [[nodiscard]] transport::Link* link() const noexcept {
        if (!multi()) {
            return links.empty() ? nullptr : links.front().get();
        }
        const std::size_t p = selector->leading();
        return p == session::PathSelector::kNoPath ? nullptr
                                                   : find(static_cast<std::uint32_t>(p + 1U));
    }

    session::CommandTracker tracker;
    session::CommandTimeouts timeouts;
    iec104_link_stats counters{};   // command and answer counters of the client
    iec104_link_stats last_stats{}; // all counters of the last closed connection
    bool had_connection{false};

    void on_asdu(std::uint32_t id, const std::uint8_t* asdu, std::size_t length) override {
        if (!multi()) {
            handle_asdu(id, asdu, length);
            return;
        }
        // Several paths: every path feeds the selector, only the leading one is delivered.
        const std::size_t p = id - 1U;
        const bool leading = selector->leading() == p;
        follow(p, asdu, length);
        if (leading) {
            handle_asdu(kClientLinkId, asdu, length);
        }
        run_actions();
    }

    // Monitor-direction objects of path @p p into the selector; remembers the stations.
    void follow(std::size_t p, const std::uint8_t* asdu, std::size_t length) {
        std::array<codec::InformationObject, codec::kMaxObjects> objects{};
        codec::DecodedAsdu d{};
        if (codec::decode_asdu(asdu, length, profile, objects.data(), objects.size(), d) !=
                IEC104_OK ||
            d.type == nullptr) {
            return;
        }
        stations.insert(d.header.common_address);
        if (d.type->direction != codec::Direction::Monitor) {
            return; // answers to own requests: only those of the leading path count
        }
        const std::uint64_t now = transport::monotonic_ms();
        for (std::size_t i = 0; i < d.object_count; ++i) {
            selector->received(p, d.header, objects.at(i), now);
        }
    }

    // One ASDU for the application: callbacks, negative answers, command answers.
    void handle_asdu(std::uint32_t id, const std::uint8_t* asdu, std::size_t length) {
        api::CallbackSink::on_asdu(id, asdu, length);
        deliver_objects(id, asdu, length, profile);
        codec::AsduHeader header{};
        std::size_t header_length = 0;
        if (codec::decode_asdu_header(asdu, length, profile, header, header_length) == IEC104_OK &&
            header.negative) {
            ++counters.negative_answers;
            logger().write(IEC104_LOG_WARNING, id,
                           "negative answer: type " + std::to_string(header.type_id) + ", cause " +
                               std::to_string(header.cause));
        }
        if (tracker.open() == 0U) {
            return;
        }
        // Answers to own commands: confirmation, termination, causes 44..47.
        std::array<codec::InformationObject, codec::kMaxObjects> objects{};
        codec::DecodedAsdu d{};
        if (codec::decode_asdu(asdu, length, profile, objects.data(), objects.size(), d) !=
                IEC104_OK ||
            d.type == nullptr) {
            return;
        }
        std::vector<session::CommandOutcome> outcomes;
        std::vector<session::PendingCommand> to_execute;
        const std::uint64_t now = transport::monotonic_ms();
        tracker.on_asdu(d.header, objects.data(), d.object_count, now, timeouts, outcomes,
                        to_execute);
        for (const session::PendingCommand& exec : to_execute) {
            // Second step of select_execute(): the execute of the selected command.
            const iec104_status st =
                send_object(exec.common_address, IEC104_COT_ACTIVATION, exec.object, exec.type);
            if (st == IEC104_OK) {
                tracker.add(exec.common_address, exec.type, exec.object, false, now, timeouts);
            } else {
                outcomes.push_back(session::CommandOutcome{
                    exec.common_address, exec.type, exec.object, session::CommandResult::Rejected});
            }
        }
        report(outcomes);
    }

    void on_state(std::uint32_t id, apci::State state) override {
        if (!multi()) {
            api::CallbackSink::on_state(id, state);
            return;
        }
        if (state == apci::State::Closed) {
            return; // reported by on_closed()
        }
        const iec104_link_state c = api::to_c(state);
        const bool important = state == apci::State::Started || state == apci::State::Stopped;
        logger().write(important ? IEC104_LOG_INFO : IEC104_LOG_DEBUG, id,
                       "path " + std::to_string(id) + " state " + iec104_link_state_string(c));
        const std::size_t p = id - 1U;
        const std::uint64_t now = transport::monotonic_ms();
        if (state == apci::State::Started) {
            selector->started(p, now, actions);
            if (selector->leading() != p) {
                logger().write(IEC104_LOG_INFO, id,
                               "path " + std::to_string(id) + " started, following");
            }
        } else if (selector->is_started(p)) {
            selector->lost(p, std::string("state ") + iec104_link_state_string(c), now, actions);
        }
        if (state == apci::State::Stopped && want_started && !closing) {
            if (transport::Link* l = find(id)) {
                (void)l->start(now); // a failure shows up as state or close
            }
        }
        run_actions();
        report_logical();
    }

    void on_closed(std::uint32_t id, transport::LinkCloseReason reason,
                   apci::CloseReason protocol_reason) override {
        if (multi()) {
            path_closed(id, api::to_c(reason, protocol_reason));
            return;
        }
        api::CallbackSink::on_closed(id, reason, protocol_reason);
        if (link() != nullptr) {
            last_stats = stats_of(*link());
        }
        std::vector<session::CommandOutcome> outcomes;
        tracker.connection_lost(outcomes);
        report(outcomes);
    }

    // A path was closed: the selector may switch; connect again later.
    void path_closed(std::uint32_t id, iec104_close_reason c) {
        const std::size_t p = id - 1U;
        const bool regular = closing || c == IEC104_CLOSE_LOCAL;
        logger().write(regular ? IEC104_LOG_INFO : IEC104_LOG_WARNING, id,
                       "path " + std::to_string(id) + " closed: " + iec104_close_reason_string(c));
        for (const auto& l : links) {
            if (l->id() == id) {
                last_stats = stats_of(*l);
            }
        }
        const std::uint64_t now = transport::monotonic_ms();
        PathSlot& slot = paths.at(p);
        slot.last_close = c;
        selector->lost(p, std::string("closed: ") + iec104_close_reason_string(c), now, actions);
        if (!closing) {
            slot.waiting = true;
            slot.reconnect_at = now + path_options.reconnect_interval_ms;
        }
        run_actions();
        report_logical();
    }

    // Carries out what the selector asked for.
    void run_actions() {
        while (!actions.empty()) {
            std::vector<session::PathAction> todo;
            todo.swap(actions);
            for (const session::PathAction& a : todo) {
                switch (a.kind) {
                case session::PathActionKind::Deliver:
                    api::CallbackSink::on_asdu(kClientLinkId, a.asdu.data(), a.asdu.size());
                    deliver_objects(kClientLinkId, a.asdu.data(), a.asdu.size(), profile);
                    break;
                case session::PathActionKind::Switched:
                    switched(a);
                    break;
                case session::PathActionKind::Interrogate:
                    interrogate(a.path);
                    break;
                case session::PathActionKind::AllLost:
                    all_lost(a);
                    break;
                }
            }
        }
    }

    void switched(const session::PathAction& a) {
        const auto path = static_cast<std::uint32_t>(a.path + 1U);
        std::string text = "path " + std::to_string(path) + " leading: " + a.reason;
        const bool first = !ever_led;
        ever_led = true;
        if (!first) {
            text += "; replayed " + std::to_string(a.replayed) + ", suppressed " +
                    std::to_string(a.suppressed) + ", dropped " + std::to_string(a.dropped);
        }
        logger().write(first ? IEC104_LOG_INFO : IEC104_LOG_WARNING, path, text);
        if (!first) {
            // E12: a command open on the former path is not repeated.
            std::vector<session::CommandOutcome> outcomes;
            tracker.connection_lost(outcomes);
            report(outcomes);
        }
        const std::uint32_t previous = a.previous == session::PathSelector::kNoPath
                                           ? 0U
                                           : static_cast<std::uint32_t>(a.previous + 1U);
        notify(path, previous, a, text);
    }

    void all_lost(const session::PathAction& a) {
        const std::string text = "all paths lost: " + a.reason;
        logger().write(want_started && !closing ? IEC104_LOG_WARNING : IEC104_LOG_INFO,
                       static_cast<std::uint32_t>(a.path + 1U), text);
        std::vector<session::CommandOutcome> outcomes;
        tracker.connection_lost(outcomes);
        report(outcomes);
        notify(0U, static_cast<std::uint32_t>(a.path + 1U), a, text);
    }

    void notify(std::uint32_t leading, std::uint32_t previous, const session::PathAction& a,
                const std::string& text) const {
        if (path_fn == nullptr) {
            return;
        }
        iec104_path_switch e{};
        e.leading = leading;
        e.previous = previous;
        e.replayed = static_cast<std::uint32_t>(a.replayed);
        e.suppressed = static_cast<std::uint32_t>(a.suppressed);
        e.dropped = static_cast<std::uint32_t>(a.dropped);
        e.reason = text.c_str();
        path_fn(path_user, &e);
    }

    // General interrogation on path @p p for every station seen (broadcast if none yet).
    void interrogate(std::size_t p) {
        transport::Link* l = find(static_cast<std::uint32_t>(p + 1U));
        if (l == nullptr) {
            return;
        }
        std::set<std::uint16_t> to = stations;
        if (to.empty()) {
            to.insert(profile.ca_length == 1U ? std::uint16_t{0xFFU} : codec::kBroadcastCa);
        }
        for (const std::uint16_t ca : to) {
            codec::InformationObject o{};
            o.qoi = kQoiStation;
            const iec104_status st = send_on(*l, ca, IEC104_COT_ACTIVATION, o, kInterrogation);
            logger().write(st == IEC104_OK ? IEC104_LOG_INFO : IEC104_LOG_WARNING, l->id(),
                           "general interrogation of station " + std::to_string(ca) +
                               " after switch-over: " + iec104_status_string(st));
        }
    }

    // Logical state of a client with paths (see iec104.h).
    [[nodiscard]] iec104_link_state logical_state() const noexcept {
        if (closing) {
            return IEC104_LINK_CLOSED;
        }
        if (selector->leading() != session::PathSelector::kNoPath) {
            return IEC104_LINK_STARTED;
        }
        iec104_link_state best = IEC104_LINK_CONNECTING;
        for (const auto& l : links) {
            if (!l->closed() && !l->connecting()) {
                const iec104_link_state s = api::to_c(l->state());
                if (s != IEC104_LINK_STARTED && progress(s) > progress(best)) {
                    best = s;
                }
            }
        }
        return best;
    }

    // Reports a change of the logical state via on_state (id 1).
    void report_logical() {
        if (closing) {
            return; // CLOSED is reported once all paths are closed
        }
        const iec104_link_state s = logical_state();
        if (s == reported) {
            return;
        }
        reported = s;
        logger().write(IEC104_LOG_DEBUG, kClientLinkId,
                       std::string("logical state ") + iec104_link_state_string(s));
        if (callbacks().on_state != nullptr) {
            callbacks().on_state(callbacks().user, kClientLinkId, s, IEC104_CLOSE_NONE);
        }
    }

    // Opens the connection of path @p p (index); on failure it is tried again later.
    void open_path(std::size_t p, std::uint64_t now) {
        PathSlot& slot = paths.at(p);
        const auto id = static_cast<std::uint32_t>(p + 1U);
        slot.waiting = false;
        ++slot.connects;
        const std::string where =
            text_of(slot.address.host) + ":" + std::to_string(slot.address.port);
        transport::Socket s;
        iec104_status st =
            transport::connect_tcp(text_of(slot.address.host).c_str(), slot.address.port, s);
        auto link = std::make_unique<transport::Link>();
        if (st == IEC104_OK) {
            st = link->init_connecting(id, config, std::move(s), now, slot.tls);
        }
        if (st != IEC104_OK) {
            logger().write(IEC104_LOG_WARNING, id,
                           "path " + std::to_string(id) + " connect to " + where +
                               " failed: " + iec104_status_string(st));
            slot.last_close = IEC104_CLOSE_CONNECT_FAILED;
            slot.waiting = true;
            slot.reconnect_at = now + path_options.reconnect_interval_ms;
            return;
        }
        link->set_trace(this);
        logger().write(IEC104_LOG_INFO, id,
                       "path " + std::to_string(id) + " connecting to " + where +
                           (slot.tls ? " (TLS)" : ""));
        links.push_back(std::move(link));
    }

    // After a poll round: connect due paths, age the buffers, end a close.
    void after_poll(std::uint64_t now) {
        if (closing) {
            if (links.empty()) {
                finish_close();
            }
            return;
        }
        for (std::size_t p = 0; p < paths.size(); ++p) {
            if (paths[p].waiting && now >= paths[p].reconnect_at) {
                open_path(p, now);
            }
        }
        selector->tick(now);
        report_logical();
    }

    // Milliseconds until the next reconnect is due (or @p timeout_ms if later).
    [[nodiscard]] int wait_limit(int timeout_ms, std::uint64_t now) const noexcept {
        int wait = timeout_ms;
        for (const PathSlot& slot : paths) {
            if (slot.waiting) {
                const std::uint64_t until = slot.reconnect_at > now ? slot.reconnect_at - now : 0U;
                wait = static_cast<int>(
                    std::min<std::uint64_t>(until, static_cast<std::uint64_t>(std::max(wait, 0))));
            }
        }
        return wait;
    }

    // All paths are closed after iec104_client_close(): back to "no connection".
    void finish_close() {
        closing = false;
        paths.clear();
        selector.reset();
        actions.clear();
        stations.clear();
        want_started = false;
        ever_led = false;
        reported = IEC104_LINK_CLOSED;
        std::vector<session::CommandOutcome> outcomes;
        tracker.connection_lost(outcomes);
        report(outcomes);
        logger().write(IEC104_LOG_INFO, kClientLinkId, "all paths closed");
        if (callbacks().on_state != nullptr) {
            callbacks().on_state(callbacks().user, kClientLinkId, IEC104_LINK_CLOSED,
                                 IEC104_CLOSE_LOCAL);
        }
    }

    // All counters of @p l plus the client's own.
    [[nodiscard]] iec104_link_stats stats_of(const transport::Link& l) const noexcept {
        iec104_link_stats s = counters;
        api::copy_link_stats(l, s);
        return s;
    }

    // Counts, logs and passes finished commands to on_command_result.
    void report(const std::vector<session::CommandOutcome>& outcomes) {
        for (const session::CommandOutcome& o : outcomes) {
            switch (o.result) {
            case session::CommandResult::Confirmed:
            case session::CommandResult::Terminated:
            case session::CommandResult::Selected:
                ++counters.commands_accepted;
                break;
            case session::CommandResult::Rejected:
                ++counters.commands_rejected;
                break;
            case session::CommandResult::NoConfirmation:
            case session::CommandResult::NoTermination:
                ++counters.commands_discarded;
                break;
            case session::CommandResult::ConnectionLost:
                break;
            }
            logger().write(IEC104_LOG_INFO, kClientLinkId,
                           "command type " + std::to_string(o.type) + " ioa " +
                               std::to_string(o.object.ioa) + ": " +
                               iec104_command_result_string(to_c(o.result)));
        }
        if (callbacks().on_command_result == nullptr) {
            return;
        }
        for (const session::CommandOutcome& o : outcomes) {
            iec104_object c{};
            if (api::from_codec(o.type, o.object, c)) {
                callbacks().on_command_result(callbacks().user, o.common_address, &c,
                                              to_c(o.result));
            }
        }
    }

    static iec104_command_result to_c(session::CommandResult r) noexcept {
        switch (r) {
        case session::CommandResult::Confirmed:
            return IEC104_COMMAND_CONFIRMED;
        case session::CommandResult::Terminated:
            return IEC104_COMMAND_TERMINATED;
        case session::CommandResult::Selected:
            return IEC104_COMMAND_SELECTED;
        case session::CommandResult::Rejected:
            return IEC104_COMMAND_REJECTED;
        case session::CommandResult::NoConfirmation:
            return IEC104_COMMAND_NO_CONFIRMATION;
        case session::CommandResult::NoTermination:
            return IEC104_COMMAND_NO_TERMINATION;
        case session::CommandResult::ConnectionLost:
            return IEC104_COMMAND_CONNECTION_LOST;
        }
        return IEC104_COMMAND_REJECTED;
    }

    // Sends a command and starts following it.
    iec104_status send_tracked(std::uint16_t common_address, const iec104_object& command,
                               bool then_execute) {
        const codec::TypeInfo* type = codec::find_type(command.type);
        if (type == nullptr || type->direction != codec::Direction::Control) {
            return IEC104_ERR_INVALID_ARGUMENT;
        }
        codec::InformationObject o{};
        const iec104_status st = api::to_codec(command, o);
        if (st != IEC104_OK) {
            return st;
        }
        if (command.type == kReadType) {
            // A read is a request (cause 5), answered by the point itself, not
            // by a confirmation: nothing to follow.
            return send_object(common_address, IEC104_COT_REQUEST, o, kReadType);
        }
        const iec104_status sent =
            send_object(common_address, IEC104_COT_ACTIVATION, o, command.type);
        if (sent == IEC104_OK) {
            tracker.add(common_address, command.type, o, then_execute, transport::monotonic_ms(),
                        timeouts);
        }
        return sent;
    }

    // Encodes one object and sends it over the link of the client (leading path).
    iec104_status send_object(std::uint16_t common_address, std::uint8_t cause,
                              const codec::InformationObject& object, std::uint8_t type) {
        transport::Link* l = link();
        if (l == nullptr) {
            return IEC104_ERR_WRONG_STATE;
        }
        if (multi() && type == kClockSync) {
            // E12: the time goes to every started path.
            for (const auto& other : links) {
                if (other.get() != l && !other->closed() &&
                    other->state() == apci::State::Started) {
                    (void)send_on(*other, common_address, cause, object, type);
                }
            }
        }
        return send_on(*l, common_address, cause, object, type);
    }

    // Encodes one object and sends it over @p l.
    iec104_status send_on(transport::Link& l, std::uint16_t common_address, std::uint8_t cause,
                          const codec::InformationObject& object, std::uint8_t type) const {
        codec::AsduHeader h{};
        h.type_id = type;
        h.cause = cause;
        h.originator = originator;
        h.common_address = common_address;
        std::array<std::uint8_t, codec::kMaxAsduLength> buf{};
        std::size_t n = 0;
        const iec104_status st =
            codec::encode_asdu(h, &object, 1, profile, buf.data(), buf.size(), n);
        if (st != IEC104_OK) {
            return st;
        }
        return l.send_asdu(buf.data(), n, transport::monotonic_ms());
    }
};

extern "C" {

iec104_status iec104_client_create(const iec104_link_config* config,
                                   const iec104_callbacks* callbacks, iec104_client** out) {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    iec104_link_config c{};
    iec104_link_config_init(&c);
    if (config != nullptr) {
        c = *config;
    }
    const apci::ApciConfig a = api::to_apci(c, apci::Role::Controlling);
    apci::ConfigWarnings warnings{};
    const iec104_status st = apci::validate(a, warnings);
    if (st != IEC104_OK) {
        return st;
    }
    if (!codec::is_valid(api::to_profile(c))) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    // The handle is owned by the C caller until iec104_client_destroy().
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    *out = new (std::nothrow) iec104_client(a, c, callbacks);
    return *out != nullptr ? IEC104_OK : IEC104_ERR_INTERNAL;
}

iec104_status iec104_client_connect(iec104_client* client, const char* host, uint16_t port) {
    if (client == nullptr || host == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (client->multi() || !client->links.empty()) {
        return IEC104_ERR_WRONG_STATE;
    }
    transport::Socket s;
    const iec104_status st = transport::connect_tcp(host, port, s);
    if (st != IEC104_OK) {
        return st;
    }
    auto link = std::make_unique<transport::Link>();
    const iec104_status init = link->init_connecting(kClientLinkId, client->config, std::move(s),
                                                     transport::monotonic_ms(), client->tls);
    if (init != IEC104_OK) {
        return init;
    }
    link->set_trace(client);
    client->logger().write(IEC104_LOG_INFO, kClientLinkId,
                           "connecting to " + std::string(host) + ":" + std::to_string(port) +
                               (client->tls ? " (TLS)" : ""));
    client->links.push_back(std::move(link));
    client->had_connection = true;
    client->counters = iec104_link_stats{};
    return IEC104_OK;
}

iec104_status iec104_client_start(iec104_client* client) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    const std::uint64_t now = transport::monotonic_ms();
    if (client->multi()) {
        if (client->closing) {
            return IEC104_ERR_WRONG_STATE;
        }
        // Every path now, and each one again after its reconnect.
        client->want_started = true;
        for (const auto& l : client->links) {
            if (!l->closed() && !l->connecting() && l->state() == apci::State::Stopped) {
                (void)l->start(now);
            }
        }
        return IEC104_OK;
    }
    transport::Link* link = client->link();
    return link != nullptr ? link->start(now) : IEC104_ERR_WRONG_STATE;
}

iec104_status iec104_client_stop(iec104_client* client) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    const std::uint64_t now = transport::monotonic_ms();
    if (client->multi()) {
        if (client->closing) {
            return IEC104_ERR_WRONG_STATE;
        }
        client->want_started = false;
        for (const auto& l : client->links) {
            if (!l->closed() && l->state() == apci::State::Started) {
                (void)l->stop(now);
            }
        }
        return IEC104_OK;
    }
    transport::Link* link = client->link();
    return link != nullptr ? link->stop(now) : IEC104_ERR_WRONG_STATE;
}

iec104_status iec104_client_poll(iec104_client* client, int timeout_ms) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    short unused = 0;
    const int wait =
        client->multi() ? client->wait_limit(timeout_ms, transport::monotonic_ms()) : timeout_ms;
    const iec104_status st = api::poll_links(client->links, nullptr, wait, *client, unused);
    if (client->multi()) {
        client->after_poll(transport::monotonic_ms());
    }
    // Time limits of open commands.
    std::vector<session::CommandOutcome> outcomes;
    client->tracker.tick(transport::monotonic_ms(), outcomes);
    client->report(outcomes);
    return st;
}

iec104_status iec104_client_send_asdu(iec104_client* client, const uint8_t* asdu, size_t length) {
    if (client == nullptr || asdu == nullptr || length == 0U || length > codec::kMaxAsduLength) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    transport::Link* link = client->link();
    return link != nullptr ? link->send_asdu(asdu, length, transport::monotonic_ms())
                           : IEC104_ERR_WRONG_STATE;
}

iec104_status iec104_client_send_command(iec104_client* client, uint16_t common_address,
                                         const iec104_object* command) {
    if (client == nullptr || command == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    return client->send_tracked(common_address, *command, false);
}

iec104_status iec104_client_select_execute(iec104_client* client, uint16_t common_address,
                                           const iec104_object* command) {
    if (client == nullptr || command == nullptr || !session::is_command_type(command->type) ||
        command->type == 51U || command->type == 64U) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    iec104_object select = *command;
    select.select = 1;
    return client->send_tracked(common_address, select, true);
}

void iec104_client_options_init(iec104_client_options* options) {
    if (options != nullptr) {
        const session::CommandTimeouts defaults{};
        options->confirmation_timeout_ms = defaults.confirmation_ms;
        options->termination_timeout_ms = defaults.termination_ms;
        options->expect_termination = defaults.expect_termination ? 1 : 0;
    }
}

iec104_status iec104_client_set_options(iec104_client* client,
                                        const iec104_client_options* options) {
    if (client == nullptr || options == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    client->timeouts.confirmation_ms = options->confirmation_timeout_ms;
    client->timeouts.termination_ms = options->termination_timeout_ms;
    client->timeouts.expect_termination = options->expect_termination != 0;
    return IEC104_OK;
}

iec104_status iec104_client_read(iec104_client* client, uint16_t common_address, uint32_t ioa) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    iec104_object read{};
    read.type = kReadType;
    read.ioa = ioa;
    return client->send_tracked(common_address, read, false);
}

iec104_status iec104_client_interrogation(iec104_client* client, uint16_t common_address,
                                          uint8_t qoi) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    codec::InformationObject o{}; // IOA 0: the request concerns the whole station
    o.qoi = qoi;
    return client->send_object(common_address, IEC104_COT_ACTIVATION, o, 100);
}

iec104_status iec104_client_counter_interrogation(iec104_client* client, uint16_t common_address,
                                                  uint8_t rqt, uint8_t frz) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    codec::InformationObject o{};
    o.qcc = codec::Qcc{rqt, frz};
    return client->send_object(common_address, IEC104_COT_ACTIVATION, o, 101);
}

iec104_status iec104_client_clock_sync(iec104_client* client, uint16_t common_address,
                                       const iec104_time* time) {
    if (client == nullptr || time == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    codec::InformationObject o{};
    const iec104_status st = api::to_cp56(*time, o.cp56);
    if (st != IEC104_OK) {
        return st;
    }
    return client->send_object(common_address, IEC104_COT_ACTIVATION, o, 103);
}

iec104_link_state iec104_client_state(const iec104_client* client) {
    if (client != nullptr && client->multi()) {
        return client->logical_state();
    }
    if (client == nullptr || client->links.empty()) {
        return IEC104_LINK_CLOSED;
    }
    const transport::Link& link = *client->links.front();
    if (link.closed()) {
        return IEC104_LINK_CLOSED;
    }
    return link.connecting() ? IEC104_LINK_CONNECTING : api::to_c(link.state());
}

void iec104_client_close(iec104_client* client) {
    if (client == nullptr) {
        return;
    }
    if (client->multi()) {
        // All paths; CLOSED is reported once the last one is closed (next poll).
        client->closing = true;
        for (const auto& l : client->links) {
            l->close();
        }
        for (PathSlot& slot : client->paths) {
            slot.waiting = false;
        }
        return;
    }
    if (client->link() != nullptr) {
        client->link()->close();
    }
}

void iec104_client_set_log(iec104_client* client, iec104_log_level max_level, iec104_log_fn fn,
                           void* user) {
    if (client == nullptr) {
        return;
    }
    client->logger().set(max_level, fn, user);
    api::log_config_warnings(client->config, client->logger());
}

iec104_status iec104_client_get_stats(const iec104_client* client, iec104_link_stats* out) {
    if (client == nullptr || out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    // With paths: the leading one, else any connected one.
    const transport::Link* l = client->link();
    if (l == nullptr && !client->links.empty()) {
        l = client->links.front().get();
    }
    if (l != nullptr) {
        *out = client->stats_of(*l);
        return IEC104_OK;
    }
    if (!client->had_connection) {
        return IEC104_ERR_WRONG_STATE;
    }
    *out = client->last_stats;
    return IEC104_OK;
}

iec104_status iec104_client_set_tls(iec104_client* client, const iec104_tls_config* config) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    const iec104_status st =
        api::make_tls_context(transport::TlsRole::Client, config, client->logger(), client->tls);
    if (st == IEC104_OK) {
        client->tls_config.reset(config != nullptr ? new (std::nothrow) iec104_tls_config(*config)
                                                   : nullptr);
    }
    return st;
}

iec104_status iec104_client_get_tls_info(const iec104_client* client, iec104_tls_info* out) {
    if (client == nullptr || out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    const transport::Link* l = client->link();
    return l != nullptr && api::copy_tls_info(*l, *out) ? IEC104_OK : IEC104_ERR_WRONG_STATE;
}

void iec104_path_options_init(iec104_path_options* options) {
    if (options == nullptr) {
        return;
    }
    const session::PathSettings defaults{};
    *options = iec104_path_options{};
    options->reconnect_interval_ms = 5000U;
    options->replay_window_ms = 0U; // t1 + t3 + 5 s
    options->replay_max_objects = static_cast<std::uint32_t>(defaults.replay_max_objects);
    options->replay_tolerance_ms = static_cast<std::uint32_t>(defaults.replay_tolerance_ms);
    options->interrogation_after_switch = defaults.interrogation_after_switch ? 1 : 0;
    options->prefer_first_path = defaults.prefer_first_path ? 1 : 0;
}

iec104_status iec104_client_connect_paths(iec104_client* client, const iec104_path_config* paths,
                                          size_t count, const iec104_path_options* options) {
    if (client == nullptr || paths == nullptr || count == 0U || count > IEC104_MAX_PATHS) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (client->multi() || !client->links.empty()) {
        return IEC104_ERR_WRONG_STATE;
    }
    iec104_path_options o{};
    iec104_path_options_init(&o);
    if (options != nullptr) {
        o = *options;
    }
    std::vector<PathSlot> slots(count);
    for (std::size_t p = 0; p < count; ++p) {
        const iec104_path_config& a = paths[p];
        if (a.host[0] == '\0' ||
            std::find(std::begin(a.host), std::end(a.host), '\0') == std::end(a.host)) {
            return IEC104_ERR_INVALID_ARGUMENT;
        }
        slots[p].address = a;
        slots[p].address.host[sizeof a.host - 1U] = '\0';
        slots[p].address.tls_peer_name[sizeof a.tls_peer_name - 1U] = '\0';
        slots[p].address.tls_peer_fingerprint[sizeof a.tls_peer_fingerprint - 1U] = '\0';
        slots[p].tls = client->tls;
        const bool own_peer = a.tls_peer_name[0] != '\0' || a.tls_peer_fingerprint[0] != '\0';
        if (client->tls && client->tls_config && own_peer) {
            // TLS settings of the client with the expected peer of this path.
            iec104_tls_config t = *client->tls_config;
            if (a.tls_peer_name[0] != '\0') {
                put(t.peer_name, text_of(slots[p].address.tls_peer_name));
            }
            if (a.tls_peer_fingerprint[0] != '\0') {
                put(t.peer_fingerprint, text_of(slots[p].address.tls_peer_fingerprint));
            }
            const iec104_status st = api::make_tls_context(transport::TlsRole::Client, &t,
                                                           client->logger(), slots[p].tls);
            if (st != IEC104_OK) {
                return st;
            }
        }
    }
    session::PathSettings settings{};
    settings.replay_window_ms = o.replay_window_ms != 0U ? o.replay_window_ms
                                                         : std::uint64_t{client->config.t1_ms} +
                                                               client->config.t3_ms + 5000U;
    settings.replay_max_objects = o.replay_max_objects;
    settings.replay_tolerance_ms = o.replay_tolerance_ms;
    settings.interrogation_after_switch = o.interrogation_after_switch != 0;
    settings.prefer_first_path = o.prefer_first_path != 0;
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): plain new, no exceptions
    auto* selector = new (std::nothrow) session::PathSelector(count, settings, client->profile);
    if (selector == nullptr) {
        return IEC104_ERR_INTERNAL;
    }
    client->selector.reset(selector);
    client->paths = std::move(slots);
    client->path_options = o;
    client->stations.clear();
    client->want_started = false;
    client->closing = false;
    client->ever_led = false;
    client->reported = IEC104_LINK_CONNECTING;
    client->had_connection = true;
    client->counters = iec104_link_stats{};
    client->logger().write(IEC104_LOG_INFO, kClientLinkId,
                           std::to_string(count) + " paths, replay window " +
                               std::to_string(settings.replay_window_ms) + " ms");
    const std::uint64_t now = transport::monotonic_ms();
    for (std::size_t p = 0; p < count; ++p) {
        client->open_path(p, now);
    }
    return IEC104_OK;
}

uint32_t iec104_client_leading_path(const iec104_client* client) {
    if (client == nullptr || !client->multi()) {
        return 0U;
    }
    const std::size_t p = client->selector->leading();
    return p == session::PathSelector::kNoPath ? 0U : static_cast<std::uint32_t>(p + 1U);
}

iec104_status iec104_client_get_path_info(const iec104_client* client, uint32_t path,
                                          iec104_path_info* out) {
    if (client == nullptr || out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (!client->multi()) {
        return IEC104_ERR_WRONG_STATE;
    }
    if (path == 0U || path > client->paths.size()) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    const std::size_t p = path - 1U;
    const PathSlot& slot = client->paths[p];
    *out = iec104_path_info{};
    put(out->host, text_of(slot.address.host));
    out->port = slot.address.port;
    out->state = IEC104_LINK_CLOSED;
    out->last_close = slot.last_close;
    if (const transport::Link* l = client->find(path)) {
        out->state = l->connecting() ? IEC104_LINK_CONNECTING : api::to_c(l->state());
        out->silent = l->silent() ? 1 : 0;
        api::copy_link_stats(*l, out->stats);
    }
    out->leading = client->selector->leading() == p ? 1 : 0;
    out->connects = slot.connects;
    const session::PathCounters& c = client->selector->counters(p);
    out->became_leading = c.became_leading;
    out->followed = c.followed;
    out->replayed = c.replayed;
    out->suppressed = c.suppressed;
    out->dropped = c.dropped;
    out->buffered = static_cast<std::uint32_t>(client->selector->buffered(p));
    return IEC104_OK;
}

void iec104_client_set_path_callback(iec104_client* client, iec104_path_fn fn, void* user) {
    if (client != nullptr) {
        client->path_fn = fn;
        client->path_user = user;
    }
}

iec104_status iec104_client_disturb_path(iec104_client* client, uint32_t path,
                                         iec104_path_fault fault) {
    if (client == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (!client->multi()) {
        return IEC104_ERR_WRONG_STATE;
    }
    if (path == 0U || path > client->paths.size()) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    transport::Link* l = client->find(path);
    const std::string name = "path " + std::to_string(path);
    switch (fault) {
    case IEC104_PATH_CLOSE:
        if (l == nullptr) {
            return IEC104_ERR_WRONG_STATE;
        }
        client->logger().write(IEC104_LOG_WARNING, path, name + " disturbed: closed (test)");
        l->close();
        return IEC104_OK;
    case IEC104_PATH_SILENT:
        if (l == nullptr) {
            return IEC104_ERR_WRONG_STATE;
        }
        client->logger().write(IEC104_LOG_WARNING, path, name + " disturbed: silent (test)");
        l->set_silent(true);
        return IEC104_OK;
    case IEC104_PATH_RESTORE:
        client->logger().write(IEC104_LOG_INFO, path, name + " restored (test)");
        if (l != nullptr) {
            l->set_silent(false);
        } else if (!client->closing) {
            client->paths[path - 1U].reconnect_at = 0U; // connect at the next poll
        }
        return IEC104_OK;
    }
    return IEC104_ERR_INVALID_ARGUMENT;
}

void iec104_client_destroy(iec104_client* client) {
    delete client; // NOLINT(cppcoreguidelines-owning-memory): ends the ownership of the C caller
}

} // extern "C"
