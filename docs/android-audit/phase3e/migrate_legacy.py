"""Phase 3E-C: bring the legacy SCP Beta 4 setup of the side-by-side app into the regular app (one APK for both).

    python migrate_legacy.py check                 what would happen; changes nothing
    python migrate_legacy.py run [--allow-running] do every step not yet done; verify every step already done

Source:  local.newdarkandroid.ss2.legacytest   games/scp4 (external), profiles/scp4 (internal), touch-settings.cfg
Target:  local.newdarkandroid.ss2              games/scp4, profiles/scp4, touch-settings.cfg, labels

Rules (PHASE3E_UNIFIED_APK_RESULTS.md §8):
- Nothing is ever deleted, overwritten or moved in the source app. The PC installations are not touched.
- Backups of both apps' internal data (tar) and of their installed APKs are written to build/android/phase3e-preserve/
  first (skipped when they exist).
- A target that exists already is only verified (every file's sha256 equal to the source); if it differs, the script
  stops. Copies go to NAME.copying / NAME.migrating first and are renamed after the check, so an interrupted run never
  leaves a half copy under the real name.
- The profile is not copied while the legacy app's game process runs (it may still write the profile), unless
  --allow-running is given: then the files as saved on disk are copied, and anything that process saves later stays
  only in the legacy app.
- Touch settings: the regular app's file is replaced by the legacy app's only if the regular one holds no choice of the
  player (every control at the current or the previous default position and size, default options, Direct cursor).
  The regular app's file is kept as touch-settings.cfg.before-3e. Otherwise the script stops and says what differs.
- Labels are presentation only (GameFolders.java): they do not choose the NVScript version, the saves or anything else.
Needs adb with run-as (debuggable builds), MSYS_NO_PATHCONV=1 in Git Bash is not needed (no shell rewriting here).
"""
import hashlib, os, re, subprocess, sys, time
ADB = r"C:\Users\<user>\AppData\Local\Android\Sdk\platform-tools\adb.exe"; S = ["-s", "<device-serial>"]
ROOT = r"C:\Dev\NewDarkAndroid"; PRESERVE = os.path.join(ROOT, "build", "android", "phase3e-preserve")
SRC, DST, NAME = "local.newdarkandroid.ss2.legacytest", "local.newdarkandroid.ss2", "scp4"
EXT = "/storage/emulated/0/Android/data"
LABELS = {"scp4": "System Shock 2 \u2014 SCP Beta 4 (Legacy)", "ss2scp": "System Shock 2 \u2014 SCP Beta 8", "ss2": "System Shock 2 (no mods)"}

def adb(*a, binary=False, inp=None):
    r = subprocess.run([ADB, *S, *a], capture_output=True, input=inp)
    return r.stdout if binary else r.stdout.decode("utf-8", "replace")
def sh(c): return adb("shell", c)
def ras(pkg, c): return sh(f"run-as {pkg} sh -c '{c}'")
def say(*a): print(*a, flush=True)
def stop(msg): say("STOP:", msg); sys.exit(1)
def manifest(text):
    m = {}
    for l in text.replace("\r", "").splitlines():
        if not l.strip(): continue
        h, p = l.split(None, 1); p = p.lstrip("*").strip()
        m[p[2:] if p.startswith("./") else p] = h
    return m
def ext_manifest(pkg, rel): return manifest(sh(f"cd {EXT}/{pkg}/files/{rel} && find . -type f -exec sha256sum {{}} +"))
def int_manifest(pkg, rel): return manifest(ras(pkg, f"cd files/{rel} && find . -type f -exec sha256sum {{}} +"))
def running(pkg): return sh(f"pidof {pkg}:game").strip()

def touch_tables():
    c = open(os.path.join(ROOT, "port", "backends", "android", "touch", "touch_settings.c"), encoding="utf-8").read()
    cur = [(n, float(x), float(y)) for n, x, y in re.findall(r'\{"(\w+)", "[^"]*", ([.\d]+)f,([.\d]+)f,[.\d]+f?\}', c)]
    prev = [tuple(map(float, p)) for p in re.findall(r"\{([.\d]+)f,([.\d]+)f\}", c[c.index("previous_defaults"):])]
    return cur, prev
