# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# iec104_gui.jl - test GUI in the browser: controlling station (client) and
# controlled station (server) of the IEC 104 library, side by side.
#
#   julia iec104_gui.jl [port] [address]     default 8080 127.0.0.1
#   then open http://localhost:8080
#
# Only the Julia standard library is used (Sockets); the page is index.html
# next to this file. The GUI can send commands: it listens on 127.0.0.1 only,
# unless another address is given explicitly.
#
# Design: one Julia thread. A poll task drives client and server every 20 ms;
# the library calls the callbacks inside that poll and they only store data.
# HTTP requests are handled in their own tasks between two polls.

include(joinpath(@__DIR__, "..", "IEC104.jl"))
using .IEC104
using Sockets
using Printf

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
    # Event list shown on the page
    log::Vector{Tuple{Int,Float64,String}}
    log_seq::Int
end

const GUI = Gui(C_NULL, IEC104.LINK_CLOSED, 0, 1, Dict(), C_NULL, 1, true, Dict(), Dict(), [], 0)
const LOG_MAX = 500

note(text::AbstractString) = begin
    GUI.log_seq += 1
    push!(GUI.log, (GUI.log_seq, time(), String(text)))
    length(GUI.log) > LOG_MAX && popfirst!(GUI.log)
    nothing
end

# --- Callbacks (run inside the poll, called from C: store only, no I/O) -------

function cb_client_state(::Ptr{Cvoid}, ::UInt32, state::Cint, reason::Cint)::Cvoid
    GUI.client_state = state
    GUI.client_reason = reason
    note("Leitstelle: " * IEC104.state_string(state) *
         (state == IEC104.LINK_CLOSED ? " (" * IEC104.close_reason_string(reason) * ")" : ""))
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
        note("Station: Verbindung $conn geschlossen (" * IEC104.close_reason_string(reason) * ")")
    else
        GUI.connections[conn] = state
        note("Station: Verbindung $conn " * IEC104.state_string(state))
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

function cb_security(::Ptr{Cvoid}, conn::UInt32, event::Cint, detail::Cstring)::Cvoid
    note("TLS $conn: " * IEC104.security_event_string(event) * ": " * unsafe_string(detail))
    return nothing
end

function cb_log(::Ptr{Cvoid}, level::Cint, conn::UInt32, message::Cstring)::Cvoid
    level <= 1 && note("Library $conn: " * unsafe_string(message))   # errors and warnings
    return nothing
end

# --- Actions ---------------------------------------------------------------------

p_int(p, k, d) = haskey(p, k) && !isempty(p[k]) ? parse(Int, p[k]) : d
p_num(p, k, d) = haskey(p, k) && !isempty(p[k]) ? parse(Float64, replace(p[k], ',' => '.')) : d
p_str(p, k) = get(p, k, "")

function gui_client_connect(p)
    GUI.client != C_NULL && (client_destroy(GUI.client); GUI.client = C_NULL)
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
    client_set_log(GUI.client, 1, @cfunction(cb_log, Cvoid, (Ptr{Cvoid}, Cint, UInt32, Cstring)), C_NULL)
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
        on_state = @cfunction(cb_server_state, Cvoid, (Ptr{Cvoid}, UInt32, Cint, Cint)),
        on_command = @cfunction(cb_server_command, Cint, (Ptr{Cvoid}, UInt32, Ptr{AsduInfo}, Ptr{Object})),
        on_security = @cfunction(cb_security, Cvoid, (Ptr{Cvoid}, UInt32, Cint, Cstring)))
    GUI.server = server_create(LinkConfig(), cb)
    server_set_log(GUI.server, 1, @cfunction(cb_log, Cvoid, (Ptr{Cvoid}, Cint, UInt32, Cstring)), C_NULL)
    GUI.server_station = UInt16(p_int(p, "station", 1))
    for o in values(GUI.points)
        server_add_point(GUI.server, GUI.server_station, o)
    end
    IEC104.server_listen(GUI.server, p_int(p, "port", 2404))
    note("Station: lauscht auf Port $(server_port(GUI.server)), gemeinsame Adresse $(GUI.server_station)")
end

