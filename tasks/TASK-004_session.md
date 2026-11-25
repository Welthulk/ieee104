# TASK-004: Session-Schicht – Abläufe und Werte-API

Status: erledigt (25.11.2026) · Module: `src/session`, `src/api`, `include/iec104/iec104.h`, `tools/selftest` · Grundlage: `intern/02_Konzept_IEC104_Library.md` (Befehle und Abläufe, Zeit), `docs/05_Normfragen_und_Festlegungen.md` (N3, N6, N8, N10, N13)

## Ausgangslage

Verbindungen laufen über TCP (TASK-003), ASDUs gehen als Oktette über die C-API. Es fehlen die Anwendungsabläufe der Norm (Abschnitt 7) und eine C-Schnittstelle für Werte, damit Anwender keine ASDUs von Hand bauen müssen.

## Ziel

Server und Client beherrschen die Grundabläufe: Generalabfrage, Zählerabfrage, Uhrzeitsynchronisation, Lesebefehl, Testbefehl, Ende der Initialisierung, Befehle mit Bestätigung und Beendigung, negative Quittungen bei Unbekanntem. Anwender arbeiten mit Werten (`iec104_object`), nicht mit Bytes. Der Selbsttest deckt die Abläufe ab und lässt sich gegen fremde Geräte einsetzen.

## Umfang

1. **Werte-API** (`iec104.h`): `iec104_object` (IOA, Typ, Wert, Qualität, Zeitmarke, Befehlsqualifier), `iec104_time` (CP56Time2a), Kodieren/Dekodieren über den vorhandenen Codec.
2. **Server-Abbild**: Datenpunkte anlegen und aktualisieren (`iec104_server_update`), spontane Übertragung (COT 3) an gestartete Verbindungen, Zugehörigkeit zu Abfragegruppen 1–16.
3. **Server-Abläufe**: Generalabfrage (Station und Gruppen) mit ACTCON, Daten (COT 20–36), ACTTERM; Zählerabfrage (COT 37–41); Uhrzeit (Callback, ACTCON); Lesebefehl (COT 5); Testbefehl mit Zeitmarke (Spiegeln); Befehle an die Anwendung (Callback mit Annahme/Ablehnung → ACTCON positiv/negativ, ACTTERM konfigurierbar); unbekannte Typkennung → COT 44, unbekannte Ursache → 45, unbekannte gemeinsame Adresse → 46, unbekannte IOA → 47 (jeweils negativ); Ende der Initialisierung (Typ 70, COT 4) nach dem ersten STARTDT, abschaltbar.
4. **Client-Abläufe**: Generalabfrage, Zählerabfrage, Uhrzeit, Befehle senden; empfangene Daten als Werte-Callback; Bestätigungen und Beendigungen als Ergebnis-Callback.
5. **Selbsttest**: Server mit Abbild; Client prüft Generalabfrage, Uhrzeit, Zählerabfrage, Befehle mit ACTCON/ACTTERM, spontane Daten.
6. **Tests**: Abläufe ohne Netz (Session-Objekte direkt gekoppelt) und über Loopback; Gegenprobe gegen lib60870 (`simple_client` erhält Antworten auf GA und Testbefehl).

## Nicht Teil dieser Aufgabe

- Select/Execute mit Zeitüberwachung je IOA, Max-Delay für zeitgestempelte Befehle (TASK-005).
- Pufferung bei STOPDT und Redundanzgruppen (TASK-008), TLS (TASK-007).

## Definition of Done

- [x] Jeder Ablauf mit Quelle oder `TODO(norm)` im Kommentar.
- [x] Tests für jeden Ablauf, positive und negative Quittungen.
- [x] Selbsttest erweitert, `docs/04_Selbsttest.md` aktualisiert.
- [x] Gegenprobe mit lib60870 dokumentiert.
- [x] `debug`, `asan` grün, `clang`-Preset ohne Befunde, Fuzzer grün (`fuzz_session` 600 s, 61 Mio. Eingaben; `fuzz_codec`, `fuzz_apci` je 60 s).
- [x] Weitergabe-Zip neu gebaut und geprüft.

