# Interoperabilitätsliste (IEC 60870-5-104, Abschnitt 9)

IEC 60870-5-104 Library 0.9.1 · Rolle: Station und Leitstelle · Parameter: Standardwerte

Erzeugt mit `iec104_interop_list`. Gliederung und Bezeichnungen nach öffentlichen Wiedergaben der Checkliste (docs/02_Technik_und_Quellen.md, [ORA]); die Angaben stammen aus dem Code der Library. Eigene Parameter: `iec104_interop_list --config datei.conf`, zum Abgleich mit der Liste einer Gegenstelle `--format csv`.

| Zeichen | Bedeutung |
| --- | --- |
| X | unterstützt, von der Library selbst oder über ihre Schnittstelle |
| X roh | nur als rohe ASDU (`on_asdu`, `*_send_asdu`) |
| teilw. | teilweise; die Bemerkung sagt, was die Anwendung tun muss |
| – | in IEC 104 nicht vorgesehen (schwarzes Feld der Checkliste) |
| (leer) | nicht unterstützt |

## 9.1 System or device

| Punkt | Bezeichnung | Station | Leitstelle | Bemerkung |
| --- | --- | --- | --- | --- |
| Controlling station definition | Master, Client |  | X | iec104_client_* |
| Controlled station definition | Slave, Server | X |  | iec104_server_* |

## 9.2 Network configuration, 9.3 Physical layer, 9.4 Link layer

Entfallen in IEC 104 (schwarze Felder): Übertragung über TCP/IP.

## 9.5 Application layer

| Punkt | Wert | Station | Leitstelle | Bemerkung |
| --- | --- | --- | --- | --- |
| Transmission mode for application data | Mode 1 (least significant octet first) | X | X |  |
| Common address of ASDU | 2 Oktette | X | X | Norm: zwei Oktette |
| Information object address | 3 Oktette, unstrukturiert | X | X | Norm: drei Oktette |
| Cause of transmission | 2 Oktette mit Herkunftsadresse 0 | X | X | Norm: zwei Oktette |
| Length of APDU | max. 253 Oktette (ASDU 249) | X | X | Norm: 253; empfangen werden immer bis 253 (max_asdu_length) |

### 9.5 Process information in monitor direction

