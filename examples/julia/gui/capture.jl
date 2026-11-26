# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# capture.jl - IEC 104 frames from a capture file (listening in): pcap and pcapng
# as written by tcpdump and Wireshark, also while tcpdump is still writing.
#
# Pure Julia on bytes, no capture rights needed: the capture itself is made by
# tcpdump (with the rights of the user who starts it), this code only reads the
# file. Link layers: Ethernet (also VLAN), Linux cooked v1/v2 ("-i any"),
# loopback/null, raw IP; IPv4 and IPv6; TCP reassembled per direction
# (retransmissions, reordering, gaps), then cut into APDUs (start octet 0x68,
# length octet). File formats: pcap (libpcap) and pcapng (blocks SHB, IDB, EPB,
# SPB), little and big endian, micro- and nanoseconds.
#
# Usage: r = Capture.Reader(ports = [2404]); Capture.feed!(r, bytes);
#        for e in Capture.events!(r) ... end   (e.kind :frame or :note)

module Capture

export Reader, feed!, events!

"One direction of a TCP connection."
mutable struct Stream
    next::Union{Nothing,UInt32}            # next expected sequence number
    buf::Vector{UInt8}                     # bytes not yet cut into APDUs
    pending::Dict{UInt32,Vector{UInt8}}    # segments that came before a gap closed
    synced::Bool                           # buf starts at an APDU boundary
end
Stream() = Stream(nothing, UInt8[], Dict(), false)

"Something found in the capture: a frame (APDU) or a note (gap, TLS, unknown link layer)."
struct Event
    kind::Symbol            # :frame or :note
    time::Float64           # capture time, seconds since 1970
    src::String             # "10.0.0.5:2404"
    dst::String
    from_station::Bool      # sent by the side with the IEC 104 port
    apdu::Vector{UInt8}
    text::String            # notes
end

mutable struct Reader
    ports::Vector{Int}                      # IEC 104 ports (station side), default 2404
    tls_ports::Vector{Int}                  # encrypted: only noted
    data::Vector{UInt8}                     # unread file bytes
    format::Symbol                          # :unknown, :pcap, :pcapng, :bad
    little::Bool
    nano::Bool                              # pcap: nanosecond timestamps
    linktype::Int                           # pcap
    if_link::Vector{Int}                    # pcapng: link type per interface
    if_res::Vector{Float64}                 # pcapng: seconds per timestamp unit
    streams::Dict{NTuple{4,String},Stream}
    tls_noted::Set{String}
    events::Vector{Event}
    packets::Int
    frames::Int
    gaps::Int
    skipped::Int                            # packets that are not IEC 104 (other ports, not TCP)
end
Reader(; ports = [2404], tls_ports = [19998]) =
    Reader(collect(ports), collect(tls_ports), UInt8[], :unknown, true, false, 0, Int[], Float64[],
           Dict(), Set{String}(), Event[], 0, 0, 0, 0)

# --- Integers -------------------------------------------------------------------------

u16(b, i, little = false) = little ? UInt16(b[i]) | UInt16(b[i+1]) << 8 : UInt16(b[i]) << 8 | UInt16(b[i+1])
u32(b, i, little = false) = little ?
    UInt32(b[i]) | UInt32(b[i+1]) << 8 | UInt32(b[i+2]) << 16 | UInt32(b[i+3]) << 24 :
    UInt32(b[i]) << 24 | UInt32(b[i+1]) << 16 | UInt32(b[i+2]) << 8 | UInt32(b[i+3])

note!(r::Reader, t, text) = push!(r.events, Event(:note, t, "", "", false, UInt8[], text))

# --- File formats ------------------------------------------------------------------

"Appends bytes of the capture file and decodes everything that is complete."
function feed!(r::Reader, bytes::AbstractVector{UInt8})
    append!(r.data, bytes)
    r.format == :unknown && detect!(r)
    r.format == :pcap && pcap_records!(r)
    r.format == :pcapng && pcapng_blocks!(r)
    return r
end

"The events found since the last call."
events!(r::Reader) = (e = r.events; r.events = Event[]; e)

function detect!(r::Reader)
    length(r.data) >= 4 || return
    m = r.data[1:4]
    if m == [0xd4, 0xc3, 0xb2, 0xa1] || m == [0x4d, 0x3c, 0xb2, 0xa1]
        r.little, r.nano = true, m[1] == 0x4d
    elseif m == [0xa1, 0xb2, 0xc3, 0xd4] || m == [0xa1, 0xb2, 0x3c, 0x4d]
        r.little, r.nano = false, m[4] == 0x4d
    elseif m == [0x0a, 0x0d, 0x0d, 0x0a]
        r.format = :pcapng
        return
    else
        r.format = :bad
        note!(r, time(), "keine pcap- oder pcapng-Datei (Anfang " *
                         join(string.(m; base = 16, pad = 2), ' ') * ")")
        return
    end
    length(r.data) >= 24 || return
    r.linktype = Int(u32(r.data, 21, r.little) & 0x0fffffff)
    deleteat!(r.data, 1:24)
    r.format = :pcap
