# TASK-002: APCI-Zustandsmaschine (sans-I/O) mit Selbsttest ohne Netz

Status: erledigt (25.11.2026) · Modul: `src/apci` · Grundlage: `intern/02_Konzept` (Timer, k/w, Testkonzept Ebene 2), `intern/03_Recherchebericht` (APCI, Timer), `intern/04_Analyse.md` (N1, N2, W4, W6, F4, F10)

## Ausgangslage

Der Codec ist fertig (TASK-001): APCI-Rahmen I/S/U, vollständige ASDUs. Es fehlt die Logik einer Verbindung: wann welcher Rahmen gesendet werden darf, Quittierung, Timer, Abbruchbedingungen. Die Normtexte stehen nicht zur Verfügung (F4). Jede Regel wird aus öffentlichen Quellen belegt; Unbelegtes wird `TODO(norm)`.

## Ziel

Eine Verbindung (Client- oder Server-Rolle) als reine Zustandsmaschine: Bytes und Zeit rein, Bytes und Ereignisse raus, kein Socket, keine Systemuhr. Zwei Instanzen lassen sich im Test über einen Speicherkanal koppeln und mit simulierter Zeit gegeneinander laufen lassen (Selbsttest ohne Netz, F10).

## Umfang

1. **Konfiguration** (`src/apci/apci_config.hpp/.cpp`): t0–t3 in Millisekunden, k, w, strikt/tolerant, Plausibilitätsprüfung t2 < t1 und w ≤ 2/3 k (Warnung / Fehler / aus, Default Warnung, W4). Bereiche nach `intern/02_Konzept_IEC104_Library.md`, t3 = 0 (aus) nur tolerant (W6).
2. **Verbindung** (`src/apci/connection.hpp/.cpp`): Rolle (steuernde / gesteuerte Station), Zustände gestoppt / Start angefordert / gestartet / Stopp angefordert / geschlossen. STARTDT, STOPDT, TESTFR act/con, I-Rahmen mit N(S)/N(R) modulo 32768, S-Rahmen nach w empfangenen I-Rahmen oder Ablauf t2, Senden gesperrt bei k unquittierten I-Rahmen, t1 für unquittierte I-Rahmen und U-Rahmen act, t3 Testrahmen bei Ruhe, Abbruch bei falscher Folgenummer oder N(R) außerhalb des Fensters.
3. **Schnittstelle ohne Allokation im Betrieb**: Eingabe `receive(bytes, now)`, `tick(now)`, `start()`, `stop()`, `send_asdu()`; Ausgabe `take_output()` (zu sendende Bytes) und `next_event()` (ASDU empfangen, Zustand geändert, quittiert, Verbindung schließen mit Grund). `next_deadline()` für die spätere Event-Loop.
4. **Neue Statuscodes** in `iec104.h`, nur angehängt: Senden derzeit nicht möglich (Fenster k voll oder Ausgabepuffer voll), Aufruf im aktuellen Zustand nicht erlaubt.
5. **Tests mit simulierter Zeit**: jede Regel aus Punkt 2 einzeln, Zählerüberlauf bei 32767 → 0, alle Timer-Pfade, falsches N(R)/N(S).
6. **Selbsttest ohne Netz**: Client- und Server-Instanz über Speicherkanal gekoppelt; STARTDT, Austausch von ASDUs in beide Richtungen mit Quittierung, k-Grenze, Ruhe mit TESTFR, STOPDT mit offenen Quittungen, Verbindungsabbruch.
7. **Fuzzer** `fuzz/fuzz_apci.cpp`: beliebige Bytes und Zeitsprünge; Invarianten: keine Abstürze, jede Ausgabe ist ein gültiger APCI-Rahmen, nie mehr als k unquittierte I-Rahmen.

## Nicht Teil dieser Aufgabe

- TCP, Sockets, t0 (Verbindungsaufbau gehört zum Transport, TASK-003; t0 steht nur in der Konfiguration).
- Pufferung unquittierter ASDUs für Redundanzumschaltung (TASK-008); die Verbindung meldet nur, bis wohin quittiert ist.
- Session-Abläufe (GA, Befehle).

## Definition of Done

- [x] Jede Regel im Code mit Quelle oder `TODO(norm)` im Kommentar.
- [x] Tests für alle Regeln und Timer-Pfade mit simulierter Zeit, Selbsttest ohne Netz grün.
- [x] `ctest --preset debug` und `ctest --preset asan` grün, `clang`-Preset ohne clang-tidy-Befunde.
- [x] Fuzzer für die Zustandsmaschine läuft 10 Minuten ohne Befund (600 s, 8,5 Mio. Eingaben).
- [x] Offene Normfragen unter „Offen“ eingetragen.

## Umsetzung (Stand 25.11.2026)

| Datei | Inhalt |
| --- | --- |
| `src/apci/apci_config.hpp/.cpp` | Timer in ms, k, w, Rolle, strikt/tolerant, Plausibilitätsprüfung Warnung/Fehler/aus |
| `src/apci/connection.hpp/.cpp` | Zustandsmaschine mit Regeln R1–R12, Ereigniswarteschlange, Ausgabepuffer, `next_deadline()` |
| `docs/03_Quellen_APCI.md` | öffentliche Belege je Regel, Liste des Unbelegten |
| `include/iec104/iec104.h` | neue Statuscodes `IEC104_ERR_WOULD_BLOCK` (-7), `IEC104_ERR_WRONG_STATE` (-8) |
| `tests/apci_pair.hpp` | zwei Verbindungen im Speicher gekoppelt, simulierte Zeit |
| `tests/test_apci_connection.cpp` | 23 Tests: Konfiguration und jede Regel R1–R12 |
| `tests/test_apci_selftest.cpp` | Selbsttest ohne Netz: 1000 + 100 ASDUs, Ruhe mit Testrahmen, Stopp, Neustart, Schließen |
| `fuzz/fuzz_apci.cpp` | Skript-Fuzzer: Bytes, Zeit, Senden, Start/Stopp/Schließen |

Entwurfsentscheidungen:

- Die Verbindung kennt keine Sockets und keine Uhr; Zeit kommt als Millisekunden vom Aufrufer. Einzige Allokation: k Zeitstempel in `init()`.
- Empfang hält an, wenn die Ereigniswarteschlange voll ist (Rückgabe = verbrauchte Bytes); kein Ereignis geht verloren.
- Steuerrahmen (S, U) haben eine Reserve im Ausgabepuffer; ASDUs bekommen `IEC104_ERR_WOULD_BLOCK`, bevor die Reserve angegriffen wird.
- Bei Erreichen von k wird das Senden angehalten, nicht geschlossen.

## Offen

Siehe `docs/03_Quellen_APCI.md`, Abschnitt „Offen“. Zusammengefasst:

| Nr. | Thema | Annahme im Code |
| --- | --- | --- |
| O1 | Normtext 5.1–5.5 nicht öffentlich; Bild 17 nur aus dem Normentwurf (FDIS) | Regeln nach Bild 17, AMD1, Interop-Liste; je Stelle `TODO(norm)` |
| O2 | Bild 18 (steuernde Station) nicht öffentlich | I-Rahmen im Zustand gestoppt: strikt schließen; nach eigenem STOPDT act sofort quittieren |
| O3 | N(R) außerhalb des Fensters, falsche N(S) | schließen (N(S) nur durch Herstellerdoku belegt) |
| O4 | Erreichen von k | Senden anhalten statt schließen |
| O5 | Startbedingung t3 | ab letztem empfangenem Rahmen |
