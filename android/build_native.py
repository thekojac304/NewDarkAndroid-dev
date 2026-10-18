#!/usr/bin/env python3
"""Build the native libraries of the Android app (android/README.md) and stage them where Gradle packages them.

    python android/build_native.py [--sdl2 DIR] [--ndk DIR] [--api N] [--no-rebuild-guest]

Phase 1B content: the game-free conformance program instead of the game.

  build/android/libs/arm64-v8a/libSDL2.so          SDL 2 (from --sdl2, default build/deps/SDL2; release-2.32.10)
  build/android/libs/arm64-v8a/libss2conf.so       port/ with the conformance program lifted by default
  build/android/libs/arm64-v8a/libss2conf_cr.so    the same lifted with --cache-regs, built with PORT_ABI_CHECK
  build/android/assets/conformance/conformance.exe the guest program both were lifted from

The conformance program is built and lifted exactly as port/tests/run_conformance.py does it (Zig, Python with pefile and
capstone). CMake, Ninja and the NDK come from the Android SDK as there. Then: cd android && gradlew assembleDebug.
"""
import argparse, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE); PORT = os.path.join(ROOT, "port")
sys.path.insert(0, os.path.join(PORT, "tests"))
import run_conformance as rc        # build_guest, lift, sdk_tool, android_ndk, run

ABI = "arm64-v8a"

def cmake_build(src, bdir, a, defs, target):
    cmake, ninja = rc.sdk_tool("cmake"), rc.sdk_tool("ninja")
    if not (cmake and ninja): sys.exit("needs CMake and Ninja (on PATH or in the Android SDK)")
    cfg = [cmake, "-S", src, "-B", bdir, "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}",
           "-DCMAKE_TOOLCHAIN_FILE=" + os.path.join(rc.android_ndk(a), "build", "cmake", "android.toolchain.cmake"),
           f"-DANDROID_ABI={ABI}", f"-DANDROID_PLATFORM=android-{a.api}", f"-DCMAKE_BUILD_TYPE={a.config}"] + [f"-D{k}={v}" for k, v in defs.items()]
    rc.run(cfg, stdout=subprocess.DEVNULL); rc.run([cmake, "--build", bdir, "--target", target], stdout=subprocess.DEVNULL)

def lift_game(exe, gen, nvscript=None):
    """Lift SS2.exe and its DLLs into gen/ exactly as port/build.py's step 1 does with its defaults (no --cache-regs, no
    fast paths, no symbols); unchanged inputs are not lifted again. Returns the seconds each lift took."""
    sys.path.insert(0, PORT); import build as pb           # port/build.py: MODULES, find_ci, sha, run (cwd = repository)
    times, lift_srcs = {}, [os.path.join(ROOT, f) for f in pb.LIFT_SOURCES]
    def lift(src, pfx, md, cmd, extra):
        sigf, s = os.path.join(md, "src.sha1"), pb.sha(src, *lift_srcs, extra=extra)
        if os.path.exists(sigf) and open(sigf).read().strip() == s and os.path.exists(os.path.join(md, f"{pfx}_meta.json")): print(f"  {os.path.basename(src)}: already lifted"); return
        print(f"  lifting {os.path.basename(src)} ...", flush=True); shutil.rmtree(md, ignore_errors=True); t = time.time()
        pb.run(cmd, f"lift {src}"); open(sigf, "w").write(s); times[pfx] = time.time() - t
    nd = os.path.join(gen, "nd")
    lift(exe, "nd", nd, [sys.executable, "lift.py", "--smc", exe, "nd", nd], "")
    pb.run([sys.executable, os.path.join("tools", "apply_hooks.py"), nd], "fast paths (none: puts the lifted code back)")
    pb.run([sys.executable, os.path.join("host", "gen_hostdata.py"), os.path.join(nd, "nd_meta.json"), exe, os.path.join(nd, "nd_hostdata.c")], "host data")
    if os.path.exists(os.path.join(nd, "nd_symtab.c")): os.remove(os.path.join(nd, "nd_symtab.c"))
    mods = []
    for fname, sub, pfx, rebase in pb.modules(nvscript):
        src = fname if sub is None else pb.find_ci(os.path.dirname(exe), sub, fname); md = os.path.join(gen, pfx)
        if not os.path.isfile(src): sys.exit(f"{src} not found")
        reb = ["--rebase", hex(rebase)] if rebase else []
        lift(src, pfx, md, [sys.executable, "lift.py", "--iat-indirect"] + reb + [src, pfx, md], str(rebase))
        pb.run([sys.executable, os.path.join("host", "gen_moddata.py"), src, pfx, os.path.join(md, f"{pfx}_moddata.c")] + reb, f"{os.path.basename(fname)} data")
        mods.append(pfx)
    pb.drop_stale_nvscript(gen, mods)                       # an earlier --nvscript lift that is not built in now
    md = os.path.join(gen, "mods"); os.makedirs(md, exist_ok=True)
    with open(os.path.join(md, "mods.c"), "w") as f:
        f.write('#include "recomp_mod.h"\n' + "".join(f"extern const RecompModDesc {p}_desc;\n" for p in mods) + "const RecompModDesc *const recomp_mods[] = { " + "".join(f"&{p}_desc, " for p in mods) + "0 };\n")
    return times

