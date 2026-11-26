# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# selftest.jl - self-test of the test GUI, included by iec104_gui.jl.
#
# A station and a controlling station of the library talk to each other on
# 127.0.0.1 with their own handles; the station and controlling station of the
# page are not touched. Every step states its purpose, the expectation and what
# was observed. The report lists the frames of each step with their meaning
# (frames.jl), short (failed steps only) or full.
#
# Expectations follow the library's documented behaviour (docs/05, E10 for the
# buffering during STOPDT) and the field layout in docs/02.
#
# The steps run in their own task; they wait with sleep(), so the poll task
# keeps the library running. Callbacks only store events (docs/07, section 4).

mutable struct Step
    nr::Int
    title::String
    purpose::String
    expected::String
    observed::String
    ok::Union{Nothing,Bool}     # nothing: skipped
    running::Bool
    t_start::Float64
    t_end::Float64
    first_frame::Int            # frames of this step: ST.frames[first_frame:last_frame]
    last_frame::Int
end

mutable struct SelfTest
    running::Bool
    started::Float64
    finished::Float64
    steps::Vector{Step}
    client::Ptr{Cvoid}
    server::Ptr{Cvoid}
    port::Int
    accept::Bool                # answer of the station to commands
    client_state::Cint
    events::Vector{Any}         # NamedTuples stored by the callbacks
    frames::Vector{Tuple{Float64,String,String,String}}   # time, side and direction, summary, meaning
end

const ST = SelfTest(false, 0.0, 0.0, Step[], C_NULL, C_NULL, 0, true, IEC104.LINK_CLOSED, [], [])
const ST_FRAMES_MAX = 5000
const ST_CLIENT_CONN = Ref(0)   # connection number of the test client at the station
const ST_T3_S = 3            # short t3 of the controlling station: the idle test comes quickly
const ST_STEPS = 22          # without the optional lib60870 step
const CMD_TERMINATED = 1     # iec104_command_result: confirmed and terminated
const CMD_REJECTED = 3       # negative answer

# Data points of the test station 1: IOA, type, value, interrogation groups (bit 0 = group 1).
const ST_POINTS = [(101, 1, 1.0, 1), (102, 3, 2.0, 1), (103, 5, 7.0, 1), (104, 9, 0.5, 1),
                   (105, 11, 1234.0, 1), (106, 13, 21.5, 1), (107, 15, 100.0, 1), (108, 13, 8.5, 2)]

# --- Callbacks (inside the poll: store only) ---------------------------------------

st_event(e) = (push!(ST.events, e); nothing)

function st_client_state(::Ptr{Cvoid}, ::UInt32, state::Cint, reason::Cint)::Cvoid
    ST.client_state = state
    st_event((kind = :cstate, state = Int(state), reason = Int(reason)))
end

function st_client_object(::Ptr{Cvoid}, ::UInt32, info::Ptr{AsduInfo}, obj::Ptr{Object})::Cvoid
    i = unsafe_load(info)
    o = unsafe_load(obj)
    st_event((kind = :object, type = Int(i.type), cause = Int(i.cause), negative = i.negative != 0,
              station = Int(i.common_address), ioa = Int(o.ioa), value = o.value))
end

function st_command_result(::Ptr{Cvoid}, ::UInt16, cmd::Ptr{Object}, result::Cint)::Cvoid
    c = unsafe_load(cmd)
    st_event((kind = :result, type = Int(c.type), ioa = Int(c.ioa), result = Int(result)))
end

function st_server_state(::Ptr{Cvoid}, conn::UInt32, state::Cint, reason::Cint)::Cvoid
    st_event((kind = :sstate, conn = Int(conn), state = Int(state), reason = Int(reason)))
end

function st_server_command(::Ptr{Cvoid}, conn::UInt32, info::Ptr{AsduInfo}, cmd::Ptr{Object})::Cint
    i = unsafe_load(info)
    c = unsafe_load(cmd)
    st_event((kind = :command, conn = Int(conn), type = Int(i.type), ioa = Int(c.ioa), value = c.value,
              select = c.select != 0))
    return Cint(ST.accept ? 1 : 0)
end

