# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# IEC104.jl - thin Julia binding of the C API (include/iec104/iec104.h) via ccall.
#
# Only the types and functions the examples need; every other function of
# the header is called the same way (see docs/07_Julia.md). The library is
# taken from the environment variable IEC104_LIB (path of libiec104.so.1),
# else from ../lib next to this file (layout of the binary package), else
# from the normal search path (LD_LIBRARY_PATH).

module IEC104

export LinkConfig, IecTime, Object, AsduInfo, Callbacks, Iec104Error, TlsConfig
export version, tls_available, check, type_name, decode_asdu, client_set_options
export server_create, server_listen, server_port, server_add_point, server_update,
       server_poll, server_destroy
export client_create, client_connect, client_start, client_stop, client_poll,
       client_interrogation, client_destroy, client_close, client_state,
       client_counter_interrogation, client_clock_sync, client_send_command,
       client_select_execute, client_read, client_set_tls, client_set_log, server_set_log,
       server_connection_count, time_now, settext!
export PathOptions, PathInfo, PathSwitch, client_connect_paths, client_leading_path,
       client_path_info, client_set_path_callback, client_disturb_path, text

const LIB = let packaged = normpath(joinpath(@__DIR__, "..", "lib", "libiec104.so.1"))
    get(ENV, "IEC104_LIB", isfile(packaged) ? packaged : "libiec104.so.1")
end

# Values of the C enums used here (C enums are Cint).
const OK = Cint(0)
const LINK_CONNECTING = Cint(0)
const LINK_STOPPED = Cint(1)
const LINK_STARTED = Cint(3)
const LINK_CLOSED = Cint(5)
const COT_SPONTANEOUS = 0x03
const COT_ACTIVATION_CON = 0x07
const COT_ACTIVATION_TERMINATION = 0x0a
const COT_INTERROGATED_STATION = 0x14
const DEFAULT_PORT = 2404
const PATH_CLOSE = Cint(0)     # iec104_path_fault
const PATH_SILENT = Cint(1)
const PATH_RESTORE = Cint(2)

# --- Types: same field order and widths as in iec104.h -------------------

"Timers, windows and ASDU profile (iec104_link_config); mutable, so fields can be changed."
mutable struct LinkConfig
    t0_ms::UInt32
    t1_ms::UInt32
    t2_ms::UInt32
    t3_ms::UInt32
    k::UInt16
    w::UInt16
    tolerant::Cint
    cot_length::UInt8
    ca_length::UInt8
    ioa_length::UInt8
    max_asdu_length::UInt8
    originator::UInt8
    LinkConfig() = (c = new(); ccall((:iec104_link_config_init, LIB), Cvoid, (Ref{LinkConfig},), c); c)
end

"Time tag CP56Time2a (iec104_time)."
struct IecTime
    year::UInt16
    month::UInt8
    day::UInt8
    hour::UInt8
    minute::UInt8
    second::UInt8
    millisecond::UInt16
    day_of_week::UInt8
    invalid::UInt8
    summer_time::UInt8
end
IecTime() = IecTime(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)

"One information object (iec104_object)."
struct Object
    type::UInt8
    ioa::UInt32
    value::Float64
    bits::UInt32
    quality::UInt8
    flags::UInt8
    qualifier::UInt8
    select::UInt8
    time::IecTime
end
"Object of type @p type at @p ioa with @p value, everything else 0."
Object(type::Integer, ioa::Integer, value::Real) =
    Object(UInt8(type), UInt32(ioa), Float64(value), 0, 0, 0, 0, 0, IecTime())

"Data unit identifier of a received ASDU (iec104_asdu_info)."
struct AsduInfo
    type::UInt8
    cause::UInt8
    negative::UInt8
    test::UInt8
    originator::UInt8
    common_address::UInt16
end

"Callbacks (iec104_callbacks): C function pointers from @cfunction, C_NULL = not used."
mutable struct Callbacks
    user::Ptr{Cvoid}
    on_asdu::Ptr{Cvoid}
    on_state::Ptr{Cvoid}
    on_object::Ptr{Cvoid}
    on_command::Ptr{Cvoid}
    on_command_result::Ptr{Cvoid}
    on_security::Ptr{Cvoid}
