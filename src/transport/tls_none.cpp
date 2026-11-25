/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file tls_none.cpp
 * @brief TLS backend of a build without TLS (CMake option IEC104_TLS=OFF).
 *
 * Every attempt to set up TLS reports IEC104_ERR_UNSUPPORTED; plain TCP is
 * not affected.
 *
 * Layer: transport.
 */
#include "transport/tls.hpp"

namespace iec104::transport {

iec104_status create_tls_context(TlsRole /*role*/, const TlsSettings& /*settings*/,
                                 std::shared_ptr<TlsContext>& /*out*/, std::string& error) {
    error = "library built without TLS";
    return IEC104_ERR_UNSUPPORTED;
}

bool tls_available() noexcept {
    return false;
}

} // namespace iec104::transport
