# IEC 104: Profil konfigurierbar machen, strikt testen

> **Hinweis (25.11.2026):** Die Aussagen zu Rust (rustls, `Strictness::…`, `TimeBase::…`, Backend-Trait, cargo-fuzz) sind überholt. Entschieden sind C++17 mit C-API und OpenSSL 3.x als TLS-Backend, siehe `intern/02_Konzept_IEC104_Library.md` und `docs/05_Normfragen_und_Festlegungen.md`.

Gültig ist IEC 60870-5-104 Ed. 2.1 (2016-06), also Ed. 2 von 2006 plus Amendment 1 von 2016, ergänzt um die einseitige Berichtigung COR1:2023. Ein Projekt für eine Edition 3 ließ sich nicht belegen ([IEC Webstore 87194](https://webstore.iec.ch/publication/87194); [VDE-Verlag Ed. 2.1](https://assets.vde-verlag.de/iec-normen/preview-pdf/info_iec60870-5-104%7Bed2.1%7Db.pdf)). Das Protokoll ist schmal: ein 6 Oktette langer APCI, 15 Bit breite Folgenummern, drei Rahmenformate, feste Feldlängen (COT 2, CA 2, IOA 3) und darüber die Anwendungsschicht von IEC 60870-5-101. Die Interop-Probleme entstehen fast nie durch Normverstöße. Sie entstehen durch Freiheitsgrade, die jede Gegenstelle in ihrer Interoperabilitätsliste anders belegt: Zeitbasis und SU-Bit, Timer (WEMAG setzt t1 = 250 s statt 15 s), Befehlsmodell (Direktbefehl oder SBO, Typ 45 oder 58, ACTTERM ja oder nein) und Redundanzkonzept. Für eine Rust-Library folgt daraus: Der Kern muss strikt normkonform sein, und jede bekannte Abweichung gehört als expliziter Profilparameter in die Konfiguration, nicht in Sonderlogik. Sicherheit ist nicht Teil von IEC 104. TLS nach IEC 62351-3 Ed. 2 (2023) ist der pragmatische erste Schritt. rustls deckt TLS 1.2 und 1.3 ab, unterstützt aber bewusst keine Renegotiation, die Ed. 2 für TLS 1.2 regelt. Zu Aucoteam als Hersteller einer Prozessabkopplung fand die Recherche keine öffentliche Quelle. Die Schnittstelle zu dieser Gegenstelle bleibt deshalb vollständig offen und muss über deren Interop-Liste geklärt werden.

## Ed. 2.1 plus COR1:2023 ist der einzige sinnvolle Zielstand

Ed. 2 (2006) hat Ed. 1 (2000) ersetzt. Die Änderungen betrafen die Abläufe, die Interoperabilität und neu Abschnitt 10 zu redundanten Verbindungen ([Ed. 2 Preview](https://www.en-standard.eu/publicdoc/iec_previews/115350.pdf)). AMD1:2016 bringt **keine neuen Typkennungen und keine Sicherheitsinhalte**, aber vier Punkte, die direkt in Code münden ([AMD1 Sample](https://cdn.standards.iteh.ai/samples/22910/722bcd41d12f48898e3b0b642418b49d/IEC-60870-5-104-2006-AMD1-2016.pdf)):

| AMD1-Änderung | Konsequenz im Code |
|---|---|
| 5.1: empfangenes N(R) quittiert alle I-APDUs mit N(S) < N(R) | kumulative Quittung, Sendepuffer bis N(R) freigeben |
| 6: zeitgestempelte Befehle (58-64, 100, 101, 103, 105, 107, 110-113) jenseits der maximalen Verzögerung werden weder ausgeführt noch gespiegelt | stilles Verwerfen, kein ACTCON; Verzögerung als Parameter |
| Bild 30: C_TS_TA_1 (107) mit IOA = 0 | Validierung |
| 9.5: COT 44 nur für nicht unterstützte Typkennungen; schattierte Felder „shall not be used“ | Fehlerpfade sauber trennen |

Die IEC nennt 2026 als Stabilitätsdatum. Das bedeutet nur, dass TC57 über Bestätigung, Überarbeitung oder Rückzug entscheidet, und ist kein Beleg für eine Ed. 3 ([IEC Webstore 87194](https://webstore.iec.ch/publication/87194)). **Offen:** Inhalt von COR1:2023, Ausgabedaten der DIN EN 60870-5-104 (VDE 0809-5-104), Stand eines möglichen TC57-Projekts. Den Normtext selbst hat die Recherche nicht eingesehen; viele Verhaltensdetails stammen aus Implementierungen (Wireshark, lib60870) und sind vor dem Codieren am gekauften Text zu prüfen.

IEC 104 ist ein Begleitstandard: Er nimmt die Anwendungsschicht von IEC 60870-5-101 und ersetzt die FT1.2-Sicherungsschicht durch TCP/IP. Die Linkadresse entfällt, COT und CA haben fest 2 Oktette, die IOA fest 3 Oktette ([Wireshark packet-iec104.c](https://raw.githubusercontent.com/wireshark/wireshark/master/epan/dissectors/packet-iec104.c)). Alle Mehrbytewerte sind Little Endian („Mode 1“) ([Kepware](https://support.ptc.com/help/kepware/drivers/en/kepware/drivers/IEC608705104/IEC_60870-5-104_Interoperability_Guide.html)). Für die Library heißt das: eine ASDU-Schicht mit Längenparametern (cot_len, ca_len, ioa_len), im 104-Profil fest auf (2, 2, 3). Abschnitt 8 der Norm ergänzt Typen, die es im ursprünglichen 101 nicht gab: 58-64 (Befehle mit CP56Time2a), 107 C_TS_TA_1 und 127 F_SC_NB_1 ([Ed. 2 Preview](https://www.en-standard.eu/publicdoc/iec_previews/115350.pdf)).

## Sechs Oktette APCI tragen die gesamte Flusssteuerung

Ein APDU beginnt mit 0x68, gefolgt von einem Längenoktett und vier Steueroktetten. Die Länge zählt Steueroktette plus ASDU; **maximal 253**, damit **ASDU maximal 249 Oktette** ([Wireshark](https://raw.githubusercontent.com/wireshark/wireshark/master/epan/dissectors/packet-iec104.c); [Beckhoff](https://infosys.beckhoff.com/content/1033/tf6500_tc3_iec60870_5_10x/983963275.html)). Interop-Listen erlauben, das Maximum systemseitig zu reduzieren ([IPCOMM](https://www.ipcomm.de/pstack/IEC104Slave/iec104slave-interoperability.pdf)). Die Library muss also beim Senden eine konfigurierbare Obergrenze einhalten und ASDUs entsprechend aufteilen.

| Format | Erkennung (Bits 0-1 von Oktett 1) | Inhalt | Bytes |
|---|---|---|---|
| I | Bit 0 = 0 | N(S) Bits 1-15, N(R) Bits 17-31, ASDU folgt | `68 LL ns_lo ns_hi nr_lo nr_hi ASDU` (Zähler um 1 Bit nach links geschoben) |
| S | 01 | nur N(R), kein ASDU | `68 04 01 00 nr_lo nr_hi` |
| U | 11 | Funktionsbits, kein ASDU | `68 04 XX 00 00 00` |

Die U-Funktionen in Oktett 1: **STARTDT act 0x07, con 0x0B; STOPDT act 0x13, con 0x23; TESTFR act 0x43, con 0x83** ([Matoušek, FIT VUT Brno](https://www.fit.vut.cz/research/publication-file/11570/TR-IEC104.pdf), dessen eigene Tabelle 0x0B falsch beschriftet; konsistent mit den Wireshark-Masken). Folgenummern laufen modulo 32768.

Die normativen Defaults aus der Interop-Vorlage ([IPCOMM](https://www.ipcomm.de/pstack/IEC104Slave/iec104slave-interoperability.pdf); identisch bei [Kepware](https://support.ptc.com/help/kepware/drivers/en/kepware/drivers/IEC608705104/IEC_60870-5-104_Interoperability_Guide.html) und [GE B30](https://www.manualslib.com/manual/1371298/Ge-B30.html?page=590)):

| Parameter | Bedeutung | Default | Bereich |
|---|---|---|---|
| t0 | Timeout Verbindungsaufbau | 30 s | 1-255 s |
| t1 | Timeout für Sende- oder Test-APDUs; Ablauf führt zum Schließen der TCP-Verbindung | 15 s | 1-255 s |
| t2 | Quittung spätestens nach t2 ohne eigene Daten; t2 < t1 | 10 s | 1-255 s |
| t3 | Testrahmen bei langer Ruhe | 20 s | empfohlen 1 s bis 48 h |
| k | max. unquittierte gesendete I-APDUs | 12 | 1-32767 |
| w | Quittung spätestens nach w empfangenen I-APDUs; w ≤ 2/3 k | 8 | 1-32767 |
| Port | TCP | 2404 | konfigurierbar |

Die Praxis weicht davon erheblich ab. WEMAG Netz (Regelzone 50Hertz) fordert in Version 1.8 vom 24.07.2026 **t1 = 250 s, t2 = 240 s, t3 = 255 s** bei k = 12, w = 8 ([WEMAG](https://www.wemag-netz.de/sites/default/files/2026-07/ergaenzende-anschlussbedingungen-ueber-iec-60870-5-104-v01.pdf)). Svenska kraftnät nutzt t3 = 30 s; t3 = 0 schaltet die Überwachung ab ([SvK TR02-04-04E](https://www.svk.se/4ab5fb/siteassets/aktorsportalen/entreprenorer-i-elnatet/tekniska-riktlinjer/tr02/tr02-04-04e-2-2022-10-25.pdf)). Hersteller interpretieren die Timer unterschiedlich: Beckhoff definiert t1 nur als Antwortzeit für U-Rahmen und t0 als Zyklus für Verbindungsversuche ([Beckhoff](https://infosys.beckhoff.com/content/1033/tf6500_tc3_iec60870_5_10x/983963275.html)), Weidmüller setzt t0 = 15 s ([Weidmüller](https://support.weidmueller.com/online-documentation/latest/322745/files/Interoperability60870/IEC60870-5-104/IEC60870-5-104.html)). Daraus folgt: Timer als Struktur mit Normdefaults, voller Bereich 1-255 s ohne künstliche Kappung, Plausibilitätsprüfung t2 < t1 und w ≤ 2/3 k als Warnung im toleranten Modus und als Fehler im strikten Modus.

Das genaue Zustandsverhalten von Abschnitt 5 war nicht im Normtext zugänglich. **Nach gängiger Implementierungspraxis, aber unbelegt:** Die Unterstation sendet nach dem TCP-Aufbau keine I-Rahmen vor STARTDT act; STOPDT con folgt erst, wenn alle eigenen I-Rahmen quittiert sind; TESTFR act wird auch im STOPDT-Zustand beantwortet; ein N(R) außerhalb des Fensters (letzte Quittung bis V(S)) oder ein N(S)-Sprung führt zum Verbindungsabbruch. lib60870 bestätigt nur, dass t1-Ablauf die Verbindung schließt und der Sender nach k unquittierten APDUs stoppt ([lib60870 User Guide](https://github.com/mz-automation/lib60870/blob/master/user_guide.adoc)). Diese Punkte gehören vor Implementierung gegen den Normtext und gegen lib60870 als Gegenstelle geprüft.

## ASDU-Kopf, Typkennungen und COT: die CP24-Frage ist der erste Toleranzschalter

Der Datenkopf hat in 104 sechs Oktette: Typkennung (1), VSQ (1; SQ-Bit plus Anzahl 0-127), COT (2; Ursache in 0x3F, P/N 0x40, Test 0x80, zweites Oktett Herkunftsadresse, 0 wenn ungenutzt), CA (2; 0 ungenutzt, 1-65534 Station, 65535 Broadcast). Jedes Objekt trägt eine 3-Oktett-IOA; IOA 0 steht für systembezogene Befehle ([Wireshark](https://raw.githubusercontent.com/wireshark/wireshark/master/epan/dissectors/packet-iec104.c); [Matoušek](https://www.fit.vut.cz/research/publication-file/11570/TR-IEC104.pdf); [IPCOMM](https://www.ipcomm.de/pstack/IEC104Slave/iec104slave-interoperability.pdf)).

| Gruppe | Typkennungen | Hinweis |
|---|---|---|
| Melderichtung ohne Zeit | 1, 3, 5, 7, 9, 11, 13, 15, 20, 21 | 13 = Float, 9/11 normiert/skaliert |
| Melderichtung mit CP56Time2a | 30-40 | einzig zulässige Zeitmarkenvariante in 104 laut Kepware |
| nur 101 (CP24Time2a) | 2, 4, 6, 8, 10, 12, 14, 16, 17, 18, 19 | Kepware: in 104 unzulässig; Beckhoff bietet sie trotzdem an |
| Befehle ohne Zeit | 45-51 | Einzel-, Doppel-, Stufen-, Sollwert-, Bitmusterbefehl |
| Befehle mit CP56Time2a | 58-64 | 104-Ergänzung |
| System | 70 (End of Init), 100 (GA), 101 (Zählerabfrage), 102 (Lesen), 103 (Uhrzeit), 104, 105 (Reset), 106, 107 | 107 = Testbefehl mit Zeitmarke |
| Parameter | 110-113 | |
| Dateitransfer | 120-127 | 127 QueryLog |
| privat | 136-255 | EWE nutzt 201/202 |
| Security (TS 60870-5-7) | 41, 81-87, 90-95 | nicht Teil von 104 |

Quellen: Elementlängen je Typ aus [Wireshark](https://raw.githubusercontent.com/wireshark/wireshark/master/epan/dissectors/packet-iec104.c); Zulässigkeit der Zeitmarken aus [Kepware](https://support.ptc.com/help/kepware/drivers/en/kepware/drivers/IEC608705104/IEC_60870-5-104_Interoperability_Guide.html), Gegenbeispiel [Beckhoff](https://infosys.beckhoff.com/content/1033/tf6500_tc3_iec60870_5_10x/984447883.html); EWE-Privattypen aus [EWE Netz 2026](https://www.ewe-netz.de/-/media/ewe-netz/downloads/20260812fernwirktechnische-anbindung-von-kundenanlagen-vom-01092026.pdf). Die Interop-Vorlage von IPCOMM trägt sogar noch die 101-Formulierung „entweder <2>...<19> oder <30>-<40>“ ([IPCOMM](https://www.ipcomm.de/pstack/IEC104Slave/iec104slave-interoperability.pdf)). Der verbatim-Beleg aus dem Normtext für den Ausschluss der CP24-Typen fehlt. Die Library sollte sie im strikten Modus ablehnen und im toleranten Modus dekodieren können.

Die wichtigsten COT-Werte: 1 zyklisch, 2 Hintergrund, 3 spontan, 4 initialisiert, 5 Abfrage, 6 Aktivierung, 7 ACTCON, 8 Deaktivierung, 9 DEACTCON, 10 ACTTERM, 11 Rückmeldung Fernbefehl, 12 Rückmeldung Ortbefehl, 13 Datei, 20 Generalabfrage, 21-36 Gruppen 1-16, 37 Zählerabfrage allgemein, 38-41 Zählergruppen 1-4, **44-47 unbekannte Typkennung / COT / CA / IOA**, 48-63 privat ([Wireshark](https://raw.githubusercontent.com/wireshark/wireshark/master/epan/dissectors/packet-iec104.c)). Die vollständige Matrix „welche COT zu welchem Typ“ war nicht verfügbar; sie steht in Abschnitt 9.5 der Norm und muss von dort als Tabelle in den Code.

Informationselemente laut Wireshark-Masken: Qualitätsbits IV 0x80, NT 0x40, SB 0x20, BL 0x10, bei QDS zusätzlich OV 0x01. DIQ-Werte: 0 und 3 unbestimmt, 1 AUS, 2 EIN. NVA ist int16/32768, SVA int16, Float IEEE 754 Little Endian. **CP56Time2a** hat 7 Oktette: Millisekunden inklusive Sekunden (0-59999), Minute plus IV-Bit, Stunde plus SU-Bit, Tag plus Wochentag (3 Bit), Monat, Jahr 0-99 ([Wireshark](https://raw.githubusercontent.com/wireshark/wireshark/master/epan/dissectors/packet-iec104.c)). Die Norm legt **nicht** fest, ob die Zeit UTC oder Ortszeit ist; das ist ein Punkt der Interop-Liste. Der Zeittyp der Library darf also keine Zeitzone annehmen und muss IV, SU und optionalen Wochentag explizit führen.

Die Abläufe aus Abschnitt 7 folgen dem Muster Aktivierung, Bestätigung, Daten, Terminierung: Nach Initialisierung meldet die Unterstation M_EI_NA_1 mit COT 4 ([IPCOMM](https://www.ipcomm.de/pstack/IEC104Slave/iec104slave-interoperability.pdf)); SvK fordert dies nur nach lokaler RTU-Initialisierung, nicht nach jedem TCP-Neuaufbau ([SvK](https://www.svk.se/4ab5fb/siteassets/aktorsportalen/entreprenorer-i-elnatet/tekniska-riktlinjer/tr02/tr02-04-04e-2-2022-10-25.pdf)). Die Generalabfrage läuft nach üblicher Lesart als C_IC_NA_1 (COT 6, IOA 0, QOI 20), ACTCON, Daten mit COT 20, ACTTERM; die Sequenzdiagramme selbst sind **unbelegt** aus dem Normtext. AMD1 räumt ein, dass Initialisierungssequenzen je nach TCP-Stack „slightly“ abweichen dürfen ([AMD1](https://cdn.standards.iteh.ai/samples/22910/722bcd41d12f48898e3b0b642418b49d/IEC-60870-5-104-2006-AMD1-2016.pdf)), was einen toleranten Zustandsautomaten rechtfertigt.

## Redundanzgruppen: eine aktive Verbindung, der Ereignispuffer gehört zur Gruppe

Abschnitt 10 (neu seit Ed. 2) definiert mehrere TCP-Verbindungen zwischen Leitstelle und Unterstation, von denen genau eine per STARTDT Nutzdaten überträgt; die übrigen bleiben über TESTFR offen ([Ed. 2 Preview](https://www.en-standard.eu/publicdoc/iec_previews/115350.pdf)). Die normativen Details (Anzahl Verbindungen je Gruppe, Behandlung unquittierter I-Rahmen beim Umschalten) waren nicht zugänglich und sind **offen**. lib60870 bietet drei Servermodi: eine gemeinsame Gruppe mit einer Ereigniswarteschlange, die auch ohne verbundenen Client puffert; je Verbindung eine eigene Gruppe, bei der Ereignisse beim Abbruch verloren gehen; mehrere Gruppen mit Zuordnung über Client-IP ([lib60870](https://github.com/mz-automation/lib60870/blob/master/user_guide.adoc)).

Die Profile der Netzbetreiber zeigen die Spannweite. Pfalzwerke Netz: zwei Zentralstationen, eine aktive pro Gruppe, Umschaltung per STOPDT auf der alten und STARTDT auf der neuen Verbindung, danach neue GA ([Pfalzwerke](https://www.pfalzwerke-netz.de/pfalzwerke-netz/downloads/netz-anschliessen/spezifikation_fernwirkkommunikation_2026.pdf)). WEMAG: Meldungen an beide Gegenstellen, Befehle nur von der aktiven ([WEMAG](https://www.wemag-netz.de/sites/default/files/2026-07/ergaenzende-anschlussbedingungen-ueber-iec-60870-5-104-v01.pdf)). EWE: eine Verbindung je Netzanschlusspunkt ([EWE](https://www.ewe-netz.de/-/media/ewe-netz/downloads/20260812fernwirktechnische-anbindung-von-kundenanlagen-vom-01092026.pdf)). SvK: mindestens **4 Verbindungen je Gruppe** in der Leitstelle, mindestens **8 Gruppen** in der Unterstation, Puffer mindestens **100 Einträge**, bei Überlauf Löschen des ältesten Ereignisses plus Alarm ([SvK](https://www.svk.se/4ab5fb/siteassets/aktorsportalen/entreprenorer-i-elnatet/tekniska-riktlinjer/tr02/tr02-04-04e-2-2022-10-25.pdf)). Schneider Geo SCADA hielt die Standby-Verbindung früher nicht offen und musste nachbessern ([SE Geo SCADA](https://community.se.com/t5/Geo-SCADA-Knowledge-Base/IEC-60870-Driver-Improvements/ba-p/279024)); COPA-DATA dokumentiert Verbindungsverluste durch fehlerhafte Start/Stop-Implementierungen ([COPA-DATA KB](https://selfservice.copadata.com/portal/en/kb/articles/iec60870-5-104-drivers-master-and-slave-are-loosing-redundant-connection-because-of-errors-in-start-stop-procedure-implementations-19-7-2022)). Architekturfolge: Ereigniswarteschlange und Überlaufstrategie im Objekt `RedundancyGroup`, die Verbindung hält nur Fensterzustand (V(S), V(R), Quittungen, Timer).

## Die Interoperabilitätsliste ist der eigentliche Vertrag

Abschnitt 9 ist eine Checkliste, die beide Seiten ausfüllen: 9.1 Rolle, 9.2 Netzkonfiguration, 9.3 physikalische Schicht, 9.4 Sicherungsschicht, 9.5 Anwendungsschicht mit Typ/COT-Matrix und Feldlängen, 9.6 Grundfunktionen; in 104 zusätzlich Timer, k/w, Port und RFC-2200-Suite. Schwarze Felder sind im Begleitstandard nicht wählbar, schattierte nicht definiert ([Ed. 2 Preview](https://www.en-standard.eu/publicdoc/iec_previews/115350.pdf); [AMD1](https://cdn.standards.iteh.ai/samples/22910/722bcd41d12f48898e3b0b642418b49d/IEC-60870-5-104-2006-AMD1-2016.pdf)). Abzustimmen sind unter anderem: unterstützte Typen je Richtung, Stations- und Gruppenabfrage, Uhrzeitsynchronisation (Wochentag, SU-Bit), Befehlsübertragung (direkt, Select/Execute, ACTTERM bei Sollwerten, QU-Impulsarten, Überwachung der maximalen Befehlsverzögerung), Zählerabfragemodi A bis D, Parameterladen, Dateitransfer, Timer, k/w ([Kepware](https://support.ptc.com/help/kepware/drivers/en/kepware/drivers/IEC608705104/IEC_60870-5-104_Interoperability_Guide.html); [Weidmüller](https://support.weidmueller.com/online-documentation/latest/322745/files/Interoperability60870/IEC60870-5-104/IEC60870-5-104.html)).

Auch die Listen selbst sind fehlerhaft: Kepware markiert IOA mit 1, 2 oder 3 Oktetten als wählbar, obwohl 104 3 Oktette festlegt ([Kepware](https://support.ptc.com/help/kepware/drivers/en/kepware/drivers/IEC608705104/IEC_60870-5-104_Interoperability_Guide.html)); WinCC OA beschriftet Typ 70 mit „C_SC_NA_1“ ([WinCC OA](https://www.winccoa.com/documentation/WinCCOA/latest/en_US/Treiber_IEC/iec_104_compatibility.html)). Die Library sollte ihr eigenes Profil deshalb als Datenstruktur führen, die sich als Abschnitt-9-Tabelle ausgeben und maschinell gegen die Liste der Gegenstelle abgleichen lässt.

## Deutsche Netzbetreiber: viele VNB-Profile, kein öffentliches ÜNB-Profil

Die VDE/ETG/ITG-Studie (Modul C, 2016) ordnet IEC 101/104 dem Austausch „zwischen Netzleitsystem und Prozess“ zu und IEC 60870-6 (TASE.2/ICCP) dem Austausch zwischen Leitsystemen ([VDE-Studie](https://vde.com/resource/blob/2315798/0f1f7879daf64ba3d236d8f58c99acfb/vde-studie-sua---modul-c-netzleittechnik-data.pdf)). Ein öffentliches 104-Profil von **50Hertz, Amprion, TenneT oder TransnetBW** wurde nicht gefunden; ebenso keine Quelle, ob deutsche ÜNB zwischen Leitstellen 104 oder ICCP fahren. Die Studie der vier ÜNB zum Datenbedarf (Consentec/FGH 2016) nennt kein Protokoll ([Consentec/FGH](https://amprion.net/Dokumente/Dialog/Downloads/Studien/Notwendiger-Daten-und-Informationsbedarf/Consentec-FGH_4UeNB_Datenbedarf-EIN_GA_komplett.pdf)). Öffentlich sind vor allem VNB-Anschlussbedingungen für Kunden- und Erzeugungsanlagen:

| Punkt | WEMAG v1.8 (07/2026) | Pfalzwerke v1.0 (06/2026) | EWE v5.0 (09/2026) | SvK (2022, ÜNB SE) |
|---|---|---|---|---|
| Rolle | NLS-Frontend über Gateway-Router | Zentrale Client, RTU Server, Port 2404 | NB-Gateway Client, CA = 1 | konfigurierbar |
| Timer | t1 250, t2 240, t3 255 s | Anhang A nicht ausgewertet | nicht ausgewertet | 30/15/10/30 s |
| Melden | 13, 30, 31, 70 | 30, 31, 36 | 30, 31, 36, privat 201/202 | nur 30-40 |
| Befehle | 45, 46, 50, direkt, Sollwert mit ACTTERM, Verwerfen nach 20 s | 58, 59, 63, kein Dauerbefehl | 59, 63 | 45-51 oder 58-64, SBO 20 s |
| Zeit | NTP, Zeitbasis offen | NTP, Ortszeit mit SU-Bit, 100 ms Genauigkeit | Zeitbasis offen | NTP, UTC+1, SU = 0 |
| Redundanz | zwei Gegenstellen | Z1/Z2, STOPDT/STARTDT, neue GA | eine Verbindung | ≥ 4 je Gruppe, ≥ 8 Gruppen |

Quellen: [WEMAG](https://www.wemag-netz.de/sites/default/files/2026-07/ergaenzende-anschlussbedingungen-ueber-iec-60870-5-104-v01.pdf), [Pfalzwerke](https://www.pfalzwerke-netz.de/pfalzwerke-netz/downloads/netz-anschliessen/spezifikation_fernwirkkommunikation_2026.pdf), [EWE](https://www.ewe-netz.de/-/media/ewe-netz/downloads/20260812fernwirktechnische-anbindung-von-kundenanlagen-vom-01092026.pdf), [SvK](https://www.svk.se/4ab5fb/siteassets/aktorsportalen/entreprenorer-i-elnatet/tekniska-riktlinjer/tr02/tr02-04-04e-2-2022-10-25.pdf). N-ERGIE Netz nutzt für Kundenanlagen noch IEC 101 ([N-ERGIE](https://www.n-ergie-netz.de/public/remotemedien/media/nng/produkte_und_dienstleistungen_2/netzanschluss_2/recht_technik/Beschreibung_der_fernsteuertechnischen_Anbindung_von_Kundenanlagen_ueber_IEC_60870-5-101.pdf)).

Mehrere VNB setzen ein eigenes Gateway als 104-Client vor die Anlage, mit isoliertem Kleinnetz (EWE: festes /29, kein paralleles WAN) und Abweisung unbekannter Absender (Pfalzwerke) ([EWE](https://www.ewe-netz.de/-/media/ewe-netz/downloads/20260812fernwirktechnische-anbindung-von-kundenanlagen-vom-01092026.pdf); [Pfalzwerke](https://www.pfalzwerke-netz.de/pfalzwerke-netz/downloads/netz-anschliessen/spezifikation_fernwirkkommunikation_2026.pdf)). Das ist funktional eine Prozessabkopplung, heißt in den Quellen aber nicht so; eine BSI- oder BDEW-Definition des Begriffs wurde nicht gefunden. **Aucoteam:** Die Website beschreibt einen Berliner Engineering-Dienstleister (Testing, Automation, Software, Fertigung, Branche Energie und Umwelt), ohne Bezug zu Netzleittechnik, IEC 104, Gateways oder Datendioden ([aucoteam.de](https://www.aucoteam.de)). Ob das Produkt eine echte Einweg-Diode, einen bidirektionalen Protokollbruch oder einen 104-Proxy darstellt, ist **offen**. Fachlich gilt: Über eine unidirektionale Diode läuft 104 nicht durchgängig, weil TCP, S-Quittungen, STARTDT und Befehle einen Rückkanal brauchen. Dann terminieren auf beiden Seiten Proxys das Protokoll (Schlussfolgerung, keine Quelle). Für die Library heißt das: Server- und Client-Rolle müssen gleichwertig ausgebaut sein, weil eine Abkopplung typischerweise beide Rollen in einem Prozess braucht.

### Interop-Fallstricke mit Belegen

| Fallstrick | Beleg | Library-Antwort |
|---|---|---|
| Zeitbasis UTC/Ortszeit, SU-Bit | SvK UTC+1 SU = 0 gegen Pfalzwerke Ortszeit mit SU; Geo SCADA musste Ortszeit je Kanal nachrüsten ([SE](https://community.se.com/t5/Geo-SCADA-Knowledge-Base/IEC-60870-Driver-Improvements/ba-p/279024)) | Zeitbasis pro Gegenstelle: UTC, feste Offsetzone, Ortszeit mit SU-Auswertung |
| Zeitqualität | Geo SCADA wertete IV/substituted erst nachträglich aus; WinCC OA ergänzt fehlende Zeit aus Rechnerzeit ([WinCC OA](https://www.winccoa.com/documentation/WinCCOA/latest/en_US/Treiber_IEC/iec_104_compatibility.html)) | IV immer durchreichen; Ersatzzeit nur markiert |
| Verspätete Zeitbefehle | AMD1 und SvK: Verwerfen ohne Bestätigung | Uhrabweichung loggen; Max-Delay konfigurierbar; Diagnosezähler |
| SBO vs. direkt | SvK: Direktbefehl auf SBO-Punkt gibt negatives ACTCON | Befehlsmodell je IOA |
| ACTTERM bei Sollwerten | WEMAG ja, WinCC OA nein | Flag „expect ACTTERM“ mit eigenem Timeout |
| Befehls-Timeouts | Geo SCADA hatte nur globales 60-s-Timeout | getrennte Timeouts Select/Execute/Term |
| Mehrere CAs | Geo SCADA: Ausfall einer CA traf alle | Zustand je CA isolieren |
| Timer | WEMAG t1 250 s | Bereich 1-255 s ohne Kappung |
| Standby-Verbindung | Geo SCADA hielt sie nicht offen | TESTFR auf Standby-Verbindungen |

Für COT 44-47 in der Praxis, fehlendes GA-ACTTERM und Zeitmarken mit Wert 0/1970 fand die Recherche keine dokumentierten Fälle. Ein Analyse-Tool nennt als typische Fehlerbilder fehlende STARTDT con, verspätete S-Quittungen, driftende Folgezähler und ausbleibende TESTFR con ([ARIEC60870](https://masarray.github.io/ARIEC60870/iec104-sequence-counter-mismatch.html), Herausgeber unbekannt).

## Sicherheit: TLS nach 62351-3 zuerst, rustls hat eine Renegotiation-Lücke

IEC 104 schließt Sicherheit ausdrücklich aus ([Ed. 2 Preview](https://www.en-standard.eu/publicdoc/iec_previews/115350.pdf)). **IEC 62351-3 Ed. 2.0 (2023-06)** ist ein eigenständiges TLS-Profil für TLS 1.2 und 1.3, mit Pflicht-Ciphersuites für TLS 1.2, Regeln zu Resumption und Renegotiation (7.4.4, 7.4.5, RFC 5746 in 7.5.2), CRL/OCSP-Revocation, OCSP-Stapling und Session Key Update für TLS 1.3 (8.4) ([VDE-Preview 62351-3](https://assets.vde-verlag.de/iec-normen/preview-pdf/info_iec62351-3%7Bed2.0%7Db.pdf)). Den Port überlässt die Norm dem Anwendungsprotokoll; IANA registriert **TCP 19998 als „iec-104-sec“** ([IANA](https://www.iana.org/assignments/service-names-port-numbers?search=19998)). **IEC TS 60870-5-7 Ed. 2.0 (2025-03-18)** ersetzt die Ausgabe 2013, bildet IEC 62351-5:2023 (Authentisierung auf Anwendungsebene) auf 101/104 ab, ist auf 62351-3:2023 und 104:2016 abgestimmt und bringt RBAC nach 62351-8 ([IEC Webstore 87773](https://webstore.iec.ch/en/publication/87773)). Die dafür zusätzlichen Typkennungen 41, 81-87, 90-95 und COT 14-16 sind in Wireshark hinterlegt ([Wireshark](https://raw.githubusercontent.com/wireshark/wireshark/master/epan/dissectors/packet-iec104.c)). Gegenstellen für Ed. 2 sind kaum belegbar: lib60870 bietet Secure Authentication nur für die Ausgabe 2013 als kommerzielles Add-on ([lib60870](https://github.com/mz-automation/lib60870)).

Regulatorisch gilt seit **6. Dezember 2025 das NIS2UmsuCG**. Der neue IT-Sicherheitskatalog nach §5c EnWG ist laut BNetzA noch „geplant“; bis dahin gelten die Kataloge nach §11 Abs. 1a/1b EnWG alter Fassung, und Systeme zur Angriffserkennung müssen weiterlaufen ([BNetzA](https://www.bundesnetzagentur.de/DE/Fachthemen/ElektrizitaetundGas/Versorgungssicherheit/IT_Sicherheit/Festlegung_5cEnWG/artikel.html)). Das **BDEW/OE/VSE-Whitepaper 3.0 (30.09.2024)** sagt ausdrücklich, dass IEC 101/104 ohne Zusatzmaßnahmen keine Integrität, Authentisierung oder Verschlüsselung bieten, und empfiehlt IEC-62351-Erweiterungen, Protokollbrüche an Zonengrenzen, Prüfung der Quell-IP auch in der Zielkomponente und keine nicht vertrauenswürdigen selbstsignierten Zertifikate ([Whitepaper 3.0](https://www.bdew.de/media/documents/BDEW-OE-VSE-Whitepaper-3.0.pdf)). Dass das Risiko real ist, zeigt Industroyer2 (2022): ein reines 104-Werkzeug, das ausschließlich gültige Sequenzen nutzt (TESTFR, STARTDT, C_IC_NA_1, dann C_SC_NA_1/C_DC_NA_1 auf hartkodierte IOAs) ([Nozomi Networks](https://nozominetworks.com/blog/industroyer2-nozomi-networks-labs-analyzes-the-iec-104-payload)). Der Angriff braucht keine Parserlücke, sondern nur fehlende Authentisierung.

rustls 0.23.46 unterstützt TLS 1.2 und 1.3, nur AEAD-Suites mit ECDHE, Resumption, gegenseitige Authentisierung, CRLs für Client-Zertifikate und OCSP-Stapling als Server. **Renegotiation ist ausdrücklich nicht unterstützt**, ebenso Suites ohne Forward Secrecy ([rustls Manual](https://docs.rs/rustls/latest/rustls/manual/_04_features/index.html); [WebPkiClientVerifier](https://docs.rs/rustls/latest/rustls/server/struct.WebPkiClientVerifier.html)). Ob 62351-3 Ed. 2 Renegotiation fordert oder nur regelt und ob Tabelle 1 Pflicht-Suites ohne PFS oder mit CBC enthält, lässt sich ohne Normtext nicht entscheiden (**offen**). Konsequenz: TLS 1.3 bevorzugen (Key Update statt Renegotiation), bei TLS-1.2-Gegenstellen, die periodisch renegotiieren, einen geplanten Verbindungsneuaufbau als Ersatz anbieten und das TLS-Backend hinter einer Schnittstelle kapseln, damit notfalls ein OpenSSL-Backend einsetzbar ist. Clientseitige OCSP-Prüfung und Server-CRLs in rustls wurden nicht verifiziert.

## Konformität: IEC TS 60870-5-604 prüft, garantiert aber keine Interop

**IEC TS 60870-5-604 Ed. 2.0 (2016-06)** ist die Konformitätsprüfspezifikation für 104; sie bezieht sich auf 104:2006 Abschnitt 9 als PICS und zieht AMD1 für Mehrdeutigkeiten heran. Sie betont selbst, dass sie **keine Interoperabilität garantiert**. Die Tabellen decken Konfiguration, Transportebene, Datenkopf, ASDUs je Richtung, Grundfunktionen (Initialisierung, redundante Verbindungen in Tabelle 13, GA, Uhrzeit, Befehle, Zähler, Parameter, Dateitransfer) sowie Negativ- und PIXIT-Tests ab; die Prüfung beginnt mit der Stationsinitialisierung ([VDE-Preview 604](https://assets.vde-verlag.de/iec-normen/preview-pdf/info_iects60870-5-604%7Bed2.0.RLV%7Den.pdf)). Ein formales Zertifizierungsprogramm wie bei IEC 61850 ist nicht belegt; DNV (ehemals KEMA) bietet Konformitätsprüfungen mit den Werkzeugen UniGrid SA und UniGrid telecontrol für 101/104 und 62351 an ([DNV](https://www.dnv.com/services/iec-60870-5-conformance-testing-59791)). Für Secure Authentication existiert eine eigene Prüfspezifikation ([BSI Knowledge](https://knowledge.bsigroup.com/products/power-systems-management-and-associated-information-exchange-data-and-communications-security-conformance-test-cases-for-iec-ts-62351-5-and-iec-ts-60870-5-7)).

| Werkzeug | Zweck | Lizenz |
|---|---|---|
| lib60870 (MZ Automation) | Referenz-Gegenstelle, TLS über mbedtls 2.28 | GPLv3/kommerziell ([GitHub](https://github.com/mz-automation/lib60870)) |
| Triangle MicroWorks Test Harness | 62351-3 TLS und Secure Authentication beidseitig | kommerziell ([TMW](https://products.trianglemicroworks.com/products/testing-and-configuration-tools/test-harness-pages/iec-60870-5-secure-authentication)) |
| DNV UniGrid | Konformitätsprüfung 101/104/62351 | kommerziell ([DNV](https://www.dnv.com/services/iec-60870-5-conformance-testing-59791)) |
| QTester104 | Client/Testwerkzeug | Open Source ([SourceForge](https://sourceforge.net/p/qtester104/code)) |
| Defensics IEC-104 | Fuzzing Client und Server | kommerziell ([Black Duck](https://www.blackduck.com/fuzz-testing/defensics/protocols/iec104-server.html)) |
| EPF (Fraunhofer FKIE) | protokollbewusstes Fuzzing, nur Modul iec104, Autor sucht Mitwirkende für Rust-Neuschreibung | GPL-2.0 ([GitHub](https://github.com/fkie-cad/epf)) |
| Wireshark-Dissector | Trace-Analyse, inkl. Typen aus TS 60870-5-7 | Open Source ([Quelltext](https://raw.githubusercontent.com/wireshark/wireshark/master/epan/dissectors/packet-iec104.c)) |

lib60870 als externer Testpartner berührt die GPL nicht, solange nicht gelinkt wird. Ob eine an AMD1 angepasste 604-Ausgabe oder eine TLS-Prüfspezifikation für 104 existiert, ist **offen**.

## Konsequenzen für das Library-Design

| Thema | Entscheidung | Begründung |
|---|---|---|
| Normstand | Ed. 2.1 + COR1:2023; AMD1-Regeln (kumulative Quittung, Max-Delay-Verwerfen, IOA 0 bei 107, COT 44 nur für Typen) fest eingebaut | AMD1 |
| Strikt/tolerant | `Strictness::{Strict, Lenient}` je Verbindung; strikt für Tests nach 604, tolerant für Feld | Beckhoff-CP24-Typen, fehlerhafte Interop-Listen, AMD1-Toleranz bei Init-Sequenzen |
| Was tolerant erlaubt | CP24-Typen dekodieren, unbekannte private Typen (136-255) an Callback, verletzte Plausibilitäten t2 < t1 / w ≤ 2/3 k nur warnen | EWE 201/202, Hersteller-Abweichungen |
| Was nie tolerant ist | N(R) außerhalb des Fensters, N(S)-Sprung, Länge > 253, mehrere U-Bits | Datenintegrität (Normtext zu prüfen) |
| Timer | Struktur mit Defaults 30/15/10/20 s, k 12, w 8; Bereich 1-255 s; t3 = 0 als „aus“ optional | WEMAG 250 s, SvK t3 = 0 |
| Zeitbasis | `TimeBase::{Utc, FixedOffset, LocalWithSu}` pro Gegenstelle; CP56Time2a-Typ ohne Zeitzonenannahme mit IV, SU, Wochentag | SvK, Pfalzwerke, Geo SCADA |
| ASDU-Schicht | parametrisierte Feldlängen, 104-Profil (2,2,3); Sendeobergrenze APDU konfigurierbar | Interop-Liste 9.5 |
| Befehle | Modell je IOA (direkt/SBO, Typ ohne/mit Zeit, ACTTERM erwartet), getrennte Timeouts, Max-Delay | SvK, WEMAG, Geo SCADA |
| Redundanz | `RedundancyGroup` besitzt Ereignispuffer (Größe, Überlaufstrategie, Alarm); mehrere Gruppen per IP-Zuordnung | lib60870, SvK |
| Rollen | Client und Server gleichwertig, beide im selben Prozess nutzbar | Prozessabkopplung als Proxy |
| Profil als Daten | eigenes Profil als Abschnitt-9-Tabelle exportierbar und gegen Fremdliste prüfbar | fehlerhafte Herstellerlisten |
| TLS | rustls, TLS 1.3 bevorzugt, Port-Default 19998 (TLS) bzw. 2404; Backend-Trait für OpenSSL; Neuaufbau statt Renegotiation; Trust-Store/CRL-Reload ohne Verbindungsverlust | 62351-3, rustls-Grenzen, 62351-9 |
| Abwehr | IP-Allowlist in der Komponente, Rate-Limit für Befehle, Log-Hooks für jede STARTDT- und Befehlssequenz | BDEW 3.0, Industroyer2 |
| Tests | Zustandsautomat sans-I/O, cargo-fuzz auf Parser, 604-Tabellen als Testkatalog, lib60870 als Gegenstelle | 604, EPF |

## Fazit

Der aufwendige Teil einer 104-Library ist nicht der Codec, sondern die Parametrierung. Drei deutsche VNB-Profile aus 2026 unterscheiden sich in Timern, Typwahl, Befehlsmodell, Zeitbasis und Redundanz; ein einziges Default-Profil trägt deshalb nirgends. Ein sans-I/O-Kern mit strikter Normlogik und einer explizit typisierten Profilschicht macht diese Unterschiede testbar statt zu verstecken. Der kritischste Einzelparameter ist die Zeitbasis: Sie entscheidet bei zeitgestempelten Befehlen darüber, ob ein Befehl ausgeführt oder still verworfen wird.

Vor dem ersten Code sind vier Punkte zu klären. Erstens den Normtext von 104 Ed. 2.1 (Abschnitte 5, 9.5, 10) und 62351-3:2023 (Tabelle 1, 7.4.5) kaufen, denn die Zustandsautomaten und Pflicht-Suites sind hier nur aus Sekundärquellen belegt. Zweitens von Aucoteam die Interoperabilitätsliste, die Rolle (Client, Server, beides), die Zeitbasis und die Aussage zu TLS anfordern; öffentlich ist dazu nichts. Drittens bei 50Hertz klären, welches Profil auf der Leitsystemseite gilt, da kein öffentliches ÜNB-Profil existiert. Viertens prüfen, ob die Gegenstelle TLS-1.2-Renegotiation verlangt, weil rustls das nicht kann.
