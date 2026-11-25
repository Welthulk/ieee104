/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file server_session.cpp
 * @brief Implementation of the application layer of the controlled station.
 *
 * Layer: session (no I/O).
 */
#include "session/server_session.hpp"

#include <array>
#include <utility>

namespace iec104::session {
namespace {

// Causes of transmission (docs/02_Technik_und_Quellen.md, "Übertragungsursachen").
constexpr std::uint8_t kCotSpontaneousInit = 4;
constexpr std::uint8_t kCotRequest = 5;
constexpr std::uint8_t kCotActivation = 6;
constexpr std::uint8_t kCotActivationCon = 7;
constexpr std::uint8_t kCotDeactivation = 8;
constexpr std::uint8_t kCotDeactivationCon = 9;
constexpr std::uint8_t kCotActivationTerm = 10;
constexpr std::uint8_t kCotStation = 20;  // 21..36 = group 1..16
constexpr std::uint8_t kCotCounters = 37; // 38..41 = counter group 1..4
constexpr std::uint8_t kCotUnknownType = 44;
constexpr std::uint8_t kCotUnknownCause = 45;
constexpr std::uint8_t kCotUnknownStation = 46;
constexpr std::uint8_t kCotUnknownIoa = 47;

// Type identifications handled here.
constexpr std::uint8_t kEndOfInit = 70;
constexpr std::uint8_t kInterrogation = 100;
constexpr std::uint8_t kCounterInterrogation = 101;
constexpr std::uint8_t kRead = 102;
constexpr std::uint8_t kClockSync = 103;
constexpr std::uint8_t kTest = 104;
constexpr std::uint8_t kResetProcess = 105;
constexpr std::uint8_t kTestWithTime = 107;

constexpr std::uint8_t kLastGroup = 36;  // QOI 21..36 = group 1..16
constexpr std::uint8_t kAllCounters = 5; // RQT 5 = general counter request

/** @brief Commands of the control direction (45..51 without, 58..64 with time tag). */
bool is_command(std::uint8_t type) noexcept {
    return (type >= 45U && type <= 51U) || (type >= 58U && type <= 64U);
}

/** @brief S/E bit of a command object; bitstring commands have none. */
bool is_select(std::uint8_t type, const codec::InformationObject& o) noexcept {
    switch (type) {
    case 45:
    case 58:
        return o.sco.select;
    case 46:
    case 59:
        return o.dco.select;
    case 47:
    case 60:
        return o.rco.select;
    case 48:
    case 49:
    case 50:
    case 61:
    case 62:
    case 63:
        return o.qos.select;
    default:
        return false;
    }
}

/** @brief Broadcast common address for the CA length of the profile (65535 or 255). */
std::uint16_t broadcast_address(const codec::AsduProfile& profile) noexcept {
    return profile.ca_length == 1U ? 0xFFU : 0xFFFFU;
}

} // namespace

// ---------------------------------------------------------------------------
// Incoming requests
// ---------------------------------------------------------------------------

bool ServerSession::handle(const std::uint8_t* asdu, std::size_t length, const ProcessImage& image,
                           CommandHandler& handler, const SessionOptions& options) {
    codec::AsduHeader header{};
    std::size_t header_length = 0;
    if (codec::decode_asdu_header(asdu, length, profile_, header, header_length) != IEC104_OK ||
        codec::is_private_type(header.type_id)) {
        return false; // nothing the session can answer; raw data for the application
    }
    std::array<codec::InformationObject, codec::kMaxObjects> objects{};
    codec::DecodedAsdu d{};
    const iec104_status st =
        codec::decode_asdu(asdu, length, profile_, objects.data(), objects.size(), d);
    if (st == IEC104_ERR_UNSUPPORTED) {
        // Unknown type, or a type the strict profile rejects: negative mirror
        // with cause 44 (docs/02_Technik_und_Quellen.md, "Übertragungsursachen").
        push_ready(mirror(asdu, length, kCotUnknownType, true));
        return true;
    }
    if (st != IEC104_OK) {
        return true; // malformed: no meaningful answer; the raw ASDU was already reported
    }
    if (d.type->direction == codec::Direction::Monitor) {
        return false; // data for the application
    }
    if (d.object_count == 0U) {
        return true;
    }
    const codec::InformationObject& o = objects[0];
    const std::uint8_t type = header.type_id;

    // Station check. The broadcast address is accepted for the system
    // commands interrogation, counter interrogation, clock sync and reset.
    // An empty image means the application manages its stations itself.
    // TODO(norm): broadcast address per type, clause unknown
    // (docs/05_Normfragen_und_Festlegungen.md, N10).
    const bool broadcast = header.common_address == broadcast_address(profile_);
    const bool system = type == kInterrogation || type == kCounterInterrogation ||
                        type == kClockSync || type == kResetProcess;
    if (!image.empty() && !(broadcast && system) && !image.has_station(header.common_address)) {
        push_ready(mirror(asdu, length, kCotUnknownStation, true));
        return true;
    }

    switch (type) {
    case kInterrogation:
        answer_interrogation(d, o, image, asdu, length);
        break;
    case kCounterInterrogation:
        answer_counters(d, o, image, asdu, length);
        break;
    case kRead:
        answer_read(d, o, image, asdu, length);
        break;
    case kTest:
    case kTestWithTime:
        // The test command is confirmed by mirroring it (cause 7).
        push_ready(mirror(asdu, length,
                          header.cause == kCotActivation ? kCotActivationCon : kCotUnknownCause,
                          header.cause != kCotActivation));
        break;
    default:
        if (is_command(type) || type == kClockSync || type == kResetProcess) {
            answer_command(d, o, handler, options, asdu, length);
        } else {
            // Control-direction types the library does not implement yet
            // (parameters 110..113, delay acquisition 106): negative confirmation.
            push_ready(mirror(asdu, length,
                              header.cause == kCotActivation ? kCotActivationCon : kCotUnknownCause,
                              true));
        }
        break;
    }
    return true;
}

void ServerSession::answer_interrogation(const codec::DecodedAsdu& d,
                                         const codec::InformationObject& o,
                                         const ProcessImage& image, const std::uint8_t* asdu,
                                         std::size_t length) {
    const codec::AsduHeader& h = d.header;
    if (h.cause == kCotDeactivation) {
        // Stopping a running interrogation is not supported: negative DEACTCON.
        // TODO(norm): deactivation of an interrogation, clause 7.
        push_ready(mirror(asdu, length, kCotDeactivationCon, true));
        return;
    }
    if (h.cause != kCotActivation) {
        push_ready(mirror(asdu, length, kCotUnknownCause, true));
        return;
    }
    if (o.ioa != 0U) {
        push_ready(mirror(asdu, length, kCotUnknownIoa, true)); // interrogation uses IOA 0
        return;
    }
    if (o.qoi < kCotStation || o.qoi > kLastGroup) {
        push_ready(mirror(asdu, length, kCotActivationCon, true));
        return;
    }
    // QOI 20..36 and the cause of the answers 20..36 have the same values:
    // station interrogation answers with 20, group n with 20 + n.
    const unsigned group = o.qoi == kCotStation ? 0U : static_cast<unsigned>(o.qoi - kCotStation);
    const bool broadcast = h.common_address == broadcast_address(profile_);
    const std::vector<std::uint16_t> stations =
        broadcast ? image.stations() : std::vector<std::uint16_t>{h.common_address};
    for (const std::uint16_t station : stations) {
        // Confirmation, data, termination, each with the address of the
        // station (also when the request used the broadcast address).
        codec::AsduHeader reply = h;
        reply.common_address = station;
        reply.cause = kCotActivationCon;
        push_encoded(reply, o);

        codec::AsduHeader data = h;
        data.common_address = station;
        data.cause = o.qoi;
        push_job(data, image.select(station, false, group));

        reply.cause = kCotActivationTerm;
        push_encoded(reply, o);
    }
}

void ServerSession::answer_counters(const codec::DecodedAsdu& d, const codec::InformationObject& o,
                                    const ProcessImage& image, const std::uint8_t* asdu,
                                    std::size_t length) {
    const codec::AsduHeader& h = d.header;
    if (h.cause != kCotActivation) {
        push_ready(mirror(asdu, length, kCotUnknownCause, true));
        return;
    }
    if (o.ioa != 0U) {
        push_ready(mirror(asdu, length, kCotUnknownIoa, true));
        return;
    }
    if (o.qcc.rqt < 1U || o.qcc.rqt > kAllCounters) {
        push_ready(mirror(asdu, length, kCotActivationCon, true));
        return;
    }
    const unsigned group = o.qcc.rqt == kAllCounters ? 0U : o.qcc.rqt;
    const bool broadcast = h.common_address == broadcast_address(profile_);
    const std::vector<std::uint16_t> stations =
        broadcast ? image.stations() : std::vector<std::uint16_t>{h.common_address};
    for (const std::uint16_t station : stations) {
        codec::AsduHeader reply = h;
        reply.common_address = station;
        reply.cause = kCotActivationCon;
        push_encoded(reply, o);
        // FRZ 0 = read: report the counters. Freeze and reset (FRZ 1..3) are
        // confirmed, but the image has no frozen values to report.
        // TODO(norm): freeze/reset semantics of the counter interrogation, clause 7.
        if (o.qcc.frz == 0U) {
            codec::AsduHeader data = h;
            data.common_address = station;
            data.cause = static_cast<std::uint8_t>(kCotCounters + (group == 0U ? 0U : group));
            data.negative = false;
            push_job(data, image.select(station, true, group));
        }
        reply.cause = kCotActivationTerm;
        push_encoded(reply, o);
    }
}

void ServerSession::answer_read(const codec::DecodedAsdu& d, const codec::InformationObject& o,
                                const ProcessImage& image, const std::uint8_t* asdu,
                                std::size_t length) {
    const codec::AsduHeader& h = d.header;
    if (h.cause != kCotRequest) {
        push_ready(mirror(asdu, length, kCotUnknownCause, true));
        return;
    }
    const Point* p = image.find(h.common_address, o.ioa);
    if (p == nullptr) {
        push_ready(mirror(asdu, length, kCotUnknownIoa, true));
        return;
    }
    codec::AsduHeader data = h;
    data.type_id = p->type;
    data.cause = kCotRequest;
    push_encoded(data, p->value);
}

void ServerSession::answer_command(const codec::DecodedAsdu& d, const codec::InformationObject& o,
                                   CommandHandler& handler, const SessionOptions& options,
                                   const std::uint8_t* asdu, std::size_t length) {
    const codec::AsduHeader& h = d.header;
    const bool command = is_command(h.type_id);
    if (h.cause == kCotDeactivation && command) {
        // Breaking off a selected command. Select/execute supervision follows
        // later; until then the deactivation is confirmed.
        push_ready(mirror(asdu, length, kCotDeactivationCon, false));
        return;
    }
    if (h.cause != kCotActivation) {
        push_ready(mirror(asdu, length, kCotUnknownCause, true));
        return;
    }
    const bool accepted = handler.on_command(h, o);
    push_ready(mirror(asdu, length, kCotActivationCon, !accepted));
    // Termination after an executed command; a select is only confirmed.
    if (accepted && command && options.command_termination && !is_select(h.type_id, o)) {
        push_ready(mirror(asdu, length, kCotActivationTerm, false));
    }
}

// ---------------------------------------------------------------------------
// Outgoing
// ---------------------------------------------------------------------------

void ServerSession::on_started(const ProcessImage& image, const SessionOptions& options) {
    if (!options.send_end_of_init || init_sent_) {
        return;
    }
    init_sent_ = true;
    // One end of initialization per station (type 70, cause 4, IOA 0;
    // docs/05_Normfragen_und_Festlegungen.md, N8).
    for (const std::uint16_t station : image.stations()) {
        codec::AsduHeader h{};
        h.type_id = kEndOfInit;
        h.cause = kCotSpontaneousInit;
        h.originator = originator_;
        h.common_address = station;
        codec::InformationObject o{};
        o.coi = codec::Coi{options.cause_of_init, false};
        push_encoded(h, o);
    }
}

iec104_status ServerSession::enqueue(const std::uint8_t* asdu, std::size_t length) {
    if (queue_.size() >= kQueueLimit) {
        return IEC104_ERR_WOULD_BLOCK;
    }
    push_ready({asdu, asdu + length});
    return IEC104_OK;
}

void ServerSession::push_ready(std::vector<std::uint8_t> asdu) {
    if (asdu.empty()) {
        return;
    }
    Item item{};
    item.asdu = std::move(asdu);
    queue_.push_back(std::move(item));
}

void ServerSession::push_encoded(const codec::AsduHeader& header,
                                 const codec::InformationObject& object) {
    std::array<std::uint8_t, codec::kMaxAsduLength> buf{};
    std::size_t n = 0;
    if (codec::encode_asdu(header, &object, 1, profile_, buf.data(), buf.size(), n) == IEC104_OK) {
        push_ready({buf.begin(), buf.begin() + static_cast<long>(n)});
    }
}

void ServerSession::push_job(const codec::AsduHeader& header, std::vector<Point> points) {
    if (points.empty()) {
        return; // nothing to report: confirmation and termination follow directly
    }
    Item item{};
    item.header = header;
    item.points = std::move(points);
    queue_.push_back(std::move(item));
}

std::vector<std::uint8_t> ServerSession::mirror(const std::uint8_t* asdu, std::size_t length,
                                                std::uint8_t cause, bool negative) const {
    // The answer is the request itself with a new cause and P/N bit; only the
    // data unit identifier changes. This also works for types the codec
    // does not know (cause 44).
    codec::AsduHeader h{};
    std::size_t header_length = 0;
    if (codec::decode_asdu_header(asdu, length, profile_, h, header_length) != IEC104_OK) {
        return {};
    }
    h.cause = cause;
    h.negative = negative;
    std::vector<std::uint8_t> out(asdu, asdu + length);
    std::size_t written = 0;
    if (codec::encode_asdu_header(h, profile_, out.data(), out.size(), written) != IEC104_OK) {
        return {};
    }
    return out;
}

void ServerSession::pump(AsduSender& sender) {
    while (!queue_.empty() && sender.can_send()) {
        Item& item = queue_.front();
        if (item.points.empty()) {
            if (sender.send(item.asdu.data(), item.asdu.size()) != IEC104_OK) {
                return;
            }
            queue_.pop_front();
            continue;
        }
        if (!pump_job(item, sender)) {
            return;
        }
        if (item.next >= item.points.size()) {
            queue_.pop_front();
        }
    }
}

bool ServerSession::pump_job(Item& job, AsduSender& sender) {
    // Collect the next run of points of the same type (at most 127).
    const std::uint8_t type = job.points[job.next].type;
    std::vector<codec::InformationObject> objects;
    for (std::size_t i = job.next;
         i < job.points.size() && job.points[i].type == type && objects.size() < codec::kMaxObjects;
         ++i) {
        objects.push_back(job.points[i].value);
    }
    codec::AsduHeader h = job.header;
    h.type_id = type;
    h.sequence = false;
    std::array<std::uint8_t, codec::kMaxAsduLength> buf{};
    std::size_t n = 0;
    std::size_t used = 0;
    if (codec::encode_asdu_fitting(h, objects.data(), objects.size(), profile_, buf.data(),
                                   buf.size(), n, used) != IEC104_OK) {
        ++job.next; // a point that cannot be encoded is skipped, not retried forever
        return true;
    }
    if (sender.send(buf.data(), n) != IEC104_OK) {
        return false;
    }
    job.next += used;
    return true;
}

} // namespace iec104::session