end
"Callbacks with @p user (any mutable Julia object, kept alive by the caller) and the given pointers."
Callbacks(user; on_asdu = C_NULL, on_state = C_NULL, on_object = C_NULL, on_command = C_NULL,
          on_command_result = C_NULL, on_security = C_NULL) =
    Callbacks(pointer_from_objref(user), on_asdu, on_state, on_object, on_command,
              on_command_result, on_security)

"TLS settings (iec104_tls_config); text fields as fixed byte arrays, set them with settext!."
mutable struct TlsConfig
    ca_file::NTuple{512,UInt8}
    certificate_file::NTuple{512,UInt8}
    private_key_file::NTuple{512,UInt8}
    key_password::NTuple{128,UInt8}
    crl_file::NTuple{512,UInt8}
    peer_name::NTuple{256,UInt8}
    peer_fingerprint::NTuple{128,UInt8}
    cipher_list::NTuple{512,UInt8}
    cipher_suites::NTuple{256,UInt8}
    min_version::UInt16
    max_version::UInt16
    verify_peer::Cint
    renegotiation::Cint
    renewal_interval_s::UInt32
    session_resumption::Cint
    session_lifetime_s::UInt32
    TlsConfig() = (c = new(); ccall((:iec104_tls_config_init, LIB), Cvoid, (Ref{TlsConfig},), c); c)
end

"Sets a text field of a TlsConfig, e.g. settext!(tls, :ca_file, \"/etc/pki/ca.pem\")."
function settext!(c::TlsConfig, field::Symbol, text::AbstractString)
    N = length(getfield(c, field))
    bytes = codeunits(text)
    length(bytes) < N || throw(ArgumentError("$field: text too long"))
    setfield!(c, field, ntuple(i -> i <= length(bytes) ? bytes[i] : 0x00, N))
    return c
end

"Address of one path (iec104_path_config)."
struct PathConfig
    host::NTuple{256,UInt8}
    port::UInt16
    tls_peer_name::NTuple{256,UInt8}
    tls_peer_fingerprint::NTuple{128,UInt8}
end
fixed(text::AbstractString, N) = (b = codeunits(text);
    length(b) < N || throw(ArgumentError("text too long: $text"));
    ntuple(i -> i <= length(b) ? b[i] : 0x00, N))
PathConfig(host::AbstractString, port::Integer; peer_name = "", peer_fingerprint = "") =
    PathConfig(fixed(host, 256), UInt16(port), fixed(peer_name, 256), fixed(peer_fingerprint, 128))

"Behaviour of a client with paths (iec104_path_options); mutable, so fields can be changed."
mutable struct PathOptions
    reconnect_interval_ms::UInt32
    replay_window_ms::UInt32        # 0 = t1 + t3 + 5 s
    replay_max_objects::UInt32
    replay_tolerance_ms::UInt32
    interrogation_after_switch::Cint
    prefer_first_path::Cint
    PathOptions() = (o = new(); ccall((:iec104_path_options_init, LIB), Cvoid, (Ref{PathOptions},), o); o)
end

"Counters of a connection (iec104_link_stats)."
struct LinkStats
    i_frames_sent::UInt64
    i_frames_received::UInt64
    s_frames_sent::UInt64
    s_frames_received::UInt64
    u_frames_sent::UInt64
    u_frames_received::UInt64
    bytes_sent::UInt64
    bytes_received::UInt64
    requests::UInt64
    commands_accepted::UInt64
    commands_rejected::UInt64
    commands_discarded::UInt64
    negative_answers::UInt64
    send_queue::UInt32
end

"State and counters of one path (iec104_path_info)."
struct PathInfo
    host::NTuple{256,UInt8}
    port::UInt16
    state::Cint
    last_close::Cint
    leading::Cint
    silent::Cint
    connects::UInt64
    became_leading::UInt64
    followed::UInt64
    replayed::UInt64
    suppressed::UInt64
    dropped::UInt64
    buffered::UInt32
    stats::LinkStats
end

