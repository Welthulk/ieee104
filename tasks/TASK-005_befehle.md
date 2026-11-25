# TASK-005: Befehle – Select/Execute, Zeitüberwachung, Max-Delay

Status: offen · Module: `src/session`, `src/api`, `include/iec104/iec104.h`, `tools/selftest` · Grundlage: `intern/02_Konzept_IEC104_Library.md` („Befehle und Abläufe“), `docs/05_Normfragen_und_Festlegungen.md` (N6, N8, N13), TASK-004

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

- [ ] Jede Regel mit Quelle oder `TODO(norm)` im Kommentar.
- [ ] Tests für jedes Ergebnis und jeden Fehlerfall, mit simulierter Zeit.
- [ ] Selbsttest erweitert, `docs/04_Selbsttest.md` aktualisiert.
- [ ] `debug`, `asan` grün, `clang`-Preset ohne Befunde, Fuzzer grün.
- [ ] Weitergabe-Zip neu gebaut und geprüft.

## Offen

- (wird beim Bearbeiten gefüllt)
