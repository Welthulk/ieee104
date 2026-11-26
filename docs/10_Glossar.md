# Glossar

Stand: 26.11.2026

Begriffe für Test-GUI und Prüfstand. Einführung: `docs/01_IEC104_verstehen.md`, Bedienung: `docs/07_Julia.md`, Abschnitt 7.

## 1. Rollen

| Begriff | Bedeutung | Beispiel/Hinweis |
| --- | --- | --- |
| Station | gesteuerte Station (Controlled Station), auch Server, Slave, RTU, Unterstation. Lauscht auf einem Port, liefert Daten, führt Befehle aus | Fernwirkgerät oder Gateway im Netz. In der GUI heißt auch die gemeinsame Adresse „Station“ (Abschnitt 2) |
| Leitstelle | steuernde Station (Controlling Station), auch Client, Master. Baut die Verbindung auf, empfängt Daten, sendet Befehle | Leitsystem; in der GUI der Reiter „Prüfstand (Leitstelle)“ |
| Prüfstand | Reiter „Prüfstand (Leitstelle)“ der Test-GUI: die GUI ist Leitstelle gegen eine Station | Werteliste, Prozessabbild mit Zählern je Ursache, Befehle mit Laufzeiten; `docs/07_Julia.md`, Abschnitt 7.4 |
| Teststation | eigene Station der GUI (Reiter „Station und lib60870“, Bereich „Teststation (Server)“) | Datenpunkte der Typen 1, 3, 5, 9, 11, 13, 15 anlegen, Werte spontan senden, Befehle annehmen oder ablehnen; lauscht nur auf `127.0.0.1` |
| Gegenstelle | die jeweils andere Seite einer Verbindung | vor dem Test ihre Interoperabilitätsliste und Signalliste anfordern |
| lib60870 | unabhängige Implementierung von IEC 104, hier nur fremde Gegenstelle | die GUI startet ein unverändertes Leitstellen-Beispiel (`simple_client`, `cs104_client_async`, `cs104_client_no_threads`) als eigenen Prozess gegen die Teststation; nichts wird gelinkt |

## 2. Adressen

| Begriff | Bedeutung | Beispiel/Hinweis |
| --- | --- | --- |
| IP-Adresse | Netzadresse der Station; die Leitstelle verbindet sich dorthin | GUI-Feld „Adresse“; `127.0.0.1` für die eigene Teststation |
| Port | TCP-Port, auf dem die Station lauscht | 2404 ohne TLS, 19998 mit TLS; frei wählbar, muss mit der Station übereinstimmen |
| Gemeinsame Adresse (CA) | Adresse der Station bzw. des logischen Geräts in jedem Telegramm, 2 Byte | GUI-Feld „Station (CA)“, Spalte „Stat.“; 0 ungenutzt, 1–65534 Station, 65535 Broadcast. Falsche CA: negative Antwort mit Ursache 46 |
| Informationsobjektadresse (IOA) | Adresse des einzelnen Datenpunkts innerhalb einer Station, 3 Byte | IOA 100; IOA 0 = systembezogene Befehle wie die Generalabfrage. Unbekannte IOA: negative Antwort mit Ursache 47 |
| Herkunftsadresse | zweites Byte der Übertragungsursache, kennzeichnet den Absender einer Anfrage | 0 = ungenutzt (Standard) |

## 3. Datenpunkt

Ein Datenpunkt ist ein einzelnes Signal einer Station. IEC 104 kennt kein Datenmodell: Was ein Punkt bedeutet, steht nur in der Signalliste.

