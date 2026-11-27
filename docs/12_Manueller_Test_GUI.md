# Manueller Test der Test-GUI

Stand: 27.11.2026 · für die Test-GUI ab Version 1.0.0 · Dauer etwa 40 Minuten · Begriffe: `docs/10_Glossar.md`

Dieser Durchgang prüft alle Teile der GUI, ohne dass eine echte Station nötig ist: Die GUI bringt eine eigene Teststation mit, gegen die ihre Leitstelle arbeitet. Wer ihn einmal durchgespielt hat, kennt alle Knöpfe, die er beim Test gegen eine echte Station braucht (`docs/11_Test_gegen_Station.md`).

Jeder Block sagt zuerst, worum es geht, dann folgen die Schritte. Spalte „✓“ zum Abhaken; weicht etwas ab, die Nummer und die Zeilen unter „Telegramme und Ereignisse“ notieren.

## Vorbereitung

Alle Befehle in diesem Dokument tippt man in ein **Terminal**: unter Linux ein Terminalfenster, unter Windows ein **WSL-Fenster** (Startmenü „Ubuntu“ bzw. „WSL“). Bedient wird die GUI im Browser, unter Windows im normalen Windows-Browser.

| Nr | Wo | Was | Hinweis |
| --- | --- | --- | --- |
| V1 | Terminal | `julia --version` | Version 1.10 oder neuer? Sonst Julia installieren: `curl -fsSL https://install.julialang.org \| sh`, danach das Terminal schließen und neu öffnen |
| V2 | Terminal | Binärpaket entpacken: `cd ~ && unzip iec104-<version>-linux-x86_64.zip` | Liegt das Zip unter Windows, z. B. im Ordner Downloads: `unzip /mnt/c/Users/<name>/Downloads/iec104-<version>-linux-x86_64.zip` |
| V3 | Terminal | `cd ~/iec104-<version>-linux-x86_64 && bash pruefen.sh` | jede Zeile „ok“, auch „Julia ab 1.10“ und „Library aus Julia ladbar“ |
| V4 | Terminal | `bash gui.sh 8080 127.0.0.1` | Die GUI läuft, solange das Fenster offen ist; Startzeile „IEC 104 Test-GUI (Library …): http://localhost:8080“. Beenden mit Strg+C |
| V5 | Browser | `http://localhost:8080` öffnen; war die Seite schon offen, einmal F5 | Erreicht der Windows-Browser die GUI nicht: in V4 `bash gui.sh 8080 0.0.0.0` und die angezeigte Adresse öffnen |

Für Block G (fremde Leitstelle lib60870) liegt lib60870 gebaut im Repo unter `testgegenstelle/lib60870-C`. `gui.sh` findet es dort selbst, wenn das Binärpaket unter `dist/` des Repos liegt, ebenso unter `lib60870/lib60870-C` neben dem Paketordner oder neben dem Repo; sonst in V4 den Ordner angeben: `bash gui.sh 8080 127.0.0.1 <ordner>/lib60870-C`. Beim Start steht „Gegenstelle lib60870: …“ oder „Ohne lib60870“. Ohne lib60870 entfällt Block G, der Selbsttest zeigt Schritt 27 als übersprungen.

Aufbau der Seite: oben die Reiter „Prüfstand (Leitstelle)“, „Mithören“, „Station und lib60870“, „Selbsttest“; darunter immer der Bereich **„Telegramme und Ereignisse“**. Dort steht jede Meldung und jeder Rahmen mit einer Erklärung in einem Satz – bei Fragen zuerst dort nachsehen.

## A. Selbsttest

**Worum es geht:** Die GUI startet eine eigene Station und eine eigene Leitstelle auf 127.0.0.1 und prüft etwa 20 Abläufe der Library automatisch. Besteht der Selbsttest, sind Rechner, Library und GUI in Ordnung; alle Fehler in den späteren Blöcken liegen dann an der Bedienung oder an der Gegenstelle.

