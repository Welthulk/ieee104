# TASK-006: Betrieb – Logging, Statistik, Konfigurationsdatei, Installation

Status: erledigt (25.11.2026) · Module: `src/api`, `src/apci`, `src/session`, `src/transport`, CMake, `tools/selftest`

## Ziel

Die Library ist im Betrieb beobachtbar und lässt sich wie jede andere CMake-Library einbinden.

## Umfang

| Nr. | Punkt | Ergebnis |
| --- | --- | --- |
| 1 | Logging | Log-Callback je Handle mit Stufe (Fehler, Warnung, Info, Debug, Trace); Meldungen für Verbindungen, Zustände, Plausibilitätswarnungen, negative Antworten, verworfene Befehle; Trace mit Hexdump jeder APDU |
| 2 | Statistik | Zähler je Verbindung: Rahmen I/S/U gesendet/empfangen, Oktette, ASDUs, Anfragen, negative Antworten, verworfene Befehle, Sendewarteschlange |
| 3 | Konfigurationsdatei | `iec104_config_load()`: Datei mit `schlüssel = wert` → Link-, Server- und Client-Parameter, Port, Adresse; Fehler mit Zeilennummer |
| 4 | Laufzeitmodell | Festlegung: Poll im Thread der Anwendung (`*_poll`), kein interner Thread |
| 5 | Installation | `cmake --install`, `find_package(iec104)`, Ziel `iec104::iec104`; Bau als statische oder geteilte Library |
| 6 | Selbsttest | Optionen `--config`, `--log`, `--stats` |

## Definition of Done

- [x] Tests für Konfigurationsdatei, Statistik und Logging.
- [x] Fremdes CMake-Projekt baut gegen die installierte Library (statisch und geteilt).
- [x] `debug`, `asan` grün, `clang` ohne Befunde, Fuzzer grün.
- [x] Doku und Weitergabe-Zip aktualisiert.

## Umsetzung

| Datei | Inhalt |
| --- | --- |
| `src/api/logger.*`, `link_common.*` | Log-Callback, Stufenfilter, Hexdump; Zustände, Schließgründe, Trace |
| `src/api/server.cpp`, `client.cpp` | Log, Statistik, Beobachter für negative Antworten und verworfene Befehle |
| `src/api/config.cpp` | `iec104_config_load()` |
| `src/apci/connection.*` | Zähler je Rahmenart, Trace-Haken |
| `src/session/server_session.*` | Zähler, `SessionObserver` |
| `src/transport/*` | Adresse der Gegenstelle, Trace-Weiterleitung |
| CMake, `cmake/iec104Config.cmake.in`, `CMakePresets.json` | Objektbibliothek, statisch/geteilt, Install/Export, Preset `shared` |
| `examples/`, `tools/check_install.sh` | Hello World, Test gegen installierte Library |
| `docs/06_Anleitung.md`, `CHANGELOG.md` | Anleitung, Änderungsliste |
| `tools/make_package.sh` | Version, Patch gegen vorige Lieferung, Tag `lieferung-<version>` |
| Tests | `test_operation.cpp` (4), `test_apci_connection.cpp` (+1), `Examples.HelloWorldInstalled`; 130 gesamt |

## Offen

| Nr. | Thema | Stand |
| --- | --- | --- |
| O1 | Installationstest mit Sanitizern | übersprungen (fremdes Projekt bräuchte die Sanitizer-Laufzeit) |
| O2 | Meldungen der Zustandsmaschine selbst (z. B. Plausibilität beim Empfang) | nur über Schließgrund |