| Typ | Bezeichnung | Station | Leitstelle | Bemerkung |
| --- | --- | --- | --- | --- |
| <1> M_SP_NA_1 | Single-point information | X | X |  |
| <2> M_SP_TA_1 | Single-point information with time tag | – | – | CP24Time2a; nur im toleranten Modus, roh |
| <3> M_DP_NA_1 | Double-point information | X | X |  |
| <4> M_DP_TA_1 | Double-point information with time tag | – | – | CP24Time2a; nur im toleranten Modus, roh |
| <5> M_ST_NA_1 | Step position information | X | X |  |
| <6> M_ST_TA_1 | Step position information with time tag | – | – | CP24Time2a; nur im toleranten Modus, roh |
| <7> M_BO_NA_1 | Bitstring of 32 bit | X | X |  |
| <8> M_BO_TA_1 | Bitstring of 32 bit with time tag | – | – | CP24Time2a; nur im toleranten Modus, roh |
| <9> M_ME_NA_1 | Measured value, normalized value | X | X |  |
| <10> M_ME_TA_1 | Measured value, normalized value with time tag | – | – | CP24Time2a; nur im toleranten Modus, roh |
| <11> M_ME_NB_1 | Measured value, scaled value | X | X |  |
| <12> M_ME_TB_1 | Measured value, scaled value with time tag | – | – | CP24Time2a; nur im toleranten Modus, roh |
| <13> M_ME_NC_1 | Measured value, short floating point value | X | X |  |
| <14> M_ME_TC_1 | Measured value, short floating point value with time tag | – | – | CP24Time2a; nur im toleranten Modus, roh |
| <15> M_IT_NA_1 | Integrated totals | X | X |  |
| <16> M_IT_TA_1 | Integrated totals with time tag | – | – | CP24Time2a; nur im toleranten Modus, roh |
| <17> M_EP_TA_1 | Event of protection equipment with time tag | – | – | CP24Time2a; nur im toleranten Modus, roh |
| <18> M_EP_TB_1 | Packed start events of protection equipment with time tag | – | – | CP24Time2a; nur im toleranten Modus, roh |
| <19> M_EP_TC_1 | Packed output circuit information of protection equipment with time tag | – | – | CP24Time2a; nur im toleranten Modus, roh |
| <20> M_PS_NA_1 | Packed single-point information with status change detection | X roh | X roh | nicht im Prozessabbild, keine Generalabfrage |
| <21> M_ME_ND_1 | Measured value, normalized value without quality descriptor | X | X |  |
| <30> M_SP_TB_1 | Single-point information with time tag CP56Time2a | X | X | Generalabfrage antwortet mit Typ ohne Zeitmarke |
| <31> M_DP_TB_1 | Double-point information with time tag CP56Time2a | X | X | Generalabfrage antwortet mit Typ ohne Zeitmarke |
| <32> M_ST_TB_1 | Step position information with time tag CP56Time2a | X | X | Generalabfrage antwortet mit Typ ohne Zeitmarke |
| <33> M_BO_TB_1 | Bitstring of 32 bit with time tag CP56Time2a | X | X | Generalabfrage antwortet mit Typ ohne Zeitmarke |
| <34> M_ME_TD_1 | Measured value, normalized value with time tag CP56Time2a | X | X | Generalabfrage antwortet mit Typ ohne Zeitmarke |
| <35> M_ME_TE_1 | Measured value, scaled value with time tag CP56Time2a | X | X | Generalabfrage antwortet mit Typ ohne Zeitmarke |
| <36> M_ME_TF_1 | Measured value, short floating point value with time tag CP56Time2a | X | X | Generalabfrage antwortet mit Typ ohne Zeitmarke |
| <37> M_IT_TB_1 | Integrated totals with time tag CP56Time2a | X | X | Zählerabfrage antwortet mit Typ 15 |
| <38> M_EP_TD_1 | Event of protection equipment with time tag CP56Time2a | X roh | X roh |  |
| <39> M_EP_TE_1 | Packed start events of protection equipment with time tag CP56Time2a | X roh | X roh |  |
| <40> M_EP_TF_1 | Packed output circuit information of protection equipment with time tag CP56Time2a | X roh | X roh |  |

### 9.5 Process information in control direction

| Typ | Bezeichnung | Station | Leitstelle | Bemerkung |
| --- | --- | --- | --- | --- |
| <45> C_SC_NA_1 | Single command | X | X |  |
| <46> C_DC_NA_1 | Double command | X | X |  |
| <47> C_RC_NA_1 | Regulating step command | X | X |  |
| <48> C_SE_NA_1 | Set point command, normalized value | X | X |  |
| <49> C_SE_NB_1 | Set point command, scaled value | X | X |  |
| <50> C_SE_NC_1 | Set point command, short floating point value | X | X |  |
| <51> C_BO_NA_1 | Bitstring of 32 bit | X | X | ohne Select/Execute |
| <58> C_SC_TA_1 | Single command with time tag CP56Time2a | X | X | maximale Befehlsverzögerung prüfbar |
| <59> C_DC_TA_1 | Double command with time tag CP56Time2a | X | X | maximale Befehlsverzögerung prüfbar |
| <60> C_RC_TA_1 | Regulating step command with time tag CP56Time2a | X | X | maximale Befehlsverzögerung prüfbar |
| <61> C_SE_TA_1 | Set point command, normalized value with time tag CP56Time2a | X | X | maximale Befehlsverzögerung prüfbar |
| <62> C_SE_TB_1 | Set point command, scaled value with time tag CP56Time2a | X | X | maximale Befehlsverzögerung prüfbar |
| <63> C_SE_TC_1 | Set point command, short floating point value with time tag CP56Time2a | X | X | maximale Befehlsverzögerung prüfbar |
| <64> C_BO_TA_1 | Bitstring of 32 bit with time tag CP56Time2a | X | X | ohne Select/Execute |

### 9.5 System information in monitor direction

| Typ | Bezeichnung | Station | Leitstelle | Bemerkung |
| --- | --- | --- | --- | --- |
| <70> M_EI_NA_1 | End of initialization | X | X | nach dem ersten STARTDT jeder Verbindung, je Station mit Daten- oder Befehlspunkten (send_end_of_init, cause_of_init) |

