/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file connection.hpp
 * @brief One IEC 104 connection as a sans-I/O state machine (APCI layer).
 *
 * The class implements the transmission control of IEC 60870-5-104 for one
 * TCP connection: data transfer start/stop, link test, numbering and
 * acknowledgement of I-frames, the timers t1..t3 and the windows k and w. It
 * never touches a socket or a clock:
 *
 *   bytes from TCP  --receive()-->  +------------+  --take_output()-->  bytes to TCP
 *   current time    --tick()----->  | Connection |  --next_event()--->  ASDUs, state changes,
 *   ASDUs to send   --send_asdu()-> +------------+                      acknowledgements, close
 *
 * The caller (transport layer or a test) owns the socket and the clock and
 * moves bytes and time in and out. Because of this, every rule can be tested
 * with simulated time, and two instances can be connected back to back in
 * memory (self-test without network, tests/test_apci_selftest.cpp).
 *
 * States (both roles):
 *
 *   Idle --open()--> Stopped --STARTDT--> Started --STOPDT--> Stopped
 *                       |                    |
 *                       +------- any error --+--> Closed (terminal)
 *
 * The controlling station (client) drives STARTDT/STOPDT with start() and
 * stop() and passes through StartPending / StopPending while it waits for
 * the confirmation. The controlled station (server) answers them; it passes
 * through StopPending while own I-frames are still unacknowledged.
 *
 * The rules and their sources are listed at the implementation
 * (connection.cpp); unproven rules carry TODO(norm) (docs/05_Normfragen_und_Festlegungen.md, N1).
 *
 * Memory: the class allocates once, in init() (time stamps for k
 * unacknowledged I-frames); nothing is allocated while the connection runs.
 *
 * Layer: apci (sans-I/O: bytes + time in, bytes + events out).
 */
#pragma once

#include "apci/apci_config.hpp"
#include "codec/apci.hpp"
#include "iec104/iec104.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace iec104::apci {

/** @brief Monotonic time in milliseconds, supplied by the caller (any origin). */
using TimeMs = std::uint64_t;

/** @brief next_deadline() value when no timer is running. */
inline constexpr TimeMs kNoDeadline = ~TimeMs{0};

/** @brief State of a connection. */
enum class State : std::uint8_t {
    Idle,         ///< Created, TCP not connected yet (before open()).
    Stopped,      ///< TCP connected, data transfer stopped: only U- and S-frames.
    StartPending, ///< Controlling station: STARTDT act sent, waiting for con.
    Started,      ///< Data transfer started: I-frames in both directions.
    StopPending,  ///< STOPDT act sent or received, waiting for open acknowledgements / con.
    Closed,       ///< Terminal. The caller closes the TCP connection.
};

/** @brief Why the connection asks to be closed (Event::reason). */
enum class CloseReason : std::uint8_t {
    None,            ///< Not closed.
    T1Timeout,       ///< An I-frame or a U-frame act was not acknowledged within t1.
    SequenceError,   ///< A received I-frame has N(S) other than the expected V(R).
    AckOutOfWindow,  ///< A received N(R) acknowledges frames that were never sent.
    MalformedFrame,  ///< The byte stream is not a valid APDU sequence.
    UnexpectedFrame, ///< A frame that is not allowed in the current state or role.
    LocalRequest,    ///< close() was called.
    OutputOverflow,  ///< The caller did not take the output; control frames could not be queued.
};

/** @brief Kind of an Event. */
enum class EventKind : std::uint8_t {
    AsduReceived,   ///< An I-frame was received; Event::asdu holds its ASDU.
    StateChanged,   ///< The state changed; Event::state holds the new state.
    Acknowledged,   ///< The peer acknowledged own I-frames; Event::count holds how many.
    CloseRequested, ///< The connection is Closed; Event::reason says why.
};