function st_server_asdu(::Ptr{Cvoid}, conn::UInt32, asdu::Ptr{UInt8}, len::Csize_t)::Cvoid
    len >= 3 && st_event((kind = :request, conn = Int(conn), type = Int(unsafe_load(asdu, 1)),
                          cause = Int(unsafe_load(asdu, 3) & 0x3f)))
    return nothing
end

"Library log of the test handles: errors as events, every frame (trace) described."
function st_log(side::String, level::Cint, conn::UInt32, message::Cstring)
    try
        m = unsafe_string(message)
        if level <= 1
            st_event((kind = :log, level = Int(level), text = m))
        elseif level == 4
            sent = startswith(m, "sent ")
            (sent || startswith(m, "received ")) || return nothing
            summary, meaning = Frames.describe(Frames.parse_hex(m[(sent ? 6 : 10):end]))
            who = side == "L" ? "Leitstelle" : "Station($conn)"
            length(ST.frames) < ST_FRAMES_MAX &&
                push!(ST.frames, (time(), who * (sent ? " →" : " ←"), summary, meaning))
        end
    catch
    end
    return nothing
end
st_log_client(::Ptr{Cvoid}, level::Cint, conn::UInt32, message::Cstring)::Cvoid =
    st_log("L", level, conn, message)
st_log_server(::Ptr{Cvoid}, level::Cint, conn::UInt32, message::Cstring)::Cvoid =
    st_log("S", level, conn, message)

# --- Helpers for the steps -----------------------------------------------------------------

struct StepFailed <: Exception
    msg::String
end
fail(msg) = throw(StepFailed(msg))

"Waits until pred() holds, at most `timeout` seconds; the poll task runs meanwhile."
function wait_until(pred, timeout)
    t_end = time() + timeout
    while !pred()
        time() > t_end && return false
        sleep(0.02)
    end
    return true
end

matches(e, kw) = all(hasproperty(e, k) && getproperty(e, k) == v for (k, v) in kw)
"Events since index `mark` with the given fields, e.g. seen(m; kind = :object, cause = 3)."
seen(mark; kw...) = [e for e in ST.events[mark+1:end] if matches(e, kw)]

"Waits for an event with the given fields; fails with `what` after `timeout` seconds."
function expect(mark, what, timeout = 3.0; kw...)
    wait_until(() -> !isempty(seen(mark; kw...)), timeout) || fail("$what fehlt nach $(timeout) s")
    return first(seen(mark; kw...))
end

st_object(t, ioa, v) = Object(t, ioa, v)
st_timed(t, ioa, v) = Object(UInt8(t), UInt32(ioa), Float64(v), 0, 0, 0, 0, 0, time_now())
result_text(r) = IEC104.command_result_string(r)
ioas(es) = join(sort!(unique(e.ioa for e in es)), ", ")

"Runs one step; f() returns the observation or calls fail()."
function run_step(f, title, purpose, expected)
    s = Step(length(ST.steps) + 1, title, purpose, expected, "", false, true, time(), 0.0,
             length(ST.frames) + 1, 0)
    push!(ST.steps, s)
    try
        s.observed = f()
        s.ok = true
    catch e
        s.observed = e isa StepFailed ? e.msg :
                     e isa Iec104Error ? "Fehler der Library: $(e.msg)" : sprint(showerror, e)
        s.ok = false
    end
    sleep(0.15)   # late answers and acknowledgements still belong to this step
    s.t_end = time()
    s.last_frame = length(ST.frames)
    s.running = false
    return s.ok
end

function skip_step(title, purpose, why)
    s = Step(length(ST.steps) + 1, title, purpose, "–", why, nothing, false, time(), time(),
             length(ST.frames) + 1, length(ST.frames))
    push!(ST.steps, s)
end

# --- The test ---------------------------------------------------------------------------------

