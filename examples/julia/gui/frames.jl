# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# frames.jl - plain German description of an IEC 104 APDU for the commented
# protocol of the test GUI: frame format (I, S, U), type, cause, station and
# the first information object, plus one sentence what the frame means.
#
# Pure Julia, no call into the library. Only the default profile (cause of
# transmission 2 octets, common address 2, IOA 3) like the GUI uses.
# Field layout: docs/02_Technik_und_Quellen.md; reference frames in
# tests/gui/test_frames.jl.

module Frames

export describe, parse_hex, details

# Type identification -> (name, German short text).
const TYPES = Dict{Int,Tuple{String,String}}(
    1 => ("M_SP_NA_1", "Einzelmeldung"), 3 => ("M_DP_NA_1", "Doppelmeldung"),
    5 => ("M_ST_NA_1", "Stufenstellung"), 7 => ("M_BO_NA_1", "Bitmuster"),
    9 => ("M_ME_NA_1", "Messwert normiert"), 11 => ("M_ME_NB_1", "Messwert skaliert"),
    13 => ("M_ME_NC_1", "Messwert Gleitkomma"), 15 => ("M_IT_NA_1", "Zählwert"),
    21 => ("M_ME_ND_1", "Messwert normiert ohne Qualität"),
    30 => ("M_SP_TB_1", "Einzelmeldung mit Zeit"), 31 => ("M_DP_TB_1", "Doppelmeldung mit Zeit"),
    32 => ("M_ST_TB_1", "Stufenstellung mit Zeit"), 33 => ("M_BO_TB_1", "Bitmuster mit Zeit"),
    34 => ("M_ME_TD_1", "Messwert normiert mit Zeit"), 35 => ("M_ME_TE_1", "Messwert skaliert mit Zeit"),
    36 => ("M_ME_TF_1", "Messwert Gleitkomma mit Zeit"), 37 => ("M_IT_TB_1", "Zählwert mit Zeit"),
    45 => ("C_SC_NA_1", "Einzelbefehl"), 46 => ("C_DC_NA_1", "Doppelbefehl"),
    47 => ("C_RC_NA_1", "Stufenbefehl"), 48 => ("C_SE_NA_1", "Sollwert normiert"),
    49 => ("C_SE_NB_1", "Sollwert skaliert"), 50 => ("C_SE_NC_1", "Sollwert Gleitkomma"),
    51 => ("C_BO_NA_1", "Bitmuster-Befehl"),
    58 => ("C_SC_TA_1", "Einzelbefehl mit Zeit"), 59 => ("C_DC_TA_1", "Doppelbefehl mit Zeit"),
    60 => ("C_RC_TA_1", "Stufenbefehl mit Zeit"), 61 => ("C_SE_TA_1", "Sollwert normiert mit Zeit"),
    62 => ("C_SE_TB_1", "Sollwert skaliert mit Zeit"), 63 => ("C_SE_TC_1", "Sollwert Gleitkomma mit Zeit"),
    64 => ("C_BO_TA_1", "Bitmuster-Befehl mit Zeit"),
    70 => ("M_EI_NA_1", "Ende der Initialisierung"),
    100 => ("C_IC_NA_1", "Generalabfrage"), 101 => ("C_CI_NA_1", "Zählerabfrage"),
    102 => ("C_RD_NA_1", "Lesebefehl"), 103 => ("C_CS_NA_1", "Uhrzeit"),
    104 => ("C_TS_NA_1", "Testbefehl"), 105 => ("C_RP_NA_1", "Rücksetzen"),
    106 => ("C_CD_NA_1", "Laufzeitmessung"), 107 => ("C_TS_TA_1", "Testbefehl mit Zeit"))

