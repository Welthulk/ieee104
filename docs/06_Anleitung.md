# Anleitung: Library verwenden

Stand: 25.11.2026 · Schnittstelle: `include/iec104/iec104.h` · Beispiele: `examples/`

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

Eigenes CMake-Projekt:

```cmake
find_package(iec104 0.2 REQUIRED)          # CMAKE_PREFIX_PATH=/opt/iec104
target_link_libraries(meine_app PRIVATE iec104::iec104)
```

Ohne CMake: `-I/opt/iec104/include -L/opt/iec104/lib -liec104 -lstdc++ -lm`.

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

## 6. Regeln

| Regel | Grund |
| --- | --- |
| Ein Handle wird nur von einem Thread benutzt | keine internen Sperren; Poll, Senden und Callbacks laufen im Thread der Anwendung (E8) |
| In Callbacks dürfen Sende-, Start- und Stopp-Funktionen desselben Handles aufgerufen werden, `*_destroy()` nicht | das Handle wird gerade benutzt |
| `IEC104_ERR_WOULD_BLOCK` beim Senden: pollen und erneut senden | Fenster k voll |
| Zeiger in Callbacks gelten nur während des Aufrufs | Puffer der Library |
| Zeitmarken: UTC | `iec104_time` hat keine Zeitzone (N13) |
