#!/usr/bin/env python3
# Mock-up picture of the Poltergeist panel with Rack's component graphics and a sample screen.
#   python3 tools/preview_poltergeist.py out.png [path/to/Rack/res/ComponentLibrary]
import os, re, sys, base64, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "poltergeist-preview.png")
LIB = sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser("~/Rack-src/res/ComponentLibrary")
svg = open(os.path.join(HERE, "..", "res", "Poltergeist.svg")).read()
L = {}
src = open(os.path.join(HERE, "..", "res", "make_poltergeist.py")).read()
exec(src.split("# ---- LAYOUT")[1].split("CONTROL,")[0].split("\n", 1)[1], L)
W, H = 71.12, 128.5
PX = 25.4 / 75.0
g = []

def comp(name, x, y, angle=0.0):
    s = open(os.path.join(LIB, name + ".svg")).read()
    m = re.search(r'width="([0-9.]+)(px|mm)?"', s); n = re.search(r'height="([0-9.]+)(px|mm)?"', s)
    w = float(m.group(1)) * (1 if m.group(2) == "mm" else PX); h = float(n.group(1)) * (1 if n.group(2) == "mm" else PX)
    data = base64.b64encode(s.encode()).decode()
    g.append(f'<g transform="rotate({angle} {x} {y})"><image href="data:image/svg+xml;base64,{data}" x="{x - w/2}" y="{y - h/2}" width="{w}" height="{h}"/></g>')

def knob(base, x, y, v):
    if os.path.exists(os.path.join(LIB, base + "_bg.svg")):
        comp(base + "_bg", x, y)
    comp(base, x, y, -135 + 270 * v)

K1X, C3 = L["K1_X"], L["COL3"]
knob("RoundBlackKnob", K1X[0], L["K1_Y"], 0.7)
knob("RoundBlackKnob", K1X[1], L["K1_Y"], 3 / 15)
for x, v in zip(C3, [0.6, 0.4, 0.2]): knob("RoundSmallBlackKnob", x, L["K2_Y"], v)
knob("RoundSmallBlackKnob", C3[0], L["K3_Y"], 0.45)
comp("VCVBezel", C3[1], L["K3_Y"]); comp("VCVBezel", C3[2], L["K3_Y"])
g.append(f'<circle cx="{C3[1]}" cy="{L["K3_Y"]}" r="2.3" fill="#3CFF8A" fill-opacity="0.9"/>')
for x in C3: comp("PJ301M", x, L["IN_Y"])
for x in L["OUT_X"]: comp("PJ301M", x, L["OUT_Y"])
for x in (5.08, W - 2 * 5.08):
    for y in (0.0, 128.5 - 5.08):
        comp("ScrewSilver", x + 2.54, y + 2.54)

# sample screen: ghost + text (same layout as PoltergeistDisplay)
dx, dy, dw, dh = L["DISP_X"], L["DISP_Y"], L["DISP_W"], L["DISP_H"]
s = dh * 0.24; cx = dx + 9.0; cy = dy + dh * 0.42; hem = cy + s * 1.15; w = 2 * s / 3
path = f"M{cx - s},{cy} A{s},{s} 0 0 1 {cx + s},{cy} L{cx + s},{hem} "
for i in range(3):
    x0 = cx + s - i * w
    path += f"Q{x0 - w/2},{hem - s*0.4} {x0 - w},{hem} "
g.append(f'<path d="{path}Z" fill="#E0F7FF" stroke="#9CFFD6" stroke-width="0.4"/>')
for k in (-1, 1):
    ex = cx + k * s * 0.36; ey = cy - s * 0.05
    g.append(f'<ellipse cx="{ex}" cy="{ey}" rx="{s*0.2}" ry="{s*0.26}" fill="#141220"/>')
    g.append(f'<circle cx="{ex + s*0.05}" cy="{ey - s*0.06}" r="{s*0.07}" fill="#fff"/>')
mono = "font-family:'Share Tech Mono','DejaVu Sans Mono',monospace"
tx = dx + 18.5
g.append(f'<text x="{tx}" y="{dy + 7.6}" font-size="4.6" fill="#9CFFD6" style="{mono}">SWAPPED</text>')
g.append(f'<text x="{tx}" y="{dy + 12.4}" font-size="2.9" fill="#EDE7FF" style="{mono}">LFO SINE &gt;</text>')
g.append(f'<text x="{tx}" y="{dy + 15.6}" font-size="2.9" fill="#EDE7FF" style="{mono}">VCF CUTOFF</text>')
g.append(f'<text x="{dx + 1.6}" y="{dy + dh - 1.5}" font-size="2.9" fill="#9C97B5" style="{mono}">FENCE 4  CABLES 7  HAUNTED 2</text>')

svg = svg.replace("</svg>", "\n".join(g) + "\n</svg>")
tmp = OUT + ".svg"; open(tmp, "w").write(svg)
pw, ph = int(W * 6), int(H * 6)
open(tmp + ".html", "w").write(f'<html><body style="margin:0;background:#111"><img src="file://{tmp}" style="width:{pw}px;height:{ph}px"></body></html>')
js = f"""const {{chromium}}=require('playwright');(async()=>{{const b=await chromium.launch();const p=await b.newPage({{deviceScaleFactor:2}});
await p.setViewportSize({{width:{pw},height:{ph}}});
await p.goto('file://{tmp}.html');
await p.waitForTimeout(400);await p.screenshot({{path:'{OUT}'}});await b.close();}})();"""
subprocess.run(["node", "-e", js], check=True, cwd="/tmp")
os.remove(tmp); os.remove(tmp + ".html"); print(OUT)
