"""Device driver for the relative-cursor acceptance tests (Phase 3D-B), legacy-test app on the S26 Ultra.

Input goes through the real touchscreen path: android/tools/uhid_input -t makes Android see an external HID multitouch
touchscreen, so contacts travel Android input -> TouchSurface -> touch_surface.c -> the touch bridge, as a finger does.
The game runs against a DISPOSABLE write folder (files/cursortest/<name>: copies of profiles/scp4's cam.cfg, user.cfg,
user.bnd, save_0 and save_7), so the real profile is never written. SS2PORT_TOUCH_DEBUG=1 makes the bridge log every
finger, cursor placement and button edge; they are read live from logcat.

  python cursor_dev.py start NAME [WxH]     prepare, start the touchscreen and the game (game resolution WxH, default 1280x720)
  python cursor_dev.py touch CMD [CMD ...]  uhid commands (touch ID down X Y | touch ID move X Y | touch ID up | swipe ... | sleep MS)
  python cursor_dev.py shot NAME            screencap -> build/android/relative-cursor/<run>/NAME.jpg (half size)
  python cursor_dev.py log [N]              the last N [touch] lines since start
  python cursor_dev.py key KEYCODE          adb shell input keyevent (HOME and so on)
  python cursor_dev.py stop                 end the game, pull run.log, remove the disposable folder
"""
import json, os, re, subprocess, sys, time
ADB = r"C:\Users\<user>\AppData\Local\Android\Sdk\platform-tools\adb.exe"; S = ["-s", "<device-serial>"]
P = "local.newdarkandroid.ss2.legacytest"; D = f"/data/user/0/{P}/files"; SRC = "files/profiles/scp4"
ROOT = r"C:\Dev\NewDarkAndroid"; OUT = os.path.join(ROOT, "build", "android", "relative-cursor"); STATE = os.path.join(OUT, "state.json")
CMD = "/data/local/tmp/touch.cmd"
def adb(*a, **kw): return subprocess.run([ADB, *S, *a], capture_output=True, text=True, **kw)
def sh(c): return adb("shell", c).stdout
def ras(c): return sh(f"run-as {P} sh -c '{c}'")
def state(): return json.load(open(STATE)) if os.path.exists(STATE) else {}
def save_state(s): os.makedirs(OUT, exist_ok=True); json.dump(s, open(STATE, "w"))
def touch_lines():
    st = state(); log = adb("logcat", "-d", "-v", "time", "-s", "ss2port:I").stdout.splitlines()
    return [l for l in log if "[touch]" in l and l[:18] >= st.get("since", "")]

def start(name, res="1280x720"):
    w, h = res.split("x"); W = f"files/cursortest/{name}"
    ras(f"rm -rf files/cursortest; mkdir -p {W}; cp {SRC}/cam.cfg {SRC}/user.cfg {SRC}/user.bnd {W}/; cp -r {SRC}/save_0 {SRC}/save_7 {W}/")
    cam = subprocess.run([ADB, *S, "exec-out", "run-as", P, "cat", f"{W}/cam.cfg"], capture_output=True).stdout.decode("latin1")
    cam = re.sub(r"(?m)^game_screen_size .*$", f"game_screen_size {w} {h}\r", cam.replace("\r\n", "\n")).replace("\r\r", "\r")
    cam = cam.replace("\n", "\r\n").replace("\r\r\n", "\r\n")
    tmp = os.path.join(OUT, "cam.cfg"); os.makedirs(OUT, exist_ok=True); open(tmp, "w", newline="").write(cam)
    adb("push", tmp, "/data/local/tmp/cd-cam.cfg"); sh(f"chmod 644 /data/local/tmp/cd-cam.cfg; run-as {P} cp /data/local/tmp/cd-cam.cfg {W}/cam.cfg; rm /data/local/tmp/cd-cam.cfg")
    print("write folder", W, ras(f"ls {W}").split(), "|", ras(f"grep game_screen_size {W}/cam.cfg").strip())
    open(os.path.join(OUT, "s.txt"), "w").write("600000 quit\n")        # a backstop: the game ends itself after 10 minutes
    adb("push", os.path.join(OUT, "s.txt"), "/data/local/tmp/cd-s.txt"); sh(f"chmod 644 /data/local/tmp/cd-s.txt; run-as {P} cp /data/local/tmp/cd-s.txt files/s.txt")
    if "uhid_input" not in sh("ps -A | grep uhid_input"):
        sh(f"rm -f {CMD}; touch {CMD}; nohup /data/local/tmp/uhid_input -t {CMD} > /data/local/tmp/touch.log 2>&1 &"); time.sleep(1.5)
    sh(f"echo 'rotate 90' >> {CMD}")
    sh(f"am force-stop {P}"); adb("logcat", "-c")
    since = time.strftime("%m-%d %H:%M:%S")
    env = f"SS2PORT_TOUCH_DEBUG=1,SS2PORT_WRITE_DIR={D}/cursortest/{name},SS2PORT_INPUT_SCRIPT={D}/s.txt"
    print(sh(f"am start -n {P}/local.newdarkandroid.ss2.LauncherActivity --es autorun game --es game scp4 --ei frames 0 --es env {env}").strip())
    run = os.path.join(OUT, name); os.makedirs(run, exist_ok=True)
    save_state({"name": name, "res": res, "since": since, "run": run})
    for i in range(40):
        time.sleep(1)
        if any("mode CURSOR" in l for l in touch_lines()): break
    time.sleep(4)
    for l in touch_lines(): print(" ", l[19:].strip()[:200])

