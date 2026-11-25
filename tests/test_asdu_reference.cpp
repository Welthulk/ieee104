/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file test_asdu_reference.cpp
 * @brief ASDU codec against reference ASDUs of a foreign implementation.
 *
 * Source of all bytes: tests/data/lib60870_asdu_reference.txt, encoded by
 * lib60870-C v2.4.1 (external test peer only, not linked) and cross-checked
 * with Wireshark 3.4.10; see the head of that file. The comment above each
 * line there lists the values lib60870 was given; the field checks below
 * repeat those values.
 *
 * Two kinds of tests:
 *   - EveryReferenceAsduRoundTrips: every line decodes, has the expected type
 *     name, and re-encodes to the identical octets;
 *   - one test per type group that checks decoded field values.
 */
#include "codec/asdu.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

using namespace iec104::codec;

namespace {

struct ReferenceAsdu {
    int line = 0;          // line number in the reference file
    std::string name;      // type name, e.g. "M_SP_NA_1"
    std::string profile;   // "iec104" or "cot1_ca1_ioa2"
    bool sequence = false; // SQ1 in the file
    std::vector<std::uint8_t> bytes;
};

// Reads all "ASDU <name> <profile> SQ<n>: <hex>" lines.
std::vector<ReferenceAsdu> load_reference() {
    std::vector<ReferenceAsdu> result;
    std::ifstream in(IEC104_TEST_DATA_DIR "/lib60870_asdu_reference.txt");
    std::string text;
    int line = 0;
    while (std::getline(in, text)) {
        ++line;
        if (text.rfind("ASDU ", 0) != 0) {
            continue;
        }
        ReferenceAsdu r;
        r.line = line;
        const std::size_t colon = text.find(':');
        std::istringstream head(text.substr(5, colon - 5));
        std::string sq;
        head >> r.name >> r.profile >> sq;
        r.sequence = (sq == "SQ1");
        std::istringstream hex(text.substr(colon + 1));
        unsigned value = 0;
        while (hex >> std::hex >> value) {
            r.bytes.push_back(static_cast<std::uint8_t>(value));
        }
        result.push_back(r);
    }
    return result;
}

// Profile named in the reference file. Tolerant, so that the CP24 types decode.
AsduProfile profile_for(const std::string& name) {
    AsduProfile p{};
    if (name == "cot1_ca1_ioa2") {
        p = AsduProfile{1, 1, 2};
    }
    p.strict = false;
    return p;
}

// Finds the reference ASDU for a type name, profile and SQ value.
ReferenceAsdu find(const std::string& name, bool sequence = false,
                   const std::string& profile = "iec104") {
    for (const ReferenceAsdu& r : load_reference()) {
        if (r.name == name && r.sequence == sequence && r.profile == profile) {
            return r;
        }
    }
    ADD_FAILURE() << "reference ASDU missing: " << name;
    return {};
}

// Decodes a reference ASDU; fails the test on error.
struct Decoded {
    DecodedAsdu asdu{};
    std::array<InformationObject, 127> objects{};
};
Decoded decode(const ReferenceAsdu& r) {
    Decoded d;
    EXPECT_EQ(decode_asdu(r.bytes.data(), r.bytes.size(), profile_for(r.profile), d.objects.data(),
                          d.objects.size(), d.asdu),
              IEC104_OK)
        << r.name;
    return d;
}

// 2026-11-25 11:01:05.931, the time every reference ASDU with CP56Time2a carries.
void expect_reference_time(const Cp56Time2a& t) {
    EXPECT_EQ(t.milliseconds, 5931U);
    EXPECT_EQ(t.minute, 1U);
    EXPECT_EQ(t.hour, 11U);
    EXPECT_EQ(t.day_of_month, 25U);
    EXPECT_EQ(t.month, 11U);
    EXPECT_EQ(t.year, 26U);
    EXPECT_FALSE(t.invalid);
    EXPECT_FALSE(t.summer_time);
}

} // namespace

