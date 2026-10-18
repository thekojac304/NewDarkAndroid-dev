"""Device CROUCH test (Phase 3E-A) in the regular app, through the real touchscreen path.

android/tools/uhid_input -t makes Android see an external HID multitouch touchscreen, so a contact travels Android input
-> TouchSurface -> touch_surface.c -> the touch bridge, as a finger does. The game runs against a DISPOSABLE write folder
(files/crouchtest/<name>: copies of profiles/<game>'s cam.cfg, user.cfg, user.bnd if any, and save_<slot>), so the
real profile is never written. A scripted input loads the slot (SS2PORT_INPUT_SCRIPT); SS2PORT_TOUCH_DEBUG=1 makes the
bridge log every finger and key edge, read live from logcat.

  python crouch_dev.py start GAME SLOT NAME      prepare and start; returns once the save has loaded (mode LOOK)
  python crouch_dev.py touch CMD [CMD ...]       uhid commands (touch ID down X Y | touch ID up | swipe ... | sleep MS)
  python crouch_dev.py crouch [MS]               a tap of MS ms (default 100) on the CROUCH button (its settings position)
  python crouch_dev.py shot NAME                 screencap -> build/android/phase3e-crouch/<run>/NAME.jpg (half size)
  python crouch_dev.py log [N]                   the last N [touch] lines since start
  python crouch_dev.py stop                      end the game, pull run.log, remove the disposable folder
"""
import json, os, re, subprocess, sys, time
ADB = r"C:\Users\<user>\AppData\Local\Android\Sdk\platform-tools\adb.exe"; S = ["-s", "<device-serial>"]
P = "local.newdarkandroid.ss2"; D = f"/data/user/0/{P}/files"
ROOT = r"C:\Dev\NewDarkAndroid"; OUT = os.path.join(ROOT, "build", "android", "phase3e-crouch"); STATE = os.path.join(OUT, "state.json")
GEN = os.path.join(ROOT, "docs", "android-audit", "phase3b", "scp_script.py"); PY = os.path.join(ROOT, "build", "venv", "Scripts", "python.exe")
CMD = "/data/local/tmp/touch.cmd"; W_, H_ = 2340, 1080
def adb(*a, **kw): return subprocess.run([ADB, *S, *a], capture_output=True, text=True, **kw)
def sh(c): return adb("shell", c).stdout
def ras(c): return sh(f"run-as {P} sh -c '{c}'")
def state(): return json.load(open(STATE)) if os.path.exists(STATE) else {}
def save_state(s): os.makedirs(OUT, exist_ok=True); json.dump(s, open(STATE, "w"))
def touch_lines():
    st = state(); log = adb("logcat", "-d", "-v", "time", "-s", "ss2port:I").stdout.splitlines()
    return [l for l in log if "[touch]" in l and l[:18] >= st.get("since", "")]

def crouch_xy():
    cfg = ras("cat files/touch-settings.cfg")
    x = float(re.search(r"crouch\.x=([\d.]+)", cfg).group(1)) if "crouch.x" in cfg else .79
    y = float(re.search(r"crouch\.y=([\d.]+)", cfg).group(1)) if "crouch.y" in cfg else .54
    return round(x * W_), round(y * H_)

