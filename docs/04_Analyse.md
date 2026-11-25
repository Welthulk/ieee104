# Analyse vor Umsetzungsbeginn (TASK-000)

Stand: 25.11.2026 · Grundlage: `docs/01`–`03`, Folien, Code-Stand `b6c670c`, `tasks/TASK-000`, `tasks/TASK-001`

Build-Stand auf der Zielplattform (AlmaLinux 9.9, GCC 11.5, Clang 22.1, CMake 3.31, OpenSSL 3.5): `debug` und `asan` mit 21/21 Tests grün, keine Warnungen, `clang`-Preset ohne clang-tidy-Befund, Fuzzer 120 s (227 Mio. Eingaben) ohne Befund. Für clang-tidy 22 waren drei kleine Anpassungen nötig (siehe Commit `b6c670c`).

## 1. Widersprüche zwischen den Unterlagen

| Nr. | Fundstelle | Widerspruch | Vorschlag |
| --- | --- | --- | --- |
| W1 | `03` Einleitung, „Konsequenzen“ (Zeilen Strikt/tolerant, Zeitbasis, TLS, Tests) | `03` plant eine Rust-Library (`Strictness::…`, `TimeBase::…`, Backend-Trait, cargo-fuzz). `02`, Folien, `CLAUDE.md`: C++17 mit C-API. | C++17 gilt. In `03` oben einen Hinweis „Sprachaussagen überholt, siehe `02`“ ergänzen. Rust-Begriffe im Code nicht übernehmen. |
| W2 | `03` Abschnitt Sicherheit und „Konsequenzen/TLS“ gegen `02` Abschnitt TLS | `03`: rustls als Backend, OpenSSL nur als Notlösung, „Neuaufbau statt Renegotiation“. `02`: OpenSSL 3.x als erstes Backend, mbedTLS als Alternative, Renegotiation konfigurierbar. | OpenSSL 3.x gilt. Die rustls-Lücke entfällt damit. Renegotiation (TLS 1.2) und Key Update (TLS 1.3) wie in `02` als Parameter. |
| W3 | `02` „Offene Entscheidungen“ | „Sprache bestätigen“ und „TLS-Backend bestätigen“ stehen noch offen, `CLAUDE.md` und TASK-000 legen beides fest. | In `02` als entschieden abhaken. |
| W4 | `02` Konfiguration, Zeile „Prüfung t2 < t1 und w ≤ 2/3 k“ gegen `03` Timer-Abschnitt | `02`: Default „Warnung“, wählbar Warnung/Fehler/aus. `03`: Warnung im toleranten, Fehler im strikten Modus. Da `02` den strikten Modus als Default setzt, ergibt `03` den Default „Fehler“. | Eigener Parameter wie in `02` (Default Warnung), unabhängig vom Schalter „strikt“. Entscheidung durch den Auftraggeber (Frage F3). |
| W5 | `02` „Strikter Modus: an“ gegen `03` „Konsequenzen/Strikt-tolerant“ | `03`: strikt für Konformitätstests, tolerant für den Feldbetrieb. `02`: Default strikt. | Default strikt (Normverhalten als Default, Leitprinzip in `02`). Tolerant bewusst einschalten. Frage F3. |
| W6 | `02` „t3 Bereich 1 s–48 h“ gegen `03` „t3 = 0 als aus optional“ | `02` kennt t3 = 0 nicht. Eine Gegenstelle im Recherchebericht schaltet die Überwachung mit 0 ab. | Bereich 0 und 1 s–48 h, 0 = aus, nur im toleranten Modus zulässig. |
| W7 | `02` Konfiguration gegen TASK-001 Punkt 4 | `02` hat keinen Parameter für die maximale ASDU-Länge. `03` und TASK-001 fordern ihn (Default 249). | In `02` unter „ASDU-Profil“ ergänzen: `max_asdu_length`, 1–249, Default 249. |
| W8 | `02` Testkonzept „Profiltests“ und Folie 9 gegen TASK-000 Abschnitt 4 | `02` will Testprofile nach Netzbetreibern benennen. Die Neutralitätsregel verbietet das. | Testprofile nach Eigenschaft benennen, z. B. `long_timeout_profile` (t1 250 s, t2 240 s, t3 255 s), `local_time_su_profile`, `fixed_offset_time_profile`, `select_before_operate_profile`, `time_tagged_commands_only_profile`, `no_actterm_setpoint_profile`, `large_redundancy_profile`. |
| W9 | `03` AMD1-Tabelle, Zeile „6“ | Die Liste „zeitgestempelte Befehle“ enthält 100, 101, 103 und 105. Diese Typen tragen laut derselben Typtabelle keine CP56Time2a-Zeitmarke. | Max-Delay vorerst nur auf Typen mit Zeitmarke anwenden (58–64, 107). `TODO(norm): AMD1 clause 6` (Normfrage N6). |
| W10 | `02` Testkonzept („GoogleTest oder Catch2“) gegen `CLAUDE.md`/CMake | Das Repo nutzt GoogleTest, `setup_tools.sh` installiert zusätzlich Catch2. | GoogleTest festschreiben, Catch2 aus dem Setup-Skript nehmen. |
| W11 | `02` Testkonzept (Fuzzing 24 h) gegen `CLAUDE.md` (300 s), TASK-001 (10 min) | Unterschiedliche Fuzz-Dauern. | Kein echter Widerspruch: kurz je Aufgabe, 24 h vor Auslieferung. In `CLAUDE.md` so beschreiben. |
| W12 | `IEC104_Repo_Setup.md` gegen Zielplattform | Der Auftrag nennt GCC 13 und Clang 18 als geprüft, Zielplattform ist GCC 11.5 und Clang 22. | Erledigt: Build auf der Zielplattform geprüft, clang-tidy-Befunde behoben. |

