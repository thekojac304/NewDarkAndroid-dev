"""Audio test driver for the legacy-test app: python audio_run.py NAME CFGVARIANT STEP [STEP ...]
Runs the game once against a DISPOSABLE write folder files/audiotest/NAME (never the real profiles/scp4): it is made
fresh from copies of profiles/scp4's cam.cfg, user.cfg, user.bnd and save_0/save_7 only. CFGVARIANT edits the copied cam.cfg
("orig" = unchanged, "VAR=VALUE" = replace that one variable's line, "VAR=" = delete it). SS2PORT_PROFILE=1 is set, so
run.log gets the audio callback statistics every 5 s. While the game runs, `dumpsys audio` and AudioFlinger are sampled.
Pulls SS2.log, run.log, the samples and captures into build/android/legacy-audio/NAME/. Env APP=regular runs the
regular Beta 8 app instead (its own ss2scp profile copied the same way, game folder ss2scp)."""
import os, shutil, subprocess, sys, time
ADB = r"C:\Users\<user>\AppData\Local\Android\Sdk\platform-tools\adb.exe"; S = ["-s", "<device-serial>"]
REG = os.environ.get("APP") == "regular"
P = "local.newdarkandroid.ss2" if REG else "local.newdarkandroid.ss2.legacytest"; D = f"/data/user/0/{P}/files"
GAME = os.environ.get("GAME", "ss2scp" if REG else "scp4"); SRC = f"files/profiles/{GAME}"
ROOT = r"C:\Dev\NewDarkAndroid"; PY = os.path.join(ROOT, "build", "venv", "Scripts", "python.exe")
GEN = os.path.join(ROOT, "docs", "android-audit", "phase3b", "scp_script.py")
name, variant, steps = sys.argv[1], sys.argv[2], sys.argv[3:]
out = os.path.join(ROOT, "build", "android", "legacy-audio", name); shutil.rmtree(out, ignore_errors=True); os.makedirs(out)
def adb(*a, **kw): return subprocess.run([ADB, *S, *a], capture_output=True, text=True, **kw)
def sh(cmd): return adb("shell", cmd).stdout
def ras(cmd): return sh(f"run-as {P} sh -c '{cmd}'")
REAL = os.environ.get("REAL") == "1"     # the real profile, through the launcher's own preparation: nothing is copied or removed
W = SRC if REAL else f"files/audiotest/{name}"
saves = os.environ.get("SAVES", "save_0 save_7").split()
if not REAL: ras(f"rm -rf {W}; mkdir -p {W}; cp {SRC}/cam.cfg {SRC}/user.cfg {W}/; [ -f {SRC}/user.bnd ] && cp {SRC}/user.bnd {W}/; "
    + "".join(f"[ -d {SRC}/{s} ] && cp -r {SRC}/{s} {W}/; " for s in saves) + "true")
if not REAL:
  chk = ras(f"sha256sum {W}/cam.cfg {SRC}/cam.cfg").split()
  if len(chk) != 4 or chk[0] != chk[2]: sys.exit(f"profile copy failed: {chk}")
  print("disposable write folder", W, ":", ras(f"ls {W}").split())
if not REAL:
 cam = subprocess.run([ADB, *S, "exec-out", "run-as", P, "cat", f"{W}/cam.cfg"], capture_output=True).stdout.decode("latin1")
 if variant != "orig":
    var, _, val = variant.partition("="); lines = cam.split("\n"); new = []; hit = False
    for l in lines:
        if l.strip().split(" ")[0].lower() == var.lower(): hit = True; (new.append(f"{var} {val}\r" if l.endswith("\r") else f"{var} {val}") if val else None)
        else: new.append(l)
    if not hit and val: new.insert(len(new) - 1 if new and not new[-1] else len(new), f"{var} {val}\r")
    cam = "\n".join(new)
 open(os.path.join(out, "cam.cfg"), "w", newline="").write(cam)
 adb("push", os.path.join(out, "cam.cfg"), "/data/local/tmp/at-cam.cfg"); sh(f"chmod 644 /data/local/tmp/at-cam.cfg; run-as {P} cp /data/local/tmp/at-cam.cfg {W}/cam.cfg; rm /data/local/tmp/at-cam.cfg")
