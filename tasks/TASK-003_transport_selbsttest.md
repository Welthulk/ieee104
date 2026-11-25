# TASK-003: TCP-Transport, C-API und Selbsttest-Programme

Status: erledigt (25.11.2026) · Module: `src/transport`, `src/api`, `include/iec104/iec104.h`, `tools/selftest` · Grundlage: `docs/02_Konzept` (Laufzeitmodell, Konfiguration), `docs/04_Analyse.md` (F7, F10), TASK-002

## Ausgangslage

Codec (TASK-001) und APCI-Zustandsmaschine (TASK-002) sind fertig und ohne Netz getestet. Es fehlt alles, was echte Bytes über TCP bewegt, und eine C-Schnittstelle, über die Programme die Library nutzen.

## Ziel

Zwei Programme, ein Server und ein Client, verbinden sich über TCP und tauschen Daten aus. Damit kann der Auftraggeber die Library selbst testen, bis der integrierte Test gegen seine Gegenstelle folgt (F10). Adresse und Ports sind konfigurierbar.

## Umfang

1. **Transport** (`src/transport`): TCP für Linux mit `poll`, nicht blockierende Sockets, Server mit mehreren Verbindungen, Client mit Verbindungsaufbau und t0, Event-Loop im Aufrufer-Thread (`iec104_*_poll(timeout)`), Monotonic-Uhr für die Zustandsmaschine. Port 0 = vom System gewählt, tatsächlicher Port abfragbar.
2. **C-API** (`iec104.h`, `src/api`): Handles für Server und Client, Konfigurations-Structs mit Normdefaults (Timer, k, w, strikt, ASDU-Profil, Adresse, Port), Callbacks für empfangene ASDUs und Zustandswechsel, ASDU senden, STARTDT/STOPDT, Schließen. Nur C-Typen, alles mit `IEC104_API`.
3. **Selbsttest-Programme** (`tools/selftest`): `iec104_selftest_server` und `iec104_selftest_client`. Optionen `--host`, `--port`, `--count`, `--interval`, `--duration`, Timer und k/w. Ablauf: STARTDT, spontane Messwerte (Server → Client), Werte in Gegenrichtung, Ruhe mit Testrahmen, STOPDT, Ergebnisbericht mit Zählern und Exit-Code 0/1.
4. **ctest**: Server und Client gegeneinander über Loopback mit Port 0.
5. **Gegenprobe**: Selbsttest-Client gegen den Beispielserver von lib60870 (externe Gegenstelle, nur Test, nicht gelinkt), dokumentiert.

## Nicht Teil dieser Aufgabe

- Session-Abläufe GA, Uhrzeit, Befehle (TASK-004/005); der Selbsttest wird dort erweitert.
- TLS (TASK-007), Redundanzgruppen (TASK-008), Logging und Statistik als Schnittstelle (TASK-006).

## Definition of Done

- [x] Server und Client laufen gegeneinander, Ports konfigurierbar, ctest über Loopback grün.
- [x] Selbsttest-Programme mit Hilfe (`--help`), Ergebnisbericht und Exit-Code.
- [x] Gegenprobe gegen lib60870 durchgeführt und dokumentiert.
- [x] `debug`, `asan` grün, `clang`-Preset ohne Befunde, Fuzzer unverändert grün.
- [x] Bedienung der Selbsttest-Programme im README beschrieben.

## Umsetzung (Stand 25.11.2026)

| Datei | Inhalt |
| --- | --- |
| `src/transport/tcp.hpp/.cpp` | nicht blockierende Sockets (IPv4/IPv6), listen/accept/connect, Monotonic-Uhr |
| `src/transport/link.hpp/.cpp` | Socket + Zustandsmaschine, Verbindungsaufbau mit t0, Schließgründe |
| `src/api/server.cpp`, `client.cpp`, `link_common.*` | C-API, gemeinsame Poll-Schleife |
| `include/iec104/iec104.h` | `iec104_link_config`, Zustände, Schließgründe, Callbacks, Server- und Client-Funktionen; Statuscodes `IEC104_ERR_IO`, `IEC104_ERR_CLOSED` |
| `tools/selftest/` | `iec104_selftest_server`, `iec104_selftest_client`, `run_selftest.sh` |
| `tests/test_tcp_loopback.cpp` | C-API über echtes TCP: Verbindung, 200 ASDUs, STOPDT, Schließen, Connect abgewiesen, Argumentprüfung |
| `docs/06_Selbsttest.md` | Anleitung für den Selbsttest und manuelle Tests |

Gegenprobe mit lib60870-C 2.4.1 (Beispielprogramme, nur als externe Gegenstelle):

| Richtung | Ergebnis |
| --- | --- |
| `iec104_selftest_client --expect 0 --count 0 --t3 2 --idle 6` gegen `cs104_server` | PASSED: Verbindung, STARTDT, Ruhephase, STOPDT; 4 periodische Messwerte der Gegenstelle empfangen |
| `simple_client` gegen `iec104_selftest_server --count 20 --interval 0` | alle 20 Messwerte empfangen, Verbindung vom Client regulär geschlossen; Generalabfrage und Testbefehl des Clients werden noch nicht beantwortet (Session, TASK-004) |

## Offen

| Nr. | Thema | Stand |
| --- | --- | --- |
| O1 | Öffentliche Funktionen, um ASDUs als Werte zu bauen und zu lesen | Selbsttest nutzt den internen Codec; C-Funktionen folgen mit der Session (TASK-004) |
| O2 | Client probiert nur die erste Adresse aus der Namensauflösung | ausreichend für Version 1 |
| O3 | Warnungen der Plausibilitätsprüfung (t2 < t1, w ≤ 2/3 k) werden über die C-API nicht gemeldet | mit Logging (TASK-006) |
