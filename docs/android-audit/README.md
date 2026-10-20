# Android port audit: executive summary

Phase 0 (audit and planning only), 2026-10-17. Target: System Shock 2 (NewDark 2.48) as a native Android ARM64 APK,
first on the Google Pixel 10, with the existing Shockolate touch controls and support for pre-modded installs,
especially SCP.

## Workspace

| Item | Value |
| --- | --- |
| Upstream | `https://github.com/Owloeb/NewDarkRecompiled` |
| Checkout | `<repo>` (new; `<local path>` was empty and no other checkout existed) |
| Upstream commit | `02f77da02d0743869defbf09171790e4ab2b6059` (`main`, 2026-10-15, "Update FLAGS.md"; 184 commits). Remote branches: `main`, `per-pixel-lighting`, `pixel-shaders` |
| Local branch | `android-port-audit` (created from `main`). The only changes are these uncommitted audit documents. Nothing is pushed. |
| Relevant build configurations | `port/build.py` (lift + CMake; targets `linux`/`windows`; `--cache-regs`, `--fastpaths`, `--abi-check`, `--prof`, `--low-dll-bases`); `port/CMakeLists.txt` (`PORT_BACKEND`=sdl2/null/vita, `PORT_SYSTEM`=posix/win32, `PORT_PREEMPT`, `PORT_VIDEO`, `PORT_SHADERS`, `PORT_PPL`) |
| Shockolate Android project | `<Shockolate Android checkout>`, branch `touch-controls` at `7502370` (38 commits not pushed, plus uncommitted, unaccepted polish work). Not modified. |
| Accepted touch baseline | Committed `7502370`, files in `AndroidProject/app/jni/touch/` (TOUCH_CONTROLS.md §1) |
| Android toolchain on the PC | SDK with NDK `29.0.14206865` (clang 21), as used by the Shockolate build |
| Local SS2 install | `<LaunchBox install>`: GOG + SS2Tool + Blue's Mod Manager, **NewDark 2.47** (not 2.48), mods inactive. Read only, unchanged. |

## Reports

