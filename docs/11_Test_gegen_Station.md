# Anleitung: Station mit dem Prüfstand testen

Stand: 26.11.2026 · für die Test-GUI ab Version 0.9.3 · Begriffe: `docs/10_Glossar.md` · Probelauf in der GUI: `docs/12_Manueller_Test_GUI.md`

Der Prüfstand ist die Leitstelle der Test-GUI. Mit ihm liest man Werte einer Station aus, sendet Befehle und prüft, ob die Rückmeldung kommt. Bedient wird er im Browser, kompiliert wird nichts.

## 1. Was man braucht

| Was | Hinweis |
| --- | --- |
| Rechner mit Linux x86-64 | AlmaLinux/RHEL 9, Ubuntu ab 22.04, Debian 12 oder WSL2 unter Windows |
| Julia ab 1.10 | `curl -fsSL https://install.julialang.org \| sh`, danach neues Terminal |
| Binärpaket | `iec104-<version>-linux-x86_64.zip`, entpackt in einen beliebigen Ordner |
| Netzweg zur Station | IP-Adresse und Port der Station müssen vom Rechner aus erreichbar sein; die GUI ändert keine Netz- oder Firewall-Einstellungen |
| Freigabe | Befehle schalten echte Betriebsmittel: nur mit Freigabe des Betreibers und im vereinbarten Zeitfenster |

## 2. Vom Betreiber der Station erfragen

| Punkt | Frage | Wo in der GUI |
| --- | --- | --- |
| Adresse | IP-Adresse, Port (Standard 2404, mit TLS 19998) | Prüfstand: Adresse, Port |
| Station | gemeinsame Adresse (CA), ggf. mehrere | Prüfstand: Station (CA) |
| Zulassung | welche IP-Adressen als Leitstelle zugelassen sind; ob schon eine andere Leitstelle verbunden ist | – |
| TLS | ja oder nein; wenn ja: CA, eigenes Zertifikat und Schlüssel, Name der Station im Zertifikat | Prüfstand: „Timer, Fenster, Befehle, TLS“ aufklappen: CA (PEM), Zertifikat, Schlüssel, Name der Gegenstelle |
| Timer | weichen t0–t3, k, w vom Standard ab? | ebenda |
| Meldungen | Liste der Datenpunkte: IOA, Typ, Bedeutung | Werteliste |
| Befehle | IOA, Typ, Werte (z. B. Doppelbefehl 1 = aus, 2 = ein), direkt oder Select dann Execute, QU, Rückmelde-IOA | Werteliste, Befehl |
| Beendigung | sendet die Station nach einem Befehl ACTTERM? | Haken „Befehle enden mit ACTTERM“ |
| Pufferung | liefert die Station nach STOPDT/STARTDT nach, was in der Zwischenzeit kam? | – |
| Zeit | Zeitmarken in UTC oder Ortszeit; darf die Uhr gestellt werden? | „Uhrzeit setzen (UTC)“ nur nach Absprache |

Die vollständige Liste für Entwickler steht in `docs/08_Interop.md`, Abschnitt 5.1.

## 3. Installieren und starten

```bash
unzip iec104-<version>-linux-x86_64.zip
cd iec104-<version>-linux-x86_64
bash pruefen.sh     # jede Zeile „ok“, auch „Julia ab 1.10“ und „Library aus Julia ladbar“
bash gui.sh         # Startzeile: IEC 104 Test-GUI (Library …): http://localhost:8080
```

Dann `http://localhost:8080` im Browser öffnen. Unter WSL geht das im Windows-Browser. Erreicht er `localhost` nicht: `bash gui.sh 8080 0.0.0.0` und die angezeigte Adresse öffnen. Achtung: So ist die GUI ohne Anmeldung aus dem Netz erreichbar und kann Befehle an die Station senden – nur kurz und nur in einem geschützten Netz. Beenden mit Strg+C.

## 4. Ablauf

Die GUI zeigt Störungen oben meist nur knapp („getrennt“, „abgelehnt“). Den Grund nennt immer der Bereich **„Telegramme und Ereignisse“** unten: Warnungen braun, Fehler rot, bei negativen Antworten die Ursache (z. B. „Ursache 47 IOA unbekannt negativ“).

| Schritt | In der GUI | Ergebnis |
| --- | --- | --- |
| 1. Rechner prüfen | Reiter „Selbsttest“, „Selbsttest starten“, warten bis fertig | „22 von 22 bestanden“ (mit lib60870 23) |
| 2. Verbinden | Reiter „Prüfstand (Leitstelle)“: Adresse, Port, Station (CA); Haken „nach dem Verbinden STARTDT und Generalabfrage“; „Verbinden“ | neben „Verbindung zur Station“ steht „Datenübertragung läuft“; das Prozessabbild füllt sich |
| 3. Werteliste | „Empfangene übernehmen“. Dann je Zeile ✎: Die Werte stehen in der Eingabezeile über der Tabelle; Text und erwarteten Typ ergänzen, „In die Liste“. Fehlende Punkte dort mit Station, IOA und Typ eintragen, „In die Liste“ | grau = in der Liste, aber nichts empfangen; kursiv = empfangen, aber nicht in der Liste; gelb = anderer Typ |
| 4. Werte beobachten | „Generalabfrage“ wiederholen, auf spontane Meldungen warten | Zähler je Ursache (GA, spo, zyk …) zeigen, wie jeder Punkt kommt |
| 5. Befehl eintragen | Eingabezeile der Werteliste: Station, IOA, Typ 45–64, Text, Rückmelde-IOA; „In die Liste“ | Zeile mit „→ RM“ und der Rückmelde-IOA |
| 6. Befehl senden | Kasten „Befehl“: „aus der Werteliste“ wählen (setzt Station, IOA, Typ, Rückmelde-IOA). **„Wert“ selbst eintragen**, die erlaubten Werte stehen daneben. „Ablauf“ wie mit dem Betreiber vereinbart; „Senden“ | Tabelle „Befehle und Laufzeiten“: Ergebnis, Millisekunden bis Bestätigung, Beendigung und Rückmeldung |
| 7. Zähler | „Zählerabfrage“, danach noch einmal „Empfangene übernehmen“ (Zähler kommen nicht mit der Generalabfrage) | Zählwerte mit Ursache 37 |