function cause_text(c::Integer)
    c == 1 && return "zyklisch"
    c == 2 && return "Hintergrund"
    c == 3 && return "spontan"
    c == 4 && return "initialisiert"
    c == 5 && return "Abfrage"
    c == 6 && return "Aktivierung"
    c == 7 && return "Bestätigung"
    c == 8 && return "Abbruch"
    c == 9 && return "Abbruch bestätigt"
    c == 10 && return "Beendigung"
    c == 11 && return "Rückmeldung fern"
    c == 12 && return "Rückmeldung lokal"
    c == 20 && return "Generalabfrage"
    21 <= c <= 36 && return "Gruppe $(c - 20)"
    c == 37 && return "Zählerabfrage"
    38 <= c <= 41 && return "Zählergruppe $(c - 37)"
    c == 44 && return "Typ unbekannt"
    c == 45 && return "Ursache unbekannt"
    c == 46 && return "Station unbekannt"
    c == 47 && return "IOA unbekannt"
    return "Ursache $c"
end

"Octets of a hex dump like \"68 04 07 00 00 00\"."
parse_hex(s::AbstractString) = UInt8[parse(UInt8, h; base = 16) for h in split(s)]

# --- Elements ----------------------------------------------------------------------

u16(b, i) = UInt16(b[i]) | UInt16(b[i+1]) << 8
i16(b, i) = reinterpret(Int16, u16(b, i))
u32(b, i) = UInt32(b[i]) | UInt32(b[i+1]) << 8 | UInt32(b[i+2]) << 16 | UInt32(b[i+3]) << 24

function quality(q::UInt8)
    names = [n for (bit, n) in ((0x01, "OV"), (0x10, "BL"), (0x20, "SB"), (0x40, "NT"), (0x80, "IV"))
             if q & bit != 0]
    return isempty(names) ? "" : " [" * join(names, " ") * "]"
end

"CP56Time2a at index i (7 octets)."
function cp56(b, i)
    ms = u16(b, i)
    t = string(2000 + (b[i+6] & 0x7f), "-", lpad(b[i+5] & 0x0f, 2, '0'), "-", lpad(b[i+4] & 0x1f, 2, '0'),
               " ", lpad(b[i+3] & 0x1f, 2, '0'), ":", lpad(b[i+2] & 0x3f, 2, '0'), ":",
               lpad(ms ÷ 1000, 2, '0'), ".", lpad(ms % 1000, 3, '0'))
    b[i+3] & 0x80 != 0 && (t *= " SU")
    b[i+2] & 0x80 != 0 && (t *= " ungültig")
    return t
end

const DOUBLE = ("unbestimmt", "aus", "ein", "gestört")        # double point (DIQ)
const DCO = ("nicht erlaubt", "aus", "ein", "nicht erlaubt")   # double command (DCS)
const STEP = ("nicht erlaubt", "tiefer", "höher", "nicht erlaubt")

command_mode(q::UInt8) = q & 0x80 != 0 ? " Select" : ""

