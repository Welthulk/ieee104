# Interop-Tests und Integrationstest

Stand: 25.11.2026 · Gegenstelle: lib60870-C v2.4.1, für TLS mit mbedTLS 3.6.8 · Skript: `tools/interop/run_lib60870.sh`

## 1. Zweck

Die Unit-Tests und der Selbsttest prüfen die Library gegen sich selbst. Fehler, die beide Seiten gleich machen, finden sie nicht. Deshalb läuft die Library zusätzlich gegen eine unabhängige Implementierung: lib60870-C. Sie dient nur als Gegenstelle. Das Skript startet deren unveränderte Beispielprogramme und spricht über TCP mit ihnen; nichts wird gelinkt, kein Code übernommen.

## 2. Aufruf

lib60870 holen und bauen: README, Abschnitt „Gegen lib60870 testen“. Dann:

```bash
tools/interop/run_lib60870.sh ../lib60870/lib60870-C build/debug                   # Standard
tools/interop/run_lib60870.sh --mit-stationen ../lib60870/lib60870-C build/debug   # alle Fälle
```

| Lauf | Fälle | Lauscht | Feste Ports |
| --- | --- | --- | --- |
| Standard | 2, 6, 7, 8: lib60870 ist Leitstelle und verbindet sich mit den eigenen Servern | nur `127.0.0.1` | 19998 (TLS-Fälle) |
| `--mit-stationen` | zusätzlich 1, 3, 4, 5 mit `cs104_server`, `cs104_redundancy_server`, `tls_server` | diese drei Beispiele auf `0.0.0.0` | zusätzlich 2404 |

Die Stations-Beispiele von lib60870 binden fest an `0.0.0.0`, sind also während des Laufs über alle Netzwerkschnittstellen erreichbar; ohne Änderung an lib60870 geht das nicht anders. Deshalb laufen sie nur mit `--mit-stationen`.

| Ergebnis | Bedeutung |
| --- | --- |
| bestanden | Verhalten wie erwartet |
| Abweichung Gegenstelle | bekannter Fehler eines Beispielprogramms (Abschnitt 4); eigenes Verhalten korrekt |
| FEHLER | eigenes Verhalten falsch; Rückgabe 1, Log unter `build/debug/interop/` |
| übersprungen | lib60870 ohne TLS gebaut, oder Fall mit lib60870-Station ohne `--mit-stationen` |

In `ctest`: `cmake --preset debug -DIEC104_LIB60870_DIR=../lib60870/lib60870-C`, Test `Interop.Lib60870` (Standardlauf). Im Preset `shared` zusätzlich `Julia.GuiLib60870` (Abschnitt 6).

## 3. Fälle und Ergebnis

| Nr. | Fall | Gegenstelle | Lauf | Geprüft | Ergebnis |
| --- | --- | --- | --- | --- | --- |
| 1 | Client → Server | `cs104_server` | `--mit-stationen` | STARTDT, Generalabfrage, Uhrzeit, Zählerabfrage (Ursache 44), TESTFR, STOPDT | bestanden |
| 2 | Server ← Client | `simple_client` gegen `hello_server` | Standard | STARTDT con, Ende der Initialisierung, Generalabfrage mit ACTCON/ACTTERM, Testbefehl 107 | bestanden |
| 3 | Umschaltung | `cs104_redundancy_server` | `--mit-stationen` | zwei Verbindungen, STOPDT/STARTDT, die gepufferte Reihe (IOA 110) läuft lückenlos weiter | bestanden; Abweichung Gegenstelle (ACTTERM) |
| 4 | TLS 1.3 Client → Server | `tls_server` | `--mit-stationen` | Handshake mit gegenseitiger Authentifizierung, Uhrzeit, TESTFR | bestanden; Abweichung Gegenstelle (ACTTERM) |
| 5 | TLS 1.2 Client → Server | `tls_server` | `--mit-stationen` | wie 4, dazu Renegotiation durch die Gegenstelle alle 2 s | bestanden (5 Renegotiations in 7 s); Abweichung Gegenstelle (ACTTERM) |
| 6 | TLS Server ← Client | `tls_client` | Standard | zwei Verbindungen nacheinander, Generalabfrage, Einzelbefehl, Uhrzeit | bestanden |
| 7 | TLS mit EC-Schlüsseln | `tls_client` | Standard | P-256-Zertifikate | Abweichung Gegenstelle (EC abgelehnt) |
| 8 | Abgelaufenes Zertifikat | `tls_client` mit seinen Original-Zertifikaten | Standard | Client-Zertifikat seit 2022 abgelaufen | bestanden: abgewiesen |

