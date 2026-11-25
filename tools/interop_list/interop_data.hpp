/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file interop_data.hpp
 * @brief What the library supports, item by item of the interoperability list.
 *
 * The interoperability list (clause 9 of the standard) is the checklist both
 * sides fill in. This file holds the library's answers; iec104_interop_list
 * prints them. Structure and item names follow the public reproductions of
 * the checklist (docs/02_Technik_und_Quellen.md, [ORA]).
 * The answers are taken from the code; the test InteropList.MatchesTypeTable
 * checks the type rows against the codec's type table.
 *
 * Marks (columns "Station" = controlled station, "Leitstelle" = controlling
 * station):
 *
 * | Mark    | Meaning                                                          |
 * |---------|------------------------------------------------------------------|
 * | X       | supported, by the library itself or through the API              |
 * | X roh   | possible as raw ASDU only (on_asdu, *_send_asdu)                 |
 * | teilw.  | partly; the remark says what the application has to do           |
 * | –       | not provided for in IEC 104 (black box in the checklist)         |
 * | (empty) | not supported                                                    |
 */
#pragma once

#include <cstdint>

namespace interop {

enum class Mark : std::uint8_t { No, Yes, Raw, Partial, NotIn104 };

enum class Group : std::uint8_t { Monitor, Control, SystemMonitor, SystemControl, Parameter, File };

/** @brief One type identification. Causes: list such as "3 5 20-36" (sent or received). */
struct TypeRow {
    std::uint8_t id;
    const char* name;        ///< Mnemonic, e.g. "M_SP_NA_1" (must match the codec).
    const char* description; ///< As in the checklist.
    Group group;
    Mark station;
    Mark master;
    const char* station_causes;
    const char* master_causes;
    const char* remark; ///< German, short.
};

constexpr const char* kMonitorCauses = "1 2 3 5 11 12 20-36";
constexpr const char* kCommandStation = "6 7 8 9 10 45 46 47";
constexpr const char* kCommandMaster = "6 7 10 44 45 46 47";

// clang-format off
inline constexpr TypeRow kTypes[] = {
    // Process information in monitor direction.
    {1, "M_SP_NA_1", "Single-point information", Group::Monitor, Mark::Yes, Mark::Yes, kMonitorCauses, kMonitorCauses, ""},
    {2, "M_SP_TA_1", "Single-point information with time tag", Group::Monitor, Mark::NotIn104, Mark::NotIn104, "", "", "CP24Time2a; nur im toleranten Modus, roh"},
    {3, "M_DP_NA_1", "Double-point information", Group::Monitor, Mark::Yes, Mark::Yes, kMonitorCauses, kMonitorCauses, ""},
    {4, "M_DP_TA_1", "Double-point information with time tag", Group::Monitor, Mark::NotIn104, Mark::NotIn104, "", "", "CP24Time2a; nur im toleranten Modus, roh"},
    {5, "M_ST_NA_1", "Step position information", Group::Monitor, Mark::Yes, Mark::Yes, kMonitorCauses, kMonitorCauses, ""},
    {6, "M_ST_TA_1", "Step position information with time tag", Group::Monitor, Mark::NotIn104, Mark::NotIn104, "", "", "CP24Time2a; nur im toleranten Modus, roh"},
    {7, "M_BO_NA_1", "Bitstring of 32 bit", Group::Monitor, Mark::Yes, Mark::Yes, kMonitorCauses, kMonitorCauses, ""},
    {8, "M_BO_TA_1", "Bitstring of 32 bit with time tag", Group::Monitor, Mark::NotIn104, Mark::NotIn104, "", "", "CP24Time2a; nur im toleranten Modus, roh"},
    {9, "M_ME_NA_1", "Measured value, normalized value", Group::Monitor, Mark::Yes, Mark::Yes, kMonitorCauses, kMonitorCauses, ""},
    {10, "M_ME_TA_1", "Measured value, normalized value with time tag", Group::Monitor, Mark::NotIn104, Mark::NotIn104, "", "", "CP24Time2a; nur im toleranten Modus, roh"},
    {11, "M_ME_NB_1", "Measured value, scaled value", Group::Monitor, Mark::Yes, Mark::Yes, kMonitorCauses, kMonitorCauses, ""},
    {12, "M_ME_TB_1", "Measured value, scaled value with time tag", Group::Monitor, Mark::NotIn104, Mark::NotIn104, "", "", "CP24Time2a; nur im toleranten Modus, roh"},
    {13, "M_ME_NC_1", "Measured value, short floating point value", Group::Monitor, Mark::Yes, Mark::Yes, kMonitorCauses, kMonitorCauses, ""},
    {14, "M_ME_TC_1", "Measured value, short floating point value with time tag", Group::Monitor, Mark::NotIn104, Mark::NotIn104, "", "", "CP24Time2a; nur im toleranten Modus, roh"},
    {15, "M_IT_NA_1", "Integrated totals", Group::Monitor, Mark::Yes, Mark::Yes, "3 5 37-41", "3 5 37-41", ""},
    {16, "M_IT_TA_1", "Integrated totals with time tag", Group::Monitor, Mark::NotIn104, Mark::NotIn104, "", "", "CP24Time2a; nur im toleranten Modus, roh"},
    {17, "M_EP_TA_1", "Event of protection equipment with time tag", Group::Monitor, Mark::NotIn104, Mark::NotIn104, "", "", "CP24Time2a; nur im toleranten Modus, roh"},
    {18, "M_EP_TB_1", "Packed start events of protection equipment with time tag", Group::Monitor, Mark::NotIn104, Mark::NotIn104, "", "", "CP24Time2a; nur im toleranten Modus, roh"},
    {19, "M_EP_TC_1", "Packed output circuit information of protection equipment with time tag", Group::Monitor, Mark::NotIn104, Mark::NotIn104, "", "", "CP24Time2a; nur im toleranten Modus, roh"},
    {20, "M_PS_NA_1", "Packed single-point information with status change detection", Group::Monitor, Mark::Raw, Mark::Raw, "", "", "nicht im Prozessabbild, keine Generalabfrage"},
    {21, "M_ME_ND_1", "Measured value, normalized value without quality descriptor", Group::Monitor, Mark::Yes, Mark::Yes, kMonitorCauses, kMonitorCauses, ""},
    {30, "M_SP_TB_1", "Single-point information with time tag CP56Time2a", Group::Monitor, Mark::Yes, Mark::Yes, "3 11 12", "3 11 12", "Generalabfrage antwortet mit Typ ohne Zeitmarke"},
    {31, "M_DP_TB_1", "Double-point information with time tag CP56Time2a", Group::Monitor, Mark::Yes, Mark::Yes, "3 11 12", "3 11 12", "Generalabfrage antwortet mit Typ ohne Zeitmarke"},
    {32, "M_ST_TB_1", "Step position information with time tag CP56Time2a", Group::Monitor, Mark::Yes, Mark::Yes, "3 11 12", "3 11 12", "Generalabfrage antwortet mit Typ ohne Zeitmarke"},
    {33, "M_BO_TB_1", "Bitstring of 32 bit with time tag CP56Time2a", Group::Monitor, Mark::Yes, Mark::Yes, "3 11 12", "3 11 12", "Generalabfrage antwortet mit Typ ohne Zeitmarke"},
    {34, "M_ME_TD_1", "Measured value, normalized value with time tag CP56Time2a", Group::Monitor, Mark::Yes, Mark::Yes, "3", "3", "Generalabfrage antwortet mit Typ ohne Zeitmarke"},
    {35, "M_ME_TE_1", "Measured value, scaled value with time tag CP56Time2a", Group::Monitor, Mark::Yes, Mark::Yes, "3", "3", "Generalabfrage antwortet mit Typ ohne Zeitmarke"},
    {36, "M_ME_TF_1", "Measured value, short floating point value with time tag CP56Time2a", Group::Monitor, Mark::Yes, Mark::Yes, "3", "3", "Generalabfrage antwortet mit Typ ohne Zeitmarke"},
    {37, "M_IT_TB_1", "Integrated totals with time tag CP56Time2a", Group::Monitor, Mark::Yes, Mark::Yes, "3", "3", "Zählerabfrage antwortet mit Typ 15"},
    {38, "M_EP_TD_1", "Event of protection equipment with time tag CP56Time2a", Group::Monitor, Mark::Raw, Mark::Raw, "", "", ""},
    {39, "M_EP_TE_1", "Packed start events of protection equipment with time tag CP56Time2a", Group::Monitor, Mark::Raw, Mark::Raw, "", "", ""},
    {40, "M_EP_TF_1", "Packed output circuit information of protection equipment with time tag CP56Time2a", Group::Monitor, Mark::Raw, Mark::Raw, "", "", ""},

    // Process information in control direction.
    {45, "C_SC_NA_1", "Single command", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, ""},
    {46, "C_DC_NA_1", "Double command", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, ""},
    {47, "C_RC_NA_1", "Regulating step command", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, ""},
    {48, "C_SE_NA_1", "Set point command, normalized value", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, ""},
    {49, "C_SE_NB_1", "Set point command, scaled value", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, ""},
    {50, "C_SE_NC_1", "Set point command, short floating point value", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, ""},
    {51, "C_BO_NA_1", "Bitstring of 32 bit", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, "ohne Select/Execute"},
    {58, "C_SC_TA_1", "Single command with time tag CP56Time2a", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, "maximale Befehlsverzögerung prüfbar"},
    {59, "C_DC_TA_1", "Double command with time tag CP56Time2a", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, "maximale Befehlsverzögerung prüfbar"},
    {60, "C_RC_TA_1", "Regulating step command with time tag CP56Time2a", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, "maximale Befehlsverzögerung prüfbar"},
    {61, "C_SE_TA_1", "Set point command, normalized value with time tag CP56Time2a", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, "maximale Befehlsverzögerung prüfbar"},
    {62, "C_SE_TB_1", "Set point command, scaled value with time tag CP56Time2a", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, "maximale Befehlsverzögerung prüfbar"},
    {63, "C_SE_TC_1", "Set point command, short floating point value with time tag CP56Time2a", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, "maximale Befehlsverzögerung prüfbar"},
    {64, "C_BO_TA_1", "Bitstring of 32 bit with time tag CP56Time2a", Group::Control, Mark::Yes, Mark::Yes, kCommandStation, kCommandMaster, "ohne Select/Execute"},

    // System information in monitor direction.
    {70, "M_EI_NA_1", "End of initialization", Group::SystemMonitor, Mark::Yes, Mark::Yes, "4", "4", "nach dem ersten STARTDT jeder Verbindung, je Station mit Daten- oder Befehlspunkten (send_end_of_init, cause_of_init)"},

    // System information in control direction.
    {100, "C_IC_NA_1", "Interrogation command", Group::SystemControl, Mark::Yes, Mark::Yes, "6 7 8 9 10 45 46 47", "6 7 10 44 45 46 47", "Station und Gruppen 1–16; Abbruch (8) wird negativ bestätigt"},
    {101, "C_CI_NA_1", "Counter interrogation command", Group::SystemControl, Mark::Yes, Mark::Yes, "6 7 10 45 46 47", "6 7 10 44 45 46 47", "Freeze/Reset nur bestätigt"},
    {102, "C_RD_NA_1", "Read command", Group::SystemControl, Mark::Yes, Mark::Yes, "5 45 46 47", "5 44 45 46 47", "Antwort aus dem Prozessabbild mit dem Typ ohne Zeitmarke, Ursache 5"},
    {103, "C_CS_NA_1", "Clock synchronization command", Group::SystemControl, Mark::Yes, Mark::Yes, "6 7 45 46", "6 7 44 45 46", "Uhr stellt die Anwendung (on_command)"},
    {104, "C_TS_NA_1", "Test command", Group::SystemControl, Mark::NotIn104, Mark::NotIn104, "6 7 45 46", "", "Station spiegelt ihn trotzdem; Leitstelle nur roh"},
    {105, "C_RP_NA_1", "Reset process command", Group::SystemControl, Mark::Yes, Mark::Yes, "6 7 45 46", "6 7 44 45 46", "Ausführung durch die Anwendung (on_command)"},
    {106, "C_CD_NA_1", "Delay acquisition command", Group::SystemControl, Mark::NotIn104, Mark::NotIn104, "6 7 45 46", "", "Station antwortet negativ"},
    {107, "C_TS_TA_1", "Test command with time tag CP56Time2a", Group::SystemControl, Mark::Yes, Mark::Yes, "6 7 45 46", "6 7 44 45 46", "maximale Befehlsverzögerung prüfbar; Station spiegelt (Ursache 7)"},

    // Parameter in control direction.
    {110, "P_ME_NA_1", "Parameter of measured value, normalized value", Group::Parameter, Mark::No, Mark::Raw, "", "", "Station antwortet negativ (Ursache 7, P/N = 1)"},
    {111, "P_ME_NB_1", "Parameter of measured value, scaled value", Group::Parameter, Mark::No, Mark::Raw, "", "", "Station antwortet negativ (Ursache 7, P/N = 1)"},
    {112, "P_ME_NC_1", "Parameter of measured value, short floating point value", Group::Parameter, Mark::No, Mark::Raw, "", "", "Station antwortet negativ (Ursache 7, P/N = 1)"},
    {113, "P_AC_NA_1", "Parameter activation", Group::Parameter, Mark::No, Mark::Raw, "", "", "Station antwortet negativ (Ursache 7, P/N = 1)"},

    // File transfer (not in the codec's type table).
    {120, "F_FR_NA_1", "File ready", Group::File, Mark::No, Mark::No, "", "", "Station antwortet mit Ursache 44"},
    {121, "F_SR_NA_1", "Section ready", Group::File, Mark::No, Mark::No, "", "", "Station antwortet mit Ursache 44"},
    {122, "F_SC_NA_1", "Call directory, select file, call file, call section", Group::File, Mark::No, Mark::No, "", "", "Station antwortet mit Ursache 44"},
    {123, "F_LS_NA_1", "Last section, last segment", Group::File, Mark::No, Mark::No, "", "", "Station antwortet mit Ursache 44"},
    {124, "F_AF_NA_1", "Ack file, ack section", Group::File, Mark::No, Mark::No, "", "", "Station antwortet mit Ursache 44"},
    {125, "F_SG_NA_1", "Segment", Group::File, Mark::No, Mark::No, "", "", "Station antwortet mit Ursache 44"},
    {126, "F_DR_TA_1", "Directory", Group::File, Mark::No, Mark::No, "", "", "Station antwortet mit Ursache 44"},
    {127, "F_SC_NB_1", "QueryLog – Request archive file", Group::File, Mark::No, Mark::No, "", "", "Station antwortet mit Ursache 44"},
};

/** @brief One item of the basic application functions (9.6). */
struct FunctionRow {
    const char* group; ///< Heading in the checklist, e.g. "Command transmission".
    const char* item;
    Mark station;
    Mark master;
    const char* remark;
};

inline constexpr FunctionRow kFunctions[] = {
    {"Station initialization", "Remote initialization", Mark::Partial, Mark::Yes, "Station: C_RP_NA_1 an die Anwendung (on_command); Typ 70 danach sendet die Anwendung (iec104_server_send, Ursache 4); automatisch nur beim ersten STARTDT je Verbindung"},
    {"Cyclic data transmission", "Cyclic data transmission", Mark::Yes, Mark::Yes, "Station: Anwendung sendet mit Ursache 1 (iec104_server_send), nicht gepuffert"},
    {"Read procedure", "Read procedure", Mark::Yes, Mark::Yes, "Antwort aus dem Prozessabbild; Leitstelle: iec104_client_read()"},
    {"Spontaneous transmission", "Spontaneous transmission", Mark::Yes, Mark::Yes, "iec104_server_update(); gepuffert je Redundanzgruppe bis zur Quittung"},
    {"Double transmission", "Single-point information M_SP_NA_1, M_SP_TA_1, M_SP_TB_1 and M_PS_NA_1", Mark::Yes, Mark::Yes, "Station: Anwendung sendet beide Typen"},
    {"Double transmission", "Double-point information M_DP_NA_1, M_DP_TA_1 and M_DP_TB_1", Mark::Yes, Mark::Yes, "Station: Anwendung sendet beide Typen"},
    {"Double transmission", "Step position information M_ST_NA_1, M_ST_TA_1 and M_ST_TB_1", Mark::Yes, Mark::Yes, "Station: Anwendung sendet beide Typen"},
    {"Double transmission", "Bitstring of 32 bit M_BO_NA_1, M_BO_TA_1 and M_BO_TB_1", Mark::Yes, Mark::Yes, "Station: Anwendung sendet beide Typen"},
    {"Double transmission", "Measured value, normalized value M_ME_NA_1, M_ME_TA_1, M_ME_ND_1 and M_ME_TD_1", Mark::Yes, Mark::Yes, "Station: Anwendung sendet beide Typen"},
    {"Double transmission", "Measured value, scaled value M_ME_NB_1, M_ME_TB_1 and M_ME_TE_1", Mark::Yes, Mark::Yes, "Station: Anwendung sendet beide Typen"},
    {"Double transmission", "Measured value, short floating point number M_ME_NC_1, M_ME_TC_1 and M_ME_TF_1", Mark::Yes, Mark::Yes, "Station: Anwendung sendet beide Typen"},
    {"Station interrogation", "global", Mark::Yes, Mark::Yes, "Station antwortet selbst aus dem Prozessabbild"},
    {"Station interrogation", "group 1 … group 16", Mark::Yes, Mark::Yes, "Gruppenmaske je Punkt (iec104_server_add_point)"},
    {"Clock synchronization", "Clock synchronization", Mark::Yes, Mark::Yes, "Station: Uhr stellt die Anwendung (on_command)"},
    {"Clock synchronization", "Day of week used", Mark::Yes, Mark::Yes, "iec104_time.day_of_week, 0 = nicht verwendet"},
    {"Clock synchronization", "RES1, GEN (time tag substituted/ not substituted) used", Mark::Partial, Mark::Partial, "nur in rohen ASDUs und gespiegelten Bestätigungen erhalten; über iec104_time immer 0"},
    {"Clock synchronization", "SU-bit (summertime) used", Mark::Yes, Mark::Yes, "iec104_time.summer_time"},
    {"Command transmission", "Direct command transmission", Mark::Yes, Mark::Yes, "Station: IOA-Prüfung (Ursache 47) nur mit Befehlspunkten"},
    {"Command transmission", "Direct set point command transmission", Mark::Yes, Mark::Yes, ""},
    {"Command transmission", "Select and execute command", Mark::Yes, Mark::Yes, "Station: Befehlspunkt mit Auswahl-Timeout; Abbruch (8) nur mit Befehlspunkten wirksam, sonst positive Bestätigung ohne Anwendung"},
    {"Command transmission", "Select and execute set point command", Mark::Yes, Mark::Yes, "Station: Befehlspunkt mit Auswahl-Timeout"},
    {"Command transmission", "C_SE ACTTERM used", Mark::Yes, Mark::Yes, "abschaltbar (command_termination, expect_termination)"},
    {"Command transmission", "No additional definition", Mark::Yes, Mark::Yes, "Qualifier (QU) wird durchgereicht"},
    {"Command transmission", "Short-pulse duration", Mark::Partial, Mark::Yes, "Station: Dauer bestimmt die Anwendung"},
    {"Command transmission", "Long-pulse duration", Mark::Partial, Mark::Yes, "Station: Dauer bestimmt die Anwendung"},
    {"Command transmission", "Persistent output", Mark::Partial, Mark::Yes, "Station: Ausgabe bestimmt die Anwendung"},
    {"Command transmission", "Supervision of maximum delay in command direction", Mark::Yes, Mark::No, "Station: max_command_delay_ms; Leitstelle: Zeitmarke setzt die Anwendung (iec104_object.time)"},
    {"Transmission of integrated totals", "Mode A: Local freeze with spontaneous transmission", Mark::Partial, Mark::Yes, "Station: Anwendung friert ein und sendet spontan"},
    {"Transmission of integrated totals", "Mode B: Local freeze with counter interrogation", Mark::Partial, Mark::Yes, "Station: Anwendung friert ein und legt den Wert mit iec104_server_add_point() ab (iec104_server_update() sendet spontan); Zählerabfrage antwortet aus dem Prozessabbild"},
    {"Transmission of integrated totals", "Mode C: Freeze and transmit by counter-interrogation commands", Mark::Partial, Mark::Yes, "Station bestätigt FRZ; Einfrieren durch die Anwendung (on_asdu)"},
    {"Transmission of integrated totals", "Mode D: Freeze by counter-interrogation command, frozen values reported spontaneously", Mark::Partial, Mark::Yes, "Station bestätigt FRZ; Einfrieren und Senden durch die Anwendung"},
    {"Transmission of integrated totals", "Counter read", Mark::Yes, Mark::Yes, ""},
    {"Transmission of integrated totals", "Counter freeze without reset", Mark::Partial, Mark::Yes, "Station: siehe Mode C"},
    {"Transmission of integrated totals", "Counter freeze with reset", Mark::Partial, Mark::Yes, "Station: siehe Mode C"},
    {"Transmission of integrated totals", "Counter reset", Mark::Partial, Mark::Yes, "Station: siehe Mode C"},
    {"Transmission of integrated totals", "General request counter", Mark::Yes, Mark::Yes, ""},
    {"Transmission of integrated totals", "Request counter group 1 … group 4", Mark::Yes, Mark::Yes, "Bits 0..3 der Gruppenmaske"},
    {"Parameter loading", "Threshold value", Mark::No, Mark::No, "Station antwortet negativ"},
    {"Parameter loading", "Smoothing factor", Mark::No, Mark::No, "Station antwortet negativ"},
    {"Parameter loading", "Low limit for transmission of measured values", Mark::No, Mark::No, "Station antwortet negativ"},
    {"Parameter loading", "High limit for transmission of measured values", Mark::No, Mark::No, "Station antwortet negativ"},
    {"Parameter activation", "Act/deact of persistent cyclic or periodic transmission of the addressed object", Mark::No, Mark::No, "Station antwortet negativ"},
    {"Test procedure", "Test procedure", Mark::Yes, Mark::Yes, "C_TS_TA_1 (107), TESTFR auf der Verbindungsebene"},
    {"File transfer", "File transfer in monitor direction: Transparent file", Mark::No, Mark::No, ""},
    {"File transfer", "File transfer in monitor direction: Transmission of disturbance data of protection equipment", Mark::No, Mark::No, ""},
    {"File transfer", "File transfer in monitor direction: Transmission of sequences of events", Mark::No, Mark::No, ""},
    {"File transfer", "File transfer in monitor direction: Transmission of sequences of recorded analogue values", Mark::No, Mark::No, ""},
    {"File transfer", "File transfer in control direction: Transparent file", Mark::No, Mark::No, ""},
    {"Background scan", "Background scan", Mark::Yes, Mark::Yes, "Station: Anwendung sendet mit Ursache 2, nicht gepuffert"},
    {"Acquisition of transmission delay", "Acquisition of transmission delay", Mark::NotIn104, Mark::NotIn104, "C_CD_NA_1 (106)"},
};
// clang-format on

} // namespace interop
