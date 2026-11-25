/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_api.cpp
 * @brief Unit tests for the general C API functions (include/iec104/iec104.h).
 *
 * These tests use only the public C header, like a library user would.
 */
#include "iec104/iec104.h"

#include <cstring>
#include <gtest/gtest.h>

// Rule: the version string reported by the built library matches the project
// version. Source: CMakeLists.txt (project VERSION 0.1.0) and the
// IEC104_VERSION_* macros in iec104.h.
TEST(Api, VersionStringMatchesMacros) {
    const char* v = iec104_version_string();
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v, "0.1.0");
}

// Rule: iec104_status_string() never returns NULL, also for values that are
// not a declared status code (C callers can pass any int).
// Source: iec104.h (API contract).
TEST(Api, StatusStringNeverNull) {
    EXPECT_STREQ(iec104_status_string(IEC104_OK), "ok");
    EXPECT_STREQ(iec104_status_string(IEC104_ERR_MALFORMED), "malformed frame");
    EXPECT_STREQ(iec104_status_string(IEC104_ERR_WOULD_BLOCK), "would block");
    EXPECT_STREQ(iec104_status_string(IEC104_ERR_WRONG_STATE), "not allowed in this state");
    EXPECT_STREQ(iec104_status_string(static_cast<iec104_status>(-50)), "unknown status");
}
