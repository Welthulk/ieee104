# Technik und Quellen

Stand: 25.11.2026

Dieses Dokument fasst die technischen Grundlagen der Library zusammen: Architektur, Protokollaufbau, Parameter und die öffentlichen Quellen, auf die sich die Regeln im Code stützen. Kommentare im Code verweisen auf die Abschnitte hier („docs/02_Technik_und_Quellen.md, …“). Die Regeln der Verbindungssteuerung sind in `docs/03_Quellen_APCI.md` einzeln belegt, offene Normfragen stehen in `docs/05_Normfragen_und_Festlegungen.md`.

Normstand: IEC 60870-5-104 Ed. 2.1 (2016), also Ed. 2 von 2006 mit Amendment 1 von 2016, ergänzt um die Berichtigung COR1:2023. Der Normtext lag bei der Entwicklung nicht vor; die Quellen sind am Ende aufgeführt (Kürzel in eckigen Klammern).

## Sprache und Architektur

Der Kern ist in C++17 geschrieben, nach außen gibt es nur eine C-Schnittstelle. Innen bringt das RAII und Typsicherheit, außen ein stabiles ABI, das sich aus C, C++ und jeder Sprache mit C-Anbindung nutzen lässt.

Regeln:

- Keine Exceptions und kein RTTI im Library-Code (`-fno-exceptions -fno-rtti`); Fehler sind Rückgabewerte vom Typ `iec104_status`.
- Öffentliche Header sind reines C, ohne STL-Typen; alle exportierten Symbole tragen `IEC104_API`, alle anderen sind verborgen.
- Lesbarkeit vor Performance: kleine Funktionen, ausführliche Kommentare mit Bit-Layouts, jede Protokollregel mit Quelle.
- Normverhalten ist Default. Bekannte Abweichungen von Gegenstellen werden Parameter, nie ein Code-Zweig für ein bestimmtes Gerät.

Schichten (jede ohne Abhängigkeit nach oben):

| Schicht | Ordner | Aufgabe |
| --- | --- | --- |
| codec | `src/codec` | APCI und ASDU kodieren und dekodieren; reine Funktionen auf Byte-Puffern, kein I/O, keine Allokation |
| apci | `src/apci` | Zustandsmaschine je Verbindung: Bytes und Zeit rein, Bytes und Ereignisse raus |
| session | `src/session` | Abläufe der gesteuerten Station: Prozessabbild, Generalabfrage, Zählerabfrage, Lesen, Test, Uhrzeit, Befehle; später Pufferung und Redundanzgruppen |
| transport | `src/transport` | TCP (später TLS), Verbindungsaufbau, Zeitbasis |
| API | `include/iec104/iec104.h`, `src/api` | C-Schnittstelle: Handles, Konfiguration, Callbacks |

Weil Codec und Zustandsmaschine kein I/O machen, sind sie vollständig ohne Netz und mit simulierter Zeit testbar.

## APCI

Eine APDU beginnt mit dem Startoktett 0x68, gefolgt vom Längenoktett und vier Steueroktetten. Die Länge zählt Steueroktette plus ASDU; sie ist höchstens 253, die APDU damit höchstens 255 Oktette und die ASDU höchstens 249 Oktette [WS].

| Format | Erkennung (Oktett 3) | Inhalt |
| --- | --- | --- |
| I | Bit 0 = 0 | N(S) und N(R), je 15 Bit, um ein Bit nach links geschoben; ASDU folgt |
| S | Bits 0–1 = 01 | nur N(R) |
| U | Bits 0–1 = 11 | genau eine Funktion: STARTDT act 0x07 / con 0x0B, STOPDT act 0x13 / con 0x23, TESTFR act 0x43 / con 0x83 |

Folgenummern zählen modulo 32768 und beginnen nach dem Verbindungsaufbau bei 0 [VUT]. Die gesteuerte Station lauscht auf TCP-Port 2404 [IANA], [WS].

## Verbindung und Timer

| Parameter | Bedeutung | Default | Bereich |
| --- | --- | --- | --- |
| t0 | Timeout Verbindungsaufbau | 30 s | 1–255 s |
| t1 | Timeout für Sende- oder Test-APDUs; Ablauf schließt die Verbindung | 15 s | 1–255 s |
| t2 | Quittung spätestens nach t2, wenn keine eigenen Daten anstehen; t2 < t1 | 10 s | 1–255 s |
| t3 | Testrahmen nach langer Ruhe | 20 s | 1 s – 48 h |
| k | höchstens k unquittierte gesendete I-Rahmen | 12 | 1–32767 |
| w | Quittung spätestens nach w empfangenen I-Rahmen; Empfehlung w ≤ 2/3 k | 8 | 1–32767 |

Quelle der Werte: Interoperabilitätsliste der Norm, öffentlich wiedergegeben in [ORA].

Abweichungen aus der Praxis, die die Library als Parameter abdeckt:

- Einige Netzbetreiber fordern lange Timer, z. B. t1 = 250 s, t2 = 240 s, t3 = 255 s. Deshalb gilt der volle Bereich ohne künstliche Grenzen, und die Empfehlungen t2 < t1 und w ≤ 2/3 k werden standardmäßig nur gewarnt (E2 in `docs/05_Normfragen_und_Festlegungen.md`).
- Einige Gegenstellen schalten den Verbindungstest mit t3 = 0 ab; das ist im toleranten Modus erlaubt (E3).
- Hersteller interpretieren t1 unterschiedlich, manche nur als Antwortzeit für U-Rahmen. Die Library überwacht mit t1 I-Rahmen und U-Rahmen act (`docs/03_Quellen_APCI.md`, R10).

## ASDU-Kopf und ASDU-Profil

