# TASK-003: TCP-Transport, C-API und Selbsttest-Programme

Status: offen · Module: `src/transport`, `src/api`, `include/iec104/iec104.h`, `tools/selftest` · Grundlage: `docs/02_Konzept` (Laufzeitmodell, Konfiguration), `docs/04_Analyse.md` (F7, F10), TASK-002

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

- [ ] Server und Client laufen gegeneinander, Ports konfigurierbar, ctest über Loopback grün.
- [ ] Selbsttest-Programme mit Hilfe (`--help`), Ergebnisbericht und Exit-Code.
- [ ] Gegenprobe gegen lib60870 durchgeführt und dokumentiert.
- [ ] `debug`, `asan` grün, `clang`-Preset ohne Befunde, Fuzzer unverändert grün.
- [ ] Bedienung der Selbsttest-Programme im README beschrieben.

## Offen

- (wird beim Bearbeiten gefüllt)
