#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# make_logo.py - draws the logo drafts as SVG (lines and shapes only, no fonts in
# the marks): logo_<k>.svg (icon), schriftzug_<k>.svg (icon with word mark) and
# sheet.svg (overview with small sizes). Run in this folder: python3 make_logo.py
import math, sys

NAVY, NAVY2 = "#1e2f44", "#2c4f7a"
CYAN, GREEN, WHITE = "#53c1f0", "#6ed39a", "#ffffff"
DARK, BLUE, GREEN_D = "#22364d", "#2c6fb3", "#2e9e6a"

def head(w, h):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" '
            f'fill="none" stroke-linecap="round" stroke-linejoin="round">\n')

def badge(grad="g"):
    return (f'<defs><linearGradient id="{grad}" x1="0" y1="0" x2="1" y2="1">'
            f'<stop offset="0" stop-color="{NAVY}"/><stop offset="1" stop-color="{NAVY2}"/></linearGradient></defs>\n'
            f'<rect x="16" y="16" width="480" height="480" rx="104" fill="url(#{grad})"/>\n')

def digits_104(x, top, h, w, gap, stroke, color, zero=None):
    """Monoline 1, 0, 4; returns SVG. zero: optional replacement for the 0."""
    b = top + h
    x1 = x + w * 0.62
    one = f'<path d="M {x + w*0.12:.1f} {top + h*0.26:.1f} L {x1:.1f} {top} L {x1:.1f} {b}"/>'
    x0 = x + w + gap
    o = zero if zero is not None else f'<rect x="{x0}" y="{top}" width="{w}" height="{h}" rx="{w/2}"/>'
    x4 = x0 + w + gap
    stem = x4 + w * 0.70
    bar = top + h * 0.70
    four = f'<path d="M {stem:.1f} {b} L {stem:.1f} {top} L {x4:.1f} {bar:.1f} L {x4 + w*1.02:.1f} {bar:.1f}"/>'
    return f'<g stroke="{color}" stroke-width="{stroke}">{one}{o if zero is None else ""}{four}</g>' + (zero or "")

def pt(cx, cy, rx, ry, deg):
    a = math.radians(deg)
    return cx + rx * math.cos(a), cy + ry * math.sin(a)

def arrow(x, y, dx, dy, length, half, color):
    """Filled arrow head: base centred at (x, y), tip at distance length in direction (dx, dy)."""
    n = math.hypot(dx, dy)
    ux, uy = dx / n, dy / n
    tip = (x + length * ux, y + length * uy)
    l = (x - half * uy, y + half * ux)
    r = (x + half * uy, y - half * ux)
    return (f'<polygon points="{tip[0]:.1f},{tip[1]:.1f} {l[0]:.1f},{l[1]:.1f} {r[0]:.1f},{r[1]:.1f}" '
            f'fill="{color}" stroke="{color}" stroke-width="5"/>')

BITS_68 = [0, 1, 1, 0, 1, 0, 0, 0]   # 0x68, the start octet of every APDU

def variant_a():
    s = head(512, 512) + badge("ga")
    # two paths between controlling station (left) and station (right)
    s += f'<path d="M 108 226 Q 256 66 404 226" stroke="{CYAN}" stroke-width="16"/>\n'
    s += f'<path d="M 404 286 Q 256 446 108 286" stroke="{GREEN}" stroke-width="16"/>\n'
    s += arrow(246, 146, 1, 0, 34, 21, CYAN) + arrow(266, 366, -1, 0, 34, 21, GREEN) + "\n"
    s += f'<circle cx="92" cy="256" r="30" fill="{WHITE}"/><circle cx="420" cy="256" r="30" fill="{WHITE}"/>\n'
    s += f'<circle cx="92" cy="256" r="11" fill="{NAVY}"/><circle cx="420" cy="256" r="11" fill="{NAVY2}"/>\n'
    s += digits_104(166, 208, 96, 46, 21, 20, WHITE) + "\n"
    for i, bit in enumerate(BITS_68):
        x = 256 - 3.5 * 26 + i * 26
        s += (f'<circle cx="{x}" cy="440" r="7" fill="{WHITE}"/>' if bit else
              f'<circle cx="{x}" cy="440" r="5.5" stroke="{WHITE}" stroke-opacity="0.45" stroke-width="3"/>')
    return s + "\n</svg>\n"

def zero_arrows(cx, cy, rx, ry, stroke, c1, c2):
    """The 0 as two arcs that follow each other: request and answer."""
    def arc(a0, a1, color):
        x0, y0 = pt(cx, cy, rx, ry, a0)
        x1, y1 = pt(cx, cy, rx, ry, a1)
        t = math.radians(a1)
        head = arrow(x1, y1, -rx * math.sin(t), ry * math.cos(t), stroke * 1.25, stroke * 0.95, color)
        return (f'<path d="M {x0:.1f} {y0:.1f} A {rx} {ry} 0 0 1 {x1:.1f} {y1:.1f}" stroke="{color}" '
                f'stroke-width="{stroke}"/>' + head)
    return arc(198, 322, c1) + arc(18, 142, c2)

def variant_b():
    s = head(512, 512)
    s += f'<rect x="16" y="16" width="480" height="480" rx="104" fill="#f3f6fa" stroke="#d5dde8" stroke-width="4"/>\n'
    s += digits_104(70, 146, 220, 100, 36, 36, DARK, zero=zero_arrows(256, 256, 62, 110, 32, BLUE, GREEN_D))
    return s + "\n</svg>\n"

