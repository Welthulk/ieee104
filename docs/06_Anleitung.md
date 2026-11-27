# Anleitung: Library verwenden

Stand: 25.11.2026 · Schnittstelle: `include/iec104/iec104.h` · Beispiele: `examples/` · Julia: `docs/07_Julia.md`

## 1. Einbinden

Installieren:

```bash
cmake --preset release && cmake --build --preset release
cmake --install build/release --prefix /opt/iec104
```

| Variante | Einstellung |
| --- | --- |
| statisch (Default) | – |
| geteilt (`libiec104.so`) | `-DBUILD_SHARED_LIBS=ON` oder Preset `shared` |
| ohne TLS (ohne OpenSSL) | `-DIEC104_TLS=OFF`; `*_set_tls()` liefert dann `IEC104_ERR_UNSUPPORTED` |

### Fertig gebaute Library

`iec104-<version>-linux-x86_64.zip` enthält nur das Nötigste zum Verwenden, ohne selbst zu bauen:

| Datei | Inhalt |
| --- | --- |
| `lib/libiec104.so.1` | geteilte Library (Release, mit TLS), eine Datei ohne Symlinks |
| `include/iec104/iec104.h` | Header |
| `LICENSES/` | Lizenztexte |
| `README.md` | Voraussetzungen, Übersetzen, Starten |

| Voraussetzung | Mindestens |
| --- | --- |
| System | Linux x86-64: AlmaLinux/RHEL 9, Ubuntu ab 22.04, Debian 12, auch unter WSL2 |
| glibc, libstdc++ | 2.34, GCC 11 |
| OpenSSL | 3.0 (`libssl.so.3`, auf diesen Systemen vorhanden) |

```bash
gcc -Iinclude meine_app.c lib/libiec104.so.1 -Wl,-rpath,'$ORIGIN/lib' -o meine_app
```

Für `-liec104` einmal `ln -s libiec104.so.1 lib/libiec104.so` anlegen. `find_package(iec104)` braucht die installierte Variante (oben), nicht das Zip.

Eigenes CMake-Projekt:

```cmake
find_package(iec104 1.0 REQUIRED)          # CMAKE_PREFIX_PATH=/opt/iec104
target_link_libraries(meine_app PRIVATE iec104::iec104)
```

Ohne CMake: `-I/opt/iec104/include -L/opt/iec104/lib -liec104`; statisch zusätzlich `-lssl -lcrypto -lstdc++ -lm`.

## 2. Hello World

Zwei Programme in `examples/`: ein Server mit einem Messwert und ein Client, der ihn abfragt.

```bash
cmake -S examples -B build-ex -DCMAKE_PREFIX_PATH=/opt/iec104 && cmake --build build-ex
build-ex/hello_server 2404 &
build-ex/hello_client 127.0.0.1 2404
```

Ausgabe des Clients:

```
state: stopped
state: start pending
state: started
type  70  cause  4  station 1  IOA 0  value 0
type 100  cause  7  station 1  IOA 0  value 0
type  13  cause 20  station 1  IOA 100  value 2
type 100  cause 10  station 1  IOA 0  value 0
type  13  cause  3  station 1  IOA 100  value 3
type  13  cause  3  station 1  IOA 100  value 4
type  13  cause  3  station 1  IOA 100  value 5
```

| Zeile | Bedeutung |
| --- | --- |
| type 70, cause 4 | Ende der Initialisierung (Server nach STARTDT) |
| type 100, cause 7 | Bestätigung der Generalabfrage |
| type 13, cause 20 | Messwert aus der Generalabfrage |
| type 100, cause 10 | Ende der Generalabfrage |
| type 13, cause 3 | spontane Änderung |

### Server (`examples/hello_server.c`, Kern)

