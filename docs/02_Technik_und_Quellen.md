# Technik und Quellen

Stand: 25.11.2026 · Normstand IEC 60870-5-104 Ed. 2.1 (2016) mit COR1:2023 · Quellen-Kürzel siehe letzter Abschnitt.

## Sprache und Architektur

| Punkt | Festlegung |
| --- | --- |
| Sprache | Kern C++17, Schnittstelle nur C (`include/iec104/iec104.h`) |
| Fehler | Rückgabewert `iec104_status`; keine Exceptions, kein RTTI (`-fno-exceptions -fno-rtti`) |
| Öffentlicher Header | reines C, keine STL-Typen, Export nur über `IEC104_API` |
| Abweichungen von Gegenstellen | Parameter, nie ein Code-Zweig für ein Gerät |
| Stil | Lesbarkeit vor Performance; jede Protokollregel mit Quelle im Kommentar |

| Schicht | Ordner | Aufgabe |
| --- | --- | --- |
| codec | `src/codec` | APCI und ASDU kodieren/dekodieren; reine Funktionen, kein I/O, keine Allokation |
| apci | `src/apci` | Zustandsmaschine je Verbindung: Bytes und Zeit rein, Bytes und Ereignisse raus |
| session | `src/session` | Prozessabbild, Abfragen, Uhrzeit, Befehle, Befehlsverfolgung |
| transport | `src/transport` | TCP, Verbindungsaufbau, Uhren |
| API | `src/api` | C-Schnittstelle: Handles, Konfiguration, Callbacks |

Keine Schicht hängt von einer höheren ab. Codec, Zustandsmaschine und Session sind ohne Netz und mit simulierter Zeit testbar.

## APCI

APDU = 0x68, Längenoktett, 4 Steueroktette, ASDU. Länge ≤ 253, also APDU ≤ 255 und ASDU ≤ 249 Oktette [WS].

| Format | Erkennung (Oktett 3) | Inhalt |
| --- | --- | --- |
| I | Bit 0 = 0 | N(S) und N(R), je 15 Bit, um ein Bit nach links geschoben; ASDU folgt |
| S | Bits 0–1 = 01 | nur N(R) |
| U | Bits 0–1 = 11 | genau eine Funktion: STARTDT act 0x07 / con 0x0B, STOPDT act 0x13 / con 0x23, TESTFR act 0x43 / con 0x83 |

Folgenummern modulo 32768, Start bei 0 nach dem Verbindungsaufbau [VUT]. Port 2404 [IANA].

## Verbindung und Timer

| Parameter | Bedeutung | Default | Bereich |
| --- | --- | --- | --- |
| t0 | Timeout Verbindungsaufbau | 30 s | 1–255 s |
| t1 | Timeout für Sende- und Test-APDUs; Ablauf schließt die Verbindung | 15 s | 1–255 s |
| t2 | Quittung spätestens nach t2 ohne eigene Daten; t2 < t1 | 10 s | 1–255 s |
| t3 | Testrahmen nach Ruhe | 20 s | 1 s–48 h |
| k | max. unquittierte gesendete I-Rahmen | 12 | 1–32767 |
| w | Quittung spätestens nach w I-Rahmen; w ≤ 2/3 k empfohlen | 8 | 1–32767 |

Quelle: Interoperabilitätsliste, wiedergegeben in [ORA].

| Abweichung in der Praxis | Umsetzung |
| --- | --- |
| lange Timer (z. B. t1 250 s, t2 240 s, t3 255 s) | voller Bereich; t2 < t1 und w ≤ 2/3 k nur als Warnung (E2) |
| Verbindungstest abgeschaltet (t3 = 0) | nur im toleranten Modus (E3) |
| t1 nur für U-Rahmen | t1 überwacht I-Rahmen und U-Rahmen act (R10) |

## ASDU-Kopf und ASDU-Profil

Datenkopf im IEC-104-Profil (6 Oktette) [WS], [VUT]:

| Feld | Länge | Inhalt |
| --- | --- | --- |
| Typkennung | 1 | siehe „Typkennungen“ |
| VSQ | 1 | Bit 7 SQ, Bits 0–6 Anzahl der Objekte (0–127) |
| COT | 2 | Bits 0–5 Ursache, Bit 6 P/N (negativ), Bit 7 T (Test); zweites Oktett Herkunftsadresse (0 = ungenutzt) |
| CA | 2 | gemeinsame Adresse: 0 ungenutzt, 1–65534 Station, 65535 Broadcast |

