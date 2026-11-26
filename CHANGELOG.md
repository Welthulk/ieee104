# Änderungen

## 0.9.2 (26.11.2026)

| Bereich | Änderung |
| --- | --- |
| Test-GUI | Korrektur: „Senden“ beim Befehl tat in aktuellen Browsern nichts. Die Funktion hieß `command()`, und Buttons haben dort eine eigene Eigenschaft `command`, die im `onclick` vorgeht; jetzt `sendCommand()` |
| Test-GUI | „aus der Werteliste“ zeigt bei leerer Liste, wie sie sich füllt; gescheiterter Verbindungsaufbau nur noch als deutsche Meldung (ohne englische Zeile der Library); Lesebefehl ohne „(Element fehlt)“ |
| Test | `test_page.js` prüft jeden Namen in `onclick`/`onchange`/`oninput`: Funktion des Seitenskripts und keine Eigenschaft von Element oder Dokument |
| Werkzeug | `wsl_ausprobieren.sh` nimmt immer das neueste Paket und entpackt ein Zip, das neuer ist als die entpackten Ordner (vorher blieb ein älterer Ordner stehen) |

## 0.9.1 (26.11.2026)

| Bereich | Änderung |
| --- | --- |
| Test-GUI | Korrektur: Die Seite fand Prozessabbild, Werteliste und Befehle nicht (falsche Verschachtelung im JSON), schickte dann bei jeder Aktualisierung `undefined` und die GUI meldete im Terminal laufend „invalid base 10 digit 'u'“; dadurch blieben Station, Selbsttest und Ereignisse auf der Seite leer |
| Test-GUI | Befehle: Typ ist Pflicht; ein Listeneintrag mit einem Typ ohne Auswahlpunkt (z. B. 60) schickte stillschweigend einen Einzelbefehl (45). Alle Befehlstypen 45–51 und 58–64 wählbar; Rückmelde-IOA wird vor dem Senden geprüft (vorher: Befehl gesendet, aber Fehler gemeldet und nicht verfolgt) |
| Test-GUI | Fehlerhafte Zahlen in Anfragen: bei der Zustandsabfrage Standardwert, bei Aktionen deutsche Meldung; ein Fehler im GUI-Programm antwortet mit 500 und erscheint im Terminal nur einmal; Pfad der Werteliste mit `~` |
| Test | `tests/gui/test_page.js`: führt das Skript der Seite in gjs gegen eine laufende GUI aus (ctest `Julia.GuiPage`, wenn gjs vorhanden): unbekannte Element-IDs, Ausnahmen, Antworten ungleich 200, Fehlerzeile der Seite und Ausgaben der GUI im Terminal schlagen fehl |
| Doku | Bild der Test-GUI im README (`docs/bilder/test_gui.png`, erzeugt mit `tools/gui_screenshot.sh` über WebKitGTK) |

## 0.9.0 (26.11.2026)

| Bereich | Änderung |
| --- | --- |
| Test-GUI | Prüfstand: Reiter „Prüfstand (Leitstelle)“ gegen eine Station. Werteliste direkt in der GUI (Station, IOA, Typ, Text, Rückmelde-IOA), automatisch gespeichert (`~/.config/iec104/werteliste.tsv`), „Empfangene übernehmen“; Prozessabbild aus Liste und Empfangenem mit Zählern je Ursache (zyklisch, Hintergrund, spontan, GA, Zählerabfrage, Lesen, Rückmeldung, Befehl, sonst), Markierung fehlender, fremder und abweichender Punkte, als Tabelle speichern |
| Test-GUI | Befehle mit QU/QL, Ablauf direkt, Select+Execute, nur Select, nur Execute; Tabelle mit Ergebnis und Laufzeiten bis Select-Bestätigung, ACTCON, ACTTERM, Rückmeldung und Endergebnis; Zeitlimits und „ohne ACTTERM“ einstellbar; nach dem Verbinden auf Wunsch STARTDT und Generalabfrage; Lesebefehl, Zählerabfrage mit Gruppe und Einfrieren |
| Test-GUI | Telegramme: Filter nach Richtung, I/S/U, Typ, Ursache, IOA, Text; alle Objekte, Hex; anhalten; Angezeigtes kopieren und speichern; 20000 Einträge. Reiter für Prüfstand, Station und lib60870, Selbsttest. Trenngründe auf Deutsch |
| API | `iec104_decode_asdu()`: ASDU in Kopf und Objekte zerlegen (z. B. für Mitschnitte), `iec104_type_name()`; in Julia `decode_asdu`, `type_name`, `client_set_options` |
| Transport | Reihenfolge der Ereignisse: Eine angenommene Verbindung meldet „verbunden“ jetzt vor ihren ersten Rahmen. Bisher kam die Meldung erst danach, wenn die Gegenstelle STARTDT gleich nach dem Verbindungsaufbau schickte |
| Test | `Operation.ConnectedReportedBeforeFirstFrame` prüft diese Reihenfolge; `run_lib60870.sh` las den Port gelegentlich aus dem Log des vorigen Laufs (Fall „Server <- Client“ schlug selten fehl) |