"A switch-over, as passed to the path callback (iec104_path_switch)."
struct PathSwitch
    leading::UInt32     # 0 = no path started any more
    previous::UInt32
    replayed::UInt32
    suppressed::UInt32
    dropped::UInt32
    reason::Cstring     # valid only during the callback
end

"Text of a fixed C string field, e.g. text(info.host)."
text(t::NTuple{N,UInt8}) where {N} = String(UInt8[b for b in Iterators.takewhile(!=(0x00), t)])

# Sizes must match the C header (checked once when the module is loaded).
@assert sizeof(LinkConfig) == 32 && sizeof(IecTime) == 14 && sizeof(Object) == 40 &&
        sizeof(AsduInfo) == 8 && sizeof(Callbacks) == 56 && sizeof(TlsConfig) == 3352 &&
        sizeof(PathConfig) == 642 && sizeof(PathOptions) == 24 && sizeof(LinkStats) == 112 &&
        sizeof(PathInfo) == 448 && sizeof(PathSwitch) == 32 && fieldoffset(PathInfo, 7) == 280 &&
        fieldoffset(PathInfo, 14) == 336

# --- Status ----------------------------------------------------------------

"Error of a library function; `status` is the iec104_status, `msg` its text."
struct Iec104Error <: Exception
    status::Cint
    msg::String
end
Base.showerror(io::IO, e::Iec104Error) = print(io, "iec104: ", e.msg, " (", e.status, ")")

status_string(st::Integer) = unsafe_string(ccall((:iec104_status_string, LIB), Cstring, (Cint,), st))

"Throws Iec104Error unless @p st is IEC104_OK."
check(st::Integer) = st == OK ? nothing : throw(Iec104Error(st, status_string(st)))

version() = unsafe_string(ccall((:iec104_version_string, LIB), Cstring, ()))

"Texts of the C enums (iec104_*_string)."
state_string(x::Integer) = unsafe_string(ccall((:iec104_link_state_string, LIB), Cstring, (Cint,), x))
close_reason_string(x::Integer) =
    unsafe_string(ccall((:iec104_close_reason_string, LIB), Cstring, (Cint,), x))
command_result_string(x::Integer) =
    unsafe_string(ccall((:iec104_command_result_string, LIB), Cstring, (Cint,), x))
security_event_string(x::Integer) =
    unsafe_string(ccall((:iec104_security_event_string, LIB), Cstring, (Cint,), x))

"Current time as CP56Time2a (UTC)."
function time_now()
    t = Ref{IecTime}()
    check(ccall((:iec104_time_from_unix_ms, LIB), Cint, (Int64, Ref{IecTime}),
                round(Int64, time() * 1000), t))
    return t[]
end
tls_available() = ccall((:iec104_tls_available, LIB), Cint, ()) != 0

# --- Server (controlled station) ----------------------------------------------

function server_create(config::LinkConfig, callbacks::Callbacks)
    out = Ref{Ptr{Cvoid}}(C_NULL)
    check(ccall((:iec104_server_create, LIB), Cint,
                (Ref{LinkConfig}, Ref{Callbacks}, Ref{Ptr{Cvoid}}), config, callbacks, out))
    return out[]
end
server_listen(s, port::Integer; address = C_NULL) =
    check(ccall((:iec104_server_listen, LIB), Cint, (Ptr{Cvoid}, Cstring, UInt16), s, address, port))
server_port(s) = Int(ccall((:iec104_server_port, LIB), UInt16, (Ptr{Cvoid},), s))
server_add_point(s, station::Integer, o::Object, groups::Integer = 1) =
    check(ccall((:iec104_server_add_point, LIB), Cint, (Ptr{Cvoid}, UInt16, Ref{Object}, UInt16),
                s, station, o, groups))
"Changes a value and sends it spontaneously (buffered per redundancy group)."
server_update(s, station::Integer, o::Object) =
    ccall((:iec104_server_update, LIB), Cint, (Ptr{Cvoid}, UInt16, Ref{Object}), s, station, o)
"Receive, timers, answers, callbacks; waits at most @p timeout_ms."
server_poll(s, timeout_ms::Integer = 100) =
    ccall((:iec104_server_poll, LIB), Cint, (Ptr{Cvoid}, Cint), s, timeout_ms)
