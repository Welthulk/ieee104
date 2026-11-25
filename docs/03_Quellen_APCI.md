# Öffentliche Belege für die APCI-Regeln

Stand: 25.11.2026 · Anlass: Der Normtext lag bei der Entwicklung nicht vor (siehe `docs/05_Normfragen_und_Festlegungen.md`). Jede Regel der Zustandsmaschine (`src/apci/connection.cpp`, R1–R12) wird hier mit öffentlicher Quelle belegt oder als offen markiert.

## Quellen

| Kürzel | Quelle | Art |
| --- | --- | --- |
| AMD1 | IEC 60870-5-104:2006/AMD1:2016, Leseprobe: https://cdn.standards.iteh.ai/samples/22910/722bcd41d12f48898e3b0b642418b49d/IEC-60870-5-104-2006-AMD1-2016.pdf | normativ (Auszug) |
| CON | Inhaltsverzeichnis der konsolidierten Ed. 2.1 (Vorschau): https://vde-verlag.de/iec-normen/preview-pdf/info_iec60870-5-104{ed2.1}b.pdf | normativ (nur Gliederung) |
| FIG17 | Bild 17 „Zustände der gesteuerten Station“ aus IEC 60870-5-104/FDIS, wiedergegeben in Kerkers, MSc-Arbeit, Universität Twente, S. 34–36: https://essay.utwente.nl/fileshare/file/72277/Kerkers_MA_EEMCS.pdf | Normentwurf, sekundär wiedergegeben |
| VUT | Matoušek, „Description and analysis of IEC 104 Protocol“, FIT VUT Brno 2017, Abschnitt 2.1: https://www.fit.vut.cz/research/publication-file/11570/TR-IEC104.pdf | sekundär, normnah |
| ORA | Interoperabilitätsliste der Norm (Abschnitt 9.6), wiedergegeben im Oracle Network Management System User's Guide, „IEC60870-5-104 Options“: https://docs.oracle.com/en/industries/energy-water/network-management-system/26000/nms-users-guide/NMS_USER_26000/10_SCADA_Extensions.10.27.html | Normtabelle, sekundär wiedergegeben |
| PRO | Protokollhandbuch eines Gateway-Herstellers: https://www.prosoft-technology.com/prosoft/download/14015/317164/file/PLX32_104S_Protocol_UM.pdf | Herstellerdoku |
| LIB | lib60870 User Guide (nur Verhaltenshinweis, kein Code übernommen): https://github.com/mz-automation/lib60870/blob/master/user_guide.adoc | sekundär |

Gliederung laut CON: 5.1 Schutz gegen Verlust und Verdopplung, 5.2 Testprozeduren, 5.3 Übertragungssteuerung mit Start/Stopp (Bild 17 gesteuerte, Bild 18 steuernde Station), 5.4 Portnummer, 5.5 maximale Zahl unquittierter I-Rahmen (k), 10 Redundante Verbindungen.

## Regeln und Belege

