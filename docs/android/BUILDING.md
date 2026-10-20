# Building the Android port

You build the APK yourself, on your PC, from **your own copy of System Shock 2**. The recompiler reads your `SS2.exe`
(read only) and produces code that goes into your APK. That output is derived from copyrighted software: **keep it, and
the APK, to yourself.**

## 1. What you need

| Tool | Tested version | Notes |
| --- | --- | --- |
| OS | Windows 11 | Linux/macOS should work for the Python/Gradle parts; only Windows has been run |
| Python | 3.12 – 3.14 | `pip install -r requirements.txt` (`pefile==2024.8.26`, `capstone==5.0.9`, `ziglang==0.16.0`; Pillow only for the icon tool). Use a venv |
| JDK | **17** | Gradle 8.7 / AGP 8.5.1 |
| Gradle | 8.7 | Supplied by `android/gradlew` (downloads on first use) |
| Android SDK | platform **android-34**, build-tools **34.0.0** | `ANDROID_HOME` must be set |
| Android NDK | **29.0.14206865** | `ndkVersion` is pinned in `android/app/build.gradle` |
| CMake | **3.22.1** (SDK package) | |
| SDL 2 source | **2.32.10** (`release-2.32.10` tag) | Fetched below; not stored in this repo |
| Git | any | |

Target/minimum SDK: `targetSdk 34`, `minSdk 26`; ABI: `arm64-v8a` only.

## 2. Your game files

You need the **retail NewDark 2.48** build of System Shock 2 (GOG and Steam copies are identical):

| File | Size | SHA-256 |
| --- | --- | --- |
| `SS2.exe` | 5,121,024 | `c7ff08c5a1837942410cc2328b4a6579ddb1d5a2678b8274db8b0347ce19823e` |

The recompiler also picks up `Data\allobjs.osm`, `osm\Squirrel.osm` and `lgvid.dll` next to `SS2.exe`. The generated
code accepts **only this `SS2.exe`** (and the module versions it was generated from); the app refuses anything else and
says why. (A NewDark 2.47 install, for example, imports but will not play.) Nothing is written next to your `SS2.exe`.

**Optional NVScript (needed for SCP).** Pass up to two of these, whichever you use; each is identified by sha256 in
`port/nvscript_versions.json`, and any other file is refused:

| Version | Used by | Size | SHA-256 |
| --- | --- | --- | --- |
| NVScript of SCP Beta 8 | SCP Beta 8 | 704512 | `f15fc91b276e73ad52d5026a86a7d51b83e5644a20c3e58a53a10a4b52d4f11a` |
| NVScript 1.2.4 (2017) | SCP Beta 4 | 662016 | `abed4ee229dda567553b7d805f219008b6390c2d2da37459629eb28ac9ee6a9a` |

The authoritative values are in [`port/nvscript_versions.json`](../../port/nvscript_versions.json). Get these files from
your own SCP installation (`DMM/SCP_beta8/NVScript.osm`, or `osm/NVScript.osm` of an SCP Beta 4 install).

## 3. Build

All paths below are examples; nothing in the scripts depends on a particular drive or user name.

```sh
git clone <this repository> NewDarkAndroid && cd NewDarkAndroid
python -m venv build/venv && build/venv/bin/pip install -r requirements.txt     # Windows: build\venv\Scripts\
git clone -b release-2.32.10 --depth 1 https://github.com/libsdl-org/SDL build/deps/SDL2   # or set SDL2_SOURCE / -Psdl2.dir
export ANDROID_HOME=/path/to/Android/Sdk                                                    # Windows: setx / $env:ANDROID_HOME

# 1) native libraries: SDL2 + the game-free conformance programs (about 1.5 minutes)
python android/build_native.py
# 2) lift YOUR game and build libss2game.so (about 8 minutes the first time, seconds after)
python android/build_native.py --no-rebuild-guest --game "/path/to/System Shock 2/SS2.exe" \
    --game-backend all \
    --nvscript "/path/to/SCP_beta8/NVScript.osm" --nvscript "/path/to/SCP_beta4/osm/NVScript.osm"
#    (--nvscript is optional and repeatable; omit it for a vanilla-only build)
# 3) the APK
cd android && ./gradlew assemblePlayDebug        # gradlew.bat on Windows
```

Outputs:

| File | What |
| --- | --- |
| `android/app/build/outputs/apk/play/debug/app-play-debug.apk` | **The game build** (about 47 MB): launcher + game, no diagnostics |
| `android/app/build/outputs/apk/dev/debug/app-dev-debug.apk` | Everything, including conformance programs and diagnostics (about 96 MB) |

Both are signed with the standard Android **debug** key. That is fine for your own phone. See
[docs/release/SIGNING.md](../release/SIGNING.md) for release signing.

Install: `adb install -r android/app/build/outputs/apk/play/debug/app-play-debug.apk`

### Python only, no Android

`python port/build.py "/path/to/SS2.exe"` builds the Linux/WSL/Windows-test host (needs CMake, a C compiler and SDL 2). See
[../UPSTREAM_README.md](../UPSTREAM_README.md).

## 4. Tests that need no game

| Command | Checks |
| --- | --- |
| `python port/tests/run_conformance.py --target android --serial <adb serial> --build build/conf-android` | 511 checks of the CPU/ABI emulation on the device (needs a phone, builds without any game) |
| `sh port/backends/android/touch/tests/run.sh` | Touch-control suites (desktop C compiler) |
| `sh port/tests/setjmp/run.sh` | setjmp/longjmp |

## 5. Platform notes

- 16 KB page size: the shipped libraries are linked with 16 KB ELF alignment and the APK is 16 KB-zipaligned; it
  starts on 16 KB-page devices.
- The first full build takes about 20 minutes on a modern PC; rebuilds are incremental.
- `build_native.py --build <dir>` keeps several builds apart (for example one per NVScript combination).
