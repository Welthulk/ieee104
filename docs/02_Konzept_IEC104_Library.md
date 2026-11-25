# Konzept IEC-104-Library (C/C++)

Stand: 24.11.2026 · Udo

Eigene IEC-60870-5-104-Library für die SpPS: Kern in C/C++, stabile C-Schnittstelle (auch für Rust), jede bekannte Abweichung zwischen Gegenstellen als Parameter, TLS über eine externe Library.

## Ziel und Rahmen

Die Library stellt IEC 104 als Client und Server bereit. Sie wird in die SpPS eingebunden, zuerst für die Anbindung an die Prozessabkopplung (Aucoteam).

- **Umfang Version 1:** Client (Controlling Station) und Server (Controlled Station), alle Monitor-, Steuer-, System- und Parametertypen, Redundanzgruppen nach Edition 2, TLS nach IEC 62351-3.
- **Später:** Dateiübertragung (Typen 120–127), Secure Authentication nach IEC TS 60870-5-7, IEC 101 seriell.
- **Normbasis:** IEC 60870-5-104 Ed. 2.1 (2006 + AMD1:2016) inkl. COR1:2023.
- **Leitprinzip:** Normverhalten als Default. Jede bekannte Abweichung einer Gegenstelle ist ein Parameter, nie ein Code-Zweig für einen Hersteller.
- **Schnittstellen:** stabile C-API (auch für Rust über `bindgen`), optional ein dünner C++-Wrapper.
- **Lizenz:** Eigenentwicklung ohne GPL-Code. lib60870-C (GPLv3) darf nur als Testgegenstelle genutzt werden, nicht im Produkt.

## Sprachwahl

Empfehlung: Kern in C++17 mit reiner C-API nach außen. Damit hat man RAII und Typsicherheit innen und ein stabiles ABI außen.

| Kriterium | C11 | C++17 mit C-API |
| --- | --- | --- |
| ABI-Stabilität nach außen | nativ | über `extern "C"`, gleichwertig |
| Rust-Anbindung (`bindgen`) | direkt | direkt (nur C-Header sichtbar) |
| Speicher- und Ressourcensicherheit | manuell | RAII, `std::unique_ptr`, weniger Lecks |
| Typsicherheit ASDU-Typen | Makros, `union` | `enum class`, `std::variant` |
| Embedded-Tauglichkeit | sehr gut | gut, wenn ohne Exceptions und RTTI gebaut |
| Statische Analyse | MISRA C, cppcheck | clang-tidy, MISRA C++ 2023 |

Regeln für den C++-Kern: keine Exceptions über die API-Grenze, Fehler als Returncodes. Keine STL-Typen in öffentlichen Headern. Build mit CMake, Zielplattformen Linux x86-64 (und weitere nach SpPS-Bedarf).

## Architektur

Vier Schichten, jede ohne Abhängigkeit nach oben. Codec und Zustandsmaschine machen kein I/O und sind damit vollständig ohne Netzwerk testbar.

| Schicht | Modul | Aufgabe |
| --- | --- | --- |
| 1 | `codec` | APCI und ASDU kodieren und dekodieren, alle Typkennungen, CP56Time2a, Qualitätsbits. Reine Funktionen auf Byte-Puffern. |
| 2 | `apci` | Zustandsmaschine je Verbindung: STARTDT/STOPDT/TESTFR, N(S)/N(R), k/w, t0–t3. Bekommt Bytes und Zeit rein, gibt Bytes und Ereignisse raus. |
| 3 | `session` | Anwendungsabläufe: Generalabfrage, Uhrzeit, Befehle mit Select/Execute, Pufferung, Redundanzgruppen. |
| 4 | `transport` | TCP und TLS, Event-Loop, Verbindungsaufbau. Austauschbares Backend (TLS-Library, I/O-Modell). |
| API | `iec104.h` | C-Schnittstelle: Handles, Konfiguration, Callbacks, Fehlercodes. |

