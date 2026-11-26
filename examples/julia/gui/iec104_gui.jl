# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# iec104_gui.jl - test GUI in the browser: controlling station (client) and
# controlled station (server) of the IEC 104 library, side by side, plus an
# example program of lib60870 as independent controlling station.
#
#   julia iec104_gui.jl [port] [address] [lib60870]
#     port      default 8080, 0 = any free port (printed in the start line)
#     address   default 127.0.0.1; the GUI and its station listen there
#     lib60870  folder lib60870-C (or its parent lib60870) with built
#               examples; else environment variable IEC104_LIB60870
#   then open http://localhost:8080
#
# Only the Julia standard library is used (Sockets); the page is index.html
# next to this file. The GUI can send commands and start programs: it listens
# on 127.0.0.1 only, unless another address is given explicitly. Requests
# must name this GUI in the Host header (localhost or an IP address, against
# DNS rebinding) and /api/ requests must carry "X-IEC104-GUI: 1", which a
# page of another site cannot send (no CORS, only GET).
#
# Design: one Julia thread. A poll task drives client and server every 20 ms;
# the library calls the callbacks inside that poll and they only store data.
# HTTP requests, the output of lib60870 and its end are handled in their own
# tasks between two polls (docs/07_Julia.md, section 4).

include(joinpath(@__DIR__, "..", "IEC104.jl"))
using .IEC104
using Sockets
using Printf
include(joinpath(@__DIR__, "frames.jl"))   # description of frames for the commented protocol
using .Frames

# --- State ---------------------------------------------------------------------

"One received data point of the client."
mutable struct Row
    station::UInt16
    ioa::UInt32
    type::UInt8
    cause::UInt8
    negative::Bool
    value::Float64
    bits::UInt32
    quality::UInt8
    time::IecTime
    count::Int
    updated::Float64
end

mutable struct Gui
    # Controlling station (client)
    client::Ptr{Cvoid}
    client_state::Cint
    client_reason::Cint
    station::UInt16
    rows::Dict{Tuple{UInt16,UInt32,UInt8},Row}
    # Controlled station (server)
    server::Ptr{Cvoid}
    server_station::UInt16
    accept_commands::Bool
    points::Dict{UInt32,Object}
    connections::Dict{UInt32,Cint}
    # Event list shown on the page: number, time, kind ("event", "frame", "error"), text, meaning
    log::Vector{Tuple{Int,Float64,String,String,String}}
    log_seq::Int
end

const GUI = Gui(C_NULL, IEC104.LINK_CLOSED, 0, 1, Dict(), C_NULL, 1, true, Dict(), Dict(), [], 0)
const LOG_MAX = 3000   # frames included
const ROWS_MAX = 1000  # rows of the client table sent to the page
const BOOT = string(round(Int, time() * 1000))   # identifies this run of the GUI for the page

# Address and port of the GUI, set by main(). The station listens on the same address.
const GUI_ADDRESS = Ref("127.0.0.1")
const GUI_PORT = Ref(0)

"Counterpart lib60870: an unchanged example program as separate process (never linked)."
mutable struct Peer
    folder::String                          # lib60870-C, "" = not configured
    process::Union{Base.Process,Nothing}    # running program
    program::String                         # key of the last started program
end

const PEER = Peer("", nothing, "")

# Programs the page may start: only a key of this table comes from the browser,
# never a path or an argument. All are controlling stations (clients); the
# lib60870 stations are not offered because they listen on all addresses.
const PEER_PROGRAMS = [
    (key = "simple_client", dir = "cs104_client", prog = "simple_client",
     label = "verbindet, STARTDT, nach 2 s Generalabfrage an Station 1, nach 5 s Testbefehl " *
             "mit Zeitmarke (Typ 107), endet nach etwa 9 s"),
    (key = "cs104_client_async", dir = "cs104_client_async", prog = "cs104_client_async",
     label = "bleibt verbunden: STARTDT, Generalabfrage an Station 1 sofort und alle 10 s; " *
             "läuft bis Beenden"),
    (key = "cs104_client_no_threads", dir = "cs104_client_no_threads",
     prog = "cs104_client_no_threads",
     label = "bleibt verbunden: nur STARTDT (sendet trotz seiner Meldung keine Abfrage), " *
             "empfängt spontane Daten, Testrahmen nach 2 s Ruhe; läuft bis Beenden"),
]
const PEER_LINE_MAX = 300   # longer output lines are cut

note(text::AbstractString; kind = "event", explain = "") = begin
    GUI.log_seq += 1
    push!(GUI.log, (GUI.log_seq, time(), String(kind), String(text), String(explain)))
    length(GUI.log) > LOG_MAX && popfirst!(GUI.log)
    nothing