## 0.8.0 (26.11.2026)

| Bereich | Änderung |
| --- | --- |
| Test-GUI | Selbsttest: 22 Schritte (mit lib60870-Ordner 23) mit eigener Station und Leitstelle auf 127.0.0.1, je Schritt Zweck, Erwartung, Beobachtung; „Protokoll kopieren“ (Kurzfassung) und „Protokoll speichern“ (alle Rahmen); Ersatz per markiertem Textfeld, wo der Browser die Zwischenablage sperrt |
| Test-GUI | Jeder gesendete und empfangene Rahmen unter Ereignisse dekodiert (I/S/U, Typ, Ursache, Station, erstes Objekt mit Wert, Qualität, Zeitmarke) und in einem Satz erklärt; ein- und ausblendbar, „Ereignisse kopieren“; Warnungen braun, Fehler rot |
| Test-GUI | Wert an der Station ohne Leitstelle mit STARTDT: Hinweis „gespeichert, nichts gesendet“ |
| Test-GUI | Korrekturen aus der Prüfung: Datenpunkt wird auch ohne laufende Station geprüft (blockierte sonst den Start, Meldung nennt jetzt die IOA); Klicks in der Punkttabelle gehen nicht mehr verloren; nach Neustart der GUI zeigt die offene Seite wieder Ereignisse; Zustand nach erneutem Verbinden stimmt; Tabelle der Leitstelle zeigt höchstens 1000 Punkte; Download des Protokolls bricht in Firefox nicht mehr ab |
| Test-GUI | Rahmen im Klartext: Quittung nach Überlauf der Folgenummer richtig, unvollständiges Element behält den Kopf |
| Werkzeug | `wsl_ausprobieren.sh`: kein „detached HEAD“-Hinweis mehr beim Holen von lib60870 |
| Test | `tests/gui/test_frames.jl` (ctest `Julia.Frames`, nur Julia) mit Rahmen aus dem lib60870-Mitschnitt; `test_gui.sh` startet den Selbsttest und prüft Ergebnis und Protokoll |

## 0.7.0 (26.11.2026)

| Bereich | Änderung |
| --- | --- |
| Test-GUI | Bereich „Gegenstelle lib60870“: startet ein unverändertes lib60870-Beispiel (`simple_client`, `cs104_client_async`, `cs104_client_no_threads`) als Leitstelle gegen die GUI-Station auf 127.0.0.1; Ausgabe unter Ereignisse. Ordner beim Start: `gui.sh 8080 127.0.0.1 <lib60870-C>` |
| Test-GUI | Schutz: Host-Kopfzeile nur `localhost` oder IP-Adresse, `/api/` nur mit Kopfzeile `X-IEC104-GUI`, nur GET; die Station lauscht auf der Adresse der GUI (Standard 127.0.0.1) statt auf allen |
| Test-GUI | Zustände und Fehler auf Deutsch mit Hinweis (z. B. „niemand lauscht – Station gestartet?“, „erst STARTDT“, erlaubter Wertebereich); Wertebereich neben den Eingabefeldern; Typ einer IOA lässt sich nicht mehr ändern; keine doppelten Zeilen unter Ereignisse |
| Beispiele | `server.jl` und `hello_server` lauschen ohne Adressangabe nur auf 127.0.0.1 |
| Interop | `run_lib60870.sh` startet lib60870-Stationen (lauschen fest auf 0.0.0.0) nur mit `--mit-stationen` |
| Werkzeug | `wsl_ausprobieren.sh --mit-lib60870`: lib60870 v2.4.1 neben den Arbeitsordner holen und bauen, GUI-Aufruf mit lib60870 ausgeben |
| Test | `tests/gui/test_gui.sh` (ctest `Julia.Gui`, `Julia.GuiLib60870` im shared-Build) |

## 0.6.2 (25.11.2026)