## 2. Offene Normfragen

Abschnittsnummern stammen aus `03`. Wo sie dort nicht belegt sind, steht „unbekannt“.

| Nr. | Thema (Abschnitt) | Modul | Auswirkung | Vorläufige Annahme im Code |
| --- | --- | --- | --- | --- |
| N1 | Zustandsverhalten APCI (104, Abschnitt 5) | apci | Kernlogik der Verbindung: wann I-Rahmen erlaubt sind, wann abgebrochen wird | Gängige Praxis aus `03`: keine I-Rahmen vor STARTDT act; STOPDT con erst nach Quittung aller eigenen I-Rahmen; TESTFR act auch im STOPDT-Zustand beantworten; N(R) außerhalb des Fensters oder N(S)-Sprung führt zum Abbruch. Jede Stelle `TODO(norm): clause 5`. |
| N2 | Bedeutung von t1 (Abschnitt 5, unbekannt) | apci | Ein Hersteller nutzt t1 nur für U-Rahmen | t1 überwacht I- und U-Testrahmen (Lesart `03`). Kein Parameter, solange keine Gegenstelle es braucht. |
| N3 | Matrix Typkennung/Übertragungsursache (9.5) | codec, session | Validierung eingehender ASDUs, negative Quittung | Leere Tabelle `TODO(norm): 9.5` (TASK-001). Bis dahin jede COT zulassen, COT 44–47 nach AMD1 nur für ihren Zweck. |
| N4 | CP24-Typen (2–19) in 104 | codec | Strikt ablehnen oder tolerant dekodieren | Strikt: `IEC104_ERR_UNSUPPORTED`. Tolerant: dekodieren (TASK-001). Beleg aus dem Normtext fehlt. |
| N5 | Typen 104 (Testbefehl ohne Zeit) und 106 in 104 | codec | TASK-001 nimmt 100–107 vollständig auf | Wie TASK-001 alle aufnehmen und als `TODO(norm)` markieren. Ob 104 in 104 durch 107 ersetzt wird, ist in den Unterlagen nicht belegt. |
| N6 | Max-Delay für Befehle (AMD1, Abschnitt 6) | session | Ein zu spät eintreffender Befehl wird still verworfen | Nur Typen mit CP56Time2a (58–64, 107), Default aus (siehe W9). |
| N7 | Redundanzgruppen (Abschnitt 10) | session | Verbindungen je Gruppe; was beim Umschalten mit unquittierten I-Rahmen geschieht | Ereignispuffer gehört der Gruppe; unquittierte Ereignisse bleiben beim Umschalten im Puffer und werden auf der neuen Verbindung erneut gesendet; danach GA durch den Client. `TODO(norm): clause 10`. |
| N8 | Abläufe GA, Befehle, Initialisierung (Abschnitt 7) | session | Reihenfolge ACTCON/Daten/ACTTERM, Typ 70 nach Neustart | Übliche Lesart aus `03`: Aktivierung, Bestätigung, Daten, Terminierung. Typ 70 mit COT 4 nach lokaler Initialisierung (Parameter). |
| N9 | VSQ mit Anzahl 0 | codec | Dekodieren einer ASDU ohne Objekte | Strikt: `IEC104_ERR_MALFORMED`, tolerant: annehmen. `TODO(norm): clause unknown`. |
| N10 | CA 0 und 65535 (Broadcast): bei welchen Typen erlaubt | session | Annahme von GA und Uhrzeit als Broadcast | CA 0 abweisen, 65535 nur bei Systembefehlen (100, 101, 103, 105) annehmen. `TODO(norm): clause unknown`. |
| N11 | Inhalt von COR1:2023 | alle | Unbekannte Korrekturen | Keine Annahme, nach Beschaffung prüfen. |
| N12 | Cipher-Suites (62351-3 Ed. 2, Tabelle 1), Renegotiation (7.4.4, 7.4.5, 7.5.2), Key Update (8.4) | transport (TLS) | Defaults der TLS-Konfiguration | TLS 1.3 bevorzugt; für TLS 1.2 nur ECDHE mit AEAD; sichere Renegotiation (RFC 5746) aktiv, Initiierung per Parameter; Liste je Verbindung überschreibbar. |
| N13 | Uhrzeit: UTC oder Ortszeit (nicht in der Norm festgelegt) | session | Bewertung von Zeitmarken, Max-Delay | Bekannt und gewollt: Zeitbasis als Parameter (UTC / fester Offset / Ortszeit mit SU). CP56Time2a im Codec ohne Zeitzone (umgesetzt). |

