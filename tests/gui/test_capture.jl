# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# test_capture.jl - unit test of examples/julia/gui/capture.jl (listening in on a
# capture file). The captures are built here byte by byte (pcap and pcapng
# layout as documented by libpcap and the pcapng specification); the frames are
# taken from tests/data/lib60870_capture.txt (lib60870-C).
#
#   julia tests/gui/test_capture.jl                 run the tests
#   julia tests/gui/test_capture.jl --sample FILE   write a sample capture (pcap) for the GUI test

using Test
include(joinpath(@__DIR__, "..", "..", "examples", "julia", "gui", "capture.jl"))
using .Capture

hexb(s) = UInt8[parse(UInt8, h; base = 16) for h in split(s)]
le16(x) = reinterpret(UInt8, [htol(UInt16(x))])
le32(x) = reinterpret(UInt8, [htol(UInt32(x))])
be16(x) = reinterpret(UInt8, [hton(UInt16(x))])
be32(x) = reinterpret(UInt8, [hton(UInt32(x))])

# Frames of a short session (capture of lib60870): STARTDT act/con, interrogation,
# its confirmation, three scaled values, S-frame.
const STARTDT_ACT = hexb("68 04 07 00 00 00")
const STARTDT_CON = hexb("68 04 0b 00 00 00")
const GI_ACT = hexb("68 0e 00 00 04 00 64 01 06 03 01 00 00 00 00 14")
const GI_CON = hexb("68 0e 04 00 02 00 64 01 07 03 01 00 00 00 00 14")
const VALUES = hexb("68 1c 06 00 02 00 0b 03 14 03 01 00 64 00 00 ff ff 00 65 00 00 17 00 00 66 00 00 fc 08 00")
const S_FRAME = hexb("68 04 01 00 14 00")

"TCP segment in IPv4 (no checksums: the reader does not check them)."
function ipv4_tcp(src, dst, sp, dp, seq, data; flags = 0x18)
    tcp = vcat(be16(sp), be16(dp), be32(seq), be32(0), UInt8[0x50, flags], be16(65535), be16(0), be16(0), data)
    ip = vcat(UInt8[0x45, 0], be16(20 + length(tcp)), be16(0), be16(0x4000), UInt8[64, 6], be16(0), src, dst)
    return vcat(ip, tcp)
end
function ipv6_tcp(src, dst, sp, dp, seq, data; flags = 0x18)
    tcp = vcat(be16(sp), be16(dp), be32(seq), be32(0), UInt8[0x50, flags], be16(65535), be16(0), be16(0), data)
    return vcat(UInt8[0x60, 0, 0, 0], be16(length(tcp)), UInt8[6, 64], src, dst, tcp)
end
ether(ip; vlan = false) = vcat(zeros(UInt8, 12), vlan ? vcat(be16(0x8100), be16(7)) : UInt8[],
                               be16(ip[1] >> 4 == 6 ? 0x86dd : 0x0800), ip)
sll2(ip) = vcat(be16(0x0800), zeros(UInt8, 18), ip)

pcap(packets; link = 1) = vcat(le32(0xa1b2c3d4), le16(2), le16(4), le32(0), le32(0), le32(65535), le32(link),
    vcat((vcat(le32(1795600000 + k), le32(250000), le32(length(p)), le32(length(p)), p)
          for (k, p) in enumerate(packets))...))

function pcapng(packets; link = 1, nano = false)
    shb = vcat(le32(0x0a0d0d0a), le32(28), le32(0x1a2b3c4d), le16(1), le16(0), le32(0xffffffff), le32(0xffffffff), le32(28))
    opt = nano ? vcat(le16(9), le16(1), UInt8[9, 0, 0, 0], le16(0), le16(0)) : UInt8[]
    idb_len = 20 + length(opt)
    idb = vcat(le32(1), le32(idb_len), le16(link), le16(0), le32(65535), opt, le32(idb_len))
    blocks = UInt8[]
    for (k, p) in enumerate(packets)
        pad = (4 - length(p) % 4) % 4
        len = 32 + length(p) + pad
        ts = UInt64(1795600000 + k) * (nano ? 1_000_000_000 : 1_000_000)
        append!(blocks, vcat(le32(6), le32(len), le32(0), le32(ts >> 32), le32(ts & 0xffffffff),
                             le32(length(p)), le32(length(p)), p, zeros(UInt8, pad), le32(len)))
    end
    return vcat(shb, idb, blocks)
end

const L = UInt8[10, 0, 0, 9]    # controlling station
const S = UInt8[10, 0, 0, 5]    # station, port 2404