Datenkopf im IEC-104-Profil (6 Oktette) [WS], [VUT]:

| Feld | Länge | Inhalt |
| --- | --- | --- |
| Typkennung | 1 | siehe „Typkennungen“ |
| VSQ | 1 | Bit 7 SQ, Bits 0–6 Anzahl der Objekte (0–127) |
| COT | 2 | Bits 0–5 Ursache, Bit 6 P/N (negativ), Bit 7 T (Test); zweites Oktett Herkunftsadresse (0 = ungenutzt) |
| CA | 2 | gemeinsame Adresse: 0 ungenutzt, 1–65534 Station, 65535 Broadcast |

Jedes Informationsobjekt beginnt mit einer 3-Oktett-IOA; IOA 0 steht für systembezogene Befehle wie die Generalabfrage. Alle Mehroktettwerte werden mit dem niederwertigen Oktett zuerst übertragen (in Interoperabilitätslisten „Mode 1“).

SQ = 0: jedes Objekt hat seine eigene IOA. SQ = 1: nur das erste Objekt trägt eine IOA, die folgenden haben fortlaufende Adressen.

ASDU-Profil (Parameter, `AsduProfile`):

| Parameter | Default | Bereich | Zweck |
| --- | --- | --- | --- |
| Längen COT / CA / IOA | 2 / 2 / 3 | 1–2 / 1–2 / 1–3 | Gegenstellen außerhalb der Norm (Feldlängen wie bei IEC 101) |
| maximale ASDU-Länge | 249 | 1–249 | Gegenstellen mit kleinerem Maximum; der Sender teilt lange Objektlisten auf |
| strikter Modus | an | an / tolerant | Abweichungen bewusst zulassen (E1) |

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

1 zyklisch, 2 Hintergrund, 3 spontan, 4 initialisiert, 5 Abfrage, 6 Aktivierung, 7 Bestätigung der Aktivierung, 8 Abbruch, 9 Bestätigung des Abbruchs, 10 Beendigung der Aktivierung, 11 Rückmeldung Fernbefehl, 12 Rückmeldung Ortsbefehl, 13 Dateiübertragung, 20 Generalabfrage, 21–36 Gruppen 1–16, 37 allgemeine Zählerabfrage, 38–41 Zählergruppen 1–4, 44 unbekannte Typkennung, 45 unbekannte Übertragungsursache, 46 unbekannte gemeinsame Adresse, 47 unbekannte IOA, 48–63 privat [WS].

Welche Ursache für welchen Typ zulässig ist, regelt Abschnitt 9.5 der Norm; die Tabelle ist angelegt, aber noch leer (N3).

## Informationselemente

- Qualitätsbits: IV 0x80 (ungültig), NT 0x40 (nicht aktuell), SB 0x20 (ersetzt), BL 0x10 (blockiert), bei QDS zusätzlich OV 0x01 (Überlauf) [WS].
- DIQ: 0 und 3 unbestimmt, 1 AUS, 2 EIN.
- NVA: vorzeichenbehaftet 16 Bit, Wert = Rohwert / 32768. SVA: vorzeichenbehaftet 16 Bit. R32: IEEE 754 einfache Genauigkeit, niederwertiges Oktett zuerst.
- CP56Time2a, 7 Oktette: Millisekunden einschließlich Sekunden (0–59999), Minute mit IV-Bit, Stunde mit SU-Bit (Sommerzeit), Tag mit Wochentag (3 Bit), Monat, Jahr 0–99 [WS].

Bit-Layouts aller Elemente: `src/codec/elements.hpp`. Jede Bitposition ist durch die Referenz-ASDUs bestätigt.

## Zeit

Die Norm legt nicht fest, ob Zeitmarken UTC oder Ortszeit sind; das ist ein Punkt der Interoperabilitätsliste. Der Codec trägt deshalb keine Zeitzone und führt IV, SU und den optionalen Wochentag ausdrücklich. Die Zeitbasis (UTC, fester Offset, Ortszeit mit SU-Bit) wird ein Parameter der Session-Schicht (N13).

## Sicherheit

IEC 104 selbst bietet weder Integrität noch Authentifizierung noch Verschlüsselung. Vorgesehen ist TLS nach IEC 62351-3 Ed. 2 (2023) für TLS 1.2 und 1.3, mit Regeln zu Cipher-Suites, Resumption, Renegotiation und Key Update [62351]. Den Port überlässt die Norm dem Anwendungsprotokoll; IANA registriert TCP 19998 als „iec-104-sec“ [IANA]. Die Library bindet TLS über OpenSSL 3.x hinter einer schmalen Backend-Schnittstelle an (in Arbeit).

## Designfolgerungen

| Thema | Folgerung |
| --- | --- |
| Strikt und tolerant | Strikt ist Default (Normverhalten). Tolerant erlaubt CP24-Typen, ASDUs ohne Objekte und t3 = 0 |
| Nie toleriert | Länge über 253, mehr als ein U-Funktionsbit, falsche N(S), N(R) außerhalb des Fensters: das sind Fehler in der Rahmenstruktur oder der Datenintegrität |
| Dekodieren | verlustfrei, auch reservierte Bits bleiben erhalten; Kodieren schneidet nie ab (E5) |
| Zustandsmaschine | ohne I/O, mit von außen gelieferter Zeit; dadurch vollständig mit simulierter Zeit testbar |
| Tests | Unit-Tests mit Referenzbytes einer fremden Implementierung, Fuzzing von Codec und Zustandsmaschine, Selbsttest über TCP, Gegenprobe mit lib60870 als externer Gegenstelle |

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
| [LIB] | lib60870 (nur externe Testgegenstelle, weder gelinkt noch Code übernommen): https://github.com/mz-automation/lib60870 |
