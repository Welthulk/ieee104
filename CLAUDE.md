# CLAUDE.md – Regeln für Claude Code in diesem Repo

Exemplarische IEC-60870-5-104-Library: Client und Server, Kern C++17, stabile C-API, TLS über externe Library (OpenSSL 3.x).

## Verbindliche Unterlagen

- `tasks/TASK-000_Auftrag.md` ist der Gesamtauftrag und hat Vorrang vor den übrigen Aufgaben.
- `intern/02_Konzept_IEC104_Library.md` ist maßgeblich (Sprache, Architektur, Parameter, Tests).
- `intern/03_Recherchebericht_IEC104_im_Detail.md` liefert Belege und Werte. Seine Rust-Aussagen (rustls, `Strictness::…`, cargo-fuzz) sind überholt; Sprache ist C++17, TLS-Backend OpenSSL 3.x.
- `docs/01_IEC104_verstehen.md` ist die Einführung.
- `docs/02_Technik_und_Quellen.md` ist die namensfreie Fassung von Konzept und Recherche; Code zitiert nur diese.
- `docs/03_Quellen_APCI.md` belegt die Regeln der Zustandsmaschine aus öffentlichen Quellen (Normtexte liegen nicht vor, F4).
- `docs/05_Normfragen_und_Festlegungen.md` ist die weitergabefähige Fassung der offenen Normfragen (N1–N13) und Festlegungen (E1–E8). Code und Tests verweisen nur darauf, nie auf `intern/04_Analyse.md` oder `tasks/`.

## Weitergabe-Zip

- Lieferung: Version in `CMakeLists.txt` und `iec104.h` erhöhen, `docs/09_Interoperabilitaetsliste.md` neu erzeugen (`build/debug/iec104_interop_list > docs/09_Interoperabilitaetsliste.md`, Test `Doc.InteropListUpToDate`), `CHANGELOG.md` ergänzen, committen, dann `tools/make_package.sh`. Es erzeugt `dist/iec104-<version>.zip`, den Patch gegen die vorige Lieferung, die fertig gebaute Library `dist/iec104-<version>-linux-x86_64.zip` (nur `lib/libiec104.so.0`, Header, Lizenzen, `julia/`, Kurzanleitung aus `packaging/README-binary.md`; Release, TLS; Symbolversionen höchstens glibc 2.34 / OpenSSL 3.0.0, läuft auch unter WSL2) und setzt das Tag `lieferung-<version>`. Alle drei Dateien committen, mit Tag pushen (`git push origin lieferung-<version>`).
- `tools/make_package.sh` baut `dist/iec104-<version>.zip` aus dem letzten Commit, ohne `intern/`, `tasks/`, CLAUDE.md, `.vscode/` und interne Skripte, und prüft das Zip selbst (Begriffsliste, Verweise auf interne Unterlagen). Bei einem Treffer wird es gelöscht.
- Das Zip geht in ein firmeninternes Repo und an Kollegen für manuelle Tests. Keine Kunden-, Projekt- oder Herstellernamen, keine Hinweise auf KI-Werkzeuge, Aufgabennummern oder diesen Arbeitsablauf. Autor und Rechteinhaber stehen nur im SPDX-Kopf und in `LICENSES/`.
- `docs/` ist Weitergabe-Doku und wird von `check_neutral.sh` voll geprüft; Projektbezug, Recherche mit Kundennamen, Analyse und Folien liegen in `intern/`.
- Code und Tests verweisen nur auf `docs/`, nie auf `intern/` oder `tasks/`.

## Build und Test

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan    # vor jedem Commit
cmake --preset clang && cmake --build --preset clang                           # clang-tidy
cmake --preset fuzz  && cmake --build --preset fuzz
tools/make_fuzz_seeds.sh   # Startdateien aus tests/data/ nach fuzz/corpus
tools/make_package.sh      # Weitergabe-Zip nach dist/ (vorher committen)
./build/fuzz/fuzz_codec -max_total_time=300 fuzz/corpus
./build/fuzz/fuzz_apci  -max_total_time=300 fuzz/corpus_apci
./build/fuzz/fuzz_session -max_total_time=300 fuzz/corpus_session
```

Fuzz-Dauer: 5 min bei jeder Änderung am Codec, 10 min zum Abschluss einer Aufgabe, 24 h vor einer Auslieferung.

Zielplattform (Version 1 nur Linux x86-64): AlmaLinux 9, GCC 11.5, Clang 22, CMake 3.31, OpenSSL 3.5, GoogleTest aus EPEL.
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

- Exemplarische Library: Lesbarkeit und Nachvollziehbarkeit vor Performance.
- Kommentarstandard nach `tasks/TASK-000_Auftrag.md`, Abschnitt 5.
- Neutralitätsregel nach `tasks/TASK-000_Auftrag.md`, Abschnitt 4; `tools/check_neutral.sh` vor jedem Commit.
- Lizenzkopf: Jede neue Quelldatei (C/C++, CMake, Skripte) beginnt mit dem SPDX-Kopfblock wie in den vorhandenen Dateien. `include/iec104/iec104.h` ist `Apache-2.0`, alles andere `LicenseRef-Proprietary` (Texte in `LICENSES/`). Nur dort steht der Rechteinhaber, nirgends sonst.
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
- Vor jedem Commit: `tools/check_neutral.sh --message <Datei mit Commit-Nachricht>`.
- Keine Co-Author- oder Sitzungszeilen in Commit-Nachrichten (Neutralitätsregel).
- Nicht pushen, ohne zu fragen.
