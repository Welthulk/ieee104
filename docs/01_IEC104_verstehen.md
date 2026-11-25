# IEC 60870-5-104 verstehen

Stand: 24.11.2026 · Udo

## Kurz gesagt

IEC 60870-5-104 (kurz IEC 104) ist das Standardprotokoll der Fernwirktechnik über TCP/IP. Damit tauschen Leitstelle und Unterstation (oder zwei Systeme) Prozessdaten aus: Meldungen und Messwerte kommen rein, Befehle und Sollwerte gehen raus.

- **Herkunft:** IEC 104 nimmt die Anwendungsschicht von IEC 60870-5-101 (seriell) und legt sie auf TCP/IP. Datentypen und Bedeutung sind identisch, nur der Transport ist neu.
- **Normstand:** IEC 60870-5-104:2006 + AMD1:2016, deutsch als DIN EN 60870-5-104 ([DIN Media](https://www.dinmedia.de/en/standard/din-en-60870-5-104-berichtigung-1/383629959)).
- **Verbreitung:** Vor allem in Europa und Asien Standard für Stationsleittechnik und Kopplungen zwischen Leitsystemen.
- **Charakter:** Signalorientiert. Es gibt Datenpunkte mit Adresse, Typ und Qualität, aber kein Objekt- oder Netzmodell (anders als IEC 61850 oder CIM).

## Rollen und Verbindung

Es gibt genau zwei Rollen, und die TCP-Richtung ist fest: Die steuernde Seite baut die Verbindung auf, die gesteuerte Seite lauscht auf Port 2404.

| Rolle (Norm) | Üblicher Name | TCP | Typische Aufgabe |
| --- | --- | --- | --- |
| Controlling Station | Master, Client | verbindet sich | Leitsystem: empfängt Daten, sendet Befehle |
| Controlled Station | Slave, Server, RTU | lauscht auf 2404 | Unterstation oder Gateway: liefert Daten, führt Befehle aus |

- **Daten fließen in beide Richtungen.** Die Unterstation sendet spontan, ohne gepollt zu werden. Das ist der große Unterschied zu Modbus.
- **Mehrere Verbindungen:** Ein Server kann mehrere Clients bedienen. Bei Redundanz (Edition 2) bilden mehrere Verbindungen eine Redundanzgruppe: nur eine überträgt Daten (STARTDT), die anderen stehen bereit und werden nur mit Testtelegrammen überwacht.
- **Kopplung zweier Systeme:** Soll ein System beide Rollen spielen (z. B. ein Gateway), braucht es Client und Server gleichzeitig.

## Aufbau eines Telegramms

Jedes Telegramm (APDU) besteht aus einem Steuerteil (APCI, 6 Byte) und optional den Nutzdaten (ASDU). Die APDU ist höchstens 255 Byte lang (Startbyte 0x68 + Länge ≤ 253) ([Beckhoff](https://infosys.beckhoff.com/content/1033/ts650x_tcplc_iec60870-5-10x/11728916875.html)).

**APCI: drei Rahmenformate**

| Format | Zweck | Inhalt |
| --- | --- | --- |
| I-Format | Nutzdaten übertragen | Sende- und Empfangszähler N(S), N(R) + ASDU |
| S-Format | Empfang quittieren | nur N(R), keine Nutzdaten |
| U-Format | Verbindung steuern | STARTDT, STOPDT, TESTFR (je act/con) |

**ASDU: was in den Nutzdaten steht**

| Feld | Länge bei 104 | Bedeutung |
| --- | --- | --- |
| Typkennung (TypeID) | 1 Byte | Welche Art Daten, z. B. 13 = Messwert Gleitkomma |
| VSQ | 1 Byte | Anzahl Objekte + ob die Adressen fortlaufend sind (SQ-Bit) |
| Übertragungsursache (COT) | 2 Byte | Warum gesendet, z. B. 3 = spontan, 20 = Generalabfrage; plus Test- und Negativ-Bit und Herkunftsadresse |
| Gemeinsame Adresse (CA) | 2 Byte | Station bzw. logisches Gerät |
| Objektadresse (IOA) | 3 Byte | der einzelne Datenpunkt |
| Informationselemente | je Typ | Wert + Qualität (+ Zeitstempel) |

Ein Datenpunkt ist also eindeutig durch **CA + IOA** bestimmt. Ein Telegramm kann mehrere Objekte desselben Typs bündeln. Die Feldlängen sind bei 104 fest, bei 101 dagegen konfigurierbar ([ABB-Handbuch](https://manualslib.com/manual/1657686/Abb-Eds500-Series.html?page=75)).

## Was man damit machen kann

IEC 104 deckt alles ab, was eine Leitstelle mit Prozessdaten tut: überwachen, steuern, abfragen, Zeit synchronisieren. Die wichtigsten Typkennungen:

| Funktion | Typen (Auswahl) | Richtung |
| --- | --- | --- |
| Einzelmeldung (Ein/Aus) | 1, mit Zeit 30 | Station → Leitstelle |
| Doppelmeldung (Schalterstellung: Zwischen/Aus/Ein/Stör) | 3, mit Zeit 31 | Station → Leitstelle |
| Stufenstellung (Trafo) | 5, mit Zeit 32 | Station → Leitstelle |
| Messwert normiert / skaliert / Gleitkomma | 9 / 11 / 13, mit Zeit 34 / 35 / 36 | Station → Leitstelle |
| Zählwert (Energie) | 15, mit Zeit 37 | Station → Leitstelle |
| Einzel-, Doppel-, Stufenbefehl | 45, 46, 47 (mit Zeit 58–60) | Leitstelle → Station |
| Sollwert normiert / skaliert / Gleitkomma | 48, 49, 50 (mit Zeit 61–63) | Leitstelle → Station |
| Generalabfrage | 100 | Leitstelle → Station |
| Zählerabfrage | 101 | Leitstelle → Station |
| Uhrzeitsynchronisation | 103 | Leitstelle → Station |
| Parameter (Grenzwerte, Totband) | 110–113 | Leitstelle → Station |
| Dateiübertragung (z. B. Störschriebe) | 120–127 | beide |

- **Qualität:** Jeder Wert trägt Qualitätsbits: ungültig (IV), nicht aktuell (NT), ersetzt (SB), blockiert (BL), Überlauf (OV).
- **Zeitstempel:** CP56Time2a, 7 Byte mit Millisekunden. Damit lassen sich Ereignisse am Entstehungsort zeitlich ordnen.
- **Generalabfrage:** Nach dem Verbindungsaufbau holt sich die Leitstelle das komplette Abbild. Danach kommen nur noch Änderungen spontan.
- **Befehle mit Select/Execute:** Ein Befehl kann zweistufig laufen (erst auswählen, dann ausführen). Der Ablauf wird über die COT quittiert: Aktivierung (6), Bestätigung (7), Abschluss (10).
- **Spiegelrichtung:** Viele Typen lassen sich auch rückwärts nutzen, z. B. Messwerte von der Leitstelle an ein unterlagertes System. Ob die Gegenseite das kann, steht in ihrer Interoperabilitätsliste.

## Verbindungsablauf und Timer

Eine TCP-Verbindung allein überträgt noch keine Daten. Erst nach STARTDT fließen I-Telegramme, und beide Seiten überwachen sich mit Zählern und Timern.

1. Client baut TCP auf Port 2404 auf.
2. Client sendet STARTDT act, Server antwortet STARTDT con.
3. Client sendet Generalabfrage (100) und Uhrzeitsynchronisation (103).
4. Server liefert das Abbild (COT 20) und schließt mit Aktivierungsabschluss (COT 10) ab.
5. Danach sendet der Server spontan (COT 3). Bei Stille prüfen beide Seiten mit TESTFR.
6. STOPDT hält den Datenfluss an, ohne TCP zu trennen (genutzt bei Redundanzumschaltung).

| Parameter | Default | Bedeutung |
| --- | --- | --- |
| k | 12 | max. unquittierte I-Telegramme, danach stoppt der Sender |
| w | 8 | spätestens nach w empfangenen I-Telegrammen quittieren |
| t0 | 30 s | Timeout Verbindungsaufbau |
| t1 | 15 s | Timeout für Quittung, danach Verbindung trennen |
| t2 | 10 s | spätestens dann S-Quittung senden (t2 < t1) |
| t3 | 20 s | Leerlauf, danach TESTFR senden |

Die Sequenzzähler N(S)/N(R) laufen 15 Bit modulo 32768. Wird ein Telegramm nicht quittiert, gibt es keine Wiederholung: Die Verbindung wird abgebaut und neu aufgebaut, danach folgt eine neue Generalabfrage.

## Grenzen und Sicherheit

IEC 104 transportiert Datenpunkte zuverlässig, aber Semantik und Sicherheit muss man selbst organisieren.

- **Kein Datenmodell:** Was IOA 4711 bedeutet, steht nur in einer Signalliste. Diese Liste ist das eigentliche Schnittstellendokument und muss von beiden Seiten gepflegt werden.
- **Interoperabilitätsliste:** Jede Implementierung legt fest, welche Typen, COTs und Optionen sie unterstützt. Die Liste ist in der Norm vorgegeben und wird zum Abgleich ausgefüllt ([Kepware-Beispiel](https://support.ptc.com/help/kepware/drivers/en/kepware/drivers/IEC608705104/IEC_60870-5-104_Interoperability_Guide.html)). Hier entscheidet sich, ob zwei Geräte zusammen funktionieren.
- **Keine Massendaten:** Ausgelegt für Signale im Bereich von Tausenden Punkten, nicht für Modelle, Fahrpläne oder große Zeitreihen.
- **Ungesichert im Grundprotokoll:** Klartext, keine Authentifizierung. Abhilfe: TLS nach IEC 62351-3 und Secure Authentication nach IEC 62351-5 bzw. IEC TS 60870-5-7 ([TU Brno](https://www.fit.vut.cz/research/publication-file/11570/TR-IEC104.pdf)). In der Praxis läuft 104 oft ungesichert in abgeschotteten Netzen, geschützt durch Firewalls oder VPN.
- **Interpretationsspielraum:** Hersteller nutzen Optionen unterschiedlich (z. B. Zeitstempel in Lokalzeit oder UTC, Verhalten bei Pufferung). Interop-Tests mit der echten Gegenseite sind Pflicht.

## Bezug zur Prozessabkopplung (Aucoteam)

Öffentlich ist zu Aucoteams Komponente nichts Konkretes zu finden. Die Fragen unten muss Aucoteam beantworten. AUCOTEAM GmbH ist ein Berliner Automatisierungsdienstleister (gegründet 1991, 50 bis 249 Mitarbeiter) mit Kunden unter anderem im Energiesektor ([Clutch](https://clutch.co/profile/aucoteam-gmbh), [all about automation](https://www.berlin.allaboutautomation.de/en/exhibitors/aucoteam-gmbh-228204/)).

Typisch für eine Prozessabkopplung ist ein Gateway zwischen Prozessnetz und IT-System. Es spricht nach außen IEC 104 und entkoppelt die Systeme sicherheitstechnisch. Für uns hieße das: Wir sind wahrscheinlich 104-Client, das Gateway ist Server. Das ist eine Annahme und muss bestätigt werden.

**Offene Fragen an Aucoteam**

- [ ] Rolle: Ist das Gateway Server, Client oder beides? Wer baut die Verbindung auf?
- [ ] Interoperabilitätsliste der Komponente anfordern (Typen, COTs, Optionen).
- [ ] Richtung: Nur Daten lesen, oder auch Befehle und Sollwerte senden?
- [ ] Mengen: Anzahl Datenpunkte, erwartete Änderungsrate, Zeitstempel ja/nein.
- [ ] Redundanz: Wie viele Verbindungen, Redundanzgruppen nach Edition 2?
- [ ] Sicherheit: TLS (IEC 62351-3) gefordert oder Netztrennung allein?
- [ ] Zeit: Uhrzeitsynchronisation über 104 oder NTP/PTP, Zeitstempel in UTC oder Lokalzeit?
- [ ] Signalliste: Wer führt sie, in welchem Format?
- [ ] Testumgebung: Gibt es einen Simulator oder ein Testsystem für Interop-Tests?