function selftest_start(p)
    ST.running && throw(ArgumentError("Selbsttest läuft bereits"))
    ST.running = true
    ST.started = time()
    ST.finished = 0.0
    empty!(ST.steps)
    empty!(ST.events)
    empty!(ST.frames)
    ST_CLIENT_CONN[] = 0
    ST.accept = true
    note("Selbsttest gestartet (eigene Station und Leitstelle auf 127.0.0.1)")
    @async try
        selftest_steps()
    catch e
        note("Selbsttest abgebrochen: " * sprint(showerror, e); kind = "error")
    finally
        selftest_cleanup()
        ST.finished = time()
        ST.running = false
        passed = count(s -> s.ok === true, ST.steps)
        note("Selbsttest beendet: $passed von $(count(s -> s.ok !== nothing, ST.steps)) bestanden" *
             (any(s -> s.ok === false, ST.steps) ? " – Einzelheiten unter „Protokoll kopieren“" : ""))
    end
    return nothing
end

function selftest_cleanup()
    c, s = ST.client, ST.server
    ST.client = C_NULL   # the poll task must not use the handles any more
    ST.server = C_NULL
    c != C_NULL && client_destroy(c)
    s != C_NULL && server_destroy(s)
end

"Called by the poll task of the GUI."
function selftest_poll()
    ST.client != C_NULL && client_poll(ST.client, 0)
    ST.server != C_NULL && server_poll(ST.server, 0)
    return nothing
end