### 9.5 System information in control direction

| Typ | Bezeichnung | Station | Leitstelle | Bemerkung |
| --- | --- | --- | --- | --- |
| <100> C_IC_NA_1 | Interrogation command | X | X | Station und Gruppen 1–16; Abbruch (8) wird negativ bestätigt |
| <101> C_CI_NA_1 | Counter interrogation command | X | X | Freeze/Reset nur bestätigt |
| <102> C_RD_NA_1 | Read command | X | X | Antwort aus dem Prozessabbild mit dem Typ ohne Zeitmarke, Ursache 5 |
| <103> C_CS_NA_1 | Clock synchronization command | X | X | Uhr stellt die Anwendung (on_command) |
| <104> C_TS_NA_1 | Test command | – | – | Station spiegelt ihn trotzdem; Leitstelle nur roh |
| <105> C_RP_NA_1 | Reset process command | X | X | Ausführung durch die Anwendung (on_command) |
| <106> C_CD_NA_1 | Delay acquisition command | – | – | Station antwortet negativ |
| <107> C_TS_TA_1 | Test command with time tag CP56Time2a | X | X | maximale Befehlsverzögerung prüfbar; Station spiegelt (Ursache 7) |

### 9.5 Parameter in control direction

| Typ | Bezeichnung | Station | Leitstelle | Bemerkung |
| --- | --- | --- | --- | --- |
| <110> P_ME_NA_1 | Parameter of measured value, normalized value |  | X roh | Station antwortet negativ (Ursache 7, P/N = 1) |
| <111> P_ME_NB_1 | Parameter of measured value, scaled value |  | X roh | Station antwortet negativ (Ursache 7, P/N = 1) |
| <112> P_ME_NC_1 | Parameter of measured value, short floating point value |  | X roh | Station antwortet negativ (Ursache 7, P/N = 1) |
| <113> P_AC_NA_1 | Parameter activation |  | X roh | Station antwortet negativ (Ursache 7, P/N = 1) |

### 9.5 File transfer

| Typ | Bezeichnung | Station | Leitstelle | Bemerkung |
| --- | --- | --- | --- | --- |
| <120> F_FR_NA_1 | File ready |  |  | Station antwortet mit Ursache 44 |
| <121> F_SR_NA_1 | Section ready |  |  | Station antwortet mit Ursache 44 |
| <122> F_SC_NA_1 | Call directory, select file, call file, call section |  |  | Station antwortet mit Ursache 44 |
| <123> F_LS_NA_1 | Last section, last segment |  |  | Station antwortet mit Ursache 44 |
| <124> F_AF_NA_1 | Ack file, ack section |  |  | Station antwortet mit Ursache 44 |
| <125> F_SG_NA_1 | Segment |  |  | Station antwortet mit Ursache 44 |
| <126> F_DR_TA_1 | Directory |  |  | Station antwortet mit Ursache 44 |
| <127> F_SC_NB_1 | QueryLog – Request archive file |  |  | Station antwortet mit Ursache 44 |

### 9.5 Type identification and cause of transmission assignments (Station)

Ursachen, die die Station sendet oder auswertet.

