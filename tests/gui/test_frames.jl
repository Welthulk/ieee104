# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# test_frames.jl - unit test of examples/julia/gui/frames.jl (description of
# frames for the commented protocol of the test GUI).
#
#   julia tests/gui/test_frames.jl
#
# Reference frames marked "capture" are taken verbatim from
# tests/data/lib60870_capture.txt (produced by lib60870-C, an independent
# implementation). Frames marked "built" were assembled by hand from the field
# layout in docs/02_Technik_und_Quellen.md.

using Test
include(joinpath(@__DIR__, "..", "..", "examples", "julia", "gui", "frames.jl"))
using .Frames

d(hex) = describe(parse_hex(hex))

@testset "Frames" begin
    # capture: SEND 68 04 07 00 00 00 / RCVD 68 04 0b 00 00 00
    @test d("68 04 07 00 00 00") == ("U STARTDT act", "Leitstelle bittet, die Datenübertragung zu starten")
    @test d("68 04 0b 00 00 00")[1] == "U STARTDT con"
    # built: STOPDT and TESTFR function octets (docs/02)
    @test d("68 04 13 00 00 00")[1] == "U STOPDT act"
    @test d("68 04 23 00 00 00")[1] == "U STOPDT con"
    @test d("68 04 43 00 00 00")[1] == "U TESTFR act"
    @test d("68 04 83 00 00 00")[1] == "U TESTFR con"

    # capture: SEND 68 04 01 00 14 00 (acknowledges up to N(R) = 10)
    @test d("68 04 01 00 14 00") == ("S N(R)=10", "Quittung: alle I-Rahmen bis Nr. 9 sind angekommen")

    # capture: general interrogation, originator 3, station 1
    s, m = d("68 0e 00 00 04 00 64 01 06 03 01 00 00 00 00 14")
    @test s == "I N(S)=0 N(R)=2 · 100 C_IC_NA_1 Generalabfrage · Ursache 6 Aktivierung · " *
               "Station 1 · IOA 0 QOI 20 (Station)"
    @test m == "Leitstelle fordert alle Werte an"
    s, m = d("68 0e 04 00 02 00 64 01 07 03 01 00 00 00 00 14")
    @test occursin("Ursache 7 Bestätigung", s) && m == "Station bestätigt die Generalabfrage"
    @test d("68 0e 0e 00 02 00 64 01 0a 03 01 00 00 00 00 14")[2] ==
          "Station meldet: alle Werte gesendet, die Generalabfrage ist beendet"

    # capture: three scaled values in answer to the interrogation (IOA 100: -1)
    s, m = d("68 1c 06 00 02 00 0b 03 14 03 01 00 64 00 00 ff ff 00 65 00 00 17 00 00 66 00 00 fc 08 00")
    @test s == "I N(S)=3 N(R)=1 · 11 M_ME_NB_1 Messwert skaliert · Ursache 20 Generalabfrage · " *
               "Station 1 · IOA 100 Wert -1 (+2 weitere)"
    @test m == "Antwort auf die Generalabfrage"

    # capture: sequence of 8 single points from IOA 300 (VSQ 0x88)
    s, _ = d("68 15 0a 00 02 00 01 88 14 03 01 00 2c 01 00 01 00 01 00 01 00 01 00")
    @test occursin("IOA 300 ein (+7 weitere)", s)

    # capture: cyclic scaled value IOA 110
    s, m = d("68 10 00 00 00 00 0b 01 01 00 01 00 6e 00 00 00 00 00")
    @test occursin("Ursache 1 zyklisch", s) && occursin("IOA 110 Wert 0", s) && m == "Zyklische Meldung"

    # capture: test command with time tag 2026-11-25 11:01:05.931 and its mirror
    s, m = d("68 16 02 00 14 00 6b 01 06 03 01 00 00 00 00 38 49 2b 17 01 0b 19 0b 1a")
    @test occursin("107 C_TS_TA_1", s) && occursin("Zähler 18744 Zeit 2026-11-25 11:01:05.931", s)
    @test m == "Testbefehl: prüft die Verbindung bis in die Anwendung"
    @test d("68 16 14 00 04 00 6b 01 07 03 01 00 00 00 00 38 49 2b 17 01 0b 19 0b 1a")[2] ==
          "Station spiegelt den Testbefehl"

    # built: short float 5.0 (00 00 a0 40), spontaneous, IOA 100
    s, m = d("68 12 00 00 00 00 0d 01 03 00 01 00 64 00 00 00 00 a0 40 00")
    @test occursin("IOA 100 Wert 5.0", s) && startswith(m, "Spontane Meldung")

    # built: single command "on" with select (SCO 0x81) to IOA 6000, then negative confirmation
    s, m = d("68 0e 00 00 00 00 2d 01 06 00 01 00 70 17 00 81")
    @test occursin("IOA 6000 ein Select", s) && startswith(m, "Leitstelle wählt den Befehl aus")
    @test d("68 0e 00 00 00 00 2d 01 47 00 01 00 70 17 00 01")[2] == "Station lehnt den Befehl ab"
    @test d("68 0e 00 00 00 00 2d 01 0a 00 01 00 70 17 00 01")[2] == "Station meldet: Befehl ausgeführt"

    # built: interrogation of unknown station 99 answered with cause 46, negative (0x6e)
    s, m = d("68 0e 00 00 00 00 64 01 6e 00 63 00 00 00 00 14")
    @test occursin("Ursache 46 Station unbekannt negativ", s) && occursin("Station 99", s)
    @test m == "Station kennt die gemeinsame Adresse (Station) nicht"

    # built: acknowledgement after the counter wrapped (N(R) = 0)
    @test d("68 04 01 00 00 00")[2] == "Quittung: alle I-Rahmen bis Nr. 32767 sind angekommen"
    # built: short float with only one value octet: header kept, element marked
    s, m = d("68 0d 00 00 00 00 0d 01 03 00 01 00 64 00 00 41")
    @test occursin("Station 1 · IOA 100 (Element unvollständig: 41)", s) && startswith(m, "Spontane Meldung")

    # details: fields for the filters and one line per object
    x = details(parse_hex("68 1c 06 00 02 00 0b 03 14 03 01 00 64 00 00 ff ff 00 65 00 00 17 00 00 66 00 00 fc 08 00"))
    @test (x.format, x.type, x.cause, x.station, x.negative) == ('I', 11, 20, 1, false)
    @test x.ioas == [100, 101, 102]
    @test x.objects == ["IOA 100 Wert -1", "IOA 101 Wert 23", "IOA 102 Wert 2300"]
    # capture: sequence (SQ = 1) of 8 single points from IOA 300
    x = details(parse_hex("68 15 0a 00 02 00 01 88 14 03 01 00 2c 01 00 01 00 01 00 01 00 01 00"))
    @test x.ioas == collect(UInt32(300):UInt32(307)) && x.objects[2] == "IOA 301 aus"
    @test details(parse_hex("68 04 01 00 14 00")).format == 'S'
    @test details(parse_hex("68 04 43 00 00 00")).format == 'U'
    # built: negative answer (P/N) to an interrogation of station 99
    x = details(parse_hex("68 0e 00 00 00 00 64 01 6e 00 63 00 00 00 00 14"))
    @test x.negative && x.cause == 46 && x.station == 99
    @test details(parse_hex("01 02 03")).format == '?'

    # unreadable input never throws
    @test startswith(d("68 0e 00 00")[1], "?")
    @test startswith(d("01 02 03 04 05 06")[1], "?")
end
