/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file redundancy_group.hpp
 * @brief Redundancy group of a controlled station: connections and event buffer.
 *
 * Several connections of one controlling station form a redundancy group
 * (edition 2, clause 10; docs/05_Normfragen_und_Festlegungen.md, N7). Exactly
 * one of them transfers data: the one started last with STARTDT. The others
 * stay connected in STOPDT and are supervised with TESTFR. The controlling
 * station switches over with STOPDT on the old and STARTDT on the new
 * connection.
 *
 * The event buffer belongs to the group, not to a connection:
 *
 *   add()          Pending   in the buffer, no connection active
 *   queue_on()     Queued    handed to the session of the active connection
 *   sent()         InFlight  sent as I-frame, not yet acknowledged
 *   acknowledged()           removed: the peer confirmed it with N(R)
 *
 * If the active connection stops or breaks, its Queued and InFlight events
 * become Pending again and go out on the next active connection, so an
 * event may arrive twice but is never lost (within the buffer size).
 * TODO(norm): clause 10, handling of unacknowledged events at switch-over.
 *
 * Layer: session (no I/O).
 */
#pragma once

#include "iec104/iec104.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace iec104::session {

/** @brief What happens when an event arrives at a full buffer. */
enum class Overflow : std::uint8_t {
    DropOldest, ///< The oldest event is dropped (counted, logged).
    Reject,     ///< The new event is rejected (IEC104_ERR_WOULD_BLOCK).
};

/** @brief Settings of a group. */
struct GroupConfig {
    std::vector<std::string> peers; ///< IP addresses of the controlling station; empty = any.
    std::size_t capacity{1000};     ///< Events in the buffer (pending, queued, in flight).
    Overflow overflow{Overflow::DropOldest};
};

/** @brief Counters and state of a group. */
struct GroupStats {
    std::uint32_t active{0};      ///< Active connection, 0 = none.
    std::size_t connections{0};   ///< Member connections.
    std::size_t buffered{0};      ///< Events in the buffer.
    std::size_t in_flight{0};     ///< ... of them sent, not acknowledged.
    std::uint64_t events{0};      ///< Events added.
    std::uint64_t dropped{0};     ///< Dropped because the buffer was full (DropOldest).
    std::uint64_t rejected{0};    ///< Rejected because the buffer was full (Reject).
    std::uint64_t resent{0};      ///< Events queued again after a stop or break.
    std::uint64_t switchovers{0}; ///< Changes of the active connection to another one.
};

/** @brief State of one buffered event. */
enum class EventState : std::uint8_t { Pending, Queued, InFlight };

/** @brief One buffered event. */
struct BufferedEvent {
    std::uint64_t id{0};
    std::vector<std::uint8_t> asdu;
    EventState state{EventState::Pending};
    std::uint32_t connection{0}; ///< Queued, InFlight: the connection.
};

/** @brief A redundancy group (see file comment). */
class RedundancyGroup {
  public:
    explicit RedundancyGroup(GroupConfig config) noexcept : config_(std::move(config)) {}

    /** @brief True if a connection from @p address belongs to this group. */
    [[nodiscard]] bool accepts(const std::string& address) const;

    /** @brief Adds a connection (in STOPDT). */
    void join(std::uint32_t connection);

    /**
     * @brief Removes a connection (closed).
     * @return The new active connection, 0 if none (see stop()).
     */
    std::uint32_t leave(std::uint32_t connection);

    /**
     * @brief Data transfer on @p connection was started; it becomes the active one.
     * @return The previously active connection (0 if none or the same). Its
     *         queued events are Pending again; the caller removes them from
     *         that connection's session.
     */
    std::uint32_t start(std::uint32_t connection);

    /**
     * @brief Data transfer on @p connection was stopped.
     *
     * If it was the active one, its events become Pending and the started
     * member that was started last (if any) becomes active.
     * @return The active connection afterwards, 0 if none.
     */
    std::uint32_t stop(std::uint32_t connection);

    [[nodiscard]] std::uint32_t active() const noexcept { return active_; }
    [[nodiscard]] bool has_member(std::uint32_t connection) const {
        return members_.count(connection) != 0U;
    }

    /** @brief Result of add(). */
    struct Added {
        iec104_status status{IEC104_OK};    ///< OK or IEC104_ERR_WOULD_BLOCK (Reject, full).
        std::uint64_t id{0};                ///< Id of the new event.
        bool dropped{false};                ///< An old event was dropped to make room ...
        std::uint64_t dropped_id{0};        ///< ... with this id ...
        std::uint32_t dropped_queued_on{0}; ///< ... queued in this session (0: not queued).
    };

    /** @brief Adds an event (Pending). */
    Added add(const std::uint8_t* asdu, std::size_t length);

    /**
     * @brief Hands all Pending events to the active connection (they become Queued).
     * @return The events in order of age; pointers valid until the next change.
     */
    std::vector<const BufferedEvent*> queue_on_active();

    /** @brief An event was sent as I-frame (Queued -> InFlight). */
    void sent(std::uint64_t id);

    /** @brief An event was acknowledged by the peer: it leaves the buffer. */
    void acknowledged(std::uint64_t id);

    /** @brief Counters and state. */
    [[nodiscard]] GroupStats stats() const;

    [[nodiscard]] const GroupConfig& config() const noexcept { return config_; }

  private:
    BufferedEvent* find(std::uint64_t id);
    void revert(std::uint32_t connection);

    struct Member {
        bool started{false};
        std::uint64_t started_order{0}; // larger = started later
    };

    GroupConfig config_;
    std::map<std::uint32_t, Member> members_;
    std::uint32_t active_{0};
    std::uint64_t start_counter_{0};
    std::deque<BufferedEvent> buffer_; // oldest first
    std::uint64_t next_id_{1};
    GroupStats counters_{};
};

} // namespace iec104::session