## 3. Abgleich vorhandener Code gegen Konzept

**Passt**

| Thema | Stand |
| --- | --- |
| Schichtung | `codec` ohne I/O und ohne Allokation, nur reine Funktionen auf Puffern |
| Fehlerbehandlung | `noexcept`, Rückgabe `iec104_status`, keine Exceptions, kein RTTI (`-fno-exceptions -fno-rtti`) |
| C-API | reiner C-Header, `IEC104_API`, Sichtbarkeit `hidden`, `extern "C"` |
| APCI | I/S/U-Rahmen, 15-Bit-Zähler, Länge 4–253, ein U-Bit pro Rahmen, reservierte Bits geprüft |
| ASDU-Kopf | Feldlängen als Profil (2/2/3 Default), P/N- und Test-Bit, ORG |
| CP56Time2a | ohne Zeitzone, mit IV, SU, Wochentag; strenges `validate()` getrennt vom toleranten Dekodieren |
| Build | Warnungen als Fehler, Presets für ASan/UBSan, clang-tidy, libFuzzer |

**Fehlt (laut Konzept, Reihenfolge siehe Abschnitt 4)**

| Thema | Wo geplant |
| --- | --- |
| Informationselemente, Typtabelle, vollständige ASDUs, Aufteilen nach `max_asdu_length`, Schalter strikt/tolerant | TASK-001 |
| Zustandsmaschine, Timer, k/w, injizierbare Uhr | TASK-002 |
| Konfigurationsobjekt (Timer, Profil, Zeitbasis, Plausibilitätsprüfung) | TASK-002/003 |
| Session, Transport, TLS, Redundanz, Logging, Statistik | TASK-003 ff. |
| `install(EXPORT …)` für `find_package(iec104)` | TASK-005 (das Ziel `iec104Targets` ist angelegt, wird aber nicht installiert) |
| Startkorpus für den Fuzzer | TASK-001 |

**Anzupassen**