Datenpunkte verwaltet die Library nicht als Datenbank. Die SpPS liefert und empfängt Werte über Callbacks. Nur für die Generalabfrage und die Pufferung hält der Server ein optionales Abbild.

## Konfiguration

Alle Parameter gelten je Verbindung bzw. Gegenstelle und kommen aus einem Konfigurationsobjekt (C-Struct, ladbar aus Datei). Die Spalte "Abweichung" nennt belegte Beispiele aus der Recherche.

**Verbindung und Timer**

| Parameter | Default (Norm) | Bereich | Abweichung in der Praxis |
| --- | --- | --- | --- |
| Port | 2404 (TLS: 19998) | 1–65535 | projektspezifisch |
| t0 | 30 s | 1–255 s | Weidmüller 15 s |
| t1 | 15 s | 1–255 s | WEMAG 250 s |
| t2 | 10 s | 1–255 s, < t1 | WEMAG 240 s |
| t3 | 20 s | 0 (aus) oder 1 s–48 h | WEMAG 255 s, SvK 30 s; 0 nur im toleranten Modus |
| k | 12 | 1–32767 | |
| w | 8 | 1–32767, ≤ 2/3 k | |
| Prüfung t2 < t1 und w ≤ 2/3 k | Warnung | Warnung / Fehler / aus | |

**ASDU-Profil**

| Parameter | Default | Optionen | Zweck |
| --- | --- | --- | --- |
| Längen COT / CA / IOA | 2 / 2 / 3 | 1–2 / 1–2 / 1–3 | Gegenstellen außerhalb der Norm |
| Herkunftsadresse (ORG) | 0 | 0–255 | |
| Gemeinsame Adressen | Liste | eine oder mehrere CA je Verbindung | |
| Max. ASDU-Länge | 249 | 1–249 | Gegenstellen mit kleinerem Maximum; Sender teilt ASDUs auf |
| Strikter Modus | an | an / tolerant | CP24-Typen (2, 4, …, 16, 17–19) annehmen oder ablehnen |
| Unbekannte Typen / COT | negativ quittieren (COT 44/45) | quittieren / ignorieren / trennen | |

**Zeit**

| Parameter | Default | Optionen | Abweichung |
| --- | --- | --- | --- |
| Zeitbasis | UTC | UTC / UTC+1 fest / Ortszeit mit SU-Bit | SvK UTC+1, Pfalzwerke Ortszeit |
| Uhrzeitsync über 104 (Typ 103) | senden nach GA | aus / einmalig / zyklisch | Gegenstelle per NTP synchronisiert |
| Max. Befehlsverzögerung (zeitgestempelte Befehle) | 0 = aus | Sekunden | AMD1: zu späte Befehle stumm verwerfen |

**Befehle und Abläufe**

| Parameter | Default | Optionen | Abweichung |
| --- | --- | --- | --- |
| Befehlsmodell je IOA | direkt | direkt / Select-Execute | SvK SBO |
| Befehlstypen | ohne Zeit (45–50) | ohne / mit Zeit (58–64) | Pfalzwerke nur mit Zeit |
| ACTTERM erwarten | ja | ja / nein | WinCC OA sendet keins bei Sollwerten |
| Befehls-Timeout | 10 s | je IOA | SvK 20 s |
| Generalabfrage nach STARTDT | an | an / aus, Gruppen 21–36 | |
| End of Initialization (Typ 70) | nach Start senden | immer / nur lokaler Neustart / nie | SvK nur lokaler Neustart |

**Redundanz und Pufferung**

| Parameter | Default | Optionen | Abweichung |
| --- | --- | --- | --- |
| Redundanzgruppen | 1 Gruppe, 1 Verbindung | n Gruppen à m Verbindungen | SvK ≥ 4 je Gruppe, ≥ 8 Gruppen |
| Ereignispuffer je Gruppe | 1000 | Größe, Überlaufstrategie | SvK ≥ 100 mit Alarm |
| Puffer bei STOPDT | an | an / aus | |
| Puffer-Überlauf | älteste verwerfen + Ereignis | verwerfen / blockieren | |

