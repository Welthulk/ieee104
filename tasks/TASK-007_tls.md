# TASK-007: TLS über OpenSSL 3.x

Status: erledigt (25.11.2026) · Module: `src/transport`, `src/api`, CMake, `tools/selftest`

## Ziel

Server und Client sprechen IEC 104 über TLS nach dem Profil von IEC 62351-3 Ed. 2, soweit es öffentlich belegt ist. TLS kommt aus OpenSSL 3.x hinter einer schmalen Backend-Schnittstelle.

## Umfang

| Nr. | Punkt | Ergebnis |
| --- | --- | --- |
| 1 | Backend-Schnittstelle | `transport/tls.hpp`: `TlsContext`, `TlsSession`; Backend OpenSSL, ohne OpenSSL ein Stub (`IEC104_TLS=OFF`) |
| 2 | Gegenseitige Authentifizierung | CA-Datei, eigenes Zertifikat mit Kette, Schlüssel (auch mit Passwort), Prüfung Name oder SHA-256-Fingerprint der Gegenstelle |
| 3 | Sperrlisten | CRL-Datei, Prüfung der ganzen Kette |
| 4 | Versionen, Suites | TLS 1.2/1.3 einzeln wählbar, Liste für 1.2 und 1.3 je Handle |
| 5 | Schlüsselerneuerung | Key Update (1.3) bzw. Renegotiation (1.2) nach Zeit; Renegotiation abschaltbar |
| 6 | Session Resumption | an/aus, Lebensdauer |
| 7 | Zertifikatswechsel | `*_set_tls()` jederzeit, gilt für neue Verbindungen |
| 8 | Sicherheitsereignisse | Callback `on_security` + Log; Schließgrund `IEC104_CLOSE_TLS` |
| 9 | Handshake-Zeit | TLS-Handshake fällt unter t0 (Client und Server) |
| 10 | Konfigurationsdatei, Selbsttest | Schlüssel `tls_*`, Optionen `--tls*`, ctest über TLS |

## Definition of Done

- [x] Tests: Handshake 1.2/1.3, Negativtests (fremde CA, abgelaufen, gesperrt, Name, Fingerprint, ohne Client-Zertifikat, Klartext gegen TLS, Handshake-Timeout), Erneuerung, Resumption, Zertifikatswechsel.
- [x] Gegenprobe mit `openssl s_client`/`s_server` (STARTDT über TLS 1.3, Renegotiation 1.2 im Mitschnitt, gegenseitige Authentifizierung).
- [x] `debug`, `asan` grün, `clang` ohne Befunde, Fuzzer grün; Build mit `IEC104_TLS=OFF` grün.
- [x] Doku (02, 04, 05, 06), README, CHANGELOG, Weitergabe-Zip und fertig gebaute Library.

## Umsetzung

| Datei | Inhalt |
| --- | --- |
| `src/transport/tls.hpp`, `tls.cpp` | Backend-Schnittstelle, Fingerprint-Normalisierung |
| `src/transport/tls_openssl.cpp` | OpenSSL-Backend: eigenes Socket-BIO (kein SIGPIPE), Profil, CRL, Name, Fingerprint, Erneuerung, Resumption |
| `src/transport/tls_none.cpp` | Stub für `IEC104_TLS=OFF` |
| `src/transport/link.*` | Phasen Verbindungsaufbau/Handshake/offen, t0 für den Handshake, Sicherheitsereignisse |
| `src/api/*` | `iec104_tls_config`, `*_set_tls`, `*_get_tls_info`, `on_security`, `IEC104_CLOSE_TLS`, Konfigurationsschlüssel |
| `tools/make_test_certs.sh` | Test-PKI zur Bauzeit |
| `tools/make_package.sh` | Binärpaket mit Prüfung von Zeichenketten und Symbolversionen |
| Tests | `test_tls.cpp` (14), `ConfigFile.TlsKeys`, `SelfTest.ServerAndClientOverTls`; 146 gesamt |

## Offen

| Nr. | Thema | Stand |
| --- | --- | --- |
| O1 | Normtext IEC 62351-3 Ed. 2 | Defaults (N12) sind Annahmen, `TODO(norm)` in `tls_openssl.cpp` |
| O2 | OCSP, OCSP-Stapling | nicht umgesetzt, nur CRL |
| O3 | Schlüsselerneuerung nach Datenmenge | nur nach Zeit |
| O4 | Warnung vor Ablauf des eigenen Zertifikats | nicht umgesetzt |
| O5 | Interop mit lib60870 über TLS | nicht geprüft (lib60870 braucht dafür mbedTLS) |
