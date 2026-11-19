#!/usr/bin/env python3
# Draws res/MinimalNation.svg and the knob layers res/Knob241-bg.svg / Knob241-fg.svg.
# VCV Rack can't draw SVG <text>, so labels become outline paths with fontTools.
# Positions in mm must match the LAYOUT block in src/MinimalNation.cpp.
#   pip install fonttools ; python3 res/make_panel.py
import os, sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
W, H = 60.96, 128.5      # 12 HP
BG, INK, SOFT, RED = "#FFFFFF", "#111113", "#8A898E", "#EF3B2C"
KNOB_X, KNOB_Y = 30.48, 54.0
IN_X, OUT_X, JACK_Y = 17.0, 43.96, 104.0
KNOB_D = 36.0            # knob diameter in mm (ring included)
JACK = 2.9

_f = {}
def text(s, x, y, size, fill=INK, spacing=0.12):
    if FONT_BOLD not in _f:
        f = TTFont(FONT_BOLD); _f[FONT_BOLD] = (f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    gs, cmap, upm = _f[FONT_BOLD]
    sc = size / upm; tr = spacing * upm
    names = [cmap[ord(c)] for c in s]
    w = sum(gs[n].width + tr for n in names) - tr
    x0 = x - w * sc / 2; pen = SVGPathPen(gs); cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (sc, 0, 0, -sc, x0 + cx * sc, y))); cx += gs[n].width + tr
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'

# ---- panel ----
p = [f'<rect width="{W}" height="{H}" fill="{BG}"/>']
p.append(text("MINIMAL NATION", W / 2, 16.0, 3.6, spacing=0.14))
p.append(f'<rect x="{W/2-1.0:.2f}" y="19.6" width="2.0" height="2.0" fill="{RED}"/>')
p.append(text("IN", IN_X, JACK_Y - 6.4, JACK))
p.append(text("OUT", OUT_X, JACK_Y - 6.4, JACK))
svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">'
       + "".join(p) + "</svg>")
out = os.path.join(HERE, "MinimalNation.svg")
open(out, "w").write(svg)
sys.path.insert(0, HERE)
import haer_logo
haer_logo.add_to_file(out, x=W / 2, y=126.0, cap=3.0, bg=BG)

# ---- knob 241 layers (drawn in a 100 x 100 box, scaled to KNOB_D mm) ----
def knob(body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{KNOB_D}mm" height="{KNOB_D}mm" '
            f'viewBox="0 0 100 100">{body}</svg>')
bg = ('<defs><linearGradient id="ball" x1="0" y1="0" x2="0" y2="1">'
      '<stop offset="0" stop-color="#F4F4F6"/><stop offset="0.5" stop-color="#8A898E"/>'
      '<stop offset="1" stop-color="#141315"/></linearGradient></defs>'
      '<circle cx="50" cy="50" r="49" fill="#FFFFFF"/>'
      f'<circle cx="50" cy="50" r="47.4" fill="none" stroke="{INK}" stroke-width="3.2"/>'
      f'<circle cx="50" cy="50" r="33" fill="url(#ball)" stroke="{INK}" stroke-width="0.8"/>')
fg = (f'<rect x="42.5" y="1.6" width="15" height="49.9" fill="{INK}"/>'
      f'<circle cx="50" cy="93" r="5.5" fill="{RED}"/>')
open(os.path.join(HERE, "Knob241-bg.svg"), "w").write(knob(bg))
open(os.path.join(HERE, "Knob241-fg.svg"), "w").write(knob(fg))
print("panel and knob written")
