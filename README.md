# IEC 60870-5-104: Einführung, Recherche, Konzept und Library

Exemplarische Library für IEC 60870-5-104: Client und Server, Kern in C++17, stabile C-API, TLS über OpenSSL. Stand: 25.11.2026.

## Unterlagen

| Datei | Inhalt |
| --- | --- |
| `docs/01_IEC104_verstehen.md` | Einführung ins Protokoll, offene Fragen an die Gegenstelle |
| `docs/02_Konzept_IEC104_Library.md` | Konzept: Sprache, Architektur, Parameter, TLS, Laufzeit, Tests, offene Entscheidungen |
| `docs/03_Recherchebericht_IEC104_im_Detail.md` | Recherche: Norm, Praxis bei Netzbetreibern, Interop-Fallstricke, Sicherheit, Tests (mit Quellen) |
| `docs/04_Analyse.md` | Analyse vor Umsetzungsbeginn: Widersprüche, offene Normfragen, Code-Abgleich, Aufgabenreihe |
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

## Lizenz

Rechteinhaber und Lizenztexte: `LICENSES/`. Der öffentliche Header `include/iec104/iec104.h` steht unter Apache-2.0 (`LICENSES/Apache-2.0.txt`), alle übrigen Dateien sind proprietär (`LICENSES/LicenseRef-Proprietary.txt`). Jede Quelldatei nennt ihre Lizenz im SPDX-Kopf.

## Stand

| Modul | Stand |
| --- | --- |
| `codec` | APCI (I/S/U), ASDU-Kopf, IOA, CP56Time2a, Informationselemente, Typtabelle (59 Typen), vollständige ASDUs mit SQ 0/1, Aufteilen, strikt/tolerant (`tasks/TASK-001_codec.md`) |
| `apci`, `session`, `transport` | offen |
| C-API | Version und Statuscodes |