end

function pcap_records!(r::Reader)
    while length(r.data) >= 16
        d = r.data
        incl = Int(u32(d, 9, r.little))
        incl > 262144 && (r.format = :bad; note!(r, time(), "pcap-Datei beschädigt (Satzlänge $incl)"); return)
        length(d) >= 16 + incl || return
        t = u32(d, 1, r.little) + u32(d, 5, r.little) * (r.nano ? 1e-9 : 1e-6)
        packet!(r, Float64(t), r.linktype, view(d, 17:16+incl))
        deleteat!(r.data, 1:16+incl)
    end
end

function pcapng_blocks!(r::Reader)
    while length(r.data) >= 12
        d = r.data
        if u32(d, 1) == 0x0a0d0d0a   # section header: byte order from its magic
            r.little = u32(d, 9, true) == 0x1a2b3c4d
            empty!(r.if_link); empty!(r.if_res)
        end
        type = u32(d, 1, r.little)
        len = Int(u32(d, 5, r.little))
        (len < 12 || len % 4 != 0 || len > 16_777_216) &&
            (r.format = :bad; note!(r, time(), "pcapng-Datei beschädigt (Blocklänge $len)"); return)
        length(d) >= len || return
        if type == 1                          # interface description
            push!(r.if_link, Int(u16(d, 9, r.little)))
            push!(r.if_res, if_tsresol(d, len, r.little))
        elseif type == 6 && len >= 32         # enhanced packet
            ifc = Int(u32(d, 9, r.little)) + 1
            ts = UInt64(u32(d, 13, r.little)) << 32 | u32(d, 17, r.little)
            cap = Int(u32(d, 21, r.little))
            if 1 <= ifc <= length(r.if_link) && 28 + cap <= len
                packet!(r, ts * r.if_res[ifc], r.if_link[ifc], view(d, 29:28+cap))
            end
        elseif type == 3 && len >= 16 && !isempty(r.if_link)   # simple packet: no time
            cap = min(Int(u32(d, 9, r.little)), len - 16)
            packet!(r, time(), r.if_link[1], view(d, 13:12+cap))
        end
        deleteat!(r.data, 1:len)
    end
end

"Seconds per timestamp unit from the option if_tsresol (9), default microseconds."
function if_tsresol(d, len, little)
    i = 17                                  # options after link type, reserved, snaplen
    while i + 3 <= len - 4
        code, olen = u16(d, i, little), Int(u16(d, i + 2, little))
        code == 0 && break
        if code == 9 && olen >= 1
            v = d[i+4]
            return v & 0x80 == 0 ? 10.0^(-Int(v)) : 2.0^(-Int(v & 0x7f))
        end
        i += 4 + ((olen + 3) & ~3)
    end
    return 1e-6
end

# --- Link, network, transport -------------------------------------------------------

function packet!(r::Reader, t::Float64, link::Int, p::AbstractVector{UInt8})
    r.packets += 1
    ip = link_payload(link, p)
    ip === nothing && (r.skipped += 1; return)
    tcp!(r, t, ip)
end

"IP packet inside the link layer frame, nothing if not IP or not supported."
function link_payload(link::Int, p)
    if link == 1                                         # Ethernet
        length(p) >= 14 || return nothing
        et, i = u16(p, 13), 15
        while et in (0x8100, 0x88a8) && length(p) >= i + 3   # VLAN tags
            et, i = u16(p, i + 2), i + 4
        end
        return et in (0x0800, 0x86dd) ? view(p, i:length(p)) : nothing
    elseif link == 113                                   # Linux cooked v1
        length(p) >= 16 && u16(p, 15) in (0x0800, 0x86dd) || return nothing
        return view(p, 17:length(p))
    elseif link == 276                                   # Linux cooked v2
        length(p) >= 20 && u16(p, 1) in (0x0800, 0x86dd) || return nothing
        return view(p, 21:length(p))
    elseif link in (0, 108)                              # BSD null / OpenBSD loopback
        length(p) >= 4 || return nothing
        return view(p, 5:length(p))
    elseif link in (101, 12, 14, 228, 229)               # raw IP
        return p
    end
    return nothing
end

ipv4_text(p, i) = join(Int.(p[i:i+3]), '.')
ipv6_text(p, i) = "[" * join((string(u16(p, i + 2k); base = 16) for k in 0:7), ':') * "]"