scr = os.path.join(out, "script.txt"); subprocess.run([PY, GEN, scr, *steps], check=True)
adb("push", scr, "/data/local/tmp/at-script.txt")
sh(f"chmod 644 /data/local/tmp/at-script.txt; run-as {P} sh -c 'cp /data/local/tmp/at-script.txt files/s.txt; rm -rf files/shots; mkdir files/shots'; rm /data/local/tmp/at-script.txt")
sh(f"am force-stop {P}")
env = f"SS2PORT_INPUT_SCRIPT={D}/s.txt,SS2PORT_SHOT_DIR={D}/shots,SS2PORT_PROFILE=1" + ("" if REAL else f",SS2PORT_WRITE_DIR={D}/audiotest/{name}") + (("," + os.environ["ENV"]) if os.environ.get("ENV") else "")
print(sh(f"am start -n {P}/local.newdarkandroid.ss2.LauncherActivity --es autorun game --es game {GAME} --ei frames 0 --es env {env}" + (f" --es portargs {os.environ['PA']}" if os.environ.get("PA") else "")).strip())
time.sleep(8); t0 = time.time(); samples = []
ACTS = [(float(a.split(":")[0]), a.split(":")[1]) for a in os.environ.get("ACTIONS", "").split()]
SAMPLE_AT = [float(x) for x in os.environ.get("SAMPLE_AT", "10 30").split()]
while time.time() - t0 < 300:
    pid = sh(f"pidof {P}:game").strip()
    if not pid: break
    el = time.time() - t0
    if ACTS and el >= ACTS[0][0]:          # ACTIONS="35:home 47:icon": Home key, or the launcher icon (as a tap on it)
        what = ACTS.pop(0)[1]; print(f" t={int(el)}s action {what}")
        if what == "home": sh("input keyevent KEYCODE_HOME")
        elif what == "icon": sh(f"monkey -p {P} -c android.intent.category.LAUNCHER 1")
    if SAMPLE_AT and el >= SAMPLE_AT[0]:
        SAMPLE_AT.pop(0)
        a = sh("dumpsys audio"); f = sh("dumpsys media.audio_flinger")
        open(os.path.join(out, f"dumpsys-audio-{int(el)}s.txt"), "w").write(a)
        open(os.path.join(out, f"audioflinger-{int(el)}s.txt"), "w").write(f)
        samples.append((el, pid))
    time.sleep(1)
else: print("TIMEOUT: still running; force-stopping"); sh(f"am force-stop {P}")
time.sleep(2)
for src, dst in ((f"{W}/SS2.log", "SS2.log"), ("files/runs/game/run.log", "run.log"), (f"{W}/monolog.txt", "monolog.txt"), ("files/results/history.txt", "history.txt")):
    r = subprocess.run([ADB, *S, "exec-out", "run-as", P, "cat", src], capture_output=True)
    if r.returncode == 0: open(os.path.join(out, dst), "wb").write(r.stdout)
os.makedirs(os.path.join(out, "shots"))
for f in sh(f"run-as {P} ls files/shots").split():
    r = subprocess.run([ADB, *S, "exec-out", "run-as", P, "cat", f"files/shots/{f}"], capture_output=True)
    open(os.path.join(out, "shots", f), "wb").write(r.stdout)
if not os.environ.get("KEEPW") and not REAL: ras(f"rm -rf {W}")
try:
    from PIL import Image
    for f in sorted(os.listdir(os.path.join(out, "shots"))):
        p = os.path.join(out, "shots", f)
        if f.endswith(".bmp"): Image.open(p).convert("RGB").save(p[:-4] + ".jpg", quality=85); os.remove(p)
except Exception as e: print("convert:", e)
rl = open(os.path.join(out, "run.log"), "rb").read().decode("latin1") if os.path.exists(os.path.join(out, "run.log")) else ""
for l in rl.splitlines():
    if any(k in l.lower() for k in ("profile:", "directsound", "openal", "sdl audio", "audio:", "dsound")): print(" ", l.strip()[:260])
log = open(os.path.join(out, "SS2.log"), "rb").read().decode("latin1") if os.path.exists(os.path.join(out, "SS2.log")) else ""
for l in log.splitlines():
    if any(k in l.lower() for k in ("openal", "sound", "sfx", "loaded script")): print(" SS2.log:", l.strip())
for el, pid in samples:
    a = open(os.path.join(out, f"dumpsys-audio-{int(el)}s.txt")).read()
    print(f" t={int(el)}s players for pid {pid}:", [l.strip()[:200] for l in a.splitlines() if f"u/pid:" in l and f"/{pid} " in l][:4])
    f = open(os.path.join(out, f"audioflinger-{int(el)}s.txt")).read()
    print(f"   audioflinger tracks:", [l.strip()[:160] for l in f.splitlines() if l.strip().startswith(tuple("0123456789")) and f" {pid} " in f" {l} "][:3])
h = open(os.path.join(out, "history.txt")).read().strip().splitlines() if os.path.exists(os.path.join(out, "history.txt")) else []
print("last history:", h[-1] if h else None)
