# Overnight integration report: Phase 2B-2 to Phase 4

Session of 2026-10-18, about 03:15–05:30 (America/New_York), on `android-port` from `f366189` (Phase 2B-1). Device:
Galaxy S26 Ultra (SM-S948U1, Android 17/API 37, arm64). Nothing was pushed or published.

**One condition shaped the whole night.** A third-party app on the phone, `io.japp.blackscreen` ("Black Screen"),
kept a full-screen overlay up from start to finish. It held input focus, received every touch and key (including the
HID test touchscreen and the on-screen keyboard), and made `screencap` black. It was left alone: it is the player's
app. The game kept running underneath, so the device work used the host's scripted input
(`SS2PORT_INPUT_SCRIPT`) and a new in-game frame capture (`SS2PORT_SHOT_DIR`). **No touch reached the game this
session**, and nothing below claims touch acceptance.

## 1. Milestones

| Milestone | Label | In one line |
| --- | --- | --- |
| A, touchscreen polish | **PARTIAL** | KBD (Android keyboard) button and new default layout done and host-tested; layout verified on the device by frame capture at three resolutions; KBD and the touch acceptance tests could not be run (overlay) |
| B, NVScript | **PASS** | SCP Beta 8's `NVScript.osm` (MSVC 2017) recompiled; SSE2 support in the lifter, differentially tested against the CPU; static TLS and CRT shims in the host; loads, initialises and runs on Android and Windows; SCP's module gate opens only with it |
| C, SCP | **PARTIAL** | SCP Beta 8 runs from a DMM-style copy; its missions, gamesys, assets, Squirrel and NVScript scripts, Earth (both shafts, UNN changes), quicksave/quickload verified; level transition, item use and a measured rule change not reached |
| D, Android polish | **PARTIAL** | Play launcher, game folders, lifecycle and soak tests, dev/play builds done; folder import implemented but not device-tested (picker blocked by the overlay) |

## 2. Commits (local, `android-port`)

| Commit | Content |
| --- | --- |
| `018ebae` | Phase 2B-2 (A): KBD, default layout, layout upgrade, frame capture and `shot` test tooling |
| `2405785` | Phase 3A (B): `lift_sse.py`, lifter and runtime SSE2/P6 support, static TLS, kernel32 shims, `--nvscript`, `harness/sse_test` |
| `a5efed2` | Phase 3B (C): game folder selection in the launcher, SCP results and evidence |
| `ee9b99d` | Phase 4 (D): Play launcher, `GameImport`, dev/play flavors |
| (this commit) | This report; the final dev APK hash in the Phase 4 report |

## 3. APKs (all debug-signed personal builds; preserved under `build/android/baseline/`)

