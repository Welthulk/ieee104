/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file path_selector.hpp
 * @brief Path redundancy of a controlling station: which path is leading, replay at switch-over.
 *
 * A controlling station keeps two or more paths (connections) to stations
 * that carry the same process (docs/05_Normfragen_und_Festlegungen.md, E11).
 * All paths are started; only the leading path is delivered to the
 * application. The others follow: what they receive in monitor direction is
 * kept for a time window.
 *
 *   started(p)   the path reached "started"; the first one becomes leading
 *   lost(p)      the path left "started" (closed, t1, STOPDT, TLS ...); if it
 *                was leading, another started path takes over
 *   received()   one monitor-direction object of a path; true = deliver now
 *
 * Switch-over: a silent failure is detected late (t1, or t3 + t1). Objects
 * the new leading path received in that time are delivered afterwards
 * (replay). For every point the object last delivered is looked up in the
 * buffer of the new path (same value and quality, time tag ignored, received
 * within the tolerance); everything after it is replayed in the order of
 * arrival. Without such a counterpart everything from (time of the last
 * delivery - tolerance) is replayed; for a point never delivered, all of it.
 * Afterwards a general interrogation on the new path aligns what the replay
 * cannot (Interrogate action). Duplicates are possible around the failure,
 * losses are not, as long as the window covers the detection time.
 *
 * Layer: session (no I/O, no clock: times come from the caller).
 */
#pragma once

#include "codec/asdu.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace iec104::session {

/** @brief Times are milliseconds of a monotonic clock. */
using PathTime = std::uint64_t;

/** @brief Settings of the selector (see iec104_path_options). */
struct PathSettings {
    PathTime replay_window_ms{50000};      ///< Objects of following paths kept this long.
    std::size_t replay_max_objects{10000}; ///< At most this many per path; older ones are dropped.
    PathTime replay_tolerance_ms{500};     ///< Arrival times of both paths may differ this much.
    bool interrogation_after_switch{true}; ///< Interrogate action after a switch-over.
    bool prefer_first_path{false};         ///< Path 0 takes over again when it is back.
};

/** @brief What the caller has to do. */
enum class PathActionKind : std::uint8_t {
    Deliver,     ///< Hand @ref PathAction::asdu (one object) to the application.
    Switched,    ///< @ref PathAction::path is leading now (previous: @ref PathAction::previous).
    Interrogate, ///< Send a general interrogation on @ref PathAction::path.
    AllLost,     ///< No path is started any more.
};

/** @brief One instruction of the selector. */
struct PathAction {
    PathActionKind kind{PathActionKind::Deliver};
    std::size_t path{0};            ///< Path the action belongs to.
    std::size_t previous{0};        ///< Switched: former leading path, kNoPath if none.
    std::vector<std::uint8_t> asdu; ///< Deliver: ASDU with exactly this object.
    std::string reason;             ///< Switched, AllLost: why.
    std::size_t replayed{0};        ///< Switched: objects delivered from the buffer.
    std::size_t suppressed{0};      ///< Switched: buffered objects not delivered (already known).
    std::size_t dropped{0};         ///< Switched: objects dropped from the buffer (window, size).
};

/** @brief Counters of one path. */
struct PathCounters {
    std::uint64_t became_leading{0}; ///< Times this path became leading.
    std::uint64_t replayed{0};       ///< Objects replayed from its buffer.
    std::uint64_t suppressed{0};     ///< Buffered objects not replayed.
    std::uint64_t dropped{0};        ///< Objects dropped from its buffer (window or size).
    std::uint64_t followed{0};       ///< Objects received while following.
};

/** @brief Selector of the leading path, with replay buffer. */
class PathSelector {
  public:
    /** @brief "No path". */
    static constexpr std::size_t kNoPath = static_cast<std::size_t>(-1);

    /**
     * @param count    Number of paths, 1..4.
     * @param settings See PathSettings.
     * @param profile  ASDU profile used to encode buffered objects.
     */
    PathSelector(std::size_t count, const PathSettings& settings,
                 const codec::AsduProfile& profile);

    /** @brief Path @p p reached "started". */
    void started(std::size_t p, PathTime now, std::vector<PathAction>& out);

    /** @brief Path @p p left "started"; @p reason in words for the log. */
    void lost(std::size_t p, const std::string& reason, PathTime now, std::vector<PathAction>& out);

    /**
     * @brief One monitor-direction object received on path @p p.
     * @return true if @p p is leading: deliver the received ASDU as it is.
     */
    bool received(std::size_t p, const codec::AsduHeader& header,
                  const codec::InformationObject& object, PathTime now);

    /** @brief Drops buffered objects older than the window. */
    void tick(PathTime now);

    /** @brief The leading path, kNoPath if none. */
    [[nodiscard]] std::size_t leading() const noexcept { return leading_; }

    /** @brief true if path @p p is started. */
    [[nodiscard]] bool is_started(std::size_t p) const noexcept;

    /** @brief Counters of path @p p. */
    [[nodiscard]] const PathCounters& counters(std::size_t p) const noexcept;

    /** @brief Objects currently buffered for path @p p. */
    [[nodiscard]] std::size_t buffered(std::size_t p) const noexcept;

    /** @brief Number of paths. */
    [[nodiscard]] std::size_t count() const noexcept { return paths_.size(); }

  private:
    struct Entry {
        PathTime time{0};
        std::uint64_t key{0};            ///< Station and IOA.
        std::vector<std::uint8_t> asdu;  ///< One object, ready to deliver.
        std::vector<std::uint8_t> value; ///< Element octets without time tag.
    };
    struct Path {
        bool started{false};
        std::deque<Entry> buffer;
        std::size_t dropped_since_switch{0};
        PathCounters counters;
    };
    struct Delivered {
        PathTime time{0};
        std::vector<std::uint8_t> value;
    };

    bool encode(const codec::AsduHeader& header, const codec::InformationObject& object,
                Entry& out) const;
    void prune(Path& path, PathTime now) const;
    [[nodiscard]] std::size_t replay_start(const std::deque<Entry>& buffer,
                                           const std::vector<std::size_t>& positions,
                                           std::uint64_t key) const;
    void make_leading(std::size_t p, std::size_t previous, const std::string& reason, PathTime now,
                      std::vector<PathAction>& out);
    void replay(std::size_t p, PathTime now, PathAction& switched, std::vector<PathAction>& out);

    PathSettings settings_;
    codec::AsduProfile profile_;
    std::vector<Path> paths_;
    std::size_t leading_{kNoPath};
    bool ever_led_{false};
    std::map<std::uint64_t, Delivered> delivered_; ///< Last object delivered per point.
};

} // namespace iec104::session