"A session: SYN in both directions, frames, a frame split over two segments, a retransmission."
function session(; wrap = ether)
    p = Vector{UInt8}[]
    push!(p, wrap(ipv4_tcp(L, S, 51000, 2404, 1000, UInt8[]; flags = 0x02)))
    push!(p, wrap(ipv4_tcp(S, L, 2404, 51000, 5000, UInt8[]; flags = 0x12)))
    push!(p, wrap(ipv4_tcp(L, S, 51000, 2404, 1001, STARTDT_ACT)))
    push!(p, wrap(ipv4_tcp(S, L, 2404, 51000, 5001, STARTDT_CON)))
    push!(p, wrap(ipv4_tcp(L, S, 51000, 2404, 1007, GI_ACT)))
    push!(p, wrap(ipv4_tcp(L, S, 51000, 2404, 1007, GI_ACT)))                    # retransmission
    both = vcat(GI_CON, VALUES)
    push!(p, wrap(ipv4_tcp(S, L, 2404, 51000, 5007, both[1:20])))               # split frame
    push!(p, wrap(ipv4_tcp(S, L, 2404, 51000, 5027, both[21:end])))
    push!(p, wrap(ipv4_tcp(L, S, 51000, 2404, 1023, S_FRAME)))
    push!(p, wrap(ipv4_tcp(L, S, 51000, 2404, 1029, UInt8[]; flags = 0x11)))     # FIN
    return p
end

const EXPECTED = [(false, STARTDT_ACT), (true, STARTDT_CON), (false, GI_ACT), (true, GI_CON),
                  (true, VALUES), (false, S_FRAME)]

frames(r) = [(e.from_station, e.apdu) for e in events!(r) if e.kind == :frame]

if length(ARGS) == 2 && ARGS[1] == "--sample"
    write(ARGS[2], pcap(session()))
    exit(0)
end