```c
iec104_callbacks callbacks = {0};
callbacks.on_command = on_command;               /* Befehle: 1 = annehmen, 0 = ablehnen */

iec104_server* server = NULL;
iec104_server_create(NULL, &callbacks, &server); /* NULL = Normwerte (t0..t3, k, w) */

iec104_object value = {0};
value.type = 13;                                 /* M_ME_NC_1, Gleitkomma */
value.ioa = 100;
iec104_server_add_point(server, 1, &value, 0x0001);  /* Station 1, Abfragegruppe 1 */

iec104_server_listen(server, NULL, 2404);
for (;;) {
    iec104_server_poll(server, 100);             /* Empfang, Timer, Antworten, Callbacks */
    value.value += 1.0;
    iec104_server_update(server, 1, &value);     /* spontan an alle gestarteten Verbindungen */
}
```

Generalabfrage, Zählerabfrage, Lesebefehl, Testbefehl und STARTDT/STOPDT beantwortet die Library selbst.

### Client (`examples/hello_client.c`, Kern)

```c
iec104_callbacks callbacks = {0};
callbacks.on_state = on_state;                   /* STOPPED, STARTED, CLOSED, ... */
callbacks.on_object = on_object;                 /* jedes empfangene Objekt als Wert */

iec104_client* client = NULL;
iec104_client_create(NULL, &callbacks, &client);
iec104_client_connect(client, "127.0.0.1", 2404);
for (;;) {
    iec104_client_poll(client, 100);
    if (state == IEC104_LINK_STOPPED && !started)     { iec104_client_start(client); started = 1; }
    if (state == IEC104_LINK_STARTED && !interrogated) {
        iec104_client_interrogation(client, 1, 20);     /* Station 1, alle Punkte */
        interrogated = 1;
    }
}
```

## 3. Grundbegriffe

| Begriff | Bedeutung |
| --- | --- |
| Handle | `iec104_server*` (gesteuerte Station, beliebig viele Verbindungen) oder `iec104_client*` (steuernde Station, eine Verbindung) |
| Poll | `iec104_server_poll()` / `iec104_client_poll()` erledigt alles: Empfang, Timer, Senden, Callbacks. Regelmäßig aufrufen (z. B. alle 10–100 ms) |
| Callbacks | `on_state`, `on_object`, `on_command` (Server), `on_command_result` (Client), `on_asdu` (Rohdaten); jeder Zeiger darf NULL sein |
| Objekt | `iec104_object`: Typ, IOA, Wert, Qualität, Zeitmarke, Qualifier; Belegung je Typ siehe Tabelle im Header |
| Verbindungs-Id | `connection` in Callbacks und Server-Funktionen; Client immer 1 |
| Status | jede Funktion liefert `iec104_status`; Text mit `iec104_status_string()` |

Zustände einer Verbindung:

```
CONNECTING (nur Client) -> STOPPED -> (STARTDT) -> STARTED -> (STOPDT) -> STOPPED
                                  jederzeit -> CLOSED (Grund: iec104_close_reason)
```

Daten fließen nur in STARTED.

## 4. Typische Aufgaben

### Server

| Aufgabe | Aufruf |
| --- | --- |
| Datenpunkt anlegen | `iec104_server_add_point(server, station, &obj, gruppen)` |
| Wert ändern und spontan melden | `iec104_server_update(server, station, &obj)` |
| zyklisch oder mit eigener Ursache senden | `iec104_server_send(server, 0, station, IEC104_COT_PERIODIC, objs, n)` |
| Befehle entscheiden | `on_command` liefert 1 (positive Bestätigung, danach Beendigung) oder 0 |
| Befehlsmodell je IOA | `iec104_server_add_command(server, station, ioa, typ, IEC104_COMMAND_SELECT_EXECUTE, 5000)` |
| verspätete Befehle verwerfen | `iec104_server_options.max_command_delay_ms` |
| Ende der Initialisierung, Beendigung abschalten | `iec104_server_options` |

### Client

