# Änderungen

## 0.2.0 (25.11.2026)

| Bereich | Änderung |
| --- | --- |
| Betrieb | Log-Callback je Handle (`iec104_*_set_log`), Stufen Fehler bis Trace; Trace zeigt jede APDU als Hexdump |
| Betrieb | Statistik je Verbindung (`iec104_*_get_stats`): Rahmen, Oktette, Anfragen, Befehle, negative Antworten |
| Betrieb | Konfigurationsdatei `key = value` (`iec104_config_load`) |
| Einbindung | `cmake --install`, `find_package(iec104)`, Ziel `iec104::iec104`; statisch oder geteilt (`BUILD_SHARED_LIBS`, Preset `shared`) |
| Beispiele | `examples/hello_server.c`, `examples/hello_client.c`; Test baut sie gegen die installierte Library |
| Selbsttest | Optionen `--config`, `--log`, `--stats` |
| Doku | neu `docs/06_Anleitung.md`; docs/02–05 gestrafft |
| Festlegung | E8: kein interner Thread, Poll im Thread der Anwendung |

## 0.1.0 (25.11.2026)

Erste Lieferung: Codec aller Typen, Zustandsmaschine der Verbindung, TCP, C-API, Abläufe der gesteuerten Station (Generalabfrage, Zählerabfrage, Uhrzeit, Befehle), Befehlsmodelle mit Select/Execute, maximale Befehlsverzögerung, Befehlsverfolgung am Client, Selbsttest-Programme.
