# Implementation plan: System Shock 2 (NewDark 2.48) on Android ARM64

Phase 0 audit, 2026-10-17. Builds on ANDROID_COMPATIBILITY.md, TOUCH_CONTROLS.md, MOD_COMPATIBILITY.md and
SCP_COMPATIBILITY.md. Nothing in this plan has been started.

## 1. Recommended architecture

```
Android app (Java)                  NewDarkActivity extends SDLActivity: landscape, immersive, haptics, big-stack native thread
                                    Launcher: pick/import game folder, choose profile (Phase 5)
SDL 2.32.10 (from source, as in the Shockolate build), patched SDLSurface.java for raw touch
libmain.so (NDK clang, arm64-v8a)
  port/backends/android/            NEW: SDL_main, argv/options, logcat+file log, lifecycle, overlay hook, touch + controller
    touch/                          IMPORTED from system-shock-android@7502370 (core, radial, settings, surface, editor, render→GLES)
    touch_ss2_bridge.c              NEW: touch edges → PlatEvent (DIK keys, mouse dx/dy, buttons, wheel, text)
  port/backends/sdl2/               REUSED: gl_render.c (GLES 2.0 path), gl_thread.c, plat_sdl2.c (with __ANDROID__ tweaks)
  port/backends/posix/              REUSED unchanged: memory, threads, files
  port/core, win32, crt, dx, video  REUSED unchanged (upstream)
  generated C (out/port)            lifted on the PC from the user's own SS2.exe, allobjs.osm, Squirrel.osm, lgvid.dll (+ NVScript.osm in Phase 4)
```

Principles:

- **Keep layers separate.** Upstream code stays where it is. Android-only code lives in `port/backends/android/` and
  `android/` (Gradle project). Imported touch code lives in `port/backends/android/touch/` with a `PROVENANCE.md`
  recording the source repo, commit `7502370` and per-file changes.
- **Upstream-shaped changes outside the backend**, each small and proposed upstream so future merges stay cheap:
  1. `runtime/rt.h` aarch64 setjmp (B1).
  2. `port/CMakeLists.txt` `if(ANDROID)` shared library.
  3. `port/build.py` `--target android` (or a separate script that only lifts).
  4. Possibly a `PLAT_EV_MOUSE_MOVE` absolute flag in `win32/user32.c`.
  5. NVScript in the `MODULES` table.
- **Personal builds only.** The APK contains code generated from the user's game files (ANDROID_COMPATIBILITY.md
  §5). The pipeline therefore runs on the owner's Windows PC, and no game-derived artefact is published.
- **No engine changes.** The recompiled game is untouched. Everything goes through `plat.h`.

### Build pipeline (Windows PC)

1. **Lift** (once per change of game binaries or lifter): Python 3 with `pefile` and `capstone`. Use `port/build.py`'s
   lift stage, which writes `out/port/{nd,ao,sq,lv,mods}`. It runs about 8 minutes the first time (`port/README.md:30`).
   `build.py` is documented for Linux/WSL/macOS. Running its lift stage on native Windows Python is expected to work,
   since it is plain Python, but this is **unverified**. WSL is the documented fallback.
2. **Native build:** Gradle `externalNativeBuild` → CMake on `port/` with
   `-DPORT_GENERATED=…/out/port -DPORT_BACKEND=android -DANDROID_ABI=arm64-v8a`, NDK `29.0.14206865` (already installed
   and used by the Shockolate project), CMake 3.22+, Ninja.
3. **APK:** debug-signed, sideloaded with `adb install`.

## 2. Game-file management on Android

`plat_fs_*` uses POSIX `open`/`opendir` (`port/backends/posix/plat_posix.c:110-146`). The game folder must therefore
be reachable through a real file path. Storage Access Framework `content://` trees are not usable directly.