| Bereich | Änderung |
| --- | --- |
| Lieferung | `beispiel.sh` konnte „connection closed, reason 3“ melden: Julia puffert die Ausgabe in eine Datei, „listening“ kam erst beim Ende der Station an, die Leitstelle verband sich zu spät. `server.jl` leert die Ausgabe jetzt sofort |
| Julia | `server.jl [port] [sekunden] [adresse]`: optionale Adresse; `beispiel.sh` startet die Station nur auf `127.0.0.1` und zeigt bei Fehlern Log und Portzustand der Station |

## 0.6.1 (25.11.2026)

| Bereich | Änderung |
| --- | --- |
| Build | Ohne installiertes GoogleTest lädt CMake GoogleTest 1.14.0 von GitHub (feste Prüfsumme, nur für die Tests); abschaltbar mit `IEC104_FETCH_GTEST=OFF` |
| Werkzeug | `wsl_ausprobieren.sh`: GoogleTest nicht mehr Pflicht; geht der Download nicht, Bau ohne Unit-Tests und Selbsttests über TCP, TLS und Umschaltung |
| Lieferung | `gui.sh [port] [adresse]`: mit `0.0.0.0` aus dem Windows-Browser über die WSL-Adresse erreichbar; `beispiel.sh` wartet bis zu 5 Minuten auf den ersten Julia-Start |

## 0.6.0 (25.11.2026)

| Bereich | Änderung |
| --- | --- |
| Interop | `iec104_interop_list`: Interoperabilitätsliste nach Abschnitt 9 für Rolle und Konfiguration, Markdown oder CSV; `docs/09_Interoperabilitaetsliste.md` für die Standardwerte, Test hält sie aktuell |
| Doku | API-Doku mit Doxygen (Ziel `doc`), öffentlicher Header ohne Warnungen (Test `Doc.NoDoxygenWarnings`); Typentabelle bei `iec104_object` repariert |
| Client | `iec104_client_read()`; Lesebefehl (102) jetzt mit Ursache 5 statt 6 (vorher von jeder Station abgelehnt) |
| Server | Eine Station ist über ihre Daten- oder Befehlspunkte bekannt: Stationen nur mit Befehlspunkten werden nicht mehr mit Ursache 46 abgelehnt, bekommen das Ende der Initialisierung und antworten auf eine Broadcast-Abfrage. Mit Befehlspunkten und leerem Prozessabbild gilt die Prüfung jetzt ebenfalls (andere Adressen: Ursache 46); nur ein Server ganz ohne Punkte nimmt jede Adresse an |
| Server | Broadcast-Abfrage ohne bekannte Station wird einmal mit der Broadcast-Adresse bestätigt (vorher keine Antwort) |
| Server | `iec104_server_set_options` prüft `cause_of_init` (≤ 127) und übernimmt bei Fehler nichts |
| Werkzeug | `iec104_interop_list`: exakte Zeitwerte, fehlende Optionswerte als Fehler, CSV mit BOM und Parametern in der Rollenspalte |
| Lieferung | Binärpaket mit `pruefen.sh`, `gui.sh`, `beispiel.sh` für Linux und WSL |
| Werkzeug | `tools/wsl_ausprobieren.sh`: Repo nach `~/src/iee104` holen, Voraussetzungen prüfen, fertige Library testen, bauen, alle Tests |
| Julia | `client_read` in `IEC104.jl` |

## 0.5.0 (25.11.2026)

| Bereich | Änderung |
| --- | --- |
| Interop | `tools/interop/run_lib60870.sh`: 8 Fälle gegen lib60870 mit und ohne TLS; Ergebnisse und Befunde in `docs/08_Interop.md` |
| Interop | Vorbereitung Integrationstest: Checkliste, Ablauf, Fehlerbilder, Vorlage `tools/interop/gegenstelle.conf` |
| TLS | Renegotiation durch die Gegenstelle wird gemeldet (`key renewal: renegotiation complete`) |
| Selbsttest | Client `--series-ioa`; `--switchover` auch gegen fremde Server; klare Meldung bei fehlendem ACTCON/ACTTERM |
| Tests | Referenzmitschnitt über TLS (`Capture.ForeignTlsServer`); Test-PKI auch mit RSA-Schlüsseln |
| Doku | README mit Schnellstart ohne Bauen, Feature-Liste, Tests, Test gegen lib60870 |
| Test-GUI | `examples/julia/gui/`: Oberfläche im Browser für Leitstelle und Station (Abfragen, Befehle mit Select/Execute, Werte, TLS, Ereignisse); nur Julia-Standardbibliothek |
| Julia | `IEC104.jl` um Befehle, Uhrzeit, Zählerabfrage, TLS, Log und Texte der Zustände erweitert |