| Aufgabe | Aufruf |
| --- | --- |
| Generalabfrage | `iec104_client_interrogation(client, station, 20)` (Gruppe n: 20 + n) |
| einzelnen Punkt lesen | `iec104_client_read(client, station, ioa)`; Antwort über `on_object` mit Ursache 5 |
| Zählerabfrage | `iec104_client_counter_interrogation(client, station, 5, 0)` |
| Uhrzeit setzen | `iec104_client_clock_sync(client, station, &zeit)`; `iec104_time_from_unix_ms()` |
| Befehl direkt | `iec104_client_send_command(client, station, &cmd)` |
| Befehl mit Select/Execute | `iec104_client_select_execute(client, station, &cmd)` |
| Ergebnis eines Befehls | `on_command_result`: `TERMINATED`, `REJECTED`, `NO_CONFIRMATION`, … |
| Timeouts für Befehle | `iec104_client_set_options()` |

Beispiel Befehl:

```c
iec104_object cmd = {0};
cmd.type = 45;      /* C_SC_NA_1 Einzelbefehl */
cmd.ioa = 6000;
cmd.value = 1;      /* EIN */
iec104_client_select_execute(client, 1, &cmd);   /* Ergebnis kommt über on_command_result */
```

### Beide

| Aufgabe | Aufruf |
| --- | --- |
| Timer, k, w, ASDU-Profil | `iec104_link_config` vor `*_create()` |
| Log | `iec104_server_set_log()` / `iec104_client_set_log()`; Stufe `IEC104_LOG_TRACE` zeigt jede APDU als Hexdump |
| Statistik | `iec104_server_get_stats(server, id, &s)` / `iec104_client_get_stats(client, &s)` |
| Rohdaten (Typen außerhalb der Werte-API) | `on_asdu`, `iec104_server_send_asdu()`, `iec104_client_send_asdu()` |

## 5. Konfigurationsdatei

```ini
# Gegenstelle mit langen Timern
host = 10.0.0.5
port = 2404
t1 = 250
t2 = 240
t3 = 255
max_command_delay = 10
confirmation_timeout = 20
```

```c
iec104_config cfg;
iec104_config_init(&cfg);
int zeile = 0;
if (iec104_config_load("anlage.conf", &cfg, &zeile) != IEC104_OK) { /* Fehler in Zeile 'zeile' */ }
iec104_client_create(&cfg.link, &callbacks, &client);
iec104_client_set_options(client, &cfg.client);
iec104_client_connect(client, cfg.host, cfg.port);
```

Alle Schlüssel: Kommentar zu `iec104_config_load()` im Header. Zeiten in Sekunden.

## 6. Redundanz und Pufferung

Eine Leitstelle hält zur Station oft zwei oder mehr Verbindungen: eine überträgt, die anderen stehen bereit. Diese Verbindungen bilden eine Redundanzgruppe. Spontane Daten (`iec104_server_update()`, `iec104_server_send()` an alle) laufen durch den Ereignispuffer der Gruppe und bleiben dort, bis die Leitstelle sie quittiert hat.

```c
iec104_group_config g;
iec104_group_config_init(&g);                   /* 1000 Ereignisse, älteste verwerfen */
strcpy(g.peers[0], "10.0.1.10");                /* Leitstelle, Rechner 1 */
strcpy(g.peers[1], "10.0.2.10");                /* Leitstelle, Rechner 2 */
uint32_t gruppe;
iec104_server_add_group(server, &g, &gruppe);   /* vor iec104_server_listen() */
```

