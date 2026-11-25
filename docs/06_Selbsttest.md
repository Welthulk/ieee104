# Selbsttest und manuelle Tests

Diese Anleitung beschreibt, wie die Library ohne fremde Gegenstelle geprüft wird und wie die beiden Selbsttest-Programme für manuelle Tests gegen andere IEC-104-Geräte eingesetzt werden.

## 1. Überblick

| Programm | Rolle | Was es tut |
| --- | --- | --- |
| `iec104_selftest_server` | gesteuerte Station (Unterstation) | lauscht auf einem TCP-Port, sendet nach STARTDT spontane Messwerte (Typ 11, M_ME_NB_1, IOA 100, Werte 0, 1, 2, …), bestätigt jeden empfangenen Sollwertbefehl (Typ 49, C_SE_NB_1) mit Übertragungsursache 7 |
| `iec104_selftest_client` | steuernde Station (Leitstelle) | verbindet sich, sendet STARTDT, sendet Sollwertbefehle (IOA 5000, Werte 0, 1, 2, …), prüft Messwerte und Bestätigungen auf Vollständigkeit und Reihenfolge, hält optional eine Ruhephase, sendet STOPDT, schließt und gibt einen Bericht aus |

Gemeinsame Adresse (CA) ist 1, Profil IEC 104 (COT 2, CA 2, IOA 3 Oktette).

Der Client prüft schrittweise und meldet jeden Schritt mit `ok` oder `FAILED`:

```
iec104_selftest_client: 127.0.0.1:2404
  connect .......................... ok
  start data transfer (STARTDT) .... ok
  measured values received ......... 100/100, in order
  commands confirmed ............... 10/10, in order
  idle period (link test) .......... 25 s, ok
  stop data transfer (STOPDT) ...... ok
result: PASSED
```

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

Erwartung: Client `result: PASSED`; der Server meldet je Verbindung `connection N result: PASSED`. Der Server läuft weiter und nimmt neue Verbindungen an, bis er mit Strg-C beendet wird.

### 4.2 Client gegen eine fremde Unterstation

```bash
iec104_selftest_client --host <Gerät> --port 2404 --expect 0 --count 0 --idle 60
```

Mit `--expect 0 --count 0` werden nur Verbindungsaufbau, STARTDT, Ruhephase mit Verbindungstest und STOPDT geprüft. Daten des Geräts werden gezählt und als „other ASDUs received (ignored)“ ausgewiesen. Generalabfrage und Befehle an fremde Geräte folgen mit der Session-Schicht.

### 4.3 Fremde Leitstelle gegen den Selbsttest-Server

```bash
iec104_selftest_server --port 2404 --count 100 --interval 100 --verbose
```

Mit `--verbose` gibt der Server jeden Zustandswechsel und jede empfangene ASDU aus. Die Leitstelle sollte nach STARTDT die Messwerte 0 bis 99 an IOA 100 anzeigen.

### 4.4 Abweichende Parameter der Gegenstelle

Timer, k und w lassen sich auf beiden Seiten setzen (Timer in Sekunden):

```bash
iec104_selftest_client --host <Gerät> --t1 250 --t2 240 --t3 255 --k 12 --w 8
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
| `--count N` | Messwerte je Verbindung | 100 |
| `--interval MS` | Abstand zwischen zwei Messwerten, 0 = so schnell wie möglich | 10 |
| `--duration S` | Laufzeit, 0 = bis Strg-C | 0 |
| `--once` | nach der ersten Verbindung beenden, Exit-Code wie beim Client | aus |
| `--port-file F` | tatsächlichen Port in Datei F schreiben | – |

### Client

| Option | Bedeutung | Default |
| --- | --- | --- |
| `--host ADDR` | Adresse des Servers | 127.0.0.1 |
| `--port N` | TCP-Port | 2404 |
| `--count N` | zu sendende Sollwertbefehle | 10 |
| `--expect N` | erwartete Messwerte | 100 |
| `--idle S` | Ruhephase vor STOPDT, größer als t3 wählen | 0 |
| `--timeout S` | Grenze je Schritt | 60 |

### Beide

| Option | Bedeutung | Default |
| --- | --- | --- |
| `--t0 … --t3 S` | Timer in Sekunden | 30, 15, 10, 20 |
| `--k N`, `--w N` | Fenstergrößen | 12, 8 |
| `--tolerant` | bekannte Abweichungen zulassen | aus (strikt) |
| `--verbose` | jeden Zustandswechsel und jede ASDU ausgeben | aus |

## 6. Was der Selbsttest abdeckt und was nicht

Abgedeckt: TCP-Verbindungsaufbau und -abbau, STARTDT/STOPDT, Folgenummern und Quittierung über viele Rahmen (mehr als k), Verbindungstest nach t3, Zeitüberwachung t1, Kodierung und Dekodierung der verwendeten Typen.

Noch nicht abgedeckt (folgt mit der Session-Schicht): Generalabfrage, Uhrzeitsynchronisation, Zählerabfrage, Befehle mit Select/Execute und Beendigung (ACTTERM), TLS, Redundanzgruppen.