function selftest_steps()
    log_fn_c = @cfunction(st_log_client, Cvoid, (Ptr{Cvoid}, Cint, UInt32, Cstring))
    log_fn_s = @cfunction(st_log_server, Cvoid, (Ptr{Cvoid}, Cint, UInt32, Cstring))

    run_step("Station starten",
             "Die Library als Station (Server) auf 127.0.0.1 mit einem Punkt je Meldetyp",
             "lauscht auf einem freien Port; 8 Datenpunkte (Typ 1, 3, 5, 9, 11, 13, 15) angelegt") do
        cb = Callbacks(ST;
            on_asdu = @cfunction(st_server_asdu, Cvoid, (Ptr{Cvoid}, UInt32, Ptr{UInt8}, Csize_t)),
            on_state = @cfunction(st_server_state, Cvoid, (Ptr{Cvoid}, UInt32, Cint, Cint)),
            on_command = @cfunction(st_server_command, Cint, (Ptr{Cvoid}, UInt32, Ptr{AsduInfo}, Ptr{Object})))
        ST.server = server_create(LinkConfig(), cb)
        server_set_log(ST.server, 4, log_fn_s, C_NULL)
        for (ioa, t, v, groups) in ST_POINTS
            server_add_point(ST.server, 1, st_object(t, ioa, v), groups)
        end
        server_listen(ST.server, 0; address = "127.0.0.1")
        ST.port = server_port(ST.server)
        "lauscht auf 127.0.0.1:$(ST.port), Station 1, IOA 101–108"
    end || return

    m = length(ST.events)
    run_step("Verbinden",
             "Die Library als Leitstelle (Client) baut die TCP-Verbindung zur Station auf",
             "Verbindung steht, beide Seiten im Zustand „verbunden, Datenübertragung aus“") do
        cfg = LinkConfig()
        cfg.t3_ms = ST_T3_S * 1000
        cb = Callbacks(ST;
            on_state = @cfunction(st_client_state, Cvoid, (Ptr{Cvoid}, UInt32, Cint, Cint)),
            on_object = @cfunction(st_client_object, Cvoid, (Ptr{Cvoid}, UInt32, Ptr{AsduInfo}, Ptr{Object})),
            on_command_result = @cfunction(st_command_result, Cvoid, (Ptr{Cvoid}, UInt16, Ptr{Object}, Cint)))
        ST.client = client_create(cfg, cb)
        client_set_log(ST.client, 4, log_fn_c, C_NULL)
        client_connect(ST.client, "127.0.0.1", ST.port)
        expect(m, "Leitstelle verbunden", 5.0; kind = :cstate, state = Int(IEC104.LINK_STOPPED))
        e = expect(m, "Verbindung an der Station", 2.0; kind = :sstate, state = Int(IEC104.LINK_STOPPED))
        ST_CLIENT_CONN[] = e.conn
        "verbunden; Station sieht die Verbindung"
    end || return

    m = length(ST.events)
    run_step("STARTDT",
             "Die Leitstelle gibt die Datenübertragung frei (U-Rahmen STARTDT act/con)",
             "Zustand „Datenübertragung läuft“; Station meldet „Ende der Initialisierung“ (Typ 70, Ursache 4)") do
        client_start(ST.client)
        expect(m, "STARTDT-Bestätigung"; kind = :cstate, state = Int(IEC104.LINK_STARTED))
        expect(m, "Ende der Initialisierung (Typ 70)"; kind = :object, type = 70, cause = 4)
        "Datenübertragung läuft, Typ 70 Ursache 4 erhalten"
    end || return

    m = length(ST.events)
    run_step("Generalabfrage Station",
             "Abfrage aller Werte der Station (Typ 100, QOI 20)",
             "Bestätigung (Ursache 7), alle Punkte außer dem Zählwert genau einmal mit Ursache 20, " *
             "Typ und Wert wie angelegt (IOA 101–106, 108), Beendigung (Ursache 10)") do
        client_interrogation(ST.client, 1, 20)
        expect(m, "Beendigung der Generalabfrage"; kind = :object, type = 100, cause = 10)
        isempty(seen(m; kind = :object, type = 100, cause = 7, negative = false)) &&
            fail("keine positive Bestätigung")
        got = seen(m; kind = :object, cause = 20)
        ioas(got) == "101, 102, 103, 104, 105, 106, 108" || fail("Punkte mit Ursache 20: $(ioas(got))")
        length(got) == 7 || fail("$(length(got)) Antworten statt 7 (doppelt?)")
        bad = String[]
        for (ioa, t, v, _) in ST_POINTS
            t == 15 && continue
            e = first(x for x in got if x.ioa == ioa)
            (e.type == t && e.value == v) || push!(bad, "IOA $ioa: Typ $(e.type) Wert $(e.value) statt Typ $t Wert $v")
        end
        isempty(bad) || fail(join(bad, "; "))
        "Bestätigung, 7 Werte mit richtigem Typ und Wert (IOA $(ioas(got))), Beendigung"
    end

    m = length(ST.events)
    run_step("Generalabfrage Gruppe 2",
             "Abfrage nur der Punkte der Gruppe 2 (QOI 22)",
             "nur IOA 108 mit Ursache 22, eingerahmt von Bestätigung und Beendigung") do
        client_interrogation(ST.client, 1, 22)
        expect(m, "Beendigung der Gruppenabfrage"; kind = :object, type = 100, cause = 10)
        got = seen(m; kind = :object, cause = 22)
        ioas(got) == "108" || fail("Punkte mit Ursache 22: $(ioas(got))")
        isempty(seen(m; kind = :object, cause = 20)) || fail("Punkte der Station mitgeschickt")
        "nur IOA 108 mit Ursache 22"
    end

    m = length(ST.events)
    run_step("Zählerabfrage",
             "Abfrage der Zählwerte (Typ 101, RQT 5 = alle)",
             "Bestätigung, Zählwert IOA 107 = 100 mit Ursache 37, Beendigung") do
        client_counter_interrogation(ST.client, 1)
        expect(m, "Beendigung der Zählerabfrage"; kind = :object, type = 101, cause = 10)
        e = expect(m, "Zählwert IOA 107"; kind = :object, ioa = 107, cause = 37)
        e.value == 100 || fail("Zählwert $(e.value) statt 100")
        "Zählwert IOA 107 = $(e.value), Ursache 37"
    end

    m = length(ST.events)
    run_step("Lesebefehl",
             "Einen einzelnen Wert abfragen (Typ 102, Ursache 5)",
             "IOA 106 = 21.5 mit Ursache 5") do
        client_read(ST.client, 1, 106)
        e = expect(m, "Antwort auf den Lesebefehl"; kind = :object, ioa = 106, cause = 5)
        e.value == 21.5 || fail("Wert $(e.value) statt 21.5")
        "IOA 106 = $(e.value), Ursache 5"
    end

    m = length(ST.events)
    changes = [(101, 1, 0.0), (102, 3, 1.0), (103, 5, -3.0), (104, 9, -0.25), (105, 11, -42.0), (106, 13, 3.25)]
    run_step("Spontane Werte",
             "Die Station ändert je Meldetyp einen Wert; die Leitstelle muss ihn ohne Abfrage erhalten",
             "6 Werte mit Ursache 3 (spontan), jeweils der neue Wert") do
        for (ioa, t, v) in changes
            check(server_update(ST.server, 1, st_object(t, ioa, v)))
        end
        bad = String[]
        for (ioa, t, v) in changes
            e = expect(m, "spontaner Wert IOA $ioa"; kind = :object, ioa = ioa, cause = 3)
            e.value == v || push!(bad, "IOA $ioa: $(e.value) statt $v")
        end
        isempty(bad) || fail(join(bad, "; "))
        "6 Werte spontan erhalten: " * join(("$ioa=$v" for (ioa, _, v) in changes), ", ")
    end

    m = length(ST.events)
    run_step("Uhrzeit setzen",
             "Uhrzeitsynchronisation (Typ 103) mit der aktuellen Zeit",
             "Station nimmt an, Leitstelle erhält Bestätigung (Ursache 7, positiv)") do
        client_clock_sync(ST.client, 1)
        expect(m, "Uhrzeit an der Station"; kind = :command, type = 103)
        expect(m, "Bestätigung der Uhrzeit"; kind = :object, type = 103, cause = 7, negative = false)
        "Station hat die Uhrzeit erhalten und bestätigt"
    end

    m = length(ST.events)
    direct = [(45, 1.0), (46, 2.0), (47, 2.0), (48, 0.5), (49, 100.0), (50, 12.5)]
    run_step("Befehle direkt (45–50)",
             "Je Befehlstyp ein Befehl ohne Auswahl an IOA 6000 + Typ",
             "Station erhält jeden Befehl mit dem Wert; Ergebnis „ausgeführt“ (Bestätigung und Beendigung)") do
        for (t, v) in direct
            client_send_command(ST.client, 1, st_object(t, 6000 + t, v))
        end
        bad = String[]
        for (t, v) in direct
            r = expect(m, "Ergebnis Typ $t"; kind = :result, ioa = 6000 + t)
            r.result == CMD_TERMINATED || push!(bad, "Typ $t: $(result_text(r.result))")
            c = seen(m; kind = :command, type = t, ioa = 6000 + t)
            (isempty(c) || first(c).value != v) && push!(bad, "Typ $t: Wert an der Station falsch")
        end
        isempty(bad) || fail(join(bad, "; "))
        "6 Befehle ausgeführt (Typ 45–50)"
    end

    m = length(ST.events)
    run_step("Select/Execute (45)",
             "Einzelbefehl in zwei Schritten: erst auswählen, dann ausführen",
             "Station erhält Auswahl und danach Ausführung; Ergebnis „ausgeführt“ (die Library meldet nur das Endergebnis)") do
        client_select_execute(ST.client, 1, st_object(45, 6100, 1.0))
        expect(m, "Ergebnis „ausgeführt“"; kind = :result, ioa = 6100, result = CMD_TERMINATED)
        isempty(seen(m; kind = :command, ioa = 6100, select = true)) && fail("Auswahl kam nicht an")
        isempty(seen(m; kind = :command, ioa = 6100, select = false)) && fail("Ausführung kam nicht an")
        "Auswahl und Ausführung an der Station, Ergebnis „ausgeführt“"
    end

    m = length(ST.events)
    timed = [(58, 1.0), (59, 1.0), (60, 1.0), (61, -0.5), (62, -100.0), (63, 7.25)]
    run_step("Befehle mit Zeitmarke (58–63)",
             "Je Befehlstyp mit Zeitmarke (CP56Time2a, jetzt) ein Befehl",
             "Ergebnis jeweils „ausgeführt“") do
        for (t, v) in timed
            client_send_command(ST.client, 1, st_timed(t, 6000 + t, v))
        end
        bad = String[]
        for (t, _) in timed
            r = expect(m, "Ergebnis Typ $t"; kind = :result, ioa = 6000 + t)
            r.result == CMD_TERMINATED || push!(bad, "Typ $t: $(result_text(r.result))")
        end
        isempty(bad) || fail(join(bad, "; "))
        "6 Befehle mit Zeitmarke ausgeführt"
    end

    m = length(ST.events)
    run_step("Befehl abgelehnt",
             "Die Station lehnt einen Befehl ab",
             "negative Bestätigung; Ergebnis „abgelehnt“") do
        ST.accept = false
        try
            client_send_command(ST.client, 1, st_object(45, 6200, 1.0))
            r = expect(m, "Ergebnis"; kind = :result, ioa = 6200)
            r.result == CMD_REJECTED || fail("Ergebnis „$(result_text(r.result))“")
        finally
            ST.accept = true
        end
        "negative Bestätigung, Ergebnis „abgelehnt“"
    end

    m = length(ST.events)
    run_step("Unbekannte IOA",
             "Lesebefehl an eine IOA, die es nicht gibt (9999)",
             "negative Antwort mit Ursache 47 (IOA unbekannt)") do
        client_read(ST.client, 1, 9999)
        expect(m, "Antwort mit Ursache 47"; kind = :object, type = 102, cause = 47, negative = true)
        "negativ, Ursache 47"
    end

    m = length(ST.events)
    run_step("Unbekannte Station",
             "Generalabfrage an Station 99, die es nicht gibt",
             "negative Antwort mit Ursache 46 (Station unbekannt)") do
        client_interrogation(ST.client, 99, 20)
        expect(m, "Antwort mit Ursache 46"; kind = :object, type = 100, cause = 46, negative = true, station = 99)
        "negativ, Ursache 46"
    end

    run_step("Wert außerhalb des Bereichs",
             "Einzelbefehl mit Wert 2 (erlaubt sind 0 und 1)",
             "die Library weist ihn vor dem Senden ab („value out of range“); kein Rahmen geht hinaus") do
        before = count(f -> startswith(f[2], "Leitstelle →") && startswith(f[3], "I "), ST.frames)
        try
            client_send_command(ST.client, 1, st_object(45, 6300, 2.0))
            fail("Befehl wurde angenommen")
        catch e
            e isa Iec104Error && e.status == -5 || rethrow()   # IEC104_ERR_OUT_OF_RANGE
        end
        sleep(0.2)
        after = count(f -> startswith(f[2], "Leitstelle →") && startswith(f[3], "I "), ST.frames)
        after == before || fail("trotzdem ein I-Rahmen gesendet")
        "lokal abgewiesen: value out of range, nichts gesendet"
    end

    m = length(ST.events)
    run_step("STOPDT",
             "Die Leitstelle hält die Datenübertragung an; danach ändert die Station einen Wert",
             "Zustand „verbunden, Datenübertragung aus“; der neue Wert kommt NICHT an (die Station puffert ihn)") do
        client_stop(ST.client)
        expect(m, "STOPDT-Bestätigung"; kind = :cstate, state = Int(IEC104.LINK_STOPPED))
        check(server_update(ST.server, 1, st_object(13, 106, 99.0)))
        sleep(1.0)
        isempty(seen(m; kind = :object, ioa = 106)) || fail("Wert kam trotz STOPDT an")
        "angehalten; in 1 s kein Wert angekommen"
    end

    m = length(ST.events)
    run_step("STARTDT erneut",
             "Die Leitstelle startet die Datenübertragung wieder",
             "Zustand „Datenübertragung läuft“; der gepufferte Wert IOA 106 = 99 kommt mit Ursache 3 nach") do
        client_start(ST.client)
        expect(m, "STARTDT-Bestätigung"; kind = :cstate, state = Int(IEC104.LINK_STARTED))
        e = expect(m, "gepufferter Wert IOA 106"; kind = :object, ioa = 106, cause = 3)
        e.value == 99.0 || fail("Wert $(e.value) statt 99")
        "läuft wieder; IOA 106 = 99 nachgeliefert"
    end

    run_step("Testrahmen (t3)",
             "Ohne Verkehr schickt die Leitstelle nach t3 = $(ST_T3_S) s einen Testrahmen",
             "U-Rahmen TESTFR act von der Leitstelle, TESTFR con von der Station") do
        first_frame = length(ST.frames) + 1
        ok = wait_until(() -> any(f -> f[2] == "Leitstelle ←" && f[3] == "U TESTFR con",
                                  ST.frames[first_frame:end]), ST_T3_S + 5.0)
        ok || fail("kein TESTFR con innerhalb $(ST_T3_S + 5) s")
        any(f -> f[2] == "Leitstelle →" && f[3] == "U TESTFR act", ST.frames[first_frame:end]) ||
            fail("TESTFR act fehlt")
        "TESTFR act gesendet, TESTFR con erhalten"
    end

    m = length(ST.events)
    run_step("Trennen",
             "Die Leitstelle beendet die Verbindung",
             "Leitstelle „getrennt (von dieser Seite)“, Station „getrennt (Gegenstelle)“") do
        client_close(ST.client)
        expect(m, "Leitstelle getrennt"; kind = :cstate, state = Int(IEC104.LINK_CLOSED), reason = 1)
        expect(m, "Station getrennt"; kind = :sstate, state = Int(IEC104.LINK_CLOSED))
        "beide Seiten getrennt"
    end

    run_step("Rahmenstatistik",
             "Alle Rahmen der Leitstelle zählen und prüfen, ob jeder lesbar war",
             "I-, S- und U-Rahmen in beide Richtungen, kein unlesbarer Rahmen") do
        lf = [f for f in ST.frames if startswith(f[2], "Leitstelle")]
        n(dir, k) = count(f -> f[2] == "Leitstelle $dir" && startswith(f[3], k), lf)
        bad = count(f -> startswith(f[3], "?"), ST.frames)
        bad == 0 || fail("$bad unlesbare Rahmen")
        n("→", "I ") > 0 && n("←", "I ") > 0 && n("→", "U ") > 0 || fail("zu wenige Rahmen")
        "gesendet I $(n("→", "I ")), S $(n("→", "S ")), U $(n("→", "U ")); " *
        "empfangen I $(n("←", "I ")), S $(n("←", "S ")), U $(n("←", "U "))"
    end

    run_step("Fehler der Library",
             "Fehler und Warnungen, die die Library während des Tests gemeldet hat",
             "keine Fehler; Warnungen nur für die absichtlich provozierten negativen Antworten " *
             "(Befehl abgelehnt, unbekannte IOA, unbekannte Station)") do
        errors = seen(0; kind = :log, level = 0)
        isempty(errors) || fail(join(unique(e.text for e in errors), "; "))
        warnings = unique(e.text for e in seen(0; kind = :log, level = 1))
        expected = r"negative answer.*(type 45, cause 7|type 102, cause 47|type 100, cause 46)"
        other = filter(w -> !occursin(expected, w), warnings)
        isempty(other) || fail("unerwartete Warnungen: " * join(other, "; "))
        "keine Fehler; $(length(warnings)) erwartete Warnungen (negative Antworten)"
    end

    selftest_lib60870()
