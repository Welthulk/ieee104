# TASK-008: Redundanzgruppen und Ereignispuffer

Status: erledigt (25.11.2026) · Module: `src/session`, `src/transport`, `src/api`, `tools/selftest`

## Ziel

Die gesteuerte Station bedient mehrere Verbindungen einer Leitstelle als Redundanzgruppe (Ed. 2, Abschnitt 10): eine Verbindung überträgt, die anderen stehen bereit. Ereignisse gehen bei Abbruch und Umschaltung nicht verloren.

## Umfang

| Nr. | Punkt | Ergebnis |
| --- | --- | --- |
| 1 | Gruppe | `session::RedundancyGroup`: Mitglieder, aktive Verbindung, Ereignispuffer |
| 2 | Pufferung | Ereignis bleibt bis zur Quittung (N(R)) im Puffer; bei Abbruch oder Umschaltung erneut senden |
| 3 | Überlauf | älteste verwerfen (Default) oder ablehnen; Zähler, Log |
| 4 | Umschaltung | STOPDT/STARTDT; STARTDT auf einer zweiten Verbindung übernimmt |
| 5 | Mehrere Gruppen | Zuordnung über IP-Adresse der Leitstelle; unbekannte Adressen abgewiesen |
| 6 | Ohne Gruppen | jede Verbindung eigene Gruppe, Puffer während STOPDT |
| 7 | C-API, Konfiguration, Selbsttest | `iec104_server_add_group`, Gruppenstatistik, Schlüssel, Optionen |

## Definition of Done

- [x] Unit-Tests der Gruppe (sans-I/O) und Tests über TCP: Umschaltung, Abbruch mit unquittierten Ereignissen, Überlauf, Zuordnung, Abweisung.
- [x] Selbsttest mit Umschaltung zwischen zwei Verbindungen (`SelfTest.SwitchOver`, Umschaltung mitten in einer Reihe von 300 Werten).
- [x] `debug`, `asan` grün, `clang` ohne Befunde, Fuzzer grün.
- [x] Doku (02, 04, 05, 06), README, CHANGELOG, Lieferung 0.4.0.

## Umsetzung

| Datei | Inhalt |
| --- | --- |
| `src/session/redundancy_group.*` | Gruppe: Mitglieder, aktive Verbindung, Ereignispuffer mit Zuständen Pending/Queued/InFlight, Überlauf |
| `src/session/server_session.*` | Ereignisse mit Kennung einreihen, zurücknehmen, Meldung nach dem Senden |
| `src/transport/link.*` | Quittungen (`Acknowledged`) an die API |
| `src/api/server.cpp` | Zuordnung nach Adresse, I-Rahmen je Verbindung mitschreiben, Quittungen auf Ereignisse abbilden, Umschaltung, Log |
| `src/api/config.cpp` | `group`, `event_buffer_size`, `overflow` |
| `tools/selftest/*` | `--group`, `--buffer`, `--overflow`, `--broadcast`, `--switchover` |
| Tests | `test_redundancy.cpp` (13), `ConfigFile.RedundancyGroups`, `SelfTest.SwitchOver`; 161 gesamt |

## Offen

| Nr. | Thema | Stand |
| --- | --- | --- |
| O1 | Normtext Abschnitt 10 | Regeln nach Sekundärquellen, `TODO(norm): clause 10` |
| O2 | Höchstzahl Verbindungen je Gruppe | keine Grenze |
| O3 | Puffer über einen Neustart der Anwendung | nur im Speicher |
| O4 | Callback bei Wechsel der aktiven Verbindung | nur Log und Statistik |