server_destroy(s) = ccall((:iec104_server_destroy, LIB), Cvoid, (Ptr{Cvoid},), s)
server_connection_count(s) = Int(ccall((:iec104_server_connection_count, LIB), Csize_t, (Ptr{Cvoid},), s))
"Log callback: @p fn from @cfunction with (Ptr{Cvoid}, Cint, UInt32, Cstring) -> Cvoid; level 0..4."
server_set_log(s, level::Integer, fn::Ptr{Cvoid}, user::Ptr{Cvoid}) =
    ccall((:iec104_server_set_log, LIB), Cvoid, (Ptr{Cvoid}, Cint, Ptr{Cvoid}, Ptr{Cvoid}),
          s, level, fn, user)

# --- Client (controlling station) -----------------------------------------------

function client_create(config::LinkConfig, callbacks::Callbacks)
    out = Ref{Ptr{Cvoid}}(C_NULL)
    check(ccall((:iec104_client_create, LIB), Cint,
                (Ref{LinkConfig}, Ref{Callbacks}, Ref{Ptr{Cvoid}}), config, callbacks, out))
    return out[]
end
client_connect(c, host::AbstractString, port::Integer) =
    check(ccall((:iec104_client_connect, LIB), Cint, (Ptr{Cvoid}, Cstring, UInt16), c, host, port))
client_start(c) = check(ccall((:iec104_client_start, LIB), Cint, (Ptr{Cvoid},), c))
client_stop(c) = check(ccall((:iec104_client_stop, LIB), Cint, (Ptr{Cvoid},), c))
client_poll(c, timeout_ms::Integer = 100) =
    ccall((:iec104_client_poll, LIB), Cint, (Ptr{Cvoid}, Cint), c, timeout_ms)
"General interrogation of @p station; @p qoi 20 = all points, 21..36 = group 1..16."
client_interrogation(c, station::Integer, qoi::Integer = 20) =
    check(ccall((:iec104_client_interrogation, LIB), Cint, (Ptr{Cvoid}, UInt16, UInt8),
                c, station, qoi))
client_destroy(c) = ccall((:iec104_client_destroy, LIB), Cvoid, (Ptr{Cvoid},), c)
client_close(c) = ccall((:iec104_client_close, LIB), Cvoid, (Ptr{Cvoid},), c)
client_state(c) = ccall((:iec104_client_state, LIB), Cint, (Ptr{Cvoid},), c)
"Counter interrogation: @p rqt 5 = all counters, @p frz 0 = read."
client_counter_interrogation(c, station::Integer, rqt::Integer = 5, frz::Integer = 0) =
    check(ccall((:iec104_client_counter_interrogation, LIB), Cint, (Ptr{Cvoid}, UInt16, UInt8, UInt8),
                c, station, rqt, frz))
"Clock synchronization with the current UTC time."
client_clock_sync(c, station::Integer) =
    check(ccall((:iec104_client_clock_sync, LIB), Cint, (Ptr{Cvoid}, UInt16, Ref{IecTime}),
                c, station, time_now()))
"Command (types 45..51, 58..64); the result arrives via on_command_result."
client_send_command(c, station::Integer, o::Object) =
    check(ccall((:iec104_client_send_command, LIB), Cint, (Ptr{Cvoid}, UInt16, Ref{Object}),
                c, station, o))
"Read command (102, cause 5): the station answers with the object via on_object."
client_read(c, station::Integer, ioa::Integer) =
    check(ccall((:iec104_client_read, LIB), Cint, (Ptr{Cvoid}, UInt16, UInt32), c, station, ioa))
"Select, then execute (select before operate)."
client_select_execute(c, station::Integer, o::Object) =
    check(ccall((:iec104_client_select_execute, LIB), Cint, (Ptr{Cvoid}, UInt16, Ref{Object}),
                c, station, o))
"TLS for the next connect; nothing = plain TCP."
client_set_tls(c, tls::Union{TlsConfig,Nothing}) =
    check(ccall((:iec104_client_set_tls, LIB), Cint, (Ptr{Cvoid}, Ptr{Cvoid}), c,
                tls === nothing ? C_NULL : pointer_from_objref(tls)))
