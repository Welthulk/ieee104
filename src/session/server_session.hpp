/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file server_session.hpp
 * @brief Application layer of the controlled station for one connection.
 *
 * The session answers the requests of the controlling station (clause 7 of
 * the standard, "Abläufe"; sequences follow the usual reading activation ->
 * confirmation -> data -> termination, docs/05_Normfragen_und_Festlegungen.md,
 * N8):
 *
 *   request (cause 6)          answer
 *   100 interrogation          ACTCON (7), points (cause 20 or 21..36), ACTTERM (10)
 *   101 counter interrogation  ACTCON, counters (cause 37 or 38..41), ACTTERM
 *   102 read (cause 5)         the point (cause 5), or negative cause 47
 *   103 clock sync             ACTCON, positive if the application accepts
 *   104, 107 test command      ACTCON (mirrored)
 *   105 reset process          ACTCON, positive if the application accepts
 *   45..51, 58..64 command     ACTCON (+/- by application), ACTTERM after execute
 *
 * Negative answers (P/N = 1) for unknown type (cause 44), wrong cause (45),
 * unknown common address (46) and unknown IOA (47) (docs/02_Technik_und_Quellen.md,
 * "Übertragungsursachen").
 *
 * Everything the session sends goes through one ordered queue, so that an
 * interrogation answer stays between its confirmation and its termination
 * even when the window k is full. A long interrogation is a job in this
 * queue that produces its ASDUs only when the connection can take them.
 *
 * Layer: session (no I/O; sending goes through the AsduSender interface).
 */
#pragma once

#include "codec/asdu.hpp"
#include "iec104/iec104.h"
#include "session/command_table.hpp"
#include "session/process_image.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <utility>
#include <vector>

namespace iec104::session {

/** @brief Where the session puts its ASDUs (implemented by the API over a TCP link). */
class AsduSender {
  public:
    AsduSender() = default;
    AsduSender(const AsduSender&) = delete;
    AsduSender& operator=(const AsduSender&) = delete;
    AsduSender(AsduSender&&) = delete;
    AsduSender& operator=(AsduSender&&) = delete;
    virtual ~AsduSender() = default;
    /** @brief True if an ASDU of maximum length can be sent now. */
    virtual bool can_send() = 0;
    /** @brief Sends one ASDU. @return IEC104_OK or IEC104_ERR_WOULD_BLOCK. */
    virtual iec104_status send(const std::uint8_t* asdu, std::size_t length) = 0;
    /** @brief The ASDU just sent was the buffered event @p id (see enqueue_event()). */
    virtual void sent_event(std::uint64_t /*id*/) {}
};

/** @brief The application's decision on commands (implemented by the API over a callback). */
class CommandHandler {
  public:
    CommandHandler() = default;
    CommandHandler(const CommandHandler&) = delete;
    CommandHandler& operator=(const CommandHandler&) = delete;
    CommandHandler(CommandHandler&&) = delete;
    CommandHandler& operator=(CommandHandler&&) = delete;
    virtual ~CommandHandler() = default;
    /** @brief A command, clock sync or reset arrived. @return true = accept. */
    virtual bool on_command(const codec::AsduHeader& header,
                            const codec::InformationObject& object) = 0;
};

/** @brief Options of the controlled station (iec104_server_options). */
struct SessionOptions {
    bool send_end_of_init{true};    ///< Type 70 on the first STARTDT of the connection.
    bool command_termination{true}; ///< ACTTERM after an accepted execute command.
    std::uint8_t cause_of_init{0};  ///< COI cause in type 70.
    /**
     * @brief Maximum age of a command with time tag (58..64, 107), 0 = no check.
     *
     * Older commands are discarded without answer (amendment 1, maximum
     * command delay; docs/05_Normfragen_und_Festlegungen.md, N6).
     */
    std::uint32_t max_command_delay_ms{0};
};

/** @brief The two clocks the session needs. */
struct SessionClock {
    std::uint64_t monotonic_ms{0}; ///< For the validity of selections.
    std::int64_t utc_ms{0};        ///< Wall clock (UTC, ms since 1970) for the command delay.
};

/**
 * @brief Receives notable events of a session (for logging). Every method may be ignored.
 */
class SessionObserver {
  public:
    SessionObserver() = default;
    SessionObserver(const SessionObserver&) = delete;
    SessionObserver& operator=(const SessionObserver&) = delete;
    SessionObserver(SessionObserver&&) = delete;
    SessionObserver& operator=(SessionObserver&&) = delete;
    virtual ~SessionObserver() = default;
    /** @brief A request was answered negatively (@p asdu is the request). */
    virtual void negative_answer(const std::uint8_t* asdu, std::size_t length,
                                 std::uint8_t cause) = 0;
    /** @brief A time-tagged command was discarded (maximum command delay). */
    virtual void discarded_command(const codec::AsduHeader& header,
                                   const codec::InformationObject& object) = 0;
};

/** @brief Counters of one session. */
struct SessionStats {
    std::uint64_t requests{0}; ///< Interrogations, counter interrogations, reads, tests.
    std::uint64_t commands_accepted{
        0}; ///< Commands, clock syncs, resets accepted by the application.
    std::uint64_t commands_rejected{0};  ///< ... rejected by the application.
    std::uint64_t commands_discarded{0}; ///< Discarded: older than the maximum command delay.
    std::uint64_t negative_answers{0};   ///< Answers with P/N = 1 (incl. causes 44..47).
};

/** @brief Everything the station provides to a session for handling one request (never NULL). */
struct StationContext {
    const ProcessImage* image{nullptr};     ///< Data points (interrogation, read).
    const CommandTable* commands{nullptr};  ///< Command points (empty = application decides).
    const SessionOptions* options{nullptr}; ///< Behaviour.
    SessionClock clock;                     ///< Current time.
    SessionObserver* observer{nullptr};     ///< Optional, may be NULL.
};

/** @brief Application layer of one connection of the controlled station. */
class ServerSession {
  public:
    /** @param profile Field lengths, maximum ASDU length, strict/tolerant. */
    ServerSession(const codec::AsduProfile& profile, std::uint8_t originator) noexcept
        : profile_(profile), originator_(originator) {}

