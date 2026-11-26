# Analyse WinPP104 („Fink-Tester“) und Folgerungen für den Prüfstand

Stand: 26.11.2026. Nur intern: Herstellername und Produkt kommen nicht ins Repo außerhalb von `intern/` und nicht in Lieferungen.

## 1. Anlass

Für den Integrationstest der Library mit der PA (Nachweis für das MVP) brauchen wir ein Prüfwerkzeug. Kollegen kennen dafür WinPP104 von Ingenieurbüro Fink (ppfink.de), den „Fink-Tester“. Die Analyse stützt sich auf die öffentliche Produktbeschreibung und die Anleitung (Stand Januar 2018). Ein Exemplar lag nicht vor.

## 2. Rahmen des Werkzeugs

| Merkmal | WinPP104 |
| --- | --- |
| Plattform | Windows-Programm |
| Lizenz | Dongle |
| Protokoll | IEC 60870-5-104 |
| Rollen | Mithören (passiv), Zentrale simulieren, Station simulieren |

## 3. Funktionsumfang

| Bereich | Funktion |
| --- | --- |
| Mithören | Passiver Mitschnitt des Verkehrs zwischen Zentrale und Station; Tabelle der TCP-Verbindungen |
| Prozessabbild | Je Objekt der letzte Wert und Zähler je Ursache: zyklisch, Hintergrund, spontan, GA, Zählerabfrage, act/con, sonstige |
| Protokoll | Zeitstempel, Umlaufpuffer, 7 Ausgabeformate (von Hex bis Klartext) |
| Darstellung | Farben je Ursache, Typ oder IOA |
| Filter | Typ, Ursache, Qualität, IP-Adresse, Station, IOA, Zeitraum |
| Objekttexte | CSV-Datei mit IOA, Farbe und Text |
| Senden | 12 frei belegbare Telegramme; 12 Listen mit bis zu 3000 Objekten |
| Last | Meldeschwall, d. h. viele Meldungen in kurzer Zeit |
| Station | GA automatisch beantworten; externe CSV-Dateien zyklisch einspielen |
| Befehle | Rückmelde-Tabelle für bis zu 1000 Befehle; Prüfung der Befehlslaufzeit |
| Fehler simulieren | Privat-ASDU, N(S) um 2 springen lassen, falsch oder gar nicht quittieren |
| Prüfung | Formale Prüfung empfangener Telegramme |
| Import | Mitschnitte im pcap-Format |

## 4. Bewertung für den PA-Test

Beim Test ist die PA die Station, wir sind die Leitstelle. Wir lesen Datenpunkte und senden Befehle bzw. Sollwerte.

| Funktion | Nutzen für den PA-Test | Bewertung |
| --- | --- | --- |
| Zentrale simulieren | Kern des Tests | unverzichtbar |
| Prozessabbild mit Zählern je Ursache | Zeigt, ob jeder Punkt in GA, spontan und zyklisch kommt | unverzichtbar |
| Objekttexte per CSV | Kunde liefert die Datenpunktliste; Texte machen den Test lesbar | unverzichtbar |
| Rückmelde-Tabelle, Befehlslaufzeit | Nachweis, dass Befehl und Rückmeldung zusammenpassen | unverzichtbar |
| Protokoll mit Filter und Export | Fehlersuche, Nachweis gegenüber dem Kunden | unverzichtbar |
| Formale Prüfung | Abweichungen der Gegenstelle früh sehen | wichtig |
| Station simulieren, Listen, Meldeschwall | Lasttest der eigenen Leitstelle, Gegenstück zur PA | später |
| pcap-Import | Mitschnitte vom Kunden auswerten | später |
| Fehlersimulation | Robustheit der Gegenstelle prüfen | später, nur gegen Testgeräte |
| Mithören live | Braucht Capture-Rechte bzw. Spiegelport | nur mit Freigabe der IT |

## 5. Folgerung: eigener Prüfstand

Wir bauen „etwas Ähnliches“ mit eigenem Namen. Code, Oberfläche und Texte werden nicht übernommen. Grundlage ist unsere Library mit derselben Codebasis wie im Produkt. Damit prüft der Test zugleich die Library.

| Stufe | Inhalt | Stand |
| --- | --- | --- |
| 1 | Leitstelle gegen eine Station, Bedienung im Browser. Datenpunktliste (CSV mit Station, IOA, Typ, Text, Rückmelde-IOA). Prozessabbild mit Zählern je Ursache. Telegrammprotokoll im Klartext mit Filter, Export und Mitschrift. Befehle mit Laufzeiten (Bestätigung, Beendigung, Rückmeldung). GA, Zählerabfrage, Lesen, Uhrzeit. TLS optional. Eingebaute Teststation zum Ausprobieren | in Arbeit (0.9.0) |
| 2 | Station simulieren: Listen, Meldeschwall, zyklische Werte, GA aus Liste | geplant |
| 3 | pcap-Import, formale Prüfung als Bericht, Fehlersimulation (nur Testgeräte) | geplant |
| – | Mithören live | nur nach Freigabe der IT |

## 6. Unterschiede zu WinPP104

| Punkt | WinPP104 | Prüfstand |
| --- | --- | --- |
| Plattform | Windows | Linux (Aachen), WSL; Windows später |
| Lizenz | Dongle | keine, eigene Software |
| Bedienung | Windows-Oberfläche | Browser auf 127.0.0.1 |
| Protokollkern | eigener | unsere Library (auch Produkt) |
| Klartext | 7 Formate | Kurzzeile plus ein erklärender Satz auf Deutsch, alle Objekte aufklappbar |
| TLS (IEC 62351-3) | keine Angabe gefunden | ja, über die Library |

## 7. Offene Punkte

1. Fährt die PA TLS? Wenn ja: Zertifikate, Port 19998 (Interop-Liste).
2. Datenpunktliste der PA vom Kunden bzw. aus Aachen, mit der Zuordnung Befehls-IOA zu Rückmelde-IOA.
3. Erreicht WSL am Kundenlaptop die PA über VPN? Netz- und WSL-Einstellungen ändern wir nicht selbst.
4. Ist eine Windows-Fassung nötig (Winsock-Port, OpenSSL für Windows)?
5. Mithören nur mit schriftlicher Freigabe der IT.
