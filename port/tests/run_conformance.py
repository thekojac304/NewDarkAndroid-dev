#!/usr/bin/env python3
"""run_conformance.py: builds the guest conformance test (tests/guest/conformance.c) as a 32-bit Windows executable,
recompiles it with lift.py, builds the portable host around it and runs it. No game files are needed.

    python3 port/tests/run_conformance.py [--backend null|sdl2] [--build DIR] [--skip SECTION ...] [--cc CC] [--cflags ...]
    python3 port/tests/run_conformance.py --target windows --wrap wine        # Windows exe, run under Wine (or on Windows)
    python3 port/tests/run_conformance.py --cache-regs                        # lift with lift.py --cache-regs
    python3 port/tests/run_conformance.py --target android [--serial ID]     # arm64 Android: NDK build, run on a device via adb
    python3 port/tests/run_conformance.py --write-dir [--target ...]          # only the "writedir" section: listings with SS2PORT_WRITE_DIR

Needs: python3 -m pip install pefile capstone ziglang, CMake, a host C compiler (and SDL2 for --backend sdl2).
--target android needs the Android NDK (--ndk, ANDROID_NDK_ROOT, or the newest under ANDROID_HOME/ndk; CMake and Ninja
come from the SDK when not on PATH) and adb with one device attached (or --serial); it builds the host with the NDK's
clang for arm64-v8a, pushes it with the guest to --device-dir and runs it there with adb shell.
--write-dir prepares a game folder and a separate write folder (WRITEDIR_LAYOUT), runs only the guest's "writedir" section
with SS2PORT_WRITE_DIR set, and afterwards checks that the game folder was not changed.
Exit status: 0 when every check passes."""
import argparse, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__)); PORT = os.path.dirname(HERE); ROOT = os.path.dirname(PORT)
GUEST = os.path.join(HERE, "guest")
PY = sys.executable

# --write-dir: (folder, path, contents); folder "game" is the guest's working directory, "write" is SS2PORT_WRITE_DIR.
# tests/guest/conformance.c (test_writedir) expects exactly this.
WRITEDIR_LAYOUT = [
    ("game", "ctest_wd/a.txt", b"game a"), ("game", "ctest_wd/both.txt", b"game"), ("game", "ctest_wd/Mixed.TXT", b"game mixed"),
    ("game", "ctest_wd/game.bin", b"12345678"), ("game", "ctest_wd/gsub/g.txt", b"g"),
    ("write", "ctest_wd/both.txt", b"write both!"), ("write", "ctest_wd/MIXED.txt", b"write mixed!!"), ("write", "ctest_wd/w_only.txt", b"w"),
    ("write", "ctest_wd/wsub/w.txt", b"ww"), ("write", "wonly_dir/x.txt", b"x"),
]
def make_writedir_layout(game, write):
    for side, rel, data in WRITEDIR_LAYOUT:
        p = os.path.join(game if side == "game" else write, *rel.split("/")); os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f: f.write(data)
def snapshot(d):
    return sorted((os.path.relpath(os.path.join(r, f), d), open(os.path.join(r, f), "rb").read()) for r, _, fs in os.walk(d) for f in fs)

def run(cmd, **kw):
    r = subprocess.run(cmd, **kw)
    if r.returncode: sys.exit(f"failed ({r.returncode}): {' '.join(cmd)}")

def zig_libc():
    import ziglang; return os.path.join(os.path.dirname(ziglang.__file__), "lib", "libc")

def build_guest(bdir):
    """conformance.exe: freestanding, x87 only (the recompiler does not translate SSE), at the classic 0x400000 base."""
    gd = os.path.join(bdir, "guest"); os.makedirs(gd, exist_ok=True)
    # import library for MSVCR90.dll from the prototypes in crt90.h
    names = re.findall(r"\bIMP\b[^;(]*?\b(\w+)\s*\(", open(os.path.join(GUEST, "crt90.h")).read())
    with open(os.path.join(gd, "msvcr90.def"), "w") as f: f.write("LIBRARY MSVCR90.dll\nEXPORTS\n" + "".join(n + "\n" for n in names))
    zig = [PY, "-m", "ziglang"]
    run(zig + ["dlltool", "-d", os.path.join(gd, "msvcr90.def"), "-l", os.path.join(gd, "libmsvcr90.a"), "-m", "i386"])
    lc = zig_libc(); exe = os.path.join(gd, "conformance.exe")
    run(zig + ["cc", "-target", "x86-windows-gnu", "-O1", "-march=pentium", "-mno-sse", "-mno-mmx", "-fno-stack-protector", "-fno-builtin", "-Wno-dll-attribute-on-redeclaration", "-Wno-incompatible-library-redeclaration",
               "-nostdlib", "-isystem", os.path.join(lc, "include", "any-windows-any"), "-isystem", os.path.join(lc, "include", "generic-mingw"),
               "-Wl,--entry=start@0", "-Wl,--image-base=0x400000", os.path.join(GUEST, "conformance.c"), "-L", gd,
               "-lmsvcr90", "-lkernel32", "-luser32", "-lwinmm", "-o", exe])
    return exe