def variant_c():
    s = head(512, 512) + badge("gc")
    # 0x68 as a square wave: low-high-high-low-high-low-low-low
    x, lo, hi, step = 76, 196, 110, 45
    d = f"M {x} {lo}"
    level = 0
    for bit in BITS_68:
        if bit != level:
            d += f" L {x} {hi if bit else lo}"
            level = bit
        x += step
        d += f" L {x} {hi if level else lo}"
    s += f'<path d="{d}" stroke="{CYAN}" stroke-width="16"/>\n'
    for i, bit in enumerate(BITS_68):
        cx = 76 + step * i + step / 2
        s += (f'<circle cx="{cx}" cy="236" r="6" fill="{GREEN}"/>' if bit else
              f'<circle cx="{cx}" cy="236" r="4.5" stroke="{WHITE}" stroke-opacity="0.4" stroke-width="3"/>')
    s += "\n" + digits_104(121, 282, 150, 76, 21, 30, WHITE)
    return s + "\n</svg>\n"

def wordmark(x, base, cap, color, accent, stroke):
    """ "iec104" as monoline letters; returns (svg, width)."""
    xh = cap * 0.70
    r = xh / 2
    cy = base - r
    s = f'<g stroke="{color}" stroke-width="{stroke}">'
    s += f'<path d="M {x} {base - xh} L {x} {base}"/>'
    ecx = x + stroke * 1.5 + r
    ex, ey = ecx + r * math.cos(math.radians(38)), cy + r * math.sin(math.radians(38))
    s += f'<path d="M {ecx - r:.1f} {cy:.1f} L {ecx + r:.1f} {cy:.1f} A {r:.1f} {r:.1f} 0 1 0 {ex:.1f} {ey:.1f}"/>'
    ccx = ecx + r + stroke * 1.4 + r
    c0 = (ccx + r * math.cos(math.radians(-42)), cy + r * math.sin(math.radians(-42)))
    c1 = (ccx + r * math.cos(math.radians(42)), cy + r * math.sin(math.radians(42)))
    s += f'<path d="M {c0[0]:.1f} {c0[1]:.1f} A {r:.1f} {r:.1f} 0 1 0 {c1[0]:.1f} {c1[1]:.1f}"/></g>'
    s += f'<circle cx="{x}" cy="{base - xh - stroke * 1.5:.1f}" r="{stroke * 0.62:.1f}" fill="{color}"/>'
    dx = ccx + r + stroke * 1.6
    w = cap * 0.50
    s += digits_104(dx, base - cap, cap, w, stroke * 1.05, stroke, accent)
    return s, dx + 3 * w + 2 * stroke * 1.05 + stroke - x

def lockup(key):
    icon = VARIANTS[key]().split("\n", 1)[1].rsplit("</svg>", 1)[0]
    s = head(1180, 340)
    s += (f'<svg x="30" y="30" width="280" height="280" viewBox="0 0 512 512" fill="none" '
          f'stroke-linecap="round" stroke-linejoin="round">{icon}</svg>\n')
    wm, _ = wordmark(372, 212, 132, DARK, BLUE, 24)
    s += wm + "\n"
    s += (f'<text x="362" y="286" font-family="Cantarell, \'Segoe UI\', \'Helvetica Neue\', Arial, sans-serif" '
          f'font-size="38" fill="#5b6b7d" stroke="none">IEC 60870-5-104 · Client · Server · TLS</text>\n')
    return s + "</svg>\n"

VARIANTS = {"a": variant_a, "b": variant_b, "c": variant_c}

if __name__ == "__main__":
    for k, f in VARIANTS.items():
        open(f"logo_{k}.svg", "w").write(f())
        open(f"schriftzug_{k}.svg", "w").write(lockup(k))
    inner = 'fill="none" stroke-linecap="round" stroke-linejoin="round"'
    sheet = head(1700, 880) + '<rect width="1700" height="880" fill="#ffffff"/>\n'
    for i, (k, f) in enumerate(VARIANTS.items()):
        body = f().split("\n", 1)[1].rsplit("</svg>", 1)[0]
        sheet += f'<svg x="{40 + i * 560}" y="30" width="512" height="512" viewBox="0 0 512 512" {inner}>{body}</svg>\n'
        lk = lockup(k).split("\n", 1)[1].rsplit("</svg>", 1)[0]
        sheet += f'<svg x="{40 + i * 560}" y="580" width="520" height="150" viewBox="0 0 1180 340" {inner}>{lk}</svg>\n'
        # small sizes: does it still read at 64 and 32 px?
        sheet += f'<svg x="{40 + i * 560}" y="780" width="64" height="64" viewBox="0 0 512 512" {inner}>{body}</svg>\n'
        sheet += f'<svg x="{130 + i * 560}" y="796" width="32" height="32" viewBox="0 0 512 512" {inner}>{body}</svg>\n'
        sheet += (f'<text x="{200 + i * 560}" y="826" font-family="Cantarell, sans-serif" font-size="30" fill="#22364d" '
                  f'stroke="none">Variante {k.upper()}</text>\n')
    open("sheet.svg", "w").write(sheet + "</svg>\n")
