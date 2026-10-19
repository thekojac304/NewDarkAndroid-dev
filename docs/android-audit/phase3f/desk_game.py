"""Desktop run for Phase 3F: copy a profile's configuration and ONE save slot into a scratch write folder, start the
desktop port, load that slot through the menus, then run timed steps (game-state probes and radial actions through the
input script's "game" verb, keys, captures). The source profile and game folder are only read.

    python desk_game.py NAME EXE GAME_DIR PROFILE_DIR SLOT OUT_DIR STEP [STEP ...]

Steps after the load (time advances): wait:MS  shot  game:ACTION (spaces as ~)  tap:KEY  KEY:MS (hold)  cmd:TEXT  click:N  quit
Extra environment: SS2PORT_GAME_PROBE stays off unless the step list starts with "probe-env".
"""
import os, shutil, subprocess, sys
ROOT = r"C:\Dev\NewDarkAndroid"
PY = os.path.join(ROOT, "build", "venv", "Scripts", "python.exe"); GEN = os.path.join(ROOT, "docs", "android-audit", "phase3b", "scp_script.py")
name, exe, game, prof, slot, out = sys.argv[1:7]
steps = sys.argv[7:]
W = os.path.join(out, "write"); shutil.rmtree(out, ignore_errors=True); os.makedirs(W)
for f in os.listdir(prof):
    p = os.path.join(prof, f)
    if os.path.isfile(p) and f.lower() in ("cam.cfg", "user.cfg", "user.bnd", "cam_ext.cfg"): shutil.copy(p, W)
shutil.copytree(os.path.join(prof, f"save_{slot}"), os.path.join(W, f"save_{slot}"))
RES = os.environ.get("DESK_RES")   # e.g. 640x480 when the PC's display is smaller than the profile's game_screen_size
if RES:
    c = os.path.join(W, "cam.cfg"); lines = open(c).read().splitlines() if os.path.exists(c) else []
    lines = [l for l in lines if not l.startswith("game_screen_size")] + ["game_screen_size " + RES.replace("x", " ")]
    open(c, "w").write("\n".join(lines) + "\n")
y = 68 + 20.3 * int(slot)
scr = os.path.join(out, "script.txt")
r = subprocess.run([PY, GEN, scr] + ([f"res:{RES}"] if RES else []) + ["wait:5000", "esc", "wait:1500", "esc", "wait:1500", "esc", "wait:2000", "loadgame" if not RES else "click:%d:%d" % (544 * int(RES.split("x")[0]) // 640, 127 * int(RES.split("x")[1]) // 480), "wait:2500" if RES else "wait:1500",
                    f"menu:600:{y:.1f}", "wait:500", "menu:870:216", "wait:25000"], check=True, capture_output=True, text=True)
t = int(r.stdout.split()[-2]) + 500
L = []
env_probe = bool(steps) and steps[0] == "probe-env"
if env_probe: steps = steps[1:]
for s in steps:
    k, _, v = s.partition(":")
    if k == "wait": t += int(v)
    elif k == "shot": L.append(f"{t} shot"); t += 600
    elif k == "game": L.append(f"{t} game {v.replace('~', ' ')}"); t += 300
    elif k == "tap": L.append(f"{t} tap {v}"); t += 300
    elif k == "cmd":
        for ch in ":" + v.replace("~", " "):
            key = {":": "semicolon", "_": "minus", " ": "space"}.get(ch, ch)
            if ch in ":_": L.append(f"{t} down lshift"); t += 30
            L.append(f"{t} down {key}"); L.append(f"{t} char {'space' if ch == ' ' else ch}"); L.append(f"{t + 40} up {key}")
            if ch in ":_": L.append(f"{t + 60} up lshift")
            t += 80
        L.append(f"{t + 100} tap enter"); t += 700
    elif k == "click": L.append(f"{t} click {v or 1}"); t += 300                   # mouse button 1 (fire) or 2 (use)
    elif k == "quit": L.append(f"{t} quit")
    else: L.append(f"{t} down {k}"); L.append(f"{t + int(v)} up {k}"); t += int(v) + 200
if not any(x.endswith(" quit") for x in L): L.append(f"{t + 500} quit")
open(scr, "a").write("\n".join(L) + "\n")
env = dict(os.environ, SS2PORT_WRITE_DIR=W, SS2PORT_INPUT_SCRIPT=scr, SS2PORT_NOSOUND="1", SS2PORT_NOALERT="1", SS2PORT_SHOT_DIR=os.path.join(out, "shots"))
if env_probe: env["SS2PORT_GAME_PROBE"] = "1"
os.makedirs(os.path.join(out, "shots"), exist_ok=True)
with open(os.path.join(out, "run.log"), "w") as lf:
    rr = subprocess.run([exe, "--windowed"] + (["--trace"] if os.environ.get("DESK_TRACE") else []) + ["ss2.exe"], cwd=game, env=env, stdout=lf, stderr=subprocess.STDOUT, timeout=900)
print(name, "exit", rr.returncode)