| Nr. | Stelle | Befund | Maßnahme |
| --- | --- | --- | --- |
| A1 | alle Dateien in `src/`, `include/`, `tests/`, `fuzz/` | Kommentarstandard (TASK-000, Abschnitt 5) nicht erfüllt: keine `@file`-Kopfblöcke, kein `@brief`/`@param`/`@return` mit allen Statuscodes, Bit-Layout nur bei CP56Time2a, nicht bei Steuerfeld, VSQ und COT | Erster Schritt nach Freigabe |
| A2 | Tests (`test_apci`, `test_asdu_header`, `test_cp56time2a`) | Referenzbytes sind von Hand hergeleitet, die Quelle steht nur pauschal da („docs/03 … and the Wireshark dissector“). TASK-001 verlangt belegte Referenzen. | Je Testfall die Quelle nennen. Vorschlag F5: Referenz-Hexdumps aus Mitschnitten gegen lib60870 (tshark) |
| A3 | Code-Kommentare | Verweise auf deutsche Abschnittstitel, z. B. „section ‚Sechs Oktette APCI‘“ | Durch Dateiverweis plus englische Beschreibung ersetzen |
| A4 | `asdu_header.hpp` | `kBroadcastCa` definiert, aber nicht genutzt; CA 0 nicht geprüft | In TASK-001/003 nach N10 klären |
| A5 | `asdu_header.cpp` | VSQ mit Anzahl 0 wird ohne Prüfung angenommen | Nach N9 in TASK-001 |
| A6 | `cp56time2a` | RES1 bleibt erhalten, RES2–RES4 werden verworfen. Gewollt, aber nicht begründet | Begründung im Kommentar (A1) |
| A7 | `iec104.h` | Unter Windows fehlt `__declspec(dllimport)` für Nutzer der DLL | Nur nötig, falls Windows Zielplattform wird (Frage F7) |
| A8 | `CLAUDE.md`, `README.md` | Bezüge auf Projekt und Firma | Neutral umformulieren, Abschnitt 6 von TASK-000 ergänzen (DoD) |
| A9 | `tools/setup_tools.sh` | Distributionsname eines Hardwareherstellers, Name eines Fremd-Repos im Kommentar | Neutral formulieren |
| A10 | `.vscode/extensions.json` | Erweiterungs-ID enthält einen Firmennamen | Frage F6 |
| A11 | `IEC104_Repo_Setup.md` (Wurzelordner) | Erledigter Einrichtungsauftrag, enthält Projekt- und Firmennamen, ist nicht von der Prüfung ausgenommen | Frage F1 |
| A12 | Commit-Nachrichten `cbee03d` (gepusht) und `67f7f99` (lokal) | Enthalten eine Co-Author-Zeile mit Firmen-Mailadresse, `cbee03d` zusätzlich einen Sitzungslink | Frage F6 |

## 4. Vorschlag Aufgabenreihe

| Aufgabe | Ziel (ein Satz) | Größe |
| --- | --- | --- |
| TASK-000 Rest | Vorhandenen Code auf den Kommentarstandard bringen, Testquellen nachtragen | S |
| TASK-001 | Codec vollständig: Elemente, Typtabelle, ASDUs mit SQ 0/1, Aufteilen, strikt/tolerant, Fuzzer erweitert | L |
| TASK-002 | APCI-Zustandsmaschine sans-I/O mit injizierter Uhr: STARTDT/STOPDT/TESTFR, k/w, t0–t3, Zählerüberlauf; Timer-Konfiguration mit Plausibilitätsprüfung; Fuzzer für die Zustandsmaschine; Selbsttest ohne Netz: zwei Zustandsmaschinen über einen Speicherkanal gekoppelt | L |
| TASK-003 | Session-Grundabläufe: Generalabfrage (auch Gruppen), Zählerabfrage, Uhrzeitsynchronisation, End of Initialization, Zeitbasis | M |
| TASK-004 | Befehle: direkt und Select/Execute je IOA, mit und ohne Zeitmarke, ACTTERM optional, getrennte Timeouts, Max-Delay | M |
| TASK-005 | Transport TCP und Event-Loop (`poll`/`epoll`, eigener Thread oder `iec104_poll`), C-API mit Handles, Konfiguration (Ports für TCP und TLS je Verbindung konfigurierbar, Port 0 = vom System gewählt), Callbacks, Logging, Statistik, `install(EXPORT)` | L |
| TASK-006 | Selbsttest-Programme Server und Client (zugleich Beispielprogramme), Ports und Adresse per Kommandozeile und Konfigurationsdatei; Selbsttest-Szenarien (STARTDT, GA, Befehle, Uhrzeit, Zähler, Verbindungsabbruch) mit Ergebnisbericht und Exit-Code; Integrationstest in ctest über Loopback | M |
| TASK-007 | TLS über OpenSSL 3.x hinter `tls_backend`: gegenseitige X.509-Authentifizierung, CRL, Cipher-Liste, Renegotiation/Key Update, Zertifikatswechsel zur Laufzeit, Negativtests | L |
| TASK-008 | Redundanzgruppen mit Ereignispuffer je Gruppe, Überlaufstrategie, Umschaltung per STOPDT/STARTDT, mehrere Gruppen | L |
| TASK-009 | Interop-Tests gegen lib60870 als externe Gegenstelle (GA, Befehle, Uhrzeit, Redundanz), Mitschnitte als Referenzdaten | M |
| TASK-010 | API-Doku mit Doxygen, Export der eigenen Konfiguration als Interoperabilitätsliste (Abschnitt 9) | M |

## 5. Fragen an den Auftraggeber

