# Änderungen

## 1.0.1 (27.11.2026)

- Bild und Schritt-für-Schritt-Anleitung „Zwei Wege“ in README und `docs/07_Julia.md`.
- Test-GUI: Meldungen der ersten Station heißen „Station A“, sobald Station B läuft.
- Teststation: die IOA eines neuen Datenpunkts wird vorgeschlagen (nächste freie), eigene Eingabe gilt.
- lib60870 v2.4.1 liegt gebaut unter `testgegenstelle/` (fremde Leitstelle für die Test-GUI, GPLv3, mit Quellen); `gui.sh` findet sie ohne Angabe.

## 1.0.0 (27.11.2026)

- Wegeredundanz: Eine Leitstelle hält bis zu vier Wege zu Stationen mit demselben Prozess. Fällt der führende Weg aus, übernimmt ein anderer ohne Verlust von Meldungen (`iec104_client_connect_paths()`, `docs/06_Anleitung.md`).
- Test-GUI: zweiter Weg im Prüfstand, Station B, Wege von Hand oder zufällig stören, Reihenprüfung.
- Die Library heißt jetzt `libiec104.so.1`; neue Felder in `iec104_config`.

## 0.9.3 (26.11.2026)

- Anleitung `docs/11_Test_gegen_Station.md`: eine Station mit dem Prüfstand testen.
- Anleitung `docs/12_Manueller_Test_GUI.md`: alle Teile der GUI Schritt für Schritt ausprobieren.
- Rückmeldung eines Befehls zählt nur, was die Station von sich aus meldet.
- Mithören meldet, wenn die Mitschnittdatei fehlt oder nicht wächst.
- `tools/wsl_ausprobieren.sh` läuft auch, wenn das Terminal noch im alten Ordner steht.
- Selbsttest-Protokoll zeigt während des Laufs einen Zwischenstand.

## 0.9.2 (26.11.2026)

- Test-GUI: neuer Reiter „Mithören“ – IEC-104-Verkehr aus einem Mitschnitt (tcpdump, Wireshark) lesen, auch live.
- Glossar `docs/10_Glossar.md`, Bilder der GUI im README.
- „Senden“ beim Befehl funktioniert wieder in aktuellen Browsern.
- Kleinere Verbesserungen an Prüfstand und Teststation.

## 0.9.1 (26.11.2026)

- Test-GUI: Anzeige von Prozessabbild, Werteliste und Befehlen korrigiert.
- Befehlstyp muss gewählt werden, alle Befehlstypen 45–64 wählbar.

## 0.9.0 (26.11.2026)

- Test-GUI mit Prüfstand: Leitstelle gegen eine Station, Werteliste, Prozessabbild mit Zählern je Ursache, Befehle mit Laufzeiten.
- Telegramme mit Filtern, Kopieren und Speichern.
- API: `iec104_decode_asdu()` und `iec104_type_name()`.

## 0.8.0 (26.11.2026)

- Test-GUI: Selbsttest mit kommentiertem Protokoll, jeder Rahmen im Klartext erklärt.

## 0.7.0 (26.11.2026)

- Test-GUI: lib60870 als fremde Leitstelle gegen die eigene Station; Zugriff nur noch von der eigenen Seite.

## 0.6.2 (25.11.2026)

- Beispielskript wartet zuverlässig auf die Julia-Station.

## 0.6.1 (25.11.2026)

- Kleinere Korrekturen an Binärpaket und Skripten für WSL.

## 0.6.0 (25.11.2026)

- Interoperabilitätsliste (`iec104_interop_list`, `docs/09_Interoperabilitaetsliste.md`).
- API-Doku mit Doxygen, Lesebefehl `iec104_client_read()`.
- Binärpaket mit Prüf- und Startskripten, `tools/wsl_ausprobieren.sh`.

## 0.5.0 (25.11.2026)

- Interop-Tests gegen lib60870, mit und ohne TLS (`docs/08_Interop.md`).
- Erste Test-GUI im Browser.

## 0.4.0 (25.11.2026)

- Redundanzgruppen mit Ereignispuffer.
- Anbindung für Julia (`docs/07_Julia.md`).

## 0.3.0 (25.11.2026)

- TLS nach IEC 62351-3 mit OpenSSL 3.

## 0.2.0 (25.11.2026)

- Log, Statistik, Konfigurationsdatei; Einbindung per `find_package(iec104)`.

## 0.1.0 (25.11.2026)

- Erste Version: Codec, Verbindungssteuerung, TCP, C-API, Abläufe der Station, Selbsttest-Programme.
