#!/usr/bin/env python3
"""Writes an SS2PORT_INPUT_SCRIPT for the SCP tests (1280x720 game, 4:3 menus: the cursor is homed, then moved).

    python scp_script.py OUT.txt STEP [STEP ...]

Steps (in order; time advances):
  wait:MS            shot               esc               newgame           loadgame:SLOT
  KEY:MS             hold a key (w a s d, space, ...) for MS ms
  tap:KEY            cmd:TEXT           console command (":" + TEXT + Enter, typed at human speed; '_' for underscores)
  turn:DX[:DY]       relative mouse look   click:X:Y          home the cursor, move to window pixel X,Y, left click
  pos                bind k to log_player_pos once, then each 'pos' presses k (SS2.log gets PLAYER POS)
  shots:N:MS         N captures, MS apart      quit
"""
import sys
L = []
def at(ms, cmd): L.append((ms, cmd))
SHIFTED = {":": "semicolon", "_": "minus"}
def slow_type(ms, text):
    for ch in text:
        key = SHIFTED.get(ch, "space" if ch == " " else ch)
        if ch in SHIFTED: at(ms, "down lshift"); ms += 30
        at(ms, f"down {key}"); at(ms, f"char {'space' if ch == ' ' else ch}"); at(ms + 40, f"up {key}")
        if ch in SHIFTED: at(ms + 60, "up lshift")
        ms += 80
    return ms
def click(ms, x, y): at(ms + 50, f"place {x} {y}"); at(ms + 250, "btn 1 down"); at(ms + 350, "btn 1 up"); return ms + 700
RES = (1280, 720)
def main():
    global RES
    t = 3000; bound = False
    if sys.argv[2].startswith("res:"): RES = tuple(map(int, sys.argv[2][4:].split("x"))); del sys.argv[2]
    for step in sys.argv[2:]:
        k, _, v = step.partition(":")
        if k == "wait": t += int(v)
        elif k == "shot": at(t, "shot"); t += 400
        elif k == "shots":
            n, ms = map(int, v.split(":"))
            for i in range(n): at(t, "shot"); t += ms
        elif k == "esc": at(t, "tap esc"); t += 500
        elif k == "tap": at(t, f"tap {v}"); t += 300
        elif k == "newgame": t = click(t, 989, 75)
        elif k == "loadgame": t = click(t, 976, 190); t += 1000
        elif k == "click": x, y = v.split(":"); t = click(t, int(x), int(y))
        elif k == "menu":   # a point of a 4:3 menu read off a half-size capture (2340x1080 screen; menus are drawn at the same place at any resolution)
            hx, hy = map(float, v.split(":"))
            if RES == (1280, 720): gx = (2 * hx - 210) / 1.5; t = click(t, round((gx - 160) * 4 / 3), round(hy * 4 / 3))
            else: t = click(t, round((2 * hx - 450) / 1440 * RES[0]), round(2 * hy / 1080 * RES[1]))   # 4:3: the menu fills the picture
        elif k == "cmd": t = slow_type(t, ":" + v.replace("~", " ")); at(t + 100, "tap enter"); t += 700
        elif k == "pos":
            if not bound: t = slow_type(t, ":bind k log_player_pos"); at(t + 100, "tap enter"); t += 700; bound = True
            at(t, "down k"); at(t + 40, "up k"); t += 150
        elif k == "turn":
            p = v.split(":"); dx = int(p[0]); dy = int(p[1]) if len(p) > 1 else 0
            for i in range(10): at(t + i * 20, f"move {dx // 10} {dy // 10}")
            t += 400
        elif k == "game":   # a point of the game picture (HUD, inventory, PDA) read off a half-size capture
            hx, hy = map(float, v.split(":"))
            if RES == (1280, 720): t = click(t, round((2 * hx - 210) / 1.5), round(2 * hy / 1.5))
            else: t = click(t, round((2 * hx - 450) / 1440 * RES[0]), round(2 * hy / 1080 * RES[1]))
        elif k == "frob": at(t, "btn 2 down"); at(t + 150, "btn 2 up"); t += 600     # USE: right click at the crosshair
        elif k == "quit": at(t, "quit")
        else: at(t, f"down {k}"); at(t + int(v), f"up {k}"); t += int(v) + 200
    L.sort(key=lambda x: x[0])
    open(sys.argv[1], "w").write("".join(f"{ms} {c}\n" for ms, c in L))
    print(len(L), "lines, ends at", t, "ms")
if __name__ == "__main__": main()
