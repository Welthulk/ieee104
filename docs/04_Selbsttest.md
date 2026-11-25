# Selbsttest und manuelle Tests

Zwei Programme prüfen die Library über TCP, gegeneinander oder gegen fremde Geräte. Sie nutzen nur die öffentliche C-API und dienen zugleich als Beispiel.

## 1. Überblick

| Programm | Rolle | Was es tut |
| --- | --- | --- |
| `iec104_selftest_server` | gesteuerte Station (Unterstation), gemeinsame Adresse 1 | hält ein Prozessabbild, beantwortet Generalabfrage, Zählerabfrage, Lesebefehl und Testbefehl, führt Befehle nach Befehlsmodell aus (direkt oder Select/Execute), verwirft verspätete Befehle mit Zeitmarke, nimmt die Uhrzeit an, sendet nach STARTDT „Ende der Initialisierung“ und eine Reihe spontaner Messwerte |
| `iec104_selftest_client` | steuernde Station (Leitstelle) | verbindet sich, prüft jeden Ablauf Schritt für Schritt und gibt einen Bericht aus; mit `--foreign` gegen beliebige IEC-104-Server |

Prozessabbild des Selbsttest-Servers:

| IOA | Typ | Inhalt | Gruppe |
| --- | --- | --- | --- |
| 100 | 11 (M_ME_NB_1, skaliert) | spontane Messreihe 0, 1, 2, … | Abfragegruppe 1 |
| 101 | 13 (M_ME_NC_1, Gleitkomma) | 49,95 | Abfragegruppe 1 |
| 300 | 1 (M_SP_NA_1, Einzelmeldung) | folgt dem Einzelbefehl an IOA 6000 | Abfragegruppe 2 |
| 200 | 15 (M_IT_NA_1, Zählwert) | 123456 | Zählergruppe 1 |

Befehlspunkte des Selbsttest-Servers:

| IOA | Typ | Befehlsmodell | Verhalten |
| --- | --- | --- | --- |
| 5000 | 49 (C_SE_NB_1, Sollwert skaliert) | direkt | ausführen, Bestätigung und Beendigung |
| 6000 | 45 (C_SC_NA_1, Einzelbefehl) | Select/Execute, Auswahl 2 s gültig | schaltet IOA 300 und meldet sie spontan |
| 6001 | 58 (C_SC_TA_1, Einzelbefehl mit Zeitmarke) | direkt | ausführen |

Befehle mit Zeitmarke, die älter als 10 s sind, verwirft der Server ohne Antwort (maximale Befehlsverzögerung nach AMD1). Befehle an andere IOAs lehnt er mit Ursache 47 ab.

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
  set point commands (direct) .......... 10/10 executed
  execute without select (rejected) .... rejected
  select and execute with feedback ..... executed (terminated)
  selection expires after 2 s .......... selected, then rejected
  command with time tag (type 58) ...... executed (terminated)
  late command discarded (max. delay) .. no confirmation
  unknown command IOA (cause 47) ....... rejected
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
| set point commands | direkter Befehlspunkt: jeder Sollwert wird bestätigt und beendet („executed“) |
| execute without select | Select/Execute-Punkt: Ausführen ohne vorherige Auswahl wird abgelehnt |
| select and execute | Auswahl und Ausführung in einem Aufruf; danach spontane Rückmeldung von IOA 300 = 1 |
| selection expires | nach einer Auswahl 2,5 s warten; die Ausführung wird dann abgelehnt |
| command with time tag | Befehl mit aktueller Zeitmarke wird ausgeführt |
| late command discarded | Befehl mit 60 s alter Zeitmarke bleibt ohne Antwort; der Client meldet nach 3 s „no confirmation“ |
| unknown command IOA | Befehl an eine IOA ohne Befehlspunkt wird mit Ursache 47 abgelehnt |
| unknown station | Generalabfrage an Station 99 wird mit Ursache 46 negativ beantwortet |
| spontaneous values | alle Werte der Messreihe vollständig und in der richtigen Reihenfolge |
| idle period | Verbindung bleibt über die Ruhephase bestehen (Testrahmen nach t3, Antwort innerhalb t1) |

Exit-Code: `0` = bestanden, `1` = ein Schritt fehlgeschlagen, `2` = falscher Aufruf oder Port nicht verfügbar.

## 2. Bauen