def player_choices(text):
    """what in a touch-settings.cfg is the player's own choice (empty: nothing)"""
    v = dict(l.split("=", 1) for l in text.replace("\r", "").splitlines() if "=" in l)
    f = lambda k, d: float(v.get(k, d))
    out = []
    for k, d in (("opacity", 1), ("sensitivity_x", 1), ("sensitivity_y", 1), ("invert_y", 0), ("haptics_enabled", 0), ("haptic_strength", 1),
                 ("weapon_wheel", 0), ("consumable_wheel", 0), ("explosive_wheel", 0), ("cursor_relative", 0), ("cursor_speed", 1)):
        if abs(f(k, d) - d) > 1e-5: out.append(f"{k}={v.get(k)}")
    cur, prev = touch_tables()
    at = lambda pos: all(abs(f(f"{n}.x", -1) - pos[i][0]) < 1e-5 and abs(f(f"{n}.y", -1) - pos[i][1]) < 1e-5 for i, (n, _, _) in enumerate(cur))
    if not (at([(x, y) for _, x, y in cur]) or at(prev)): out.append("moved controls")
    if any(abs(f(f"{n}.scale", 1) - 1) > 1e-5 for n, _, _ in cur if n != "stick"): out.append("resized controls")
    return out

def backups(do):
    os.makedirs(PRESERVE, exist_ok=True)
    for pkg in (SRC, DST):
        tar = os.path.join(PRESERVE, "data", f"{pkg}.internal.tar"); apk = os.path.join(PRESERVE, "apk", f"{pkg}.installed.apk")
        for path, what in ((tar, "internal data"), (apk, "installed APK")):
            if os.path.exists(path): say(f"  backup of {pkg} {what}: {path} (kept)"); continue
            if not do: say(f"  would back up {pkg} {what} to {path}"); continue
            os.makedirs(os.path.dirname(path), exist_ok=True)
            data = adb("exec-out", f"run-as {pkg} tar cf - files shared_prefs", binary=True) if what == "internal data" else \
                adb("exec-out", "cat " + sh(f"pm path {pkg}").strip().replace("package:", ""), binary=True)
            open(path, "wb").write(data); say(f"  backed up {pkg} {what}: {path} ({len(data):,} bytes, sha256 {hashlib.sha256(data).hexdigest()[:16]}...)")

def game(do):
    src = ext_manifest(SRC, f"games/{NAME}")
    if not src: stop(f"the legacy app has no games/{NAME}")
    if NAME in sh(f"ls {EXT}/{DST}/files/games").split():
        dst = ext_manifest(DST, f"games/{NAME}")
        if dst != src: stop(f"the regular app already has games/{NAME} and it differs from the legacy app's ({len(dst)} vs {len(src)} files)")
        say(f"  games/{NAME}: present, {len(dst)} of {len(src)} files equal (verified)"); return
    if not do: say(f"  would copy games/{NAME} ({len(src)} files) on the phone"); return
    if NAME in sh(f"ls {EXT}/{DST}/files/imported 2>/dev/null").split(): stop(f"imported/{NAME} exists in the regular app")
    sh(f"rm -rf {EXT}/{DST}/files/games/{NAME}.copying; cp -a {EXT}/{SRC}/files/games/{NAME} {EXT}/{DST}/files/games/{NAME}.copying")
    dst = ext_manifest(DST, f"games/{NAME}.copying")
    if dst != src: stop(f"the copy differs from the source ({len(dst)} vs {len(src)} files); left as games/{NAME}.copying")
    sh(f"mv {EXT}/{DST}/files/games/{NAME}.copying {EXT}/{DST}/files/games/{NAME}")
    say(f"  games/{NAME}: copied and verified, {len(dst)} files")