Einen einzelnen Wert liest man mit der IOA im Feld „Lesebefehl IOA“ und dem Knopf „Lesen“ (Antwort mit Ursache 5, Spalte „les“). „STOPDT“ und „STARTDT“ halten die Datenübertragung an und starten sie wieder; ob die Station danach nachliefert, was in der Zwischenzeit kam, hängt von der Station ab.

Als Rückmeldung eines Befehls zählt der erste Wert der Rückmelde-IOA, den die Station von sich aus meldet (spontan oder als Rückmeldung, Ursache 3, 11 oder 12), nicht die Antwort auf eine Generalabfrage.

## 5. Ergebnisse sichern

| Was | Wie |
| --- | --- |
| Prozessabbild | „Als Tabelle speichern“ (CSV, öffnet in Excel) |
| Telegramme | Filter setzen, „Angezeigte speichern“; ohne Filter alles |
| Werteliste | Datei `~/.config/iec104/werteliste.tsv` kopieren; beim nächsten Start ist sie wieder da |
| Selbsttest | Reiter „Selbsttest“, „Protokoll speichern“ |

## 6. Mithören (optional)

Läuft schon eine andere Leitstelle mit der Station, zeigt der Reiter „Mithören“ deren Verkehr aus einem Mitschnitt. Mitschneiden darf nur, wer dafür die Freigabe und die Rechte hat; die GUI liest nur die Datei. Ablauf und Befehl: `docs/07_Julia.md`, Abschnitt 7.5.

## 7. Wenn es hakt

Den Text der Meldung findet man unter „Telegramme und Ereignisse“; bei einem Befehl dort im Feld „IOA“ die IOA des Befehls eintragen.

| Beobachtung | Ursache meist | Was tun |
| --- | --- | --- |
| „getrennt – an Adresse und Port lauscht niemand“ | falsche Adresse oder Port, Station aus | Adresse, Port, Netzweg mit dem Betreiber prüfen |
| „getrennt – keine Antwort innerhalb t0“ | Station im Netz nicht erreichbar | Netzweg, VPN, Freigaben im Netz |
| „getrennt – Gegenstelle hat die Verbindung beendet“ gleich nach dem Verbinden | Station lässt diese Leitstelle (IP-Adresse) nicht zu oder hat schon eine aktive Leitstelle | Betreiber fragen, welche Adressen zugelassen sind |
| bleibt bei „STARTDT gesendet, warte auf Bestätigung“, dann „getrennt – Gegenstelle hat nicht rechtzeitig quittiert (t1)“ | Station erwartet eine andere Leitstelle oder Redundanz, seltener ein längeres t1 | Betreiber fragen; t1 abgleichen |
| Generalabfrage oder Befehl negativ, Ursache 46 („Station unbekannt“) | falsche gemeinsame Adresse, oben oder in der Werteliste | „Station (CA)“ oben und Spalte „Stat.“ der Werteliste prüfen; „aus der Werteliste“ übernimmt die Station der Liste |
| Befehl „abgelehnt“, Ursache 47 („IOA unbekannt“) | IOA unbekannt | IOA und Typ aus der Liste des Betreibers |
| Befehl „abgelehnt“, Ursache 7 negativ | Station verlangt Select dann Execute, Wert nicht erlaubt oder Befehl gesperrt | Ablauf „Select, dann Execute“; Wert prüfen; Betreiber fragen |
| Befehl „keine Beendigung“ | Station sendet kein ACTTERM | Haken „Befehle enden mit ACTTERM“ entfernen, neu verbinden |
| Rückmeldung bleibt bei „IOA … …“ (die Rückmelde-IOA, z. B. „IOA 1001 …“) | falsche Rückmelde-IOA, oder die Station meldet den neuen Stand nicht spontan | Rückmelde-IOA mit der Liste des Betreibers vergleichen; Zeile der Rückmelde-IOA im Prozessabbild ansehen |
| oben rot „i/o error“ oder „invalid argument“ nach „Verbinden“ mit TLS | Datei für CA, Zertifikat oder Schlüssel fehlt, ist nicht lesbar, oder CA-Feld leer | Ereignis „TLS settings: …“ unter Telegramme lesen; vollständige Pfade eintragen |
| „getrennt – TLS-Fehler, siehe TLS-Ereignis“ | Zertifikat, CA oder Name passen nicht, oder die Station spricht auf diesem Port kein TLS | Ereignis „TLS …“ lesen; Angaben des Betreibers, `docs/08_Interop.md`, Abschnitt 5.3 |

Weitere Fehlerbilder für Entwickler: `docs/08_Interop.md`, Abschnitt 5.3.
