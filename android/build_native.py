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
import argparse, os, shutil, subprocess, sys

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

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdl2", default=os.environ.get("SDL2_SOURCE") or os.path.join(ROOT, "build", "deps", "SDL2"), help="SDL 2 source tree")
    ap.add_argument("--ndk"); ap.add_argument("--api", type=int, default=26); ap.add_argument("--config", default="RelWithDebInfo")
    ap.add_argument("--build", default=os.path.join(ROOT, "build", "android"))
    ap.add_argument("--no-rebuild-guest", action="store_true", help="reuse the conformance program and its lifted C if present")
    a = ap.parse_args(); a.build = os.path.abspath(a.build)
    if not os.path.isfile(os.path.join(a.sdl2, "CMakeLists.txt")): sys.exit(f"no SDL 2 source in {a.sdl2} (--sdl2)")
    libs = os.path.join(a.build, "libs", ABI); assets = os.path.join(a.build, "assets", "conformance")
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
    for f in sorted(os.listdir(libs)): print(f"  {os.path.join(libs, f)}  {os.path.getsize(os.path.join(libs, f)):,} bytes")
    print(f"  {os.path.join(assets, 'conformance.exe')}  {os.path.getsize(os.path.join(assets, 'conformance.exe')):,} bytes")

if __name__ == "__main__": main()