| Nr | Tun | Erwartet | Erklärung | ✓ |
| --- | --- | --- | --- | --- |
| A1 | Reiter „Selbsttest“, „Selbsttest starten“ | Tabelle füllt sich Schritt für Schritt; nach etwa 20 s (mit lib60870 30 s) „26 von 26 bestanden“ bzw. „27 von 27“ | Schritt 27 (lib60870) braucht 9 s, weil das fremde Programm absichtlich wartet; die Schritte 23–26 prüfen zwei Wege | |
| A2 | „Protokoll kopieren“ und in einen Editor einfügen | Kurzfassung: Kopf, Ergebnis, Tabelle aller Schritte | Kopiert man vor dem Ende, steht dort „Zwischenstand … (noch nicht fertig)“ | |
| A3 | „Protokoll speichern“ | Datei `iec104_selbsttest_<datum>.txt` mit allen Rahmen jedes Schritts | Zum Weitergeben, wenn ein Schritt fehlschlägt | |

## B. Teststation einrichten

**Worum es geht:** Die Teststation spielt die Rolle der echten Station (Fernwirkgerät). Man legt Datenpunkte an – Meldungen und Messwerte, die die Station kennt – und kann ihre Werte von Hand ändern. Jede Änderung schickt die Station als spontane Meldung an verbundene Leitstellen.

| Nr | Tun | Erwartet | Erklärung | ✓ |
| --- | --- | --- | --- | --- |
| B1 | Reiter „Station und lib60870“: Port 2404, Station (CA) 1, „Starten“ | rechts oben grün „Port 2404, Station 1“ | 2404 ist der Standardport von IEC 104; die Station lauscht nur auf 127.0.0.1 | |
| B2 | Datenpunkt „1 M_SP_NA_1 Einzelmeldung“, IOA 1001, Wert 0, „Anlegen / senden“ | Zeile 1001 in der Tabelle | Einzelmeldung: 0 = aus, 1 = ein (z. B. Schalterstellung). Die IOA ist die Adresse des Datenpunkts in der Station; die GUI schlägt die nächste freie vor, eine eigene Eingabe gilt | |
| B3 | Datenpunkt „13 M_ME_NC_1 Messwert Gleitkomma“, IOA 2001, Wert 230.5, Text `Spannung L1 (kV)`, „Anlegen / senden“ | Zeile 2001 mit dem Text | Messwert als Gleitkommazahl (z. B. Spannung). Der Text ist optional und landet auch in der Werteliste des Prüfstands (Block D), dort mit ✎ änderbar | |
| B4 | Datenpunkt Typ 1, IOA 2001, Wert 1, „Anlegen / senden“ | rote Zeile oben: „IOA 2001 ist schon als Typ 13 angelegt …“ | Eine IOA hat genau einen Typ; die GUI verhindert Verwechslungen | |
| B5 | Datenpunkt Typ 1, IOA 1002, Wert 2, „Anlegen / senden“ | rote Zeile: „Wert 2.0 passt nicht zu Typ 1 – erlaubt: 0 oder 1“ | Werte werden vor dem Senden geprüft | |

## C. Prüfstand: verbinden und Werte lesen

**Worum es geht:** Der Prüfstand ist die Leitstelle. Sie baut eine TCP-Verbindung zur Station auf, gibt mit STARTDT die Datenübertragung frei und fragt mit der Generalabfrage (GA) alle Werte ab. Das Ergebnis steht im Prozessabbild: je Datenpunkt der letzte Wert und Zähler, wie oft er mit welcher Ursache kam.