Danach je Objekt eine IOA (3 Oktette; IOA 0 = systembezogene Befehle wie die Generalabfrage). Mehroktettwerte: niederwertiges Oktett zuerst („Mode 1“).

SQ = 0: jedes Objekt mit eigener IOA. SQ = 1: eine IOA, folgende Objekte fortlaufend adressiert.

ASDU-Profil (`iec104_link_config`):

| Parameter | Default | Bereich | Zweck |
| --- | --- | --- | --- |
| Längen COT / CA / IOA | 2 / 2 / 3 | 1–2 / 1–2 / 1–3 | Gegenstellen mit Feldlängen wie IEC 101 |
| max. ASDU-Länge | 249 | 1–249 | kleineres Maximum der Gegenstelle; lange Listen werden aufgeteilt |
| strikt | an | an / tolerant | bekannte Abweichungen zulassen (E1) |

## Typkennungen

| Gruppe | Typkennungen | Hinweis |
| --- | --- | --- |
| Melderichtung ohne Zeitmarke | 1, 3, 5, 7, 9, 11, 13, 15, 20, 21 | 13 = Gleitkomma, 9/11 normiert/skaliert |
| Melderichtung mit CP56Time2a | 30–40 | |
| nur IEC 101 (CP24Time2a) | 2, 4, 6, 8, 10, 12, 14, 16, 17, 18, 19 | in IEC 104 nach verbreiteter Lesart unzulässig; nur im toleranten Modus (N4) |
| Befehle ohne Zeitmarke | 45–51 | Einzel-, Doppel-, Stufen-, Sollwert-, Bitmusterbefehl |
| Befehle mit CP56Time2a | 58–64 | nur IEC 104 |
| System | 70, 100–107 | 70 Ende der Initialisierung, 100 Generalabfrage, 101 Zählerabfrage, 102 Lesen, 103 Uhrzeit, 104 Test, 105 Reset, 106 Verzögerung, 107 Test mit Zeitmarke |
| Parameter | 110–113 | |
| Dateiübertragung | 120–127 | nicht im Umfang der Version 1 |
| privat | 136–255 | projektspezifisch; die Library reicht sie roh durch |
| Security (IEC TS 60870-5-7) | 41, 81–87, 90–95 | nicht Teil von IEC 104 |

Elementlängen je Typ: [WS]; Elementfolge je Typ: `src/codec/type_table.cpp`, geprüft gegen Referenz-ASDUs (`tests/data/lib60870_asdu_reference.txt`).

## Übertragungsursachen

| COT | Bedeutung | COT | Bedeutung |
| --- | --- | --- | --- |
| 1 | zyklisch | 11 | Rückmeldung Fernbefehl |
| 2 | Hintergrund | 12 | Rückmeldung Ortsbefehl |
| 3 | spontan | 13 | Dateiübertragung |
| 4 | initialisiert | 20 | Generalabfrage (Station) |
| 5 | Abfrage | 21–36 | Generalabfrage Gruppe 1–16 |
| 6 | Aktivierung | 37 | Zählerabfrage allgemein |
| 7 | Bestätigung der Aktivierung | 38–41 | Zählerabfrage Gruppe 1–4 |
| 8 | Abbruch | 44 | unbekannte Typkennung |
| 9 | Bestätigung des Abbruchs | 45 | unbekannte Übertragungsursache |
| 10 | Beendigung der Aktivierung | 46 | unbekannte gemeinsame Adresse |
| | | 47 | unbekannte IOA |

48–63: privat. Quelle [WS]. Zulässige Kombinationen Typ/COT (Abschnitt 9.5): N3.

## Informationselemente

| Element | Inhalt |
| --- | --- |
| Qualität | IV 0x80 ungültig, NT 0x40 nicht aktuell, SB 0x20 ersetzt, BL 0x10 blockiert; QDS zusätzlich OV 0x01 Überlauf [WS] |
| DIQ | 0 und 3 unbestimmt, 1 AUS, 2 EIN |
| NVA | 16 Bit mit Vorzeichen, Wert = Rohwert / 32768 |
| SVA | 16 Bit mit Vorzeichen |
| R32 | IEEE 754 einfach, niederwertiges Oktett zuerst |
| CP56Time2a | 7 Oktette: ms inkl. Sekunden (0–59999), Minute + IV, Stunde + SU, Tag + Wochentag, Monat, Jahr 0–99 [WS] |

Bit-Layouts: `src/codec/elements.hpp`, bestätigt durch die Referenz-ASDUs in `tests/data/`.

## Zeit

