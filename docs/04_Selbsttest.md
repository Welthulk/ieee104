# Selbsttest und manuelle Tests

Diese Anleitung beschreibt, wie die Library ohne fremde Gegenstelle geprüft wird und wie die beiden Selbsttest-Programme für manuelle Tests gegen andere IEC-104-Geräte eingesetzt werden. Beide Programme nutzen ausschließlich die öffentliche C-Schnittstelle (`include/iec104/iec104.h`) und sind damit zugleich Beispiele für deren Verwendung.

## 1. Überblick

| Programm | Rolle | Was es tut |
| --- | --- | --- |
| `iec104_selftest_server` | gesteuerte Station (Unterstation), gemeinsame Adresse 1 | hält ein Prozessabbild, beantwortet Generalabfrage, Zählerabfrage, Lesebefehl und Testbefehl, nimmt Sollwerte, Einzelbefehle und Uhrzeit an, sendet nach STARTDT „Ende der Initialisierung“ und eine Reihe spontaner Messwerte |
| `iec104_selftest_client` | steuernde Station (Leitstelle) | verbindet sich, prüft jeden Ablauf Schritt für Schritt und gibt einen Bericht aus; mit `--foreign` gegen beliebige IEC-104-Server |

Prozessabbild des Selbsttest-Servers:

| IOA | Typ | Inhalt | Gruppe |
| --- | --- | --- | --- |
| 100 | 11 (M_ME_NB_1, skaliert) | spontane Messreihe 0, 1, 2, … | Abfragegruppe 1 |
| 101 | 13 (M_ME_NC_1, Gleitkomma) | 49,95 | Abfragegruppe 1 |
| 300 | 1 (M_SP_NA_1, Einzelmeldung) | folgt dem Einzelbefehl an IOA 6000 | Abfragegruppe 2 |
| 200 | 15 (M_IT_NA_1, Zählwert) | 123456 | Zählergruppe 1 |

Befehle: Sollwert skaliert (Typ 49) an IOA 5000 und Einzelbefehl (Typ 45) an IOA 6000 werden angenommen, alle anderen abgelehnt (negative Bestätigung).

Der Client prüft im Selbsttest-Modus:

```
iec104_selftest_client: 127.0.0.1:2404, station 1
  connect .............................. ok
  start data transfer (STARTDT) ........ ok
  end of initialization (type 70) ...... ok
  general interrogation ................ 3/3 objects
  group interrogation 2 ................ 1/1 objects
  clock synchronization ................ ok
  counter interrogation ................ ok
  set point commands (type 49) ......... 10/10 confirmed
  single command with feedback ......... ok
  rejected command (negative ACTCON) ... ok
  unknown station (cause 46) ........... ok
  spontaneous values ................... 100/100, in order
  idle period (link test) .............. 25 s, ok
  stop data transfer (STOPDT) .......... ok
result: PASSED
```

| Schritt | Geprüft wird |
| --- | --- |
| end of initialization | Typ 70 mit Übertragungsursache 4 nach STARTDT |
| general interrogation | Bestätigung (Ursache 7), drei Datenpunkte mit Ursache 20, Beendigung (Ursache 10) |
| group interrogation 2 | nur IOA 300, Ursache 22 |
| clock synchronization | positive Bestätigung |
| counter interrogation | Bestätigung, Zählwert 123456 mit Ursache 37, Beendigung |
| set point commands | je Befehl Bestätigung und Beendigung |
| single command with feedback | Bestätigung, Beendigung und spontane Rückmeldung von IOA 300 = 1 |
| rejected command | negative Bestätigung für einen Sollwert an unbekannter IOA |
| unknown station | Generalabfrage an Station 99 wird mit Ursache 46 negativ beantwortet |
| spontaneous values | alle Werte der Messreihe vollständig und in der richtigen Reihenfolge |
| idle period | Verbindung bleibt über die Ruhephase bestehen (Testrahmen nach t3, Antwort innerhalb t1) |