Voraussetzungen: Linux x86-64, CMake ab 3.21, GCC oder Clang mit C++17, GoogleTest, OpenSSL 3 mit Headern und dem Programm `openssl` (`tools/setup_tools.sh` installiert alles unter AlmaLinux 9).

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug          # alle automatischen Tests, darunter der Selbsttest über TCP und TLS
```

Der Build legt unter `build/debug/tls/` eine Test-PKI an (`tools/make_test_certs.sh`): CA, Server- und Client-Zertifikat, dazu gesperrte, abgelaufene und fremde Zertifikate für Negativtests. Nur für Tests.

Die Programme liegen danach unter `build/debug/iec104_selftest_server` und `build/debug/iec104_selftest_client`. Mit `cmake --install build/debug --prefix /opt/iec104` landen sie in `/opt/iec104/bin`.

## 3. Automatischer Selbsttest

`ctest` startet `tools/selftest/run_selftest.sh`. Das Skript

1. startet den Server auf einem freien Port (`--port 0`, der Port steht anschließend in einer Datei),
2. startet den Client gegen diesen Port mit kurzem t3 (1 s) und 3 s Ruhe, damit auch der Verbindungstest mit TESTFR läuft,
3. wertet die Exit-Codes beider Programme aus.

Ein zweiter Lauf macht dasselbe über TLS mit gegenseitiger Authentifizierung, Sperrliste, Namensprüfung und Schlüsselerneuerung alle 2 s. Ein dritter prüft die Umschaltung einer Redundanzgruppe (`--switchover`, Abschnitt 4.7).

Von Hand:

```bash
tools/selftest/run_selftest.sh build/debug/iec104_selftest_server build/debug/iec104_selftest_client
tools/selftest/run_selftest.sh build/debug/iec104_selftest_server build/debug/iec104_selftest_client \
    --tls build/debug/tls
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

Erwartung: Client `result: PASSED`; der Server meldet je Verbindung `connection N result: PASSED`. Der Server läuft weiter und nimmt neue Verbindungen an, bis er mit Strg-C beendet wird. Jede Verbindung erhält ihre eigene Messreihe ab 0. Der Test der maximalen Befehlsverzögerung setzt voraus, dass die Uhren beider Rechner auf wenige Sekunden genau übereinstimmen (z. B. per NTP).

### 4.2 Client gegen eine fremde Unterstation

```bash
iec104_selftest_client --host <Gerät> --port 2404 --foreign --ca <gemeinsame Adresse> --idle 60
```

Mit `--foreign` prüft der Client nur die Abläufe: Verbindung, STARTDT, Generalabfrage, Uhrzeit, Zählerabfrage, Ruhephase, STOPDT. Eine normgerechte Ablehnung (Ursache 44–47) meldet er als „not supported (cause …)“, nicht als Fehler:

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

Erwartung an die Leitstelle: Nach STARTDT kommt „Ende der Initialisierung“, eine Generalabfrage liefert IOA 100, 101 und 300, eine Zählerabfrage IOA 200, und die Messwerte an IOA 100 zählen hoch. Ein Einzelbefehl an IOA 6000 muss erst ausgewählt (Select) und dann innerhalb von 2 s ausgeführt werden; danach meldet IOA 300 den neuen Zustand. Ein direkter Einzelbefehl an IOA 6000 wird abgelehnt; so lässt sich prüfen, ob die Leitstelle Select/Execute beherrscht. Mit `--verbose` gibt der Server jeden Zustandswechsel, jeden Befehl und die gesetzte Uhrzeit aus. Beim Schließen der Verbindung meldet er, wie viele Anfragen und Befehle er beantwortet hat.

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
| Server nicht erreichbar | Client gegen eine Adresse, die nicht antwortet, `--t0 5` | nach 5 s `FAILED (t0 timeout (connect or TLS handshake))` |
| Gegenstelle antwortet nicht mehr | Server mit `kill -STOP <pid>` anhalten, während der Client in der Ruhephase ist (`--idle 60 --t3 5`) | nach t3 + t1 (5 s + 15 s) `idle period … FAILED (t1 timeout (no acknowledgement))` |
| Verbindungsabbruch | Server während der Übertragung mit Strg-C beenden | Client: `FAILED (closed by peer)` |
| Port belegt | zweiten Server auf demselben Port starten | `cannot listen on port 2404: i/o error`, Exit-Code 2 |

Nach `kill -STOP` den Server mit `kill -CONT <pid>` wieder fortsetzen oder beenden.

### 4.6 TLS

Mit eigenen Zertifikaten oder der Test-PKI (`P=build/debug/tls`):