// Rule: every reference ASDU decodes with the length the type table predicts,
// carries the type named in the file, and re-encodes to identical octets.
// Source: reference file (whole file, 72 ASDUs).
TEST(AsduReference, EveryReferenceAsduRoundTrips) {
    const std::vector<ReferenceAsdu> refs = load_reference();
    ASSERT_GE(refs.size(), 72U) << "reference file missing or truncated";

    for (const ReferenceAsdu& r : refs) {
        SCOPED_TRACE("reference line " + std::to_string(r.line) + " " + r.name);
        const AsduProfile profile = profile_for(r.profile);
        Decoded d = decode(r);
        ASSERT_NE(d.asdu.type, nullptr);
        EXPECT_STREQ(d.asdu.type->name, r.name.c_str());
        EXPECT_EQ(d.asdu.header.sequence, r.sequence);
        EXPECT_EQ(d.asdu.object_count, d.asdu.header.num_objects);

        std::array<std::uint8_t, 256> out{};
        std::size_t written = 0;
        ASSERT_EQ(encode_asdu(d.asdu.header, d.objects.data(), d.asdu.object_count, profile,
                              out.data(), out.size(), written),
                  IEC104_OK);
        ASSERT_EQ(written, r.bytes.size());
        EXPECT_TRUE(std::equal(r.bytes.begin(), r.bytes.end(), out.begin()));
    }
}

// Rule: in the strict profile the IEC 101 CP24Time2a types are rejected,
// every other reference ASDU is accepted (docs/04_Analyse.md, N4).
// Source: reference file; CP24 types are the lines with type ids 2..19 (even) and 17..19.
TEST(AsduReference, StrictProfileRejectsOnlyCp24Types) {
    for (const ReferenceAsdu& r : load_reference()) {
        SCOPED_TRACE(r.name);
        AsduProfile strict = profile_for(r.profile);
        strict.strict = true;
        std::array<InformationObject, 127> objects{};
        DecodedAsdu out{};
        const iec104_status st = decode_asdu(r.bytes.data(), r.bytes.size(), strict, objects.data(),
                                             objects.size(), out);
        const TypeInfo* type = find_type(r.bytes[0]);
        ASSERT_NE(type, nullptr);
        EXPECT_EQ(st, type->time_tag == TimeTag::Cp24 ? IEC104_ERR_UNSUPPORTED : IEC104_OK);
    }
}