Exit-Code: `0` = bestanden, `1` = ein Schritt fehlgeschlagen, `2` = falscher Aufruf oder Port nicht verfügbar.

## 2. Bauen

Voraussetzungen: Linux x86-64, CMake ab 3.21, GCC oder Clang mit C++17, GoogleTest (`tools/setup_tools.sh` installiert alles unter AlmaLinux 9).

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug          # alle automatischen Tests, darunter der Selbsttest über TCP
```

Die Programme liegen danach unter `build/debug/iec104_selftest_server` und `build/debug/iec104_selftest_client`. Mit `cmake --install build/debug --prefix /opt/iec104` landen sie in `/opt/iec104/bin`.

## 3. Automatischer Selbsttest

`ctest` startet `tools/selftest/run_selftest.sh`. Das Skript

1. startet den Server auf einem freien Port (`--port 0`, der Port steht anschließend in einer Datei),
2. startet den Client gegen diesen Port mit kurzem t3 (1 s) und 3 s Ruhe, damit auch der Verbindungstest mit TESTFR läuft,
3. wertet die Exit-Codes beider Programme aus.

Von Hand:

```bash
tools/selftest/run_selftest.sh build/debug/iec104_selftest_server build/debug/iec104_selftest_client
```

## 4. Manuelle Tests

### 4.1 Server und Client auf zwei Rechnern

Rechner A (Server):

```bash
iec104_selftest_server --port 2404 --count 1000 --interval 10
```

Rechner B (Client):

```bash
iec104_selftest_client --host <Adresse von A> --port 2404 --expect 1000 --count 50 --idle 30
```

Erwartung: Client `result: PASSED`; der Server meldet je Verbindung `connection N result: PASSED`. Der Server läuft weiter und nimmt neue Verbindungen an, bis er mit Strg-C beendet wird. Jede Verbindung erhält ihre eigene Messreihe ab 0.

### 4.2 Client gegen eine fremde Unterstation

```bash
iec104_selftest_client --host <Gerät> --port 2404 --foreign --ca <gemeinsame Adresse> --idle 60
```

Mit `--foreign` prüft der Client nur die Abläufe, nicht die Datenpunkte des Selbsttest-Servers: Verbindungsaufbau, STARTDT, Generalabfrage (Bestätigung und Beendigung, Anzahl der Objekte), Uhrzeitsynchronisation, Zählerabfrage, Ruhephase mit Verbindungstest und STOPDT. Lehnt das Gerät eine Anfrage normgerecht ab (Ursache 44 bis 47), meldet der Client „not supported (cause …)“; das gilt nicht als Fehler. Beispiel gegen einen Server, der keine Zählerabfrage kennt:

```
  general interrogation ................ 14 objects
  clock synchronization ................ confirmed
  counter interrogation ................ not supported (cause 44)