end

# --- Plain German texts for the page --------------------------------------------

"State of a connection in words (iec104_link_state)."
state_text(state::Integer) = get(Dict(0 => "verbinde …",
    1 => "verbunden, Datenübertragung aus (STARTDT fehlt)",
    2 => "STARTDT gesendet, warte auf Bestätigung", 3 => "Datenübertragung läuft",
    4 => "STOPDT gesendet, warte auf Bestätigung", 5 => "getrennt"), Int(state), "Zustand $state")

"Hint for a close reason (iec104_close_reason), \"\" if none."
close_hint(reason::Integer) = get(Dict(
    1 => "von dieser Seite getrennt",
    2 => "Gegenstelle hat die Verbindung beendet",
    3 => "an Adresse und Port lauscht niemand – Station gestartet? Adresse und Port richtig?",
    4 => "keine Antwort innerhalb t0 – Adresse im Netz erreichbar?",
    5 => "Gegenstelle hat nicht rechtzeitig quittiert (t1) – Timer der Gegenstelle prüfen",
    9 => "Rahmen im falschen Zustand oder in falscher Rolle",
    11 => "TLS-Fehler, siehe TLS-Ereignis"), Int(reason), "")

# Allowed values per type (time-tagged types like their base type).
const RANGES = Dict(1 => "0 oder 1", 3 => "0…3 (1 = aus, 2 = ein)", 5 => "−64…63",
    9 => "−1 bis knapp unter 1, z. B. 0.5", 11 => "−32768…32767", 13 => "beliebig",
    15 => "ganze Zahl", 45 => "0 oder 1", 46 => "0…3 (1 = aus, 2 = ein)",
    47 => "0…3 (1 = tiefer, 2 = höher)", 48 => "−1 bis knapp unter 1, z. B. 0.5",
    49 => "−32768…32767", 50 => "beliebig")
const BASE_TYPE = Dict(30 => 1, 31 => 3, 32 => 5, 34 => 9, 35 => 11, 36 => 13, 37 => 15,
                       58 => 45, 59 => 46, 60 => 47, 61 => 48, 62 => 49, 63 => 50)
range_text(type::Integer) = get(RANGES, get(BASE_TYPE, Int(type), Int(type)), "")

"Value fits the monitor type (same limits as the library, src/api/values.cpp)."
function value_ok(type::Integer, v::Real)
    isfinite(v) || return false
    t = get(BASE_TYPE, Int(type), Int(type))
    whole(lo, hi) = isinteger(v) && lo <= v <= hi
    t == 1 && return whole(0, 1)
    t == 3 && return whole(0, 3)
    t == 5 && return whole(-64, 63)
    t == 9 && return -32768 <= round(v * 32768) <= 32767
    t == 11 && return whole(-32768, 32767)
    t == 15 && return whole(typemin(Int32), typemax(Int32))
    return true
end

# --- Callbacks (run inside the poll, called from C: store only, no I/O) -------

function cb_client_state(::Ptr{Cvoid}, ::UInt32, state::Cint, reason::Cint)::Cvoid
    GUI.client_state = state
    GUI.client_reason = reason
    hint = state == IEC104.LINK_CLOSED ? close_hint(reason) : ""
    note("Leitstelle: " * state_text(state) *
         (state == IEC104.LINK_CLOSED ? " (" * IEC104.close_reason_string(reason) * ")" : "") *
         (isempty(hint) ? "" : " – " * hint))
    return nothing
end

function cb_client_object(::Ptr{Cvoid}, ::UInt32, info::Ptr{AsduInfo}, obj::Ptr{Object})::Cvoid
    i = unsafe_load(info)
    o = unsafe_load(obj)
    key = (i.common_address, o.ioa, i.type)
    row = get(GUI.rows, key, nothing)
    if row === nothing
        GUI.rows[key] = Row(i.common_address, o.ioa, i.type, i.cause, i.negative != 0, o.value,
                            o.bits, o.quality, o.time, 1, time())
    else
        row.cause = i.cause
        row.negative = i.negative != 0
        row.value = o.value
        row.bits = o.bits
        row.quality = o.quality
        row.time = o.time
        row.count += 1
        row.updated = time()
    end
    return nothing
end

function cb_command_result(::Ptr{Cvoid}, station::UInt16, cmd::Ptr{Object}, result::Cint)::Cvoid
    c = unsafe_load(cmd)
    note("Befehl Typ $(c.type) an $station/$(c.ioa), Wert $(c.value): " *
         IEC104.command_result_string(result))
    return nothing
end