## 0.4.0 (25.11.2026)

| Bereich | Änderung |
| --- | --- |
| Redundanz | Redundanzgruppen (`iec104_server_add_group`): eine aktive Verbindung je Gruppe, Umschaltung per STOPDT/STARTDT, Übernahme durch STARTDT auf einer zweiten Verbindung |
| Pufferung | Ereignispuffer je Gruppe: Ereignis bleibt bis zur Quittung, nach Abbruch oder Umschaltung erneut gesendet; Überlauf: älteste verwerfen oder ablehnen |
| Pufferung | ohne Gruppen: jede Verbindung puffert während STOPDT (`event_buffer_size`, `overflow` in `iec104_server_options`) |
| Zuordnung | neue Verbindungen nach IP-Adresse der Leitstelle; unbekannte Adressen werden abgewiesen |
| Betrieb | `iec104_server_get_group_stats`, `iec104_server_connection_group`; Log bei Übernahme und Überlauf |
| Konfiguration | Schlüssel `group`, `event_buffer_size`, `overflow` |
| Selbsttest | Server `--group`, `--buffer`, `--overflow`, `--broadcast`; Client `--switchover`; ctest `SelfTest.SwitchOver` |
| Festlegung | E10: Redundanzgruppen |
| Lieferung | fertig gebaute Library auf das Nötigste reduziert: `lib/libiec104.so.0`, Header, Lizenzen, Julia-Anbindung, Kurzanleitung |
| Julia | `examples/julia/IEC104.jl` (Anbindung per `ccall`), Client- und Server-Beispiel; Anleitung `docs/07_Julia.md` |

## 0.3.0 (25.11.2026)

| Bereich | Änderung |
| --- | --- |
| TLS | TLS 1.2/1.3 nach IEC 62351-3 mit OpenSSL 3.x: `iec104_tls_config`, `iec104_server_set_tls`, `iec104_client_set_tls` |
| TLS | gegenseitige Authentifizierung, Sperrliste, Name oder SHA-256-Fingerprint der Gegenstelle, Schlüssel mit Passwort |
| TLS | Key Update (1.3) und Renegotiation (1.2) nach Zeit, Session Resumption, Zertifikatswechsel zur Laufzeit |
| TLS | Callback `on_security`, Schließgrund `IEC104_CLOSE_TLS`, `iec104_*_get_tls_info` |
| Konfiguration | Schlüssel `tls`, `tls_*`; relative Pfade ab dem Verzeichnis der Datei |
| Selbsttest | Optionen `--tls*`; ctest auch über TLS; Test-PKI `tools/make_test_certs.sh` |
| Build | Option `IEC104_TLS` (Default an); `find_package(iec104)` zieht OpenSSL nach |
| Lieferung | zusätzlich fertig gebaute Library `iec104-<version>-linux-x86_64.zip` |
| Festlegung | E9: TLS-Handshake fällt unter t0 |

## 0.2.0 (25.11.2026)

| Bereich | Änderung |
| --- | --- |
| Betrieb | Log-Callback je Handle (`iec104_*_set_log`), Stufen Fehler bis Trace; Trace zeigt jede APDU als Hexdump |
| Betrieb | Statistik je Verbindung (`iec104_*_get_stats`): Rahmen, Oktette, Anfragen, Befehle, negative Antworten |
| Betrieb | Konfigurationsdatei `key = value` (`iec104_config_load`) |
| Einbindung | `cmake --install`, `find_package(iec104)`, Ziel `iec104::iec104`; statisch oder geteilt (`BUILD_SHARED_LIBS`, Preset `shared`) |
| Beispiele | `examples/hello_server.c`, `examples/hello_client.c`; Test baut sie gegen die installierte Library |
| Selbsttest | Optionen `--config`, `--log`, `--stats` |
| Doku | neu `docs/06_Anleitung.md`; docs/02–05 gestrafft |
| Festlegung | E8: kein interner Thread, Poll im Thread der Anwendung |

## 0.1.0 (25.11.2026)

Erste Lieferung: Codec aller Typen, Zustandsmaschine der Verbindung, TCP, C-API, Abläufe der gesteuerten Station (Generalabfrage, Zählerabfrage, Uhrzeit, Befehle), Befehlsmodelle mit Select/Execute, maximale Befehlsverzögerung, Befehlsverfolgung am Client, Selbsttest-Programme.