end

"Optional: lib60870 (simple_client) as independent controlling station against the test station."
function selftest_lib60870()
    title = "lib60870 als fremde Leitstelle"
    purpose = "Eine unabhängige Implementierung (lib60870, simple_client) verbindet sich mit der Station"
    i = findfirst(x -> x.key == "simple_client", PEER_PROGRAMS)
    x = PEER_PROGRAMS[i]
    if !peer_found(x)
        skip_step(title, purpose, "übersprungen: kein lib60870-Ordner beim Start der GUI angegeben")
        return
    end
    if peer_running()
        skip_step(title, purpose, "übersprungen: eine Gegenstelle läuft bereits")
        return
    end
    m = length(ST.events)
    run_step(title, purpose,
             "Verbindung, STARTDT, Generalabfrage und Testbefehl kommen an der Station an; simple_client endet mit 0") do
        proc = peer_launch(x, ST.port)
        wait_until(() -> !process_running(proc), 20.0) || fail("simple_client nach 20 s nicht beendet")
        proc.exitcode == 0 || fail("simple_client endete mit $(proc.exitcode)")
        isempty(seen(m; kind = :sstate, state = Int(IEC104.LINK_STARTED))) && fail("kein STARTDT")
        isempty(seen(m; kind = :request, type = 100)) && fail("keine Generalabfrage")
        isempty(seen(m; kind = :request, type = 107)) && fail("kein Testbefehl")
        "verbunden, STARTDT, Generalabfrage, Testbefehl; Rückgabe 0"
    end