Die Library kann ihre aktuelle Konfiguration als ausgefüllte Interoperabilitätsliste (Abschnitt 9 der Norm) exportieren. So lässt sich direkt mit der Liste der Gegenstelle abgleichen.

## TLS

TLS wird nicht selbst implementiert, sondern über eine externe Library hinter einer schmalen Backend-Schnittstelle (`tls_backend`) angebunden. Erstes Backend: OpenSSL 3.x. mbedTLS kommt als Alternative für schlanke Ziele infrage.

| Kriterium | OpenSSL 3.x | mbedTLS 3.x |
| --- | --- | --- |
| Lizenz | Apache 2.0 | Apache 2.0 |
| TLS 1.2 / 1.3 | ja / ja | ja / ja |
| Sichere Renegotiation (TLS 1.2) | ja | ja, per Build-Option |
| Verbreitung auf Linux-Servern | Standard, Systempaket | eher Embedded |

**Profil nach IEC 62351-3 Ed. 2 (2023), konfigurierbar:**

- TLS-Versionen: 1.2 und 1.3, einzeln abschaltbar.
- Cipher-Suites: Liste je Verbindung. Default aus Tabelle 1 der Norm (Normtext liegt noch nicht vor, ist zu prüfen).
- Gegenseitige Authentifizierung mit X.509: CA-Kette, eigenes Zertifikat und Schlüssel, Prüfung des Gegenstellen-Namens oder Fingerprints.
- Sperrprüfung: CRL-Datei, optional OCSP.
- Renegotiation (1.2) bzw. Key Update (1.3) nach Zeit oder Datenmenge, Grenzwerte konfigurierbar.
- Session Resumption: an/aus, Lebensdauer.
- Zertifikatswechsel zur Laufzeit ohne Neustart der SpPS.
- Sicherheitsereignisse (Handshake-Fehler, abgelaufenes Zertifikat) als Callback, angelehnt an IEC 62351-14.

Grundlage für die Anforderung ist das BDEW-Whitepaper 3.0: IEC 104 hat ohne Zusatzmaßnahmen weder Integrität noch Authentifizierung noch Verschlüsselung.

## Laufzeitmodell, Fehler, Logging

Die Library bringt einen eigenen Event-Loop mit, lässt sich aber auch vom Host takten. So passt sie in die SpPS, egal wie deren Threading aussieht.

- **Betriebsarten:** (a) interner Thread je Instanz, (b) Host ruft `iec104_poll(timeout)` selbst auf. I/O über `poll`/`epoll`, keine harte Abhängigkeit auf eine Async-Bibliothek.
- **Threadsicherheit:** Senden ist aus jedem Thread erlaubt (interne Queue). Callbacks kommen immer aus dem Loop-Thread.
- **Zeitquelle:** monotone Uhr für Timer, Wanduhr nur für Zeitstempel. Beide sind injizierbar (wichtig für Tests).
- **Fehler:** C-Fehlercodes plus Textbeschreibung. Protokollverletzungen der Gegenstelle führen je nach Konfiguration zu negativer Quittung, Ereignis oder Verbindungsabbau.
- **Logging:** Callback mit Level und Kategorie. Optionaler Rohdaten-Trace (Hexdump je APDU) für die Inbetriebnahme.
- **Statistik je Verbindung:** gesendete und empfangene APDUs, Timeouts, Verbindungsabbrüche, Pufferfüllstand.
- **Speicher:** Puffer werden vorab allokiert, Obergrenzen sind konfigurierbar, keine unbegrenzten Queues.

## Testkonzept

Getestet wird in sechs Ebenen, von reinen Funktionen bis zur echten Gegenstelle. Die Ebenen 1 bis 4 laufen automatisch in der CI, 5 und 6 vor jeder Auslieferung.