function tcp!(r::Reader, t::Float64, p)
    isempty(p) && return
    v = p[1] >> 4
    if v == 4
        length(p) >= 20 || return
        ihl = Int(p[1] & 0x0f) * 4
        total = min(Int(u16(p, 3)), length(p))
        (p[10] == 6 && u16(p, 7) & 0x3fff == 0 && total >= ihl + 20) || (r.skipped += 1; return)  # TCP, no fragment
        src, dst, seg = ipv4_text(p, 13), ipv4_text(p, 17), view(p, ihl+1:total)
    elseif v == 6
        length(p) >= 40 && p[7] == 6 || (r.skipped += 1; return)
        total = min(40 + Int(u16(p, 5)), length(p))
        src, dst, seg = ipv6_text(p, 9), ipv6_text(p, 25), view(p, 41:total)
    else
        r.skipped += 1
        return
    end
    length(seg) >= 20 || return
    sp, dp = Int(u16(seg, 1)), Int(u16(seg, 3))
    seq = u32(seg, 5)
    flags = seg[14]
    off = Int(seg[13] >> 4) * 4
    data = view(seg, off+1:length(seg))
    a, b = "$src:$sp", "$dst:$dp"
    if sp in r.tls_ports || dp in r.tls_ports
        key = sp in r.tls_ports ? "$a-$b" : "$b-$a"
        if !(key in r.tls_noted)
            push!(r.tls_noted, key)
            note!(r, t, "TLS-Verbindung $a ↔ $b: Inhalt verschlüsselt, nicht lesbar")
        end
        return
    end
    (sp in r.ports || dp in r.ports) || (r.skipped += 1; return)
    s = get!(Stream, r.streams, (src, string(sp), dst, string(dp)))
    if flags & 0x02 != 0                                   # SYN: stream starts with seq + 1
        s.next, s.synced = seq + one(UInt32), true
        empty!(s.buf); empty!(s.pending)
    end
    if flags & 0x05 != 0 && isempty(data)                  # FIN or RST without data
        return
    end
    isempty(data) && return
    segment!(r, t, s, a, b, sp in r.ports, seq, data)
end

# Stream position arithmetic modulo 2^32.
before(x::UInt32, y::UInt32) = reinterpret(Int32, x - y) < 0

function segment!(r::Reader, t, s::Stream, a, b, from_station, seq::UInt32, data)
    if s.next === nothing          # capture started inside the connection
        s.next = seq
    end
    if before(seq, s.next)         # retransmission, maybe with new bytes at the end
        skip = Int(s.next - seq)
        skip >= length(data) && return
        data, seq = view(data, skip+1:length(data)), s.next
    end
    if seq != s.next               # gap: keep until it closes
        s.pending[seq] = Vector{UInt8}(data)
        if length(s.pending) > 64  # gap stays open: missing bytes are lost, go on after it
            r.gaps += 1
            note!(r, t, "Lücke im Mitschnitt $a → $b: Daten fehlen, weiter beim nächsten Rahmen")
            first = minimum(keys(s.pending); by = k -> Int64(reinterpret(Int32, k - s.next::UInt32)))
            s.next, s.synced = first, false
            empty!(s.buf)
        else
            return
        end
    else
        append!(s.buf, data)
        s.next = s.next::UInt32 + UInt32(length(data))
    end
    while (d = get(s.pending, s.next, nothing)) !== nothing   # segments that now fit
        delete!(s.pending, s.next)
        append!(s.buf, d)
        s.next = s.next::UInt32 + UInt32(length(d))
    end
    cut!(r, t, s, a, b, from_station)
end

"Cuts the stream into APDUs; without a known boundary it looks for a plausible start."
function cut!(r::Reader, t, s::Stream, a, b, from_station)
    while length(s.buf) >= 2
        if !s.synced || s.buf[1] != 0x68
            i = findnext(k -> plausible(s.buf, k), 1:length(s.buf), 1)
            i === nothing && (length(s.buf) > 300 && (empty!(s.buf)); return)
            i > 1 && s.synced && note!(r, t, "$a → $b: $(i - 1) Byte ohne Rahmenanfang übersprungen")
            deleteat!(s.buf, 1:i-1)
            s.synced = true
        end
        n = Int(s.buf[2]) + 2
        length(s.buf) >= n || return
        push!(r.events, Event(:frame, t, a, b, from_station, s.buf[1:n], ""))
        r.frames += 1
        deleteat!(s.buf, 1:n)
    end
end

"An APDU can start at k: 0x68, length 4..253, and for length 4 a valid S or U control field."
function plausible(b, k)
    k + 1 <= length(b) && b[k] == 0x68 || return false
    n = Int(b[k+1])
    4 <= n <= 253 || return false
    k + 2 <= length(b) || return true
    c = b[k+2]
    n == 4 && return c & 0x03 == 0x01 || c in (0x07, 0x0b, 0x13, 0x23, 0x43, 0x83)
    return c & 0x01 == 0x00 && n >= 10        # I-frame with at least an ASDU header
end

end # module Capture