UTC oder Ortszeit ist nicht genormt, sondern Teil der Interoperabilitätsliste. Der Codec trägt keine Zeitzone; IV, SU und Wochentag sind eigene Felder. Die Library rechnet in UTC (N13).

## Redundanz

| Punkt | Stand |
| --- | --- |
| Redundante Verbindungen | Abschnitt 10 der Norm, neu seit Edition 2 |
| Gruppe | mehrere Verbindungen einer Leitstelle; genau eine überträgt Daten (STARTDT), die anderen stehen in STOPDT bereit und werden mit TESTFR überwacht |
| Umschalten | Leitstelle sendet STOPDT auf der alten, STARTDT auf der neuen Verbindung, danach üblicherweise eine Generalabfrage |
| Ereignispuffer | gehört der Gruppe, puffert auch ohne gestartete Verbindung; Modelle in [LIB]: eine Gruppe für alle, Gruppe je Verbindung, mehrere Gruppen nach Adresse |
| Umsetzung | `src/session/redundancy_group.hpp`; Festlegungen docs/05, N7 und E10 |

## Sicherheit

| Punkt | Stand |
| --- | --- |
| IEC 104 | keine Integrität, Authentifizierung oder Verschlüsselung |
| TLS | IEC 62351-3 Ed. 2 (2023), TLS 1.2/1.3 [62351]; umgesetzt mit OpenSSL 3.x hinter einer Backend-Schnittstelle (`src/transport/tls.hpp`), Profil siehe docs/05, N12 |
| Authentifizierung | gegenseitig mit X.509: CA, eigenes Zertifikat mit Kette, Sperrlisten (CRL), optional Name oder SHA-256-Fingerprint der Gegenstelle |
| Schlüsselerneuerung | Key Update (TLS 1.3, RFC 8446), Renegotiation (TLS 1.2, RFC 5746), nach Zeit |
| Ereignisse | Callback `on_security`: Handshake, abgelehntes Zertifikat, TLS-Fehler, Schlüsselerneuerung |
| Port | 2404 ohne TLS, 19998 mit TLS („iec-104-sec“) [IANA] |

## Designfolgerungen

| Thema | Folgerung |
| --- | --- |
| strikt/tolerant | strikt ist Default; tolerant erlaubt CP24-Typen, ASDUs ohne Objekte, t3 = 0 |
| nie toleriert | Länge > 253, mehrere U-Funktionsbits, falsche N(S), N(R) außerhalb des Fensters |
| Dekodieren | verlustfrei, reservierte Bits bleiben erhalten; Kodieren schneidet nie ab (E5) |
| Tests | Referenzbytes einer fremden Implementierung, Fuzzing, Selbsttest über TCP, Gegenprobe mit lib60870 |

## Quellen

| Kürzel | Quelle |
| --- | --- |
| [WS] | Wireshark, IEC 60870-5-104 Dissector: https://raw.githubusercontent.com/wireshark/wireshark/master/epan/dissectors/packet-iec104.c |
| [VUT] | Matoušek, „Description and analysis of IEC 104 Protocol“, FIT VUT Brno 2017: https://www.fit.vut.cz/research/publication-file/11570/TR-IEC104.pdf |
| [ORA] | Oracle Network Management System, User's Guide, IEC 60870-5-104 Options (Wiedergabe der Interoperabilitätsliste): https://docs.oracle.com/en/industries/energy-water/network-management-system/26000/nms-users-guide/NMS_USER_26000/10_SCADA_Extensions.10.27.html |
| [IANA] | IANA Service Name and Transport Protocol Port Number Registry: https://www.iana.org/assignments/service-names-port-numbers?search=iec-104 |
| [AMD1] | IEC 60870-5-104:2006/AMD1:2016, Leseprobe: https://cdn.standards.iteh.ai/samples/22910/722bcd41d12f48898e3b0b642418b49d/IEC-60870-5-104-2006-AMD1-2016.pdf |
| [CON] | IEC 60870-5-104 Ed. 2.1, Vorschau mit Inhaltsverzeichnis: https://vde-verlag.de/iec-normen/preview-pdf/info_iec60870-5-104{ed2.1}b.pdf |
| [62351] | IEC 62351-3 Ed. 2.0, Vorschau: https://assets.vde-verlag.de/iec-normen/preview-pdf/info_iec62351-3%7Bed2.0%7Db.pdf |
| [LIB] | lib60870 (nur externe Testgegenstelle, weder gelinkt noch Code übernommen): https://github.com/mz-automation/lib60870, Servermodi: https://github.com/mz-automation/lib60870/blob/master/user_guide.adoc |