function cb_server_state(::Ptr{Cvoid}, conn::UInt32, state::Cint, reason::Cint)::Cvoid
    if state == IEC104.LINK_CLOSED
        delete!(GUI.connections, conn)
        hint = close_hint(reason)
        note("Station: Verbindung $conn getrennt (" * IEC104.close_reason_string(reason) * ")" *
             (isempty(hint) ? "" : " – " * hint))
    else
        GUI.connections[conn] = state
        note("Station: Verbindung $conn " * state_text(state))
    end
    return nothing
end

function cb_server_command(::Ptr{Cvoid}, conn::UInt32, info::Ptr{AsduInfo}, cmd::Ptr{Object})::Cint
    i = unsafe_load(info)
    c = unsafe_load(cmd)
    what = i.type == 103 ? "Uhrzeit" : "Befehl Typ $(i.type) IOA $(c.ioa) Wert $(c.value)" *
                                       (c.select != 0 ? " (Auswahl)" : "")
    note("Station: $what von Verbindung $conn: " * (GUI.accept_commands ? "angenommen" : "abgelehnt"))
    return Cint(GUI.accept_commands ? 1 : 0)
end

# Requests the station answers by itself (no on_command), shown as events.
const REQUESTS = Dict(100 => "Generalabfrage", 101 => "Zählerabfrage", 102 => "Lesebefehl",
                      104 => "Testbefehl", 107 => "Testbefehl mit Zeitmarke")

function cb_server_asdu(::Ptr{Cvoid}, conn::UInt32, asdu::Ptr{UInt8}, len::Csize_t)::Cvoid
    len >= 3 || return nothing
    type = unsafe_load(asdu, 1)
    what = get(REQUESTS, Int(type), nothing)
    what === nothing && return nothing
    note("Station: $what (Typ $type, Ursache $(unsafe_load(asdu, 3) & 0x3f)) von Verbindung $conn")
    return nothing
end

function cb_security(::Ptr{Cvoid}, conn::UInt32, event::Cint, detail::Cstring)::Cvoid
    note("TLS $conn: " * IEC104.security_event_string(event) * ": " * unsafe_string(detail))
    return nothing
end

"Library log: errors and warnings as events, every frame (level trace) with its meaning."
function log_entry(side::String, level::Cint, conn::UInt32, message::Cstring)
    try
        m = unsafe_string(message)
        if level <= 1
            note("Library $conn: " * m; kind = level == 0 ? "error" : "warning")
        elseif level == 4
            sent = startswith(m, "sent ")
            (sent || startswith(m, "received ")) || return nothing
            summary, meaning = Frames.describe(Frames.parse_hex(m[(sent ? 6 : 10):end]))
            who = side == "L" ? "Leitstelle" : "Station($conn)"
            note(who * (sent ? " → " : " ← ") * summary; kind = "frame", explain = meaning)
        end
    catch
    end
    return nothing
end
cb_log_client(::Ptr{Cvoid}, level::Cint, conn::UInt32, message::Cstring)::Cvoid =
    log_entry("L", level, conn, message)
cb_log_server(::Ptr{Cvoid}, level::Cint, conn::UInt32, message::Cstring)::Cvoid =
    log_entry("S", level, conn, message)

# --- Actions ---------------------------------------------------------------------

p_int(p, k, d) = haskey(p, k) && !isempty(p[k]) ? parse(Int, p[k]) : d
p_num(p, k, d) = haskey(p, k) && !isempty(p[k]) ? parse(Float64, replace(p[k], ',' => '.')) : d
p_str(p, k) = get(p, k, "")