end

# --- Page and report --------------------------------------------------------------------------

duration(s::Step) = s.t_end > 0 ? s.t_end - s.t_start : time() - s.t_start
status_word(s::Step) = s.running ? "LÄUFT" : s.ok === true ? "OK" : s.ok === false ? "FEHLER" : "ÜBERSPRUNGEN"
seconds(x) = replace(string(round(x; digits = 1)), '.' => ',') * " s"

function selftest_json(io)
    print(io, "\"selftest\":{\"running\":", ST.running, ",\"total\":",
          ST_STEPS + (any(peer_found, PEER_PROGRAMS) ? 1 : 0),
          ",\"passed\":", count(s -> s.ok === true, ST.steps),
          ",\"failed\":", count(s -> s.ok === false && !s.running, ST.steps), ",\"steps\":[")
    for (n, s) in enumerate(ST.steps)
        n > 1 && print(io, ',')
        print(io, "{\"nr\":", s.nr, ",\"title\":")
        jstr(io, s.title)
        print(io, ",\"status\":")
        jstr(io, status_word(s))
        print(io, ",\"expected\":")
        jstr(io, s.expected)
        print(io, ",\"observed\":")
        jstr(io, s.observed)
        print(io, ",\"duration\":")
        jstr(io, seconds(duration(s)))
        print(io, '}')
    end
    print(io, "]}")
