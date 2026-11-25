/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file command_tracker.cpp
 * @brief Implementation of the command tracker of the controlling station.
 *
 * Layer: session (no I/O).
 */
#include "session/command_tracker.hpp"

#include "session/command_table.hpp"

namespace iec104::session {
namespace {

// Causes of transmission of the answers (docs/02_Technik_und_Quellen.md,
// "Übertragungsursachen").
constexpr std::uint8_t kCotActivationCon = 7;
constexpr std::uint8_t kCotActivationTerm = 10;
constexpr std::uint8_t kCotFirstUnknown = 44; // 44..47: unknown type, cause, station, IOA
constexpr std::uint8_t kCotLastUnknown = 47;

/** @brief The same command with S/E = execute. */
codec::InformationObject as_execute(std::uint8_t type, codec::InformationObject o) noexcept {
    switch (type) {
    case 45:
    case 58:
        o.sco.select = false;
        break;
    case 46:
    case 59:
        o.dco.select = false;
        break;
    case 47:
    case 60:
        o.rco.select = false;
        break;
    default:
        o.qos.select = false;
        break;
    }
    return o;
}

} // namespace

void CommandTracker::add(std::uint16_t common_address, std::uint8_t type,
                         const codec::InformationObject& object, bool then_execute,
                         std::uint64_t now_ms, const CommandTimeouts& timeouts) {
    PendingCommand p{};
    p.common_address = common_address;
    p.type = type;
    p.object = object;
    p.then_execute = then_execute;
    p.deadline_ms = now_ms + timeouts.confirmation_ms;
    pending_.push_back(p);
}

void CommandTracker::on_asdu(const codec::AsduHeader& header,
                             const codec::InformationObject* objects, std::size_t count,
                             std::uint64_t now_ms, const CommandTimeouts& timeouts,
                             std::vector<CommandOutcome>& outcomes,
                             std::vector<PendingCommand>& to_execute) {
    const std::uint8_t cause = header.cause;
    const bool unknown = cause >= kCotFirstUnknown && cause <= kCotLastUnknown;
    if (cause != kCotActivationCon && cause != kCotActivationTerm && !unknown) {
        return; // not an answer to a command
    }
    for (std::size_t n = 0; n < count; ++n) {
        // The oldest open command with the same address, type and IOA is the
        // one being answered (answers come in the order of the requests).
        std::size_t i = 0;
        while (i < pending_.size() &&
               (pending_[i].common_address != header.common_address ||
                pending_[i].type != header.type_id || pending_[i].object.ioa != objects[n].ioa)) {
            ++i;
        }
        if (i == pending_.size()) {
            continue; // an answer to something this side did not send (or already finished)
        }
        PendingCommand& p = pending_[i];
        if (header.negative || unknown) {
            finish(i, CommandResult::Rejected, outcomes);
            continue;
        }
        if (cause == kCotActivationTerm) {
            finish(i, CommandResult::Terminated, outcomes);
            continue;
        }
        // Positive confirmation.
        if (is_select(p.type, p.object)) {
            if (p.then_execute) {
                PendingCommand exec = p;
                exec.object = as_execute(p.type, p.object);
                exec.then_execute = false;
                pending_.erase(pending_.begin() + static_cast<long>(i));
                to_execute.push_back(exec); // the caller sends it and add()s it
            } else {
                finish(i, CommandResult::Selected, outcomes);
            }
            continue;
        }
        // Only executed commands (45..64) are terminated; other requests
        // (e.g. clock synchronization) end with their confirmation.
        if (timeouts.expect_termination && is_command_type(p.type)) {
            p.confirmed = true;
            p.deadline_ms = now_ms + timeouts.termination_ms;
        } else {
            finish(i, CommandResult::Confirmed, outcomes);
        }
    }
}

void CommandTracker::tick(std::uint64_t now_ms, std::vector<CommandOutcome>& outcomes) {
    std::size_t i = 0;
    while (i < pending_.size()) {
        if (now_ms >= pending_[i].deadline_ms) {
            finish(i,
                   pending_[i].confirmed ? CommandResult::NoTermination
                                         : CommandResult::NoConfirmation,
                   outcomes);
        } else {
            ++i;
        }
    }
}

void CommandTracker::connection_lost(std::vector<CommandOutcome>& outcomes) {
    while (!pending_.empty()) {
        finish(0, CommandResult::ConnectionLost, outcomes);
    }
}

void CommandTracker::finish(std::size_t index, CommandResult result,
                            std::vector<CommandOutcome>& outcomes) {
    const PendingCommand& p = pending_[index];
    outcomes.push_back(CommandOutcome{p.common_address, p.type, p.object, result});
    pending_.erase(pending_.begin() + static_cast<long>(index));
}

} // namespace iec104::session