| Begriff | Bedeutung | Beispiel/Hinweis |
| --- | --- | --- |
| Schlüssel | Station (CA) + IOA bestimmen den Datenpunkt eindeutig | Station 1, IOA 1001 |
| Typkennung (TypeID) | Art der Daten und Richtung, 1 Byte | 1 Einzelmeldung, 3 Doppelmeldung, 13 Messwert Gleitkomma, 15 Zählwert, 45 Einzelbefehl; mit Zeitmarke 30–40 und 58–64 |
| Wert | Inhalt des Punkts, Wertebereich je Typ | Einzelmeldung 0/1; Doppelmeldung 1 aus, 2 ein, 0 und 3 unbestimmt; normiert −1…<1; skaliert −32768…32767; Gleitkomma beliebig |
| Qualität | Bits zum Wert: IV ungültig, NT nicht aktuell, SB ersetzt, BL blockiert, OV Überlauf (nur Messwerte) | keine Bits gesetzt = gültig; die GUI zeigt gesetzte Bits als `[IV NT …]` |
| Zeitmarke | Zeitpunkt der Entstehung, CP56Time2a (7 Byte, Millisekunden) | nur bei Typen mit Zeitmarke. UTC oder Ortszeit legt die Norm nicht fest, die Zeitzone wird mit der Gegenstelle vereinbart; die Library rechnet in UTC |
| Übertragungsursache (COT) | warum der Wert gesendet wurde, 2 Byte mit Test- und Negativ-Bit | 1 zyklisch, 2 Hintergrund, 3 spontan, 5 Abfrage, 20 Generalabfrage, 21–36 Gruppe 1–16, 37 Zählerabfrage, 11/12 Rückmeldung |
| Signalliste | Liste aller Datenpunkte mit Bedeutung; das eigentliche Schnittstellendokument | beide Seiten pflegen sie; in der GUI als Werteliste nachgebildet |
| Interoperabilitätsliste | Checkliste der Norm (Abschnitt 9): Typen, Ursachen, Optionen, Timer einer Implementierung | Liste dieser Library: `docs/09_Interoperabilitaetsliste.md` |

Beispiel eines Datenpunkts:

| Feld | Wert |
| --- | --- |
| Station (CA) | 1 |
| IOA | 1001 |
| Text (Werteliste) | Schalter Stellung |
| Typkennung | 31 M_DP_TB_1 Doppelmeldung mit Zeitmarke |
| Wert | 2 = ein |
| Qualität | keine Bits gesetzt (gültig) |
| Zeitmarke | 26.11.2026 09:12:03.250 (UTC, vereinbart) |
| Übertragungsursache | 3 spontan |

Die Teststation beantwortet die Generalabfrage für diesen Punkt mit Typ 3 (ohne Zeitmarke) und Ursache 20.

## 4. Meldungen und Befehle

| Begriff | Bedeutung | Beispiel/Hinweis |
| --- | --- | --- |
| Melderichtung | Station → Leitstelle | Meldungen, Messwerte, Zählwerte |
| Befehlsrichtung | Leitstelle → Station | Befehle, Sollwerte, Abfragen |
| Meldung | Zustand aus der Station | 1 Einzelmeldung (aus/ein), 3 Doppelmeldung (Schalterstellung Zwischen/Aus/Ein/Stör), 5 Stufenstellung |
| Messwert | analoger Wert | 9 normiert, 11 skaliert, 13 Gleitkomma |
| Zählwert | Zählerstand, z. B. Energie | 15; kommt mit der Zählerabfrage, nicht mit der Generalabfrage |
| Einzelbefehl | Befehl mit zwei Zuständen | 45 (mit Zeitmarke 58): 0 aus, 1 ein |
| Doppelbefehl | Befehl für Schalter | 46 (59): 1 aus, 2 ein |
| Stufenbefehl | Stufe verstellen, z. B. Trafo | 47 (60): 1 tiefer, 2 höher |
| Sollwert | Vorgabewert an die Station | 48 normiert, 49 skaliert, 50 Gleitkomma (mit Zeitmarke 61–63) |
| direkt | Befehl wird ohne Auswahl sofort ausgeführt (Execute) | GUI-Ablauf „direkt“ |
| Select/Execute | zweistufig: erst auswählen (Select), nach der Bestätigung ausführen (Execute) | GUI-Abläufe „Select, dann Execute“, „nur Select“, „nur Execute“ |
| Aktivierung (ACT) | Anforderung der Leitstelle | Ursache 6 |
| Bestätigung (ACTCON) | Station nimmt die Anforderung an oder lehnt sie ab | Ursache 7 |
| Beendigung (ACTTERM) | Station meldet die Ausführung als abgeschlossen | Ursache 10. Sendet die Station kein ACTTERM: Haken „Befehle enden mit ACTTERM“ entfernen |
| Abbruch | Leitstelle bricht eine Anforderung ab | Ursache 8, Bestätigung des Abbruchs 9 |
| negative Bestätigung | Antwort mit gesetztem Negativ-Bit (P/N): abgelehnt | GUI: Ergebnis „abgelehnt“, im Prozessabbild „negativ“. Ursache 44–47: Typ, Ursache, Station oder IOA unbekannt |
| Rückmeldung | Meldung, an der man sieht, dass ein Befehl gewirkt hat | oft spontan (3) oder mit Ursache 11 (Fernbefehl) bzw. 12 (Ortsbefehl) |
| Rückmelde-IOA | IOA der Rückmeldung zu einem Befehl, in Werteliste und Befehl eintragbar | Befehl „Schalter EIN“ auf IOA 6001, Stellung auf IOA 1001; ihr erster neuer Wert nach dem Befehl wird mit Zeit angezeigt |
| QU | Zusatz bei Einzel-, Doppel-, Stufenbefehl, 0–31 | 0 nicht festgelegt, 1 Kurzimpuls, 2 Langimpuls, 3 Dauerausgabe; Dauer bestimmt die Station |
| QL | Zusatz bei Sollwerten, 0–127 | 0 = Standard |

