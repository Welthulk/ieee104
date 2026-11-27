# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# paths.jl - path redundancy in the test GUI, included by iec104_gui.jl.
#
# The controlling station of the test bench can hold two paths (docs/06,
# chapter "Zwei Wege"); the page shows both and disturbs them by hand or at
# random. A second test station ("Station B") carries the same points as the
# first ("Station A"), so that both paths can run inside the GUI. The series
# check proves that no value is lost: both stations send a running number,
# the controlling station counts what it delivers.
#
# Like everything else of the GUI: callbacks only store, the poll task calls
# path_poll() between two polls of the library.

using Random

mutable struct PathTest
    count::Int                   # paths of the test bench: 1 = one connection
    switches::Int                # switch-overs since connecting
    # Station B: second test station with the same points (path 2 inside the GUI)
    server_b::Ptr{Cvoid}
    connections_b::Dict{UInt32,Cint}
    divert::String               # value changes go to "ab" (both), "a" or "b"
    # Random faults
    auto::Bool
    both::Bool                   # both paths may be disturbed at the same time
    min_s::Float64
    max_s::Float64
    seed::Int
    rng::Xoshiro
    next::Float64
    faults::Int
    # Series check: running number on one IOA of both stations
    series_on::Bool
    series_ioa::UInt32
    series_ms::Int
    series_next::Float64
    series_sent::Int
    series_seen::BitSet
    series_dup::Int
    series_max::Int
end

const PATHS = PathTest(1, 0, C_NULL, Dict(), "ab", false, false, 5.0, 15.0, 0, Xoshiro(0), 0.0, 0,
                       false, 9000, 200, 0.0, 0, BitSet(), 0, 0)
const PATH_RECONNECT_MS = 2000   # the GUI shows a lost path coming back soon
const FAULT_TEXT = Dict(:close => "getrennt", :silent => "stumm geschaltet", :restore => "freigegeben")

# --- Callbacks ------------------------------------------------------------------------

"Switch-over of the leading path (path callback of the library)."
function cb_path(::Ptr{Cvoid}, e::Ptr{PathSwitch})::Cvoid
    s = unsafe_load(e)
    reason = unsafe_string(s.reason)
    if s.leading == 0
        note("Leitstelle: kein Weg mehr gestartet – $reason. Die Library verbindet weiter.";
             kind = "warning")
    elseif s.previous == 0 && PATHS.switches == 0
        note("Leitstelle: Weg $(s.leading) ist führend (erster gestarteter Weg); der andere Weg läuft mit")
    else
        PATHS.switches += s.previous != 0 ? 1 : 0
        note("Leitstelle: Umschaltung auf Weg $(s.leading)" *
             (s.previous != 0 ? " (bisher Weg $(s.previous))" : "") *
             " – nachgeliefert $(s.replayed), unterdrückt $(s.suppressed), verloren $(s.dropped)";
             kind = "warning",
             explain = "Grund: $reason. Nachgeliefert: was der neue Weg empfangen hat, während der " *
                       "Ausfall noch nicht erkannt war; unterdrückt: schon über den alten Weg " *
                       "zugestellt. Danach folgt eine Generalabfrage auf dem neuen Weg.")
    end
    return nothing
end

"Value of the series IOA delivered to the controlling station (called by cb_client_object)."
function series_seen(ioa, cause, value)
    (ioa == PATHS.series_ioa && cause == 3 && isinteger(value) && value >= 1) || return
    n = Int(value)
    if n in PATHS.series_seen
        PATHS.series_dup += 1
    else
        push!(PATHS.series_seen, n)
        PATHS.series_max = max(PATHS.series_max, n)
    end
end

function cb_server_b_state(::Ptr{Cvoid}, conn::UInt32, state::Cint, reason::Cint)::Cvoid
    if state == IEC104.LINK_CLOSED
        delete!(PATHS.connections_b, conn)
        note("Station B: Verbindung $conn getrennt – " * close_hint(reason))
    else
        PATHS.connections_b[conn] = state
        note("Station B: Verbindung $conn " * state_text(state))
    end
    return nothing