def lift(exe, bdir, extra=()):
    gen = os.path.join(bdir, "gen"); nd = os.path.join(gen, "nd")
    if os.path.isdir(gen): shutil.rmtree(gen)
    run([PY, os.path.join(ROOT, "lift.py")] + list(extra) + [exe, "nd", nd], stdout=subprocess.DEVNULL)
    run([PY, os.path.join(ROOT, "host", "gen_hostdata.py"), os.path.join(nd, "nd_meta.json"), exe, os.path.join(nd, "nd_hostdata.c")], stdout=subprocess.DEVNULL)
    os.makedirs(os.path.join(gen, "mods"))
    with open(os.path.join(gen, "mods", "mods.c"), "w") as f: f.write('#include "recomp_mod.h"\nconst RecompModDesc *const recomp_mods[] = { 0 };\n')
    return gen

def sdk_tool(name):
    """name from PATH, else from the Android SDK (platform-tools, or the newest cmake/<version>/bin)"""
    p = shutil.which(name)
    if p: return p
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT") or ""; exe = name + (".exe" if os.name == "nt" else "")
    cm = os.path.join(sdk, "cmake"); vers = sorted(os.listdir(cm), reverse=True) if os.path.isdir(cm) else []
    return next((c for c in [os.path.join(sdk, "platform-tools", exe)] + [os.path.join(cm, v, "bin", exe) for v in vers] if os.path.isfile(c)), None)