// Rule: monitor direction without time tag, SQ = 0 and SQ = 1 (SQ = 1
// addresses count up from the one transmitted IOA).
// Source: reference lines M_SP_NA_1, M_DP_NA_1, M_ST_NA_1, M_ME_NA_1,
// M_ME_NB_1, M_ME_NC_1, M_IT_NA_1, M_BO_NA_1, M_PS_NA_1 (Wireshark: field by
// field, except M_PS_NA_1 raw).
TEST(AsduReference, MonitorWithoutTime) {
    Decoded sp = decode(find("M_SP_NA_1", true));
    EXPECT_EQ(sp.asdu.header.cause, 20U); // interrogated by station interrogation
    EXPECT_EQ(sp.asdu.header.originator, 3U);
    EXPECT_EQ(sp.asdu.header.common_address, 1U);
    ASSERT_EQ(sp.asdu.object_count, 3U);
    EXPECT_EQ(sp.objects[0].ioa, 200U);
    EXPECT_EQ(sp.objects[1].ioa, 201U);
    EXPECT_EQ(sp.objects[2].ioa, 202U);
    EXPECT_TRUE(sp.objects[0].siq.on);
    EXPECT_EQ(sp.objects[1].siq.quality, kQualityNotTopical);
    EXPECT_TRUE(sp.objects[2].siq.on);
    EXPECT_EQ(sp.objects[2].siq.quality, kQualitySubstituted);

    Decoded sp0 = decode(find("M_SP_NA_1"));
    EXPECT_FALSE(sp0.objects[1].siq.on);
    EXPECT_EQ(sp0.objects[1].siq.quality, kQualityInvalid | kQualityBlocked);

    Decoded dp = decode(find("M_DP_NA_1"));
    EXPECT_EQ(dp.objects[0].diq.state, DoublePoint::On);
    EXPECT_EQ(dp.objects[1].diq.state, DoublePoint::Indeterminate);
    EXPECT_EQ(dp.objects[1].diq.quality, kQualityInvalid);

    Decoded st = decode(find("M_ST_NA_1"));
    EXPECT_EQ(st.objects[0].vti.value, 5);
    EXPECT_EQ(st.objects[1].vti.value, -64);
    EXPECT_TRUE(st.objects[1].vti.transient);
    EXPECT_EQ(st.objects[1].qds, kQualityOverflow);

    Decoded nva = decode(find("M_ME_NA_1"));
    EXPECT_EQ(nva.objects[0].nva, 0x4000); // 0.5
    EXPECT_EQ(nva.objects[1].nva, -32768); // -1.0

    Decoded sva = decode(find("M_ME_NB_1"));
    EXPECT_EQ(sva.objects[0].sva, 4711);
    EXPECT_EQ(sva.objects[1].sva, -32768);
    EXPECT_EQ(sva.objects[1].qds, kQualityOverflow);

    Decoded r32 = decode(find("M_ME_NC_1", true));
    EXPECT_FLOAT_EQ(r32.objects[1].r32, 50.0F);
    EXPECT_FLOAT_EQ(r32.objects[2].r32, 1.0e6F);
    EXPECT_EQ(r32.objects[2].qds, kQualityNotTopical);

    Decoded it = decode(find("M_IT_NA_1"));
    EXPECT_EQ(it.asdu.header.cause, 37U); // requested by general counter request
    EXPECT_EQ(it.objects[0].bcr.counter, 123456);
    EXPECT_EQ(it.objects[0].bcr.sequence, 7U);
    EXPECT_TRUE(it.objects[0].bcr.carry);
    EXPECT_EQ(it.objects[1].bcr.counter, -2);
    EXPECT_EQ(it.objects[1].bcr.sequence, 31U);
    EXPECT_TRUE(it.objects[1].bcr.adjusted);
    EXPECT_TRUE(it.objects[1].bcr.invalid);

    Decoded bo = decode(find("M_BO_NA_1", true));
    EXPECT_EQ(bo.objects[2].bsi, 0xDEADBEEFU);

    Decoded ps = decode(find("M_PS_NA_1"));
    EXPECT_EQ(ps.objects[0].scd.status, 0x8001U); // octets 01 80
    EXPECT_EQ(ps.objects[0].scd.change, 0x0003U); // octets 03 00
    EXPECT_EQ(ps.objects[1].qds, kQualityInvalid);
}

// Rule: monitor direction with CP56Time2a; the time tag follows the elements.
// Source: reference lines M_SP_TB_1 .. M_IT_TB_1 (Wireshark: field by field).
TEST(AsduReference, MonitorWithCp56Time2a) {
    Decoded sp = decode(find("M_SP_TB_1"));
    EXPECT_TRUE(sp.objects[0].siq.on);
    expect_reference_time(sp.objects[0].cp56);
    expect_reference_time(sp.objects[1].cp56);

    Decoded st = decode(find("M_ST_TB_1"));
    EXPECT_EQ(st.objects[0].vti.value, -3);
    EXPECT_TRUE(st.objects[0].vti.transient);
    EXPECT_EQ(st.objects[1].vti.value, 17);

    Decoded sva = decode(find("M_ME_TE_1"));
    EXPECT_EQ(sva.objects[1].sva, -1000);
    EXPECT_EQ(sva.objects[1].qds, kQualityNotTopical);
    expect_reference_time(sva.objects[1].cp56);

    Decoded r32 = decode(find("M_ME_TF_1"));
    EXPECT_FLOAT_EQ(r32.objects[0].r32, 49.95F);

    Decoded it = decode(find("M_IT_TB_1"));
    EXPECT_EQ(it.objects[0].bcr.counter, 99999);
    EXPECT_TRUE(it.objects[1].bcr.carry && it.objects[1].bcr.adjusted && it.objects[1].bcr.invalid);
}