| Option | How | For | Against |
| --- | --- | --- | --- |
| A. App-specific external dir (`/storage/emulated/0/Android/data/<pkg>/files/games/<name>/`) | `adb push` or PC USB copy. No permission needed. | Simplest; POSIX paths; no copy step in the app; ideal for development. | Removed on uninstall; Android 11+ hides it from on-device file managers. FUSE path (Android usually bypasses FUSE for an app's own dirs). |
| B. SAF import into app storage (reuse Shockolate `LauncherActivity`) | `ACTION_OPEN_DOCUMENT_TREE` → recursive copy with traversal guards, fsync and atomic swap | User-friendly; the source folder is never modified; proven code. | Temporarily doubles storage (a modded SS2 is a few GB); import time. |
| C. All-files access (`MANAGE_EXTERNAL_STORAGE`) | Direct POSIX on shared storage | No copy. | Special permission, discouraged; FUSE overhead on every open. Acceptable for sideloaded personal builds, but a fallback only. |
| D. Native SAF file layer | Implement `plat_fs_*` over JNI `ContentResolver`/`DocumentsContract` | No copy, no special permission. | A large, slow and fragile layer for directory listings and case resolution. **Rejected.** |

**Recommendation:**

- **Phases 1-4:** option A, filled with `adb push`.
- **Phase 5:** add option B with the import target in the same app-specific directory, so `adb` refresh keeps
  working.

Read-only base assets stay unchanged. Every write (saves, `cam.cfg` edits, `SS2.log`, port log) goes to a separate
**write directory** in internal storage (`filesDir/profiles/<name>/`), through `SS2PORT_WRITE_DIR`
(`port/core/vfs.c:84-106`). Android settings (touch layout, options) live in `filesDir` too.

Validation on import or launch:

- `SS2.exe` exists and matches the size and CRC the APK was built from. The host already enforces this
  (`port/core/host.c:400-403`); the launcher should check first and show a clear message.
- Warn if a recompiled module (`Data/allobjs.osm`, `osm/squirrel.osm`, `lgvid.dll`, later `osm/NVScript.osm`) differs.
- Warn about absolute paths in `cam_mod.ini`.

One game folder corresponds to one write-directory profile. That keeps SCP and vanilla saves apart
(SCP_COMPATIBILITY.md §4).

## 3. Milestones

### Prerequisites (owner actions, before Phase 1)

- A classic **NewDark 2.48** install on the PC. The local install is 2.47 (SCP_COMPATIBILITY.md §2). A clean copy
  without SS2Tool's `patch_ext` is best for the vanilla baseline, because SS2Tool adds an NVScript dependency.
- `python -m pip install pefile capstone ziglang`. This installs software on the PC, so the owner decides.
- Optionally back up or push `system-shock-android`'s local `touch-controls` branch (38 unpushed commits).

### Phase 1: Android native runtime (headless)

| | |
| --- | --- |
| **Code changes** | 1. `runtime/rt.h`: aarch64 `setjmp`/`longjmp` via bionic `_setjmp`/`_longjmp`. 2. `port/CMakeLists.txt`: `if(ANDROID)` adds a shared-library target (plus an executable target for adb-run test binaries); `-fvisibility=hidden`. 3. `port/backends/android/`: `plat_log_write` to logcat plus a file; options file → argv; `port_main` on a large pthread (8-64 MB). 4. A test path for `port/tests/run_conformance.py` that cross-compiles the host with the NDK toolchain file (a `--toolchain`/`--target android` switch, or a documented manual CMake invocation). 5. A minimal Gradle project: an `SDLActivity` subclass, landscape, loads `libmain.so`, runs the headless (null-backend) game against a game folder in app-specific storage. |
| **Files** | `runtime/rt.h`, `port/CMakeLists.txt`, `port/tests/run_conformance.py` (or a new script), new `port/backends/android/*`, new `android/` Gradle project |
| **Risks** | Other clang/aarch64 compile issues in the ~143 generated files, which only show once compiled. A 4 GB reserve refused (use `--guest-space`). Stack overflow if the big-stack thread is missed. Build time and memory for the generated C on Windows. |
| **Dependencies** | Prerequisites above; NDK 29; a lifted `out/port` from a 2.48 exe. |
| **Acceptance** | (a) The conformance suite (game-free; 509 checks on `null`, `port/README.md:179-181`) passes on the Pixel 10 via `adb shell`, both plain and with `--cache-regs` (+`--abi-check`). (b) The real game built with the `null` backend runs on the Pixel from a shell, loads `allobjs.osm`, `Squirrel.osm` and `lgvid.dll` as recompiled modules (`modules.c:96`), reaches "first frame presented" and stops cleanly with `--frames 600`. No guest fault. (c) The same headless run starts from the APK, with the log visible in logcat and in the log file. |
| **Validation** | `adb logcat`, the log file, `--verbose`; `--trace` on failure (`host.c:48-78`). |

### Phase 2: Graphical proof of concept

| | |
| --- | --- |
| **Code changes** | Android variant of `plat_sdl2.c`: `SDL_main` instead of `main` (`:180-189`); GLES by default (`:44-46`); no `SDL_StartTextInput` at start (`:61`); handle `SDL_APP_*` (release keys, pause audio, keep the render thread off at first); immersive landscape. Temporary input: SDL's touch→mouse plus a Bluetooth keyboard/mouse for development. |
| **Files** | `port/backends/android/plat_android.c` (or `#ifdef __ANDROID__` in `backends/sdl2/plat_sdl2.c`), `android/` Java |
| **Risks** | GLES driver differences (mediump precision, shader compile time, S3TC absent → CPU decode); GL context across pause/resume; audio latency; `GetTickCount` jump after resume. |
| **Dependencies** | Phase 1. |
| **Acceptance** | Intro cutscene plays with sound. Main menu works; new game. Earth training → Station → MedSci 1 with a keyboard/mouse. One save and load. One level transition. Background and foreground once without a crash. No missing-file errors in `SS2.log`. |
| **Validation** | Manual play on the Pixel 10. Log review. **Investigation task:** run with `SS2PORT_MOUSE_DEBUG=1` and record `plat_video_mouse_mode` transitions in shoot mode, interface mode (Tab), inventory, PDA, hacking and menus. This decides automatic mode detection for Phase 3 (TOUCH_CONTROLS.md §6). **First performance baseline:** default lift versus `--cache-regs --fastpaths` (§5). |

### Phase 3: Shockolate touch integration

| | |
| --- | --- |
| **Code changes** | Import from `7502370`: `touch_controls.*`, `touch_radial.*`, `touch_settings.*`, `touch_editor.c`, `touch_surface.*`, `touch_target_types.h`, the host tests, and the `SDLSurface.java` patch. Adapt the enums and defaults. Port `touch_render.c` to a GLES primitive layer drawn in `glr_present` before `gl_swap` (`gl_render.c:404-414`). New `touch_ss2_bridge.c` emits `PlatEvent`s: stick → W/A/S/D, look → mouse dx/dy, fire → left button, use → right button, keys from a data-driven SS2 action table, cursor taps and drags, movie skip, text-entry button, haptics via `touchHaptic`. If Phase 2 shows the game keeps the mouse captured in interface mode, add an absolute-position flag to `PLAT_EV_MOUSE_MOVE` in `win32/user32.c:205-213` (shared, upstreamable). Radials offer fixed key wheels (weapon slots, psi, screens). |
| **Files** | `port/backends/android/touch/*`, `touch_ss2_bridge.c`, `gl_render.c` (overlay hook), possibly `win32/user32.c`; Java haptics |
| **Risks** | Loss of the analog stick feel (no joystick path, `dx/dinput.c:4`). Mode auto-detection may not exist. Bind-file mismatch. Overlay and render-thread interaction. |
| **Dependencies** | Phase 2, including the mode-detection findings. |
| **Acceptance** | Touch only, no keyboard: complete Earth, Station and MedSci 1. Move and look simultaneously (multitouch). Fire, use, reload, jump, crouch, lean, switch weapons. Inventory drag and drop, item use on the world, PDA logs, research, hacking minigame, keypad code entry, psi power selection. A save name typed with the soft keyboard. Layout editor, scale, opacity and sensitivity persist across restarts. Ported host tests pass. |
| **Validation** | Host test suite (`-DTOUCH_HEADLESS`), a device checklist adapted from `docs/TOUCH_ALPHA_4.md`, and owner feel-testing against the Shockolate port. |

### Phase 4: SCP and modded installations

| | |
| --- | --- |
| **Code changes** | 1. **Recompile NVScript.osm:** add it to `MODULES`/`MODULES_LOW` in `port/build.py:35-36` at a free rebase; run `harness/difftest.py`/`insnfuzz.py` on it; add missing import shims, if any. 2. Same for the SS2Tool-updated `squirrel.osm` if it differs from the upstream-verified one. 3. Only if observed: absolute-path mapping (`vfs.c`), merged overlay listings (`kernel32_file.c:146-158`). 4. Log `.osm` load failures at info level (`kernel32.c:326`). |
| **Files** | `port/build.py` (and `host/build_win.py` for parity), `port/core/vfs.c`, `port/win32/kernel32*.c` |
| **Risks** | NVScript may use C++ `throw`, SEH or SSE paths the lifter does not support. Newer SCP may need a newer NVScript. Performance of SCP's heavier scripting. |
| **Dependencies** | Phase 2 (Phase 3 for comfortable testing). A 2.48 install with SS2Tool and the current SCP, verified on Windows first. |
| **Acceptance** | The SCP test plan (SCP_COMPATIBILITY.md §5) passes: the Earth gravshaft gate passes, SCP-specific changes are observed, saves and transitions work, and the logs show `nvscript.osm` loaded as a recompiled module. One asset-only mod (e.g. a Vurt pack) and one DML mod verified. |
| **Validation** | Side-by-side with the same install on Windows (`ss2_native.exe` or the original exe). |

### Phase 5: Stabilisation

| | |
| --- | --- |
| **Code changes** | Lifecycle hardening (surface loss, render thread, audio resume). SAF importer (option B). Profile selection. `SDL_GameController` mapping (secondary). A profile log line (as on the Vita: fps, render busy, file time). Crash handler that writes the guest state to the log file. Release packaging (signing key, personal-build documentation). Optional GLES quality: highp where available; ES3 caps for NPOT. |
| **Risks** | Thermal throttling; device-specific GPU issues. |
| **Acceptance** | A full campaign playthrough (vanilla, then SCP) on the Pixel 10 with no blocking bug. 30 minutes of sustained play with the frame-time and thermal data recorded. Background and restore 20 times. Controller and touch coexist. |

## 4. Order rationale

The suggested order is kept. Two adjustments come from the audit:

1. Phase 1 puts a **game-free conformance run on the device** first. It isolates toolchain and runtime problems
   (B1, memory, threads) from SDL, GL and game data.
2. Phase 2 includes the **mouse-capture investigation**, because the touch design in Phase 3 depends on it.

## 5. Performance assessment

**Evidence (PS Vita only):**

- 15-21 fps exploring and 6-8 fps in combat on a 444 MHz Cortex-A9 (`port/backends/vita/PERF.md`).
- The game thread is 90-100% busy. The cost is the recompiled code itself: a flat profile across thousands of
  functions, about 45 ms/frame plus ~0.3 ms per draw call.
- The GPU is mostly idle; the render thread is only 4-9% busy.
- Helped: `--cache-regs` (≈10 → 13-18 fps), the indirect-call cache, buffered writes, the render thread and fast
  paths (a few percent).
- Did not help: AI-efficiency DML, GPU settings.

**Inference for Android (not a measurement):** the Pixel 10's big cores are many years and several microarchitecture
generations ahead of a 444 MHz Cortex-A9, with far larger caches. The same bottleneck (single-thread execution of
generated code) should leave substantial headroom. **No target frame rate is promised** until measured.

**Likely Android-specific costs:**

- **PIC/GOT access to the global `M`** in a shared library. Mitigate with `-fvisibility=hidden`.
- **CPU DXT decode at texture upload**, if S3TC is absent.
- **Lazy GLSL compilation hitches.** Up to 256 cached fixed-function programs, compiled at first use
  (`gl_render.c:241-273`).
- **FUSE file access** if the game lives on shared storage.
- **Thermal throttling** in long sessions.
- **Indirect calls:** the binary search fallback without `--cache-regs` (`PERF.md`, Findings).
- **Guest memory access:** every load and store is `memcpy` at `M + a` (`rt.h:57-64`). It compiles to plain
  loads, but in default mode the compiler reloads registers and `M` after stores. This is exactly what `--cache-regs`
  addresses (`runtime/rt_fast.h:1-12`).

**`--cache-regs`:**

- It is opt-in, relifts everything (~8 min), and has been played on Linux and the Vita with `--abi-check` and no
  violations (`port/README.md:107-115`).
- **Recommendation:** first light in Phase 2 with the default lift, which is the verified baseline. Then an early
  A/B in the same phase: `--cache-regs --fastpaths` plus one `--abi-check` session.
- Adopt it only if it is clean (no `ABI VIOLATION`, no x87-depth faults) and measurably faster on the Pixel.
- The conformance suite must pass in both modes on arm64 (Phase 1).

**Measurement approach:**

| Metric | Tool |
| --- | --- |
| Average FPS and frame-time stability | Host-side frame timestamps in `port_frame_presented` (`host.c:422-427`), logged every 5 s with min/avg/p95/max, as the Vita `profile:` line does. Cross-check with Perfetto (SurfaceFlinger frame timeline). |
| CPU usage and hot code | `--prof` / `PORT_PROF` (`port/core/prof.c`) plus `tools/fnprof.py`. Android `simpleperf` on `libmain.so` with symbols. |
| Memory | `dumpsys meminfo <pkg>`. On Linux-like hosts the host reports touched guest memory with `--verbose` (`host.c:96-110`). |
| Loading performance | Timestamps around level loads in the port log (file-call counters as on the Vita). |
| Audio underruns | Add a counter in the audio callback (late or short fills). Perfetto audio tracks. |
| Thermal throttling | `dumpsys thermalservice` and CPU frequency traces in Perfetto, over a 30-minute session. |

## 6. First implementation task

**The smallest task that moves toward a playable build without needing game files, the APK or SDL:**

> Add an aarch64 path for `RT_HOST_SETJMP`/`RT_HOST_LONGJMP`/`RT_HJ_WORDS` in `runtime/rt.h`. Cross-compile the
> conformance suite's host for `aarch64-linux-android` with NDK 29 (null backend). Run it on the Pixel 10 with
> `adb shell`, plain and with `--cache-regs --abi-check`.

- **Done when:** every check passes, and the output is recorded in `docs/android-audit/`.
- **Why first:** it proves or disproves the core runtime (address-space reservation, threads, setjmp, x87-as-double,
  the generated-code conventions) on the real target, in hours rather than days. No legal-artefact concerns apply,
  since no game code is involved.
- **What it needs:** owner approval to install a test binary on the device (`adb push`), and Zig plus Python on the
  PC (the suite builds its 32-bit guest program with Zig, `port/tests/run_conformance.py:24-37`).

## 7. Recommended Phase 1 scope

**In scope:**

1. PC prerequisites: a 2.48 install, Python packages, and a lift of `out/port` from 2.48.
2. The aarch64 setjmp fix.
3. The arm64 conformance run on the device (§6).
4. The CMake Android targets.
5. A headless game run on the device via `adb shell` (null backend, `--frames`), with modules loaded and no faults.
6. A minimal APK (SDLActivity subclass, `libmain.so`, big-stack game thread, logcat and file logging, options file,
   app-specific game directory, write directory) that performs the same headless run.

**Out of scope for Phase 1:**

- rendering, audio and input;
- touch controls;
- the SAF importer;
- NVScript and SCP;
- `--cache-regs` as a default;
- any change under `port/core`, `win32`, `crt` or `dx`.