def android_ndk(a):
    ndk = a.ndk or os.environ.get("ANDROID_NDK_ROOT") or os.environ.get("ANDROID_NDK_HOME")
    if not ndk:
        root = os.path.join(os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT") or "", "ndk")
        vers = sorted(os.listdir(root), key=lambda v: [int(x) for x in re.findall(r"\d+", v)]) if os.path.isdir(root) else []
        ndk = os.path.join(root, vers[-1]) if vers else None
    if not ndk or not os.path.isfile(os.path.join(ndk, "build", "cmake", "android.toolchain.cmake")): sys.exit("--target android: no Android NDK found (use --ndk)")
    return ndk

def build_host(gen, bdir, a):
    win = a.target == "windows"; android = a.target == "android"
    hb = os.path.join(bdir, "host-" + a.backend + ("-win" if win else "-android" if android else ""))
    cmake, ninja = (sdk_tool("cmake"), sdk_tool("ninja")) if android else ("cmake", shutil.which("ninja"))
    cfg = [cmake, "-S", PORT, "-B", hb, "-G", "Ninja" if ninja else "Unix Makefiles", f"-DPORT_GENERATED={gen}",
           f"-DPORT_BACKEND={a.backend}", "-DCMAKE_BUILD_TYPE=" + a.config]
    if android:
        if not (cmake and ninja): sys.exit("--target android: needs CMake and Ninja (on PATH or in the Android SDK)")
        cfg += [f"-DCMAKE_MAKE_PROGRAM={ninja}", "-DCMAKE_TOOLCHAIN_FILE=" + os.path.join(android_ndk(a), "build", "cmake", "android.toolchain.cmake"),
                "-DANDROID_ABI=arm64-v8a", f"-DANDROID_PLATFORM=android-{a.android_api}"]
    if win:
        sys.path.insert(0, os.path.join(PORT, "tools")); import deps
        os.environ["ZIG_PY"] = PY
        cfg.append(f"-DCMAKE_TOOLCHAIN_FILE={os.path.join(PORT, 'cmake', 'windows-zig.cmake')}")
        if a.backend == "sdl2": cfg.append(f"-DSDL2_DIR={deps.sdl2_mingw(os.path.join(ROOT, 'build', 'deps'))}")
    if a.cc: cfg.append(f"-DCMAKE_C_COMPILER={a.cc}")
    if a.cflags: cfg.append(f"-DCMAKE_C_FLAGS={a.cflags}")
    cfg.append(f"-DPORT_ABI_CHECK={'ON' if a.abi_check else 'OFF'}")
    run(cfg, stdout=subprocess.DEVNULL); run([cmake, "--build", hb], stdout=subprocess.DEVNULL)
    return os.path.join(hb, "ss2port.exe" if win else "ss2port")

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--backend", default="null"); ap.add_argument("--target", default="native", choices=["native", "windows", "android"]); ap.add_argument("--build", default=os.path.join(ROOT, "build", "conformance"))
    ap.add_argument("--skip", action="append", default=[]); ap.add_argument("--cc"); ap.add_argument("--cflags")
    ap.add_argument("--config", default="RelWithDebInfo"); ap.add_argument("--no-rebuild-guest", action="store_true")
    ap.add_argument("--cache-regs", action="store_true", help="lift with --cache-regs (guest registers in C locals)")
    ap.add_argument("--abi-check", action="store_true", help="with --cache-regs: build with -DPORT_ABI_CHECK=ON and fail if any call breaks the calling-convention contract")
    ap.add_argument("--wrap", help="run the host under this command (e.g. 'valgrind -q', 'xvfb-run -a')")
    ap.add_argument("--ndk", help="--target android: NDK directory"); ap.add_argument("--android-api", type=int, default=26)
    ap.add_argument("--serial", help="--target android: adb device serial"); ap.add_argument("--device-dir", default="/data/local/tmp/ss2conformance")
    ap.add_argument("--write-dir", action="store_true", help="run only the writedir section, with a game folder and a separate write folder (WRITEDIR_LAYOUT)")
    a = ap.parse_args(); a.build = os.path.abspath(a.build)
    if a.abi_check and not a.cache_regs: sys.exit("--abi-check needs --cache-regs")
    if a.target == "android" and a.backend != "null": sys.exit("--target android: only --backend null")
    os.makedirs(a.build, exist_ok=True)
    exe = os.path.join(a.build, "guest", "conformance.exe")
    if not (a.no_rebuild_guest and os.path.exists(exe)): exe = build_guest(a.build)
    gen = lift(exe, a.build, ["--cache-regs"] if a.cache_regs else []) if not (a.no_rebuild_guest and os.path.isdir(os.path.join(a.build, "gen"))) else os.path.join(a.build, "gen")
    host = build_host(gen, a.build, a)
    work = os.path.join(a.build, "run"); shutil.rmtree(work, ignore_errors=True); os.makedirs(work)
    shutil.copy(exe, work)
    if a.target == "windows" and os.path.isfile(os.path.join(os.path.dirname(host), "SDL2.dll")): shutil.copy(os.path.join(os.path.dirname(host), "SDL2.dll"), work)
    env = None; wdir = os.path.join(a.build, "run-write")
    if a.write_dir:
        shutil.rmtree(wdir, ignore_errors=True); os.makedirs(wdir); make_writedir_layout(work, wdir); before = snapshot(work)
        env = dict(os.environ, SS2PORT_WRITE_DIR=wdir)
    args = (a.wrap.split() if a.wrap else []) + [host, os.path.join(work, "conformance.exe")] + [x for s in a.skip for x in ("-skip", s)]
    if a.backend != "null": args.append("-pixels")         # a real renderer: check what it draws
    if a.write_dir: args.append("-writedir")
    if a.target == "android":                               # run in a fresh directory on the device; adb shell passes the exit status back
        adb = [sdk_tool("adb") or sys.exit("--target android: adb not found")] + (["-s", a.serial] if a.serial else []); dev = a.device_dir
        run(adb + ["shell", f"rm -rf {dev} {dev}-write && mkdir -p {dev}"]); run(adb + ["push", host, exe, dev + "/"], stdout=subprocess.DEVNULL)
        pre = ""
        if a.write_dir:                                     # both folders as prepared here; adb push copies a folder's contents into a new one
            run(adb + ["push", os.path.join(work, "ctest_wd"), dev + "/"], stdout=subprocess.DEVNULL)
            run(adb + ["push", wdir, dev + "-write"], stdout=subprocess.DEVNULL); pre = f"SS2PORT_WRITE_DIR={dev}-write "
        args = adb + ["shell", f"cd {dev} && chmod 755 ss2port && {pre}./ss2port conformance.exe" + "".join(f" -skip {s}" for s in a.skip) + (" -writedir" if a.write_dir else "")]
    r = subprocess.run(args, cwd=work, env=env, stderr=subprocess.PIPE if a.abi_check else None, text=True)
    if a.write_dir:                                         # the guest's writes must have gone to the write folder only
        if a.target == "android":                           # every file of the game folder's ctest_wd, with its md5, as prepared
            import hashlib
            want = {rel: hashlib.md5(data).hexdigest() for side, rel, data in WRITEDIR_LAYOUT if side == "game"}
            out = subprocess.run(adb + ["shell", f"cd {dev} && find ctest_wd -type f -exec md5sum {{}} +"], capture_output=True, text=True).stdout
            got = {l.split()[1]: l.split()[0] for l in out.splitlines() if len(l.split()) == 2}
            game_ok = got == want
        else:
            game_ok = snapshot(work) == before
        print(f"game folder unchanged: {'yes' if game_ok else 'NO'}")
        if not game_ok: r = subprocess.CompletedProcess(args, 1)
    if a.abi_check:
        sys.stderr.write(r.stderr)
        if a.target == "android": logs = r.stderr + subprocess.run(adb + ["shell", f"cat {dev}/*.log 2>/dev/null"], capture_output=True, text=True, errors="replace").stdout
        else: logs = r.stderr + "".join(open(os.path.join(work, f), errors="replace").read() for f in os.listdir(work) if f.endswith(".log"))
        nv = logs.count("ABI VIOLATION")
        print(f"ABI check: {nv} call site(s) broke the calling-convention contract")
        if nv: r = subprocess.CompletedProcess(args, 1)
    print(f"conformance ({a.backend}{', ' + a.target if a.target != 'native' else ''}): {'PASS' if r.returncode == 0 else 'FAIL (exit %d)' % r.returncode}")
    sys.exit(0 if r.returncode == 0 else 1)

if __name__ == "__main__": main()
