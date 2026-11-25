/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_interop_list.cpp
 * @brief The data of the interoperability list matches the library.
 *
 * tools/interop_list/interop_data.hpp answers the checklist of clause 9 by
 * hand. These tests tie its type rows to the codec's type table and to the
 * value API, so that a change of the library shows up here.
 */
#include "api/values.hpp"
#include "codec/type_table.hpp"
#include "interop_list/interop_data.hpp"

#include <gtest/gtest.h>
#include <string>

using interop::Group;
using interop::Mark;

// Rule: every type of the codec has exactly one row with the same mnemonic;
// rows outside the codec are file transfer only; ids ascend.
TEST(InteropList, MatchesTypeTable) {
    std::size_t count = 0;
    const iec104::codec::TypeInfo* all = iec104::codec::all_types(count);
    for (std::size_t i = 0; i < count; ++i) {
        SCOPED_TRACE(all[i].name);
        int rows = 0;
        for (const interop::TypeRow& r : interop::kTypes) {
            if (r.id == all[i].id) {
                ++rows;
                EXPECT_EQ(std::string(r.name), all[i].name);
            }
        }
        EXPECT_EQ(rows, 1);
    }
    int previous = 0;
    for (const interop::TypeRow& r : interop::kTypes) {
        SCOPED_TRACE(r.name);
        EXPECT_GT(r.id, previous);
        previous = r.id;
        if (iec104::codec::find_type(r.id) == nullptr) {
            EXPECT_EQ(r.group, Group::File);
            EXPECT_EQ(r.station, Mark::No);
        }
    }
}

// Rule: "X" means the value API (iec104_object) handles the type, "X roh"
// means it does not; types marked "–" are not in IEC 104 (CP24, 104, 106).
TEST(InteropList, MarksMatchValueApi) {
    for (const interop::TypeRow& r : interop::kTypes) {
        SCOPED_TRACE(r.name);
        const bool value_api = iec104::api::value_type_supported(r.id);
        if (r.master == Mark::Yes) {
            EXPECT_TRUE(value_api);
        }
        if (r.master == Mark::Raw) {
            EXPECT_FALSE(value_api);
        }
    }
}
