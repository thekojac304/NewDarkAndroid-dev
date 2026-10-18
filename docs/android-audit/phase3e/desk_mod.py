"""Desktop module-selection run: copy a profile (cfgs + chosen slots) to a scratch write dir, optionally drop overlay files
into it (e.g. a fake osm/NVScript.osm: the write dir is read before the game folder), load a slot, play a little, quit.
    desk_mod.py NAME EXE GAME PROFILE SLOT OUT [overlay:REL=SRC ...] [steps...]"""
import os, shutil, subprocess, sys
ROOT = r"C:\Dev\NewDarkAndroid"
PY = os.path.join(ROOT, "build", "venv", "Scripts", "python.exe"); GEN = os.path.join(ROOT, "docs", "android-audit", "phase3b", "scp_script.py")
name, exe, game, prof, slot, out = sys.argv[1:7]
over = [a[8:] for a in sys.argv[7:] if a.startswith("overlay:")]; extra = [a for a in sys.argv[7:] if not a.startswith("overlay:")]
W = os.path.join(out, "write"); shutil.rmtree(out, ignore_errors=True); os.makedirs(W)
for f in os.listdir(prof):
    p = os.path.join(prof, f)
    if os.path.isfile(p) and f.lower() not in ("ss2.log", "monolog.txt"): shutil.copy(p, W)
for s in slot.split(","):
    if s != "-": shutil.copytree(os.path.join(prof, f"save_{s}"), os.path.join(W, f"save_{s}"))
for o in over:
    rel, src = o.split("=", 1); dst = os.path.join(W, rel); os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copy(src, dst)
scr = os.path.join(out, "script.txt"); first = slot.split(",")[0]
steps = ["wait:5000", "esc", "wait:1500", "esc", "wait:1500", "esc", "wait:2000"]
if first != "-": steps += ["loadgame", "wait:1500", f"menu:600:{68 + 20.3 * int(first):.1f}", "wait:500", "menu:870:216", "wait:25000", "w:1200", "wait:6000"]
chords = [a[6:] for a in extra if a.startswith("chord:")]; extra = [a for a in extra if not a.startswith("chord:")]
steps += extra + ([] if chords else ["quit"])
r0 = subprocess.run([PY, GEN, scr] + steps, check=True, capture_output=True, text=True)
if chords:                      # chord:MOD+KEY:WAITMS, e.g. chord:lalt+s:4000 (quicksave), then quit
    t = int(r0.stdout.split()[-2]) + 500; L = []
    for c in chords:
        keys, wait = c.rsplit(":", 1); m, k = keys.split("+")
        L += [f"{t} down {m}", f"{t + 60} down {k}", f"{t + 160} up {k}", f"{t + 220} up {m}"]; t += 220 + int(wait)
    L.append(f"{t} quit"); open(scr, "a").write(chr(10).join(L) + chr(10))
env = dict(os.environ, SS2PORT_WRITE_DIR=W, SS2PORT_INPUT_SCRIPT=scr, SS2PORT_NOSOUND="1", SS2PORT_NOALERT="1", SS2PORT_MODSTATS="1")
with open(os.path.join(out, "run.log"), "w") as lf:
    r = subprocess.run([exe, "--windowed", "ss2.exe"], cwd=game, env=env, stdout=lf, stderr=subprocess.STDOUT, timeout=400)
print(name, "exit", r.returncode)
