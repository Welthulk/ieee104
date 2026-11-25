# IEC 60870-5-104: Einführung, Recherche, Konzept und Library

Exemplarische Library für IEC 60870-5-104: Client und Server, Kern in C++17, stabile C-API, TLS über OpenSSL. Stand: 25.11.2026.

## Unterlagen

| Datei | Inhalt |
| --- | --- |
| `docs/01_IEC104_verstehen.md` | Einführung ins Protokoll, offene Fragen an die Gegenstelle |
| `docs/02_Konzept_IEC104_Library.md` | Konzept: Sprache, Architektur, Parameter, TLS, Laufzeit, Tests, offene Entscheidungen |
| `docs/03_Recherchebericht_IEC104_im_Detail.md` | Recherche: Norm, Praxis bei Netzbetreibern, Interop-Fallstricke, Sicherheit, Tests (mit Quellen) |
| `docs/04_Analyse.md` | Analyse vor Umsetzungsbeginn: Widersprüche, offene Normfragen, Code-Abgleich, Aufgabenreihe |
| `docs/05_Quellen_APCI.md` | Öffentliche Belege für die Regeln der APCI-Zustandsmaschine |
| `docs/06_Selbsttest.md` | Selbsttest-Programme und manuelle Tests: Bauen, Aufruf, Testfälle, Optionen |
| `folien/Konzept_IEC104_Library_Folien.html` | Folien zum Konzept, im Browser öffnen (10 Folien, 1920×1080) |
| `CLAUDE.md` | Regeln für Claude Code |
| `tasks/` | Aufgaben, eine nach der anderen; `TASK-000_Auftrag.md` ist der Gesamtauftrag |

## Bauen

Werkzeuge einmalig installieren: `./tools/setup_tools.sh`

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan
cmake --preset fuzz  && cmake --build --preset fuzz  && tools/make_fuzz_seeds.sh && ./build/fuzz/fuzz_codec -max_total_time=300 fuzz/corpus
```

Vor jedem Commit: `./tools/check_neutral.sh` (Begriffsliste außerhalb des Repos, siehe Kopf des Skripts).

## Selbsttest

Zwei Programme prüfen die Library über TCP, auch gegen fremde Geräte (Details: `docs/06_Selbsttest.md`):

```bash
build/debug/iec104_selftest_server --port 2404 --count 100
build/debug/iec104_selftest_client --host 127.0.0.1 --port 2404 --expect 100 --count 10 --idle 25
```

Der Client gibt je Schritt `ok`/`FAILED` und am Ende `result: PASSED` oder `FAILED` aus (Exit-Code 0/1). `ctest` lässt beide Programme automatisch über Loopback gegeneinander laufen.

## Lizenz

Rechteinhaber und Lizenztexte: `LICENSES/`. Der öffentliche Header `include/iec104/iec104.h` steht unter Apache-2.0 (`LICENSES/Apache-2.0.txt`), alle übrigen Dateien sind proprietär (`LICENSES/LicenseRef-Proprietary.txt`). Jede Quelldatei nennt ihre Lizenz im SPDX-Kopf.

## Stand

| Modul | Stand |
| --- | --- |
| `codec` | APCI (I/S/U), ASDU-Kopf, IOA, CP56Time2a, Informationselemente, Typtabelle (59 Typen), vollständige ASDUs mit SQ 0/1, Aufteilen, strikt/tolerant (`tasks/TASK-001_codec.md`) |
| `apci` | Zustandsmaschine sans-I/O (STARTDT/STOPDT/TESTFR, k/w, t1–t3), Selbsttest ohne Netz (`tasks/TASK-002_apci.md`) |
| `transport` | TCP für Linux (nicht blockierend, `poll`), Verbindungsaufbau mit t0 (`tasks/TASK-003_transport_selbsttest.md`) |
| C-API | Server und Client mit Handles, konfigurierbaren Ports und Timern, Callbacks für ASDUs und Zustände, ASDUs als Oktette |
| Selbsttest | `iec104_selftest_server`, `iec104_selftest_client`, ctest über Loopback, Gegenprobe mit lib60870 |
| `session` | offen: Generalabfrage, Uhrzeit, Befehle (TASK-004/005) |
