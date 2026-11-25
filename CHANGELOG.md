# Änderungen

## In Arbeit (nächste Lieferung)

| Bereich | Änderung |
| --- | --- |
| Interop | `tools/interop/run_lib60870.sh`: 8 Fälle gegen lib60870 mit und ohne TLS; Ergebnisse und Befunde in `docs/08_Interop.md` |
| Interop | Vorbereitung Integrationstest: Checkliste, Ablauf, Fehlerbilder, Vorlage `tools/interop/gegenstelle.conf` |
| TLS | Renegotiation durch die Gegenstelle wird gemeldet (`key renewal: renegotiation complete`) |
| Selbsttest | Client `--series-ioa`; `--switchover` auch gegen fremde Server; klare Meldung bei fehlendem ACTCON/ACTTERM |
| Tests | Referenzmitschnitt über TLS (`Capture.ForeignTlsServer`); Test-PKI auch mit RSA-Schlüsseln |
| Doku | README mit Schnellstart ohne Bauen, Feature-Liste, Tests, Test gegen lib60870 |

## 0.4.0 (25.11.2026)

| Bereich | Änderung |
| --- | --- |
| Redundanz | Redundanzgruppen (`iec104_server_add_group`): eine aktive Verbindung je Gruppe, Umschaltung per STOPDT/STARTDT, Übernahme durch STARTDT auf einer zweiten Verbindung |
| Pufferung | Ereignispuffer je Gruppe: Ereignis bleibt bis zur Quittung, nach Abbruch oder Umschaltung erneut gesendet; Überlauf: älteste verwerfen oder ablehnen |
| Pufferung | ohne Gruppen: jede Verbindung puffert während STOPDT (`event_buffer_size`, `overflow` in `iec104_server_options`) |
| Zuordnung | neue Verbindungen nach IP-Adresse der Leitstelle; unbekannte Adressen werden abgewiesen |
| Betrieb | `iec104_server_get_group_stats`, `iec104_server_connection_group`; Log bei Übernahme und Überlauf |
| Konfiguration | Schlüssel `group`, `event_buffer_size`, `overflow` |
| Selbsttest | Server `--group`, `--buffer`, `--overflow`, `--broadcast`; Client `--switchover`; ctest `SelfTest.SwitchOver` |
| Festlegung | E10: Redundanzgruppen |
| Lieferung | fertig gebaute Library auf das Nötigste reduziert: `lib/libiec104.so.0`, Header, Lizenzen, Julia-Anbindung, Kurzanleitung |
| Julia | `examples/julia/IEC104.jl` (Anbindung per `ccall`), Client- und Server-Beispiel; Anleitung `docs/07_Julia.md` |

## 0.3.0 (25.11.2026)

| Bereich | Änderung |
| --- | --- |
| TLS | TLS 1.2/1.3 nach IEC 62351-3 mit OpenSSL 3.x: `iec104_tls_config`, `iec104_server_set_tls`, `iec104_client_set_tls` |
| TLS | gegenseitige Authentifizierung, Sperrliste, Name oder SHA-256-Fingerprint der Gegenstelle, Schlüssel mit Passwort |
| TLS | Key Update (1.3) und Renegotiation (1.2) nach Zeit, Session Resumption, Zertifikatswechsel zur Laufzeit |
| TLS | Callback `on_security`, Schließgrund `IEC104_CLOSE_TLS`, `iec104_*_get_tls_info` |
| Konfiguration | Schlüssel `tls`, `tls_*`; relative Pfade ab dem Verzeichnis der Datei |
| Selbsttest | Optionen `--tls*`; ctest auch über TLS; Test-PKI `tools/make_test_certs.sh` |
| Build | Option `IEC104_TLS` (Default an); `find_package(iec104)` zieht OpenSSL nach |
| Lieferung | zusätzlich fertig gebaute Library `iec104-<version>-linux-x86_64.zip` |
| Festlegung | E9: TLS-Handshake fällt unter t0 |

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
