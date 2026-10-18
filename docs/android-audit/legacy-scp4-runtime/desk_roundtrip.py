"""Desktop round-trip driver: for each slot, load it in the port (null backend), play ~10 s, save into empty slot 15,
archive save_15 + logs under build/scp4/runs/rt-slotN/, remove save_15 again. Never touches save_0..save_14."""
import os, shutil, subprocess, sys
ROOT = r"C:\Dev\NewDarkAndroid"; B = os.path.join(ROOT, "build", "scp4"); W = os.path.join(B, "pc-write")
PY = os.path.join(ROOT, "build", "venv", "Scripts", "python.exe"); GEN = os.path.join(ROOT, "docs", "android-audit", "phase3b", "scp_script.py")
EXE = os.path.join(ROOT, "build", sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].isdigit() else "desktop-win-null-scp4", "ss2port.exe")
slots = [int(s) for s in sys.argv[1:] if s.isdigit()]
for n in slots:
    out = os.path.join(B, "runs", f"rt-slot{n}"); shutil.rmtree(out, ignore_errors=True); os.makedirs(out)
    if os.path.exists(os.path.join(W, "save_15")): sys.exit("save_15 exists; refusing")
    y = 68 + 20.3 * n
    scr = os.path.join(out, "script.txt")
    subprocess.run([PY, GEN, scr, "wait:5000", "esc", "wait:1500", "esc", "wait:1500", "esc", "wait:2000", "loadgame", "wait:1500",
                    f"menu:600:{y:.1f}", "wait:500", "menu:870:216", "wait:25000", "pos", "w:1500", "pos", "wait:8000", "pos", "shot",
                    "esc", "wait:2000", "click:904:360", "wait:2500", "menu:600:374", "wait:1000", "menu:870:216", "wait:5000", "shot", "quit"], check=True)
    env = dict(os.environ, SS2PORT_WRITE_DIR=W, SS2PORT_INPUT_SCRIPT=scr, SS2PORT_NOSOUND="1", SS2PORT_NOALERT="1", SS2PORT_MODSTATS="1",
               SS2PORT_SHOT_DIR=os.path.join(out, "shots"))
    os.makedirs(os.path.join(out, "shots"))
    with open(os.path.join(out, "run.log"), "w") as lf:
        r = subprocess.run([EXE, "--windowed", "ss2.exe"], cwd=os.path.join(B, "game-scp4"), env=env, stdout=lf, stderr=subprocess.STDOUT, timeout=300)
    shutil.copy(os.path.join(W, "SS2.log"), os.path.join(out, "SS2.log"))
    if os.path.exists(os.path.join(W, "monolog.txt")): shutil.copy(os.path.join(W, "monolog.txt"), os.path.join(out, "monolog.txt"))
    ok = os.path.isdir(os.path.join(W, "save_15"))
    if ok: shutil.move(os.path.join(W, "save_15"), os.path.join(out, "save_15"))
    print(f"slot {n}: exit {r.returncode}, save_15 written: {ok}", flush=True)