def profile(do, allow_running):
    src = int_manifest(SRC, f"profiles/{NAME}")
    if not src: stop(f"the legacy app has no profiles/{NAME}")
    if NAME in ras(DST, "ls files/profiles").split():
        dst = int_manifest(DST, f"profiles/{NAME}")
        saves = lambda m: {k: v for k, v in m.items() if k.startswith("save_")}
        if saves(dst) != saves(src): stop(f"profiles/{NAME} exists in the regular app and its saves differ from the legacy app's (played in one of them since?); nothing changed")
        say(f"  profiles/{NAME}: present; its {len(saves(dst))} save files equal the legacy app's (verified); other files: "
            f"{sum(1 for k in dst if not k.startswith('save_') and dst[k] != src.get(k))} differ (logs and configuration the game rewrites)"); return
    if running(SRC) and not allow_running: stop(f"the legacy app's game is running (pid {running(SRC)}); end it there (or pass --allow-running)")
    if not do: say(f"  would copy profiles/{NAME} ({len(src)} files)"); return
    data = adb("exec-out", f"run-as {SRC} tar cf - -C files/profiles {NAME}", binary=True)
    ras(DST, f"rm -rf files/profiles/{NAME}.migrating; mkdir -p files/profiles/{NAME}.migrating")
    subprocess.run([ADB, *S, "exec-in", f"run-as {DST} tar xf - -C files/profiles/{NAME}.migrating"], input=data, check=True)
    dst = int_manifest(DST, f"profiles/{NAME}.migrating/{NAME}")
    if dst != src: stop(f"the profile copy differs ({len(dst)} vs {len(src)} files); left in profiles/{NAME}.migrating")
    ras(DST, f"mv files/profiles/{NAME}.migrating/{NAME} files/profiles/{NAME} && rmdir files/profiles/{NAME}.migrating")
    say(f"  profiles/{NAME}: copied and verified, {len(dst)} files")

def touch_settings(do):
    s = adb("exec-out", f"run-as {SRC} cat files/touch-settings.cfg", binary=True); d = adb("exec-out", f"run-as {DST} cat files/touch-settings.cfg", binary=True)
    if not s.startswith(b"version="): say("  touch settings: the legacy app has none; the regular app's are kept"); return
    if s == d: say("  touch settings: already the legacy app's (equal)"); return
    mine = player_choices(d.decode()) if d.startswith(b"version=") else []
    if mine: stop(f"touch settings: the regular app's file has the player's own choices ({', '.join(mine)}); not replaced. Decide which to keep")
    say(f"  touch settings: the regular app's file holds no choice of the player; the legacy app's has: {', '.join(player_choices(s.decode())) or 'none'}")
    if not do: say("  would keep the regular app's as touch-settings.cfg.before-3e and use the legacy app's"); return
    if d and "touch-settings.cfg.before-3e" not in ras(DST, "ls files"):
        subprocess.run([ADB, *S, "exec-in", f"run-as {DST} sh -c 'cat > files/touch-settings.cfg.before-3e'"], input=d, check=True)
    if d and adb("exec-out", f"run-as {DST} cat files/touch-settings.cfg.before-3e", binary=True) != d: stop("touch settings: the backup differs; nothing replaced")
    subprocess.run([ADB, *S, "exec-in", f"run-as {DST} sh -c 'cat > files/touch-settings.cfg.new'"], input=s, check=True)   # exec-in ends with the input:
    if adb("exec-out", f"run-as {DST} cat files/touch-settings.cfg.new", binary=True) != s: stop("touch settings: the new file differs")
    ras(DST, "mv files/touch-settings.cfg.new files/touch-settings.cfg")                                                  # rename separately
    if adb("exec-out", f"run-as {DST} cat files/touch-settings.cfg", binary=True) != s: stop("touch settings: the written file differs")
    say("  touch settings: now the legacy app's (verified); the old one is files/touch-settings.cfg.before-3e")

def labels(do):
    have = sh(f"ls {EXT}/{DST}/files/games").split()
    for g, l in LABELS.items():
        if g not in have: continue
        if not do: say(f"  would label {g}: {l}"); continue
        sh(f"am start -n {DST}/.LauncherActivity --es label_game {g} --es label '{l}'"); time.sleep(1.5)
        say(f"  label {g}: {l}")

def main():
    do = len(sys.argv) > 1 and sys.argv[1] == "run"; allow = "--allow-running" in sys.argv
    if len(sys.argv) < 2 or sys.argv[1] not in ("run", "check"): print(__doc__); sys.exit(2)
    for pkg in (SRC, DST):
        if not sh(f"pm path {pkg}").strip(): stop(f"{pkg} is not installed")
    if running(DST): stop("the regular app's game is running; end it first")
    free = sh("df /storage/emulated/0 | tail -1").split()
    say(f"phone storage free: {int(free[3]) // 1024:,} MB" if len(free) > 3 and free[3].isdigit() else f"phone storage: {free}")
    say("1. backups"); backups(do)
    say("2. game folder"); game(do)
    say("3. profile (saves and configuration)"); profile(do, allow)
    say("4. touch settings"); touch_settings(do)
    say("5. labels"); labels(do)
    say("done" if do else "check only: nothing changed")

if __name__ == "__main__": main()
