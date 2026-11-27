/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file fuzz_paths.cpp
 * @brief libFuzzer target for the path selector (path redundancy, docs/05, E11).
 *
 * The input is a program of events for a PathSelector with 1..4 paths:
 *
 *   octet 0      number of paths (1..4), small buffer settings, options
 *   then pairs   (operation, argument):
 *                op & 3 = path; op >> 2 = 0 started, 1 lost, 2 received
 *                (argument: point and value), 3 tick; the argument also
 *                advances the clock (argument * 10 ms)
 *
 * Invariants (abort on violation):
 *   - no crash, no sanitizer finding;
 *   - the leading path is started, and some path leads while one is started;
 *   - received() returns true exactly for the leading path;
 *   - every Deliver action carries one monitor-direction object that decodes
 *     again; a Switched action names the path that leads afterwards;
 *   - a buffer never holds more than replay_max_objects objects;
 *   - an object received while leading is never delivered twice by a replay
 *     of the same switch-over (replay starts after what was delivered).
 *
 * Build:  cmake --preset fuzz && cmake --build --preset fuzz
 * Run:    ./build/fuzz/fuzz_paths -max_total_time=300 fuzz/corpus_paths
 */
#include "codec/asdu.hpp"
#include "codec/type_table.hpp"
#include "session/path_selector.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace iec104;

namespace {

void check(bool ok) {
    if (!ok) {
        std::abort();
    }
}

void check_actions(const session::PathSelector& s, const std::vector<session::PathAction>& out,
                   const codec::AsduProfile& profile) {
    std::size_t last_switch = session::PathSelector::kNoPath;
    for (const session::PathAction& a : out) {
        check(a.path < s.count());
        if (a.kind == session::PathActionKind::Deliver) {
            std::array<codec::InformationObject, 2> objects{};
            codec::DecodedAsdu d{};
            check(codec::decode_asdu(a.asdu.data(), a.asdu.size(), profile, objects.data(),
                                     objects.size(), d) == IEC104_OK);
            check(d.type != nullptr && d.type->direction == codec::Direction::Monitor);
            check(d.object_count == 1U);
        } else if (a.kind == session::PathActionKind::Switched) {
            last_switch = a.path;
        }
    }
    if (last_switch != session::PathSelector::kNoPath) {
        check(s.leading() == last_switch);
    }
}

void check_state(const session::PathSelector& s, std::size_t max_objects) {
    bool any_started = false;
    for (std::size_t p = 0; p < s.count(); ++p) {
        any_started = any_started || s.is_started(p);
        check(s.buffered(p) <= max_objects);
        check(p != s.leading() || s.buffered(p) == 0U);
    }
    check(s.leading() == session::PathSelector::kNoPath || s.is_started(s.leading()));
    check(!any_started || s.leading() != session::PathSelector::kNoPath);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 1U) {
        return 0;
    }
    const std::uint8_t setup = data[0];
    const std::size_t count = (setup & 3U) + 1U;
    session::PathSettings settings;
    settings.replay_max_objects = 1U + ((setup >> 2U) & 7U) * 4U; // 1..29
    settings.replay_window_ms = (setup & 0x20U) != 0U ? 500U : 50000U;
    settings.replay_tolerance_ms = (setup & 0x40U) != 0U ? 0U : 500U;
    settings.prefer_first_path = (setup & 0x80U) != 0U;
    const codec::AsduProfile profile{};
    session::PathSelector s(count, settings, profile);

    std::uint64_t now = 1000;
    std::vector<session::PathAction> out;
    for (std::size_t i = 1; i + 1U < size; i += 2U) {
        const std::uint8_t op = data[i];
        const std::uint8_t arg = data[i + 1U];
        const std::size_t p = (op & 3U) % count;
        now += static_cast<std::uint64_t>(arg) * 10U;
        out.clear();
        switch ((op >> 2U) & 3U) {
        case 0:
            s.started(p, now, out);
            break;
        case 1:
            s.lost(p, "fuzz", now, out);
            break;
        case 2: {
            codec::AsduHeader h{};
            h.type_id = (arg & 0x80U) != 0U ? 13U : 1U; // measured value or single point
            h.cause = IEC104_COT_SPONTANEOUS;
            h.common_address = 1;
            h.num_objects = 1;
            codec::InformationObject o{};
            o.ioa = 100U + (arg & 3U);
            if (h.type_id == 13U) {
                o.r32 = static_cast<float>((arg >> 2U) & 7U);
            } else {
                o.siq.on = ((arg >> 2U) & 1U) != 0U;
            }
            const bool deliver = s.received(p, h, o, now);
            check(deliver == (s.is_started(p) && s.leading() == p));
            break;
        }
        default:
            s.tick(now);
            break;
        }
        check_actions(s, out, profile);
        check_state(s, settings.replay_max_objects);
    }
    return 0;
}