end

function cb_server_b_command(::Ptr{Cvoid}, conn::UInt32, info::Ptr{AsduInfo}, cmd::Ptr{Object})::Cint
    i = unsafe_load(info)
    c = unsafe_load(cmd)
    what = i.type == 103 ? "Uhrzeit" : "Befehl Typ $(i.type) IOA $(c.ioa) $(value_text(c))"
    note("Station B: $what von Verbindung $conn: " * (GUI.accept_commands ? "angenommen" : "abgelehnt"))
    return Cint(GUI.accept_commands ? 1 : 0)
end

cb_log_server_b(::Ptr{Cvoid}, level::Cint, conn::UInt32, message::Cstring)::Cvoid =
    log_entry("B", level, conn, message)

# --- Station B ----------------------------------------------------------------------------

"Starts station B on @p port with the points of station A (called by gui_server_listen)."
function station_b_start(port::Integer)
    station_b_stop()
    cb = Callbacks(PATHS;
        on_state = @cfunction(cb_server_b_state, Cvoid, (Ptr{Cvoid}, UInt32, Cint, Cint)),
        on_command = @cfunction(cb_server_b_command, Cint, (Ptr{Cvoid}, UInt32, Ptr{AsduInfo}, Ptr{Object})),
        on_security = @cfunction(cb_security, Cvoid, (Ptr{Cvoid}, UInt32, Cint, Cstring)))
    s = server_create(LinkConfig(), cb)
    try
        server_set_log(s, 4, @cfunction(cb_log_server_b, Cvoid, (Ptr{Cvoid}, Cint, UInt32, Cstring)), C_NULL)
        for o in values(GUI.points)
            server_add_point(s, GUI.server_station, o)
        end
        IEC104.server_listen(s, port; address = GUI_ADDRESS[])
    catch
        server_destroy(s)
        rethrow()
    end
    PATHS.server_b = s
    note("Station B: lauscht auf $(GUI_ADDRESS[]):$(server_port(s)), dieselben Datenpunkte wie Station A")
end

function station_b_stop()
    PATHS.server_b == C_NULL && return
    s = PATHS.server_b
    PATHS.server_b = C_NULL
    server_destroy(s)
    empty!(PATHS.connections_b)
end

"Stations a value change goes to: (handle, name) pairs, after the setting of the page."
function stations_for_update(all::Bool = false)
    out = Tuple{Ptr{Cvoid},String}[]
    GUI.server != C_NULL && (all || PATHS.divert != "b") && push!(out, (GUI.server, "A"))
    PATHS.server_b != C_NULL && (all || PATHS.divert != "a") && push!(out, (PATHS.server_b, "B"))
    return out
end

# --- Actions --------------------------------------------------------------------------------

"Path @p nr of the test bench: close, silence or restore it (by hand)."
function gui_client_path(p)
    need_client()
    PATHS.count >= 2 || throw(ArgumentError("nur mit zwei Wegen (Weg 2 beim Verbinden angeben)"))
    nr = p_int(p, "nr", 1)
    1 <= nr <= PATHS.count || throw(ArgumentError("Weg 1…$(PATHS.count)"))
    fault = get(Dict("close" => :close, "silent" => :silent, "restore" => :restore), p_str(p, "fault"), nothing)
    fault === nothing && throw(ArgumentError("Störung: close, silent oder restore"))
    client_disturb_path(GUI.client, nr, fault)
    note("Leitstelle: Weg $nr von Hand $(FAULT_TEXT[fault])"; kind = fault == :restore ? "event" : "warning",
         explain = fault == :silent ?
             "Der Weg liest und schreibt nichts mehr, wie bei einer unterbrochenen Leitung. Die Library " *
             "merkt es erst über die Timer (t3, dann t1) und schaltet dann um." : "")
end