| Situation | Verhalten |
| --- | --- |
| keine Verbindung gestartet | Ereignisse sammeln sich im Puffer |
| STARTDT auf einer Verbindung | sie wird aktiv und erhält zuerst den Puffer, danach laufende Ereignisse |
| STOPDT, dann STARTDT auf einer anderen | Umschaltung ohne Verlust; die Leitstelle macht danach üblicherweise eine Generalabfrage |
| STARTDT auf einer zweiten, ohne STOPDT auf der ersten | die zweite übernimmt (Warnung im Log) |
| Abbruch der aktiven Verbindung | Unquittiertes geht auf der nächsten aktiven erneut hinaus (doppelt möglich, nie verloren) |
| Puffer voll | ältestes Ereignis verwerfen, oder mit `IEC104_OVERFLOW_REJECT` das neue ablehnen (`IEC104_ERR_WOULD_BLOCK`) |
| zyklische Daten (Ursache 1, 2) | nicht gepuffert, nur an aktive Verbindungen |
| Verbindung von unbekannter Adresse | abgewiesen, sofern keine Gruppe ohne Adressliste existiert |
| keine Gruppe angelegt | jede Verbindung ist ihre eigene Gruppe: Pufferung während STOPDT, endet mit der Verbindung |

| Aufgabe | Aufruf |
| --- | --- |
| Zustand und Zähler einer Gruppe | `iec104_server_get_group_stats(server, gruppe, &s)`: aktive Verbindung, Füllstand, verworfen, erneut gesendet, Umschaltungen |
| Gruppe einer Verbindung | `iec104_server_connection_group(server, verbindung)` |
| Puffer ohne Gruppen | `iec104_server_options.event_buffer_size`, `.overflow` |

Auf der Seite der Leitstelle sind zwei Client-Handles nötig, je eines pro Verbindung:

```c
iec104_client_connect(haupt, "10.0.0.5", 2404);
iec104_client_connect(reserve, "10.0.0.5", 2404);   /* bleibt in STOPDT, TESTFR läuft */
iec104_client_start(haupt);
/* ... Umschalten: */
iec104_client_stop(haupt);                          /* STOPDT con kommt, wenn alles quittiert ist */
iec104_client_start(reserve);                       /* sobald haupt STOPPED meldet */
iec104_client_interrogation(reserve, 1, 20);
```

In der Konfigurationsdatei eine Zeile je Gruppe (`*` = jede Adresse):

```ini
group = 10.0.1.10, 10.0.2.10
event_buffer_size = 5000
overflow = drop_oldest
```

```c
for (uint32_t i = 0; i < cfg.group_count; ++i) iec104_server_add_group(server, &cfg.groups[i], NULL);
```

### Zwei Wege (Wegeredundanz der Leitstelle)

Liefern zwei Stationen denselben Prozess (gleiche gemeinsame Adresse, gleiche IOAs), hält ein Client zu beiden je eine gestartete Verbindung. Die Anwendung sieht eine logische Verbindung (Kennung 1); zugestellt wird nur der führende Weg. Verfahren und Ablaufbild: `docs/02_Technik_und_Quellen.md`, Abschnitt Redundanz; Festlegungen: `docs/05_Normfragen_und_Festlegungen.md`, E11 und E12.

```c
iec104_path_config wege[2] = {0};
strcpy(wege[0].host, "10.0.1.5"); wege[0].port = 2404;   /* Station A */
strcpy(wege[1].host, "10.0.2.5"); wege[1].port = 2404;   /* Station B */
iec104_path_options o;
iec104_path_options_init(&o);                             /* Neuaufbau 5 s, Fenster t1 + t3 + 5 s */
iec104_client_set_path_callback(client, bei_umschaltung, NULL);
iec104_client_connect_paths(client, wege, 2, &o);
iec104_client_start(client);                              /* einmal: jeder Weg startet, sobald er steht */
/* ... wie mit einer Verbindung: poll, Befehle, Abfragen ... */
iec104_client_close(client);                              /* beendet alle Wege */
```

| Aufgabe | Aufruf |
| --- | --- |
| führender Weg | `iec104_client_leading_path(client)`: 1…n, 0 = keiner |
| Zustand und Zähler eines Wegs | `iec104_client_get_path_info(client, 2, &info)`: Zustand, führend, Verbindungsaufbauten, mitgelaufen, nachgeliefert, unterdrückt, verloren, im Puffer, Link-Statistik |
| Meldung jeder Umschaltung | `iec104_client_set_path_callback()`: neuer und alter Weg, Grund, Anzahlen; zusätzlich Warnung im Log |
| Weg stören (Test) | `iec104_client_disturb_path(client, 1, IEC104_PATH_SILENT)`; `IEC104_PATH_CLOSE`, `IEC104_PATH_RESTORE` |
| TLS je Weg | `tls_peer_name`, `tls_peer_fingerprint` in `iec104_path_config`; leer = wie in `iec104_tls_config` |

