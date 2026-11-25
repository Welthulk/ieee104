# IEC 60870-5-104: Einführung, Recherche, Konzept und Library

Eigene IEC-104-Library (C++17-Kern, C-API, TLS über OpenSSL) für die SpPS und die Anbindung an die Prozessabkopplung (Aucoteam). Stand: 25.11.2026.

## Unterlagen

| Datei | Inhalt |
| --- | --- |
| `docs/01_IEC104_verstehen.md` | Einführung ins Protokoll, offene Fragen an Aucoteam |
| `docs/02_Konzept_IEC104_Library.md` | Konzept: Sprache, Architektur, Parameter, TLS, Laufzeit, Tests, offene Entscheidungen |
| `docs/03_Recherchebericht_IEC104_im_Detail.md` | Recherche: Norm, Praxis bei Netzbetreibern, Interop-Fallstricke, Sicherheit, Tests (mit Quellen) |
| `folien/Konzept_IEC104_Library_Folien.html` | Folien zum Konzept, im Browser öffnen (10 Folien, 1920×1080) |
| `CLAUDE.md` | Regeln für Claude Code |
| `tasks/` | Aufgaben, eine nach der anderen |

## Bauen

Werkzeuge einmalig installieren: `./tools/setup_tools.sh`

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan
cmake --preset fuzz  && cmake --build --preset fuzz  && ./build/fuzz/fuzz_codec -max_total_time=300
```

## Stand

| Modul | Stand |
| --- | --- |
| `codec` | APCI (I/S/U), ASDU-Kopf, IOA, CP56Time2a mit Tests. Informationselemente und ASDUs: `tasks/TASK-001_codec.md` |
| `apci`, `session`, `transport` | offen |
| C-API | Version und Statuscodes |