## 4. Befunde

| Befund | Wirkung | Folgerung |
| --- | --- | --- |
| lib60870 lehnt EC-Schlüssel (P-256) ab: „insufficient key length“ | kein TLS mit EC-Zertifikaten | Für Gegenstellen dieser Art RSA-Zertifikate (≥ 2048, besser 3072 Bit) vorsehen; Test-PKI: `tools/make_test_certs.sh <ordner> rsa` |
| Die Beispiele `tls_server` und `cs104_redundancy_server` senden die Beendigung der Generalabfrage mit Typkennung 0 und ohne Objekte (`68 0a … 00 00 0a 00 01 00`) | der Client erkennt kein ACTTERM; Selbsttest meldet „no termination (ACTTERM type 100)“ | Fehler der Beispiele, nicht der lib60870-Bibliothek (`cs104_server` sendet korrekt). Der strikte Codec weist die ASDU ab, statt sie falsch zu deuten; Mitschnitt in `tests/data/lib60870_tls_capture.txt`, Test `Capture.ForeignTlsServer` |
| Das Client-Zertifikat der lib60870-Beispiele ist seit April 2022 abgelaufen | Verbindung zur eigenen Library wird abgewiesen | gewollt; zeigt die Zeitprüfung |
| `tls_server` verhandelt TLS 1.2 alle 2 s neu | — | Renegotiation (RFC 5746) durch die Gegenstelle funktioniert; im Log als `key renewal: renegotiation complete` |
| `simple_client` sendet in v2.4.1 keine Befehle (Code mit `#if 0` abgeschaltet) | Befehlsrichtung nur über `tls_client` geprüft | — |
| lib60870 meldet „CRL not accessible“ | nur Warnung der Gegenstelle | ohne Sperrliste dort keine Sperrprüfung |

## 5. Integrationstest mit einer Gegenstelle

### 5.1 Vorher klären

Alles Folgende steht in der Interoperabilitätsliste der Gegenstelle (Abschnitt 9 der Norm). Abweichungen werden Parameter, nie Code.

| Punkt | Frage | Parameter |
| --- | --- | --- |
| Rolle | Wer ist Server, wer Client? Mehrere Verbindungen? | Server oder Client |
| Adressen | IP-Adressen, Port (2404, mit TLS 19998), gemeinsame Adresse (CA) | `host`, `port`, `--ca` |
| Timer, Fenster | t0–t3, k, w | `t0` … `t3`, `k`, `w` |
| Feldlängen | COT 2, CA 2, IOA 3? Originator-Adresse? | `cot_length`, `ca_length`, `ioa_length`, `originator` |
| Typen | welche Typkennungen je Richtung, mit oder ohne Zeitmarke | Prozessabbild der Anwendung |
| Befehle | direkt oder Select/Execute, ACTTERM erwartet, maximale Befehlsverzögerung | `iec104_server_add_command`, `max_command_delay`, `expect_termination` |
| Zeit | UTC oder Ortszeit, SU-Bit, Uhrzeitsynchronisation von wem | Anwendung (docs/05, N13) |
| Redundanz | Anzahl Verbindungen, Umschaltung per STOPDT/STARTDT, Generalabfrage danach, Puffergröße | `group`, `event_buffer_size`, `overflow` |
| TLS | ja/nein, Version, Suites, CA, Schlüsseltyp (RSA/EC), Sperrlisten, Renegotiation | `tls_*` |
| Abweichungen | bekannte Eigenheiten (lange Timer, t3 = 0, Typen mit CP24Time2a) | `tolerant` |

### 5.2 Ablauf

