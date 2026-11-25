/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file elements.hpp
 * @brief Information elements: the building blocks inside an information object.
 *
 * An information object of an ASDU consists of an information object address
 * (IOA) followed by one or more information elements, for example
 *
 *   M_ME_NB_1 (type 11):  IOA | SVA (2 octets) | QDS (1 octet)
 *   M_SP_TB_1 (type 30):  IOA | SIQ (1 octet)  | CP56Time2a (7 octets)
 *
 * This file defines one small struct per element with a structured element
 * (several fields in one octet), the bit masks of the octet-sized elements,
 * and pure encode/decode functions. Which elements a type id consists of is
 * defined in type_table.hpp; the ASDU codec (asdu.hpp) combines both.
 *
 * Design rules:
 *   - Decoding is lossless. Every bit that is received is kept, including
 *     bits the standard reserves, so that a decoded object re-encodes to the
 *     identical octets. Reserved bits end up in the quality field of the
 *     struct or in a field named "reserved"; encoders write them back.
 *   - Decoding never fails: any octet pattern is a representable element.
 *     Value ranges are checked when encoding (never truncate), so that a
 *     caller error never turns into wrong data on the wire.
 *   - Elements have a fixed length. The functions read or write exactly
 *     that many octets; the ASDU codec checks the buffer bounds for the whole
 *     object before calling them, so they take no size argument.
 *
 * Sources: bit masks of the quality descriptors and value meanings from
 * docs/03 (section on information elements, citing the Wireshark IEC 104
 * dissector). Every bit position is also confirmed by
 * tests/data/lib60870_asdu_reference.txt (encoded by lib60870, decoded by
 * Wireshark 3.4.10); where Wireshark shows a type only as raw data, the
 * lib60870 encoding is the only source and the element says so.
 * TODO(norm): definition of the information elements, clause unknown
 * (IEC 60870-5-101 / -104, not in the documents of this repository).
 *
 * Layer: codec (pure functions, no I/O, no allocation).
 */
#pragma once

#include "codec/cp56time2a.hpp"
#include "iec104/iec104.h"

#include <cstddef>
#include <cstdint>

