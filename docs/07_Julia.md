# Anleitung: Library aus Julia verwenden

Stand: 25.11.2026 · geprüft mit Julia 1.13 · Dateien: `examples/julia/` (im Binärpaket `julia/`)

Julia ruft die C-Schnittstelle direkt mit `ccall` auf. Es gibt kein Paket zu installieren und nichts zu übersetzen: Die fertig gebaute Library und die Datei `IEC104.jl` genügen.

## 1. Schnellstart

Binärpaket `iec104-<version>-linux-x86_64.zip` entpacken, dann:

```bash
cd iec104-0.6.2-linux-x86_64
julia julia/server.jl 2404 &             # Station 1, Messwert IOA 100, steigt jede Sekunde
julia julia/client.jl 127.0.0.1 2404     # Generalabfrage, danach drei spontane Werte
```

Ausgabe des Clients:

```
type 70  cause 4  station 1  IOA 0  value 0.0       Ende der Initialisierung
type 100  cause 7  station 1  IOA 0  value 0.0      Generalabfrage bestätigt
type 13  cause 20  station 1  IOA 100  value 30.0   Wert aus der Abfrage
type 100  cause 10  station 1  IOA 0  value 0.0     Generalabfrage beendet
type 13  cause 3  station 1  IOA 100  value 31.0    spontan
```

| Datei | Inhalt |
| --- | --- |
| `IEC104.jl` | Modul `IEC104`: Typen wie im Header, Funktionen für Server und Client (u. a. `client_interrogation`, `client_read`, `client_send_command`, `client_clock_sync`) |
| `client.jl` | Leitstelle: verbinden, STARTDT, Generalabfrage, Werte ausgeben |
| `server.jl` | Station: ein Messwert, spontane Meldung jede Sekunde, Befehle annehmen |

`IEC104.jl` sucht die Library in dieser Reihenfolge:

| Ort | Wann |
| --- | --- |
| Umgebungsvariable `IEC104_LIB` | z. B. `IEC104_LIB=/opt/iec104/lib/libiec104.so.0 julia app.jl` |
| `../lib/libiec104.so.0` neben `IEC104.jl` | Aufbau des Binärpakets |
| Suchpfad des Systems | `LD_LIBRARY_PATH` oder installiert |

## 2. Eigenes Programm

```julia
include("julia/IEC104.jl")
using .IEC104

println(IEC104.version(), ", TLS: ", IEC104.tls_available())

config = LinkConfig()        # Normwerte (iec104_link_config_init)
config.t1_ms = 250_000       # Felder lassen sich ändern, z. B. lange Timer einer Gegenstelle
```

## 3. Typen

Die Julia-Typen in `IEC104.jl` haben dieselben Felder in derselben Reihenfolge wie die C-Strukturen; `IEC104.jl` prüft die Größen beim Laden.

| C | Julia | Hinweis |
| --- | --- | --- |
| `uint8_t`, `uint16_t`, `uint32_t`, `int64_t` | `UInt8`, `UInt16`, `UInt32`, `Int64` | |
| `int`, Enums (`iec104_status`, `iec104_link_state`, …) | `Cint` | Werte als Konstanten, z. B. `IEC104.LINK_STARTED` |
| `double` | `Float64` | |
| `const char*` | `Cstring` | Julia-Strings werden automatisch übergeben |
| `iec104_server*`, `iec104_client*` | `Ptr{Cvoid}` | Handle; mit `*_destroy` freigeben |
| `iec104_link_config` | `mutable struct LinkConfig` | änderbar, als `Ref{LinkConfig}` übergeben |
| `iec104_object`, `iec104_asdu_info`, `iec104_time` | `struct Object`, `AsduInfo`, `IecTime` | `Object(13, 100, 1.5)` = Typ, IOA, Wert |
| `iec104_callbacks` | `mutable struct Callbacks` | Funktionszeiger aus `@cfunction` |
| `char feld[N]` | `NTuple{N,UInt8}` | für Strukturen, die `IEC104.jl` nicht enthält |

Jede Funktion des Headers lässt sich nach demselben Muster aufrufen, auch wenn `IEC104.jl` sie nicht kennt:

```julia
# iec104_status iec104_client_clock_sync(iec104_client*, uint16_t, const iec104_time*);
t = Ref{IecTime}()
ccall((:iec104_time_from_unix_ms, IEC104.LIB), Cint, (Int64, Ref{IecTime}),
      round(Int64, time() * 1000), t)
IEC104.check(ccall((:iec104_client_clock_sync, IEC104.LIB), Cint,
                   (Ptr{Cvoid}, UInt16, Ref{IecTime}), client, 1, t))
```

`IEC104.check(status)` wirft bei einem Fehler `Iec104Error` mit dem Text aus `iec104_status_string`.

## 4. Callbacks

Die Library ruft Julia-Funktionen auf, während `client_poll` bzw. `server_poll` läuft. Drei Regeln:

| Regel | Grund |
| --- | --- |
| Callbacks nur Daten speichern lassen, ausgeben und verarbeiten nach dem Poll | `println`, `sleep`, Kanäle oder `@async` können in Julia einen Taskwechsel auslösen; innerhalb eines Aufrufs aus C führt das zum Absturz („GC error“) |
| Das Objekt hinter `user` mit `GC.@preserve` am Leben halten | C kennt nur den Zeiger, der GC sieht ihn nicht |
| Signatur in `@cfunction` exakt wie im Header | falsche Typen fallen nicht auf, sondern lesen falsche Daten |

Muster aus `client.jl`:

```julia
mutable struct ClientState
    state::Cint
    received::Vector{Tuple{AsduInfo,Object}}
end

function on_object(user::Ptr{Cvoid}, ::UInt32, info::Ptr{AsduInfo}, obj::Ptr{Object})::Cvoid
    s = unsafe_pointer_to_objref(user)::ClientState
    push!(s.received, (unsafe_load(info), unsafe_load(obj)))   # nur speichern
    return nothing
end

st = ClientState(0, [])
cb = Callbacks(st; on_object = @cfunction(on_object, Cvoid,
                   (Ptr{Cvoid}, UInt32, Ptr{AsduInfo}, Ptr{Object})))
GC.@preserve st begin
    client = client_create(LinkConfig(), cb)
    client_connect(client, "127.0.0.1", 2404)
    while true
        client_poll(client, 100)
        for (i, o) in st.received                # hier ausgeben, rechnen, speichern
            println("IOA $(o.ioa) = $(o.value)")
        end
        empty!(st.received)
        # ... STARTDT, Abfragen, Abbruchbedingung wie in client.jl
    end
end
```

| Callback | Signatur für `@cfunction` | Rückgabe |
| --- | --- | --- |
| `on_state` | `(Ptr{Cvoid}, UInt32, Cint, Cint)` | `Cvoid` |
| `on_object` | `(Ptr{Cvoid}, UInt32, Ptr{AsduInfo}, Ptr{Object})` | `Cvoid` |
| `on_command` (Server) | `(Ptr{Cvoid}, UInt32, Ptr{AsduInfo}, Ptr{Object})` | `Cint`: 1 annehmen, 0 ablehnen |
| `on_command_result` (Client) | `(Ptr{Cvoid}, UInt16, Ptr{Object}, Cint)` | `Cvoid` |
| `on_asdu` | `(Ptr{Cvoid}, UInt32, Ptr{UInt8}, Csize_t)` | `Cvoid` |
| `on_security` | `(Ptr{Cvoid}, UInt32, Cint, Cstring)` | `Cvoid` |

`on_command` muss sofort entscheiden. Die Entscheidung selbst (z. B. Wertebereich prüfen) ist erlaubt, nur keine Ausgabe und kein Warten.

## 5. Server

Kern von `server.jl`:

```julia
server = server_create(LinkConfig(), cb)
server_add_point(server, 1, Object(13, 100, 0.0))   # Station 1, M_ME_NC_1, Abfragegruppe 1
server_listen(server, 2404)
value = 0.0
while true
    server_poll(server, 100)                         # Abfragen beantwortet die Library selbst
    value += 1
    server_update(server, 1, Object(13, 100, value)) # Wert ändern und spontan senden
end
```

## 6. Regeln

| Regel | Grund |
| --- | --- |
| Ein Handle nur in einem Task benutzen; Poll, Senden und Auswerten in derselben Schleife | die Library hat keine Sperren (docs/05, E8) |
| `try … finally *_destroy(handle) end` | schließt Verbindungen auch nach Strg-C oder Fehler |
| `IEC104.jl` und Library aus derselben Version verwenden | Strukturen können sich zwischen Versionen ändern; `IEC104.jl` prüft die Größen beim Laden |
| Typen, Ursachen und Qualitätsbits: Tabellen in `include/iec104/iec104.h` | Kommentar zu `iec104_object` |

## 7. Test-GUI im Browser

`gui/iec104_gui.jl` ist ein kleines Testprogramm mit Oberfläche im Browser: links eine Leitstelle (Client), rechts eine Station (Server). Es braucht nur Julia, keine Pakete.

```bash
julia julia/gui/iec104_gui.jl            # im Binärpaket; im Quellbaum examples/julia/gui/
```

Dann im Browser `http://localhost:8080` öffnen. Anderer Port: `julia julia/gui/iec104_gui.jl 8081`. Kommt unter WSL der Windows-Browser nicht an `localhost` heran: `bash gui.sh 8080 0.0.0.0` (Binärpaket) und die angezeigte Adresse der WSL öffnen.

| Bereich | Funktionen |
| --- | --- |
| Leitstelle | Verbinden (Adresse, Port, Station, Timer, k, w, tolerant, TLS mit CA, Zertifikat, Schlüssel, Name), STARTDT, STOPDT, Trennen; Generalabfrage (Station, Gruppe 1–16), Zählerabfrage, Uhrzeit; Befehle 45–50 und 58–63 direkt oder mit Select/Execute |
| Tabelle | jeder empfangene Punkt mit Typ, Wert, Qualität, Ursache (negativ rot), Zeitmarke, Anzahl, Alter; frische Werte hervorgehoben |
| Station | Starten auf Port/Station, Datenpunkte anlegen (Typen 1, 3, 5, 9, 11, 13, 15), Wert ändern und spontan senden (+1 für schnelle Tests), Befehle annehmen oder ablehnen, Verbindungen |
| Ereignisse | Zustände, Befehle und ihr Ergebnis, TLS-Ereignisse, Fehler und Warnungen der Library |

Beide Seiten lassen sich gegeneinander testen: Station auf Port 2404 starten, Punkt anlegen, Leitstelle auf `127.0.0.1:2404` verbinden, STARTDT, Generalabfrage.

| Hinweis | Grund |
| --- | --- |
| Die GUI lauscht nur auf `127.0.0.1` | sie hat keine Anmeldung und kann Befehle senden; eine andere Adresse nur bewusst als zweiter Parameter |
| Die GUI ist ein Testwerkzeug | eine Verbindung je Seite, kein Speichern der Einstellungen |
| Eigene Erweiterungen | `iec104_gui.jl` (Julia, Aktionen in `ACTIONS`) und `index.html` (Seite) |