1. Werte aus 5.1 in eine Konfigurationsdatei übernehmen: Vorlage `tools/interop/gegenstelle.conf`. Daraus die eigene Interoperabilitätsliste erzeugen und neben die der Gegenstelle legen: `iec104_interop_list --config gegenstelle.conf --role station --format csv > eigene_liste.csv` (Rolle `master` für die Leitstelle); Abweichungen klären, bevor getestet wird.
2. Verbindung und Abläufe mit den Selbsttest-Programmen prüfen, bevor die Anwendung beteiligt ist:

   | Gegenstelle ist | Aufruf |
   | --- | --- |
   | Server | `iec104_selftest_client --config gegenstelle.conf --foreign --ca <CA> --idle 60 --log trace --stats > lauf1.log` |
   | Client | `iec104_selftest_server --config gegenstelle.conf --log trace --stats --verbose > lauf1.log` |
   | Client mit Redundanz | zusätzlich `--group <Adressen> --broadcast`; an der Gegenstelle umschalten |

3. Dann die Anwendung mit derselben Konfigurationsdatei.
4. Je Lauf aufbewahren: Konfigurationsdatei, Log mit `--log trace` (jede APDU als Hexdump), bei TLS die Sicherheitsereignisse, Version (`iec104_version_string()`). Ein Mitschnitt mit Wireshark auf Port 2404 ergänzt das Log (docs/04, Abschnitt 4.8; mit TLS nur verschlüsselt).

### 5.3 Typische Fehlerbilder

| Beobachtung | Ursache meist | Prüfen |
| --- | --- | --- |
| `closed: t1 timeout` kurz nach STARTDT | Gegenstelle quittiert spät, t1 zu kurz | Timer der Gegenstelle (oft t1 = 30 s oder mehr) |
| `closed: unexpected frame` | Gegenstelle sendet I-Rahmen vor STARTDT oder mit anderer Rolle | Rolle, `tolerant` |
| negative Antworten Ursache 44–47 | Typ, Ursache, CA oder IOA unbekannt | Typenliste, CA, Prozessabbild |
| Befehl ohne Ergebnis `NO_TERMINATION` | Gegenstelle sendet kein ACTTERM | `expect_termination = 0` |
| TLS: `peer certificate rejected` | CA, Ablaufdatum, Sperrliste, Name | Zertifikat mit `openssl x509 -noout -text -in …` |
| TLS: Handshake bricht ohne Grund ab | Schlüsseltyp oder Suite nicht unterstützt | RSA statt EC, `tls_ciphers` |

## 6. Interaktiv in der Test-GUI

Die Test-GUI (`docs/07_Julia.md`, Abschnitt 7.1) startet auf Knopfdruck ein Leitstellen-Beispiel von lib60870 gegen ihre eigene Station auf `127.0.0.1`. Datenpunkte, Werte und die Annahme von Befehlen werden in der GUI eingestellt; die Ausgabe von lib60870 steht unter Ereignisse neben den Meldungen der Station. Den Ordner bekommt die GUI beim Start:

```bash
bash gui.sh 8080 127.0.0.1 ../lib60870/lib60870-C                              # Binärpaket
julia examples/julia/gui/iec104_gui.jl 8080 127.0.0.1 ../lib60870/lib60870-C   # Quellbaum
```

| Programm | Prüft an der eigenen Station |
| --- | --- |
| `simple_client` | Verbindungsaufbau, STARTDT, Ende der Initialisierung, Generalabfrage mit ACTCON, Werten und ACTTERM, Testbefehl 107 |
| `cs104_client_async` | Generalabfrage sofort und alle 10 s, spontane Werte nach „Anlegen / senden“ |
| `cs104_client_no_threads` | Dauerverbindung mit Testrahmen (t3 = 2 s), spontane Werte |

Nur die Leitstellen-Beispiele sind wählbar, weil die Stations-Beispiele auf `0.0.0.0` lauschen. Automatisch: `tests/gui/test_gui.sh`, in `ctest` (Preset `shared`, `-DIEC104_LIB60870_DIR=…`) als `Julia.GuiLib60870`.
