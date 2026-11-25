# TASK-009: Interop-Tests gegen lib60870, Vorbereitung Integrationstest

Status: erledigt (25.11.2026) · Module: `tools/interop`, `tools/selftest`, `src/transport`, `tests`, Doku

## Ziel

Die Library läuft gegen eine unabhängige Implementierung (lib60870-C v2.4.1, nur als Gegenstelle). Der Integrationstest mit der Gegenstelle des Auftraggebers ist vorbereitet.

## Definition of Done

- [x] Skript `tools/interop/run_lib60870.sh`: 8 Fälle (TCP beide Richtungen, Umschaltung, TLS 1.2/1.3 beide Richtungen, EC-Schlüssel, abgelaufenes Zertifikat), Bewertung bestanden / Abweichung Gegenstelle / FEHLER; optional in ctest (`IEC104_LIB60870_DIR`).
- [x] Mitschnitt als Referenzdaten: `tests/data/lib60870_tls_capture.txt`, Test `Capture.ForeignTlsServer`.
- [x] Befunde dokumentiert (`docs/08_Interop.md`): EC-Schlüssel abgelehnt, ACTTERM mit Typ 0 in zwei Beispielen, abgelaufene Beispielzertifikate.
- [x] Vorbereitung Integrationstest: Checkliste, Ablauf, Fehlerbilder (docs/08), Vorlage `tools/interop/gegenstelle.conf`.
- [x] Nebenbei: Renegotiation durch die Gegenstelle im Log sichtbar (`renegotiation complete`); Selbsttest-Client `--switchover` auch gegen fremde Server (`--series-ioa`); Meldung bei fehlendem ACTTERM; Test-PKI auch mit RSA.

## Umsetzung

| Datei | Inhalt |
| --- | --- |
| `tools/interop/run_lib60870.sh` | Interop-Lauf, Ports prüfen, Gegenstellen über PID beenden |
| `tools/interop/gegenstelle.conf` | Konfigurationsvorlage |
| `tools/make_test_certs.sh` | Schlüsseltyp `rsa` (3072 Bit) |
| `src/transport/tls_openssl.cpp`, `link.cpp` | Zähler abgeschlossener Handshakes (TLS 1.2), Ereignis bei Renegotiation |
| `tools/selftest/selftest_client.cpp` | `--series-ioa`, Umschaltung mit `--foreign`, Meldung fehlender Bestätigungen |
| `tests/test_capture.cpp` | zweite Referenzdatei, Abweichungen der Gegenstelle |
| `README.md`, `docs/08_Interop.md` | Anleitung, Ergebnisse, Feature-Liste, Tests |

## Offen

| Nr. | Thema | Stand |
| --- | --- | --- |
| O1 | Befehlsrichtung ohne TLS gegen lib60870 | Beispiel-Client sendet keine Befehle; nur über TLS geprüft |
| O2 | Interop mit der Gegenstelle des Auftraggebers | vorbereitet, Termin offen |