/** @brief Output of the state machine besides the bytes to send. */
struct Event {
    EventKind kind{EventKind::StateChanged}; ///< What happened.
    State state{State::Idle};                ///< StateChanged: the new state.
    CloseReason reason{CloseReason::None};   ///< CloseRequested: the reason.
    std::uint16_t count{0};     ///< Acknowledged: number of newly acknowledged I-frames.
    std::size_t asdu_length{0}; ///< AsduReceived: number of valid octets in asdu.
    std::array<std::uint8_t, codec::kMaxAsduLength> asdu{}; ///< AsduReceived: the ASDU.
};

/**
 * @brief Transmission control of one connection (see file comment).
 *
 * Usage, e.g. in a transport loop:
 * @code
 *   Connection c;
 *   c.init(config);
 *   c.open(now);                        // TCP is connected
 *   c.start(now);                       // client only: STARTDT act
 *   loop:
 *     n = c.receive(rx, rx_len, now);   // consumed n bytes
 *     c.tick(now);                      // timers
 *     while (c.next_event(e)) handle(e);
 *     len = c.take_output(tx, sizeof tx); write(tx, len);
 *     wait until data arrives or c.next_deadline()
 * @endcode
 */
class Connection {
  public:
    /**
     * @brief Applies a configuration and resets the connection to Idle.
     *
     * @param config Parameters; checked with validate().
     * @return IEC104_OK, or the error of validate() (the connection stays unusable).
     */
    iec104_status init(const ApciConfig& config) noexcept;

    /**
     * @brief The TCP connection is established; enters Stopped and starts t3.
     *
     * @return IEC104_OK, or IEC104_ERR_WRONG_STATE if not Idle.
     */
    iec104_status open(TimeMs now) noexcept;

    /**
     * @brief Feeds bytes received from TCP.
     *
     * Frames are processed in order. Processing stops early when the event
     * queue cannot take the events of another frame; the caller then fetches
     * events and passes the rest again.
     *
     * @return Number of bytes consumed (0..size). After the connection has
     *         been closed, everything is consumed and ignored.
     */
    std::size_t receive(const std::uint8_t* data, std::size_t size, TimeMs now) noexcept;

    /** @brief Advances the timers to @p now; may produce frames and events. */
    void tick(TimeMs now) noexcept;

    /**
     * @brief Controlling station: sends STARTDT act.
     *
     * @return IEC104_OK; IEC104_ERR_WRONG_STATE if not Stopped or if this is
     *         the controlled station; IEC104_ERR_WOULD_BLOCK if the output
     *         buffer is full.
     */
    iec104_status start(TimeMs now) noexcept;

    /**
     * @brief Controlling station: sends STOPDT act.
     *
     * @return IEC104_OK; IEC104_ERR_WRONG_STATE if not Started or if this is
     *         the controlled station; IEC104_ERR_WOULD_BLOCK if the output
     *         buffer is full.
     */
    iec104_status stop(TimeMs now) noexcept;

    /**
     * @brief Sends an ASDU in an I-frame.
     *
     * The ASDU is not interpreted; it is typically produced by
     * codec::encode_asdu().
     *
     * @param asdu   The ASDU octets. Must not be NULL.
     * @param length 1..249.
     * @return IEC104_OK; IEC104_ERR_INVALID_ARGUMENT for NULL or a bad length;
     *         IEC104_ERR_WRONG_STATE if data transfer is not Started;
     *         IEC104_ERR_WOULD_BLOCK if k I-frames are unacknowledged or the
     *         output buffer is full.
     */
    iec104_status send_asdu(const std::uint8_t* asdu, std::size_t length, TimeMs now) noexcept;

    /** @brief Closes the connection locally (CloseReason::LocalRequest). */
    void close() noexcept;

    /**
     * @brief Moves bytes to be sent into @p out.
     *
     * @return Number of bytes written, 0..size. Bytes not taken stay queued.
     */
    std::size_t take_output(std::uint8_t* out, std::size_t size) noexcept;