function gui_client_connect(p)
    if GUI.client != C_NULL
        # destroy reports nothing any more: note the end of the old connection here
        old = GUI.client
        GUI.client = C_NULL
        client_destroy(old)
        GUI.client_state != IEC104.LINK_CLOSED && note("Leitstelle: alte Verbindung getrennt")
        GUI.client_state = IEC104.LINK_CLOSED
    end
    cfg = LinkConfig()
    for (k, f) in (("t0", :t0_ms), ("t1", :t1_ms), ("t2", :t2_ms), ("t3", :t3_ms))
        haskey(p, k) && !isempty(p[k]) && setfield!(cfg, f, UInt32(round(p_num(p, k, 0) * 1000)))
    end
    haskey(p, "k") && !isempty(p["k"]) && (cfg.k = UInt16(p_int(p, "k", 12)))
    haskey(p, "w") && !isempty(p["w"]) && (cfg.w = UInt16(p_int(p, "w", 8)))
    cfg.tolerant = get(p, "tolerant", "") == "1" ? 1 : 0
    cb = Callbacks(GUI;
        on_state = @cfunction(cb_client_state, Cvoid, (Ptr{Cvoid}, UInt32, Cint, Cint)),
        on_object = @cfunction(cb_client_object, Cvoid, (Ptr{Cvoid}, UInt32, Ptr{AsduInfo}, Ptr{Object})),
        on_command_result = @cfunction(cb_command_result, Cvoid, (Ptr{Cvoid}, UInt16, Ptr{Object}, Cint)),
        on_security = @cfunction(cb_security, Cvoid, (Ptr{Cvoid}, UInt32, Cint, Cstring)))
    GUI.client = client_create(cfg, cb)
    client_set_log(GUI.client, 4, @cfunction(cb_log_client, Cvoid, (Ptr{Cvoid}, Cint, UInt32, Cstring)), C_NULL)
    if get(p, "tls", "") == "1"
        t = TlsConfig()
        for (k, f) in (("ca", :ca_file), ("cert", :certificate_file), ("key", :private_key_file),
                       ("peer", :peer_name))
            isempty(p_str(p, k)) || settext!(t, f, p_str(p, k))
        end
        client_set_tls(GUI.client, t)
    end
    GUI.station = UInt16(p_int(p, "station", 1))
    host = isempty(p_str(p, "host")) ? "127.0.0.1" : p_str(p, "host")
    port = p_int(p, "port", get(p, "tls", "") == "1" ? 19998 : 2404)
    IEC104.client_connect(GUI.client, host, port)
    GUI.client_state = IEC104.LINK_CONNECTING
    note("Leitstelle: verbinde mit $host:$port" * (get(p, "tls", "") == "1" ? " (TLS)" : ""))
end

need_client() = GUI.client == C_NULL && throw(ArgumentError("keine Verbindung"))

function gui_client_command(p)
    need_client()
    type = p_int(p, "type", 45)
    o = Object(type, p_int(p, "ioa", 0), p_num(p, "value", 0.0))
    if type >= 58   # with time tag: now
        o = Object(o.type, o.ioa, o.value, 0, 0, 0, 0, 0, time_now())
    end
    if get(p, "mode", "direct") == "sbo"
        client_select_execute(GUI.client, GUI.station, o)
        note("Leitstelle: Auswahl und Ausführung Typ $type IOA $(o.ioa) Wert $(o.value)")
    else
        client_send_command(GUI.client, GUI.station, o)
        note("Leitstelle: Befehl Typ $type IOA $(o.ioa) Wert $(o.value)")
    end
end

function gui_server_listen(p)
    GUI.server != C_NULL && (server_destroy(GUI.server); GUI.server = C_NULL)
    empty!(GUI.connections)
    cb = Callbacks(GUI;
        on_asdu = @cfunction(cb_server_asdu, Cvoid, (Ptr{Cvoid}, UInt32, Ptr{UInt8}, Csize_t)),
        on_state = @cfunction(cb_server_state, Cvoid, (Ptr{Cvoid}, UInt32, Cint, Cint)),
        on_command = @cfunction(cb_server_command, Cint, (Ptr{Cvoid}, UInt32, Ptr{AsduInfo}, Ptr{Object})),
        on_security = @cfunction(cb_security, Cvoid, (Ptr{Cvoid}, UInt32, Cint, Cstring)))
    GUI.server = server_create(LinkConfig(), cb)
    server_set_log(GUI.server, 4, @cfunction(cb_log_server, Cvoid, (Ptr{Cvoid}, Cint, UInt32, Cstring)), C_NULL)
    GUI.server_station = UInt16(p_int(p, "station", 1))
    try
        for o in values(GUI.points)
            try
                server_add_point(GUI.server, GUI.server_station, o)
            catch e
                e isa Iec104Error || rethrow()
                throw(ArgumentError("Datenpunkt IOA $(o.ioa) Typ $(o.type) Wert $(o.value): $(e.msg)" *
                                    " – erlaubt: $(range_text(o.type))"))
            end
        end
        # Same address as the GUI: 127.0.0.1 unless the GUI was started on another one.
        IEC104.server_listen(GUI.server, p_int(p, "port", 2404); address = GUI_ADDRESS[])
    catch
        server_destroy(GUI.server)   # not listening: no half-started station
        GUI.server = C_NULL
        rethrow()
    end
    note("Station: lauscht auf $(GUI_ADDRESS[]):$(server_port(GUI.server)), " *
         "gemeinsame Adresse $(GUI.server_station)")
end