| Nr | Tun | Erwartet | Erklärung | ✓ |
| --- | --- | --- | --- | --- |
| C1 | Reiter „Prüfstand (Leitstelle)“: Adresse 127.0.0.1, Port 2404, Station (CA) 1; Haken „nach dem Verbinden STARTDT und Generalabfrage“; „Verbinden“ | neben „Verbindung zur Station“: „Datenübertragung läuft“; im Prozessabbild 1001 („aus“) und 2001 („Wert 230.5“), Ursache „20 Generalabfrage“, Spalte GA = 1 | Unter Telegramme die Folge: STARTDT act/con, Ende der Initialisierung (Typ 70), Generalabfrage mit Bestätigung, Werten und Beendigung | |
| C2 | Reiter „Station und lib60870“: bei 2001 „+1“ | – | Die Station meldet den neuen Wert spontan | |
| C3 | zurück zum Prüfstand | 2001 „Wert 231.5“, Ursache „3 spontan“, Spalte spo = 1 | Die Zähler zeigen, wie jeder Punkt kam: GA (Abfrage), spo (von sich aus), zyk (zyklisch) … | |
| C4 | „Lesebefehl IOA“ 2001, „Lesen“ | 2001 mit Ursache „5 Abfrage“, Spalte les = 1 | Lesebefehl: genau einen Wert abfragen | |
| C5 | „Lesebefehl IOA“ 9999, „Lesen“ | unter Telegramme: „Ursache 47 IOA unbekannt negativ“, Erklärung „Station kennt die IOA nicht“ | So meldet eine Station unbekannte Adressen | |
| C6 | Abfrage „Gruppe 1“ wählen, „Generalabfrage“ | 1001 und 2001 mit Ursache „21 Gruppe 1“ | Die Teststation legt jeden Punkt in Gruppe 1; echte Stationen teilen ihre Punkte in Gruppen | |
| C7 | „Zählerabfrage“ | unter Telegramme Bestätigung und Beendigung (Typ 101); keine Zählwerte | Die Teststation hat keinen Zählwert (Typ 15); bei einer echten Station kämen sie mit Ursache 37 | |

## D. Werteliste

**Worum es geht:** Die Werteliste beschreibt, was man von der Station erwartet: Station, IOA, Typ, einen Text und bei Befehlen die Rückmelde-IOA. Sie wird automatisch gespeichert. Im Prozessabbild sieht man dann sofort, was fehlt (grau), was unerwartet kommt (kursiv) und wo der Typ nicht stimmt (gelb).

Wichtig: ✎ in einer Zeile kopiert die Werte nur in die **Eingabezeile über der Tabelle**. Gespeichert wird erst mit **„In die Liste“**.

| Nr | Tun | Erwartet | Erklärung | ✓ |
| --- | --- | --- | --- | --- |
| D1 | „Empfangene übernehmen“ | rechts „2 in der Liste, 2 empfangen“ | Alle empfangenen Punkte kommen mit ihrem Typ in die Liste | |
| D2 | bei 2001 auf ✎; in der Eingabezeile Text „Spannung L1“; „In die Liste“ | Text erscheint im Prozessabbild | So ergänzt man Texte aus der Liste des Betreibers | |
| D3 | Eingabezeile: Station 1, IOA 4711, Typ „1 M_SP_NA_1“, Text „fehlt“; „In die Liste“ | Zeile 4711 grau | Erwartet, aber nie empfangen – typischer Befund beim Test | |
| D4 | Eingabezeile: IOA 1001, Typ „3 M_DP_NA_1“; „In die Liste“ | Zeile 1001 gelb, Hinweis „Liste erwartet Typ 3“ | Station liefert einen anderen Typ als erwartet; danach mit ✎ wieder Typ 1 setzen | |
| D5 | Filter „IOA, Text, Wert“: `Spannung` | nur Zeile 2001 | Bei vielen Punkten der schnellste Weg | |
| D6 | Auswahl „in der Liste, noch nichts empfangen“ | nur 4711 | ebenso „nicht in der Liste“, „Typ anders als in der Liste“ | |
| D7 | „Als Tabelle speichern“, Datei in Excel öffnen | Umlaute richtig, je Punkt Wert, Ursache und alle Zähler | Ergebnis zum Weitergeben | |

## E. Befehle

**Worum es geht:** Ein Befehl schaltet etwas in der Station (z. B. einen Schalter). Die Station bestätigt ihn (ACTCON), führt ihn aus und meldet das Ende (ACTTERM). Ob er wirklich gewirkt hat, sieht man an der Rückmeldung: einer Meldung, deren Wert sich danach ändert (Rückmelde-IOA). Die GUI misst alle Zeiten in Millisekunden.