"Random faults on or off: interval min..max seconds, start value of the random numbers."
function gui_client_auto(p)
    on = get(p, "on", "1") == "1"
    if on
        need_client()
        PATHS.count >= 2 || throw(ArgumentError("nur mit zwei Wegen (Weg 2 beim Verbinden angeben)"))
        lo = p_num(p, "min", 5.0)
        hi = p_num(p, "max", 15.0)
        0.5 <= lo <= hi <= 3600 || throw(ArgumentError("Abstand: 0,5 ≤ von ≤ bis ≤ 3600 s"))
        seed = p_int(p, "seed", 0)
        seed == 0 && (seed = rand(1:999999))
        PATHS.min_s, PATHS.max_s, PATHS.seed = lo, hi, seed
        PATHS.both = get(p, "both", "0") == "1"
        PATHS.rng = Xoshiro(seed)
        PATHS.faults = 0
        PATHS.next = time() + next_gap()
        PATHS.auto = true
        note("Zufällige Störungen ein: alle $(lo)–$(hi) s, Startwert $seed" *
             (PATHS.both ? ", beide Wege zugleich erlaubt" : ", nie beide zugleich");
             explain = "Mit demselben Startwert wählt ein neuer Lauf dieselben Wege, Arten und Abstände.")
    elseif PATHS.auto
        PATHS.auto = false
        note("Zufällige Störungen aus nach $(PATHS.faults) Störungen (Startwert $(PATHS.seed))")
    end
end

next_gap() = PATHS.min_s + rand(PATHS.rng) * (PATHS.max_s - PATHS.min_s)

"Series check: start (running number from 1), stop or reset the counters."
function gui_series(p)
    what = p_str(p, "do")
    if what == "start"
        ioa = p_int(p, "ioa", 9000)
        ms = p_int(p, "ms", 200)
        1 <= ioa <= 16777215 || throw(ArgumentError("IOA 1…16777215"))
        20 <= ms <= 60000 || throw(ArgumentError("Takt 20…60000 ms"))
        GUI.server != C_NULL || throw(ArgumentError("erst die Station starten (Reiter „Station“)"))
        old = get(GUI.points, UInt32(ioa), nothing)
        old !== nothing && old.type != 13 &&
            throw(ArgumentError("IOA $ioa ist schon als Typ $(old.type) angelegt; andere IOA wählen"))
        if old === nothing
            o = Object(13, ioa, 0.0)
            for (s, _) in stations_for_update(true)
                server_add_point(s, GUI.server_station, o)
            end
            GUI.points[UInt32(ioa)] = o
        end
        series_reset()
        PATHS.series_ioa = UInt32(ioa)
        PATHS.series_ms = ms
        PATHS.series_next = time()
        PATHS.series_on = true
        note("Reihenprüfung: Station(en) senden IOA $ioa = 1, 2, 3 … alle $ms ms" *
             (PATHS.server_b != C_NULL ? " (A und B)" : " (nur A)");
             explain = "Die Leitstelle zählt, welche Nummern sie zustellt: fehlend muss 0 bleiben, " *
                       "doppelt ist um eine Umschaltung herum erlaubt.")
    elseif what == "stop"
        PATHS.series_on = false
        note("Reihenprüfung angehalten: " * series_summary())
    elseif what == "reset"
        series_reset()
    else
        throw(ArgumentError("do: start, stop oder reset"))
    end
end

function series_reset()
    PATHS.series_sent = 0
    empty!(PATHS.series_seen)
    PATHS.series_dup = 0
    PATHS.series_max = 0
    PATHS.switches = 0
end

series_missing() = PATHS.series_max - length(PATHS.series_seen)
series_summary() = "gesendet $(PATHS.series_sent), zugestellt $(length(PATHS.series_seen)), " *
                   "fehlend $(series_missing()), doppelt $(PATHS.series_dup), " *
                   "Umschaltungen $(PATHS.switches)"

# --- Poll (called by the poll task of the GUI) ----------------------------------------------

