/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file path_selector.cpp
 * @brief Implementation of path_selector.hpp.
 */
#include "session/path_selector.hpp"

#include "codec/asdu_header.hpp"
#include "codec/type_table.hpp"

#include <algorithm>
#include <array>

namespace iec104::session {
namespace {

std::uint64_t point_key(std::uint16_t station, std::uint32_t ioa) {
    return (static_cast<std::uint64_t>(station) << 24U) | ioa;
}

PathTime distance(PathTime a, PathTime b) {
    return a > b ? a - b : b - a;
}

std::size_t time_tag_length(codec::TimeTag tag) {
    switch (tag) {
    case codec::TimeTag::Cp56:
        return 7U;
    case codec::TimeTag::Cp24:
        return 3U;
    case codec::TimeTag::None:
        break;
    }
    return 0U;
}

} // namespace

PathSelector::PathSelector(std::size_t count, const PathSettings& settings,
                           const codec::AsduProfile& profile)
    : settings_(settings), profile_(profile), paths_(count) {}

bool PathSelector::is_started(std::size_t p) const noexcept {
    return p < paths_.size() && paths_[p].started;
}

const PathCounters& PathSelector::counters(std::size_t p) const noexcept {
    static const PathCounters kNone{};
    return p < paths_.size() ? paths_[p].counters : kNone;
}

std::size_t PathSelector::buffered(std::size_t p) const noexcept {
    return p < paths_.size() ? paths_[p].buffer.size() : 0U;
}

bool PathSelector::encode(const codec::AsduHeader& header, const codec::InformationObject& object,
                          Entry& out) const {
    const codec::TypeInfo* type = codec::find_type(header.type_id);
    if (type == nullptr || type->direction != codec::Direction::Monitor) {
        return false;
    }
    codec::AsduHeader h = header;
    h.sequence = false;
    h.num_objects = 1;
    std::array<std::uint8_t, codec::kMaxAsduLength> buf{};
    std::size_t n = 0;
    if (codec::encode_asdu(h, &object, 1, profile_, buf.data(), buf.size(), n) != IEC104_OK) {
        return false;
    }
    const std::size_t begin = codec::header_length(profile_) + profile_.ioa_length;
    const std::size_t tag = time_tag_length(type->time_tag);
    if (n < begin + tag) {
        return false;
    }
    out.asdu.assign(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
    // The type is part of the value: a point delivered with and without time
    // tag compares equal only by its elements, which is what both paths send.
    out.value.assign(buf.begin() + static_cast<std::ptrdiff_t>(begin),
                     buf.begin() + static_cast<std::ptrdiff_t>(n - tag));
    out.key = point_key(header.common_address, object.ioa);
    return true;
}

void PathSelector::prune(Path& path, PathTime now) const {
    while (!path.buffer.empty() && (path.buffer.size() > settings_.replay_max_objects ||
                                    now - path.buffer.front().time > settings_.replay_window_ms)) {
        path.buffer.pop_front();
        ++path.dropped_since_switch;
        ++path.counters.dropped;
    }
}

void PathSelector::tick(PathTime now) {
    for (Path& path : paths_) {
        prune(path, now);
    }
}

bool PathSelector::received(std::size_t p, const codec::AsduHeader& header,
                            const codec::InformationObject& object, PathTime now) {
    if (p >= paths_.size() || !paths_[p].started) {
        return false;
    }
    Entry e;
    const bool encoded = encode(header, object, e);
    if (p == leading_) {
        if (encoded) {
            delivered_[e.key] = Delivered{now, std::move(e.value)};
        }
        return true;
    }
    if (encoded) {
        e.time = now;
        Path& path = paths_[p];
        path.buffer.push_back(std::move(e));
        ++path.counters.followed;
        prune(path, now);
    }
    return false;
}

void PathSelector::started(std::size_t p, PathTime now, std::vector<PathAction>& out) {
    if (p >= paths_.size() || paths_[p].started) {
        return;
    }
    Path& path = paths_[p];
    path.started = true;
    path.buffer.clear();
    path.dropped_since_switch = 0;
    if (leading_ == kNoPath) {
        make_leading(p, kNoPath,
                     ever_led_ ? "path " + std::to_string(p + 1) + " started, no path was leading"
                               : "first started path",
                     now, out);
    } else if (settings_.prefer_first_path && p == 0U && leading_ != 0U) {
        make_leading(p, leading_, "preferred path 1 is back", now, out);
    }
}

void PathSelector::lost(std::size_t p, const std::string& reason, PathTime now,
                        std::vector<PathAction>& out) {
    if (p >= paths_.size() || !paths_[p].started) {
        return;
    }
    Path& path = paths_[p];
    path.started = false;
    path.buffer.clear();
    if (p != leading_) {
        return;
    }
    // The next started path in order after the lost one.
    std::size_t next = kNoPath;
    for (std::size_t i = 1; i < paths_.size(); ++i) {
        const std::size_t c = (p + i) % paths_.size();
        if (paths_[c].started) {
            next = c;
            break;
        }
    }
    if (next == kNoPath) {
        leading_ = kNoPath;
        PathAction a;
        a.kind = PathActionKind::AllLost;
        a.path = p;
        a.reason = "path " + std::to_string(p + 1) + " " + reason + ", no other path started";
        out.push_back(std::move(a));
        return;
    }
    make_leading(next, p, "path " + std::to_string(p + 1) + " " + reason, now, out);
}

void PathSelector::make_leading(std::size_t p, std::size_t previous, const std::string& reason,
                                PathTime now, std::vector<PathAction>& out) {
    const bool first = !ever_led_;
    leading_ = p;
    ever_led_ = true;
    Path& path = paths_[p];
    ++path.counters.became_leading;
    PathAction switched;
    switched.kind = PathActionKind::Switched;
    switched.path = p;
    switched.previous = previous;
    switched.reason = reason;
    const std::size_t at = out.size();
    out.push_back(PathAction{}); // placeholder: the switch is reported before the replay
    replay(p, now, switched, out);
    out[at] = std::move(switched);
    if (!first && settings_.interrogation_after_switch) {
        PathAction gi;
        gi.kind = PathActionKind::Interrogate;
        gi.path = p;
        out.push_back(std::move(gi));
    }
}

std::size_t PathSelector::replay_start(const std::deque<Entry>& buffer,
                                       const std::vector<std::size_t>& positions,
                                       std::uint64_t key) const {
    const auto d = delivered_.find(key);
    if (d == delivered_.end()) {
        return 0; // never delivered: all of it
    }
    // Counterpart: same value, arrival closest to the last delivery, within the tolerance.
    const PathTime tol = settings_.replay_tolerance_ms;
    std::size_t best = kNoPath;
    PathTime best_distance = 0;
    for (const std::size_t i : positions) {
        const Entry& e = buffer[i];
        const PathTime dist = distance(e.time, d->second.time);
        if (e.value == d->second.value && dist <= tol &&
            (best == kNoPath || dist <= best_distance)) {
            best = i;
            best_distance = dist;
        }
    }
    if (best != kNoPath) {
        return best + 1;
    }
    // No counterpart: everything from (last delivery - tolerance) on.
    const PathTime from = d->second.time > tol ? d->second.time - tol : 0;
    for (const std::size_t i : positions) {
        if (buffer[i].time >= from) {
            return i;
        }
    }
    return buffer.size();
}

void PathSelector::replay(std::size_t p, PathTime now, PathAction& switched,
                          std::vector<PathAction>& out) {
    Path& path = paths_[p];
    prune(path, now);
    switched.dropped = path.dropped_since_switch;
    path.dropped_since_switch = 0;
    // Buffer positions per point, in order of arrival (one pass, so a switch-over
    // stays linear in the buffer size).
    std::map<std::uint64_t, std::vector<std::size_t>> at;
    for (std::size_t i = 0; i < path.buffer.size(); ++i) {
        at[path.buffer[i].key].push_back(i);
    }
    // Per point: buffer position from which on it is replayed.
    std::map<std::uint64_t, std::size_t> first;
    for (const auto& [key, positions] : at) {
        first[key] = replay_start(path.buffer, positions, key);
    }
    for (std::size_t i = 0; i < path.buffer.size(); ++i) {
        Entry& e = path.buffer[i];
        if (i < first[e.key]) {
            ++switched.suppressed;
            continue;
        }
        delivered_[e.key] = Delivered{e.time, e.value};
        PathAction a;
        a.kind = PathActionKind::Deliver;
        a.path = p;
        a.asdu = std::move(e.asdu);
        out.push_back(std::move(a));
        ++switched.replayed;
    }
    path.counters.replayed += switched.replayed;
    path.counters.suppressed += switched.suppressed;
    path.buffer.clear();
}

} // namespace iec104::session
