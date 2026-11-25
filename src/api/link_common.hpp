/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file link_common.hpp
 * @brief Shared pieces of the server and client C API: conversions and the poll loop.
 *
 * The C API uses its own plain C types (iec104.h); this file converts them to
 * and from the C++ types of the apci and transport layers, and implements
 * the one poll() round that server and client have in common.
 *
 * Layer: API (internal header).
 */
#pragma once

#include "apci/apci_config.hpp"
#include "apci/connection.hpp"
#include "codec/asdu.hpp"
#include "iec104/iec104.h"
#include "transport/link.hpp"

#include <memory>
#include <vector>

namespace iec104::api {

/** @brief C configuration -> APCI configuration for the given role. */
apci::ApciConfig to_apci(const iec104_link_config& c, apci::Role role) noexcept;

/** @brief APCI state -> C link state. */
iec104_link_state to_c(apci::State state) noexcept;

/** @brief Close reasons of transport and APCI layer -> C close reason. */
iec104_close_reason to_c(transport::LinkCloseReason reason, apci::CloseReason protocol) noexcept;

/**
 * @brief Forwards link events to the C callbacks.
 *
 * Server and client derive from it; the link ids are passed through.
 */
class CallbackSink : public transport::LinkSink {
  public:
    explicit CallbackSink(const iec104_callbacks* callbacks) noexcept;
    void on_asdu(std::uint32_t id, const std::uint8_t* asdu, std::size_t length) override;
    void on_state(std::uint32_t id, apci::State state) override;
    void on_closed(std::uint32_t id, transport::LinkCloseReason reason,
                   apci::CloseReason protocol_reason) override;

  protected:
    /**
     * @brief Decodes an ASDU and reports every object of a value type via on_object.
     *
     * ASDUs that cannot be decoded with @p profile, private types and types
     * outside the value API are only reported raw (on_asdu).
     */
    void deliver_objects(std::uint32_t id, const std::uint8_t* asdu, std::size_t length,
                         const codec::AsduProfile& profile) const;

    /** @brief The callbacks given at creation. */
    [[nodiscard]] const iec104_callbacks& callbacks() const noexcept { return callbacks_; }

  private:
    iec104_callbacks callbacks_{};
};

/**
 * @brief One round of the event loop for a set of links and an optional listener.
 *
 * Waits at most @p timeout_ms (less if a timer is due), then services every
 * link. Returns the socket events of the listener in @p listener_revents
 * (0 if there is none), so that the server can accept afterwards. Links
 * whose close has been reported are removed from @p links.
 *
 * @return IEC104_OK, or IEC104_ERR_IO if poll() fails.
 */
iec104_status poll_links(std::vector<std::unique_ptr<transport::Link>>& links,
                         const transport::Socket* listener, int timeout_ms,
                         transport::LinkSink& sink, short& listener_revents) noexcept;

} // namespace iec104::api
