#!/usr/bin/env python3
# Mock-up picture of the panel with the real knob layers and jacks drawn in.
#   python3 tools/preview.py out.png [knob value 0..1]
import os, re, sys, subprocess
HERE = os.path.dirname(os.path.abspath(__file__)); R = os.path.join(HERE, "..", "res")
panel = open(os.path.join(R, "MinimalNation.svg")).read()
def inner(name):
    s = open(os.path.join(R, name)).read(); return re.sub(r'^<svg[^>]*>|</svg>$', '', s)
v = float(sys.argv[2]) if len(sys.argv) > 2 else 0.62
ang = -0.83 * 180 + 2 * 0.83 * 180 * v
KX, KY, D = 30.48, 54.0, 36.0
s = D / 100
knob = (f'<g transform="translate({KX - D/2},{KY - D/2}) scale({s})">{inner("Knob241-bg.svg")}'
        f'<g transform="rotate({ang:.1f} 50 50)">{inner("Knob241-fg.svg")}</g></g>')
def jack(x, y): return (f'<circle cx="{x}" cy="{y}" r="4.1" fill="#C9CCD1"/><circle cx="{x}" cy="{y}" r="3.1" fill="#7C8088"/>'
                        f'<circle cx="{x}" cy="{y}" r="1.9" fill="#0B0B0C"/>')
screws = "".join(f'<circle cx="{x}" cy="{y}" r="1.6" fill="#B5B8BE"/>' for x in (7.6, 60.96 - 7.6 - 5.08) for y in (2.5, 126.0))
svg = panel.replace("</svg>", knob + jack(17.0, 104.0) + jack(43.96, 104.0) + screws + "</svg>")
out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "preview.png")
tmp = out + ".svg"; open(tmp, "w").write(svg)
open(tmp + ".html", "w").write(f'<html><body style="margin:0"><img src="file://{tmp}" style="width:{60.96*6}px;height:{128.5*6}px"></body></html>')
js = f"""const {{chromium}}=require('playwright');(async()=>{{const b=await chromium.launch();const p=await b.newPage({{deviceScaleFactor:2}});
await p.setViewportSize({{width:{int(60.96*6)},height:{int(128.5*6)}}});
await p.goto('file://{tmp}.html');
await p.waitForTimeout(300);await p.screenshot({{path:'{out}'}});await b.close();}})();"""
subprocess.run(["node", "-e", js], check=True, cwd="/tmp")
os.remove(tmp); os.remove(tmp + ".html"); print(out)