"Text of the element of type t at index i (first object only), \"\" if not decoded."
function element(t::Int, b, i)
    base = t in 30:37 ? (t == 30 ? 1 : t == 31 ? 3 : t == 32 ? 5 : t == 33 ? 7 :
                         t == 34 ? 9 : t == 35 ? 11 : t == 36 ? 13 : 15) :
           t in 58:64 ? t - 13 : t
    s = if base == 1
        (b[i] & 0x01 != 0 ? "ein" : "aus") * quality(b[i] & 0xf0)
    elseif base == 3
        DOUBLE[(b[i] & 0x03) + 1] * quality(b[i] & 0xf0)
    elseif base == 5
        v = Int(b[i] & 0x7f)
        v >= 64 && (v -= 128)
        "Stufe $v" * (b[i] & 0x80 != 0 ? " (läuft)" : "") * quality(b[i+1])
    elseif base == 7
        "Bits 0x" * string(u32(b, i); base = 16, pad = 8) * quality(b[i+4])
    elseif base == 9
        "Wert " * string(round(i16(b, i) / 32768; digits = 5)) * quality(b[i+2])
    elseif base == 11
        "Wert $(i16(b, i))" * quality(b[i+2])
    elseif base == 21
        "Wert " * string(round(i16(b, i) / 32768; digits = 5))
    elseif base == 13
        "Wert " * string(reinterpret(Float32, u32(b, i))) * quality(b[i+4])
    elseif base == 15
        "Zähler $(reinterpret(Int32, u32(b, i))) Folge $(b[i+4] & 0x1f)" *
            (b[i+4] & 0x20 != 0 ? " CY" : "") * (b[i+4] & 0x40 != 0 ? " CA" : "") *
            (b[i+4] & 0x80 != 0 ? " [IV]" : "")
    elseif base == 45
        (b[i] & 0x01 != 0 ? "ein" : "aus") * command_mode(b[i])
    elseif base == 46
        DCO[(b[i] & 0x03) + 1] * command_mode(b[i])
    elseif base == 47
        STEP[(b[i] & 0x03) + 1] * command_mode(b[i])
    elseif base == 48
        "Sollwert " * string(round(i16(b, i) / 32768; digits = 5)) * command_mode(b[i+2])
    elseif base == 49
        "Sollwert $(i16(b, i))" * command_mode(b[i+2])
    elseif base == 50
        "Sollwert " * string(reinterpret(Float32, u32(b, i))) * command_mode(b[i+4])
    elseif base == 51
        "Bits 0x" * string(u32(b, i); base = 16, pad = 8)
    elseif t == 70
        "Grund $(b[i] & 0x7f)" * (b[i] & 0x80 != 0 ? " (Parameter geändert)" : "")
    elseif t == 100
        q = Int(b[i])
        q == 20 ? "QOI 20 (Station)" : 21 <= q <= 36 ? "QOI $q (Gruppe $(q - 20))" : "QOI $q"
    elseif t == 101
        "RQT $(b[i] & 0x3f) FRZ $(b[i] >> 6)"
    elseif t == 103
        cp56(b, i)
    elseif t == 104
        "Muster 0x" * string(u16(b, i); base = 16, pad = 4)
    elseif t == 105
        "QRP $(b[i])"
    elseif t == 107
        "Zähler $(u16(b, i)) Zeit " * cp56(b, i + 2)
    else
        ""
    end
    # Time tag behind the element of types with time.
    if t in 30:37 || t in 58:64
        len = Dict(1 => 1, 3 => 1, 5 => 2, 7 => 5, 9 => 3, 11 => 3, 13 => 5, 15 => 5,
                   45 => 1, 46 => 1, 47 => 1, 48 => 3, 49 => 3, 50 => 5, 51 => 4)[base]
        s *= " Zeit " * cp56(b, i + len)
    end
    return s
end

# --- Meaning ------------------------------------------------------------------------