```bash
iec104_selftest_server --port 19998 --tls-ca $P/ca.pem --tls-cert $P/server.pem --tls-key $P/server.key \
    --tls-crl $P/crl.pem --log info
iec104_selftest_client --host <Adresse> --port 19998 --tls-ca $P/ca.pem --tls-cert $P/client.pem \
    --tls-key $P/client.key --tls-peer-name localhost
```

Der Client zeigt nach dem Verbindungsaufbau Version, Suite und Zertifikat der Gegenstelle:

```
  1: TLS established: TLSv1.3, TLS_AES_128_GCM_SHA256, peer CN=localhost
  connect .............................. ok
  TLS .................................. TLSv1.3, TLS_AES_128_GCM_SHA256, peer CN=localhost
```

| Test | Client-Optionen statt `client.pem`/`client.key` | Erwartung am Server |
| --- | --- | --- |
| gesperrtes Zertifikat | `--tls-cert $P/revoked.pem --tls-key $P/revoked.key` | `peer certificate rejected: certificate revoked` |
| abgelaufenes Zertifikat | `--tls-cert $P/expired.pem --tls-key $P/expired.key` | `peer certificate rejected: certificate has expired` |
| fremde CA | `--tls-cert $P/foreign.pem --tls-key $P/foreign.key` | `peer certificate rejected: unable to get local issuer certificate` |
| ohne Zertifikat | `--tls` ohne `--tls-cert`/`--tls-key` | `TLS handshake failed: peer did not return a certificate` |
| ohne TLS | keine `--tls`-Option | `TLS handshake failed: …`, Client `FAILED (closed by peer)` |
| falscher Name | `--tls-peer-name other.test` | Client: `peer certificate rejected: hostname mismatch` |
| TLS 1.2 | `--tls-version 1.2` | `TLS established: TLSv1.2, ECDHE-…` |

Mit TLS 1.3 prüft der Server das Client-Zertifikat erst, wenn der Client seinen Teil des Handshakes schon abgeschlossen hat. Der Client meldet deshalb zunächst `connect … ok` und scheitert im nächsten Schritt (`TLS failure` oder `closed by peer`).

Gegenprobe ohne eigene Software, mit OpenSSL als Client (sendet STARTDT act und zeigt die Antwort als Hexdump, beginnend mit `6804 0b00 0000` = STARTDT con):

```bash
( printf '\x68\x04\x07\x00\x00\x00'; sleep 2 ) | openssl s_client -connect <Adresse>:19998 \
    -CAfile $P/ca.pem -cert $P/client.pem -key $P/client.key -quiet | xxd | head
```

`openssl s_client … -tls1_2 -msg` zeigt bei `--tls-renewal 1` am Server jede Sekunde HelloRequest und den neuen Handshake.

### 4.7 Redundanz und Umschaltung

Der Server sendet die Messreihe mit `--broadcast` als gemeinsame Ereignisse durch den Puffer der Redundanzgruppe, nicht je Verbindung. Der Client öffnet mit `--switchover` eine zweite Verbindung als Reserve und schaltet um, während die Reihe läuft (STOPDT auf der ersten, STARTDT auf der Reserve):

```bash
iec104_selftest_server --group 127.0.0.1 --broadcast --count 300 --interval 50 --stats
iec104_selftest_client --switchover --expect 300 --idle 5
```

```
  standby connection (stopped) ......... ok
  switch-over (STOPDT, STARTDT standby)  109 values before
  spontaneous values ................... 300/300, in order
  idle connection still open ........... ok
```

Am Server zeigt `--stats` am Ende die Zähler der Gruppe (Ereignisse, verworfen, erneut gesendet, Umschaltungen).

| Test | Vorgehen | Erwartung |
| --- | --- | --- |
| Umschaltung | wie oben | Reihe lückenlos und geordnet, doppelte Werte werden gezählt („repeated“) |
| fremde Leitstelle mit zwei Verbindungen | Server mit `--group <Adresse 1>,<Adresse 2> --broadcast --count 100000 --interval 1000 --log info`, an der Leitstelle umschalten | Log `group 1: active, N buffered events` bei jedem STARTDT; IOA 100 zählt ohne Lücke weiter |
| Umschalten ohne STOPDT | Leitstelle startet die zweite Verbindung, ohne die erste zu stoppen | Log `takes over from connection …`, Daten nur noch auf der zweiten |
| Abbruch | aktive Verbindung trennen (Kabel, `kill`), danach die Reserve starten | unquittierte Werte kommen auf der Reserve erneut |
| Pufferüberlauf | Server mit `--broadcast --buffer 10 --interval 1000 --log info`; Leitstelle startet, stoppt nach einigen Werten, startet nach 20 s wieder | Log `event buffer full`; nach STARTDT nur die letzten 10 Werte (Lücke von etwa 10) |
| unbekannte Adresse | Server mit `--group 192.0.2.1`, Client von einer anderen Adresse | Server-Log `rejected: not in any redundancy group`; Client `connect … ok`, dann `STARTDT … FAILED (closed by peer)` |

