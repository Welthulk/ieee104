# CLAUDE.md – Regeln für Claude Code in diesem Repo

IEC-60870-5-104-Library für die SpPS (SOPTIM). Kern C++17, stabile C-API, TLS über externe Library.

## Verbindliche Unterlagen

- `docs/02_Konzept_IEC104_Library.md` ist maßgeblich (Sprache, Architektur, Parameter, Tests).
- `docs/03_Recherchebericht_IEC104_im_Detail.md` liefert Belege und Werte. Seine Rust-Aussagen (rustls, `Strictness::…`, cargo-fuzz) sind überholt; Sprache ist C++17, TLS-Backend OpenSSL 3.x.
- `docs/01_IEC104_verstehen.md` ist die Einführung.
- Aufgaben stehen in `tasks/`. Immer nur eine Aufgabe bearbeiten, Definition of Done abhaken.

## Build und Test

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan    # vor jedem Commit
cmake --preset clang && cmake --build --preset clang                           # clang-tidy
cmake --preset fuzz  && cmake --build --preset fuzz
./build/fuzz/fuzz_codec -max_total_time=300
```

Zielplattform: AlmaLinux 9, GCC 11.5, Clang 22, CMake 3.31, OpenSSL 3.5, GoogleTest aus EPEL.
Werkzeuge installiert `tools/setup_tools.sh`.

## Architektur (Schichten ohne Abhängigkeit nach oben)

| Schicht | Ordner | Regel |
| --- | --- | --- |
| codec | `src/codec` | reine Funktionen auf Byte-Puffern, kein I/O, keine Allokation |
| apci | `src/apci` | Zustandsmaschine, sans-I/O: Bytes + Zeit rein, Bytes + Ereignisse raus |
| session | `src/session` | GA, Uhrzeit, Befehle, Pufferung, Redundanzgruppen |
| transport | `src/transport` | TCP/TLS, Event-Loop, austauschbares Backend |
| API | `include/iec104/iec104.h`, `src/api` | C-Schnittstelle |

## Code-Regeln

- C++17, keine Exceptions, kein RTTI im Library-Code (`-fno-exceptions -fno-rtti`). Fehler als `iec104_status`.
- Öffentliche Header: nur C, keine STL-Typen, alles mit `IEC104_API` exportiert.
- Normverhalten ist Default. Jede bekannte Abweichung einer Gegenstelle wird ein Parameter, nie ein Code-Zweig für einen Hersteller.
- Keine GPL-Abhängigkeit im Produkt. lib60870 nur als externe Testgegenstelle, nie linken, nie Code übernehmen.
- Werte, die aus der Norm stammen und im Repo nicht belegt sind, nicht raten. Stattdessen `// TODO(norm): …` mit Abschnittsangabe und in der Aufgabe als offen melden.
- Bezeichner und Code-Kommentare auf Englisch, Dokumentation unter `docs/` auf Deutsch.
- Formatierung mit `.clang-format`, keine Warnungen (Library baut mit `-Werror`).
- Jede neue Funktion bekommt Tests, möglichst mit Referenz-Hexdumps und Quelle im Kommentar.

## Git

- Kleine Commits, Nachricht auf Deutsch, Präfix nach Modul (`codec: …`, `apci: …`, `build: …`).
- Nicht pushen, ohne zu fragen.
