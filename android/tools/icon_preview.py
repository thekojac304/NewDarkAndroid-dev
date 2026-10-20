"""Builds docs/android-audit/phase3g/icon/preview.html from the SVGs make_icon.py writes: the icon cropped as launchers
show it (the middle 72 of 108 units) under circle, squircle and rounded-square masks, at 192/96/48/32 px, on dark and light
wallpapers, plus the themed (monochrome) look. Render it with any browser, e.g. headless Edge:
    msedge --headless --screenshot=preview.png --window-size=1100,760 preview.html
"""
import os

D = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "android-audit", "phase3g", "icon")


def inner(name):
    s = open(os.path.join(D, name), encoding="utf-8").read()
    return s[s.index(">") + 1:s.rindex("</svg>")]


FULL, MONO = inner("icon_full.svg"), inner("icon_monochrome.svg")
MASKS = {"circle": "M54,18 a36,36 0 1,0 0.01,0 Z",
         "squircle": "M54,18 C82,18 90,26 90,54 C90,82 82,90 54,90 C26,90 18,82 18,54 C18,26 26,18 54,18 Z",
         "rounded": "M30,18 H78 A12,12 0 0 1 90,30 V78 A12,12 0 0 1 78,90 H30 A12,12 0 0 1 18,78 V30 A12,12 0 0 1 30,18 Z"}
n = 0


def icon(size, mask, mono=False, tint=None):
    global n
    n += 1
    body = (f'<rect width="108" height="108" fill="{tint[0]}"/><g fill="{tint[1]}" stroke="{tint[1]}" style="color:{tint[1]}">'
            + MONO.replace('#FFFFFF', tint[1]) + "</g>") if mono else FULL
    return (f'<svg width="{size}" height="{size}" viewBox="18 18 72 72"><defs><clipPath id="m{n}"><path d="{MASKS[mask]}"/>'
            f'</clipPath></defs><g clip-path="url(#m{n})">{body}</g></svg>')


rows = []
for wall, fg in (("#1b1f24", "#e8eef0"), ("#eef1f4", "#1b1f24")):
    cells = "".join(f'<div class="c">{icon(s, m)}<span>{m} {s}px</span></div>' for m in MASKS for s in (96, 48) )
    cells += "".join(f'<div class="c">{icon(s, "circle")}<span>{s}px</span></div>' for s in (192, 32))
    rows.append(f'<div class="row" style="background:{wall};color:{fg}">{cells}</div>')
themed = "".join(f'<div class="c">{icon(96, "circle", True, t)}<span>themed</span></div>'
                 for t in (("#1f3b3a", "#a8efe4"), ("#d9ecea", "#16413d"), ("#2a2d3a", "#c9cdf7")))
rows.append(f'<div class="row" style="background:#20262b;color:#e8eef0">{themed}'
            f'<div class="c"><svg width="192" height="192" viewBox="0 0 108 108">{FULL}<circle cx="54" cy="54" r="33" fill="none" '
            'stroke="#ff4d6d" stroke-width=".5" stroke-dasharray="2 1.5"/><rect x="18" y="18" width="72" height="72" fill="none" '
            'stroke="#ffd166" stroke-width=".5" stroke-dasharray="2 1.5"/></svg><span>full 108 canvas, 66 safe circle (red), 72 visible (yellow)</span></div></div>')
html = ('<!doctype html><meta charset="utf-8"><style>body{margin:0;font:13px sans-serif}.row{display:flex;gap:18px;align-items:flex-end;'
        'padding:18px;flex-wrap:wrap}.c{display:flex;flex-direction:column;align-items:center;gap:6px}</style>' + "".join(rows))
open(os.path.join(D, "preview.html"), "w", encoding="utf-8").write(html)
print("wrote preview.html")
