"""Phone test driver for the legacy-test app: python phone_run.py NAME STEP [STEP ...]
Writes the input script with scp_script.py, runs the game once (game folder scp4, profile scp4) and pulls SS2.log,
run.log, monolog.txt, the captures (as JPEG) and save_15 (if written; then removed from the profile so the slot stays
free) into build/scp4/phone/NAME/. Never writes to save_0..save_14."""
import os, shutil, subprocess, sys, time
ADB = r"C:\Users\<user>\AppData\Local\Android\Sdk\platform-tools\adb.exe"; S = ["-s", "<device-serial>"]
P = "local.newdarkandroid.ss2.legacytest"; D = f"/data/user/0/{P}/files"
ROOT = r"C:\Dev\NewDarkAndroid"; PY = os.path.join(ROOT, "build", "venv", "Scripts", "python.exe")
GEN = os.path.join(ROOT, "docs", "android-audit", "phase3b", "scp_script.py")
name, steps = sys.argv[1], sys.argv[2:]
out = os.path.join(ROOT, "build", "scp4", "phone", name); shutil.rmtree(out, ignore_errors=True); os.makedirs(out)
def adb(*a, **kw): return subprocess.run([ADB, *S, *a], capture_output=True, text=True, **kw)
def sh(cmd): return adb("shell", cmd).stdout
scr = os.path.join(out, "script.txt"); subprocess.run([PY, GEN, scr, *steps], check=True)
if "save_15" in sh(f"run-as {P} ls files/profiles/scp4") and not os.environ.get("ALLOW15"): sys.exit("save_15 exists in the profile; refusing")
for extra in os.environ.get("RAW", "").split(";"):        # extra raw script lines "MS command", e.g. "40000 place 447 40"
    if extra.strip(): open(scr, "a").write(extra.strip() + "\n")
if os.environ.get("RAW"):
    L = sorted((l.split(" ", 1) for l in open(scr).read().splitlines() if l.strip()), key=lambda x: int(x[0])); open(scr, "w").write("".join(f"{a} {b}\n" for a, b in L))
adb("push", scr, "/data/local/tmp/scp4-script.txt")
sh(f"chmod 644 /data/local/tmp/scp4-script.txt; run-as {P} sh -c 'cp /data/local/tmp/scp4-script.txt files/s.txt; rm -rf files/shots; mkdir files/shots'; rm /data/local/tmp/scp4-script.txt")
sh(f"am force-stop {P}")
env = f"SS2PORT_INPUT_SCRIPT={D}/s.txt,SS2PORT_SHOT_DIR={D}/shots,SS2PORT_MODSTATS=1"
print(sh(f"am start -n {P}/local.newdarkandroid.ss2.LauncherActivity --es autorun game --es game scp4 --ei frames 0 --es env {env}").strip())
time.sleep(8); t0 = time.time()
while time.time() - t0 < 600:
    if not sh(f"pidof {P}:game").strip(): break
    time.sleep(3)
else: print("TIMEOUT: still running; force-stopping"); sh(f"am force-stop {P}")
time.sleep(2)
for src, dst in (("files/profiles/scp4/SS2.log", "SS2.log"), ("files/runs/game/run.log", "run.log"), ("files/profiles/scp4/monolog.txt", "monolog.txt"), ("files/results/history.txt", "history.txt")):
    r = subprocess.run([ADB, *S, "exec-out", "run-as", P, "cat", src], capture_output=True)
    if r.returncode == 0: open(os.path.join(out, dst), "wb").write(r.stdout)
shots = sh(f"run-as {P} ls files/shots").split()
os.makedirs(os.path.join(out, "shots"))
for f in shots:
    r = subprocess.run([ADB, *S, "exec-out", "run-as", P, "cat", f"files/shots/{f}"], capture_output=True)
    open(os.path.join(out, "shots", f), "wb").write(r.stdout)
if "save_15" in sh(f"run-as {P} ls files/profiles/scp4"):
    os.makedirs(os.path.join(out, "save_15"))
    for f in sh(f"run-as {P} ls files/profiles/scp4/save_15").split():
        r = subprocess.run([ADB, *S, "exec-out", "run-as", P, "cat", f"files/profiles/scp4/save_15/{f}"], capture_output=True)
        open(os.path.join(out, "save_15", f), "wb").write(r.stdout)
    if os.environ.get("KEEP15"): print("save_15 pulled and kept in the profile")
    else: sh(f"run-as {P} rm -rf files/profiles/scp4/save_15"); print("save_15 pulled and removed from the profile")
try:
    from PIL import Image
    for f in sorted(os.listdir(os.path.join(out, "shots"))):
        p = os.path.join(out, "shots", f)
        if f.endswith(".bmp"): im = Image.open(p).convert("RGB"); im.save(p[:-4] + ".jpg", quality=85); os.remove(p)
except Exception as e: print("convert:", e)
print("shots:", sorted(os.listdir(os.path.join(out, "shots"))))
log = open(os.path.join(out, "SS2.log"), "rb").read().decode("latin1") if os.path.exists(os.path.join(out, "SS2.log")) else ""
for l in log.splitlines():
    if any(k in l for k in ("Loaded script", "SQUIRREL", "PLAYER POS", "Failed", "rror")): print(" ", l.strip())
h = open(os.path.join(out, "history.txt")).read().strip().splitlines() if os.path.exists(os.path.join(out, "history.txt")) else []
print("last history:", h[-1] if h else None)