function gui_server_point(p)
    o = Object(p_int(p, "type", 13), p_int(p, "ioa", 100), p_num(p, "value", 0.0))
    known = haskey(GUI.points, o.ioa)
    GUI.points[o.ioa] = o
    GUI.server == C_NULL && return
    if known
        check(server_update(GUI.server, GUI.server_station, o))   # spontaneous
    else
        server_add_point(GUI.server, GUI.server_station, o)
        note("Station: Datenpunkt IOA $(o.ioa) Typ $(o.type) angelegt")
    end
end

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
    print(io, "{\"client\":{\"state\":")
    jstr(io, GUI.client == C_NULL ? "–" : IEC104.state_string(GUI.client_state))
    print(io, ",\"station\":", GUI.station, ",\"rows\":[")
    rows = sort!(collect(values(GUI.rows)); by = r -> (r.station, r.ioa, r.type))
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
    print(io, "]},\"server\":{\"running\":", GUI.server != C_NULL, ",\"port\":",
          GUI.server == C_NULL ? 0 : server_port(GUI.server), ",\"station\":", GUI.server_station,
          ",\"accept\":", GUI.accept_commands, ",\"connections\":[")
    for (n, (id, st)) in enumerate(sort!(collect(GUI.connections)))
        n > 1 && print(io, ',')
        print(io, "{\"id\":", id, ",\"state\":")
        jstr(io, IEC104.state_string(st))
        print(io, '}')
    end
    print(io, "],\"points\":[")
    for (n, o) in enumerate(sort!(collect(values(GUI.points)); by = o -> o.ioa))
        n > 1 && print(io, ',')
        print(io, "{\"ioa\":", o.ioa, ",\"type\":", o.type, ",\"value\":")
        jnum(io, o.value)
        print(io, '}')
    end
    print(io, "]},\"log\":[")
    first = true
    for (seq, t, text) in GUI.log
        seq <= since && continue
        first || print(io, ',')
        first = false
        print(io, "{\"seq\":", seq, ",\"time\":")
        jstr(io, Libc.strftime("%H:%M:%S", t))
        print(io, ",\"text\":")
        jstr(io, text)
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

function route(path, p)
    path == "/" && return ("200 OK", "text/html; charset=utf-8", PAGE)
    path == "/api/state" && return ("200 OK", "application/json", state_json(p_int(p, "since", 0)))
    action = get(ACTIONS, path, nothing)
    action === nothing && return ("404 Not Found", "text/plain", "not found")
    try
        action(p)
        return ("200 OK", "application/json", "{\"ok\":true}")
    catch e
        msg = e isa Iec104Error ? e.msg : sprint(showerror, e)
        note("Fehler: $msg")
        io = IOBuffer()
        print(io, "{\"ok\":false,\"error\":")
        jstr(io, msg)
        print(io, '}')
        return ("200 OK", "application/json", String(take!(io)))
    end
end

function handle(sock)
    try
        request = readline(sock)
        while !isempty(strip(readline(sock))) end   # headers are not needed
        parts = split(request)
        length(parts) >= 2 || return
        target = parts[2]
        path, query = occursin('?', target) ? split(target, '?'; limit = 2) : (target, "")
        status, type, body = route(String(path), parse_query(query))
        write(sock, "HTTP/1.1 $status\r\nContent-Type: $type\r\nContent-Length: $(sizeof(body))\r\n" *
                    "Cache-Control: no-store\r\nConnection: close\r\n\r\n", body)
    catch e
        e isa Base.IOError || @warn "request failed" exception = e
    finally
        close(sock)
    end
end

# --- Main ----------------------------------------------------------------------------

function main(port = 8080, address = "127.0.0.1")
    address == "127.0.0.1" ||
        @warn "Die GUI ist ohne Anmeldung von $address aus erreichbar und kann Befehle senden."
    server = listen(parse(IPAddr, address), port)
    println("IEC 104 Test-GUI (Library ", IEC104.version(), "): http://localhost:$port")
    flush(stdout)
    note("GUI gestartet, Library " * IEC104.version())
    @async while true   # poll task: the library runs here, callbacks included
        GUI.client != C_NULL && client_poll(GUI.client, 0)
        GUI.server != C_NULL && server_poll(GUI.server, 0)
        sleep(0.02)
    end
    while true
        sock = accept(server)
        @async handle(sock)
    end
end

if abspath(PROGRAM_FILE) == @__FILE__
    main(parse(Int, get(ARGS, 1, "8080")), get(ARGS, 2, "127.0.0.1"))
end