function path_poll()
    PATHS.server_b != C_NULL && server_poll(PATHS.server_b, 0)
    now = time()
    # Series: the next number to both stations.
    if PATHS.series_on && now >= PATHS.series_next
        if GUI.server == C_NULL
            PATHS.series_on = false
            note("Reihenprüfung angehalten: Station beendet")
        else
            PATHS.series_sent += 1
            o = Object(13, PATHS.series_ioa, Float64(PATHS.series_sent))
            for (s, _) in stations_for_update(true)
                server_update(s, GUI.server_station, o)   # buffered while no connection is started
            end
            GUI.points[PATHS.series_ioa] = o
            PATHS.series_next = max(PATHS.series_next + PATHS.series_ms / 1000, now - 1)
        end
    end
    # Random faults.
    if PATHS.auto && now >= PATHS.next
        PATHS.next = now + next_gap()
        if GUI.client == C_NULL || PATHS.count < 2
            PATHS.auto = false
            return
        end
        nr = rand(PATHS.rng, 1:PATHS.count)
        fault = rand(PATHS.rng, (:close, :silent))
        others_ok = all(k -> k == nr || (i = client_path_info(GUI.client, k);
                                         i.state == IEC104.LINK_STARTED && i.silent == 0), 1:PATHS.count)
        if PATHS.both || others_ok
            try
                client_disturb_path(GUI.client, nr, fault)
                PATHS.faults += 1
                note("Störung $(PATHS.faults) (zufällig, Startwert $(PATHS.seed)): Weg $nr $(FAULT_TEXT[fault])";
                     kind = "warning")
            catch e
                e isa Iec104Error || rethrow()   # path not connected right now: next time
            end
        end
    end
end

# --- State for the page ------------------------------------------------------------------------

function paths_json(io)
    print(io, ",\"paths\":{\"count\":", PATHS.count, ",\"leading\":",
          GUI.client == C_NULL || PATHS.count < 2 ? 0 : client_leading_path(GUI.client),
          ",\"switches\":", PATHS.switches, ",\"list\":[")
    if GUI.client != C_NULL && PATHS.count >= 2
        for nr in 1:PATHS.count
            i = client_path_info(GUI.client, nr)
            nr > 1 && print(io, ',')
            print(io, "{\"nr\":", nr)
            jfield(io, "addr", IEC104.text(i.host) * ":" * string(i.port))
            jfield(io, "state", i.silent != 0 ? "stumm (Testhilfe)" : state_text(i.state))
            jfield(io, "close", i.last_close == 0 ? "" : close_hint(i.last_close))
            print(io, ",\"lead\":", i.leading != 0, ",\"silent\":", i.silent != 0,
                  ",\"up\":", i.state == IEC104.LINK_STARTED, ",\"connects\":", i.connects,
                  ",\"led\":", i.became_leading, ",\"followed\":", i.followed, ",\"replayed\":",
                  i.replayed, ",\"suppressed\":", i.suppressed, ",\"dropped\":", i.dropped,
                  ",\"buffered\":", i.buffered, ",\"rx\":", i.stats.i_frames_received, '}')
        end
    end
    print(io, "],\"auto\":", PATHS.auto, ",\"seed\":", PATHS.seed, ",\"faults\":", PATHS.faults,
          "},\"series\":{\"on\":", PATHS.series_on, ",\"ioa\":", PATHS.series_ioa, ",\"ms\":",
          PATHS.series_ms, ",\"sent\":", PATHS.series_sent, ",\"delivered\":",
          length(PATHS.series_seen), ",\"missing\":", series_missing(), ",\"dup\":", PATHS.series_dup,
          ",\"pending\":", PATHS.series_sent - PATHS.series_max, '}')
end

"Station B and the target of value changes, inside the server object of the state."
function station_b_json(io)
    print(io, ",\"port_b\":", PATHS.server_b == C_NULL ? 0 : server_port(PATHS.server_b))
    jfield(io, "divert", PATHS.divert)
    print(io, ",\"connections_b\":[")
    for (n, (id, st)) in enumerate(sort!(collect(PATHS.connections_b)))
        n > 1 && print(io, ',')
        print(io, "{\"id\":", id, ",\"state\":")
        jstr(io, state_text(st))
        print(io, '}')
    end
    print(io, ']')
end