def touch(cmds):
    total = sum(int(c.split()[1]) for c in cmds if c.startswith("sleep"))
    for c in cmds:
        if c.startswith("swipe"):
            p = c.split(); total += int(p[7]) if len(p) > 7 else 0
    lines = "\n".join(cmds).replace("'", "")
    n0 = len(touch_lines())
    sh(f"printf '%s\\n' {' '.join(repr(c) for c in cmds)} >> {CMD}")
    time.sleep(total / 1000 + 0.8)
    for l in touch_lines()[n0:]: print(" ", l[19:].strip()[:200])

def shot(name):
    st = state(); png = os.path.join(st["run"], name + ".png")
    data = subprocess.run([ADB, *S, "exec-out", "screencap", "-p"], capture_output=True).stdout
    open(png, "wb").write(data)
    try:
        from PIL import Image
        im = Image.open(png).convert("RGB"); im = im.resize((im.width // 2, im.height // 2)); im.save(png[:-4] + ".jpg", quality=85); os.remove(png)
        print(png[:-4] + ".jpg")
    except Exception as e: print("convert:", e, png)

def stop():
    st = state(); sh(f"am force-stop {P}"); time.sleep(1)
    r = subprocess.run([ADB, *S, "exec-out", "run-as", P, "cat", "files/runs/game/run.log"], capture_output=True)
    if st.get("run"): open(os.path.join(st["run"], "run.log"), "wb").write(r.stdout)
    t = touch_lines()
    if st.get("run"): open(os.path.join(st["run"], "touch-log.txt"), "w").write("\n".join(t) + "\n")
    ras("rm -rf files/cursortest"); print("stopped;", len(t), "touch lines saved")

def where():
    """The cursor (window pixels), the picture and the engine's 2D rectangle, from the bridge's log."""
    st = state(); gw, gh = map(int, st["res"].split("x")); cur = ui = None
    for l in touch_lines():
        m = re.search(r"cursor at (\d+),(\d+)", l)
        if m: cur = (int(m.group(1)), int(m.group(2)))
        m = re.search(r"game 2D picture (\d+),(\d+) (\d+)x(\d+) of", l)
        if m: ui = tuple(map(int, m.groups())); ui = None if "whole picture" in l else ui
    pw = 1080 * gw / gh if gw / gh < 2340 / 1080 else 2340; ph = pw * gh / gw; px = (2340 - pw) / 2; py = (1080 - ph) / 2
    return cur, ui or (0, 0, gw, gh), (px, py, pw, ph), (gw, gh)
def to_screen(wx, wy):
    cur, ui, pic, (gw, gh) = where()
    gx = ui[0] + wx * ui[2] / gw; gy = ui[1] + wy * ui[3] / gh       # the engine maps the whole window onto its 2D picture
    return pic[0] + gx * pic[2] / gw, pic[1] + gy * pic[3] / gh
def goto(sx, sy, speed=1.0, fx=1170, fy=540, steps=12, ms=240):
    """Move the cursor to screen point sx, sy with one relative swipe from fx, fy (the finger starts moving at once)."""
    cur = where()[0]
    if cur is None: sys.exit("cursor position unknown: make a first move")
    cx, cy = to_screen(*cur); dx, dy = (sx - cx) / speed, (sy - cy) / speed
    print(f"cursor window {cur} = screen {cx:.0f},{cy:.0f}; target {sx},{sy}: swipe {dx:+.0f},{dy:+.0f}")
    if abs(dx) > 2200 or abs(dy) > 1000:                     # longer than the screen: two strokes, like a person would
        goto(cx + (sx - cx) / 2, cy + (sy - cy) / 2, speed); return goto(sx, sy, speed)
    fx = min(max(fx, 40 - dx), 2300 - dx); fy = min(max(fy, 40 - dy), 1040 - dy)   # the finger stays on the screen
    touch([f"touch 0 down {fx:.0f} {fy:.0f}", f"swipe 0 {fx:.0f} {fy:.0f} {fx + dx:.0f} {fy + dy:.0f} {steps} {ms}", "sleep 100", "touch 0 up", "sleep 400"])
    cur = where()[0]; print("now window", cur, "= screen %.0f,%.0f" % to_screen(*cur))
def tap(x=1170, y=700, hold=60):
    touch([f"touch 0 down {x} {y}", f"sleep {hold}", "touch 0 up", "sleep 600"])

if __name__ == "__main__":
    c = sys.argv[1]
    if c == "goto": goto(float(sys.argv[2]), float(sys.argv[3]), *(float(v) for v in sys.argv[4:5]))
    if c == "tap": tap(*(int(v) for v in sys.argv[2:5]))
    if c == "start": start(sys.argv[2], *(sys.argv[3:4]))
    elif c == "touch": touch(sys.argv[2:])
    elif c == "shot": shot(sys.argv[2])
    elif c == "log":
        for l in touch_lines()[-int(sys.argv[2] if len(sys.argv) > 2 else 30):]: print(l[19:].strip()[:220])
    elif c == "key": print(sh(f"input keyevent {sys.argv[2]}"))
    elif c == "stop": stop()