| Nr | Tun | Erwartet | Erklärung | ✓ |
| --- | --- | --- | --- | --- |
| E1 | Werteliste-Eingabezeile: IOA 6001, Typ „45 C_SC_NA_1“, Text „Schalter“, Rückmelde-IOA 1001; „In die Liste“ | Zeile 6001 mit „→ RM 1001“ | Befehl 6001 schaltet, Meldung 1001 zeigt den Stand | |
| E2 | Kasten „Befehl“: „aus der Werteliste“ → „1/6001 Schalter (Typ 45)“; **Wert 1**; Ablauf „direkt“; „Senden“ | Tabelle „Befehle und Laufzeiten“: „ausgeführt“, Bestät. und Beend. in ms; Rückmeldung „IOA 1001 …“ (wartet) | „aus der Werteliste“ setzt Station, IOA, Typ und Rückmelde-IOA, **nicht den Wert** – den immer selbst eintragen | |
| E3 | Reiter „Station und lib60870“: bei 1001 Wert 1, „Senden“ | im Prüfstand in der Befehlszeile „… ms, IOA 1001: ein“ | Die Teststation schaltet nicht selbst; hier spielt man die Rückmeldung von Hand | |
| E4 | „Generalabfrage“, dann Befehl wie E2 noch einmal senden, danach wieder „Generalabfrage“ | neuer Befehl bleibt bei „IOA 1001 …“ | Als Rückmeldung zählt nur, was die Station von sich aus meldet (Ursache 3, 11, 12), nicht die Antwort auf eine Abfrage | |
| E5 | Ablauf „Select, dann Execute“, „Senden“ | „ausgeführt“, zusätzlich Zeit unter „Select ms“ | Erst auswählen, dann ausführen – viele Stationen verlangen das für Schalthandlungen | |
| E6 | Reiter „Station und lib60870“: Haken „Befehle annehmen“ aus; Befehl erneut senden | Ergebnis rot „abgelehnt“; unter Telegramme „Ursache 7 Bestätigung negativ“ | So sieht eine Ablehnung durch die Station aus | |
| E7 | Haken „Befehle annehmen“ wieder an; Typ „46 C_DC_NA_1“, IOA 6002, Wert 2, „direkt“, „Senden“ | „ausgeführt“ | Doppelbefehl: 1 = aus, 2 = ein; die erlaubten Werte stehen neben „Wert“ | |
| E8 | Kasten „Befehl“: Typ 45, Wert 5, „Senden“ | rote Zeile oben mit dem erlaubten Bereich; keine neue Zeile in der Tabelle | Falsche Werte gehen nicht hinaus | |
| E9 | im Kopf von „Befehle und Laufzeiten“ „Leeren“ | Tabelle leer | Offene Befehle bleiben im Hintergrund, damit späte Antworten zugeordnet werden | |

## F. Verbindung steuern

**Worum es geht:** Zwischen Leitstelle und Station läuft ein festes Protokoll: Quittungen (S-Rahmen), Testrahmen bei Ruhe (TESTFR), Anhalten und Starten der Datenübertragung (STOPDT/STARTDT). Die Teststation puffert Meldungen, solange angehalten ist.

| Nr | Tun | Erwartet | Erklärung | ✓ |
| --- | --- | --- | --- | --- |
| F1 | 20 s nichts tun, Telegramme ansehen | „U TESTFR act“ und „U TESTFR con“ | Ohne Verkehr prüft jede Seite nach t3 = 20 s, ob die Verbindung lebt | |
| F2 | „STOPDT“ | neben „Verbindung zur Station“: „verbunden, Datenübertragung aus (STARTDT fehlt)“ | Verbindung steht, aber keine Daten | |
| F3 | Station: bei 2001 „+1“; dann Prüfstand „STARTDT“ | erst kommt nichts, nach STARTDT der neue Wert mit Ursache 3 | Die Teststation hat ihn gepuffert; ob eine echte Station das tut, beim Betreiber erfragen | |
| F4 | „Trennen“ | „getrennt – von dieser Seite getrennt“ | Ordentliches Ende | |
| F5 | Port 2405, „Verbinden“ | unter Telegramme „getrennt – an Adresse und Port lauscht niemand …“ | So sieht ein falscher Port oder eine ausgeschaltete Station aus | |
| F6 | Port 2404, Station (CA) 99, „Verbinden“ | Datenübertragung läuft, aber Generalabfrage „Ursache 46 Station unbekannt negativ“, Prozessabbild bleibt ohne neue Werte | Falsche gemeinsame Adresse | |
| F7 | Station (CA) wieder 1, „Verbinden“ | wie C1 | – | |