## 5. Abfragen

| Begriff | Bedeutung | Beispiel/Hinweis |
| --- | --- | --- |
| Generalabfrage (GA) | Typ 100: Leitstelle fordert alle Werte an | nach jedem Verbindungsaufbau. Ablauf: Bestätigung (7), Werte mit Ursache 20, Beendigung (10) |
| Abfragegruppe | Teilmenge der Punkte, Gruppe 1–16 | Generalabfrage Gruppe n: Antworten mit Ursache 20 + n; welche Punkte dazugehören, legt die Station fest |
| Zählerabfrage | Typ 101: Zählwerte anfordern | alle oder Gruppe 1–4; lesen, einfrieren, einfrieren + rücksetzen, rücksetzen; Antwort mit Ursache 37–41 |
| Lesebefehl | Typ 102: einen Punkt über seine IOA abfragen | Antwort mit Ursache 5 |
| Uhrzeit setzen | Typ 103: Uhr der Station stellen | die GUI sendet UTC |
| Testbefehl | Typ 104, mit Zeitmarke 107: prüft die Anwendungsebene | Station spiegelt ihn mit Ursache 7; nicht zu verwechseln mit TESTFR |
| Ende der Initialisierung | Typ 70, Ursache 4: Station meldet sich betriebsbereit | die Teststation sendet es nach dem ersten STARTDT einer Verbindung |

## 6. Verbindung

| Begriff | Bedeutung | Beispiel/Hinweis |
| --- | --- | --- |
| TCP-Verbindung | Leitstelle verbindet sich mit Adresse und Port der Station | allein überträgt sie noch keine Daten |
| STARTDT | Datenübertragung starten (act, Antwort con) | erst danach fließen Werte. GUI-Haken „nach dem Verbinden STARTDT und Generalabfrage“ |
| STOPDT | Datenübertragung anhalten, ohne TCP zu trennen | die Teststation puffert spontane Werte bis zum nächsten STARTDT; genutzt bei Redundanzumschaltung |
| TESTFR | Testrahmen bei Ruhe auf der Verbindung (act, Antwort con) | nach t3 ohne Verkehr |
| t0 | Zeitlimit Verbindungsaufbau | 30 s |
| t1 | Zeitlimit für die Quittung gesendeter Rahmen; Ablauf trennt die Verbindung | 15 s |
| t2 | eigene Quittung spätestens nach t2 | 10 s, t2 < t1 |
| t3 | Ruhezeit bis TESTFR | 20 s |
| k | höchstens k unquittierte gesendete I-Rahmen, danach wartet der Sender | 12 |
| w | spätestens nach w empfangenen I-Rahmen quittieren | 8, w ≤ 2/3 k empfohlen |
| Folgenummern N(S)/N(R) | Sende- und Empfangszähler der I-Rahmen, 15 Bit modulo 32768 | beginnen nach jedem Verbindungsaufbau bei 0; fehlt eine Quittung, wird die Verbindung abgebaut, nicht wiederholt |
| I-Rahmen | Datenrahmen mit N(S), N(R) und ASDU | GUI: `I N(S)=0 N(R)=1` |
| S-Rahmen | reine Quittung, nur N(R) | GUI: `S N(R)=4` |
| U-Rahmen | Steuerrahmen: STARTDT, STOPDT, TESTFR | GUI: `U STARTDT act` |
| APDU | ein Telegramm: Startbyte 0x68, Länge, Steuerteil, Nutzdaten | höchstens 255 Byte |
| APCI | Steuerteil der APDU, 6 Byte | bestimmt das Rahmenformat |
| ASDU | Nutzdaten: Typkennung, VSQ, Ursache, CA, Objekte mit IOA, Wert, Qualität, Zeitmarke | höchstens 249 Byte; mehrere Objekte desselben Typs in einer ASDU möglich |
| tolerant | lässt bekannte Abweichungen einer Gegenstelle zu | Standard ist strikt; GUI-Haken „tolerant“ |
| TLS | Verschlüsselung und gegenseitige Anmeldung mit Zertifikaten nach IEC 62351-3 | Port 19998; GUI-Felder CA (PEM), Zertifikat, Schlüssel, Name der Gegenstelle |
| Redundanzgruppe | mehrere Verbindungen einer Leitstelle zur Station; genau eine überträgt (STARTDT), die anderen stehen in STOPDT bereit | Umschalten: STOPDT auf der alten, STARTDT auf der neuen Verbindung |

