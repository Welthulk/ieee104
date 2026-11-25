/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file command_tracker.hpp
 * @brief Controlling station: follows every sent command until it has a result.
 *
 * A command runs through up to three steps (usual reading of clause 7,
 * docs/05_Normfragen_und_Festlegungen.md, N8):
 *
 *   activation (cause 6)  ->  confirmation (cause 7, positive or negative)
 *                         ->  termination (cause 10), for executed commands
 *
 * The tracker matches the answers by common address, type and IOA, watches
 * two time limits (confirmation, termination; set separately, because peers
 * differ in how long they take to execute) and reports one result per command. Some peers send no
 * termination (e.g. for set points); then expect_termination = false makes
 * the positive confirmation the final result.
 *
 * For select before operate the tracker can execute automatically: after a
 * positive confirmation of the select it hands back the execute to send.
 *
 * Layer: session (no I/O; time is passed in).
 */
#pragma once

#include "codec/asdu.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace iec104::session {

/** @brief Final result of one command. */
enum class CommandResult : std::uint8_t {
    Confirmed,      ///< Positive confirmation; no termination expected.
    Terminated,     ///< Positive confirmation and termination.
    Selected,       ///< Select confirmed positively (separate select, no automatic execute).
    Rejected,       ///< Negative confirmation or termination, or cause 44..47.
    NoConfirmation, ///< No confirmation within the confirmation time.
    NoTermination,  ///< Confirmed, but no termination within the termination time.
    ConnectionLost, ///< The connection closed before the result was known.
};

/** @brief Time limits and expectations of the controlling station. */
struct CommandTimeouts {
    std::uint32_t confirmation_ms{10000}; ///< Activation -> confirmation (10 s).
    std::uint32_t termination_ms{10000};  ///< Confirmation -> termination.
    bool expect_termination{true};        ///< false: a positive confirmation is final.
};

/** @brief A command on its way. */
struct PendingCommand {
    std::uint16_t common_address{0};
    std::uint8_t type{0};
    codec::InformationObject object{};
    bool then_execute{false}; ///< Select of select_execute(): execute after confirmation.
    bool confirmed{false};    ///< Positive confirmation received, waiting for termination.
    std::uint64_t deadline_ms{0};
};

/** @brief One finished command. */
struct CommandOutcome {
    std::uint16_t common_address{0};
    std::uint8_t type{0};
    codec::InformationObject object{};
    CommandResult result{CommandResult::Rejected};
};

/** @brief Follows the commands of one connection (see file comment). */
class CommandTracker {
  public:
    /** @brief Starts following a command that has just been sent. */
    void add(std::uint16_t common_address, std::uint8_t type,
             const codec::InformationObject& object, bool then_execute, std::uint64_t now_ms,
             const CommandTimeouts& timeouts);

    /**
     * @brief Evaluates a received ASDU.
     *
     * @param outcomes   Receives finished commands.
     * @param to_execute Receives executes to send now (automatic select-execute);
     *                   the caller sends them and add()s them.
     */
    void on_asdu(const codec::AsduHeader& header, const codec::InformationObject* objects,
                 std::size_t count, std::uint64_t now_ms, const CommandTimeouts& timeouts,
                 std::vector<CommandOutcome>& outcomes, std::vector<PendingCommand>& to_execute);

    /** @brief Reports commands whose time limit has passed. */
    void tick(std::uint64_t now_ms, std::vector<CommandOutcome>& outcomes);

    /** @brief The connection is gone: every open command ends with ConnectionLost. */
    void connection_lost(std::vector<CommandOutcome>& outcomes);

    /** @brief Number of open commands. */
    [[nodiscard]] std::size_t open() const noexcept { return pending_.size(); }

  private:
    void finish(std::size_t index, CommandResult result, std::vector<CommandOutcome>& outcomes);

    std::vector<PendingCommand> pending_;
};

} // namespace iec104::session
