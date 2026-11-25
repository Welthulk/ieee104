/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file connection.cpp
 * @brief Implementation of the APCI state machine.
 *
 * Rules implemented here (R1..R12 are referenced in the code and in the
 * tests, tests/test_apci_connection.cpp). The normative text of clause 5 is
 * not available; docs/05_Quellen_APCI.md lists the public evidence per rule
 * (amendment 1 sample, figure 17 of the draft standard, interoperability
 * list 9.6, technical reports). Clause numbers follow the public table of
 * contents of edition 2.1 (5.1 loss/duplication, 5.2 test, 5.3 start/stop,
 * 5.5 k).
 *
 *   R1  I-frames only while data transfer is started. The controlled
 *       station closes on an I- or S-frame while stopped (figure 17).
 *   R2  STARTDT and STOPDT act come from the controlling station; it
 *       closes if the confirmation does not come (5.3).
 *   R3  U-frames are accepted while stopped; TESTFR act is answered in
 *       every state, by both sides (5.2, figure 17).
 *   R4  N(S) and N(R) are 15-bit numbers (modulo 32768), 0 after connect.
 *   R5  A received I-frame must carry N(S) = V(R); otherwise close
 *       (vendor documentation only; TODO(norm): 5.1).
 *   R6  N(R) acknowledges all numbers below N(R) (amendment 1, 5.1). An
 *       N(R) beyond the frames sent closes (no public source; TODO(norm): 5.1).
 *   R7  At most k I-frames are unacknowledged; then sending stops
 *       (TODO(norm): 5.5, stop or close is not proven).
 *   R8  Received I-frames are acknowledged at the latest after w frames ...
 *   R9  ... or after t2 without own data (S-frame); an own I-frame
 *       acknowledges with its N(R) as well (interoperability list).
 *   R10 t1 supervises sent I-frames and U-frames act; expiry closes
 *       (figure 17, note 2; interoperability list).
 *   R11 t3: no frame received for t3 -> send TESTFR act
 *       (TODO(norm): 5.2, start condition of t3).
 *   R12 STOPDT: the controlled station confirms at once if all its I-frames
 *       are acknowledged, otherwise after the S-frame that acknowledges the
 *       rest; an I-frame in between closes (figure 17). The controlling
 *       station acknowledges at once what it receives after its STOPDT act
 *       (own decision, figure 18 is not public).
 *
 * Layer: apci (sans-I/O).
 */
#include "apci/connection.hpp"

#include <algorithm>
#include <cstring>

