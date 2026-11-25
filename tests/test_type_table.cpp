/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_type_table.cpp
 * @brief Consistency of the type table (src/codec/type_table.hpp).
 *
 * The table content itself is checked against a foreign implementation in
 * test_asdu_reference.cpp. These tests check the scope of version 1
 * and the internal consistency of the table.
 */
#include "codec/type_table.hpp"

#include <gtest/gtest.h>
#include <set>

using namespace iec104::codec;

// Rule: every IEC 104 type of the scope of version 1 and every CP24 type is in the table.
// Source: scope in type_table.hpp.
TEST(TypeTable, ScopeOfTask001) {
    std::set<int> required{1, 3, 5, 7, 9, 11, 13, 15, 20, 21, 45, 46, 47, 48, 49, 50, 51, 70};
    for (int id = 30; id <= 40; ++id) {
        required.insert(id);
    }
    for (int id = 58; id <= 64; ++id) {
        required.insert(id);
    }
    for (int id = 100; id <= 107; ++id) {
        required.insert(id);
    }
    for (int id = 110; id <= 113; ++id) {
        required.insert(id);
    }
    const std::set<int> cp24{2, 4, 6, 8, 10, 12, 14, 16, 17, 18, 19};

    for (const int id : required) {
        const TypeInfo* t = find_type(static_cast<std::uint8_t>(id));
        ASSERT_NE(t, nullptr) << "type " << id;
        EXPECT_NE(t->time_tag, TimeTag::Cp24) << "type " << id;
    }
    for (const int id : cp24) {
        const TypeInfo* t = find_type(static_cast<std::uint8_t>(id));
        ASSERT_NE(t, nullptr) << "type " << id;
        EXPECT_EQ(t->time_tag, TimeTag::Cp24) << "type " << id;
    }
    std::size_t count = 0;
    (void)all_types(count);
    EXPECT_EQ(count, required.size() + cp24.size());
}

// Rule: entries sorted and unique; element count within bounds; a time tag,
// if any, is the last element; the name prefix matches the direction.
// Source: structure of type_table.hpp.
TEST(TypeTable, Consistency) {
    std::size_t count = 0;
    const TypeInfo* types = all_types(count);
    for (std::size_t i = 0; i < count; ++i) {
        const TypeInfo& t = types[i];
        SCOPED_TRACE(t.name);
        if (i > 0U) {
            EXPECT_LT(types[i - 1].id, t.id);
        }
        EXPECT_LE(t.element_count, kMaxElements);
        EXPECT_EQ(find_type(t.id), &t);
        if (t.time_tag == TimeTag::Cp56) {
            ASSERT_GE(t.element_count, 1U);
            EXPECT_EQ(t.elements[t.element_count - 1U], ElementKind::Cp56);
        }
        if (t.time_tag == TimeTag::Cp24) {
            ASSERT_GE(t.element_count, 1U);
            EXPECT_EQ(t.elements[t.element_count - 1U], ElementKind::Cp24);
        }
        // M_ = monitor direction, C_ and P_ = control direction.
        EXPECT_EQ(t.direction, t.name[0] == 'M' ? Direction::Monitor : Direction::Control);
    }
}

// Rule: object lengths for some types, derived from the element lengths.
// Source: docs/02_Technik_und_Quellen.md element lengths; e.g. M_ME_TF_1 = R32 4 + QDS 1 + CP56 7.
TEST(TypeTable, ObjectLength) {
    EXPECT_EQ(object_length(*find_type(1)), 1U);
    EXPECT_EQ(object_length(*find_type(36)), 12U);
    EXPECT_EQ(object_length(*find_type(39)), 11U);
    EXPECT_EQ(object_length(*find_type(102)), 0U);
    EXPECT_EQ(object_length(*find_type(107)), 9U);
}

// Rule: 136..255 is the private range; unassigned ids are not in the table.
// Source: docs/02_Technik_und_Quellen.md ("Typkennungen").
TEST(TypeTable, PrivateAndUnknown) {
    EXPECT_FALSE(is_private_type(135));
    EXPECT_TRUE(is_private_type(136));
    EXPECT_TRUE(is_private_type(255));
    EXPECT_EQ(find_type(0), nullptr);
    EXPECT_EQ(find_type(22), nullptr);
    EXPECT_EQ(find_type(120), nullptr); // file transfer: out of scope
    EXPECT_EQ(find_type(136), nullptr);
}

// Rule: the cause matrix (clause 9.5) is not filled in yet; every combination
// is allowed until it is (TODO(norm): 9.5).
// Source: type_table.hpp (N3 in docs/05_Normfragen_und_Festlegungen.md).
TEST(TypeTable, CauseMatrixIsOpen) {
    EXPECT_TRUE(is_cause_allowed(1, 3));
    EXPECT_TRUE(is_cause_allowed(100, 6));
}
