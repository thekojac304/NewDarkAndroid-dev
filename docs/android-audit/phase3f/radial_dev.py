"""Phase 3F device tests (radial menus) in the regular app, through the real touchscreen path.

android/tools/uhid_input -t makes Android see an external HID multitouch touchscreen, so a contact travels Android input
-> TouchSurface -> touch_surface.c -> the touch bridge, as a finger does. The game runs against a DISPOSABLE write folder
(files/radialtest/<name>: copies of profiles/<game>'s cam.cfg, user.cfg, user.bnd if any, and save_<slot>), so the real
profile is never written; a quicksave there writes only that folder. SS2PORT_TOUCH_DEBUG=1 and SS2PORT_GAME_DEBUG=1 log
every finger, wheel, selection, game state read and game command, read live from logcat.

  python radial_dev.py start GAME SLOT NAME [RES] [SCRIPT...]   prepare, start, wait for LOOK. RES e.g. 1280x720 (the
                                                                cam.cfg's game_screen_size); SCRIPT: "game ..." lines run
                                                                after the load (test-only give/grant_psi/cmd included)
  python radial_dev.py touch CMD [CMD ...]       uhid commands (touch ID down X Y | touch ID move X Y | touch ID up | sleep MS)
  python radial_dev.py tap BUTTON [MS]           a tap on a control (prev next ammo explosive pause consumable mode ...)
  python radial_dev.py wheel BUTTON SEG|center|out [HOLDMS]   hold the button, wait for its wheel, move to segment SEG of
                                                                it (or the centre, or outside), shot "wheel-NAME", release
  python radial_dev.py stick DX DY MS            hold the stick displaced by DX, DY (fractions of its radius) for MS
  python radial_dev.py shot NAME                 screencap -> build/android/phase3f/<run>/NAME.jpg (half size)
  python radial_dev.py log [N]                   the last N [touch]/[ss2game] lines since start
  python radial_dev.py stop                      end the game, pull run.log, remove the disposable folder
"""
import json, math, os, re, subprocess, sys, time
ADB = r"C:\Users\<user>\AppData\Local\Android\Sdk\platform-tools\adb.exe"; S = ["-s", "<device-serial>"]
P = "local.newdarkandroid.ss2"; D = f"/data/user/0/{P}/files"
ROOT = r"C:\Dev\NewDarkAndroid"; OUT = os.path.join(ROOT, "build", "android", "phase3f"); STATE = os.path.join(OUT, "state.json")
GEN = os.path.join(ROOT, "docs", "android-audit", "phase3b", "scp_script.py"); PY = os.path.join(ROOT, "build", "venv", "Scripts", "python.exe")
CMD = "/data/local/tmp/touch.cmd"; W_, H_ = 2340, 1080
DEFAULTS = {"fire": (.86, .73), "use": (.69, .85), "jump": (.95, .52), "crouch": (.79, .54), "previous": (.55, .78), "next": (.55, .90),
            "consumable": (.52, .10), "explosive": (.045, .47), "target": (.955, .25), "pause": (.05, .06), "mode": (.905, .0825),
            "settings": (.045, .19), "stick": (.20, .77), "ammo": (.045, .33)}
ALIAS = {"prev": "previous", "<": "previous", ">": "next", "item": "explosive", "ii": "pause", "reload": "consumable", "set": "settings", "kbd": "target"}
def adb(*a, **kw): return subprocess.run([ADB, *S, *a], capture_output=True, text=True, **kw)
def sh(c): return adb("shell", c).stdout
def ras(c): return sh(f"run-as {P} sh -c '{c}'")
def state(): return json.load(open(STATE)) if os.path.exists(STATE) else {}
def save_state(s): os.makedirs(OUT, exist_ok=True); json.dump(s, open(STATE, "w"))
def lines():
    st = state(); log = adb("logcat", "-d", "-v", "time", "-s", "ss2port:I").stdout.splitlines()
    return [l for l in log if ("[touch]" in l or "[ss2game]" in l) and l[:18] >= st.get("since", "")]
def button(name):
    name = ALIAS.get(name, name); cfg = state().get("cfg", "")
    x = re.search(rf"^{name}\.x=([\d.]+)", cfg, re.M); y = re.search(rf"^{name}\.y=([\d.]+)", cfg, re.M)
    fx, fy = (float(x.group(1)), float(y.group(1))) if x and y else DEFAULTS[name]
    return round(fx * W_), round(fy * H_)