function gui_server_point(p)
    o = Object(p_int(p, "type", 13), p_int(p, "ioa", 100), p_num(p, "value", 0.0))
    old = get(GUI.points, o.ioa, nothing)
    old !== nothing && old.type != o.type &&
        throw(ArgumentError("IOA $(o.ioa) ist schon als Typ $(old.type) angelegt; " *
                            "für Typ $(o.type) eine andere IOA wählen"))
    value_ok(o.type, o.value) ||
        throw(ArgumentError("Wert $(o.value) passt nicht zu Typ $(o.type) – erlaubt: $(range_text(o.type))"))
    if GUI.server != C_NULL
        if old !== nothing
            check(server_update(GUI.server, GUI.server_station, o))   # spontaneous
            any(==(IEC104.LINK_STARTED), values(GUI.connections)) ||
                note("Station: IOA $(o.ioa) = $(o.value) gespeichert; keine Leitstelle mit STARTDT, " *
                     "nichts gesendet (kommt mit der nächsten Generalabfrage)")
        else
            server_add_point(GUI.server, GUI.server_station, o)
            note("Station: Datenpunkt IOA $(o.ioa) Typ $(o.type) angelegt")
        end
    end
    GUI.points[o.ioa] = o   # only after the library accepted it
end

# --- Counterpart lib60870 (process, normal tasks; never inside a callback) -----

"lib60870-C folder from the start argument, else IEC104_LIB60870; the parent folder lib60870 is accepted."
function peer_folder(arg::AbstractString)
    folder = isempty(arg) ? get(ENV, "IEC104_LIB60870", "") : arg
    isempty(folder) && return ""
    folder = abspath(expanduser(folder))
    isdir(joinpath(folder, "lib60870-C")) && (folder = joinpath(folder, "lib60870-C"))
    return String(rstrip(normpath(folder), '/'))
end

peer_path(x) = joinpath(PEER.folder, "build", "examples", x.dir, x.prog)
peer_found(x) = !isempty(PEER.folder) && isfile(peer_path(x)) && Sys.isexecutable(peer_path(x))
peer_running() = PEER.process !== nothing && process_running(PEER.process)

"Every output line of the program becomes an event."
function peer_read(out::Pipe)
    try
        for line in eachline(out)
            line = rstrip(line)
            isvalid(line) || (line = map(c -> isvalid(c) ? c : '?', line))
            isempty(line) && continue
            length(line) > PEER_LINE_MAX && (line = first(line, PEER_LINE_MAX) * " …")
            note("lib60870: " * line)
        end
    catch e
        e isa Base.IOError || note("lib60870: Ausgabe nicht lesbar: " * sprint(showerror, e))
    finally
        close(out)
    end
end

"Waits for the end of the program and notes how it ended."
function peer_wait(proc::Base.Process, key::String, reader::Task)
    wait(proc)
    PEER.process === proc && (PEER.process = nothing)
    timedwait(() -> istaskdone(reader), 2.0)   # its last lines first
    note("lib60870: $key beendet, " *
         (proc.termsignal != 0 ? "Signal $(proc.termsignal)" : "Rückgabe $(proc.exitcode)"))
end

function gui_peer_start(p)
    isempty(PEER.folder) && throw(ArgumentError("kein lib60870-Ordner (beim Start der GUI angeben)"))
    i = findfirst(x -> x.key == p_str(p, "prog"), PEER_PROGRAMS)
    i === nothing && throw(ArgumentError("unbekanntes Programm"))
    x = PEER_PROGRAMS[i]
    peer_found(x) || throw(ArgumentError("$(x.key) fehlt: $(peer_path(x))"))
    peer_running() && throw(ArgumentError("Gegenstelle läuft bereits ($(PEER.program))"))
    port = GUI.server == C_NULL ? 0 : server_port(GUI.server)
    port == 0 && throw(ArgumentError("erst die Station starten"))
    a = GUI_ADDRESS[]
    a == "0.0.0.0" || a == "::" || startswith(a, "127.") ||
        throw(ArgumentError("Gegenstelle verbindet sich mit 127.0.0.1, die Station lauscht auf $a"))
    peer_launch(x, port)
    return nothing
end

"Starts program x of PEER_PROGRAMS against 127.0.0.1:port; output and end become events."
function peer_launch(x, port::Integer)
    cmd = `$(peer_path(x)) 127.0.0.1 $port`
    stdbuf = Sys.which("stdbuf")   # C stdio buffers blocks on a pipe; line by line instead
    stdbuf === nothing || (cmd = `$stdbuf -oL -eL $cmd`)
    out = Pipe()
    proc = run(pipeline(Cmd(cmd; dir = mktempdir()); stdin = devnull, stdout = out, stderr = out);
               wait = false)
    close(out.in)
    PEER.process = proc
    PEER.program = x.key
    note("lib60870: $(x.key) verbindet sich mit 127.0.0.1:$port (Prozess $(getpid(proc)))")
    reader = @async peer_read(out)
    @async peer_wait(proc, x.key, reader)
    return proc
end