| Parameter (`iec104_path_options`) | Standard | Wirkung |
| --- | --- | --- |
| `reconnect_interval_ms` | 5000 | Abstand zwischen zwei Versuchen, einen verlorenen Weg aufzubauen; ohne Ende |
| `replay_window_ms` | 0 = t1 + t3 + 5 s | so lange puffert ein mitlaufender Weg; muss die Erkennungszeit decken |
| `replay_max_objects` | 10000 | Obergrenze je Weg; ältere fallen heraus (Zähler „verloren“) |
| `replay_tolerance_ms` | 500 | so weit dürfen die Empfangszeiten desselben Werts auf beiden Wegen auseinanderliegen |
| `interrogation_after_switch` | 1 | Generalabfrage nach jeder Umschaltung |
| `prefer_first_path` | 0 | 1: Weg 1 führt wieder, sobald er zurück ist |

| Verhalten | |
| --- | --- |
| logischer Zustand (`on_state`, Kennung 1) | „gestartet“, solange ein Weg führt; führt keiner, der Zustand des am weitesten gekommenen Wegs („verbinde“, „gestoppt“, „STARTDT/STOPDT läuft“); „getrennt“ nur nach `iec104_client_close()` |
| Anhalten und Schließen | `iec104_client_stop()` hält alle Wege an ohne Umschaltung (ein Rückruf mit führend = 0); `iec104_client_close()` beendet alle ohne Rückruf |
| Namen | ein Rechnername wird einmal bei `iec104_client_connect_paths()` aufgelöst; der Neuaufbau verwendet die Adresse und wartet nicht auf DNS. Ist ein Name dort nicht auflösbar, starten die anderen Wege trotzdem, und der Name wird bei jedem Neuaufbau wieder gesucht (das kann den Poll kurz aufhalten; Adressen statt Namen vermeiden es) |
| TLS | jeder Weg hat seinen eigenen TLS-Kontext und Sitzungsspeicher |
| Doppelte | um eine Umschaltung möglich (z. B. Wert ohne Gegenstück auf dem alten Weg, Generalabfrage); Verluste nicht, solange das Fenster reicht |
| Timer | ein schweigender Weg fällt erst nach t3 + t1 auf (Standard 35 s); für schnelles Umschalten t1 und t3 kürzer wählen, z. B. t1 = 5 s, t3 = 5 s |
| Voraussetzung | beide Stationen dürfen gleichzeitig gestartet sein und senden dieselben Punkte; sonst ist es Kanalredundanz (oben) |
| Grenze | kein Verfahren der Norm; eine Umsetztabelle für abweichende Adressen je Weg gibt es nicht |

In der Konfigurationsdatei eine Zeile je Weg; IPv6 in Klammern:

```ini
path = 10.0.1.5:2404
path = [2001:db8::5]:2404 station-b.example     # optional TLS-Name und Fingerabdruck des Wegs
path_reconnect = 5
path_replay_window = 40
path_replay_tolerance = 0.5
path_interrogation = yes
path_prefer_first = no
```

```c
iec104_client_connect_paths(client, cfg.paths, cfg.path_count, &cfg.path_options);
```

## 7. TLS

IEC 104 selbst ist ungeschützt. Mit TLS nach IEC 62351-3 laufen dieselben APDUs verschlüsselt und beide Seiten weisen sich mit Zertifikat aus. Standardport mit TLS: 19998 (`IEC104_DEFAULT_TLS_PORT`).