| Report | Contents |
| --- | --- |
| [ANDROID_COMPATIBILITY.md](ANDROID_COMPATIBILITY.md) | Platform matrix, the confirmed blocker, required backend changes |
| [TOUCH_CONTROLS.md](TOUCH_CONTROLS.md) | Shockolate source inventory, reuse plan, SS2 input mapping, risks |
| [MOD_COMPATIBILITY.md](MOD_COMPATIBILITY.md) | Data vs script vs native mods, module identity checks, search paths |
| [SCP_COMPATIBILITY.md](SCP_COMPATIBILITY.md) | SCP version, components, dependencies, saves, test plan |
| [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) | Architecture, storage design, milestones, performance plan, first task |
| [PHASE1A_RESULTS.md](PHASE1A_RESULTS.md) | Phase 1A: arm64 setjmp fix, conformance through `adb shell` on the Galaxy S26 Ultra |
| [PHASE1B_RESULTS.md](PHASE1B_RESULTS.md) | Phase 1B: the APK (`android/`), conformance inside the app process, game-file and graphics preparation |
| [PHASE1C_RESULTS.md](PHASE1C_RESULTS.md) | Phase 1C: the recompiled NewDark 2.48 game running in the app, headless and with SDL2 + OpenGL ES; first frames on the S26 Ultra |
| [PHASE1D_RESULTS.md](PHASE1D_RESULTS.md) | Phase 1D: gameplay on the S26 Ultra (Earth and MedSci: movement, mouse look, frob, inventory, PDA, melee, save/load), input, audio, performance, lifecycle, write-folder regression test |
| [PHASE1E_RESULTS.md](PHASE1E_RESULTS.md) | Phase 1E: the Earth grav-shaft "defect" was a test error (only the low-gravity descent shaft was tried). The lift shaft works on Android and a Windows desktop build, with matching scripted position traces. |
| [PHASE2A_RESULTS.md](PHASE2A_RESULTS.md) | Phase 2A: the Shockolate touch controls (7502370) in SS2 on Android: stick, look, fire, use, jump, crouch, weapons, menus, cursor mode with exact placement, inventory and PDA by touch; multitouch tested on device |
| [PHASE2A_FRAME_LIMIT_FIX.md](PHASE2A_FRAME_LIMIT_FIX.md) | Phase 2A fix: play from the launcher button ended after ~40 s at the launcher's default 3600-frame limit (a clean exit, not a crash). The windowed game now has no default limit; tests pass `--ei frames N`. |
| [PHASE2B_UI_SCALING.md](PHASE2B_UI_SCALING.md) | Phase 2B-1: the HUD, inventory, PDA and text up to 1.5x larger at 1280x720 (2.25 screen px per interface px) with NewDark's own `d3d_disp_scaled_2d_overlay`, chosen in the launcher; touch placement fixed for 4:3 menus at wide resolutions |
| [PHASE2B2_RESULTS.md](PHASE2B2_RESULTS.md) | Phase 2B-2 (Milestone A, PARTIAL): KBD button for Android's keyboard, default buttons moved off the 640x480 interface into the bars, layout upgrade only for never-moved layouts; device checks by frame capture (a third-party overlay blocked touch input) |
| [PHASE3A_NVSCRIPT_RESULTS.md](PHASE3A_NVSCRIPT_RESULTS.md) | Phase 3A (Milestone B, PASS): SCP Beta 8's NVScript.osm (MSVC 2017) recompiled: SSE/SSE2 in the lifter (differentially tested against the CPU), static TLS and newer-CRT shims in the host; loads and runs on Android and Windows; SCP's module gate opens only with it |
| [PHASE3B_SCP_RESULTS.md](PHASE3B_SCP_RESULTS.md) | Phase 3B (Milestone C, PARTIAL): SCP Beta 8 on the S26 Ultra from a DMM-style copy: missions, gamesys, assets, Squirrel and NVScript scripts, the module gate, both Earth grav shafts, SCP's UNN changes, quicksave/quickload; level transition and item use not reached |
| [PHASE4_ANDROID_POLISH_RESULTS.md](PHASE4_ANDROID_POLISH_RESULTS.md) | Phase 4 (Milestone D, PARTIAL): Play button and game folders, folder import (not device-tested), lifecycle and soak tests, dev/play builds (play: 43.7 MB) |
| [OVERNIGHT_INTEGRATION_REPORT.md](OVERNIGHT_INTEGRATION_REPORT.md) | The overnight session (Phase 2B-2 to Phase 4): milestone labels, commits, APK hashes, tests, open items |
| [GAME_IMPORT_FIX.md](GAME_IMPORT_FIX.md) | Import fix: `games/` belongs to the adb shell, so imports go to the app-owned `imported/`; scan-then-confirm import with saves into the new game's own profile, crash recovery, `SS2.exe` version check (the user's install is NewDark 2.47); validated on the S26 Ultra |
| [LEGACY_SAVE_COMPATIBILITY.md](LEGACY_SAVE_COMPATIBILITY.md) | The user's 15 NewDark 2.47 / SCP Beta 4 saves under the port's SCP Beta 8 setup: they load and play in short tests, with SCP's "mid-game activation" warning, one reused archetype ID in Operations and two missing models |
| [LEGACY_SCP4_FEASIBILITY.md](LEGACY_SCP4_FEASIBILITY.md) | Audit: running the original SCP Beta 4 mod set on the 2.48 port. Every save dependency resolves in the original set; the blocker is recompiling NVScript 1.2.4; recommends a side-by-side build and a PC-referenced validation |
| [LEGACY_SCP4_RUNTIME_RESULTS.md](LEGACY_SCP4_RUNTIME_RESULTS.md) | Phase 3C: NVScript 1.2.4 recompiled; the original SCP Beta 4 + 17-mod setup runs in a side-by-side app (`…ss2.legacytest`). All 15 saves load on desktop and 8 on the phone; saving keeps every quest variable and NVScript record. Level changes, replicators and psi are not yet tested |
| [LEGACY_AUDIO_RESULTS.md](LEGACY_AUDIO_RESULTS.md) | Phase 3D-A: the legacy app was silent because the PC's `cam.cfg` selects OpenAL (`sfx_device 4`), which the port does not have; the launcher now switches that one line to DirectSound. PCM, Android player and lifecycle verified; listening is the user's |
| [RELATIVE_CURSOR_RESULTS.md](RELATIVE_CURSOR_RESULTS.md) | Phase 3D-B: an optional relative (trackpad) cursor style next to the default direct one (SET, MORE, CURSOR; speed 0.25-4); hold to drag. Verified on the phone through the real touchscreen path at three resolutions; comfort is the user's |
| [AUDIO_CURSOR_INTEGRATION_RESULTS.md](AUDIO_CURSOR_INTEGRATION_RESULTS.md) | Phase 3D integration: audio root cause and fix, both cursor styles, conformance 0/511, 0/511 (ABI), 0/113, touch 345/345, core suites, final run on the real profile, APK identities, the player's checklist |
| [PHASE3E_UNIFIED_APK_RESULTS.md](PHASE3E_UNIFIED_APK_RESULTS.md) | Phase 3E: one APK (`local.newdarkandroid.ss2`) for SCP Beta 4 (legacy) and SCP Beta 8; NVScript chosen by the file's identity; CROUCH fixed for the legacy bindings; legacy game and saves migrated with every file verified; APK hashes and the player's checklist |
| [PHASE3E_CROUCH_FIX.md](PHASE3E_CROUCH_FIX.md) | Phase 3E-A: why CROUCH only made the legacy player creep (Shift is `+creepon` in that `user.bnd`), and the bind-aware fix |
| [PHASE3E_MODULE_SELECTION.md](PHASE3E_MODULE_SELECTION.md) | Phase 3E-B: several NVScript versions in one build (`port/nvscript_versions.json`), selection by size, base, entry point and code CRC, refusal cases |
| [PHASE3F_ADVANCED_TOUCH_RESULTS.md](PHASE3F_ADVANCED_TOUCH_RESULTS.md) | Phase 3F: radial menus from the game's own state: weapons, ammunition and settings, psi powers, hypos and patches (new ITEM button), quicksave/quickload and logs; verified on desktop and the phone |
| [PHASE3G_FINAL_UX_POLISH.md](PHASE3G_FINAL_UX_POLISH.md) | Phase 3G: radial menus open around the held button (fanned at screen edges), the launcher redesigned as a game launcher (diagnostics behind Advanced), an original adaptive wrench icon; 0.9.3-polish |