## G. Fremde Leitstelle lib60870 (nur wenn beim Start angegeben)

**Worum es geht:** lib60870 ist eine unabhängige Implementierung von IEC 104. Ihr Beispielprogramm verbindet sich als zweite Leitstelle mit der Teststation. Klappt das, versteht die Station auch fremde Gegenstellen.

| Nr | Tun | Erwartet | Erklärung | ✓ |
| --- | --- | --- | --- | --- |
| G1 | Reiter „Station und lib60870“: Programm `simple_client`, „Starten“ | unter Telegramme Zeilen „lib60870: …“; „Station: Verbindung 2 …“; am Ende „simple_client beendet, Rückgabe 0“ | Das Programm verbindet sich, fragt ab, sendet einen Testbefehl und endet nach etwa 9 s | |
| G2 | `cs104_client_async`, „Starten“, nach 30 s „Beenden“ | Generalabfrage alle 10 s, dann „… beendet“ | Dauerverbindung einer fremden Leitstelle | |

## H. Telegramme filtern und sichern

**Worum es geht:** Bei einer echten Station entstehen schnell tausende Zeilen. Die Filter zeigen nur das Gesuchte; gesichert wird genau das Angezeigte.

| Nr | Tun | Erwartet | Erklärung | ✓ |
| --- | --- | --- | --- | --- |
| H1 | „nur empfangen ←“, Typ 13 | nur Messwert-Rahmen von der Station | Richtung aus Sicht der Leitstelle bzw. Station | |
| H2 | Filter leeren; IOA 2001 | alle Rahmen, die 2001 enthalten | auch wenn 2001 das fünfte von acht Objekten ist | |
| H3 | Haken „alle Objekte“ und „Hex“ | unter jedem Rahmen jedes Objekt und der Rahmen als Hexdump | Für die genaue Analyse | |
| H4 | „anhalten“ | Anzeige steht, wird aber weiter gesammelt | Zum Lesen bei viel Verkehr | |
| H5 | „Angezeigte speichern“ | Textdatei mit genau den angezeigten Zeilen | Zum Weitergeben | |

## I. Mithören

**Worum es geht:** Mithören zeigt den Verkehr zwischen einer anderen Leitstelle und einer Station, ohne selbst etwas zu senden – aus einer Mitschnittdatei von tcpdump oder Wireshark. Zum Üben gibt es eine Beispieldatei.

| Nr | Tun | Erwartet | Erklärung | ✓ |
| --- | --- | --- | --- | --- |
| I1 | Zweites Terminal (die GUI läuft im ersten weiter), im Ordner der Quellen der Library: `julia --startup-file=no tests/gui/test_capture.jl --sample /tmp/iec104.pcap` | Datei `/tmp/iec104.pcap` (etwa 1 KiB) | Künstlicher Mitschnitt: STARTDT, Generalabfrage, drei Messwerte, Quittung. Das Skript liegt nur in den Quellen, nicht im Binärpaket | |
| I2 | Reiter „Mithören“: Datei `/tmp/iec104.pcap`, Port 2404, „Datei auswerten“ | „10 Pakete, 6 Rahmen“; unter Telegramme 6 Zeilen „Mithören · Leitstelle 10.0.0.9:51000 → Station 10.0.0.5:2404 · …“ | Adressen und Zeit stammen aus dem Mitschnitt | |
| I3 | Reiter „Prüfstand“ | IOA 100–102 der Station 1 im Prozessabbild | Haken „ins Prozessabbild“: mitgehörte Werte erscheinen wie empfangene | |
| I4 | optional, nur wenn tcpdump installiert ist (`sudo apt install tcpdump`) und man Rechte hat: im zweiten Terminal den Befehl aus dem Reiter „Mithören“ mit `-i lo` statt `-i any` starten; „Live mitlesen“; im Prüfstand „Generalabfrage“ | zusätzlich Zeilen „Mithören · …“ für denselben Verkehr | Fehlt die Datei oder bleibt sie leer, meldet die GUI das nach 5 bzw. 10 s unter Telegramme | |

