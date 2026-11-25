# TASK-010: Doxygen, Interoperabilitätsliste

Status: erledigt (25.11.2026) · Module: `include`, `cmake`, `tools/interop_list`, `src/api`, `src/session`, Doku

## Ziel

API-Doku aus dem Header ohne Warnungen; die eigene Interoperabilitätsliste (Abschnitt 9) lässt sich für jede Konfiguration ausgeben und mit der Liste einer Gegenstelle abgleichen.

## Vorgehen

Recherche-Workflow (4 Agenten): Gliederung der Checkliste aus öffentlichen Wiedergaben, Bestandsaufnahme der Library mit Fundstellen, Gegenprüfung durch einen zweiten Agenten (51 bestätigt, 7 korrigiert), Doxygen-Probelauf.

## Definition of Done

- [x] Doxyfile (`cmake/Doxyfile.in`), Ziel `doc`, Test `Doc.NoDoxygenWarnings`; Header ohne Warnung; zerstörte Tabelle neu und gegen clang-format geschützt.
- [x] `iec104_interop_list` (Rolle, Konfiguration, Markdown/CSV), Daten in `tools/interop_list/interop_data.hpp`, Tests gegen Typtabelle und Werte-API, `docs/09` mit Aktualitätstest.
- [x] Befunde der Bestandsaufnahme behoben: Lesebefehl mit Ursache 5 und `iec104_client_read()`, Station über Befehlspunkte bekannt, `cause_of_init` geprüft, veraltete Kommentare.
- [x] `debug`, `asan` grün, `clang` ohne Befunde, Fuzzer grün, Review-Workflow.

## Offen (aus der Bestandsaufnahme, bewusst nicht in diesem Schritt)

| Nr. | Thema |
| --- | --- |
| O1 | Matrix Typ/Ursache (N3) leer, `is_cause_allowed()` ungenutzt |
| O2 | Zähler-Freeze/Reset (Modus C/D) nur bestätigt, erreicht `on_command` nicht |
| O3 | Abbruch von Befehlen ohne Befehlspunkte wird ohne Anwendung positiv bestätigt |
| O4 | Nur das erste Objekt einer Befehls-ASDU wird ausgewertet, weitere still ignoriert |
| O5 | Rohes `iec104_server_send_asdu` umgeht Warteschlange und Gruppenpuffer |
| O6 | Generalabfrage und Zählerabfrage des Clients ohne Verfolgung/Timeout |
| O7 | Zeitbasis als Parameter (N13) fehlt; maximale Befehlsverzögerung rechnet in UTC |
| O8 | Leeres Prozessabbild: keine Stationsprüfung, Broadcast-GA ohne Antwort |