"Time limits for commands (iec104_client_options); @p expect_termination false for peers without ACTTERM."
function client_set_options(c; confirmation_ms::Integer = 10000, termination_ms::Integer = 10000,
                            expect_termination::Bool = true)
    o = (UInt32(confirmation_ms), UInt32(termination_ms), Cint(expect_termination))
    check(ccall((:iec104_client_set_options, LIB), Cint, (Ptr{Cvoid}, Ref{NTuple{3,UInt32}}), c,
                (o[1], o[2], reinterpret(UInt32, o[3]))))
end
"Log callback, see server_set_log."
client_set_log(c, level::Integer, fn::Ptr{Cvoid}, user::Ptr{Cvoid}) =
    ccall((:iec104_client_set_log, LIB), Cvoid, (Ptr{Cvoid}, Cint, Ptr{Cvoid}, Ptr{Cvoid}),
          c, level, fn, user)

# --- Client with several paths (path redundancy, docs/06) ---------------------------

"""
    client_connect_paths(c, [("10.0.0.1", 2404), ("10.0.0.2", 2404)]; options = PathOptions())

Connects all paths; afterwards client_start(c) once. The application sees
one logical connection (id 1); only the leading path is delivered.
"""
function client_connect_paths(c, paths; options::PathOptions = PathOptions())
    cfg = [p isa PathConfig ? p : PathConfig(p[1], p[2]) for p in paths]
    check(ccall((:iec104_client_connect_paths, LIB), Cint,
                (Ptr{Cvoid}, Ptr{PathConfig}, Csize_t, Ref{PathOptions}), c, cfg, length(cfg), options))
end
"Leading path 1..n, 0 = none."
client_leading_path(c) = Int(ccall((:iec104_client_leading_path, LIB), UInt32, (Ptr{Cvoid},), c))
"State and counters of path @p nr (1..n)."
function client_path_info(c, nr::Integer)
    out = Ref{PathInfo}()
    check(ccall((:iec104_client_get_path_info, LIB), Cint, (Ptr{Cvoid}, UInt32, Ref{PathInfo}),
                c, nr, out))
    return out[]
end
"Path callback: @p fn from @cfunction with (Ptr{Cvoid}, Ptr{PathSwitch}) -> Cvoid."
client_set_path_callback(c, fn::Ptr{Cvoid}, user::Ptr{Cvoid}) =
    ccall((:iec104_client_set_path_callback, LIB), Cvoid, (Ptr{Cvoid}, Ptr{Cvoid}, Ptr{Cvoid}),
          c, fn, user)
"Test aid: :close, :silent or :restore path @p nr."
client_disturb_path(c, nr::Integer, fault::Symbol) =
    check(ccall((:iec104_client_disturb_path, LIB), Cint, (Ptr{Cvoid}, UInt32, Cint), c, nr,
                Dict(:close => PATH_CLOSE, :silent => PATH_SILENT, :restore => PATH_RESTORE)[fault]))

# --- Decoding (e.g. frames of a capture) -------------------------------------------

"Name of a type as in the standard, e.g. \"M_SP_NA_1\"; \"unknown type\" if not in the table."
type_name(t::Integer) = unsafe_string(ccall((:iec104_type_name, LIB), Cstring, (UInt8,), t))

"""
    decode_asdu(asdu) -> (status, info, objects)

Decodes an ASDU (the octets behind the 6 octets of the APCI) with the profile of
the standard. status 0 = ok; -6 (unsupported) still gives info. objects is
empty unless status is 0.
"""
function decode_asdu(asdu::AbstractVector{UInt8})
    info = Ref{AsduInfo}()
    objects = Vector{Object}(undef, 127)
    count = Ref{Csize_t}(0)
    data = Vector{UInt8}(asdu)
    st = ccall((:iec104_decode_asdu, LIB), Cint,
               (Ptr{Cvoid}, Ptr{UInt8}, Csize_t, Ref{AsduInfo}, Ptr{Object}, Csize_t, Ref{Csize_t}),
               C_NULL, data, length(data), info, objects, length(objects), count)
    return st, (st == 0 || st == -6) ? info[] : nothing, st == 0 ? objects[1:count[]] : Object[]
end

end # module