## 7. Begriffe der GUI

| Begriff | Bedeutung | Beispiel/Hinweis |
| --- | --- | --- |
| Test-GUI | Oberfläche im Browser, gestartet mit `julia julia/gui/iec104_gui.jl` | `http://localhost:8080`; GUI und Teststation lauschen nur auf `127.0.0.1` |
| Werteliste | die Punkte, die man erwartet: Station, IOA, Typ (leer = beliebig), Text, Rückmelde-IOA | gespeichert in `~/.config/iec104/werteliste.tsv`; „Empfangene übernehmen“ setzt alle Punkte der Generalabfrage in die Liste |
| Prozessabbild | Werteliste und Empfangenes zusammen: letzter Wert, Ursache, Zeitmarke je Punkt | grau = in der Liste, noch nichts empfangen; kursiv = empfangen, nicht in der Liste; gelb = Typ anders als in der Liste. „Als Tabelle speichern“ schreibt CSV |
| Zähler je Ursache | wie oft ein Punkt mit welcher Ursache kam | zeigt, ob jeder Punkt in der Generalabfrage und spontan kommt; „Zähler auf 0“ setzt zurück |
| zyk | zyklisch | Ursache 1 |
| hgd | Hintergrund | Ursache 2 |
| spo | spontan | Ursache 3 |
| GA | Generalabfrage | Ursachen 20–36 |
| ZA | Zählerabfrage | Ursachen 37–41 |
| les | Lesebefehl | Ursache 5 |
| RM | Rückmeldung | Ursachen 11, 12 |
| bef | Befehlsablauf | Ursachen 6–10 |
| sonst | alle übrigen Ursachen | z. B. 4 initialisiert |
| Befehle und Laufzeiten | Tabelle je gesendetem Befehl: Ergebnis und Millisekunden ab Senden | Spalten Select, Bestätigung (ACTCON), Beendigung (ACTTERM), Rückmeldung, gesamt. Ergebnisse: ausgeführt, bestätigt, ausgewählt, abgelehnt, keine Bestätigung, keine Beendigung, Verbindung verloren |
| Telegramme und Ereignisse | jeder gesendete und empfangene Rahmen dekodiert und in einem Satz erklärt, dazu Zustände, Fehler (rot) und Warnungen (braun) | `→` gesendet, `←` empfangen; Filter nach Richtung, I/S/U, Typ, Ursache, IOA, Text; „Angezeigte speichern“ für den Bericht |
| Selbsttest | prüft die Library in etwa 20 Sekunden mit eigener Station und Leitstelle auf `127.0.0.1` | Reiter „Selbsttest“; „Protokoll kopieren“ (Kurzfassung), „Protokoll speichern“ (alle Rahmen); `docs/07_Julia.md`, Abschnitt 7.2 |
| Mithören | passives Mitlesen eines Mitschnitts (tcpdump, pcap): Rahmen werden dekodiert, gesendet wird nichts | Reiter „Mithören“: Datei auswerten oder live mitlesen, während tcpdump schreibt; die GUI liest nur die Datei und braucht keine Sonderrechte (`docs/07_Julia.md`, Abschnitt 7.5) |