| Typ | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 20–36 | 37–41 | 44 | 45 | 46 | 47 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| <1> M_SP_NA_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <3> M_DP_NA_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <5> M_ST_NA_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <7> M_BO_NA_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <9> M_ME_NA_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <11> M_ME_NB_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <13> M_ME_NC_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <15> M_IT_NA_1 |  |  | X |  | X |  |  |  |  |  |  |  |  |  | X |  |  |  |  |
| <21> M_ME_ND_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <30> M_SP_TB_1 |  |  | X |  |  |  |  |  |  |  | X | X |  |  |  |  |  |  |  |
| <31> M_DP_TB_1 |  |  | X |  |  |  |  |  |  |  | X | X |  |  |  |  |  |  |  |
| <32> M_ST_TB_1 |  |  | X |  |  |  |  |  |  |  | X | X |  |  |  |  |  |  |  |
| <33> M_BO_TB_1 |  |  | X |  |  |  |  |  |  |  | X | X |  |  |  |  |  |  |  |
| <34> M_ME_TD_1 |  |  | X |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| <35> M_ME_TE_1 |  |  | X |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| <36> M_ME_TF_1 |  |  | X |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| <37> M_IT_TB_1 |  |  | X |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| <45> C_SC_NA_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <46> C_DC_NA_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <47> C_RC_NA_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <48> C_SE_NA_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <49> C_SE_NB_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <50> C_SE_NC_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <51> C_BO_NA_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <58> C_SC_TA_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <59> C_DC_TA_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <60> C_RC_TA_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <61> C_SE_TA_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <62> C_SE_TB_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <63> C_SE_TC_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <64> C_BO_TA_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <70> M_EI_NA_1 |  |  |  | X |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| <100> C_IC_NA_1 |  |  |  |  |  | X | X | X | X | X |  |  |  |  |  |  | X | X | X |
| <101> C_CI_NA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  |  | X | X | X |
| <102> C_RD_NA_1 |  |  |  |  | X |  |  |  |  |  |  |  |  |  |  |  | X | X | X |
| <103> C_CS_NA_1 |  |  |  |  |  | X | X |  |  |  |  |  |  |  |  |  | X | X |  |
| <104> C_TS_NA_1 |  |  |  |  |  | X | X |  |  |  |  |  |  |  |  |  | X | X |  |
| <105> C_RP_NA_1 |  |  |  |  |  | X | X |  |  |  |  |  |  |  |  |  | X | X |  |
| <106> C_CD_NA_1 |  |  |  |  |  | X | X |  |  |  |  |  |  |  |  |  | X | X |  |
| <107> C_TS_TA_1 |  |  |  |  |  | X | X |  |  |  |  |  |  |  |  |  | X | X |  |

### 9.5 Type identification and cause of transmission assignments (Leitstelle)

Ursachen, die die Leitstelle sendet oder auswertet.

| Typ | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 20–36 | 37–41 | 44 | 45 | 46 | 47 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| <1> M_SP_NA_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <3> M_DP_NA_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <5> M_ST_NA_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <7> M_BO_NA_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <9> M_ME_NA_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <11> M_ME_NB_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <13> M_ME_NC_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <15> M_IT_NA_1 |  |  | X |  | X |  |  |  |  |  |  |  |  |  | X |  |  |  |  |
| <21> M_ME_ND_1 | X | X | X |  | X |  |  |  |  |  | X | X |  | X |  |  |  |  |  |
| <30> M_SP_TB_1 |  |  | X |  |  |  |  |  |  |  | X | X |  |  |  |  |  |  |  |
| <31> M_DP_TB_1 |  |  | X |  |  |  |  |  |  |  | X | X |  |  |  |  |  |  |  |
| <32> M_ST_TB_1 |  |  | X |  |  |  |  |  |  |  | X | X |  |  |  |  |  |  |  |
| <33> M_BO_TB_1 |  |  | X |  |  |  |  |  |  |  | X | X |  |  |  |  |  |  |  |
| <34> M_ME_TD_1 |  |  | X |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| <35> M_ME_TE_1 |  |  | X |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| <36> M_ME_TF_1 |  |  | X |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| <37> M_IT_TB_1 |  |  | X |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| <45> C_SC_NA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <46> C_DC_NA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <47> C_RC_NA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <48> C_SE_NA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <49> C_SE_NB_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <50> C_SE_NC_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <51> C_BO_NA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <58> C_SC_TA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <59> C_DC_TA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <60> C_RC_TA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <61> C_SE_TA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <62> C_SE_TB_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <63> C_SE_TC_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <64> C_BO_TA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <70> M_EI_NA_1 |  |  |  | X |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |
| <100> C_IC_NA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <101> C_CI_NA_1 |  |  |  |  |  | X | X |  |  | X |  |  |  |  |  | X | X | X | X |
| <102> C_RD_NA_1 |  |  |  |  | X |  |  |  |  |  |  |  |  |  |  | X | X | X | X |
| <103> C_CS_NA_1 |  |  |  |  |  | X | X |  |  |  |  |  |  |  |  | X | X | X |  |
| <105> C_RP_NA_1 |  |  |  |  |  | X | X |  |  |  |  |  |  |  |  | X | X | X |  |
| <107> C_TS_TA_1 |  |  |  |  |  | X | X |  |  |  |  |  |  |  |  | X | X | X |  |

