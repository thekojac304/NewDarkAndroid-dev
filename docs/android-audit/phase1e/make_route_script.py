#!/usr/bin/env python3
"""Writes an SS2PORT_INPUT_SCRIPT for the Phase 1E grav-shaft tests (PHASE1E_RESULTS.md section 2).

    python make_route_script.py OUT.txt STEP [STEP ...]

The script skips the intro, loads the save in slot 1 from the main menu, binds k to the game's own log_player_pos
command (typed at human speed, because key bindings read the shift state between frames), then performs the steps and
presses k every 250 ms (100 ms while keys are held or jumping), so SS2.log gets a "PLAYER POS: x y z" line per sample.

Steps: KEY:MS holds a key for MS milliseconds (w forward, s back, a/d strafe), wait:MS, jump, turn:UNITS (relative mouse).

The scripts used in Phase 1E (slot 1 = the Phase 1D Earth save "shaft", saved in the x = -30 shaft facing out):
    script-lift-shaft.txt     w:1100 a:1200 s:2200 wait:7000 s:1500 wait:3000 w:400 wait:2000
    script-descent-shaft.txt  wait:1000 jump wait:3000
"""
import sys

L = []
def at(ms, cmd): L.append((ms, cmd))
def click(ms, x, y):                        # the game's cursor follows relative motion: home it, then move to (x, y)
    at(ms, "move -3000 -3000"); at(ms + 50, f"move {x} {y}"); at(ms + 250, "click 1")
SHIFTED = {":": "semicolon", "_": "minus"}
def slow_type(ms, text):
    for ch in text:
        key = SHIFTED.get(ch, "space" if ch == " " else ch)
        if ch in SHIFTED: at(ms, "down lshift"); ms += 30
        at(ms, f"down {key}"); at(ms, f"char {'space' if ch == ' ' else ch}"); at(ms + 40, f"up {key}")
        if ch in SHIFTED: at(ms + 60, "up lshift")
        ms += 80
    return ms
def sample(t0, t1, step=250):
    t = t0
    while t < t1: at(t, "down k"); at(t + 40, "up k"); t += step

def main():
    if len(sys.argv) < 3: sys.exit(__doc__)
    at(4000, "tap esc")                     # skip the intro
    click(9000, 610, 158)                   # main menu: Load Game
    click(10500, 360, 76)                   # slot 1
    click(11500, 717, 241)                  # Load
    t = slow_type(15000, ":bind k log_player_pos")
    at(t + 100, "tap enter"); t += 600
    sample(t, t + 1000); t += 1000
    for step in sys.argv[2:]:
        kind, _, val = step.partition(":")
        if kind == "turn":
            n = int(val); k = 20 if abs(n) > 500 else 1
            for i in range(k): at(t + i * 20, f"move {n // k} 0")
            t += 600; sample(t - 500, t)
        elif kind == "wait": sample(t, t + int(val)); t += int(val)
        elif kind == "jump": at(t, "tap space"); sample(t, t + 3000, 100); t += 3000
        else:
            ms = int(val); at(t, f"down {kind}"); at(t + ms, f"up {kind}"); sample(t, t + ms + 800, 100); t += ms + 800
    at(t + 200, "quit")
    L.sort(key=lambda x: x[0])
    open(sys.argv[1], "w").write("".join(f"{ms} {c}\n" for ms, c in L))
    print(len(L), "lines, ends at", t + 200, "ms")

if __name__ == "__main__": main()