// Rule: protection equipment events (SEP, SPE, OCI, QDP, CP16Time2a).
// Source: reference lines M_EP_TD_1, M_EP_TE_1, M_EP_TF_1 (Wireshark shows
// these only as raw data; values are those given to lib60870).
TEST(AsduReference, ProtectionEquipment) {
    Decoded td = decode(find("M_EP_TD_1"));
    EXPECT_EQ(td.objects[0].sep.state, EventState::On);
    EXPECT_EQ(td.objects[0].cp16, 1500U);
    EXPECT_EQ(td.objects[1].sep.state, EventState::Off);
    EXPECT_EQ(td.objects[1].sep.quality, kQualityElapsedTimeInvalid | kQualityInvalid);
    expect_reference_time(td.objects[1].cp56);

    Decoded te = decode(find("M_EP_TE_1"));
    EXPECT_EQ(te.objects[0].spe, kSpeGeneralStart | kSpePhaseL2);
    EXPECT_EQ(te.objects[1].spe, kSpeReverseDirection);
    EXPECT_EQ(te.objects[1].qdp, kQualityElapsedTimeInvalid);

    Decoded tf = decode(find("M_EP_TF_1"));
    EXPECT_EQ(tf.objects[0].oci, kOciGeneral | kOciPhaseL3);
    EXPECT_EQ(tf.objects[1].oci, kOciPhaseL1);
    EXPECT_EQ(tf.objects[1].qdp, kQualityBlocked);
}

// Rule: IEC 101 types with CP24Time2a decode in the tolerant profile.
// Source: reference lines M_SP_TA_1 (Wireshark: field by field) and
// M_EP_TA_1 (lib60870 only, unknown to Wireshark 3.4.10).
TEST(AsduReference, Cp24TypesTolerant) {
    Decoded sp = decode(find("M_SP_TA_1"));
    EXPECT_TRUE(sp.objects[0].siq.on);
    EXPECT_EQ(sp.objects[0].cp24.milliseconds, 5931U);
    EXPECT_EQ(sp.objects[0].cp24.minute, 1U);
    EXPECT_FALSE(sp.objects[0].cp24.invalid);

    Decoded ep = decode(find("M_EP_TA_1"));
    EXPECT_EQ(ep.objects[0].sep.state, EventState::On);
    EXPECT_EQ(ep.objects[0].cp16, 1500U);
    EXPECT_EQ(ep.objects[0].cp24.milliseconds, 5931U);
}

// Rule: commands without and with time tag (SCO, DCO, RCO, QOS, set points).
// Source: reference lines C_SC_NA_1 .. C_BO_TA_1 (Wireshark: field by field).
TEST(AsduReference, Commands) {
    Decoded sc = decode(find("C_SC_NA_1"));
    EXPECT_EQ(sc.asdu.header.cause, 6U); // activation
    EXPECT_EQ(sc.objects[0].ioa, 5000U);
    EXPECT_TRUE(sc.objects[0].sco.on);
    EXPECT_TRUE(sc.objects[0].sco.select);
    EXPECT_EQ(sc.objects[0].sco.qu, 0U);

    Decoded dc = decode(find("C_DC_NA_1"));
    EXPECT_EQ(dc.objects[0].dco.state, 1U); // off
    EXPECT_EQ(dc.objects[0].dco.qu, 2U);    // long pulse
    EXPECT_FALSE(dc.objects[0].dco.select);

    Decoded rc = decode(find("C_RC_TA_1"));
    EXPECT_EQ(rc.objects[0].rco.state, 1U); // lower
    EXPECT_EQ(rc.objects[0].rco.qu, 3U);    // persistent output
    expect_reference_time(rc.objects[0].cp56);

    Decoded se = decode(find("C_SE_NA_1"));
    EXPECT_EQ(se.objects[0].nva, 0x4000);
    EXPECT_TRUE(se.objects[0].qos.select);

    Decoded sb = decode(find("C_SE_NB_1"));
    EXPECT_EQ(sb.objects[0].sva, -1234);

    Decoded sct = decode(find("C_SE_TC_1"));
    EXPECT_FLOAT_EQ(sct.objects[0].r32, -3.5F);

    Decoded bo = decode(find("C_BO_NA_1"));
    EXPECT_EQ(bo.objects[0].bsi, 0xCAFE0001U);
}