## 9.6 Basic application functions

| Funktion | Punkt | Station | Leitstelle | Bemerkung |
| --- | --- | --- | --- | --- |
| Station initialization | Remote initialization | teilw. | X | Station: C_RP_NA_1 an die Anwendung (on_command); Typ 70 danach sendet die Anwendung (iec104_server_send, Ursache 4); automatisch nur beim ersten STARTDT je Verbindung |
| Cyclic data transmission | Cyclic data transmission | X | X | Station: Anwendung sendet mit Ursache 1 (iec104_server_send), nicht gepuffert |
| Read procedure | Read procedure | X | X | Antwort aus dem Prozessabbild; Leitstelle: iec104_client_read() |
| Spontaneous transmission | Spontaneous transmission | X | X | iec104_server_update(); gepuffert je Redundanzgruppe bis zur Quittung |
| Double transmission | Single-point information M_SP_NA_1, M_SP_TA_1, M_SP_TB_1 and M_PS_NA_1 | X | X | Station: Anwendung sendet beide Typen |
| Double transmission | Double-point information M_DP_NA_1, M_DP_TA_1 and M_DP_TB_1 | X | X | Station: Anwendung sendet beide Typen |
| Double transmission | Step position information M_ST_NA_1, M_ST_TA_1 and M_ST_TB_1 | X | X | Station: Anwendung sendet beide Typen |
| Double transmission | Bitstring of 32 bit M_BO_NA_1, M_BO_TA_1 and M_BO_TB_1 | X | X | Station: Anwendung sendet beide Typen |
| Double transmission | Measured value, normalized value M_ME_NA_1, M_ME_TA_1, M_ME_ND_1 and M_ME_TD_1 | X | X | Station: Anwendung sendet beide Typen |
| Double transmission | Measured value, scaled value M_ME_NB_1, M_ME_TB_1 and M_ME_TE_1 | X | X | Station: Anwendung sendet beide Typen |
| Double transmission | Measured value, short floating point number M_ME_NC_1, M_ME_TC_1 and M_ME_TF_1 | X | X | Station: Anwendung sendet beide Typen |
| Station interrogation | global | X | X | Station antwortet selbst aus dem Prozessabbild |
| Station interrogation | group 1 … group 16 | X | X | Gruppenmaske je Punkt (iec104_server_add_point) |
| Clock synchronization | Clock synchronization | X | X | Station: Uhr stellt die Anwendung (on_command) |
| Clock synchronization | Day of week used | X | X | iec104_time.day_of_week, 0 = nicht verwendet |
| Clock synchronization | RES1, GEN (time tag substituted/ not substituted) used | teilw. | teilw. | nur in rohen ASDUs und gespiegelten Bestätigungen erhalten; über iec104_time immer 0 |
| Clock synchronization | SU-bit (summertime) used | X | X | iec104_time.summer_time |
| Command transmission | Direct command transmission | X | X | Station: IOA-Prüfung (Ursache 47) nur mit Befehlspunkten |
| Command transmission | Direct set point command transmission | X | X |  |
| Command transmission | Select and execute command | X | X | Station: Befehlspunkt mit Auswahl-Timeout; Abbruch (8) nur mit Befehlspunkten wirksam, sonst positive Bestätigung ohne Anwendung |
| Command transmission | Select and execute set point command | X | X | Station: Befehlspunkt mit Auswahl-Timeout |
| Command transmission | C_SE ACTTERM used | X | X | abschaltbar (command_termination, expect_termination) |
| Command transmission | No additional definition | X | X | Qualifier (QU) wird durchgereicht |
| Command transmission | Short-pulse duration | teilw. | X | Station: Dauer bestimmt die Anwendung |
| Command transmission | Long-pulse duration | teilw. | X | Station: Dauer bestimmt die Anwendung |
| Command transmission | Persistent output | teilw. | X | Station: Ausgabe bestimmt die Anwendung |
| Command transmission | Supervision of maximum delay in command direction | X |  | Station: max_command_delay_ms; Leitstelle: Zeitmarke setzt die Anwendung (iec104_object.time) |
| Transmission of integrated totals | Mode A: Local freeze with spontaneous transmission | teilw. | X | Station: Anwendung friert ein und sendet spontan |
| Transmission of integrated totals | Mode B: Local freeze with counter interrogation | teilw. | X | Station: Anwendung friert ein und legt den Wert mit iec104_server_add_point() ab (iec104_server_update() sendet spontan); Zählerabfrage antwortet aus dem Prozessabbild |
| Transmission of integrated totals | Mode C: Freeze and transmit by counter-interrogation commands | teilw. | X | Station bestätigt FRZ; Einfrieren durch die Anwendung (on_asdu) |
| Transmission of integrated totals | Mode D: Freeze by counter-interrogation command, frozen values reported spontaneously | teilw. | X | Station bestätigt FRZ; Einfrieren und Senden durch die Anwendung |
| Transmission of integrated totals | Counter read | X | X |  |
| Transmission of integrated totals | Counter freeze without reset | teilw. | X | Station: siehe Mode C |
| Transmission of integrated totals | Counter freeze with reset | teilw. | X | Station: siehe Mode C |
| Transmission of integrated totals | Counter reset | teilw. | X | Station: siehe Mode C |
| Transmission of integrated totals | General request counter | X | X |  |
| Transmission of integrated totals | Request counter group 1 … group 4 | X | X | Bits 0..3 der Gruppenmaske |
| Parameter loading | Threshold value |  |  | Station antwortet negativ |
| Parameter loading | Smoothing factor |  |  | Station antwortet negativ |
| Parameter loading | Low limit for transmission of measured values |  |  | Station antwortet negativ |
| Parameter loading | High limit for transmission of measured values |  |  | Station antwortet negativ |
| Parameter activation | Act/deact of persistent cyclic or periodic transmission of the addressed object |  |  | Station antwortet negativ |
| Test procedure | Test procedure | X | X | C_TS_TA_1 (107), TESTFR auf der Verbindungsebene |
| File transfer | File transfer in monitor direction: Transparent file |  |  |  |
| File transfer | File transfer in monitor direction: Transmission of disturbance data of protection equipment |  |  |  |
| File transfer | File transfer in monitor direction: Transmission of sequences of events |  |  |  |
| File transfer | File transfer in monitor direction: Transmission of sequences of recorded analogue values |  |  |  |
| File transfer | File transfer in control direction: Transparent file |  |  |  |
| Background scan | Background scan | X | X | Station: Anwendung sendet mit Ursache 2, nicht gepuffert |
| Acquisition of transmission delay | Acquisition of transmission delay | – | – | C_CD_NA_1 (106) |