    /**
     * @brief Handles a received ASDU.
     * @return true if the session handled it (a request or a rejected ASDU);
     *         false if it is data for the application (monitor direction,
     *         private type, malformed), which the caller passes on.
     */
    bool handle(const std::uint8_t* asdu, std::size_t length, const StationContext& station,
                CommandHandler& handler);

    /** @brief Data transfer was started: queue end of initialization once per connection. */
    void on_started(const ProcessImage& image, const SessionOptions& options);

    /**
     * @brief Queues an ASDU for sending (e.g. a spontaneous value).
     * @return IEC104_OK, or IEC104_ERR_WOULD_BLOCK if the queue holds kQueueLimit entries.
     */
    iec104_status enqueue(const std::uint8_t* asdu, std::size_t length);

    /**
     * @brief Queues a buffered event of the redundancy group (no queue limit:
     *        the group's buffer bounds it). @p id is reported to
     *        AsduSender::sent_event() when it is sent.
     */
    void enqueue_event(const std::uint8_t* asdu, std::size_t length, std::uint64_t id);

    /** @brief Removes all queued events (the group takes them back). */
    void drop_events();

    /** @brief Removes the queued event @p id (dropped by the group). */
    void drop_event(std::uint64_t id);

    /** @brief Sends queued ASDUs while @p sender can take them. */
    void pump(AsduSender& sender);

    /** @brief Counters since creation. */
    [[nodiscard]] const SessionStats& stats() const noexcept { return stats_; }

    /** @brief Number of queued entries (ASDUs and interrogation jobs). */
    [[nodiscard]] std::size_t queued() const noexcept { return queue_.size(); }

    /** @brief Upper bound of the queue for enqueue(); answers to requests ignore it. */
    static constexpr std::size_t kQueueLimit = 10000;

  private:
    /** @brief One entry of the send queue: a ready ASDU or an interrogation job. */
    struct Item {
        std::vector<std::uint8_t> asdu; // ready ASDU (if points is empty)
        std::uint64_t event_id{0};      // ready ASDU from the group buffer, 0 = none
        std::vector<Point> points;      // job: points still to send
        std::size_t next{0};            // job: index of the next point
        codec::AsduHeader header{};     // job: header of the data ASDUs
    };

    void answer(const std::uint8_t* asdu, std::size_t length, std::uint8_t cause, bool negative);
    void push_ready(std::vector<std::uint8_t> asdu);
    void push_encoded(const codec::AsduHeader& header, const codec::InformationObject& object);
    void push_job(const codec::AsduHeader& header, std::vector<Point> points);
    std::vector<std::uint8_t> mirror(const std::uint8_t* asdu, std::size_t length,
                                     std::uint8_t cause, bool negative) const;
    void answer_interrogation(const codec::DecodedAsdu& d, const codec::InformationObject& o,
                              const ProcessImage& image, const std::uint8_t* asdu,
                              std::size_t length);
    void answer_counters(const codec::DecodedAsdu& d, const codec::InformationObject& o,
                         const ProcessImage& image, const std::uint8_t* asdu, std::size_t length);
    void answer_read(const codec::DecodedAsdu& d, const codec::InformationObject& o,
                     const ProcessImage& image, const std::uint8_t* asdu, std::size_t length);
    void answer_command(const codec::DecodedAsdu& d, const codec::InformationObject& o,
                        const StationContext& station, CommandHandler& handler,
                        const std::uint8_t* asdu, std::size_t length);
    void answer_point_command(const codec::DecodedAsdu& d, const codec::InformationObject& o,
                              const CommandPoint& point, const StationContext& station,
                              CommandHandler& handler, const std::uint8_t* asdu,
                              std::size_t length);
    void execute(const codec::DecodedAsdu& d, const codec::InformationObject& o,
                 const StationContext& station, CommandHandler& handler, const std::uint8_t* asdu,
                 std::size_t length);
    bool pump_job(Item& job, AsduSender& sender);

    codec::AsduProfile profile_;
    std::uint8_t originator_;
    std::deque<Item> queue_;
    bool init_sent_{false};
    SessionStats stats_{};
    SessionObserver* observer_{nullptr}; // of the request being handled

    /** @brief A selected command waiting for its execute (select before operate). */
    struct Selection {
        std::uint8_t type{0};
        codec::InformationObject object{};
        std::uint64_t valid_until_ms{0};
    };
    std::map<std::pair<std::uint16_t, std::uint32_t>, Selection> selections_;
};

} // namespace iec104::session