| Version | SHA-256 | Notes |
| --- | --- | --- |
| `0.5.0-phase2b1` (before) | `9850e11ca452df86a113a78ec0d58e231250ca85b8a60b19c9eb3c62af507511` | verified unchanged |
| `0.6.0-phase2b2` | `8673f4dcba26de0e4c21e34b08a32d80fec92667cabeb65d89755f36321e3053` | Milestone A only |
| `0.7.0-phase3a` | `a59d7f9285b0247714639a3f1cd0d6b77cc54ae3009e2c8ee89a3c1ba7578042` | with NVScript; its launcher already had the game-folder, Play and import changes (committed later in C and D) |
| `0.8.0-phase4` dev | `89f28ce444d1685c649155d60f811862cb67624c0ecf605264ec542681f5b3db` | everything, 88.6 MB (conformance libraries rebuilt with this session's code) |
| `0.8.0-phase4` play | `3b4ed73db444e550ad13613ed1ddd438d171e5e942e3176f725d2af2c49b9f1c` | game only, 43.7 MB. **Installed on the phone at the end** |
| `libss2game.so` (Phase 3A and 4) | `6c319481bc6dc8cd1051ab00b263720d49c91ef4ce35691f1f5a56cda822191d` | built with `--nvscript` |

## 4. NVScript

`NVScript.osm` from `SCP_beta8.7z`: 704,512 bytes, sha256 `f15fc91b…f11a`, Nameless Voice 2024, MSVC 2017, static TLS.
It recompiles to 4,521 functions. The instructions left as faults are only in CPU-gated CRT paths and data. It links
into the Android and Windows hosts, loads behind the unchanged CRC identity check, gets its TLS slot, `DllMain`
returns 1, and the engine calls it about 4,000–7,400 times per 20 s of play. Functional proof: SCP blocks Earth's
first grav shaft with "SCP ERROR: NVSCRIPT.OSM NOT LOADED" when the module is absent. With the recompiled module it
does not, on both arm64 and x86-64. The existing modules' behaviour is unchanged: allobjs lifts identically, and the
other modules differ only where former faults are now translated. Details: [PHASE3A_NVSCRIPT_RESULTS.md](PHASE3A_NVSCRIPT_RESULTS.md).

## 5. SCP

Tested version: **SCP Beta 8** (December 25, 2025), sha256 `bcf547c7…ddb3f6`, on Steam NewDark 2.48, installed as DMM
would in separate folders (`games/ss2scp` on the phone, `build/scp/game-scp` on the PC) with its own write folder.
Verified on the phone: new game; SCP's Earth (payphone, module gate, UNN front doors, lobby furniture); both grav
shafts (52.8 at the lift top; descent apex 17.54, as vanilla); the training-sim teleport; quicksave and quickload,
with the modules re-initialised; the mod path order; `.mis`, `.gam`, `.crf` and DML handling (the Steam `misdml` is
correctly rejected by its SCP fingerprint); Squirrel and NVScript scripts; case-insensitive paths. A 6.5-minute SCP
soak ran at 99 fps with flat memory. **Not reached:** Earth → Station, item pickup/use, a measured SCP rule change.
Details: [PHASE3B_SCP_RESULTS.md](PHASE3B_SCP_RESULTS.md).

## 6. Touchscreen changes

- **KBD** button (right bar): shows/hides Android's keyboard through SDL. Typed characters reach the game as key
  presses plus text, like a physical keyboard. While it is up in look mode, only II, KBD, MODE and SET take a touch.
  The manifest now uses `adjustNothing`, so the keyboard never moves the game.
- **Defaults** moved off the 640×480 interface: SET, AMMO/USE and KBD into the letterbox bars, MODE right of EQUIP.
  Verified by frame captures at 1280×720 (menu, inventory, PDA), 800×600 and 1024×768.
- **The player's layout:** never moved, so it gets the new positions in memory; the file stays byte-identical. A
  moved or resized layout is never changed. The control feel (stick, look, fire, use, cursor) is untouched.
- Bridge tests 169/169 (87 before); Shockolate's six core suites pass.

Details: [PHASE2B2_RESULTS.md](PHASE2B2_RESULTS.md).

## 7. Android usability changes

- Launcher: **Play** first (touch on, no frame limit, Interface size *largest*), **Game folder** (one write folder
  per game folder), **Interface size**, **Import a game folder...**, then the diagnostics (dev build). Player-facing
  error messages.
- `GameImport`: SAF folder picker, staged copy with completeness checks, never replaces a folder or touches saves
  (**not device-tested**).
- `dev` and `play` builds of the same package (88.6 MB / 43.7 MB).
- Test tooling: in-game frame capture (`SS2PORT_SHOT_DIR`, script `shot`), per-module call counters
  (`SS2PORT_MODSTATS=1`), a script generator (`docs/android-audit/phase3b/scp_script.py`).

Details: [PHASE4_ANDROID_POLISH_RESULTS.md](PHASE4_ANDROID_POLISH_RESULTS.md).

## 8. Tests performed

| Test | Result |
| --- | --- |
| SSE differential test (`harness/sse_test`, native CPU vs lifted C) | 435 forms, 870,000 trials, **0 differ** (154 NaN-payload-only results reported separately) |
| Touch bridge suite | **169/169**; Shockolate core suites pass |
| Conformance, Android via adb (new lifter and host) | **0/511**, **0/511** cache-regs (0 ABI violations), **0/113** write-dir (game folder unchanged) |
| Conformance, in the final dev APK (`--es autorun all`) | **0/511**, **0/511** |
| Conformance, Windows native | **0/511**, **0/511** cache-regs (0 ABI violations), **0/113** write-dir |
| Existing modules' lifts vs the pre-session lift | allobjs identical; SS2.exe, Squirrel, lgvid: only former UNIMPL faults changed |
| Vanilla startup | play and dev APKs: menu, load save, play (PASS, exit 0) |
| Vanilla save/load | quicksave → move → quickload restores the position |
| Inventory/PDA (scripted cursor clicks) | inventory and PDA open; layout checked |
| SCP | §5 (phone), and the gate run on Windows x86-64 |
| Lifecycle | Home/resume: pause, resume at ~100 fps, audio back, surface correct, input works |
| Soak | 390 s SCP, exit 0, 99 fps, RSS flat ~306 MB, 0 frames > 50 ms |
| Failure reporting | `neg-exe` exit 2, `neg-crash` signal 11, both reported |
| Player data | Saves, `cam.cfg`, `user.bnd`, `user.cfg`, `touch-settings.cfg` byte-identical to the pre-session backup (only `SS2.log` differs) |
| **Not performed** | any real or HID touch input, the on-screen keyboard, multitouch, inventory drag by touch, touch after resume, the folder-import picker, Earth → Station, SCP item use, Linux builds |

Reproduction commands are in each phase report. Device evidence (frame captures, logs) is under `docs/android-audit/phase2b2/`,
`phase3a/`, `phase3b/` and `phase4/`.

## 9. Known defects and limitations

- **Touch acceptance outstanding** (overlay): the KBD keyboard path, typing lock-out, multitouch and touch after
  resume are unverified on hardware.
- **Folder import** is unverified on hardware. `adb push` remains the verified way to install a game folder.
- **SCP:** level transitions, items and SCP's rule changes are untested. In Earth's basic-training sim the player
  stopped moving after the teleport, **in vanilla as well**. Whether that is the sim's design or a pre-existing port
  issue needs a person to play it.
- **Lifter SSE:** arithmetic always rounds to nearest (MXCSR rounding applies to float→int conversions only); MXCSR
  exception flags are not kept; which NaN propagates can differ from x86. C++ exceptions/SEH still end the game
  (none seen; NVScript's script code does not throw).
- Only the exact `NVScript.osm` that was lifted is accepted; another NVScript needs a rebuild with `--nvscript`.
- Static TLS: 8 modules, 4 KB each, no TLS callbacks.
- The APKs are debug builds (needed for `run-as`), with no release signing. They contain code generated from the
  user's own game files and GPL touch code: personal use only, not for distribution.
- Scripted test input (not touch) needs `place` plus a held click to click menus; relative `move` plus `click` does
  not register in NewDark's menus.

## 10. Recommended next work

1. **With the overlay off:** the 10-minute touch checklist in PHASE2B2_RESULTS.md §6 (KBD, typing, layout, editor),
   then multitouch and touch after resume.
2. **SCP by hand** (~15 min, `Game folder: ss2scp`): finish or skip training, pick a service, confirm **Station
   loads**, pick up and use an item (cigarettes: +1 PSI), save, restart, load.
3. **Try Import a game folder...** with a copy of the PC's SS2 folder (or the SCP folder) on the phone's storage.
4. If the basic-training sim really freezes for a person too, compare with the Windows desktop host to tell a port
   defect from the sim's design.
5. Optional: a release-signed `play` build for daily use (keep `dev` for tests); a Linux build of this session's
   lifter and host changes.