"SIGINT; still running after 3 s: SIGKILL."
function gui_peer_stop(p)
    peer_running() || throw(ArgumentError("keine Gegenstelle aktiv"))
    proc = PEER.process
    note("lib60870: $(PEER.program) wird beendet")
    kill(proc, Base.SIGINT)
    @async begin
        timedwait(() -> !process_running(proc), 3.0)
        process_running(proc) && kill(proc, Base.SIGKILL)
    end
    return nothing
end

"At exit of the GUI: a running counterpart ends too."
peer_kill() = (p = PEER.process; p !== nothing && process_running(p) && kill(p, Base.SIGKILL); nothing)

include(joinpath(@__DIR__, "selftest.jl"))   # self-test with its own station and controlling station

const ACTIONS = Dict{String,Function}(
    "/api/client/connect" => gui_client_connect,
    "/api/client/start" => p -> (need_client(); client_start(GUI.client)),
    "/api/client/stop" => p -> (need_client(); client_stop(GUI.client)),
    "/api/client/close" => p -> (need_client(); client_close(GUI.client)),
    "/api/client/gi" => p -> (need_client(); client_interrogation(GUI.client, GUI.station, p_int(p, "qoi", 20))),
    "/api/client/ci" => p -> (need_client(); client_counter_interrogation(GUI.client, GUI.station)),
    "/api/client/clock" => p -> (need_client(); client_clock_sync(GUI.client, GUI.station)),
    "/api/client/command" => gui_client_command,
    "/api/client/clear" => p -> empty!(GUI.rows),
    "/api/server/listen" => gui_server_listen,
    "/api/server/stop" => p -> (GUI.server != C_NULL && server_destroy(GUI.server); GUI.server = C_NULL;
                                empty!(GUI.connections); note("Station: beendet")),
    "/api/server/point" => gui_server_point,
    "/api/server/accept" => p -> (GUI.accept_commands = get(p, "on", "1") == "1"),
    "/api/peer/start" => gui_peer_start,
    "/api/peer/stop" => gui_peer_stop,
    "/api/selftest/start" => selftest_start,
)

# --- JSON (hand-made, the page needs little) -------------------------------------

function jstr(io, s::AbstractString)
    print(io, '"')
    for c in s
        c == '"' ? print(io, "\\\"") : c == '\\' ? print(io, "\\\\") :
        c < ' ' ? @printf(io, "\\u%04x", UInt32(c)) : print(io, c)
    end
    print(io, '"')
end
jnum(io, x) = isfinite(x) ? print(io, x) : print(io, "null")

function quality_text(q::UInt8)
    names = ((0x80, "IV"), (0x40, "NT"), (0x20, "SB"), (0x10, "BL"), (0x01, "OV"))
    join([n for (b, n) in names if q & b != 0], " ")
end
time_text(t::IecTime) = t.year == 0 ? "" :
    @sprintf("%04d-%02d-%02d %02d:%02d:%02d.%03d%s%s", t.year, t.month, t.day, t.hour, t.minute,
             t.second, t.millisecond, t.summer_time != 0 ? " SU" : "", t.invalid != 0 ? " IV" : "")