end

stamp(t) = Libc.strftime("%H:%M:%S", t) * "." * lpad(floor(Int, (t % 1) * 1000), 3, '0')

"Text report: short = summary and failed steps with their frames; full = every step with frames."
function selftest_report(full::Bool)
    io = IOBuffer()
    println(io, "IEC 60870-5-104 Selbsttest der Test-GUI")
    println(io, "Library ", IEC104.version(), " · Julia ", VERSION, " · ", Sys.KERNEL, " ", Sys.ARCH,
            " · ", ST.started > 0 ? Libc.strftime("%Y-%m-%d %H:%M:%S", ST.started) : "nicht gelaufen")
    println(io, "Station und Leitstelle der Library auf 127.0.0.1:", ST.port,
            "; Normwerte (t0 30 s, t1 15 s, t2 10 s, k 12, w 8), Leitstelle t3 = ", ST_T3_S, " s")
    ST.running && println(io, "ACHTUNG: Selbsttest läuft noch")
    passed = count(s -> s.ok === true, ST.steps)
    run = count(s -> s.ok !== nothing && !s.running, ST.steps)
    total = ST.finished > 0 ? seconds(ST.finished - ST.started) : "–"
    println(io, "Ergebnis: ", passed, " von ", run, " bestanden",
            run == passed ? "" : " – $(run - passed) FEHLER", " (Dauer ", total, ")")
    println(io)
    println(io, "Rahmen: → gesendet, ← empfangen; eigene Verbindung aus Sicht der Leitstelle")
    println(io)
    println(io, "Nr  Ergebnis      Dauer   Schritt")
    for s in ST.steps
        println(io, lpad(s.nr, 2, '0'), "  ", rpad(status_word(s), 12), "  ", lpad(seconds(duration(s)), 6),
                "  ", s.title)
    end
    shown = full ? ST.steps : filter(s -> s.ok === false && !s.running, ST.steps)
    if isempty(shown)
        println(io)
        println(io, "Alle Schritte bestanden. Vollständiges Protokoll mit allen Rahmen: „Protokoll speichern“.")
    end
    for s in shown
        println(io)
        println(io, "[", lpad(s.nr, 2, '0'), "] ", s.title, " – ", status_word(s), " (", seconds(duration(s)), ")")
        println(io, "  Zweck:      ", s.purpose)
        println(io, "  Erwartet:   ", s.expected)
        println(io, "  Beobachtet: ", s.observed)
        # Frames of the own connection once, as the controlling station sees them; the
        # station's view only where another controlling station (lib60870) talks.
        own = "Station($(ST_CLIENT_CONN[]))"
        frames = s.first_frame <= s.last_frame ?
                 [f for f in ST.frames[s.first_frame:s.last_frame] if !startswith(f[2], own)] : []
        isempty(frames) && continue
        println(io, "  Rahmen:")
        for (t, who, summary, meaning) in frames
            println(io, "    ", stamp(t), " ", who, " ", summary)
            isempty(meaning) || println(io, "                 ", " "^length(who), " ↳ ", meaning)
        end
    end
    return String(take!(io))
end