def build_game(a, libs):
    """--game: the recompiled game as libss2null.so (headless) and libss2game.so (SDL2, OpenGL ES), from one default lift."""
    exe = os.path.abspath(a.game); game = os.path.join(a.build, "game"); gen = os.path.join(game, "gen")
    if not os.path.isfile(exe): sys.exit(f"SS2.exe not found: {exe}")
    print(f"lift {exe} (default lift) ...", flush=True)
    sys.path.insert(0, PORT); import build as pb
    for path, pfx, base, label in pb.nvscript_plan(a.nvscript): print(f"  NVScript: {label} -> prefix {pfx}, lifted at {base:#x} ({path})")
    for pfx, s in lift_game(exe, gen, a.nvscript).items(): print(f"  {pfx}: lifted in {s:.0f} s")
    for name, backend in (("ss2null", "null"), ("ss2game", "sdl2")):
        if a.game_backend not in ("all", backend): continue
        print(f"lib{name}.so ({backend}) ...", flush=True); t = time.time()
        defs = {"PORT_GENERATED": gen, "PORT_BACKEND": backend, "PORT_ANDROID_LIB": name}
        if backend == "sdl2":       # SDL's build tree has an SDL2Config.cmake but not the files it includes: use an install
            prefix, sb = os.path.join(a.build, "sdl2-install"), os.path.join(a.build, "sdl2")
            rc.run([rc.sdk_tool("cmake"), "--build", sb, "--target", "SDL2main"], stdout=subprocess.DEVNULL)    # the install wants it too
            rc.run([rc.sdk_tool("cmake"), "--install", sb, "--prefix", prefix], stdout=subprocess.DEVNULL)
            defs["SDL2_DIR"] = os.path.join(prefix, "lib", "cmake", "SDL2")
        cmake_build(PORT, os.path.join(game, "host-" + backend), a, defs, name)
        shutil.copy(os.path.join(game, "host-" + backend, f"lib{name}.so"), libs)
        print(f"  built in {time.time() - t:.0f} s", flush=True)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdl2", default=os.environ.get("SDL2_SOURCE") or os.path.join(ROOT, "build", "deps", "SDL2"), help="SDL 2 source tree")
    ap.add_argument("--ndk"); ap.add_argument("--api", type=int, default=26); ap.add_argument("--config", default="RelWithDebInfo")
    ap.add_argument("--build", default=os.path.join(ROOT, "build", "android"))
    ap.add_argument("--no-rebuild-guest", action="store_true", help="reuse the conformance program and its lifted C if present")
    ap.add_argument("--game", metavar="SS2.EXE", help="also lift your own NewDark 2.48 SS2.exe (with its allobjs.osm, Squirrel.osm, lgvid.dll) and build the game libraries; read only, nothing is written next to it")
    ap.add_argument("--game-only", action="store_true", help="with --game: rebuild only the game libraries, keep SDL2 and the conformance libraries already staged")
    ap.add_argument("--nvscript", metavar="FILE", action="append", help="with --game: also recompile this NVScript.osm (SCP needs it; e.g. its mod folder's copy). Repeat it for several versions in one build (each must be listed in port/nvscript_versions.json); the game then loads whichever one an installation has. Without it, earlier lifts of it are dropped")
    ap.add_argument("--game-backend", default="all", choices=["all", "null", "sdl2"], help="with --game: which game libraries to build")
    a = ap.parse_args(); a.build = os.path.abspath(a.build)
    if not os.path.isfile(os.path.join(a.sdl2, "CMakeLists.txt")): sys.exit(f"no SDL 2 source in {a.sdl2} (--sdl2)")
    libs = os.path.join(a.build, "libs", ABI); assets = os.path.join(a.build, "assets", "conformance")
    if a.game_only:
        if not a.game: sys.exit("--game-only needs --game")
        if not os.path.isfile(os.path.join(libs, "libSDL2.so")): sys.exit("--game-only: build SDL2 and the conformance libraries first (without --game-only)")
        build_game(a, libs); return
    for d in (libs, assets): shutil.rmtree(d, ignore_errors=True); os.makedirs(d)

    print("SDL2 ...", flush=True)
    sb = os.path.join(a.build, "sdl2")
    cmake_build(a.sdl2, sb, a, {"SDL_SHARED": "ON", "SDL_STATIC": "OFF", "SDL_TEST": "OFF"}, "SDL2")
    shutil.copy(os.path.join(sb, "libSDL2.so"), libs)

    conf = os.path.join(a.build, "conformance")
    exe = os.path.join(conf, "guest", "conformance.exe"); reuse = a.no_rebuild_guest
    if not (reuse and os.path.exists(exe)): print("conformance program ...", flush=True); exe = rc.build_guest(conf); reuse = False
    for name, lift_args, abi_check in (("ss2conf", [], "OFF"), ("ss2conf_cr", ["--cache-regs"], "ON")):   # both from the same conformance.exe
        lb = os.path.join(conf, name); gen = os.path.join(lb, "gen")
        if not (reuse and os.path.isdir(gen)): print(f"lift {' '.join(lift_args) or '(default)'} ...", flush=True); gen = rc.lift(exe, lb, lift_args)
        print(f"lib{name}.so ...", flush=True)
        cmake_build(PORT, os.path.join(lb, "host"), a, {"PORT_GENERATED": gen, "PORT_BACKEND": "null", "PORT_ANDROID_LIB": name, "PORT_ABI_CHECK": abi_check}, name)
        shutil.copy(os.path.join(lb, "host", f"lib{name}.so"), libs)
    shutil.copy(exe, assets)
    if a.game: build_game(a, libs)
    for f in sorted(os.listdir(libs)): print(f"  {os.path.join(libs, f)}  {os.path.getsize(os.path.join(libs, f)):,} bytes")
    print(f"  {os.path.join(assets, 'conformance.exe')}  {os.path.getsize(os.path.join(assets, 'conformance.exe')):,} bytes")

if __name__ == "__main__": main()