function state_json(since::Int)
    io = IOBuffer()
    print(io, "{\"boot\":\"", BOOT, "\",\"client\":{\"state\":")
    jstr(io, GUI.client == C_NULL ? "–" : IEC104.state_string(GUI.client_state))
    print(io, ",\"text\":")
    jstr(io, GUI.client == C_NULL ? "nicht verbunden" : state_text(GUI.client_state))
    print(io, ",\"station\":", GUI.station, ",\"rows\":[")
    rows = sort!(collect(values(GUI.rows)); by = r -> (r.station, r.ioa, r.type))
    total = length(rows)
    length(rows) > ROWS_MAX && (rows = rows[1:ROWS_MAX])   # the page stays fast
    for (n, r) in enumerate(rows)
        n > 1 && print(io, ',')
        print(io, "{\"station\":", r.station, ",\"ioa\":", r.ioa, ",\"type\":", r.type,
              ",\"cause\":", r.cause, ",\"negative\":", r.negative, ",\"value\":")
        jnum(io, r.value)
        print(io, ",\"bits\":", r.bits, ",\"quality\":")
        jstr(io, quality_text(r.quality))
        print(io, ",\"time\":")
        jstr(io, time_text(r.time))
        print(io, ",\"count\":", r.count, ",\"age\":")
        jnum(io, round(time() - r.updated; digits = 1))
        print(io, '}')
    end
    print(io, "],\"rows_total\":", total, "},\"server\":{\"running\":", GUI.server != C_NULL, ",\"port\":",
          GUI.server == C_NULL ? 0 : server_port(GUI.server), ",\"station\":", GUI.server_station,
          ",\"accept\":", GUI.accept_commands, ",\"connections\":[")
    for (n, (id, st)) in enumerate(sort!(collect(GUI.connections)))
        n > 1 && print(io, ',')
        print(io, "{\"id\":", id, ",\"state\":")
        jstr(io, state_text(st))
        print(io, '}')
    end
    print(io, "],\"points\":[")
    for (n, o) in enumerate(sort!(collect(values(GUI.points)); by = o -> o.ioa))
        n > 1 && print(io, ',')
        print(io, "{\"ioa\":", o.ioa, ",\"type\":", o.type, ",\"value\":")
        jnum(io, o.value)
        print(io, '}')
    end
    print(io, "]},\"peer\":{\"available\":", any(peer_found, PEER_PROGRAMS), ",\"folder\":")
    jstr(io, PEER.folder)
    print(io, ",\"running\":", peer_running(), ",\"program\":")
    jstr(io, PEER.program)
    print(io, ",\"programs\":[")
    for (n, x) in enumerate(PEER_PROGRAMS)
        n > 1 && print(io, ',')
        print(io, "{\"key\":")
        jstr(io, x.key)
        print(io, ",\"label\":")
        jstr(io, x.label)
        print(io, ",\"available\":", peer_found(x), '}')
    end
    print(io, "]},")
    selftest_json(io)
    print(io, ",\"log\":[")
    first = true
    for (seq, t, kind, text, explain) in GUI.log
        seq <= since && continue
        first || print(io, ',')
        first = false
        print(io, "{\"seq\":", seq, ",\"time\":")
        jstr(io, Libc.strftime("%H:%M:%S", t))
        print(io, ",\"kind\":")
        jstr(io, kind)
        print(io, ",\"text\":")
        jstr(io, text)
        print(io, ",\"explain\":")
        jstr(io, explain)
        print(io, '}')
    end
    print(io, "]}")
    return String(take!(io))
end

# --- HTTP (only what the page needs: GET, query string) -------------------------

function urldecode(s::AbstractString)
    out = IOBuffer()
    i = 1
    b = codeunits(s)
    while i <= length(b)
        if b[i] == UInt8('+')
            write(out, ' ')
        elseif b[i] == UInt8('%') && i + 2 <= length(b)
            write(out, parse(UInt8, String(b[i+1:i+2]); base = 16))
            i += 2
        else
            write(out, b[i])
        end
        i += 1
    end
    return String(take!(out))
end

function parse_query(q::AbstractString)
    p = Dict{String,String}()
    for part in split(q, '&'; keepempty = false)
        kv = split(part, '='; limit = 2)
        p[urldecode(kv[1])] = length(kv) == 2 ? urldecode(kv[2]) : ""
    end
    return p
end

const PAGE = read(joinpath(@__DIR__, "index.html"), String)

const ACTION_NAMES = Dict(
    "/api/client/connect" => "Verbinden", "/api/client/start" => "STARTDT",
    "/api/client/stop" => "STOPDT", "/api/client/close" => "Trennen",
    "/api/client/gi" => "Generalabfrage", "/api/client/ci" => "Zählerabfrage",
    "/api/client/clock" => "Uhrzeit setzen", "/api/client/command" => "Befehl",
    "/api/server/listen" => "Station starten", "/api/server/point" => "Datenpunkt",
    "/api/peer/start" => "Gegenstelle starten", "/api/peer/stop" => "Gegenstelle beenden",
    "/api/selftest/start" => "Selbsttest")

"Error message with a hint what to do."
function error_text(e, path, p)
    e isa Iec104Error || return e isa ArgumentError ? e.msg : sprint(showerror, e)
    if e.status == -8   # IEC104_ERR_WRONG_STATE
        startswith(path, "/api/client") || return e.msg
        (GUI.client == C_NULL || GUI.client_state == IEC104.LINK_CLOSED) &&
            return "$(e.msg) – erst verbinden"
        GUI.client_state == IEC104.LINK_STARTED || return "$(e.msg) – erst STARTDT"
        return e.msg
    elseif e.status == -5   # IEC104_ERR_OUT_OF_RANGE
        r = range_text(p_int(p, "type", 0))
        return isempty(r) ? e.msg : "$(e.msg) – Typ $(p_str(p, "type")) erlaubt: $r"
    elseif e.status == -9 && path == "/api/server/listen"   # IEC104_ERR_IO
        return "$(e.msg) – Port $(p_str(p, "port")) belegt? anderen Port wählen"
    end
    return e.msg
end