// Rule: system information (end of initialization, interrogation, counter
// interrogation, read, clock sync, test commands, reset, delay).
// Source: reference lines M_EI_NA_1 .. C_TS_TA_1. Wireshark: field by field
// for 70, 100, 102, 103, 105; raw for 101, 107; unknown for 104, 106.
TEST(AsduReference, SystemInformation) {
    Decoded ei = decode(find("M_EI_NA_1"));
    EXPECT_EQ(ei.asdu.header.cause, 4U); // initialized
    EXPECT_EQ(ei.objects[0].coi.cause, 1U);
    EXPECT_TRUE(ei.objects[0].coi.parameters_changed);

    Decoded ic = decode(find("C_IC_NA_1"));
    EXPECT_EQ(ic.objects[0].ioa, 0U);
    EXPECT_EQ(ic.objects[0].qoi, kQoiStation);

    Decoded ci = decode(find("C_CI_NA_1"));
    EXPECT_EQ(ci.objects[0].qcc.rqt, 5U);
    EXPECT_EQ(ci.objects[0].qcc.frz, 2U);

    Decoded rd = decode(find("C_RD_NA_1"));
    EXPECT_EQ(rd.objects[0].ioa, 4711U);
    EXPECT_EQ(rd.asdu.object_data_size, 3U); // IOA only

    Decoded cs = decode(find("C_CS_NA_1"));
    expect_reference_time(cs.objects[0].cp56);

    Decoded ts = decode(find("C_TS_NA_1"));
    EXPECT_EQ(ts.objects[0].fbp, kFixedTestBitPattern);

    Decoded rp = decode(find("C_RP_NA_1"));
    EXPECT_EQ(rp.objects[0].qrp, 1U); // general reset of process

    Decoded cd = decode(find("C_CD_NA_1"));
    EXPECT_EQ(cd.objects[0].cp16, 1500U);

    Decoded tt = decode(find("C_TS_TA_1"));
    EXPECT_EQ(tt.objects[0].tsc, 0x1234U);
    expect_reference_time(tt.objects[0].cp56);
}

// Rule: parameters in control direction (QPM, QPA).
// Source: reference lines P_ME_NA_1 .. P_AC_NA_1 (Wireshark: field by field).
TEST(AsduReference, Parameters) {
    Decoded na = decode(find("P_ME_NA_1"));
    EXPECT_EQ(na.objects[0].nva, 0x2000); // 0.25
    EXPECT_EQ(na.objects[0].qpm.kpa, 1U); // threshold value

    Decoded nb = decode(find("P_ME_NB_1"));
    EXPECT_EQ(nb.objects[0].sva, 250);
    EXPECT_EQ(nb.objects[0].qpm.kpa, 3U);
    EXPECT_TRUE(nb.objects[0].qpm.local_change);
    EXPECT_FALSE(nb.objects[0].qpm.not_in_operation);

    Decoded nc = decode(find("P_ME_NC_1"));
    EXPECT_FLOAT_EQ(nc.objects[0].r32, 0.001F);
    EXPECT_TRUE(nc.objects[0].qpm.not_in_operation);

    Decoded ac = decode(find("P_AC_NA_1"));
    EXPECT_EQ(ac.objects[0].qpa, 3U);
}

// Rule: field lengths other than the IEC 104 profile (COT 1, CA 1, IOA 2).
// Source: reference lines with profile cot1_ca1_ioa2 (lib60870 only;
// Wireshark assumes the IEC 104 lengths).
TEST(AsduReference, OtherProfile) {
    Decoded sva = decode(find("M_ME_NB_1", false, "cot1_ca1_ioa2"));
    EXPECT_EQ(sva.asdu.header.cause, 3U);
    EXPECT_EQ(sva.asdu.header.originator, 0U);
    EXPECT_EQ(sva.asdu.header.common_address, 1U);
    EXPECT_EQ(sva.objects[0].ioa, 100U);
    EXPECT_EQ(sva.objects[0].sva, 4711);
    EXPECT_EQ(sva.objects[1].sva, -2);

    Decoded sp = decode(find("M_SP_NA_1", true, "cot1_ca1_ioa2"));
    EXPECT_EQ(sp.objects[1].ioa, 301U);

    Decoded sc = decode(find("C_SC_TA_1", false, "cot1_ca1_ioa2"));
    EXPECT_EQ(sc.objects[0].ioa, 400U);
    expect_reference_time(sc.objects[0].cp56);
}
