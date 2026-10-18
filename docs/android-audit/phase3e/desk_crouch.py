"""Desktop crouch experiment: copy a profile (cfgs + one save slot) to a scratch write dir, load the slot,
press candidate keys, log player position (log_player_pos bound to k via console in the scratch profile only)."""
import os, shutil, subprocess, sys
ROOT = r"C:\Dev\NewDarkAndroid"
PY = os.path.join(ROOT, "build", "venv", "Scripts", "python.exe"); GEN = os.path.join(ROOT, "docs", "android-audit", "phase3b", "scp_script.py")
name, exe, game, prof, slot, out = sys.argv[1:7]
keys = sys.argv[7:]          # e.g. c lshift lctrl
W = os.path.join(out, "write"); shutil.rmtree(out, ignore_errors=True); os.makedirs(W)
for f in os.listdir(prof):
    p = os.path.join(prof, f)
    if os.path.isfile(p) and f.lower() != "ss2.log": shutil.copy(p, W)
shutil.copytree(os.path.join(prof, f"save_{slot}"), os.path.join(W, f"save_{slot}"))
y = 68 + 20.3 * int(slot)
scr = os.path.join(out, "script.txt")
r = subprocess.run([PY, GEN, scr, "wait:5000", "esc", "wait:1500", "esc", "wait:1500", "esc", "wait:2000", "loadgame", "wait:1500",
                    f"menu:600:{y:.1f}", "wait:500", "menu:870:216", "wait:25000", "pos", "wait:500", "pos"], check=True, capture_output=True, text=True)
end = int(r.stdout.split()[-2]) + 500
L = []
def at(ms, c): L.append(f"{ms} {c}")
def pos(t): at(t, "down k"); at(t + 40, "up k")
t = end
def nudge(t):   # a tiny step forward and back, so the physics position settles; then the position
    at(t, "down w"); at(t + 100, "up w"); at(t + 250, "down s"); at(t + 350, "up s"); pos(t + 800)
mode = keys[0]; k = keys[1] if len(keys) > 1 else None
nudge(t); nudge(t + 1000); t += 2000                                  # P1 P2: standing baseline
if mode == "tap":
    at(t, f"down {k}"); at(t + 80, f"up {k}"); t += 1500                 # one short tap
    nudge(t); nudge(t + 1000); nudge(t + 2000); t += 3000             # P3 P4 P5
    at(t, f"down {k}"); at(t + 80, f"up {k}"); t += 1500                 # second tap
    nudge(t); nudge(t + 1000); t += 2000                              # P6 P7
elif mode == "hold":
    at(t, f"down {k}"); t += 1500
    nudge(t); nudge(t + 1000); t += 2000                              # P3 P4 held
    at(t, f"up {k}"); t += 1500
    nudge(t); nudge(t + 1000); t += 2000                              # P5 P6 released
elif mode == "shots":                                                    # stationary: shots only, no movement
    for kk in keys[1:]:
        at(t, "shot"); t += 800                                          # A: before
        at(t, f"down {kk}"); at(t + 80, f"up {kk}"); t += 1800; at(t, "shot"); t += 800      # B: after one tap
        at(t, f"down {kk}"); at(t + 80, f"up {kk}"); t += 1800; at(t, "shot"); t += 800      # C: after a second tap
        at(t, f"down {kk}"); t += 1800; at(t, "shot"); t += 300                               # D: held
        at(t, f"up {kk}"); t += 1800; at(t, "shot"); t += 800                                 # E: released
elif mode == "walk":                                                     # speed with the key held vs not
    pos(t); at(t + 100, "down w"); at(t + 1100, "up w"); pos(t + 1600); t += 2000          # P3 P4 plain
    at(t, f"down {k}"); pos(t + 300); at(t + 400, "down w"); at(t + 1400, "up w"); pos(t + 1900); at(t + 2100, f"up {k}"); t += 2600   # P5 P6
at(t, "quit")
open(scr, "a").write("\n".join(L) + "\n")
env = dict(os.environ, SS2PORT_WRITE_DIR=W, SS2PORT_INPUT_SCRIPT=scr, SS2PORT_NOSOUND="1", SS2PORT_NOALERT="1", SS2PORT_SHOT_DIR=os.path.join(out, "shots"))
os.makedirs(os.path.join(out, "shots"), exist_ok=True)
with open(os.path.join(out, "run.log"), "w") as lf:
    rr = subprocess.run([exe, "--windowed", "ss2.exe"], cwd=game, env=env, stdout=lf, stderr=subprocess.STDOUT, timeout=400)
print(name, "exit", rr.returncode)