### 4.8 Mitschnitt mit Wireshark

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
| `--broadcast` | Messreihe als gemeinsame, gepufferte Ereignisse für alle Verbindungen | aus (Reihe je Verbindung) |
| `--group ADR,ADR` | Redundanzgruppe für diese Adressen der Leitstelle, `*` = jede; mehrfach möglich | keine (jede Verbindung eigene Gruppe) |
| `--buffer N` | Ereignispuffer je Gruppe | 1000 |
| `--overflow W` | voller Puffer: `drop_oldest` oder `reject` | `drop_oldest` |

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
| `--switchover` | zweite Verbindung als Reserve, Umschaltung während der Messreihe (Server mit `--broadcast`) | aus |

### Beide

| Option | Bedeutung | Default |
| --- | --- | --- |
| `--t0 … --t3 S` | Timer in Sekunden | 30, 15, 10, 20 |
| `--k N`, `--w N` | Fenstergrößen | 12, 8 |
| `--tolerant` | bekannte Abweichungen zulassen | aus (strikt) |
| `--verbose` | jeden Zustandswechsel und jedes Objekt ausgeben | aus |
| `--config F` | Parameter aus Datei F (`docs/06_Anleitung.md`, Abschnitt 5); Optionen auf der Kommandozeile überschreiben die Datei | – |
| `--log STUFE` | Log der Library: error, warning, info, debug, trace (trace = jede APDU als Hexdump) | aus |
| `--stats` | Zähler je Verbindung am Ende ausgeben | aus |

### TLS (beide)

Jede dieser Optionen schaltet TLS ein.

| Option | Bedeutung | Default |
| --- | --- | --- |
| `--tls` | TLS ohne weitere Angaben (nur zusammen mit `--config`) | aus |
| `--tls-ca F` | vertrauenswürdige CA (PEM) | – |
| `--tls-cert F`, `--tls-key F` | eigenes Zertifikat und Schlüssel | – |
| `--tls-crl F` | Sperrliste | keine |
| `--tls-peer-name N` | erwarteter Name oder Adresse im Zertifikat der Gegenstelle | keine Prüfung |
| `--tls-fingerprint H` | erwarteter SHA-256-Fingerprint der Gegenstelle | keine Prüfung |
| `--tls-version V` | höchste Version, 1.2 oder 1.3 | 1.3 |
| `--tls-renewal S` | Key Update bzw. Renegotiation alle S Sekunden | aus |

## 6. Was der Selbsttest abdeckt und was nicht

Abgedeckt: TCP-Verbindungsaufbau und -abbau, STARTDT/STOPDT, Folgenummern und Quittierung über viele Rahmen (mehr als k), Verbindungstest nach t3, Zeitüberwachung t1, Ende der Initialisierung, Generalabfrage (Station und Gruppe), Zählerabfrage, Uhrzeitsynchronisation, Befehle mit Bestätigung und Beendigung, Rückmeldung eines Befehls, negative Bestätigungen (abgelehnter Befehl, unbekannte IOA, unbekannte Station).

Ebenfalls abgedeckt: Befehlsmodelle je IOA (direkt, Select/Execute mit Zeitüberwachung der Auswahl), maximale Befehlsverzögerung für Befehle mit Zeitmarke, Befehlsverfolgung des Clients mit Ergebnis je Befehl.

Ebenfalls abgedeckt: TLS mit gegenseitiger Authentifizierung, Sperrliste, Namensprüfung und Schlüsselerneuerung. Negativfälle (fremde CA, abgelaufen, gesperrt, Fingerprint, Handshake-Timeout, Klartext gegen TLS) prüfen die Unit-Tests `Tls.*`.

Ebenfalls abgedeckt: Redundanzgruppe mit zwei Verbindungen, Umschaltung per STOPDT/STARTDT während laufender Daten. Abbruch mit unquittierten Ereignissen, Übernahme ohne STOPDT, Überlauf und Abweisung prüfen die Unit-Tests `Redundancy.*`.