| Ebene | Was | Werkzeug | Abnahmekriterium |
| --- | --- | --- | --- |
| 1 Unit | Codec aller Typen, CP56Time2a, Qualitätsbits | GoogleTest oder Catch2 | jede Typkennung kodiert und dekodiert, Referenz-Hexdumps aus Norm und Herstellerdoku |
| 2 Zustandsmaschine | APCI mit simulierter Zeit: k/w, t0–t3, Überlauf der Sequenzzähler, STOPDT | eigene Testumgebung, injizierte Uhr | alle Timer-Pfade, Wrap bei 32767, Verhalten bei falschem N(R) |
| 3 Robustheit | Fuzzing des Decoders und der Zustandsmaschine | libFuzzer, AFL++, mit ASan/UBSan | 24 h ohne Absturz, kein Leck |
| 4 Integration | Client gegen eigenen Server, Redundanzumschaltung, Pufferung, TLS-Handshake und Zertifikatsfehler | ctest, Netzwerk-Namespaces | Szenarien aus der Konfigurationsmatrix |
| 5 Interop | gegen fremde Implementierungen | lib60870-C (nur Test), QTester104, ggf. Triangle MicroWorks Test Harness | GA, Befehle, Uhrzeit, Redundanz mit jeder Gegenstelle |
| 6 Konformität und Abnahme | Testfälle angelehnt an IEC TS 60870-5-604 Ed. 2, Test mit Aucoteam-Gateway | Testprotokoll, Wireshark-Mitschnitte | Abnahmeprotokoll je Gegenstelle |

**Zusätzlich:**

- **Langlauf:** 72 h unter Last mit 10.000 Datenpunkten und erzwungenen Verbindungsabbrüchen. Gemessen werden Speicher, Latenz und Ereignisverlust (Soll: 0 innerhalb der Puffergröße).
- **Profiltests:** Die Konfigurationen aus der Recherche werden als Testprofile hinterlegt, benannt nach ihrer Eigenschaft, nicht nach Netzbetreibern (Neutralitätsregel, `tasks/TASK-000_Auftrag.md`), z. B. `long_timeout_profile`, `local_time_su_profile`, `fixed_offset_time_profile`, `select_before_operate_profile`. So ist jede Abweichung einmal abgedeckt.
- **Statische Analyse:** clang-tidy, cppcheck, Compiler-Warnungen als Fehler.
- **TLS-Prüfung:** Handshake gegen `openssl s_server`/`s_client` und lib60870 mit TLS. Zusätzlich Negativtests: falsches Zertifikat, gesperrtes Zertifikat, abgelaufenes Zertifikat, falsche Suite.

Hinweis: IEC TS 60870-5-604 prüft Normkonformität, garantiert aber nach eigener Aussage keine Interoperabilität. Ebene 5 und der Test mit dem Aucoteam-Gateway sind deshalb Pflicht.

## Offene Entscheidungen

- [x] Sprache: C++17-Kern mit C-API (entschieden, siehe `docs/04_Analyse.md`).
- [x] TLS-Backend: OpenSSL 3.x als erstes Backend (entschieden).
- [ ] Normtexte beschaffen: IEC 60870-5-104 Ed. 2.1, IEC 62351-3 Ed. 2 (Cipher-Tabelle, Renegotiation-Regeln), IEC TS 60870-5-604.
- [ ] Interoperabilitätsliste und Testzugang vom Aucoteam-Gateway anfordern.
- [ ] Rolle in der SpPS klären: nur Client, oder auch Server?
- [ ] Zielplattformen und Threading-Modell der SpPS festlegen.
- [x] Lizenzmodell und Rechte am Code: Rechte bei SOPTIM AG, öffentlicher Header unter Apache-2.0, Rest proprietär (entschieden, F9 in `docs/04_Analyse.md`).
- [ ] Testgegenstelle Triangle MicroWorks Test Harness: Lizenz beschaffen oder nicht?

Grundlage der Werte und Abweichungen ist der Recherchebericht "IEC 104 im Detail" vom 24.11.2026.