```c
iec104_tls_config tls;
iec104_tls_config_init(&tls);                       /* Profil N12: TLS 1.2/1.3, gegenseitige Prüfung */
strcpy(tls.ca_file, "/etc/iec104/ca.pem");          /* wem vertraut wird */
strcpy(tls.certificate_file, "/etc/iec104/station.pem");
strcpy(tls.private_key_file, "/etc/iec104/station.key");
strcpy(tls.crl_file, "/etc/iec104/crl.pem");        /* optional */

iec104_server_set_tls(server, &tls);                /* vor listen; Client: iec104_client_set_tls vor connect */
iec104_server_listen(server, NULL, IEC104_DEFAULT_TLS_PORT);
```

Alles Weitere bleibt gleich: Callbacks, Senden, Poll.

| Einstellung | Feld | Default |
| --- | --- | --- |
| CA, eigenes Zertifikat, Schlüssel | `ca_file`, `certificate_file`, `private_key_file` | – (CA immer, Zertifikat am Server Pflicht) |
| verschlüsselter Schlüssel | `key_password` | – |
| Sperrliste | `crl_file` | keine Prüfung |
| Name der Gegenstelle | `peer_name` (DNS-Name oder Adresse) | keine Namensprüfung |
| Zertifikat festnageln | `peer_fingerprint` (SHA-256, hex) | aus |
| Versionen | `min_version`, `max_version` | 1.2 bis 1.3 |
| Suites | `cipher_list` (1.2), `cipher_suites` (1.3), OpenSSL-Schreibweise | ECDHE mit AEAD |
| Schlüsselerneuerung | `renewal_interval_s` | aus |
| Renegotiation (1.2) | `renegotiation` | erlaubt (nur RFC 5746) |
| Session Resumption | `session_resumption`, `session_lifetime_s` | an, 3600 s |
| Gegenstelle prüfen | `verify_peer` | 1; 0 nur für Tests |

| Thema | Verhalten |
| --- | --- |
| Fehler in den Einstellungen | `*_set_tls()` meldet ihn sofort (`IEC104_ERR_IO`: Datei, `IEC104_ERR_INVALID_ARGUMENT`: Inhalt), Grund im Log |
| Handshake | gehört zum Verbindungsaufbau und muss innerhalb von t0 fertig sein (E9) |
| Abgelehnte Gegenstelle | Verbindung zu, Grund `IEC104_CLOSE_TLS`; `on_security` nennt die Ursache, z. B. `certificate has expired` |
| Zertifikatswechsel | `*_set_tls()` erneut aufrufen; gilt für neue Verbindungen, offene laufen weiter |
| Details einer Verbindung | `iec104_server_get_tls_info()` / `iec104_client_get_tls_info()`: Version, Suite, Zertifikat der Gegenstelle |

In der Konfigurationsdatei (relative Pfade gelten ab dem Verzeichnis der Datei):

```ini
port = 19998
tls = 1
tls_ca = pki/ca.pem
tls_certificate = pki/station.pem
tls_key = pki/station.key
tls_crl = pki/crl.pem
tls_peer_name = leitstelle.example
```

```c
if (cfg.use_tls) iec104_client_set_tls(client, &cfg.tls);
```

## 8. Regeln

| Regel | Grund |
| --- | --- |
| Ein Handle wird nur von einem Thread benutzt | keine internen Sperren; Poll, Senden und Callbacks laufen im Thread der Anwendung (E8) |
| In Callbacks dürfen Sende-, Start- und Stopp-Funktionen desselben Handles aufgerufen werden, `*_destroy()` nicht | das Handle wird gerade benutzt |
| `IEC104_ERR_WOULD_BLOCK` beim Senden: pollen und erneut senden | Fenster k voll |
| Zeiger in Callbacks gelten nur während des Aufrufs | Puffer der Library |
| Zeitmarken: UTC | `iec104_time` hat keine Zeitzone (N13) |
| TLS: `peer_name` oder `peer_fingerprint` setzen, wenn die CA auch andere Stationen ausstellt | sonst wird jedes gültige Zertifikat der CA angenommen |
