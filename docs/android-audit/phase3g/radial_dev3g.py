"""Phase 3G device checks of the anchored radial menus, through the real touchscreen path (HID touchscreen from
android/tools/uhid_input -t), on top of Phase 3F's driver (phase3f/radial_dev.py: same disposable write folder
files/radialtest/NAME, same start/stop/shot/log; the real profile is never written).

What changes: the wheel opens around the held button, so the segment targets are computed with a port of
touch_radial_open_at (port/backends/android/touch/touch_radial.c), from the button the finger holds.

  python radial_dev3g.py start GAME SLOT NAME [RES]           as radial_dev.py start
  python radial_dev3g.py wheel BUTTON SEG|center|out [HOLDMS]  hold the button, slide the SAME finger from it to segment
                                                              SEG of its fan (or back to the button, or past the rim), shot, lift
  python radial_dev3g.py walkwheel BUTTON SEG [MS]            the stick held forward the whole time while doing the above
  python radial_dev3g.py geometry BUTTON N                    print the wheel geometry for N entries (no device)
  python radial_dev3g.py tap|touch|shot|log|stop ...          as radial_dev.py
"""
import math, os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "phase3f"))
import radial_dev as rd

rd.OUT = os.path.join(rd.ROOT, "build", "android", "phase3g"); rd.STATE = os.path.join(rd.OUT, "state.json")
W, H = rd.W_, rd.H_
TAU = 2 * math.pi


def visible_arc(cx, cy, R, m):
    N = 180
    v = [m <= cx + math.sin(TAU * i / N) * R <= W - m and m <= cy - math.cos(TAU * i / N) * R <= H - m for i in range(N)]
    if all(v): return 0.0, TAU
    best, first = 0, 0
    for i in range(N):
        if not v[i] or v[(i - 1) % N]: continue
        n = 0
        while n < N and v[(i + n) % N]: n += 1
        if n > best: best, first = n, i
    return TAU * first / N, TAU * (best - 1) / N


def geometry(ax, ay, n):
    """touch_radial_open_at for a finger at the button's centre: (cx, cy, inner, R, start, span); angles clockwise from up."""
    side = min(W, H); m = side * .015; base = side * (.36 if n > 8 else .28); cap = max(base, side * .45)
    inner = max(base * .28, side * .03)
    target, best, g = base * min(.55 * TAU / n, TAU / 4), -1, None
    for k in range(6):
        R = min(base * (1 + .1 * k), cap)
        start, span = visible_arc(ax, ay, R, m)
        if span >= TAU:
            if k == 0: g = (ax, ay, inner, R, -TAU / 2 / n, TAU)
            break
        used = min(span, n * TAU / 4); score = R * used / n
        if score > best: best, g = score, (ax, ay, inner, R, start + (span - used) / 2, used)
        if score >= target or R >= cap: break
    cx, cy, inner, R, start, span = g
    return cx, cy, min(inner, R * .6), R, start, span


def target(btn, n, seg):
    x, y = rd.button(btn)
    cx, cy, inner, R, start, span = geometry(x, y, n)
    a = start + span * (seg % n + .5) / n          # clockwise from up
    d = (inner + R) / 2
    return x, y, cx + math.sin(a) * d, cy - math.cos(a) * d, (cx, cy, inner, R, math.degrees(start), math.degrees(span))


def wheel(btn, where, hold=500, walk_ms=0):
    x, y = rd.button(btn)
    pre = []
    if walk_ms:                                    # the stick, held forward, in a second finger
        sx, sy = rd.button("stick"); r = .11 * H
        pre = [f"touch 1 down {sx} {sy}", f"touch 1 move {sx} {round(sy - r)}", "sleep 300"]
    out = rd.touch(pre + [f"touch 3 down {x} {y}", f"sleep {hold}"], quiet=True, settle=.6)
    n = None
    for l in out:
        mm = re.search(r"wheel \d+: (\d+) entries", l)
        if mm: n = int(mm.group(1))
    for l in out: print(" ", l)
    if where == "center" or not n: tx, ty = x, y
    else:
        _, _, tx, ty, g = target(btn, n, 0 if where == "out" else int(where))
        print("  geometry: centre %.0f,%.0f inner %.0f outer %.0f start %.1f span %.1f" % g)
        if where == "out":                         # straight out past 1.25x the rim, along entry 0
            cx, cy, R = g[0], g[1], g[3]; dx, dy = tx - cx, ty - cy; l = math.hypot(dx, dy)
            tx, ty = cx + dx / l * R * 1.4, cy + dy / l * R * 1.4
            tx, ty = min(max(tx, 2), W - 2), min(max(ty, 2), H - 2)
    steps = [f"touch 3 move {round(x + (tx - x) * k / 6)} {round(y + (ty - y) * k / 6)}" for k in range(1, 7)]
    rd.touch([s for st_ in steps for s in (st_, "sleep 30")] + ["sleep 300"], quiet=True, settle=.2)
    rd.shot(f"wheel-{btn}-{where}" + ("-walking" if walk_ms else ""))
    post = ["touch 3 up", "sleep 400"] + (["touch 1 up"] if walk_ms else []) + ["sleep 1200"]
    for l in rd.touch(post, quiet=True): print(" ", l)


if __name__ == "__main__":
    c = sys.argv[1]
    if c == "wheel": wheel(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 500)
    elif c == "walkwheel": wheel(sys.argv[2], sys.argv[3], 500, int(sys.argv[4]) if len(sys.argv) > 4 else 1)
    elif c == "geometry":
        x, y = rd.DEFAULTS[rd.ALIAS.get(sys.argv[2], sys.argv[2])]; x, y = round(x * W), round(y * H)
        g = geometry(x, y, int(sys.argv[3]))
        print("centre %.0f,%.0f inner %.0f outer %.0f start %.1f span %.1f" % (g[0], g[1], g[2], g[3], math.degrees(g[4]), math.degrees(g[5])))
    elif c == "start":
        res = sys.argv[5] if len(sys.argv) > 5 and re.fullmatch(r"\d+x\d+", sys.argv[5]) else None
        rd.start(sys.argv[2], sys.argv[3], sys.argv[4], res, sys.argv[6 if res else 5:])
    elif c == "touch": rd.touch(sys.argv[2:])
    elif c == "tap":
        x, y = rd.button(sys.argv[2]); ms = int(sys.argv[3]) if len(sys.argv) > 3 else 100
        rd.touch([f"touch 3 down {x} {y}", f"sleep {ms}", "touch 3 up", "sleep 600"])
    elif c == "shot": rd.shot(sys.argv[2])
    elif c == "log":
        for l in rd.lines()[-int(sys.argv[2] if len(sys.argv) > 2 else 30):]: print(l[19:].strip()[:220])
    elif c == "stop": rd.stop()