    /** @brief Fetches the oldest event. @return false if there is none. */
    bool next_event(Event& out) noexcept;

    /** @brief Earliest time at which tick() has something to do, or kNoDeadline. */
    [[nodiscard]] TimeMs next_deadline() const noexcept;

    /** @brief Current state. */
    [[nodiscard]] State state() const noexcept { return state_; }
    /** @brief Number of sent I-frames the peer has not acknowledged yet. */
    [[nodiscard]] std::uint16_t unacknowledged() const noexcept;
    /** @brief True if send_asdu() would currently accept an ASDU of maximum length. */
    [[nodiscard]] bool can_send() const noexcept;

  private:
    static constexpr std::size_t kEventCapacity = 8;
    static constexpr std::size_t kOutputCapacity = 4096;

    // Helpers, see connection.cpp.
    void handle_frame(const std::uint8_t* frame, const codec::Apci& apci, TimeMs now) noexcept;
    void handle_i_frame(const std::uint8_t* frame, const codec::Apci& apci, TimeMs now) noexcept;
    void handle_u_frame(codec::UFunction function, TimeMs now) noexcept;
    void handle_stop_request(TimeMs now) noexcept;
    void handle_confirmation(codec::UFunction function) noexcept;
    bool apply_ack(std::uint16_t recv_seq, TimeMs now) noexcept;
    bool send_u(codec::UFunction function, TimeMs now) noexcept;
    bool send_s(TimeMs now) noexcept;
    bool append_output(const std::uint8_t* data, std::size_t size) noexcept;
    void set_state(State next) noexcept;
    void fail(CloseReason reason) noexcept;
    void push_event(const Event& e) noexcept;
    void maybe_finish_stop(TimeMs now) noexcept;
    [[nodiscard]] TimeMs oldest_sent_at() const noexcept { return sent_at_[sent_head_]; }
    [[nodiscard]] std::size_t free_events() const noexcept { return kEventCapacity - event_count_; }

    ApciConfig config_{};
    bool configured_{false};
    State state_{State::Idle};

    // Sequence numbers (all modulo 32768).
    std::uint16_t vs_{0};         // V(S): N(S) of the next I-frame to send.
    std::uint16_t ack_{0};        // N(S) of the oldest unacknowledged sent I-frame.
    std::uint16_t vr_{0};         // V(R): expected N(S) of the next received I-frame.
    std::uint16_t unacked_rx_{0}; // received I-frames not yet acknowledged to the peer.

    // Timers: 0 = not running, otherwise the absolute expiry time.
    std::vector<TimeMs> sent_at_; // send time per unacknowledged I-frame, ring of k entries
    std::size_t sent_head_{0};    // index of the oldest unacknowledged frame in sent_at_
    TimeMs u_act_deadline_{0};    // t1 for an outstanding STARTDT/STOPDT/TESTFR act
    codec::UFunction u_act_pending_{codec::UFunction::None};
    TimeMs t2_deadline_{0};        // t2: acknowledge received I-frames
    TimeMs t3_deadline_{0};        // t3: idle test
    bool stop_con_pending_{false}; // controlled station: STOPDT con owed once all acks are in

    // Receive reassembly of one APDU.
    std::array<std::uint8_t, codec::kMaxApduLength> rx_{};
    std::size_t rx_fill_{0};

    // Output bytes and event queue (rings).
    std::array<std::uint8_t, kOutputCapacity> out_{};
    std::size_t out_fill_{0};
    std::array<Event, kEventCapacity> events_{};
    std::size_t event_head_{0};
    std::size_t event_count_{0};
};

/** @brief Short English name of a state, for logs and tests. */
const char* state_name(State state) noexcept;
/** @brief Short English name of a close reason, for logs and tests. */
const char* close_reason_name(CloseReason reason) noexcept;

} // namespace iec104::apci