namespace iec104::apci {
namespace {

using codec::FrameFormat;
using codec::UFunction;

constexpr std::uint16_t kSeqMask = codec::kSequenceModulo - 1U;

/** @brief (a - b) modulo 32768: how far sequence number a is ahead of b (R4). */
std::uint16_t seq_distance(std::uint16_t a, std::uint16_t b) noexcept {
    return static_cast<std::uint16_t>((a - b) & kSeqMask);
}

/** @brief a + 1 modulo 32768 (R4). */
std::uint16_t seq_next(std::uint16_t a) noexcept {
    return static_cast<std::uint16_t>((a + 1U) & kSeqMask);
}

// Room kept free in the output buffer for control frames (S-frame, U-frames),
// so that acknowledgements and confirmations never fail because ASDUs filled
// the buffer.
constexpr std::size_t kControlReserve = 4U * codec::kApciLength;

// Events one received frame can produce at most (ASDU + acknowledgement +
// two state changes); receive() only processes a frame if they fit.
constexpr std::size_t kEventsPerFrame = 4U;

} // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

iec104_status Connection::init(const ApciConfig& config) noexcept {
    ConfigWarnings warnings{};
    const iec104_status st = validate(config, warnings);
    if (st != IEC104_OK) {
        configured_ = false;
        return st;
    }
    *this = Connection{};
    config_ = config;
    configured_ = true;
    // The only allocation of the class: one time stamp per possible
    // unacknowledged I-frame (R7, R10).
    sent_at_.assign(config.k, 0U);
    return IEC104_OK;
}

iec104_status Connection::open(TimeMs now) noexcept {
    if (!configured_ || state_ != State::Idle) {
        return IEC104_ERR_WRONG_STATE;
    }
    set_state(State::Stopped);
    if (config_.t3_ms != 0U) {
        t3_deadline_ = now + config_.t3_ms;
    }
    return IEC104_OK;
}

void Connection::close() noexcept {
    if (state_ != State::Closed) {
        fail(CloseReason::LocalRequest);
    }
}

// ---------------------------------------------------------------------------
// Input: bytes
// ---------------------------------------------------------------------------

std::size_t Connection::receive(const std::uint8_t* data, std::size_t size, TimeMs now) noexcept {
    if (data == nullptr || state_ == State::Idle) {
        return 0U;
    }
    std::size_t pos = 0;
    while (pos < size) {
        if (state_ == State::Closed) {
            return size; // nothing is interpreted after close
        }
        if (free_events() < kEventsPerFrame) {
            break; // caller fetches events first
        }
        // Collect the two octets that give the APDU length, then the rest.
        if (rx_fill_ < 2U) {
            const std::size_t n = std::min<std::size_t>(2U - rx_fill_, size - pos);
            std::memcpy(&rx_[rx_fill_], data + pos, n);
            rx_fill_ += n;
            pos += n;
            if (rx_fill_ < 2U) {
                break;
            }
        }
        std::size_t apdu_length = 0;
        if (codec::peek_apdu_length(rx_.data(), rx_fill_, apdu_length) != IEC104_OK) {
            fail(CloseReason::MalformedFrame); // no way to find the next frame start
            return size;
        }
        const std::size_t n = std::min(apdu_length - rx_fill_, size - pos);
        std::memcpy(&rx_[rx_fill_], data + pos, n);
        rx_fill_ += n;
        pos += n;
        if (rx_fill_ < apdu_length) {
            break; // rest of the frame comes with the next call
        }
        codec::Apci apci{};
        if (codec::decode_apci(rx_.data(), rx_fill_, apci) != IEC104_OK) {
            fail(CloseReason::MalformedFrame);
            return size;
        }
        handle_frame(rx_.data(), apci, now);
        rx_fill_ = 0;
    }
    return pos;
}

void Connection::handle_frame(const std::uint8_t* frame, const codec::Apci& apci,
                              TimeMs now) noexcept {
    // R11: t3 measures the time since the last received frame of any format.
    // TODO(norm): exact start condition of t3, clause 5 (docs/04, N1).
    if (config_.t3_ms != 0U) {
        t3_deadline_ = now + config_.t3_ms;
    }
    switch (apci.format) {
    case FrameFormat::I:
        handle_i_frame(frame, apci, now);
        break;
    case FrameFormat::S:
        // R1: a stopped controlled station has nothing to be acknowledged.
        if (config_.role == Role::Controlled && state_ == State::Stopped && config_.strict) {
            fail(CloseReason::UnexpectedFrame);
            return;
        }
        if (!apply_ack(apci.recv_seq, now)) {
            fail(CloseReason::AckOutOfWindow);
        }
        break;
    case FrameFormat::U:
        handle_u_frame(apci.u_function, now);
        break;
    }
}

void Connection::handle_i_frame(const std::uint8_t* frame, const codec::Apci& apci,
                                TimeMs now) noexcept {
    // R1, R12: data only while started. The controlling station still
    // accepts frames the peer sent before it saw the STOPDT act; the
    // controlled station must not receive data after STOPDT act (figure 17).
    // Tolerant mode accepts data outside these states.
    // TODO(norm): 5.3, figure 18 (controlling station) is not public.
    const bool allowed = state_ == State::Started ||
                         (state_ == State::StopPending && config_.role == Role::Controlling);
    if (!allowed && config_.strict) {
        fail(CloseReason::UnexpectedFrame);
        return;
    }
    // R5: frames must arrive without gap or repetition.
    if (apci.send_seq != vr_) {
        fail(CloseReason::SequenceError);
        return;
    }
    // R6: the piggy-backed N(R) acknowledges own frames.
    if (!apply_ack(apci.recv_seq, now)) {
        fail(CloseReason::AckOutOfWindow);
        return;
    }
    if (state_ == State::Closed) {
        return;
    }
    vr_ = seq_next(vr_);
    ++unacked_rx_;

    Event e{};
    e.kind = EventKind::AsduReceived;
    e.asdu_length = apci.asdu_length;
    std::memcpy(e.asdu.data(), frame + codec::kApciLength, apci.asdu_length);
    push_event(e);

    // R8: acknowledge at the latest after w frames; R9: otherwise within t2.
    // R12: after an own STOPDT act acknowledge at once, so that the peer can
    // confirm the stop without waiting for t2.
    const bool stopping = state_ == State::StopPending && config_.role == Role::Controlling;
    if (unacked_rx_ >= config_.w || stopping) {
        (void)send_s(now);
    } else if (t2_deadline_ == 0U) {
        t2_deadline_ = now + config_.t2_ms;
    }
}

void Connection::handle_u_frame(UFunction function, TimeMs now) noexcept {
    const bool controlled = config_.role == Role::Controlled;
    switch (function) {
    case UFunction::TestFrAct:
        // R3: answered in every state.
        (void)send_u(UFunction::TestFrCon, now);
        return;
    case UFunction::TestFrCon:
        if (u_act_pending_ == UFunction::TestFrAct) {
            u_act_pending_ = UFunction::None;
            u_act_deadline_ = 0U;
        } else if (config_.strict) {
            fail(CloseReason::UnexpectedFrame); // confirmation without activation
        }
        return;
    case UFunction::StartDtAct:
        // R2: only the controlling station activates.
        if (!controlled) {
            fail(CloseReason::UnexpectedFrame);
            return;
        }
        if (send_u(UFunction::StartDtCon, now) && state_ == State::Stopped) {
            set_state(State::Started);
        }
        return;
    case UFunction::StopDtAct:
        if (!controlled) {
            fail(CloseReason::UnexpectedFrame);
            return;
        }
        handle_stop_request(now);
        return;
    case UFunction::StartDtCon:
    case UFunction::StopDtCon:
        handle_confirmation(function);
        return;
    case UFunction::None:
        break;
    }
    fail(CloseReason::MalformedFrame); // decode_apci() never yields None for a U-frame
}

void Connection::handle_stop_request(TimeMs now) noexcept {
    // R12: first acknowledge everything received; confirm when the peer has
    // acknowledged all own I-frames (at once if there are none).
    if (unacked_rx_ > 0U) {
        (void)send_s(now);
    }
    if (state_ == State::Started) {
        set_state(State::StopPending);
    }
    stop_con_pending_ = true;
    maybe_finish_stop(now);
}

void Connection::handle_confirmation(UFunction function) noexcept {
    // R2: confirmations go to the controlling station and only for an open act.
    const UFunction expected =
        function == UFunction::StartDtCon ? UFunction::StartDtAct : UFunction::StopDtAct;
    if (config_.role == Role::Controlled || u_act_pending_ != expected) {
        if (config_.role == Role::Controlled || config_.strict) {
            fail(CloseReason::UnexpectedFrame);
        }
        return;
    }
    u_act_pending_ = UFunction::None;
    u_act_deadline_ = 0U;
    set_state(function == UFunction::StartDtCon ? State::Started : State::Stopped);
}

bool Connection::apply_ack(std::uint16_t recv_seq, TimeMs now) noexcept {
    // R6: N(R) lies between the oldest unacknowledged frame and V(S).
    const std::uint16_t newly = seq_distance(recv_seq, ack_);
    if (newly > unacknowledged()) {
        return false;
    }
    if (newly == 0U) {
        return true;
    }
    ack_ = recv_seq;
    sent_head_ = (sent_head_ + newly) % sent_at_.size();
    Event e{};
    e.kind = EventKind::Acknowledged;
    e.count = newly;
    push_event(e);
    maybe_finish_stop(now);
    return true;
}

void Connection::maybe_finish_stop(TimeMs now) noexcept {
    if (config_.role == Role::Controlled && stop_con_pending_ && unacknowledged() == 0U) {
        if (send_u(UFunction::StopDtCon, now)) {
            stop_con_pending_ = false;
            set_state(State::Stopped);
        }
    }
}

// ---------------------------------------------------------------------------
// Input: time
// ---------------------------------------------------------------------------

void Connection::tick(TimeMs now) noexcept {
    if (state_ == State::Idle || state_ == State::Closed) {
        return;
    }
    // R10: t1 for the oldest unacknowledged I-frame and for an open act.
    if (unacknowledged() > 0U && now >= oldest_sent_at() + config_.t1_ms) {
        fail(CloseReason::T1Timeout);
        return;
    }
    if (u_act_pending_ != UFunction::None && now >= u_act_deadline_) {
        fail(CloseReason::T1Timeout);
        return;
    }
    // R9: acknowledge received frames when t2 expires.
    if (t2_deadline_ != 0U && now >= t2_deadline_) {
        (void)send_s(now);
    }
    // R11: idle test. Only one act at a time is supervised by t1, so a
    // test is not started while STARTDT or STOPDT is open.
    if (t3_deadline_ != 0U && now >= t3_deadline_) {
        if (u_act_pending_ == UFunction::None) {
            (void)send_u(UFunction::TestFrAct, now);
        }
        t3_deadline_ = now + config_.t3_ms;
    }
}

TimeMs Connection::next_deadline() const noexcept {
    if (state_ == State::Idle || state_ == State::Closed) {
        return kNoDeadline;
    }
    TimeMs next = kNoDeadline;
    if (unacknowledged() > 0U) {
        next = std::min(next, oldest_sent_at() + config_.t1_ms);
    }
    if (u_act_pending_ != UFunction::None) {
        next = std::min(next, u_act_deadline_);
    }
    for (const TimeMs t : {t2_deadline_, t3_deadline_}) {
        if (t != 0U) {
            next = std::min(next, t);
        }
    }
    return next;
}

// ---------------------------------------------------------------------------
// Input: requests of the application
// ---------------------------------------------------------------------------

iec104_status Connection::start(TimeMs now) noexcept {
    // R2: STARTDT act comes from the controlling station.
    if (config_.role != Role::Controlling || state_ != State::Stopped ||
        u_act_pending_ != UFunction::None) {
        return IEC104_ERR_WRONG_STATE;
    }
    if (free_events() == 0U || !send_u(UFunction::StartDtAct, now)) {
        return IEC104_ERR_WOULD_BLOCK;
    }
    set_state(State::StartPending);
    return IEC104_OK;
}

iec104_status Connection::stop(TimeMs now) noexcept {
    if (config_.role != Role::Controlling || state_ != State::Started ||
        u_act_pending_ != UFunction::None) {
        return IEC104_ERR_WRONG_STATE;
    }
    if (free_events() == 0U) {
        return IEC104_ERR_WOULD_BLOCK;
    }
    // R12: acknowledge everything received before asking the peer to stop.
    if (unacked_rx_ > 0U && !send_s(now)) {
        return IEC104_ERR_WOULD_BLOCK;
    }
    if (!send_u(UFunction::StopDtAct, now)) {
        return IEC104_ERR_WOULD_BLOCK;
    }
    set_state(State::StopPending);
    return IEC104_OK;
}

iec104_status Connection::send_asdu(const std::uint8_t* asdu, std::size_t length,
                                    TimeMs now) noexcept {
    if (asdu == nullptr || length == 0U || length > codec::kMaxAsduLength) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (state_ != State::Started) {
        return IEC104_ERR_WRONG_STATE; // R1
    }
    if (unacknowledged() >= config_.k ||
        kOutputCapacity - out_fill_ < codec::kApciLength + length + kControlReserve) {
        return IEC104_ERR_WOULD_BLOCK; // R7, or the caller has not taken the output
    }
    std::array<std::uint8_t, codec::kApciLength> header{};
    // R9: N(R) = V(R) acknowledges all received frames on the way.
    if (codec::encode_i_header(vs_, vr_, length, header.data(), header.size()) != IEC104_OK) {
        return IEC104_ERR_INTERNAL;
    }
    (void)append_output(header.data(), header.size());
    (void)append_output(asdu, length);

    // R10: remember when the frame left, t1 runs from here.
    sent_at_[(sent_head_ + unacknowledged()) % sent_at_.size()] = now;
    vs_ = seq_next(vs_);
    unacked_rx_ = 0;
    t2_deadline_ = 0;
    return IEC104_OK;
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

bool Connection::send_u(UFunction function, TimeMs now) noexcept {
    std::array<std::uint8_t, codec::kApciLength> frame{};
    if (codec::encode_u_frame(function, frame.data(), frame.size()) != IEC104_OK ||
        !append_output(frame.data(), frame.size())) {
        return false;
    }
    // R10: every act is supervised by t1 until its confirmation arrives.
    if (function == UFunction::StartDtAct || function == UFunction::StopDtAct ||
        function == UFunction::TestFrAct) {
        u_act_pending_ = function;
        u_act_deadline_ = now + config_.t1_ms;
    }
    return true;
}

bool Connection::send_s(TimeMs /*now*/) noexcept {
    std::array<std::uint8_t, codec::kApciLength> frame{};
    if (codec::encode_s_frame(vr_, frame.data(), frame.size()) != IEC104_OK ||
        !append_output(frame.data(), frame.size())) {
        return false;
    }
    unacked_rx_ = 0;
    t2_deadline_ = 0;
    return true;
}

bool Connection::append_output(const std::uint8_t* data, std::size_t size) noexcept {
    if (kOutputCapacity - out_fill_ < size) {
        // Only possible if the caller never takes the output: control frames
        // have their reserve. Closing is the only safe reaction.
        fail(CloseReason::OutputOverflow);
        return false;
    }
    std::memcpy(&out_[out_fill_], data, size);
    out_fill_ += size;
    return true;
}

std::size_t Connection::take_output(std::uint8_t* out, std::size_t size) noexcept {
    if (out == nullptr) {
        return 0U;
    }
    const std::size_t n = std::min(size, out_fill_);
    std::memcpy(out, out_.data(), n);
    std::memmove(out_.data(), out_.data() + n, out_fill_ - n);
    out_fill_ -= n;
    return n;
}

bool Connection::next_event(Event& out) noexcept {
    if (event_count_ == 0U) {
        return false;
    }
    out = events_[event_head_];
    event_head_ = (event_head_ + 1U) % kEventCapacity;
    --event_count_;
    return true;
}

void Connection::push_event(const Event& e) noexcept {
    // receive(), start() and stop() check the free space first; tick() only
    // produces CloseRequested, for which fail() keeps the last slot.
    if (event_count_ == kEventCapacity) {
        return;
    }
    events_[(event_head_ + event_count_) % kEventCapacity] = e;
    ++event_count_;
}

void Connection::set_state(State next) noexcept {
    if (next == state_) {
        return;
    }
    state_ = next;
    Event e{};
    e.kind = EventKind::StateChanged;
    e.state = next;
    push_event(e);
}

void Connection::fail(CloseReason reason) noexcept {
    if (state_ == State::Closed) {
        return;
    }
    state_ = State::Closed;
    u_act_pending_ = UFunction::None;
    t2_deadline_ = t3_deadline_ = u_act_deadline_ = 0U;
    // The close event must not get lost: drop the newest event if the queue is full.
    if (event_count_ == kEventCapacity) {
        --event_count_;
    }
    Event e{};
    e.kind = EventKind::CloseRequested;
    e.state = State::Closed;
    e.reason = reason;
    push_event(e);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::uint16_t Connection::unacknowledged() const noexcept {
    return seq_distance(vs_, ack_);
}

bool Connection::can_send() const noexcept {
    return state_ == State::Started && unacknowledged() < config_.k &&
           kOutputCapacity - out_fill_ >= codec::kMaxApduLength + kControlReserve;
}

const char* state_name(State state) noexcept {
    switch (state) {
    case State::Idle:
        return "idle";
    case State::Stopped:
        return "stopped";
    case State::StartPending:
        return "start pending";
    case State::Started:
        return "started";
    case State::StopPending:
        return "stop pending";
    case State::Closed:
        return "closed";
    }
    return "?";
}

const char* close_reason_name(CloseReason reason) noexcept {
    switch (reason) {
    case CloseReason::None:
        return "none";
    case CloseReason::T1Timeout:
        return "t1 timeout";
    case CloseReason::SequenceError:
        return "sequence error";
    case CloseReason::AckOutOfWindow:
        return "acknowledge out of window";
    case CloseReason::MalformedFrame:
        return "malformed frame";
    case CloseReason::UnexpectedFrame:
        return "unexpected frame";
    case CloseReason::LocalRequest:
        return "local request";
    case CloseReason::OutputOverflow:
        return "output overflow";
    }
    return "?";
}

} // namespace iec104::apci