function route(path, p)
    path == "/" && return ("200 OK", "text/html; charset=utf-8", PAGE)
    path == "/api/state" && return ("200 OK", "application/json", state_json(p_int(p, "since", 0)))
    path == "/api/selftest/report" &&
        return ("200 OK", "text/plain; charset=utf-8", selftest_report(p_str(p, "full") == "1"))
    action = get(ACTIONS, path, nothing)
    action === nothing && return ("404 Not Found", "text/plain", "not found")
    try
        action(p)
        return ("200 OK", "application/json", "{\"ok\":true}")
    catch e
        msg = error_text(e, path, p)
        note("Fehler bei " * get(ACTION_NAMES, path, path) * ": " * msg)
        io = IOBuffer()
        print(io, "{\"ok\":false,\"error\":")
        jstr(io, msg)
        print(io, '}')
        return ("200 OK", "application/json", String(take!(io)))
    end
end

"Host header names this GUI: localhost or an IP address, with the GUI's port (no DNS name: DNS rebinding)."
function host_allowed(host::AbstractString)
    m = match(r"^(localhost|[0-9]{1,3}(?:\.[0-9]{1,3}){3}|\[[0-9A-Fa-f:.]+\]):([0-9]{1,5})$"i, host)
    m === nothing && return false
    name, port = m.captures
    if startswith(name, '[')
        try
            parse(IPv6, name[2:end-1])
        catch
            return false
        end
    elseif lowercase(name) != "localhost"
        all(o -> parse(Int, o) <= 255, split(name, '.')) || return false
    end
    return parse(Int, port) == GUI_PORT[]
end

const HEADERS_MAX = 100

function handle(sock)
    try
        request = readline(sock)
        headers = Dict{String,String}()
        for _ in 1:HEADERS_MAX
            line = readline(sock)
            isempty(strip(line)) && break
            kv = split(line, ':'; limit = 2)
            length(kv) == 2 && (headers[lowercase(strip(kv[1]))] = strip(kv[2]))
        end
        parts = split(request)
        length(parts) >= 2 || return
        method, target = parts[1], parts[2]
        path, query = occursin('?', target) ? split(target, '?'; limit = 2) : (target, "")
        # No Access-Control-* headers ever: other sites get no CORS permission.
        status, type, body =
            if !host_allowed(get(headers, "host", ""))
                ("403 Forbidden", "text/plain; charset=utf-8", "Zugriff verweigert: Host nicht erlaubt\n")
            elseif method != "GET"
                ("405 Method Not Allowed", "text/plain; charset=utf-8", "nur GET\n")
            elseif startswith(path, "/api/") && get(headers, "x-iec104-gui", "") != "1"
                ("403 Forbidden", "text/plain; charset=utf-8",
                 "Zugriff verweigert: Kopfzeile X-IEC104-GUI fehlt\n")
            else
                route(String(path), parse_query(query))
            end
        allow = startswith(status, "405") ? "Allow: GET\r\n" : ""
        write(sock, "HTTP/1.1 $status\r\nContent-Type: $type\r\nContent-Length: $(sizeof(body))\r\n" *
                    "Cache-Control: no-store\r\nX-Frame-Options: DENY\r\n$(allow)Connection: close\r\n\r\n",
              body)
    catch e
        e isa Base.IOError || @warn "request failed" exception = e
    finally
        close(sock)
    end
end

# --- Main ----------------------------------------------------------------------------

function main(port = 8080, address = "127.0.0.1", lib60870 = "")
    address == "127.0.0.1" ||
        @warn "Die GUI ist ohne Anmeldung von $address aus erreichbar und kann Befehle senden."
    GUI_ADDRESS[] = address
    PEER.folder = peer_folder(lib60870)
    atexit(peer_kill)
    server = listen(parse(IPAddr, address), port)
    GUI_PORT[] = Int(getsockname(server)[2])   # port 0: the one chosen by the system
    println("IEC 104 Test-GUI (Library ", IEC104.version(), "): http://localhost:$(GUI_PORT[])")
    flush(stdout)   # scripts wait for this line
    note("GUI gestartet, Library " * IEC104.version())
    if !isempty(PEER.folder)
        found = [x.key for x in PEER_PROGRAMS if peer_found(x)]
        note("Gegenstelle lib60870: $(PEER.folder), " *
             (isempty(found) ? "keine Beispielprogramme unter build/examples" : join(found, ", ")))
    end
    @async while true   # poll task: the library runs here, callbacks included
        GUI.client != C_NULL && client_poll(GUI.client, 0)
        GUI.server != C_NULL && server_poll(GUI.server, 0)
        selftest_poll()
        sleep(0.02)
    end
    while true
        sock = accept(server)
        @async handle(sock)
    end
end

if abspath(PROGRAM_FILE) == @__FILE__
    main(parse(Int, get(ARGS, 1, "8080")), get(ARGS, 2, "127.0.0.1"), get(ARGS, 3, ""))
end