```

Mit `--verbose` gibt der Client jedes empfangene Objekt aus (Typ, Ursache, gemeinsame Adresse, IOA, Wert).

### 4.3 Fremde Leitstelle gegen den Selbsttest-Server

```bash
iec104_selftest_server --port 2404 --count 100 --interval 100 --verbose
```

Erwartung an die Leitstelle: Nach STARTDT kommt „Ende der Initialisierung“, eine Generalabfrage liefert IOA 100, 101 und 300, eine Zählerabfrage IOA 200, und die Messwerte an IOA 100 zählen hoch. Ein Einzelbefehl an IOA 6000 schaltet IOA 300. Mit `--verbose` gibt der Server jeden Zustandswechsel, jeden Befehl und die gesetzte Uhrzeit aus. Beim Schließen der Verbindung meldet er, wie viele Anfragen und Befehle er beantwortet hat.

### 4.4 Abweichende Parameter der Gegenstelle

Timer, k und w lassen sich auf beiden Seiten setzen (Timer in Sekunden):

```bash
iec104_selftest_client --host <Gerät> --foreign --t1 250 --t2 240 --t3 255 --k 12 --w 8
```

`--tolerant` erlaubt bekannte Abweichungen, zum Beispiel `--t3 0` (Verbindungstest abgeschaltet).

### 4.5 Fehlerfälle

| Test | Vorgehen | Erwartung |
| --- | --- | --- |
| Port falsch | Client gegen einen Port ohne Server | `connect … FAILED (connect failed)`, Exit-Code 1 |
| Server nicht erreichbar | Client gegen eine Adresse, die nicht antwortet, `--t0 5` | nach 5 s `FAILED (t0 timeout (connect))` |
| Gegenstelle antwortet nicht mehr | Server mit `kill -STOP <pid>` anhalten, während der Client in der Ruhephase ist (`--idle 60 --t3 5`) | nach t3 + t1 (5 s + 15 s) `idle period … FAILED (t1 timeout (no acknowledgement))` |
| Verbindungsabbruch | Server während der Übertragung mit Strg-C beenden | Client: `FAILED (closed by peer)` |
| Port belegt | zweiten Server auf demselben Port starten | `cannot listen on port 2404: i/o error`, Exit-Code 2 |

Nach `kill -STOP` den Server mit `kill -CONT <pid>` wieder fortsetzen oder beenden.

### 4.6 Mitschnitt mit Wireshark

Wireshark dekodiert IEC 104 auf Port 2404 von selbst. Für einen anderen Port: *Analyze → Decode As… → TCP port → IEC 60870-5-104*. Mitschnitt ohne Oberfläche:

```bash
tshark -i lo -f "tcp port 2404" -w selbsttest.pcapng
```

## 5. Optionen

### Server

| Option | Bedeutung | Default |
| --- | --- | --- |
| `--host ADDR` | lokale Adresse | alle |
| `--port N` | TCP-Port, 0 = freier Port | 2404 |
| `--count N` | spontane Messwerte je Verbindung | 100 |
| `--interval MS` | Abstand zwischen zwei Messwerten, 0 = so schnell wie möglich | 10 |
| `--duration S` | Laufzeit, 0 = bis Strg-C | 0 |
| `--once` | nach der ersten Verbindung beenden, Exit-Code wie beim Client | aus |
| `--port-file F` | tatsächlichen Port in Datei F schreiben | – |

### Client

| Option | Bedeutung | Default |
| --- | --- | --- |
| `--host ADDR` | Adresse des Servers | 127.0.0.1 |
| `--port N` | TCP-Port | 2404 |
| `--foreign` | beliebiger IEC-104-Server: nur Abläufe prüfen | aus |
| `--ca N` | gemeinsame Adresse der Station | 1 |
| `--count N` | zu sendende Sollwertbefehle | 10 |
| `--expect N` | erwartete spontane Messwerte | 100 |
| `--idle S` | Ruhephase vor STOPDT, größer als t3 wählen | 0 |
| `--timeout S` | Grenze je Schritt | 20 |

### Beide

| Option | Bedeutung | Default |
| --- | --- | --- |
| `--t0 … --t3 S` | Timer in Sekunden | 30, 15, 10, 20 |
| `--k N`, `--w N` | Fenstergrößen | 12, 8 |
| `--tolerant` | bekannte Abweichungen zulassen | aus (strikt) |
| `--verbose` | jeden Zustandswechsel und jedes Objekt ausgeben | aus |

## 6. Was der Selbsttest abdeckt und was nicht

Abgedeckt: TCP-Verbindungsaufbau und -abbau, STARTDT/STOPDT, Folgenummern und Quittierung über viele Rahmen (mehr als k), Verbindungstest nach t3, Zeitüberwachung t1, Ende der Initialisierung, Generalabfrage (Station und Gruppe), Zählerabfrage, Uhrzeitsynchronisation, Befehle mit Bestätigung und Beendigung, Rückmeldung eines Befehls, negative Bestätigungen (abgelehnter Befehl, unbekannte Station).

Noch nicht abgedeckt: Select/Execute mit Zeitüberwachung je Befehl, maximale Befehlsverzögerung für Befehle mit Zeitmarke, Pufferung bei unterbrochener Verbindung, TLS, Redundanzgruppen.