| Nr. | Frage | Vorschlag |
| --- | --- | --- |
| F1 | Was geschieht mit `IEC104_Repo_Setup.md` im Wurzelordner (erledigt, enthält Namen)? | Löschen; der Inhalt bleibt in der Git-Historie |
| F2 | Begriffsdatei `~/.config/iec104/forbidden_terms.txt` fehlt. Soll ich einen Entwurf aus den Namen in `docs/` erzeugen, den du prüfst? | Ja, Entwurf außerhalb des Repos ablegen |
| F3 | Defaults: strikter Modus an (W5)? Plausibilitätsprüfung t2/t1 und w/k als eigener Parameter mit Default Warnung (W4)? | Ja und ja |
| F4 | Werden die Normtexte beschafft (104 Ed. 2.1, 62351-3 Ed. 2, TS 60870-5-604)? Ohne sie bleiben N1–N12 vorläufige Annahmen. | Beschaffung vor TASK-002 |
| F5 | Dürfen Referenz-Hexdumps aus eigenen Mitschnitten gegen lib60870 (tshark) stammen? Mitschnitte sind Daten, kein übernommener Code. | Ja, mit Quelle „capture against lib60870 v2.4.1, decoded by Wireshark“ |
| F6 | Commit-Nachrichten: Co-Author-Zeile mit Firmen-Mailadresse künftig weglassen? `67f7f99` (lokal) bereinigen? `cbee03d` ist gepusht und ließe sich nur per Force-Push ändern. Gilt die Neutralität auch für die Erweiterungs-ID in `.vscode/extensions.json`? | Zeile weglassen, `67f7f99` bereinigen, `cbee03d` lassen; Erweiterungs-ID als Werkzeug erlauben |
| F7 | Ist Windows eine Zielplattform? | Nein, nur Linux x86-64 in Version 1 |
| F8 | Optionaler C++-Wrapper (`02`): in Version 1? | Nein, später |

## 6. Entscheidungen (Freigabe am 25.11.2026)

Die Analyse ist freigegeben. Zu F1–F8 gelten die Vorschläge aus Abschnitt 5.

| Nr. | Entscheidung | Umsetzung |
| --- | --- | --- |
| F1 | `IEC104_Repo_Setup.md` löschen | gelöscht, Inhalt in der Git-Historie |
| F2 | Entwurf der Begriffsliste außerhalb des Repos | `~/.config/iec104/forbidden_terms.txt`, Prüfung durch den Auftraggeber offen; `check_neutral.sh` kennt `word:` für ganze Wörter |
| F3 | Strikter Modus Default an; Plausibilitätsprüfung eigener Parameter, Default Warnung | in `02` bereits so beschrieben |
| F4 | Normtexte vor TASK-002 beschaffen | offen beim Auftraggeber |
| F5 | Referenz-Hexdumps aus Mitschnitten gegen lib60870 | `tests/data/lib60870_capture.txt` |
| F6 | Keine Co-Author-Zeilen; `67f7f99` bereinigen; `cbee03d` bleibt | Regel in `CLAUDE.md`, lokale Historie bereinigt |
| F7 | Nur Linux x86-64 in Version 1 | `CLAUDE.md`; A7 (dllimport) entfällt |
| F8 | Kein C++-Wrapper in Version 1 | – |
| F9 | Lizenz (Nachtrag 25.11.2026): Rechte liegen ausschließlich beim Rechteinhaber; was für den Zugriff gebraucht wird (öffentlicher Header), ist offen unter Apache-2.0 | SPDX-Kopfblock in jeder Quelldatei, `LICENSES/Apache-2.0.txt` und `LICENSES/LicenseRef-Proprietary.txt`; Ausnahme von der Neutralitätsregel nur für diese Kopfzeilen (`allow:` in der Begriffsdatei) und `LICENSES/` |
| F10 | Selbsttest (Nachtrag 25.11.2026): Die Library muss sich selbst testen können, bis der integrierte Test gegen die Gegenstelle des Auftraggebers folgt; Programme für den Selbsttest, Ports konfigurierbar | TASK-000 Abschnitt 1, Aufgabenreihe TASK-002 (ohne Netz), TASK-005 (Ports), TASK-006 (Programme, ctest) |

Umgesetzt aus Abschnitt 1: W1 (Hinweis in `03`), W3, W6, W7, W8 (in `02`), W10 (Catch2 aus dem Setup), W11 (Fuzz-Dauern in `CLAUDE.md`). In `02` zusätzlich die CP24-Typen korrigiert: 2, 4, …, 16 und 17–19 (vorher „2–19 gerade“).
