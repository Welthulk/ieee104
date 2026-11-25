/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file fuzz_apci.cpp
 * @brief libFuzzer target for the APCI state machine (src/apci/connection.hpp).
 *
 * The input is read as a small script, so that the fuzzer can explore
 * sequences of received bytes, elapsed time and local requests:
 *
 *   octet 0:  configuration: bit 0 role (0 client, 1 server), bit 1 tolerant,
 *             bits 2..4 k - 1 (k = 1..8), bits 5..7 w - 1 (w = 1..8)
 *   then repeated: opcode, operands
 *     0 n b..  receive the next n octets (n = 0..255)
 *     1 lo hi  advance time by lo + 256 * hi milliseconds, tick
 *     2        send a fixed 10-octet ASDU
 *     3        start()      4  stop()      5  close()
 *     other    ignored
 *
 * Invariants checked after every step (abort on violation):
 *   - the output is a sequence of complete, valid APDUs (codec::decode_apci);
 *   - N(S) of the I-frames sent counts up by one without gap;
 *   - never more than k unacknowledged I-frames;
 *   - once Closed, the state stays Closed and nothing more is sent.
 *
 * Build:  cmake --preset fuzz && cmake --build --preset fuzz
 * Run:    ./build/fuzz/fuzz_apci -max_total_time=300 fuzz/corpus_apci
 */
#include "apci/connection.hpp"
#include "codec/apci.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

using namespace iec104;
using namespace iec104::apci;

namespace {

const std::array<std::uint8_t, 10> kAsdu{0x01, 0x01, 0x03, 0x00, 0x01,
                                         0x00, 0x64, 0x00, 0x00, 0x01};

struct Checker {
    std::uint16_t next_ns{0};
    bool closed{false};

    // Takes the output and checks it frame by frame.
    void drain(Connection& c) {
        std::array<std::uint8_t, 8192> buf{};
        const std::size_t n = c.take_output(buf.data(), buf.size());
        if (closed && n > 0U) {
            std::abort(); // nothing may be sent after close was reported
        }
        std::size_t pos = 0;
        while (pos < n) {
            codec::Apci a{};
            std::size_t len = 0;
            if (codec::peek_apdu_length(buf.data() + pos, n - pos, len) != IEC104_OK ||
                codec::decode_apci(buf.data() + pos, n - pos, a) != IEC104_OK) {
                std::abort();
            }
            if (a.format == codec::FrameFormat::I) {
                if (a.send_seq != next_ns) {
                    std::abort();
                }
                next_ns = static_cast<std::uint16_t>((next_ns + 1U) % codec::kSequenceModulo);
            }
            pos += len;
        }
        Event e{};
        while (c.next_event(e)) {
            if (e.kind == EventKind::CloseRequested) {
                closed = true;
            }
        }
        if (closed && c.state() != State::Closed) {
            std::abort();
        }
    }
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0U) {
        return 0;
    }
    ApciConfig config{};
    config.role = (data[0] & 0x01U) != 0U ? Role::Controlled : Role::Controlling;
    config.strict = (data[0] & 0x02U) == 0U;
    config.k = static_cast<std::uint16_t>(((data[0] >> 2U) & 0x07U) + 1U);
    config.w = static_cast<std::uint16_t>(((data[0] >> 5U) & 0x07U) + 1U);
    config.plausibility = PlausibilityCheck::Off;

    Connection c;
    if (c.init(config) != IEC104_OK) {
        std::abort(); // every generated configuration is valid
    }
    TimeMs now = 1000;
    (void)c.open(now);
    Checker check;

    std::size_t pos = 1;
    while (pos < size) {
        const std::uint8_t op = data[pos++];
        switch (op) {
        case 0: {
            if (pos >= size) {
                return 0;
            }
            std::size_t n = data[pos++];
            n = n < size - pos ? n : size - pos;
            std::size_t used = 0;
            while (used < n) {
                const std::size_t step = c.receive(data + pos + used, n - used, now);
                used += step;
                check.drain(c);
                if (step == 0U && c.state() != State::Closed) {
                    std::abort(); // with an empty event queue receive() must progress
                }
            }
            pos += n;
            break;
        }
        case 1:
            if (pos + 1U >= size) {
                return 0;
            }
            now += static_cast<TimeMs>(data[pos]) + 256U * static_cast<TimeMs>(data[pos + 1U]);
            pos += 2U;
            c.tick(now);
            break;
        case 2:
            (void)c.send_asdu(kAsdu.data(), kAsdu.size(), now);
            break;
        case 3:
            (void)c.start(now);
            break;
        case 4:
            (void)c.stop(now);
            break;
        case 5:
            c.close();
            break;
        default:
            break;
        }
        if (c.unacknowledged() > config.k) {
            std::abort();
        }
        check.drain(c);
    }
    return 0;
}
