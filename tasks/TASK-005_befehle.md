# TASK-005: Befehle – Select/Execute, Zeitüberwachung, Max-Delay

Status: erledigt (25.11.2026) · Module: `src/session`, `src/api`, `include/iec104/iec104.h`, `tools/selftest` · Grundlage: `intern/02_Konzept_IEC104_Library.md` („Befehle und Abläufe“), `docs/05_Normfragen_und_Festlegungen.md` (N6, N8, N13), TASK-004

## Ausgangslage

Der Server beantwortet Befehle mit Bestätigung und Beendigung; die Anwendung entscheidet über Annahme oder Ablehnung (TASK-004). Es fehlen das Befehlsmodell je Datenpunkt (direkt oder Select/Execute), die Zeitüberwachungen auf beiden Seiten und das Verwerfen verspäteter zeitgestempelter Befehle (AMD1).

## Ziel

Befehle laufen wie in der Praxis gefordert: je IOA direkt oder zweistufig, mit Zeitüberwachung der Auswahl auf der Server-Seite und der Bestätigung und Beendigung auf der Client-Seite. Zu späte Befehle mit Zeitmarke werden auf Wunsch verworfen. Der Client meldet für jeden Befehl ein eindeutiges Ergebnis.

## Umfang

1. **Befehlspunkte am Server** (`iec104_server_add_command`): gemeinsame Adresse, IOA, Typ (mit und ohne Zeitmarke), Modell direkt oder Select/Execute, Auswahl-Timeout. Ohne registrierte Befehlspunkte bleibt das Verhalten aus TASK-004 (Anwendung entscheidet allein).
2. **Select/Execute am Server**: Auswahl (S/E = 1) → Anwendung entscheidet → ACTCON; Ausführung nur nach gültiger Auswahl desselben Befehls innerhalb des Timeouts, sonst negative ACTCON; Abbruch (COT 8) → DEACTCON, Auswahl aufgehoben; Direktbefehl-Punkte lehnen eine Auswahl ab; unbekannte Befehls-IOA → Ursache 47.
3. **Max-Delay am Server** (Option, Default aus): Befehle mit CP56Time2a (58–64, 107), deren Zeitmarke älter ist als das Maximum, werden ohne Antwort verworfen (AMD1, N6). Zeitbasis UTC (N13).
4. **Befehlsverfolgung am Client**: jeder über `iec104_client_send_command` gesendete Befehl wird bis zum Ergebnis verfolgt; getrennte Timeouts für Bestätigung und Beendigung; Beendigung erwartet oder nicht (Option); Ergebnis per Callback `on_command_result`: bestätigt, beendet, ausgewählt, abgelehnt, keine Bestätigung, keine Beendigung, Verbindung verloren.
5. **Select und Execute in einem Aufruf** (`iec104_client_select_execute`): Auswahl, nach positiver Bestätigung automatisch Ausführung.
6. **Selbsttest**: Server mit Befehlspunkten (Select/Execute und direkt), Max-Delay; Client prüft Select/Execute, Execute ohne Auswahl, abgelaufene Auswahl, Abbruch, verspäteten Befehl mit Zeitmarke.
7. **Tests** ohne Netz mit simulierter Zeit und über Loopback.

## Nicht Teil dieser Aufgabe

- Pufferung und Redundanzgruppen (TASK-008), TLS (TASK-007).
- Zeitbasis Ortszeit mit SU-Bit oder fester Offset (später als Parameter; bis dahin UTC).

## Definition of Done

- [x] Jede Regel mit Quelle oder `TODO(norm)` im Kommentar.
- [x] Tests für jedes Ergebnis und jeden Fehlerfall, mit simulierter Zeit.
- [x] Selbsttest erweitert, `docs/04_Selbsttest.md` aktualisiert.
- [x] `debug`, `asan` grün, `clang`-Preset ohne Befunde, Fuzzer grün (`fuzz_apci` 300 s / 16,8 Mio., `fuzz_session` 300 s / 29,8 Mio. Eingaben).
- [x] Weitergabe-Zip neu gebaut und geprüft.

## Umsetzung (Stand 25.11.2026)

| Datei | Inhalt |
| --- | --- |
| `src/session/command_table.*` | Befehlspunkte je Station und IOA: Typ, Modell, Auswahl-Timeout; `is_command_type`, `is_select` |
| `src/session/server_session.*` | Select/Execute je Punkt (Auswahl je Verbindung, gleicher Befehl, Timeout), Abbruch (COT 8), Ursache 47 für unbekannte Befehls-IOA, falscher Typ → negative ACTCON, maximale Befehlsverzögerung; Kontext `StationContext` mit Uhr |
| `src/session/command_tracker.*` | Befehlsverfolgung am Client: Ergebnis je Befehl, getrennte Timeouts, Beendigung optional, automatisches Execute nach Select |
| `src/codec/cp56time2a.*` | `to_unix_ms`, `from_unix_ms` (einzige Kalenderrechnung, auch von der C-API genutzt) |
| `src/transport/tcp.*` | `utc_ms()` (Wanduhr) |
| `include/iec104/iec104.h` | `iec104_command_model`, `iec104_server_add_command`, `max_command_delay_ms`; `iec104_command_result`, `on_command_result`, `iec104_client_options`, `iec104_client_select_execute` |
| `src/apci/connection.*` | Testrahmen und STARTDT/STOPDT getrennt mit t1 überwacht (Fund aus dem Selbsttest, s. u.) |
| Tests | `test_session.cpp` (+6 Befehlsregeln), `test_command_tracker.cpp` (6), `test_apci_connection.cpp` (+1); 124 gesamt |
| `tools/selftest/`, `docs/04_Selbsttest.md` | Befehlspunkte im Server, sieben neue Befehlsschritte im Client |

Fund während der Umsetzung: Ein offener Testrahmen (TESTFR act) blockierte `stop()`, weil Start/Stopp und Test eine gemeinsame Überwachung teilten. Der Selbsttest scheiterte dadurch gelegentlich bei STOPDT direkt nach der Ruhephase. Beide werden jetzt getrennt überwacht; ein Regeltest bildet den Fall nach.

## Offen

| Nr. | Thema | Stand |
| --- | --- | --- |
| O1 | Ablauf Select/Execute im Normtext (Abschnitt 7) | übliche Lesart, `TODO(norm)` in `command_table.hpp` |
| O2 | Max-Delay: ungültige Zeitmarke, Zeitmarken in der Zukunft (N6) | ungültig → verwerfen; Zukunft nicht geprüft; `TODO(norm)` |
| O3 | Zeitbasis Ortszeit/fester Offset für den Max-Delay-Vergleich (N13) | UTC; Parameter folgt |
| O4 | Meldung verworfener Befehle an die Anwendung | nur stilles Verwerfen; mit Logging (TASK-006) |
| O5 | Abbruch (Deaktivierung) aus der Client-API | über `iec104_client_send_asdu` möglich, eigene Funktion fehlt |