def start(game, slot, name, res=None, script=()):
    W = f"files/radialtest/{name}"; SRC = f"files/profiles/{game}"
    ras(f"rm -rf files/radialtest; mkdir -p {W}; for f in cam.cfg user.cfg user.bnd; do [ -f {SRC}/$f ] && cp {SRC}/$f {W}/; done; cp -r {SRC}/save_{slot} {W}/")
    if res: ras(f"grep -v ^game_screen_size {W}/cam.cfg > {W}/c.tmp; echo game_screen_size {res.replace('x', ' ')} >> {W}/c.tmp; mv {W}/c.tmp {W}/cam.cfg")
    print("write folder", W, ras(f"ls {W}").split(), ras(f"grep game_screen_size {W}/cam.cfg").strip())
    run = os.path.join(OUT, name); os.makedirs(run, exist_ok=True); scr = os.path.join(run, "s.txt")
    args = [PY, GEN, scr] + ([f"res:{res}"] if res else []) + ["wait:5000", "esc", "wait:1500", "esc", "wait:1500", "esc", "wait:2000", "loadgame", "wait:1500",
                    f"menu:600:{68 + 20.3 * int(slot):.1f}", "wait:500", "menu:870:216"]
    r = subprocess.run(args, check=True, capture_output=True, text=True); t = int(r.stdout.split()[-2]) + 25000
    with open(scr, "a") as f:
        for i, g in enumerate(script): f.write(f"{t + 300 * i} game {g}\n")
        f.write("900000 quit\n")                                   # a backstop: the game ends itself after 15 minutes
    adb("push", scr, "/data/local/tmp/rd-s.txt"); sh(f"chmod 644 /data/local/tmp/rd-s.txt; run-as {P} cp /data/local/tmp/rd-s.txt files/s.txt; rm /data/local/tmp/rd-s.txt")
    if "uhid_input" not in sh("ps -A | grep uhid_input"):
        sh(f"rm -f {CMD}; touch {CMD}; nohup /data/local/tmp/uhid_input -t {CMD} > /data/local/tmp/touch.log 2>&1 &"); time.sleep(1.5)
    sh(f"echo 'rotate 90' >> {CMD}")
    sh(f"am force-stop {P}"); adb("logcat", "-c")
    since = time.strftime("%m-%d %H:%M:%S")
    env = f"SS2PORT_TOUCH_DEBUG=1,SS2PORT_GAME_DEBUG=1,SS2PORT_WRITE_DIR={D}/radialtest/{name},SS2PORT_INPUT_SCRIPT={D}/s.txt"
    print(sh(f"am start -n {P}/local.newdarkandroid.ss2.LauncherActivity --es autorun game --es game {game} --ei frames 0 --es env {env}").strip())
    save_state({"name": name, "game": game, "since": since, "run": run, "cfg": ras("cat files/touch-settings.cfg")})
    for i in range(120):
        time.sleep(1)
        if any("mode LOOK" in l for l in lines()): break
    time.sleep(8 + 0.3 * len(script))
    for l in lines(): print(" ", l[19:].strip()[:200])

def touch(cmds, quiet=False, settle=.8):
    total = sum(int(c.split()[1]) for c in cmds if c.startswith("sleep"))
    n0 = len(lines())
    sh(f"printf '%s\\n' {' '.join(repr(c) for c in cmds)} >> {CMD}")
    time.sleep(total / 1000 + settle)
    out = [l[19:].strip()[:200] for l in lines()[n0:]]
    if not quiet:
        for l in out: print(" ", l)
    return out

def wheel(btn, where, hold=500):
    x, y = button(btn)
    out = touch([f"touch 5 down {x} {y}", f"sleep {hold}"], quiet=True, settle=.6)
    n = None
    for l in out:
        m = re.search(r"wheel \d+: (\d+) entries", l)
        if m: n = int(m.group(1))
    for l in out: print(" ", l)
    cx, cy = W_ * .5, H_ * .51
    if where == "center" or not n: tx, ty = cx, cy
    elif where == "out": tx, ty = cx, max(5, cy - H_ * .49)
    else:
        R = min(W_, H_) * (.36 if n > 8 else .28); seg = int(where) % n
        a = 2 * math.pi * seg / n - math.pi / 2; d = R * .64
        tx, ty = cx + math.cos(a) * d, cy + math.sin(a) * d
    steps = [f"touch 5 move {round(x + (tx - x) * k / 6)} {round(y + (ty - y) * k / 6)}" for k in range(1, 7)]
    touch([s for st_ in steps for s in (st_, "sleep 30")] + ["sleep 300"], quiet=True, settle=.2)
    shot(f"wheel-{btn}-{where}")
    for l in touch(["touch 5 up", "sleep 1500"], quiet=True): print(" ", l)

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
    ras("rm -rf files/radialtest files/s.txt"); sh(f"echo quit >> {CMD}")
    print("stopped; disposable folder removed")

if __name__ == "__main__":
    c = sys.argv[1]
    if c == "start":
        res = sys.argv[5] if len(sys.argv) > 5 and re.fullmatch(r"\d+x\d+", sys.argv[5]) else None
        start(sys.argv[2], sys.argv[3], sys.argv[4], res, sys.argv[6 if res else 5:])
    elif c == "touch": touch(sys.argv[2:])
    elif c == "tap":
        x, y = button(sys.argv[2]); ms = int(sys.argv[3]) if len(sys.argv) > 3 else 100
        touch([f"touch 5 down {x} {y}", f"sleep {ms}", "touch 5 up", "sleep 600"])
    elif c == "wheel": wheel(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 500)
    elif c == "stick":
        x, y = button("stick"); r = .11 * H_; dx, dy, ms = float(sys.argv[2]), float(sys.argv[3]), int(sys.argv[4])
        touch(["touch 1 down %d %d" % (x, y), "touch 1 move %d %d" % (x + dx * r, y + dy * r), f"sleep {ms}", "touch 1 up", "sleep 300"])
    elif c == "shot": shot(sys.argv[2])
    elif c == "log":
        for l in lines()[-int(sys.argv[2] if len(sys.argv) > 2 else 30):]: print(l[19:].strip()[:220])
    elif c == "stop": stop()
