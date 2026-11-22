#!/usr/bin/env python3
# Draws res/Poltergeist.svg (14 HP). Rack can't draw SVG <text>, so labels become paths.
# Positions in mm must match the LAYOUT block in src/Poltergeist.cpp.
#   pip install fonttools ; python3 res/make_poltergeist.py
import os, sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")
W, H = 71.12, 128.5      # 14 HP
BG, BG_DARK, LINE = "#1B1A24", "#12111A", "#34314A"
INK, SOFT, MINT, RED = "#EDE7FF", "#9C97B5", "#9CFFD6", "#FF5A6E"

# ---- LAYOUT (same numbers in src/Poltergeist.cpp) ----
DISP_X, DISP_Y, DISP_W, DISP_H = 5.0, 16.0, 61.12, 24.0
K1_Y, K1_X = 52.0, [20.0, 51.12]
K2_Y, K3_Y = 71.0, 87.5
COL3 = [13.0, 35.56, 58.12]
IN_Y = 104.0
OUT_Y, OUT_X = 117.0, [24.0, 47.12]

CONTROL, JACK, SMALL = 3.2, 2.9, 2.4

_f = {}
def text(s, x, y, size, font=FONT_BOLD, fill=INK, anchor="middle", spacing=0.06):
    if font not in _f:
        f = TTFont(font); _f[font] = (f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    gs, cmap, upm = _f[font]
    sc = size / upm; tr = spacing * upm
    names = [cmap[ord(c)] for c in s]
    w = sum(gs[n].width + tr for n in names) - tr
    x0 = {"middle": x - w * sc / 2, "end": x - w * sc}.get(anchor, x)
    pen = SVGPathPen(gs); cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (sc, 0, 0, -sc, x0 + cx * sc, y))); cx += gs[n].width + tr
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'

p = [f'<rect width="{W}" height="{H}" fill="{BG}"/>']
add = p.append
add(text("POLTERGEIST", W / 2, 10.6, 5.0, spacing=0.08))
add(text("moves your cables", W / 2, 14.4, 2.4, FONT_MED, MINT))
add(f'<rect x="{DISP_X - 0.6}" y="{DISP_Y - 0.6 + 0.8}" width="{DISP_W + 1.2}" height="{DISP_H + 1.2 - 0.8}" rx="1.2" '
    f'fill="#07060C" stroke="{MINT}" stroke-opacity="0.5" stroke-width="0.35"/>')

def ranges(x, y, r, lo, hi):
    add(text(lo, x - r, y, SMALL, FONT_BOLD, SOFT, anchor="end", spacing=0.02))
    add(text(hi, x + r, y, SMALL, FONT_BOLD, SOFT, anchor="start", spacing=0.02))

# big knobs
add(text("CHANCE", K1_X[0], K1_Y - 8.6, CONTROL))
add(text("FENCE", K1_X[1], K1_Y - 8.6, CONTROL, fill=MINT))
ranges(K1_X[0], K1_Y + 7.4, 4.4, "0", "100%")
ranges(K1_X[1], K1_Y + 7.4, 4.4, "1", "16")

# mischief weights
add(f'<line x1="4" y1="{K2_Y - 11.0}" x2="{W - 4}" y2="{K2_Y - 11.0}" stroke="{LINE}" stroke-width="0.4"/>')
for x, n in zip(COL3, ["SWAP", "WANDER", "VANISH"]):
    add(text(n, x, K2_Y - 6.6, CONTROL))
# return + buttons
for x, n, c in zip(COL3, ["RETURN", "HAUNT", "EXORCISE"], [INK, MINT, RED]):
    add(text(n, x, K3_Y - 6.6, CONTROL, fill=c))
ranges(COL3[0], K3_Y + 6.6, 3.6, "0.1s", "NEVER")

# jacks
add(f'<rect x="3.0" y="{IN_Y - 9.4}" width="{W - 6.0}" height="14.0" rx="1.4" fill="{BG_DARK}"/>')
for x, n in zip(COL3, ["HAUNT", "EXORCISE", "CHANCE"]):
    add(text(n, x, IN_Y - 5.4, JACK, fill=SOFT))
ox0, ox1 = OUT_X[0] - 9.6, OUT_X[1] + 9.6
add(f'<rect x="{ox0}" y="{OUT_Y - 7.6}" width="{ox1 - ox0}" height="12.0" rx="1.4" fill="{MINT}"/>')
for x, n in zip(OUT_X, ["MISCHIEF", "HAUNTED"]):
    add(text(n, x, OUT_Y - 4.6, JACK, fill=BG))

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">'
       + "".join(p) + "</svg>")
out = os.path.join(HERE, "Poltergeist.svg")
open(out, "w").write(svg)
sys.path.insert(0, HERE)
import haer_logo
haer_logo.add_to_file(out, x=W / 2, y=126.0, cap=3.0, bg=BG)
print("panel written")