## Overall feasibility

**Feasible, and NewDarkRecompiled is a good foundation.** The portable host has a single, small platform interface
(`port/include/plat.h`). Everything above it is OS-free C with no x86 assumptions. It already runs on one ARM device
(the PS Vita, with an OpenGL ES 2.0 renderer shared with the SDL2 backend). An Android port is mostly a new backend
and packaging, not engine work.

## Current technical readiness

- **Works today on other platforms (not tested on Android):** POSIX system layer, GLES 2.0 renderer, software audio
  mixer, Win32 path translation with case-insensitive lookup and a separate write directory, cutscene decoder,
  recompiled `allobjs.osm`/`Squirrel.osm`/`lgvid.dll`.
- **Confirmed blocker (small):** NDK clang rejects `__builtin_setjmp` on arm64 (checked here with NDK 29). This is
  about a 6-line fix in `runtime/rt.h`.
- **Missing:** an Android build target (shared library, NDK toolchain), an Android entry point (SDL_main, big-stack
  thread, logging, options, GLES default, soft-keyboard handling, lifecycle), APK packaging, game-folder access, and
  the touch bridge.
- **SCP:** blocked today, because SCP needs the native **NVScript.osm**, which is not among the recompiled modules.
  SCP's own start-up check stops progress on Earth if it is missing. NVScript.osm (MSVC 2008, static CRT,
  relocatable) looks like the already-recompiled `squirrel.osm`, so recompiling it is the expected fix.

## Major risks

1. **NVScript.osm recompilation** could hit unsupported constructs (C++ `throw`, SSE paths), which would block SCP and
   SS2Tool-patched installs.
2. **Feel of the touch controls:** the host offers no analog movement path (DirectInput joysticks are not
   implemented), so the Shockolate analog stick becomes digital W/A/S/D unless more host work is done.
3. **Automatic cursor/interface-mode detection** may not be possible from the signals the host sees. The manual
   toggle stays as the fallback.
4. **Android lifecycle and GPU driver behaviour** (context across background/foreground, mediump precision, shader
   compile hitches) can only be found on hardware.
5. **Distribution:** the APK contains code generated from the user's game files, so it can only ever be a personal
   build. It is never a public release.
6. **Prerequisites:** the local install is NewDark 2.47, and a 2.48 install is required. The accepted touch code
   exists only in an unpushed local branch.

## Recommended next action

Start Phase 1 with the task in IMPLEMENTATION_PLAN.md §6: fix the arm64 setjmp path, then run upstream's game-free
conformance suite on the Pixel 10 via `adb`. Before that, the owner should obtain a NewDark 2.48 install for lifting
and approve installing Python packages and test binaries.

## Fact vs inference

Statements with file and line references come from the source. Results marked *verified here* were run in this
audit (only the NDK `__builtin_setjmp` probe and read-only PE header and string scans of local game files). Anything
labelled Unknown, inference, or "likely" has not been demonstrated. No part of the game has been built or run on
Android, and no claim is made that SCP or any mod works on the portable host.