## Umsetzung (Stand 25.11.2026)

| Datei | Inhalt |
| --- | --- |
| `include/iec104/iec104.h` | `iec104_time`, `iec104_object` (Tabelle je Typ), `iec104_asdu_info`, `iec104_cause`, Qualitäts- und Flag-Bits; Callbacks `on_object`, `on_command`; ASDU-Profil und Herkunftsadresse in `iec104_link_config`; Server: Optionen, Abbild, `update`, `send`; Client: Befehl, Generalabfrage, Zählerabfrage, Uhrzeit; Zeitumrechnung |
| `src/api/values.hpp/.cpp` | Umrechnung C-Werte ↔ Codec, Bereichsprüfung, CP56Time2a ↔ `iec104_time`, Unix-Zeit |
| `src/session/process_image.*` | Datenpunkte je Station und IOA, Gruppen, Basistypen ohne Zeitmarke |
| `src/session/server_session.*` | Abläufe der gesteuerten Station mit geordneter Sendewarteschlange; Generalabfrage als schrittweiser Auftrag zwischen ACTCON und ACTTERM |
| `src/api/server.cpp`, `client.cpp`, `link_common.*` | Session je Verbindung, Werte-Callbacks, neue C-Funktionen |
| `tools/selftest/` | Selbsttest über alle Abläufe; Client mit `--foreign` für fremde Server; Programme nutzen nur die öffentliche C-API |
| `tests/test_session.cpp`, `test_values.cpp` | 11 + 4 Tests: Abläufe, negative Antworten, volles Fenster, Werte, Zeit |
| `fuzz/fuzz_session.cpp` | Fuzzer für empfangene Anfragen an die Session |
| `docs/04_Selbsttest.md`, README | Anleitung und Beispiel aktualisiert |

Gegenprobe mit lib60870-C 2.4.1 (nur externe Gegenstelle):

| Richtung | Ergebnis |
| --- | --- |
| `iec104_selftest_client --foreign` gegen `cs104_server` | PASSED: Generalabfrage 14 Objekte, Uhrzeit bestätigt, Zählerabfrage normgerecht abgelehnt (Ursache 44, vom Beispielserver nicht unterstützt), Ruhephase, STOPDT |
| `simple_client` gegen `iec104_selftest_server` | Ende der Initialisierung, Messwerte, Generalabfrage mit ACTCON, 3 Punkten und ACTTERM, Bestätigung des Testbefehls mit Zeitmarke; Verbindung regulär beendet |

Entscheidungen:

- Antworten auf eine Generalabfrage verwenden die Typen ohne Zeitmarke (Abbild speichert Basistypen); spontane Meldungen gehen in dem Typ, den die Anwendung angibt.
- Antworten tragen die Herkunftsadresse der Anfrage; die Broadcast-Adresse wird für Generalabfrage, Zählerabfrage, Uhrzeit und Reset angenommen und je Station beantwortet.
- Ein leeres Abbild bedeutet: Die Anwendung verwaltet ihre Stationen selbst, es gibt keine Prüfung auf Ursache 46.
- Freeze/Reset der Zählerabfrage (FRZ 1–3) wird bestätigt, aber ohne gespeicherte Zählerstände beantwortet.

## Offen

| Nr. | Thema | Stand |
| --- | --- | --- |
| O1 | Typen der Antwort auf die Generalabfrage (mit oder ohne Zeitmarke), Abschnitt 7 | ohne Zeitmarke, `TODO(norm)` in `process_image.hpp` |
| O2 | Deaktivierung einer laufenden Generalabfrage | negative DEACTCON, `TODO(norm)` |
| O3 | Freeze/Reset bei der Zählerabfrage | nur bestätigt, `TODO(norm)` |
| O4 | Broadcast-Adresse je Typ (N10) | bei 100, 101, 103, 105 angenommen |
| O5 | Pufferung spontaner Daten bei STOPDT oder Verbindungsverlust | nicht gepuffert (TASK-008) |
| O6 | Select/Execute mit Zeitüberwachung, Max-Delay (N6) | TASK-005 |