def start(game, slot, name):
    W = f"files/crouchtest/{name}"; SRC = f"files/profiles/{game}"
    ras(f"rm -rf files/crouchtest; mkdir -p {W}; for f in cam.cfg user.cfg user.bnd; do [ -f {SRC}/$f ] && cp {SRC}/$f {W}/; done; cp -r {SRC}/save_{slot} {W}/")
    print("write folder", W, ras(f"ls {W}").split())
    run = os.path.join(OUT, name); os.makedirs(run, exist_ok=True); scr = os.path.join(run, "s.txt")
    subprocess.run([PY, GEN, scr, "wait:5000", "esc", "wait:1500", "esc", "wait:1500", "esc", "wait:2000", "loadgame", "wait:1500",
                    f"menu:600:{68 + 20.3 * int(slot):.1f}", "wait:500", "menu:870:216"], check=True, capture_output=True)
    open(scr, "a").write("600000 quit\n")                         # a backstop: the game ends itself after 10 minutes
    adb("push", scr, "/data/local/tmp/cr-s.txt"); sh(f"chmod 644 /data/local/tmp/cr-s.txt; run-as {P} cp /data/local/tmp/cr-s.txt files/s.txt; rm /data/local/tmp/cr-s.txt")
    if "uhid_input" not in sh("ps -A | grep uhid_input"):
        sh(f"rm -f {CMD}; touch {CMD}; nohup /data/local/tmp/uhid_input -t {CMD} > /data/local/tmp/touch.log 2>&1 &"); time.sleep(1.5)
    sh(f"echo 'rotate 90' >> {CMD}")
    sh(f"am force-stop {P}"); adb("logcat", "-c")
    since = time.strftime("%m-%d %H:%M:%S")
    env = f"SS2PORT_TOUCH_DEBUG=1,SS2PORT_WRITE_DIR={D}/crouchtest/{name},SS2PORT_INPUT_SCRIPT={D}/s.txt"
    print(sh(f"am start -n {P}/local.newdarkandroid.ss2.LauncherActivity --es autorun game --es game {game} --ei frames 0 --es env {env}").strip())
    save_state({"name": name, "game": game, "since": since, "run": run})
    for i in range(90):
        time.sleep(1)
        if any("mode LOOK" in l for l in touch_lines()): break
    time.sleep(6)
    for l in touch_lines(): print(" ", l[19:].strip()[:200])

def touch(cmds, quiet=False):
    total = sum(int(c.split()[1]) for c in cmds if c.startswith("sleep"))
    for c in cmds:
        if c.startswith("swipe"):
            p = c.split(); total += int(p[7]) if len(p) > 7 else 0
    n0 = len(touch_lines())
    sh(f"printf '%s\\n' {' '.join(repr(c) for c in cmds)} >> {CMD}")
    time.sleep(total / 1000 + 0.8)
    out = [l[19:].strip()[:200] for l in touch_lines()[n0:]]
    if not quiet:
        for l in out: print(" ", l)
    return out

def shot(name):
    st = state(); png = os.path.join(st["run"], name + ".png")
    data = subprocess.run([ADB, *S, "exec-out", "screencap", "-p"], capture_output=True).stdout
    open(png, "wb").write(data)
    from PIL import Image
    im = Image.open(png).convert("RGB"); im = im.resize((im.width // 2, im.height // 2)); im.save(png[:-4] + ".jpg", quality=85); os.remove(png)
    print(png[:-4] + ".jpg")

def stop():
    st = state(); sh(f"am force-stop {P}"); time.sleep(1)
    r = subprocess.run([ADB, *S, "exec-out", "run-as", P, "cat", "files/runs/game/run.log"], capture_output=True)
    if st.get("run"): open(os.path.join(st["run"], "run.log"), "wb").write(r.stdout)
    ras("rm -rf files/crouchtest"); sh(f"echo quit >> {CMD}")
    print("stopped; disposable folder removed")

if __name__ == "__main__":
    c = sys.argv[1]
    if c == "start": start(sys.argv[2], sys.argv[3], sys.argv[4])
    elif c == "touch": touch(sys.argv[2:])
    elif c == "crouch":
        x, y = crouch_xy(); ms = int(sys.argv[2]) if len(sys.argv) > 2 else 100
        touch([f"touch 0 down {x} {y}", f"sleep {ms}", "touch 0 up", "sleep 400"])
    elif c == "shot": shot(sys.argv[2])
    elif c == "log":
        for l in touch_lines()[-int(sys.argv[2] if len(sys.argv) > 2 else 30):]: print(l[19:].strip()[:200])
    elif c == "stop": stop()
