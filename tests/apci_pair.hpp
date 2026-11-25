/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file apci_pair.hpp
 * @brief Test helper: two APCI connections coupled back to back in memory.
 *
 * This is the self-test without network (docs/07_Normfragen_und_Festlegungen.md, E6): a
 * controlling and a controlled station exchange their output bytes directly,
 * with a simulated clock. The helper also records every event per side, so
 * tests can check what each side saw.
 */
#pragma once

#include "apci/connection.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace iec104::test {

/** @brief One side of the pair with the events it produced. */
struct Side {
    apci::Connection conn;
    std::vector<apci::Event> events;
    std::vector<std::uint8_t> sent_bytes; // everything this side wrote, for inspection

    /** @brief Moves all pending events into @ref events. */
    void collect() {
        apci::Event e{};
        while (conn.next_event(e)) {
            events.push_back(e);
        }
    }
    /** @brief Number of received ASDUs so far. */
    std::size_t asdus() const {
        std::size_t n = 0;
        for (const apci::Event& e : events) {
            n += e.kind == apci::EventKind::AsduReceived ? 1U : 0U;
        }
        return n;
    }
    /** @brief The close reason, or None if not closed. */
    apci::CloseReason close_reason() const {
        for (const apci::Event& e : events) {
            if (e.kind == apci::EventKind::CloseRequested) {
                return e.reason;
            }
        }
        return apci::CloseReason::None;
    }
};

/** @brief Client and server connected in memory; time is simulated in ms. */
struct Pair {
    Side client;
    Side server;
    apci::TimeMs now{1000};

    explicit Pair(apci::ApciConfig client_config = {}, apci::ApciConfig server_config = {}) {
        client_config.role = apci::Role::Controlling;
        server_config.role = apci::Role::Controlled;
        (void)client.conn.init(client_config);
        (void)server.conn.init(server_config);
        (void)client.conn.open(now);
        (void)server.conn.open(now);
    }

    /** @brief Moves bytes in both directions until nothing moves any more. */
    void pump() {
        for (int round = 0; round < 1000; ++round) {
            const bool a = transfer(client, server);
            const bool b = transfer(server, client);
            client.collect();
            server.collect();
            if (!a && !b) {
                return;
            }
        }
    }

    /** @brief Advances the simulated time by @p ms in 1-ms steps, ticking and pumping. */
    void advance(apci::TimeMs ms) {
        const apci::TimeMs end = now + ms;
        while (now < end) {
            // Jump to the next deadline (or the end) instead of stepping every ms.
            const apci::TimeMs next =
                std::min({end, client.conn.next_deadline(), server.conn.next_deadline()});
            now = std::max(now + 1, next);
            if (now > end) {
                now = end;
            }
            client.conn.tick(now);
            server.conn.tick(now);
            pump();
        }
    }

    /** @brief STARTDT from the client, confirmed by the server. */
    void start() {
        (void)client.conn.start(now);
        pump();
    }

  private:
    bool transfer(Side& from, Side& to) {
        std::array<std::uint8_t, 4096> buf{};
        const std::size_t n = from.conn.take_output(buf.data(), buf.size());
        from.sent_bytes.insert(from.sent_bytes.end(), buf.begin(),
                               buf.begin() + static_cast<std::ptrdiff_t>(n));
        std::size_t pos = 0;
        while (pos < n) {
            const std::size_t used = to.conn.receive(buf.data() + pos, n - pos, now);
            pos += used;
            to.collect(); // frees the event queue, so receive() can continue
            if (used == 0U && to.conn.state() == apci::State::Idle) {
                break; // receiver not opened: bytes are lost, as on a real network
            }
            if (to.conn.state() == apci::State::Closed) {
                break;
            }
        }
        return n > 0U;
    }
};

} // namespace iec104::test