namespace iec104::codec {

// ---------------------------------------------------------------------------
// Element kinds and lengths
// ---------------------------------------------------------------------------

/**
 * @brief Every information element the type table can refer to.
 *
 * The abbreviations are the ones used in the standard and in Wireshark. The
 * comment gives the length in octets.
 */
enum class ElementKind : std::uint8_t {
    Siq,  ///< 1: single-point information with quality descriptor.
    Diq,  ///< 1: double-point information with quality descriptor.
    Vti,  ///< 1: value with transient state indication (step position).
    Qds,  ///< 1: quality descriptor.
    Bsi,  ///< 4: binary state information, 32-bit bitstring.
    Nva,  ///< 2: normalized value, signed 16 bit, value = raw / 32768.
    Sva,  ///< 2: scaled value, signed 16 bit.
    R32,  ///< 4: short floating point number, IEEE 754 single precision.
    Bcr,  ///< 5: binary counter reading.
    Scd,  ///< 4: status and status change detection (16 + 16 bits).
    Sep,  ///< 1: single event of protection equipment.
    Spe,  ///< 1: start events of protection equipment.
    Oci,  ///< 1: output circuit information of protection equipment.
    Qdp,  ///< 1: quality descriptor for events of protection equipment.
    Sco,  ///< 1: single command.
    Dco,  ///< 1: double command.
    Rco,  ///< 1: regulating step command.
    Qos,  ///< 1: qualifier of set-point command.
    Coi,  ///< 1: cause of initialization.
    Qoi,  ///< 1: qualifier of interrogation.
    Qcc,  ///< 1: qualifier of counter interrogation command.
    Qrp,  ///< 1: qualifier of reset process command.
    Qpm,  ///< 1: qualifier of parameter of measured values.
    Qpa,  ///< 1: qualifier of parameter activation.
    Fbp,  ///< 2: fixed test bit pattern.
    Tsc,  ///< 2: test sequence counter.
    Cp16, ///< 2: CP16Time2a, elapsed time in milliseconds.
    Cp24, ///< 3: CP24Time2a, time tag of IEC 60870-5-101 (minutes and milliseconds).
    Cp56, ///< 7: CP56Time2a, complete date and time (cp56time2a.hpp).
};

/**
 * @brief Length of an element on the wire.
 *
 * @param kind Element kind.
 * @return Number of octets, 1..7.
 */
std::size_t element_length(ElementKind kind) noexcept;

/**
 * @brief Short name of an element kind, e.g. "SIQ", for messages and tests.
 *
 * @param kind Element kind.
 * @return Pointer to a static, NUL-terminated string. Never NULL.
 */
const char* element_name(ElementKind kind) noexcept;

// ---------------------------------------------------------------------------
// Quality descriptor bits (SIQ, DIQ, QDS, QDP, SEP)
// ---------------------------------------------------------------------------

/*
 * The four upper bits have the same meaning in every element that carries a
 * quality: IV, NT, SB, BL. The lower bits depend on the element:
 *
 *   bit:    7    6    5    4  |  3    2    1    0
 *   SIQ:    IV   NT   SB   BL |  RES  RES  RES  SPI
 *   DIQ:    IV   NT   SB   BL |  RES  RES  DPI  DPI
 *   QDS:    IV   NT   SB   BL |  RES  RES  RES  OV
 *   QDP:    IV   NT   SB   BL |  EI   RES  RES  RES
 *   SEP:    IV   NT   SB   BL |  EI   RES  ES   ES
 *
 * Example: QDS 0x90 = invalid and blocked (IV | BL).
 */

/** @brief IV, invalid: the value is not reliable (e.g. acquisition failed). */
inline constexpr std::uint8_t kQualityInvalid = 0x80U;
/** @brief NT, not topical: the value was not updated successfully in time. */
inline constexpr std::uint8_t kQualityNotTopical = 0x40U;
/** @brief SB, substituted: the value was entered by an operator or a substitute source. */
inline constexpr std::uint8_t kQualitySubstituted = 0x20U;
/** @brief BL, blocked: the value is blocked for transmission and frozen at its last state. */
inline constexpr std::uint8_t kQualityBlocked = 0x10U;
/** @brief OV, overflow (QDS only): the value exceeds the range of the representation. */
inline constexpr std::uint8_t kQualityOverflow = 0x01U;
/**
 * @brief EI, elapsed time invalid (QDP and SEP only).
 *
 * Source: reference data (lib60870 encoding, M_EP_TD_1 and M_EP_TE_1);
 * Wireshark 3.4.10 shows these types only as raw data.
 */
inline constexpr std::uint8_t kQualityElapsedTimeInvalid = 0x08U;

// ---------------------------------------------------------------------------
// Monitor direction
// ---------------------------------------------------------------------------

/**
 * @brief SIQ, single-point information with quality descriptor (1 octet).
 *
 *   bit 0: SPI (0 = off, 1 = on); bits 1..3: reserved; bits 4..7: BL SB NT IV
 *
 * Example: 0x90 = off, IV | BL; 0x21 = on, SB (reference data, M_SP_NA_1).
 */
struct Siq {
    bool on{false}; ///< SPI, the state of the single point.
    std::uint8_t quality{
        0}; ///< All other bits of the octet: kQuality* bits plus reserved bits 1..3.
};

/**
 * @brief State of a double point (DPI of DIQ).
 *
 * Source: docs/03 ("0 und 3 unbestimmt, 1 AUS, 2 EIN"); Wireshark names 0
 * "indeterminate or intermediate".
 */
enum class DoublePoint : std::uint8_t {
    Intermediate = 0,  ///< Indeterminate or intermediate state.
    Off = 1,           ///< Determined state off.
    On = 2,            ///< Determined state on.
    Indeterminate = 3, ///< Indeterminate state.
};

/**
 * @brief DIQ, double-point information with quality descriptor (1 octet).
 *
 *   bits 0..1: DPI (DoublePoint); bits 2..3: reserved; bits 4..7: BL SB NT IV
 *
 * Example: 0x83 = indeterminate, IV (reference data, M_DP_NA_1).
 */
struct Diq {
    DoublePoint state{DoublePoint::Intermediate}; ///< DPI.
    std::uint8_t quality{0}; ///< All other bits: kQuality* bits plus reserved bits 2..3.
};

/**
 * @brief VTI, value with transient state indication (1 octet), e.g. tap position.
 *
 *   bits 0..6: value, signed 7 bit (-64..63); bit 7: T (1 = in transient state)
 *
 * Example: 0xC0 = -64, transient; 0x3F = 63 (reference data, M_ST_NA_1).
 */
struct Vti {
    std::int8_t value{0};  ///< -64..63.
    bool transient{false}; ///< T: the equipment is moving (e.g. tap changer in progress).
};

/**
 * @brief BCR, binary counter reading (5 octets).
 *
 *   octets 1..4: counter, signed 32 bit, little endian
 *   octet 5:     bits 0..4 SQ (sequence number 0..31) | bit 5 CY | bit 6 CA | bit 7 IV
 *
 * CY (carry): the counter overflowed in the period. CA (adjusted): the counter
 * was adjusted in the period. IV: the reading is invalid.
 *
 * Example: 40 E2 01 00 27 = 123456, sequence 7, CY (reference data, M_IT_NA_1).
 */
struct Bcr {
    std::int32_t counter{0};  ///< Counter reading.
    std::uint8_t sequence{0}; ///< SQ, sequence number 0..31.
    bool carry{false};        ///< CY.
    bool adjusted{false};     ///< CA.
    bool invalid{false};      ///< IV.
};

/**
 * @brief SCD, status and status change detection (4 octets), used by M_PS_NA_1.
 *
 *   octets 1..2: ST, 16 status bits (little endian)
 *   octets 3..4: CD, 16 change detection bits (little endian)
 *
 * Source: reference data (lib60870 encoding); Wireshark 3.4.10 shows
 * M_PS_NA_1 only as raw data.
 * TODO(norm): numbering of ST1..ST16 / CD1..CD16 within the octets, clause unknown.
 */
struct Scd {
    std::uint16_t status{0}; ///< ST bits.
    std::uint16_t change{0}; ///< CD bits: 1 = the status bit changed since the last report.
};

/**
 * @brief Event state of a SEP (bits 0..1), same coding as DoublePoint.
 *
 * Source: names of the lib60870 API; Wireshark 3.4.10 shows the protection
 * types only as raw data.
 */
enum class EventState : std::uint8_t {
    Indeterminate0 = 0, ///< Indeterminate.
    Off = 1,            ///< Off.
    On = 2,             ///< On.
    Indeterminate3 = 3, ///< Indeterminate.
};

/**
 * @brief SEP, single event of protection equipment (1 octet).
 *
 *   bits 0..1: ES (EventState); bit 2: reserved; bit 3: EI; bits 4..7: BL SB NT IV
 *
 * Example: 0x89 = off, EI | IV (reference data, M_EP_TD_1, lib60870
 * encoding only).
 */
struct Sep {
    EventState state{EventState::Indeterminate0}; ///< ES.
    std::uint8_t quality{0}; ///< All other bits: EI, kQuality* bits, reserved bit 2.
};

/*
 * SPE, start events of protection equipment (1 octet, bit field):
 *   bit 0 GS (general start), 1 SL1, 2 SL2, 3 SL3 (start of phase L1..L3),
 *   bit 4 SIE (earth current), bit 5 SRD (reverse direction), bits 6..7 reserved
 *
 * OCI, output circuit information of protection equipment (1 octet, bit field):
 *   bit 0 GC (general command to output circuit), 1 CL1, 2 CL2, 3 CL3,
 *   bits 4..7 reserved
 *
 * Both are kept as plain octets (all bits preserved). Source: reference data
 * (lib60870 encoding, M_EP_TE_1 / M_EP_TF_1); Wireshark 3.4.10 shows these
 * types only as raw data.
 */
/** @brief SPE bit GS, general start of operation. */
inline constexpr std::uint8_t kSpeGeneralStart = 0x01U;
/** @brief SPE bit SL1, start of operation phase L1. */
inline constexpr std::uint8_t kSpePhaseL1 = 0x02U;
/** @brief SPE bit SL2, start of operation phase L2. */
inline constexpr std::uint8_t kSpePhaseL2 = 0x04U;
/** @brief SPE bit SL3, start of operation phase L3. */
inline constexpr std::uint8_t kSpePhaseL3 = 0x08U;
/** @brief SPE bit SIE, start of operation earth current. */
inline constexpr std::uint8_t kSpeEarthCurrent = 0x10U;
/** @brief SPE bit SRD, start of operation in reverse direction. */
inline constexpr std::uint8_t kSpeReverseDirection = 0x20U;
/** @brief OCI bit GC, general command to output circuit. */
inline constexpr std::uint8_t kOciGeneral = 0x01U;
/** @brief OCI bit CL1, command to output circuit phase L1. */
inline constexpr std::uint8_t kOciPhaseL1 = 0x02U;
/** @brief OCI bit CL2, command to output circuit phase L2. */
inline constexpr std::uint8_t kOciPhaseL2 = 0x04U;
/** @brief OCI bit CL3, command to output circuit phase L3. */
inline constexpr std::uint8_t kOciPhaseL3 = 0x08U;

// ---------------------------------------------------------------------------
// Control direction: commands
// ---------------------------------------------------------------------------

/*
 * The command elements share one layout. The upper six bits are the
 * qualifier of command (QOC): QU and S/E.
 *
 *   bit:   7   | 6 5 4 3 2 | 1 0
 *   SCO:   S/E | QU        | RES SCS
 *   DCO:   S/E | QU        | DCS
 *   RCO:   S/E | QU        | RCS
 *
 * S/E: 1 = select, 0 = execute. QU (0..31): 0 = no pulse defined,
 * 1 = short pulse, 2 = long pulse, 3 = persistent output (Wireshark names).
 * DCS: 1 = off, 2 = on; RCS: 1 = next step lower, 2 = next step higher;
 * the value 0 and 3 of DCS/RCS are flagged as not defined by Wireshark.
 * TODO(norm): meaning of QU values 4..31, clause unknown.
 *
 * Example: SCO 0x81 = on, QU 0, select; DCO 0x09 = off, QU 2 (long pulse),
 * execute (reference data, C_SC_NA_1 and C_DC_NA_1).
 */

/** @brief SCO, single command (1 octet). Layout see above. */
struct Sco {
    bool on{false};       ///< SCS, the commanded state.
    bool reserved{false}; ///< Bit 1, reserved; kept as received.
    std::uint8_t qu{0};   ///< QU, qualifier of command, 0..31.
    bool select{false};   ///< S/E: true = select, false = execute.
};

/** @brief DCO, double command (1 octet). Layout see above. */
struct Dco {
    std::uint8_t state{0}; ///< DCS: 1 = off, 2 = on; 0 and 3 not defined.
    std::uint8_t qu{0};    ///< QU, qualifier of command, 0..31.
    bool select{false};    ///< S/E: true = select, false = execute.
};

/** @brief RCO, regulating step command (1 octet). Layout see above. */
struct Rco {
    std::uint8_t state{0}; ///< RCS: 1 = next step lower, 2 = next step higher; 0, 3 not defined.
    std::uint8_t qu{0};    ///< QU, qualifier of command, 0..31.
    bool select{false};    ///< S/E: true = select, false = execute.
};

/**
 * @brief QOS, qualifier of set-point command (1 octet).
 *
 *   bits 0..6: QL (0..127, 0 = default); bit 7: S/E (1 = select)
 *
 * Example: 0x80 = QL 0, select (reference data, C_SE_NA_1).
 */
struct Qos {
    std::uint8_t ql{0}; ///< QL, 0..127.
    bool select{false}; ///< S/E.
};

// ---------------------------------------------------------------------------
// System information
// ---------------------------------------------------------------------------

/**
 * @brief COI, cause of initialization (1 octet), used by M_EI_NA_1.
 *
 *   bits 0..6: cause (0 = local power switch on, 1 = local manual reset,
 *              2 = remote reset; Wireshark names)
 *   bit 7:     1 = initialization after change of local parameters
 *
 * Example: 0x81 = local manual reset after change of local parameters
 * (reference data, M_EI_NA_1).
 * TODO(norm): causes 3..127, clause unknown.
 */
struct Coi {
    std::uint8_t cause{0};          ///< 0..127.
    bool parameters_changed{false}; ///< Bit 7.
};

/**
 * @brief QCC, qualifier of counter interrogation command (1 octet).
 *
 *   bits 0..5: RQT (request: 1..4 = counter group 1..4, 5 = general)
 *   bits 6..7: FRZ (0 = read, 1 = freeze without reset, 2 = freeze with
 *              reset, 3 = counter reset)
 *
 * Example: 0x85 = general request, freeze with reset (reference data,
 * C_CI_NA_1). Bit positions and value names are those of the lib60870 API;
 * Wireshark 3.4.10 shows C_CI_NA_1 only as raw data.
 * TODO(norm): QCC definition, clause unknown.
 */
struct Qcc {
    std::uint8_t rqt{0}; ///< 0..63.
    std::uint8_t frz{0}; ///< 0..3.
};

/**
 * @brief QPM, qualifier of parameter of measured values (1 octet).
 *
 *   bits 0..5: KPA (0 = not used, 1 = threshold value, 2 = smoothing factor)
 *   bit 6:     LPC (1 = local parameter change)
 *   bit 7:     POP (1 = parameter not in operation)
 *
 * Names as shown by Wireshark 3.4.10. Example: 0x43 = KPA 3, LPC (reference
 * data, P_ME_NB_1).
 * TODO(norm): KPA values 3..63, clause unknown.
 */
struct Qpm {
    std::uint8_t kpa{0};          ///< 0..63.
    bool local_change{false};     ///< LPC.
    bool not_in_operation{false}; ///< POP.
};

/*
 * Elements without inner structure are plain integers in the information
 * object: QOI, QRP, QPA (1 octet, unsigned), FBP and TSC (2 octets,
 * unsigned, little endian), NVA and SVA (2 octets, signed), BSI (4 octets),
 * SPE, OCI, QDS, QDP (1 octet bit fields) and CP16Time2a (2 octets,
 * milliseconds).
 */

/** @brief QOI value for a station (general) interrogation; 21..36 = group 1..16 (docs/03). */
inline constexpr std::uint8_t kQoiStation = 20U;
/**
 * @brief Fixed test bit pattern of C_TS_NA_1: octets AA 55, read little endian.
 *
 * Source: reference data (lib60870 encoding); Wireshark 3.4.10 does not know
 * type 104.
 */
inline constexpr std::uint16_t kFixedTestBitPattern = 0x55AAU;

/**
 * @brief CP24Time2a, the 3-octet time tag of IEC 60870-5-101.
 *
 * Same layout as the first three octets of CP56Time2a:
 *
 *   octets 1..2: milliseconds incl. seconds, 0..59999
 *   octet 3:     bit 7 IV | bit 6 RES1 | bits 0..5 minute 0..59
 *
 * Only used by the IEC 101 types 2..19 (even) and 17..19, which the
 * strict IEC 104 profile rejects (docs/07_Normfragen_und_Festlegungen.md, N4).
 *
 * Example: 2B 17 01 = 5.931 s, minute 1 (reference data, M_SP_TA_1).
 */
struct Cp24Time2a {
    std::uint16_t milliseconds{0}; ///< 0..59999.
    std::uint8_t minute{0};        ///< 0..59.
    bool invalid{false};           ///< IV.
    bool res1{false};              ///< RES1, kept as received.
};

// ---------------------------------------------------------------------------
// Encode, decode, validate
// ---------------------------------------------------------------------------

/*
 * For every struct above:
 *   bool is_valid(const X&)          every field inside its range
 *   std::uint8_t encode_x(const X&)  the octet (1-octet elements), or
 *   void encode_x(const X&, out)     the octets (longer elements)
 *   X decode_x(octet) / decode_x(in) the struct, never fails
 *
 * encode_* assumes is_valid(); out-of-range bits are masked so that they can
 * never spill into a neighbouring field, but the ASDU codec checks is_valid()
 * first and reports IEC104_ERR_OUT_OF_RANGE instead.
 */

/** @brief SIQ: quality must not use bit 0 (that is the SPI). */
bool is_valid(const Siq& e) noexcept;
/** @brief Encodes a SIQ octet. */
std::uint8_t encode_siq(const Siq& e) noexcept;
/** @brief Decodes a SIQ octet. */
Siq decode_siq(std::uint8_t octet) noexcept;

/** @brief DIQ: state 0..3, quality must not use bits 0..1. */
bool is_valid(const Diq& e) noexcept;
/** @brief Encodes a DIQ octet. */
std::uint8_t encode_diq(const Diq& e) noexcept;
/** @brief Decodes a DIQ octet. */
Diq decode_diq(std::uint8_t octet) noexcept;

/** @brief VTI: value -64..63. */
bool is_valid(const Vti& e) noexcept;
/** @brief Encodes a VTI octet (value as 7-bit two's complement). */
std::uint8_t encode_vti(const Vti& e) noexcept;
/** @brief Decodes a VTI octet, sign-extending the 7-bit value. */
Vti decode_vti(std::uint8_t octet) noexcept;

/** @brief Length of a BCR in octets. */
inline constexpr std::size_t kBcrLength = 5U;
/** @brief BCR: sequence 0..31. */
bool is_valid(const Bcr& e) noexcept;
/** @brief Encodes a BCR into kBcrLength octets at @p out. */
void encode_bcr(const Bcr& e, std::uint8_t* out) noexcept;
/** @brief Decodes kBcrLength octets at @p in. */
Bcr decode_bcr(const std::uint8_t* in) noexcept;

/** @brief Length of an SCD in octets. */
inline constexpr std::size_t kScdLength = 4U;
/** @brief Encodes an SCD into kScdLength octets at @p out. Every value is valid. */
void encode_scd(const Scd& e, std::uint8_t* out) noexcept;
/** @brief Decodes kScdLength octets at @p in. */
Scd decode_scd(const std::uint8_t* in) noexcept;

/** @brief SEP: state 0..3, quality must not use bits 0..1. */
bool is_valid(const Sep& e) noexcept;
/** @brief Encodes a SEP octet. */
std::uint8_t encode_sep(const Sep& e) noexcept;
/** @brief Decodes a SEP octet. */
Sep decode_sep(std::uint8_t octet) noexcept;

/** @brief SCO: qu 0..31. */
bool is_valid(const Sco& e) noexcept;
/** @brief Encodes an SCO octet. */
std::uint8_t encode_sco(const Sco& e) noexcept;
/** @brief Decodes an SCO octet. */
Sco decode_sco(std::uint8_t octet) noexcept;

/** @brief DCO: state 0..3, qu 0..31. States 0 and 3 are encodable on purpose (tests, peers). */
bool is_valid(const Dco& e) noexcept;
/** @brief Encodes a DCO octet. */
std::uint8_t encode_dco(const Dco& e) noexcept;
/** @brief Decodes a DCO octet. */
Dco decode_dco(std::uint8_t octet) noexcept;

/** @brief RCO: state 0..3, qu 0..31. */
bool is_valid(const Rco& e) noexcept;
/** @brief Encodes an RCO octet. */
std::uint8_t encode_rco(const Rco& e) noexcept;
/** @brief Decodes an RCO octet. */
Rco decode_rco(std::uint8_t octet) noexcept;

/** @brief QOS: ql 0..127. */
bool is_valid(const Qos& e) noexcept;
/** @brief Encodes a QOS octet. */
std::uint8_t encode_qos(const Qos& e) noexcept;
/** @brief Decodes a QOS octet. */
Qos decode_qos(std::uint8_t octet) noexcept;

/** @brief COI: cause 0..127. */
bool is_valid(const Coi& e) noexcept;
/** @brief Encodes a COI octet. */
std::uint8_t encode_coi(const Coi& e) noexcept;
/** @brief Decodes a COI octet. */
Coi decode_coi(std::uint8_t octet) noexcept;

/** @brief QCC: rqt 0..63, frz 0..3. */
bool is_valid(const Qcc& e) noexcept;
/** @brief Encodes a QCC octet. */
std::uint8_t encode_qcc(const Qcc& e) noexcept;
/** @brief Decodes a QCC octet. */
Qcc decode_qcc(std::uint8_t octet) noexcept;

/** @brief QPM: kpa 0..63. */
bool is_valid(const Qpm& e) noexcept;
/** @brief Encodes a QPM octet. */
std::uint8_t encode_qpm(const Qpm& e) noexcept;
/** @brief Decodes a QPM octet. */
Qpm decode_qpm(std::uint8_t octet) noexcept;

/** @brief Length of a CP24Time2a in octets. */
inline constexpr std::size_t kCp24Time2aLength = 3U;
/** @brief CP24Time2a: milliseconds 0..59999, minute 0..59. */
bool is_valid(const Cp24Time2a& e) noexcept;
/** @brief Encodes a CP24Time2a into kCp24Time2aLength octets at @p out. */
void encode_cp24time2a(const Cp24Time2a& e, std::uint8_t* out) noexcept;
/** @brief Decodes kCp24Time2aLength octets at @p in. */
Cp24Time2a decode_cp24time2a(const std::uint8_t* in) noexcept;

/**
 * @brief Encodes an IEEE 754 single precision value (R32), 4 octets little endian.
 *
 * The bit pattern is copied unchanged, including NaN payloads and negative
 * zero, so that decoding and re-encoding is lossless.
 *
 * Example: 1.5F -> 00 00 C0 3F (reference data, M_ME_NC_1).
 */
void encode_r32(float value, std::uint8_t* out) noexcept;
/** @brief Decodes 4 octets little endian into an IEEE 754 single precision value. */
float decode_r32(const std::uint8_t* in) noexcept;

} // namespace iec104::codec