"One sentence what an ASDU means."
function meaning(t::Int, cause::Int, negative::Bool, select::Bool)
    cause == 44 && return "Station kennt diesen Typ nicht"
    cause == 45 && return "Station kennt diese Ursache für den Typ nicht"
    cause == 46 && return "Station kennt die gemeinsame Adresse (Station) nicht"
    cause == 47 && return "Station kennt die IOA nicht"
    if t <= 44   # monitor direction
        cause == 3 && return "Spontane Meldung: die Station meldet einen neuen Wert"
        cause == 20 && return "Antwort auf die Generalabfrage"
        21 <= cause <= 36 && return "Antwort auf die Abfrage der Gruppe $(cause - 20)"
        37 <= cause <= 41 && return "Antwort auf die Zählerabfrage"
        cause == 5 && return "Antwort auf einen Lesebefehl"
        cause == 1 && return "Zyklische Meldung"
        cause == 2 && return "Hintergrundabfrage"
        cause in (11, 12) && return "Rückmeldung nach einem Befehl"
        return ""
    end
    t == 70 && return "Ende der Initialisierung: die Station ist bereit"
    what = t == 100 ? "die Generalabfrage" : t == 101 ? "die Zählerabfrage" :
           t == 103 ? "die Uhrzeit" : t in (104, 107) ? "den Testbefehl" :
           t == 105 ? "das Rücksetzen" : select ? "die Auswahl" : "den Befehl"
    if cause == 6
        t == 100 && return "Leitstelle fordert alle Werte an"
        t == 101 && return "Leitstelle fordert die Zählwerte an"
        t == 103 && return "Leitstelle stellt die Uhr der Station"
        t in (104, 107) && return "Testbefehl: prüft die Verbindung bis in die Anwendung"
        t == 105 && return "Leitstelle setzt die Station zurück"
        return select ? "Leitstelle wählt den Befehl aus (Select, noch nicht ausführen)" :
               "Leitstelle sendet den Befehl"
    end
    cause == 5 && t == 102 && return "Leitstelle fragt einen einzelnen Wert ab"
    cause == 7 && return negative ? "Station lehnt $what ab" :
                         t in (104, 107) ? "Station spiegelt den Testbefehl" : "Station bestätigt $what"
    cause == 8 && return "Leitstelle bricht $what ab"
    cause == 9 && return negative ? "Station lehnt den Abbruch ab" : "Station bestätigt den Abbruch"
    cause == 10 && return t in (100, 101) ? "Station meldet: alle Werte gesendet, $what ist beendet" :
                          "Station meldet: Befehl ausgeführt"
    return ""
end

const U_FUNCTIONS = Dict(
    0x07 => ("U STARTDT act", "Leitstelle bittet, die Datenübertragung zu starten"),
    0x0b => ("U STARTDT con", "Station bestätigt: Datenübertragung läuft"),
    0x13 => ("U STOPDT act", "Leitstelle bittet, die Datenübertragung anzuhalten"),
    0x23 => ("U STOPDT con", "Station bestätigt: Datenübertragung angehalten"),
    0x43 => ("U TESTFR act", "Testrahmen: lebt die Verbindung? (nach t3 ohne Verkehr)"),
    0x83 => ("U TESTFR con", "Antwort auf den Testrahmen: die Verbindung lebt"))

"""
    describe(apdu) -> (summary, meaning)

Summary like "I N(S)=0 N(R)=0 · 100 C_IC_NA_1 Generalabfrage · Ursache 6 Aktivierung ·
Station 1 · IOA 0 QOI 20 (Station)" and one German sentence what the frame means.
Never throws: unreadable frames come back as hex.
"""
function describe(b::AbstractVector{UInt8})
    try
        length(b) >= 6 && b[1] == 0x68 || return ("? " * hexdump(b), "kein IEC-104-Rahmen")
        c1 = b[3]
        if c1 & 0x01 == 0x00
            ns = (u16(b, 3) >> 1)
            nr = (u16(b, 5) >> 1)
            head = "I N(S)=$ns N(R)=$nr"
            length(b) >= 6 + 6 + 3 || return (head * " (ASDU zu kurz)", "")
            return describe_asdu(head, b, 7)
        elseif c1 & 0x03 == 0x01
            nr = (u16(b, 5) >> 1)
            return ("S N(R)=$nr", "Quittung: alle I-Rahmen bis Nr. $(mod(Int(nr) - 1, 32768)) sind angekommen")
        else
            return get(U_FUNCTIONS, c1, ("U 0x" * string(c1; base = 16, pad = 2), "unbekannte U-Funktion"))
        end
    catch
        return ("? " * hexdump(b), "Rahmen nicht lesbar")
    end
end

hexdump(b) = join((string(x; base = 16, pad = 2) for x in b), ' ')

