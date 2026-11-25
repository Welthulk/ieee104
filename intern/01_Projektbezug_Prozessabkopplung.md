# Projektbezug: Prozessabkopplung

Ausgelagert aus `docs/01_IEC104_verstehen.md` (25.11.2026), da projekt- und firmenbezogen. Nicht Teil des Weitergabe-Zips.

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