| Regel | Inhalt | Beleg | Zitat (gekürzt) |
| --- | --- | --- | --- |
| R1 | Nach Verbindungsaufbau ist die Datenübertragung gestoppt; die gesteuerte Station sendet dann nur U-Rahmen und Bestätigungen | VUT S. 12 | „default state is STOPDT … does not send any data … except unnumbered control functions and confirmations“ |
| R1 | Gesteuerte Station im Zustand gestoppt: I-Rahmen oder S-Rahmen → aktiv schließen | FIG17 | „S-frame[]/Active close or I-frame[]/Active close“ |
| R2 | Nur die steuernde Station sendet STARTDT; ohne Bestätigung schließt sie | VUT S. 12 | „Only the controlling station sends the STARTDT“; „If the STARTDT is not confirmed, the connection is closed“ |
| R3 | U-Rahmen werden im Zustand gestoppt angenommen (Schleife in Bild 17); beide Seiten dürfen nach Ruhe testen | FIG17, VUT S. 12 | „U-frame[]/“; „Both stations may initiate the test procedure“ |
| R4 | Folgenummern 15 Bit, nach Verbindungsaufbau 0 | VUT S. 10–11 | „two 15-bit sequence numbers … set to zero“ |
| R5 | Falsche N(S) → Verbindung schließen | PRO | „does not match the expected sequence number, the connection is closed“ |
| R6 | N(R) quittiert alle Nummern kleiner als N(R) (AMD1 ersetzt „kleiner gleich“) | AMD1, 5.1 | „valid acknowledge for all numbers < the received number“ |
| R6 | N(R) außerhalb des Fensters → schließen | **keine öffentliche Quelle** | offen, `TODO(norm): 5.1` |
| R7 | Höchstens k unquittierte I-Rahmen; Default 12, 1–32767 | ORA, CON 5.5 | „Maximum outstanding APDUs (I-Frame) without an acknowledge (S-Frame) being received., default 12“ |
| R7 | Verhalten bei Erreichen von k: Senden anhalten | LIB (sekundär); ein Hersteller schreibt „schließen“ | offen, `TODO(norm): 5.5` |
| R8 | Quittung spätestens nach w I-Rahmen; Default 8; w ≤ 2/3 k empfohlen | ORA | „Maximum received APDUs (I Frames) before an acknowledge (S-Frame) must be transmitted, default 8“; „W should not exceed two-thirds of K“ |
| R9 | Ohne eigene Daten Quittung per S-Rahmen spätestens nach t2; t2 < t1 | ORA, VUT S. 11 | „default 10 seconds, must be less than t1“ |
| R10 | t1 überwacht gesendete U- und I-Rahmen; Ablauf → schließen | FIG17 Anm. 2, ORA | „t1 is the timeout of a sent U-frame or I-frame“; „Timeout of an acknowledge (S‑Frame) must be received for any sent APDUs“ |
| R11 | t3: Testrahmen nach langer Ruhe; Bereich 1 s–48 h. t3 = 0 (abgeschaltet) verlangen einzelne Anwenderrichtlinien; nur im toleranten Modus | ORA | „Time-out for sending test frames in case of a long idle state. Range 1 s to 48 h“ |
| R12 | STOPDT act: ohne offene I-Rahmen sofort bestätigen, sonst Zustand „Pending unconfirmed stopped“, Bestätigung nach S-Rahmen, der alles quittiert; dort I-Rahmen oder t1 → schließen | FIG17 | „STOPDT act[unconfirmed I-frames]“ → „S-frame [no unconfirmed I-frames] /Send STOPDT con“ |

## Offen (keine öffentliche Quelle)

| Thema | Annahme im Code |
| --- | --- |
| Bild 18 (steuernde Station) | I-Rahmen im Zustand gestoppt: strikt schließen, tolerant annehmen. Nach eigenem STOPDT act werden eintreffende I-Rahmen sofort per S-Rahmen quittiert, damit die Gegenstelle bestätigen kann (bekanntes Interop-Problem bei verzögerter Quittung) |
| Startbedingung von t3 | t3 läuft ab dem letzten empfangenen Rahmen beliebigen Formats |
| Gleichzeitige U-Rahmen act | Ein offener Testrahmen und ein offenes STARTDT/STOPDT werden getrennt mit t1 überwacht; ein Verbindungstest blockiert Start und Stopp nicht |
| N(R) außerhalb des Fensters | schließen |
| Erreichen von k | Senden anhalten (`IEC104_ERR_WOULD_BLOCK`), nicht schließen |
| Wortlaut „modulo 32768“ | aus 15 Bit abgeleitet |

Hinweis: FIG17 stammt aus dem Normentwurf (FDIS), nicht aus der veröffentlichten Fassung. Bei Zugang zum Normtext gegen 5.1–5.5 prüfen.