function describe_asdu(head::String, b, i::Int)
    t = Int(b[i])
    n = Int(b[i+1] & 0x7f)
    cause = Int(b[i+2] & 0x3f)
    negative = b[i+2] & 0x40 != 0
    test = b[i+2] & 0x80 != 0
    station = Int(u16(b, i + 4))
    name, text = get(TYPES, t, ("Typ $t", ""))
    parts = [head, isempty(text) ? name : "$t $name $text",
             "Ursache $cause " * cause_text(cause) * (negative ? " negativ" : "") * (test ? " Test" : ""),
             "Station $station"]
    j = i + 6
    if length(b) >= j + 2
        ioa = Int(b[j]) | Int(b[j+1]) << 8 | Int(b[j+2]) << 16
        e = try
            t == 102 ? "" : length(b) >= j + 3 ? element(t, b, j + 3) : "(Element fehlt)"
        catch
            "(Element unvollständig: " * hexdump(b[j+3:end]) * ")"
        end
        push!(parts, "IOA $ioa" * (isempty(e) ? "" : " " * e) * (n > 1 ? " (+$(n - 1) weitere)" : ""))
    end
    select = t in (45:51..., 58:64...) && length(b) >= j + 3 && command_select(t, b, j + 3)
    return (join(parts, " · "), meaning(t, cause, negative, select))
end

"S/E bit of a command element (in the qualifier octet behind the value)."
function command_select(t::Int, b, i::Int)
    base = t >= 58 ? t - 13 : t
    q = base in (45, 46, 47) ? i : base in (48, 49) ? i + 2 : base == 50 ? i + 4 : 0
    return q != 0 && q <= length(b) && b[q] & 0x80 != 0
end

# Octets of one element (time tag included) per type; types not listed: unknown length.
const ELEMENT_LENGTH = Dict(1 => 1, 3 => 1, 5 => 2, 7 => 5, 9 => 3, 11 => 3, 13 => 5, 15 => 5,
    21 => 2, 30 => 8, 31 => 8, 32 => 9, 33 => 12, 34 => 10, 35 => 10, 36 => 12, 37 => 12,
    45 => 1, 46 => 1, 47 => 1, 48 => 3, 49 => 3, 50 => 5, 51 => 4, 58 => 8, 59 => 8, 60 => 8,
    61 => 10, 62 => 10, 63 => 12, 64 => 11, 70 => 1, 100 => 1, 101 => 1, 102 => 0, 103 => 7,
    104 => 2, 105 => 1, 107 => 9)

"""
    details(apdu) -> NamedTuple

Fields for filters and the object list: format ('I', 'S', 'U', '?'), type, cause,
station (-1 without ASDU), negative, ioas (all objects) and objects (one line
per object, "IOA 100 Wert 5.0"). Never throws.
"""
function details(b::AbstractVector{UInt8})
    none = (format = '?', type = -1, cause = -1, station = -1, negative = false,
            ioas = UInt32[], objects = String[])
    try
        length(b) >= 6 && b[1] == 0x68 || return none
        b[3] & 0x01 == 0x01 && return (none..., format = b[3] & 0x03 == 0x01 ? 'S' : 'U')
        length(b) >= 12 || return (none..., format = 'I')
        t = Int(b[7])
        n = Int(b[8] & 0x7f)
        sq = b[8] & 0x80 != 0
        ioas = UInt32[]
        objects = String[]
        len = get(ELEMENT_LENGTH, t, -1)
        j = 13                       # first octet of the first object (1-based)
        ioa = UInt32(0)
        for k in 1:n
            if k == 1 || !sq
                j + 2 <= length(b) || break
                ioa = UInt32(b[j]) | UInt32(b[j+1]) << 8 | UInt32(b[j+2]) << 16
                j += 3
            else
                ioa += 1
            end
            push!(ioas, ioa)
            e = len < 0 ? "" : j + len - 1 <= length(b) ? (try element(t, b, j) catch; "" end) :
                "(Element unvollständig)"
            push!(objects, "IOA $ioa" * (isempty(e) ? "" : " " * e))
            len < 0 && break
            j += len
        end
        return (format = 'I', type = t, cause = Int(b[9] & 0x3f), station = Int(u16(b, 11)),
                negative = b[9] & 0x40 != 0, ioas = ioas, objects = objects)
    catch
        return none
    end
end

end # module Frames