## J. Neustart

| Nr | Tun | Erwartet | Erklärung | ✓ |
| --- | --- | --- | --- | --- |
| J1 | im Terminal der GUI Strg+C, dann V4 wiederholen; im Browser F5 | Werteliste aus Block D und E ist wieder da (Prozessabbild leer) | Die Werteliste steht in `~/.config/iec104/werteliste.tsv` | |

## K. Zwei Wege (Wegeredundanz)

**Worum es geht:** Die Leitstelle hält zwei Wege zu zwei Stationen mit demselben Prozess. Nur der führende Weg wird verwendet; fällt er aus, übernimmt der andere, ohne dass Werte fehlen. Station B der GUI spielt die zweite Station. Erklärung der Spalten: `docs/07_Julia.md`, Abschnitt 7.6.

| Nr | Tun | Erwartet | Erklärung | ✓ |
| --- | --- | --- | --- | --- |
| K1 | Reiter „Station“: Port 2404, Port Station B 2405, „Starten“ | Kopfzeile „Port 2404 und 2405 (B), Station 1“ | Station B hat dieselben Datenpunkte wie A | |
| K2 | Prüfstand: Port 2404, Weg 2 Port 2405; unter „Timer …“ t1 = 2, t3 = 1; „Verbinden“ | „Wege und Reihenprüfung“: „Weg 1 führend, 0 Umschaltungen“, Tabelle mit zwei Zeilen, beide „Datenübertragung läuft“ | Der erste gestartete Weg führt, der andere läuft mit | |
| K3 | „Reihe starten“ (IOA 9000, 200 ms) | Zeile darunter: gesendet und zugestellt steigen, fehlend 0 | Beide Stationen senden 1, 2, 3 … | |
| K4 | Weg 1 „trennen“ | sofort „Weg 2 führend, 1 Umschaltungen“; unter Telegramme „Umschaltung auf Weg 2 … nachgeliefert …“; nach 2 s Weg 1 wieder „läuft mit“; fehlend 0 | Ein Abbruch wird sofort bemerkt | |
| K5 | Weg 2 „stumm“ | etwa 3 s nichts, dann Umschaltung auf Weg 1 mit „nachgeliefert“ > 0; fehlend 0 | Ein schweigender Weg fällt erst nach t3 + t1 auf; was in der Zeit fehlte, liefert Weg 1 nach | |
| K6 | „zufällig stören“ mit 2 bis 5 s, eine Minute laufen lassen, dann Haken weg | Störungen und Umschaltungen im Protokoll mit Startwert; fehlend 0, doppelt evtl. > 0 | Doppelte um eine Umschaltung sind erlaubt, Verluste nicht | |
| K7 | In der Tabelle ablesen, welcher Weg führt (Weg 1 = Station A, Weg 2 = Station B). Reiter „Station“: „Wertänderung an“ = nur die Station des **anderen** Wegs; bei IOA 100 „+1“ | Prozessabbild im Prüfstand: IOA 100 unverändert | Der neue Wert kam nur über den mitlaufenden Weg und liegt in dessen Puffer | |
| K7b | innerhalb von 8 s: Prüfstand, den führenden Weg „trennen“ | Umschaltung mit „nachgeliefert“ ≥ 1; IOA 100 zeigt den neuen Wert, danach Generalabfrage | Der Puffer reicht t1 + t3 + 5 s = 8 s zurück. Dauert es länger, fällt der Wert aus dem Puffer („verloren“ zählt hoch) und kommt über die Generalabfrage | |
| K8 | „Anhalten“, „Wertänderung an“ = beide | Zeile „angehalten: … fehlend 0 …“ | – | |