@testset "Capture" begin
    r = Reader(); feed!(r, pcap(session()))
    @test frames(r) == EXPECTED
    @test r.packets == 10 && r.frames == 6 && r.gaps == 0

    # pcapng, also with nanosecond resolution; VLAN tag
    for nano in (false, true)
        r = Reader(); feed!(r, pcapng(session(; wrap = ip -> ether(ip; vlan = true)); nano))
        ev = events!(r)
        @test [(e.from_station, e.apdu) for e in ev if e.kind == :frame] == EXPECTED
        @test round(Int, ev[1].time) == 1795600003     # third packet, second 1795600000 + 3
        @test ev[1].src == "10.0.0.9:51000" && ev[1].dst == "10.0.0.5:2404"
    end

    # "tcpdump -i any": Linux cooked v2
    r = Reader(); feed!(r, pcap(session(; wrap = sll2); link = 276))
    @test frames(r) == EXPECTED

    # file still being written: bytes arrive in small pieces
    r = Reader()
    data = pcap(session())
    got = Tuple{Bool,Vector{UInt8}}[]
    for k in 1:7:length(data)
        feed!(r, data[k:min(k + 6, end)])
        append!(got, frames(r))
    end
    @test got == EXPECTED

    # segments out of order: the later one waits until the gap closes
    p = session()
    p[7], p[8] = p[8], p[7]
    r = Reader(); feed!(r, pcap(p))
    @test frames(r) == EXPECTED

    # capture starts inside the connection, in the middle of a frame: first whole frame is found
    mid = vcat(VALUES[10:end], S_FRAME, GI_ACT)
    r = Reader(); feed!(r, pcap([ether(ipv4_tcp(S, L, 2404, 51000, 777, mid))]))
    @test frames(r) == [(true, S_FRAME), (true, GI_ACT)]

    # IPv6
    a6 = vcat(UInt8[0xfd, 0], zeros(UInt8, 13), UInt8[9])
    s6 = vcat(UInt8[0xfd, 0], zeros(UInt8, 13), UInt8[5])
    r = Reader(); feed!(r, pcap([ether(ipv6_tcp(s6, a6, 2404, 51000, 1, VALUES))]))
    ev = events!(r)
    @test ev[1].apdu == VALUES && ev[1].src == "[fd00:0:0:0:0:0:0:5]:2404"

    # other ports are ignored, TLS port is noted once
    r = Reader()
    feed!(r, pcap([ether(ipv4_tcp(L, S, 51000, 80, 1, VALUES)),
                   ether(ipv4_tcp(L, S, 51000, 19998, 1, VALUES)), ether(ipv4_tcp(L, S, 51000, 19998, 9, VALUES))]))
    ev = events!(r)
    @test count(e -> e.kind == :frame, ev) == 0 && count(e -> e.kind == :note, ev) == 1
    @test occursin("verschlüsselt", ev[1].text) && r.skipped == 1

    # own port list
    r = Reader(ports = [2405]); feed!(r, pcap(session()))
    @test isempty(frames(r))

    # gaps: one segment lost, then more than 64 segments (each one frame); the gap is
    # reported once and every frame after it arrives
    p = [ether(ipv4_tcp(S, L, 2404, 51000, 5000, UInt8[]; flags = 0x02))]
    push!(p, ether(ipv4_tcp(S, L, 2404, 51000, 5001, S_FRAME)))
    # lost: 5007..5012 (one S-frame)
    for k in 0:69
        push!(p, ether(ipv4_tcp(S, L, 2404, 51000, UInt32(5013 + 6k), S_FRAME)))
    end
    r = Reader(); feed!(r, pcap(p))
    ev = events!(r)
    @test count(e -> e.kind == :frame, ev) == 71 && r.gaps == 1
    @test count(e -> e.kind == :note && occursin("Lücke", e.text), ev) == 1

    # gap near the end of the file: frames behind it come with flush! (end of file)
    p = [ether(ipv4_tcp(S, L, 2404, 51000, 5000, UInt8[]; flags = 0x02)),
         ether(ipv4_tcp(S, L, 2404, 51000, 5001, S_FRAME)),
         ether(ipv4_tcp(S, L, 2404, 51000, 5013, GI_CON)), ether(ipv4_tcp(S, L, 2404, 51000, 5029, VALUES))]
    r = Reader(); feed!(r, pcap(p))
    @test length(frames(r)) == 1
    flush!(r)
    @test frames(r) == [(true, GI_CON), (true, VALUES)] && r.gaps == 1

    # retransmission that overlaps partly with new bytes; later segment already waiting
    both = vcat(GI_CON, VALUES)
    p = [ether(ipv4_tcp(S, L, 2404, 51000, 5000, UInt8[]; flags = 0x02)),
         ether(ipv4_tcp(S, L, 2404, 51000, 5001, both[1:10])),
         ether(ipv4_tcp(S, L, 2404, 51000, 5021, both[21:end])),        # ahead of a gap
         ether(ipv4_tcp(S, L, 2404, 51000, 5006, both[6:25]))]          # overlaps both sides
    r = Reader(); feed!(r, pcap(p))
    @test frames(r) == [(true, GI_CON), (true, VALUES)] && isempty(r.streams[("10.0.0.5", "2404", "10.0.0.9", "51000")].pending)

    # sequence numbers wrap around 2^32
    p = [ether(ipv4_tcp(S, L, 2404, 51000, 0xfffffff0, UInt8[]; flags = 0x02)),
         ether(ipv4_tcp(S, L, 2404, 51000, 0xfffffff1, both[1:20])),
         ether(ipv4_tcp(S, L, 2404, 51000, 0x00000005, both[21:end]))]
    r = Reader(); feed!(r, pcap(p))
    @test frames(r) == [(true, GI_CON), (true, VALUES)]

    # FIN ends the direction: a new connection on the same ports without visible SYN is read
    p = [ether(ipv4_tcp(S, L, 2404, 51000, 100, S_FRAME)),
         ether(ipv4_tcp(S, L, 2404, 51000, 106, UInt8[]; flags = 0x11)),
         ether(ipv4_tcp(S, L, 2404, 51000, 900000, GI_CON))]
    r = Reader(); feed!(r, pcap(p))
    @test frames(r) == [(true, S_FRAME), (true, GI_CON)]

    # packets cut by the snap length: noted once, the next whole frame is found again
    cut = ether(ipv4_tcp(S, L, 2404, 51000, 5001, VALUES))[1:60]
    p = [ether(ipv4_tcp(S, L, 2404, 51000, 5000, UInt8[]; flags = 0x02)), cut,
         ether(ipv4_tcp(S, L, 2404, 51000, 5001 + length(VALUES), S_FRAME))]
    r = Reader(); feed!(r, pcap(p))
    ev = events!(r)
    @test [e.apdu for e in ev if e.kind == :frame] == [S_FRAME]
    @test count(e -> e.kind == :note && occursin("snaplen", e.text), ev) == 1

    # not a capture
    r = Reader(); feed!(r, Vector{UInt8}(codeunits("Hallo, keine Datei")))
    ev = events!(r)
    @test r.format == :bad && occursin("keine pcap", ev[1].text)
end
