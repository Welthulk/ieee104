/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file redundancy_group.cpp
 * @brief Implementation of the redundancy group (see redundancy_group.hpp).
 *
 * Layer: session (no I/O).
 */
#include "session/redundancy_group.hpp"

#include <algorithm>

namespace iec104::session {

bool RedundancyGroup::accepts(const std::string& address) const {
    return config_.peers.empty() ||
           std::find(config_.peers.begin(), config_.peers.end(), address) != config_.peers.end();
}

void RedundancyGroup::join(std::uint32_t connection) {
    members_[connection] = Member{};
}

std::uint32_t RedundancyGroup::leave(std::uint32_t connection) {
    const std::uint32_t now_active = stop(connection);
    members_.erase(connection);
    return now_active;
}

std::uint32_t RedundancyGroup::start(std::uint32_t connection) {
    Member& m = members_[connection];
    m.started = true;
    m.started_order = ++start_counter_;
    const std::uint32_t previous = active_;
    if (previous == connection) {
        return 0;
    }
    // One connection per group transfers data: the one started last.
    // TODO(norm): clause 10, STARTDT on a second connection of a group.
    if (previous != 0U) {
        revert(previous);
        ++counters_.switchovers;
    }
    active_ = connection;
    return previous;
}

std::uint32_t RedundancyGroup::stop(std::uint32_t connection) {
    const auto it = members_.find(connection);
    if (it != members_.end()) {
        it->second.started = false;
    }
    if (active_ != connection) {
        return active_;
    }
    revert(connection);
    // Another started member (left over from a take-over) carries on.
    active_ = 0;
    std::uint64_t latest = 0;
    for (const auto& [id, member] : members_) {
        if (member.started && member.started_order > latest) {
            latest = member.started_order;
            active_ = id;
        }
    }
    if (active_ != 0U) {
        ++counters_.switchovers;
    }
    return active_;
}

RedundancyGroup::Added RedundancyGroup::add(const std::uint8_t* asdu, std::size_t length) {
    Added result;
    if (buffer_.size() >= config_.capacity) {
        if (config_.overflow == Overflow::Reject || buffer_.empty()) {
            ++counters_.rejected;
            result.status = IEC104_ERR_WOULD_BLOCK;
            return result;
        }
        const BufferedEvent& oldest = buffer_.front();
        result.dropped = true;
        result.dropped_id = oldest.id;
        result.dropped_queued_on = oldest.state == EventState::Queued ? oldest.connection : 0U;
        buffer_.pop_front();
        ++counters_.dropped;
    }
    BufferedEvent e;
    e.id = next_id_++;
    e.asdu.assign(asdu, asdu + length);
    result.id = e.id;
    buffer_.push_back(std::move(e));
    ++counters_.events;
    return result;
}

std::vector<const BufferedEvent*> RedundancyGroup::queue_on_active() {
    std::vector<const BufferedEvent*> out;
    if (active_ == 0U) {
        return out;
    }
    for (BufferedEvent& e : buffer_) {
        if (e.state == EventState::Pending) {
            e.state = EventState::Queued;
            e.connection = active_;
            out.push_back(&e);
        }
    }
    return out;
}

void RedundancyGroup::sent(std::uint64_t id) {
    BufferedEvent* e = find(id);
    if (e != nullptr && e->state == EventState::Queued) {
        e->state = EventState::InFlight;
    }
}

void RedundancyGroup::acknowledged(std::uint64_t id) {
    // Acknowledgements come in order of sending; the event is near the front.
    const auto it = std::find_if(buffer_.begin(), buffer_.end(),
                                 [id](const BufferedEvent& e) { return e.id == id; });
    // Only an event still in flight counts: after a revert it waits for the next connection.
    if (it != buffer_.end() && it->state == EventState::InFlight) {
        buffer_.erase(it);
    }
}

GroupStats RedundancyGroup::stats() const {
    GroupStats s = counters_;
    s.active = active_;
    s.connections = members_.size();
    s.buffered = buffer_.size();
    s.in_flight = static_cast<std::size_t>(
        std::count_if(buffer_.begin(), buffer_.end(),
                      [](const BufferedEvent& e) { return e.state == EventState::InFlight; }));
    return s;
}

BufferedEvent* RedundancyGroup::find(std::uint64_t id) {
    const auto it = std::find_if(buffer_.begin(), buffer_.end(),
                                 [id](const BufferedEvent& e) { return e.id == id; });
    return it != buffer_.end() ? &*it : nullptr;
}

void RedundancyGroup::revert(std::uint32_t connection) {
    for (BufferedEvent& e : buffer_) {
        if (e.state != EventState::Pending && e.connection == connection) {
            if (e.state == EventState::InFlight) {
                ++counters_.resent;
            }
            e.state = EventState::Pending;
            e.connection = 0;
        }
    }
}

} // namespace iec104::session
