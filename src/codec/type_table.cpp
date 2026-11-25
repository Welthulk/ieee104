/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file type_table.cpp
 * @brief The table of type identifications and its lookups.
 *
 * One line per type. The element lists follow docs/02_Technik_und_Quellen.md and are checked
 * against tests/data/lib60870_asdu_reference.txt by the unit tests (every
 * reference ASDU must decode with exactly the length this table predicts).
 *
 * Layer: codec (pure data and lookups, no I/O, no allocation).
 */
#include "codec/type_table.hpp"

namespace iec104::codec {
namespace {

using E = ElementKind;
constexpr Direction kM = Direction::Monitor;
constexpr Direction kC = Direction::Control;

// Sorted by type id. Columns: id, name, direction, time tag, element count, elements.
constexpr std::array<TypeInfo, 59> kTypes{{
    // Process information in monitor direction, without time tag (IEC 104).
    {1, "M_SP_NA_1", kM, TimeTag::None, 1, {E::Siq}},
    // IEC 101 types with CP24Time2a (even ids 2..16, and 17..19) are listed
    // next to their IEC 104 counterparts; strict mode rejects them.
    {2, "M_SP_TA_1", kM, TimeTag::Cp24, 2, {E::Siq, E::Cp24}},
    {3, "M_DP_NA_1", kM, TimeTag::None, 1, {E::Diq}},
    {4, "M_DP_TA_1", kM, TimeTag::Cp24, 2, {E::Diq, E::Cp24}},
    {5, "M_ST_NA_1", kM, TimeTag::None, 2, {E::Vti, E::Qds}},
    {6, "M_ST_TA_1", kM, TimeTag::Cp24, 3, {E::Vti, E::Qds, E::Cp24}},
    {7, "M_BO_NA_1", kM, TimeTag::None, 2, {E::Bsi, E::Qds}},
    {8, "M_BO_TA_1", kM, TimeTag::Cp24, 3, {E::Bsi, E::Qds, E::Cp24}},
    {9, "M_ME_NA_1", kM, TimeTag::None, 2, {E::Nva, E::Qds}},
    {10, "M_ME_TA_1", kM, TimeTag::Cp24, 3, {E::Nva, E::Qds, E::Cp24}},
    {11, "M_ME_NB_1", kM, TimeTag::None, 2, {E::Sva, E::Qds}},
    {12, "M_ME_TB_1", kM, TimeTag::Cp24, 3, {E::Sva, E::Qds, E::Cp24}},
    {13, "M_ME_NC_1", kM, TimeTag::None, 2, {E::R32, E::Qds}},
    {14, "M_ME_TC_1", kM, TimeTag::Cp24, 3, {E::R32, E::Qds, E::Cp24}},
    {15, "M_IT_NA_1", kM, TimeTag::None, 1, {E::Bcr}},
    {16, "M_IT_TA_1", kM, TimeTag::Cp24, 2, {E::Bcr, E::Cp24}},
    {17, "M_EP_TA_1", kM, TimeTag::Cp24, 3, {E::Sep, E::Cp16, E::Cp24}},
    {18, "M_EP_TB_1", kM, TimeTag::Cp24, 4, {E::Spe, E::Qdp, E::Cp16, E::Cp24}},
    {19, "M_EP_TC_1", kM, TimeTag::Cp24, 4, {E::Oci, E::Qdp, E::Cp16, E::Cp24}},
    {20, "M_PS_NA_1", kM, TimeTag::None, 2, {E::Scd, E::Qds}},
    {21, "M_ME_ND_1", kM, TimeTag::None, 1, {E::Nva}},

    // Process information in monitor direction with CP56Time2a.
    {30, "M_SP_TB_1", kM, TimeTag::Cp56, 2, {E::Siq, E::Cp56}},
    {31, "M_DP_TB_1", kM, TimeTag::Cp56, 2, {E::Diq, E::Cp56}},
    {32, "M_ST_TB_1", kM, TimeTag::Cp56, 3, {E::Vti, E::Qds, E::Cp56}},
    {33, "M_BO_TB_1", kM, TimeTag::Cp56, 3, {E::Bsi, E::Qds, E::Cp56}},
    {34, "M_ME_TD_1", kM, TimeTag::Cp56, 3, {E::Nva, E::Qds, E::Cp56}},
    {35, "M_ME_TE_1", kM, TimeTag::Cp56, 3, {E::Sva, E::Qds, E::Cp56}},
    {36, "M_ME_TF_1", kM, TimeTag::Cp56, 3, {E::R32, E::Qds, E::Cp56}},
    {37, "M_IT_TB_1", kM, TimeTag::Cp56, 2, {E::Bcr, E::Cp56}},
    {38, "M_EP_TD_1", kM, TimeTag::Cp56, 3, {E::Sep, E::Cp16, E::Cp56}},
    {39, "M_EP_TE_1", kM, TimeTag::Cp56, 4, {E::Spe, E::Qdp, E::Cp16, E::Cp56}},
    {40, "M_EP_TF_1", kM, TimeTag::Cp56, 4, {E::Oci, E::Qdp, E::Cp16, E::Cp56}},

    // Process information in control direction (commands), without time tag.
    {45, "C_SC_NA_1", kC, TimeTag::None, 1, {E::Sco}},
    {46, "C_DC_NA_1", kC, TimeTag::None, 1, {E::Dco}},
    {47, "C_RC_NA_1", kC, TimeTag::None, 1, {E::Rco}},
    {48, "C_SE_NA_1", kC, TimeTag::None, 2, {E::Nva, E::Qos}},
    {49, "C_SE_NB_1", kC, TimeTag::None, 2, {E::Sva, E::Qos}},
    {50, "C_SE_NC_1", kC, TimeTag::None, 2, {E::R32, E::Qos}},
    {51, "C_BO_NA_1", kC, TimeTag::None, 1, {E::Bsi}},

    // Commands with CP56Time2a (IEC 104 only).
    {58, "C_SC_TA_1", kC, TimeTag::Cp56, 2, {E::Sco, E::Cp56}},
    {59, "C_DC_TA_1", kC, TimeTag::Cp56, 2, {E::Dco, E::Cp56}},
    {60, "C_RC_TA_1", kC, TimeTag::Cp56, 2, {E::Rco, E::Cp56}},
    {61, "C_SE_TA_1", kC, TimeTag::Cp56, 3, {E::Nva, E::Qos, E::Cp56}},
    {62, "C_SE_TB_1", kC, TimeTag::Cp56, 3, {E::Sva, E::Qos, E::Cp56}},
    {63, "C_SE_TC_1", kC, TimeTag::Cp56, 3, {E::R32, E::Qos, E::Cp56}},
    {64, "C_BO_TA_1", kC, TimeTag::Cp56, 2, {E::Bsi, E::Cp56}},

    // System information in monitor direction.
    {70, "M_EI_NA_1", kM, TimeTag::None, 1, {E::Coi}},

    // System information in control direction.
    {100, "C_IC_NA_1", kC, TimeTag::None, 1, {E::Qoi}},
    {101, "C_CI_NA_1", kC, TimeTag::None, 1, {E::Qcc}},
    {102, "C_RD_NA_1", kC, TimeTag::None, 0, {}}, // read command: IOA only
    // The CP56Time2a of the clock synchronization is the value to set, not a
    // time tag of the object, hence TimeTag::None.
    {103, "C_CS_NA_1", kC, TimeTag::None, 1, {E::Cp56}},
    // TODO(norm): whether 104 and 106 are used in IEC 104 (docs/05_Normfragen_und_Festlegungen.md,
    // N5).
    {104, "C_TS_NA_1", kC, TimeTag::None, 1, {E::Fbp}},
    {105, "C_RP_NA_1", kC, TimeTag::None, 1, {E::Qrp}},
    {106, "C_CD_NA_1", kC, TimeTag::None, 1, {E::Cp16}},
    {107, "C_TS_TA_1", kC, TimeTag::Cp56, 2, {E::Tsc, E::Cp56}},

    // Parameter in control direction.
    {110, "P_ME_NA_1", kC, TimeTag::None, 2, {E::Nva, E::Qpm}},
    {111, "P_ME_NB_1", kC, TimeTag::None, 2, {E::Sva, E::Qpm}},
    {112, "P_ME_NC_1", kC, TimeTag::None, 2, {E::R32, E::Qpm}},
    {113, "P_AC_NA_1", kC, TimeTag::None, 1, {E::Qpa}},
}};

/**
 * @brief Compile-time check of the table: every entry filled in, ids strictly
 * ascending. A wrong array size leaves zero entries at the end and fails here.
 */
constexpr bool is_well_formed(const std::array<TypeInfo, kTypes.size()>& types) {
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (types[i].name == nullptr || types[i].id == 0U ||
            (i > 0U && types[i - 1U].id >= types[i].id)) {
            return false;
        }
    }
    return true;
}
static_assert(is_well_formed(kTypes), "type table: size or order wrong");

/*
 * Matrix "which cause of transmission is allowed for which type" (standard,
 * clause 9.5). Empty on purpose: a guessed matrix would reject valid traffic. Planned shape: one
 * entry per type id with a 64-bit mask of allowed causes.
 * TODO(norm): 9.5, fill in from the standard.
 */
struct CauseRule {
    std::uint8_t type_id;
    std::uint64_t allowed_causes; // bit n set = cause n allowed
};
constexpr std::array<CauseRule, 0> kCauseMatrix{};

} // namespace

const TypeInfo* find_type(std::uint8_t type_id) noexcept {
    // A linear search over 59 entries is fast enough and easier to read than
    // an index table.
    for (const TypeInfo& t : kTypes) {
        if (t.id == type_id) {
            return &t;
        }
    }
    return nullptr;
}

const TypeInfo* all_types(std::size_t& count) noexcept {
    count = kTypes.size();
    return kTypes.data();
}

std::size_t object_length(const TypeInfo& type) noexcept {
    std::size_t length = 0;
    for (std::size_t i = 0; i < type.element_count; ++i) {
        length += element_length(type.elements[i]);
    }
    return length;
}

bool is_cause_allowed(std::uint8_t type_id, std::uint8_t cause) noexcept {
    for (const CauseRule& rule : kCauseMatrix) {
        if (rule.type_id == type_id) {
            return cause < 64U && ((rule.allowed_causes >> cause) & 1U) != 0U;
        }
    }
    return true; // no rule for this type (yet): allowed
}

} // namespace iec104::codec