## 9.6 Time-outs, k, w, port, redundancy, RFC 2200

| Parameter | Normwert | gewählt | Bemerkung |
| --- | --- | --- | --- |
| t0 | 30 s | 30 s | Verbindungsaufbau; mit TLS einschließlich Handshake |
| t1 | 15 s | 15 s | Quittung gesendeter I- und U-Rahmen (act) |
| t2 | 10 s | 10 s | spätestens eigene Quittung; t2 < t1 |
| t3 | 20 s | 20 s | Testrahmen nach Ruhe; Bereich 1 s … 48 h, 0 nur tolerant |
| k | 12 | 12 | Bereich 1 … 32767 |
| w | 8 | 8 | Bereich 1 … 32767; w ≤ 2/3 k empfohlen |
| Portnumber | 2404 | 2404 | frei wählbar |
| Redundant connections (Anzahl Gruppen) | – | keine (jede Verbindung eigene Gruppe) | Verbindungen je Gruppe unbegrenzt; eine aktiv; Puffer 1000 Ereignisse |
| RFC 2200 suite | Ethernet 802.3 | TCP/IP des Betriebssystems | IPv4 und IPv6 |
| Maximum allowable delay of commands | – | aus | nur Station; Befehle 58–64 und 107 |

## Ergänzung: Sicherheit (IEC 62351-3)

| Parameter | Normwert | gewählt | Bemerkung |
| --- | --- | --- | --- |
| TLS | – | aus | OpenSSL 3; TLS 1.2 und 1.3 |
