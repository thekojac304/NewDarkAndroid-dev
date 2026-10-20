# Phase 1D results: first playable gameplay

2026-10-17. Goal: show that System Shock 2 is actually playable on the Galaxy S26 Ultra, not just showing the intro
and main menu.

**Out of scope, and not done:** no Shockolate or touch controls, no SCP or NVScript, no optimisation. Shockolate, the
lifter, `runtime/`, the generated code and the Steam installation were not modified. Nothing was pushed.

> **Correction (Phase 1E):** the "Earth grav shafts do not lift the player" defect below was a **test error**, not a
> port defect. Every attempt here was in the x = −30 shaft, which is a 20 % low-gravity *descent* shaft: a high, slow
> jump that falls back is correct there. The lift is the x = −24 shaft. Walked into, it lifts the player from the floor
> to the top exit on Android and on a Windows desktop build alike, with matching positions. See
> [PHASE1E_RESULTS.md](PHASE1E_RESULTS.md). The original text is kept below, with the wrong statements struck through.

## Summary

**The primary success criterion is met.** The recompiled game loads playable areas on Android, and real gameplay input
works: Earth (the tram station where character creation starts) and the MedSci deck of the Von Braun.

| Area | Result |
| --- | --- |
| New game, 3D world | **PASS.** New Game, difficulty, the start cutscene, then Earth renders in 3D on the Adreno 840 with the HUD. MedSci renders with full HUD. |
| `allobjs.osm`, `Squirrel.osm` | **PASS (load and init).** Both load as recompiled modules at their lifted bases with all imports bound and `DllMain` → 1. On a save load both unload cleanly and load again. No Squirrel scripts are installed, so no `.nut` script ran. |
| Movement, mouse look | **PASS.** Held keys move the player (`w`/`a`/`s`, jump). Mouse look uses Android pointer capture with raw relative motion. Yaw and pitch both work. |
| Object interaction | **PASS.** Frob (right button): "A cryo tube door." Summon and frob: "Wrench picked up." |
| Inventory, PDA | **PASS.** Tab opens interface mode: inventory grid, equipment, status bar. The PDA opens (Emails: Deck 1 Engineering, with Email/Logs/Notes/Help tabs). The status-bar icons answer clicks. |
| Combat | **PASS (melee).** `1` equips the wrench ("Wrench equipped"). Left button swings it, captured mid-swing. No enemy was fought and no gun was fired. |
| Save and reload | **PASS.** The save dialog and typed name work. Saves land in `files/profiles/ss2/save_N/`. They reload in the same session and after a cold process restart (Earth 614 KB, MedSci 1.5 MB). |
| Character creation | **Not completed.** ~~The Earth grav shafts do not lift the player, so the path to the recruitment choices cannot be walked (§3.3, open defect).~~ *Phase 1E: test error, only the descent shaft was tried; the lift shaft works.* MedSci was reached with NewDark's `first_level` setting instead. |
| Audio | Initialised and producing non-silent samples at 43 callbacks/s with no starvation. AudioFlinger shows 0 underruns on our track. **Not listened to** (§5). |
| Performance | MedSci, default lift: **99–100 fps** (the game's own `framerate_cap 100`), frame time p95 10.8 ms, p99 ≤ 12 ms. One game thread at ~71 % of a core. RSS 281–285 MB. Level loads take about 1.0–1.2 s, with one 440–500 ms frame (§6). |
| Lifecycle | Home → icon, Recents and Back all keep or resume the same game process, with picture, sound and mouse capture. Destroying the activity ends the process cleanly. Two defects found and fixed (§7). Screen off was not tested. |
| Write-folder regression test | **Added.** 113 checks on `_findfirst` and `FindFirstFileA` over merged folders. PASS on Android (default and `--cache-regs --abi-check`) and native Windows (both). With the Phase 1C bug put back, it fails 19 checks (§8). |
| Phase 1B/1C regression | In-app `all` 0/511 twice, `neg` FAIL as designed, `game-null` PASS. Android `adb` and native Windows hosts 0/511 in both lift modes. |
| Crashes | None. The crash buffer is empty, with no `[port] FATAL` and no warnings in any game log. |

## 1. Build identity and baseline

| | |
| --- | --- |
| Branch | `android-port`, clean at the start, parent `be0e906` (Phase 1C) |
| Phase 1D commit | The commit that adds this file |
| Phase 1C APK preserved | `build/android/baseline/phase1c/app-debug-phase1c.apk`, sha256 `c23baf5a…186d92e9`, the same as PHASE1C_RESULTS.md. Its five `.so` files and `SHA256SUMS` are alongside (git-ignored). |
| Phase 1D APK | `0.3.0-phase1d` (versionCode 3), 117,667,636 bytes, sha256 `419de6c0d7239e50765e4d5c30d3cdfd8ca02ef285547b646dd4bcee9fe6a94c`, built from this commit. A copy is in `build/android/baseline/phase1d/` with `libss2game.so` (sha256 `79ae07ff…301994b5`). The gameplay runs used builds that differed only in the fixes as they were added (§4); the final APK was re-checked with in-app `all` (0/511 twice) and a windowed 900-frame game run (PASS). |
| Game inputs | The same Steam 2.48 files as Phase 1C. After the phase, `SS2.exe`, `allobjs.osm`, `squirrel.osm` and `lgvid.dll` hash as in PHASE1C_RESULTS.md §2, and no file in the install is newer than the start of the session. |
| Lift | The default lift (no `--cache-regs`), unchanged from Phase 1C (`src.sha1` signatures matched, nothing re-lifted) |
| Toolchain | Unchanged: NDK r29, SDK CMake 3.22.1 with Ninja, Gradle 8.7 offline, SDL 2.32.10 |
| Device | Galaxy S26 Ultra `SM-S948U1` (serial `<serial>`), Android 17, Adreno 840, 2340×1080 at 120 Hz |

## 2. Input

### 2.1 Why the injected `3` did nothing

**It reached the game and the key path works.** The menus simply don't use digit hotkeys.

- With `SS2PORT_INPUT_DEBUG=1`, `adb shell input keyevent KEYCODE_3` arrives as
  `text "3"` + `key down scancode 32 (3) dik 0x04` + `key up`. A key from a physical keyboard (the HID tool, §2.2)
  arrives the same way.
- SDL 2.32 starts text input at video init with the on-screen keyboard hint off, so `WM_CHAR` is delivered too.
  Skipping `SDL_StartTextInput` in Phase 1C did not affect this.
- A `--trace-only` run shows the menu polling only mouse buttons (`GetAsyncKeyState` 1, 2, 4) and building its key table
  with `MapVirtualKeyA`. The table maps correctly (VK `0x33` → scan `0x04`, etc.).
- The digit is ignored on the main menu, the difficulty screen and the Options tabs, from both injected and physical
  keyboards. Escape works everywhere.
- **In the game, digits work**: `1` runs `user.bnd`'s `equip_weapon wrench`.
- An experiment that delivered `WM_CHAR` after `WM_KEYDOWN` (Windows' order; SDL on Android sends the text first)
  changed nothing, so it was reverted.

### 2.2 How to drive the game for testing

| Method | Verdict |
| --- | --- |
| Physical USB/Bluetooth keyboard and mouse | Expected to work: it is the same Android path as the tool below. **Not tried with real hardware here** (none attached). |
| **`android/tools/uhid_input.c`** (new, test-only) | **Reliable.** The shell user may open `/dev/uhid`, so this small native tool creates a real HID keyboard and mouse. Android lists it as `KEYBOARD \| ALPHAKEY \| EXTERNAL` and `CURSOR \| EXTERNAL`. Keys can be held for any time, and the mouse gets pointer capture with raw motion. All gameplay in this report was driven with it ([android/tools/README.md](../../android/tools/README.md)). |
| `adb shell input keyevent` | Works for single key presses (Escape, letters, typing). It cannot hold keys long enough to walk. |
| `adb shell input tap` | **Does not work for menus.** SDL turns the tap into an emulated mouse click at the last mouse position, not the finger's (`mouse button 1 down at 540,270 (from touch)`). The game keeps its DirectInput mouse exclusive even in menus and moves its own cursor only by relative motion, so a tap never places it. |

**Pointing at a menu item:** `move -3000 -3000` sends the game's cursor to the picture's top-left. Then `move X Y`
puts it on game pixel (X, Y) of the 800×600 picture. This was exact on every screen used.

**An uncapped session** is `--ei frames 0` (already supported). The run ends when the game quits or its activity is
destroyed. New launcher extras: `--es env NAME=value,...` (for example `SS2PORT_PROFILE=1`) and `--es portargs "..."`
(for example `--trace-only ...`).

**Device note:** the first time a new keyboard presses Esc, Samsung One UI shows a system "Use Esc as Back?" prompt over
the game. It has to be dismissed once on the device.

## 3. Gameplay on the device

### 3.1 What was exercised

All runs used `libss2game.so` (SDL2 + OpenGL ES) with `--ei frames 0`, from a cold start.

1. Escape skips the intro. **New Game** → difficulty → **Start Game**. The start cutscene (a second `lgvid` stream,
   320×240 Indeo → 640×480) plays and is skipped with Escape.
2. **Earth, tram station** (`earth.mis`).
   - The 3D world renders: tram, platform, lighting, grav-shaft particles.
   - Walking forward, back and strafing, mouse yaw, pitch and jump all work.
   - Frob on the tram doors.
   - Saved as "shaft" and reloaded. That run used an early Phase 1D build: the diagnostics only, before fixes 1–4.
3. **MedSci deck** (`medsci1.mis`), reached with a one-line `first_level medsci1.mis` in `files/profiles/ss2/user.cfg`.
   NewDark's `doc/fm.cfg` documents this Shock setting for FMs; it also works from `user.cfg`. The game folder is
   untouched.
   - Full HUD with health 35 and psi 15.
   - Tab interface mode: inventory, equipment, status icons ("0 cyber modules"), PDA.
   - Frob a cryo-tube door.
   - The command prompt (`:`, shifted characters) runs `summon_obj wrench`. Frob picks it up, `1` equips it, the left
     button swings it.
   - Saved as "medsci", the process was force-stopped, a cold start loaded the save from the main menu, and MedSci came
     back.
   - Whether the saved *position* was restored was not checked.
4. The `user.cfg` override was removed from the device afterwards, so the default campaign starts on Earth again. The
   saves are kept.

### 3.2 Module-load evidence

From `phase1d/game-earth-run.log.txt` and `phase1d/game-medsci-run.log.txt` (thunk traces removed):

```
[port] allobjs.osm: loaded the recompiled module at 10000000 (61 imports bound), DllMain -> 1
[port] Squirrel.osm: loaded the recompiled module at 30000000 (112 imports bound), DllMain -> 1
[port] not found (FindFirstFile): "osm\sq_scripts\*.nut"          (no Squirrel scripts installed; searched 5 paths)
[port] Direct3D: pixel shaders are translated to GLSL
... on loading a save:
[port] allobjs.osm: FreeLibrary (references 1, thread 1004)  ->  unloaded (DllMain -> 1)
[port] Squirrel.osm: FreeLibrary (references 1, thread 1004) ->  unloaded (DllMain -> 1)
[port] allobjs.osm: loaded the recompiled module at 10000000 (61 imports bound), DllMain -> 1
```

- Squirrel's CRT start-up probes `FlsAlloc`, `EncodePointer` and similar through `GetProcAddress`. They are reported as
  not available and the CRT uses its fallbacks. This is not fatal, and init completes.
- The many `not found (stat)` lines are the game's normal resource search-path probes (`<local path>`, `<local path>`, …).
  The files are found in later search paths.
- `lgvid.dll` decodes both cutscenes.

**Not shown:** that individual `allobjs.osm` scripts ran. Door, pickup and HUD behaviour all worked, but I have no
per-script trace.

### 3.3 ~~Open defect: the Earth grav shafts do not lift the player~~ (test error, see Phase 1E)

*(The user confirmed that these are grav shafts, with no buttons or doors.)*

> **Phase 1E:** "either shaft" is wrong. The second attempt meant for the other shaft re-entered the same left disc, so
> only the x = −30 descent shaft (room 44, `RoomGrav` 20) was tested. What is described below is correct behaviour for
> that shaft. The lift shaft (x = −24, room 43, `RoomGrav` −10) works ([PHASE1E_RESULTS.md](PHASE1E_RESULTS.md)).

**What happens**

- ~~Walking into either shaft~~ Walking into the x = −30 shaft onto its emitter disc and waiting more than 5 s: the
  player stays on the floor.
- Jumping inside gives a short rise, then the player falls back. Holding jump while pushing forward does not reach the
  upper exit.
- With `phys_freq 60` and `framerate_cap 60` in a write-folder `cam_ext.cfg`, the result was the same, so it is not the
  100 fps physics rate.
- `EARTH.MIS` sets the room-gravity property `P$RoomGrav` on three rooms (values 20, 0 and −10), so the shaft depends
  on room gravity.

**Not investigated**

- Whether the room lookup, gravity property or physics integration differs in the recompiled code on arm64. Candidates
  are x87 80-bit vs double precision in the physics, and room-portal tests. Game code is already built with
  `-ffp-contract=off`.
- **Whether a desktop build of the same recompiled code has the same bug.** This is the next check, because it decides
  whether the bug is Android-specific.

**Impact:** ~~a normal new game cannot leave Earth, so~~ character creation (the three recruitment choices) was not
reached. The rest of gameplay was tested on MedSci. *(Phase 1E: the lift works, so the route is open; character
creation itself is still untested.)*

## 4. Defects found and fixed

| # | Defect | Fix |
| --- | --- | --- |
| 1 | **Mouse capture lost in the in-game menu.** Opening the Esc menu on MedSci made every click land in the wrong place. Pointer capture showed `UNCAPTURED` while SDL was in relative mode, so hover positions became huge relative motion (`xrel=536`). Cause: SDL calls `releasePointerCapture()`/`requestPointerCapture()` straight from the game thread. The game frees and recaptures the mouse in the same frame, and the view drops the second request while it still thinks it holds capture. | `plat_sdl2.c`, Android only: 300 ms after each switch to relative mode, ask again through SDL's own `reclaimRelativeMouseModeIfNeeded()` over JNI. JNI uses the activity's class, because `FindClass` on the game thread cannot see app classes. Afterwards capture stays `RELATIVE` through the menu, and Save works. |
| 2 | **Reopening the app from the home screen destroyed the game's activity**, but the game kept running without a window at ~96 % CPU. Cause: `LauncherActivity` was `singleTask`, the root of the game's task, and relaunching it cleared the activity above it. `SDL_APP_TERMINATING` was ignored. | The manifest uses `singleTop`. A launcher opened from the home screen while it is not the task root finishes at once, so the game simply resumes. `plat_sdl2.c` (Android) handles `SDL_APP_TERMINATING` with `port_exit(0)`, which flushes writes, records `exit 0` and gets a PASS verdict. |
| 3 | **Back could finish the game's activity.** SDL's default `onBackPressed` calls through to Android. | `android_main.c` sets `SDL_ANDROID_TRAP_BACK_BUTTON=1` (with the other SDL settings, overridable). Back now reaches the game as a key (`AC Back`, which has no DirectInput code, so the game ignores it) and the activity stays. |
| 4 | **After `am force-stop` of a game run, every later `--es autorun` was ignored** ("a run is already in progress"), because the dead run stayed pending until the launcher was next resumed. | `LauncherActivity.settleDeadRun()`: reports a pending run whose process is gone (FAIL, with its status) before queueing the next one. |

## 5. Audio

**Evidence that audio is processed** (MedSci, `SS2PORT_PROFILE=1`, every 5 s window):

- `DirectSound: mixing to 44100 Hz stereo`. SDL audio opened at 44.1 kHz, S16, 1024 frames.
- **215–216 callbacks per 5 s in every window**, including level loads. That is 43/s, exactly 44100/1024: no
  starvation and no stalls in the mixer callback.
- The longest gap between callbacks is 40–41 ms, steady, from Android's burst scheduling. Once, right after resume, it
  was 55 ms.
- **Peak sample level** is 4,065–21,222 per window. The samples are not silent. 0–3 silent buffers per window, only
  around loads.
- AudioFlinger shows the game's track `Active yes`, state `A`, **Underruns 0**, and the mixer's raw underrun counters at
  `partial=0 empty=0`.
- On Home the track goes to `P` (paused, inactive). On resume it is `A` and active again.

**What this does not show:** that the sound is audible and correct (right sounds, no crackle, correct speaker routing).
Nobody listened.

**Manual listening checklist** (about 3 minutes, device volume up, no Bluetooth headset):

1. Start `game` and leave the intro running: music and the Irrational logo sound, no crackle.
2. Esc → main menu: hovering and clicking buttons makes UI clicks.
3. New Game → Start: the cutscene has voice-over in sync with the picture.
4. In game: footsteps while walking, the ambient hum, and the wrench swing "whoosh" (summon one as in §3.1 or use a
   save).
5. Home for 5 s, then back: sound stops while away and comes back. No burst of stale audio.
6. Pass if all five are heard cleanly. Note any crackle and when it happens.

## 6. Performance

Default lift, stock `cam_ext.cfg` (`framerate_cap 100`, vsync on the 120 Hz panel). From the new `SS2PORT_PROFILE` line,
`top`, `dumpsys meminfo` and `simpleperf`.

| Scene | fps | Frame ms (mean / p50 / p95 / p99 / max) | Frames > 50 ms |
| --- | --- | --- | --- |
| Main menu | 100.0 | 10.0 / 9.9 / 12.2 / 13.0 / 14.5 | 0 |
| Intro / start cutscene | 15.0 (the video's own rate) | 66.7 / 65.8 / 71.8 / 73.5 / 73.5 | all (by design) |
| Earth tram station, walking | 98.8–99.1 | 10.1 / 10.1 / 10.8 / 11.1 / 11.8 | 0 |
| MedSci, walking and turning | 99.1–99.6 | 10.1 / 10.0 / 10.8 / 11.0–12.0 / 12.1–13.7 | 0 |
| MedSci after resume from Home | 99.6 | 10.0 / 10.0 / 10.8 / 11.0 / 12.1 | 0 |

- **The frame rate is capped by the game** (`framerate_cap 100`), so the device's headroom is not measured. One core:
  the game thread is at 71 % (`top -H`), about 7 ms of CPU per 10 ms frame. `SwapWindow` takes 1–3 % of the time.
- **Loading:**
  - New game Esc → MedSci world ready: 1.0 s (Esc at +0, `allobjs.osm` at +0.25 s, shaders ready at +0.97 s).
  - Load a MedSci save from the menu: 1.16 s.
  - Load the Earth save: about 1 s.
  - Each load has one long frame of 440–500 ms. No other stall over 50 ms was seen in play.
- **Memory (MedSci):**
  - RSS 281–285 MB (`/proc/self/statm`).
  - `dumpsys meminfo`: total PSS 254 MB and total RSS 386 MB, of which Graphics is 105 MB (EGL 69 MB, GL 27 MB) and
    native heap 17 MB.
  - The 4 GB guest reservation is mostly untouched.
- **CPU hotspots (simpleperf, 10 s walking in MedSci, 12,638 samples):**
  - The game thread has 95 % of the process's samples.
  - Within it: `libss2game.so` 84.9 %, the Adreno GLES driver 10.3 %, libc 2.6 %.
  - Top symbols: **`nd_call` 19.2 %** (indirect-call dispatch and table lookup), **`maybe_yield` 14.9 %** (the
    `PORT_PREEMPT` check), `rt_preempt` 1.1 %, `thunk_run` 0.8 %. No single game function is above 2 %.
  - Full list: `phase1d/simpleperf-medsci-top.txt`.
  - These are runtime overheads, which is useful to know later. **Nothing was optimised.**
- **`--cache-regs` game build: not made.** With the game capped at 100 fps and both builds expected to hold it, the
  comparison would show nothing. A useful comparison needs `framerate_cap` raised and a repeatable scene. It is left
  for the performance phase.

## 7. Android lifecycle (bounded test, final APK, MedSci running)

| Test | Result |
| --- | --- |
| Home | `onPause` → `surfaceDestroyed` → `nativePause`. The game thread blocks (SDL's block-on-pause). The audio track is paused. The process stays (same pid). |
| App icon from the home screen | **Before fix 2:** the game's activity was destroyed and the process was orphaned. **After:** the same pid resumes, `surfaceChanged` at 2340×1080, the picture comes back, the audio track is active, pointer capture is `RELATIVE`, and mouse look works. |
| Recents, then back to the app | Resumes the same game process |
| Back key | Passed to the game. The activity stays (fix 3). |
| Activity destroyed (`am start --activity-clear-top` the launcher) | The game logs "the Android activity is being destroyed", exits 0 after 72.7 s, and the launcher reports PASS |
| `am force-stop` during a run | The next autorun reports it as FAIL and then starts (fix 4) |
| Screen off/on | **Not tested.** The device has a secure lock screen, and unlocking needs the owner's credential. |
| Display surface recovery | Verified for Home/resume (`surfaceDestroyed`/`surfaceChanged`, rendering resumes, no GL errors). The EGL context survived, so no texture reload path was exercised. |

## 8. Write-folder regression test (the Phase 1C bug)

**The new section `writedir`** is in `port/tests/guest/conformance.c` (113 checks). It runs only with `-writedir`, so
the 511-check baseline is unchanged.

`run_conformance.py --write-dir`:

- prepares a game folder and a separate write folder (`WRITEDIR_LAYOUT`), with names in both, names in one only, two
  spellings of the same name, and folders on only one side;
- runs the section with `SS2PORT_WRITE_DIR` set;
- then checks that the game folder was not written. On Android it compares the md5 of every file.

**What it checks**

- `_findfirst` and `FindFirstFileA` list the merged folder, with each name once (case-insensitive) and the write
  folder's size and spelling where both folders have the name.
- The path and the pattern can be in any case.
- `*` lists files and folders of both sides, with the folder flag set.
- A name without wildcards resolves to the write folder's copy.
- A folder that only one side has can be listed.
- Missing names and folders give `-1` with `errno 2`, and `INVALID_HANDLE_VALUE` with
  `ERROR_FILE_NOT_FOUND`/`ERROR_PATH_NOT_FOUND`.
- Reads agree with listings.
- A new file appears in the listing at once and disappears after `remove`.

| Run | Result |
| --- | --- |
| Android `adb`, default lift | PASS, 0/113, game folder unchanged |
| Android `adb`, `--cache-regs --abi-check` | PASS, 0/113, 0 ABI violations, game folder unchanged |
| Native Windows host, default and `--cache-regs` + `PORT_ABI_CHECK` | PASS, 0/113, game folder unchanged |
| **Negative control:** Phase 1C behaviour put back temporarily in `vfs_opendir` (game folder skipped when a write folder exists) | **FAIL, 19 of 102 checks** (fewer checks ran because some are conditional). Restored with `git checkout`; `vfs.c` is unchanged in this commit. |
| The baseline suites in the same session | Android `adb` 0/511 (default) and 0/511 with 0 ABI violations (`--cache-regs --abi-check`). Native Windows 0/511 (both). In-app `all` 0/511 twice. |

The native Windows host was built with the Phase 1A scratch `.cmd` toolchain, as before, because upstream's
`windows-zig.cmake` wrappers cannot run on Windows (PHASE1A_RESULTS.md). The Linux and QEMU runs from Phase 1A were not
repeated.

## 9. Changes

| File | Change |
| --- | --- |
| `port/backends/sdl2/plat_sdl2.c` | Opt-in diagnostics on every platform: `SS2PORT_PROFILE=1` (frame times, stalls, swap, RSS, audio callbacks) and `SS2PORT_INPUT_DEBUG=1`. Android only: the pointer-capture reclaim (fix 1) and `SDL_APP_TERMINATING` → `port_exit(0)` (fix 2). Without those variables, desktop behaviour is unchanged. |
| `port/backends/android/android_main.c` | `SDL_ANDROID_TRAP_BACK_BUTTON=1` (fix 3) |
| `port/tests/guest/conformance.c`, `crt90.h`, `run_conformance.py`, `port/README.md` | The `writedir` section and `--write-dir` (§8) |
| `android/app/…/LauncherActivity.java`, `AndroidManifest.xml`, `build.gradle` | `--es env`, `--es portargs`, `settleDeadRun` (fix 4), `singleTop` plus finishing a duplicate launcher (fix 2). Version 0.3.0-phase1d. |
| `android/tools/uhid_input.c`, `android/tools/README.md` | The test HID keyboard and mouse (§2.2). Not part of the APK. |
| `android/README.md`, `docs/android-audit/README.md`, this report, `phase1d/` | Documentation and evidence: two game run logs without thunk traces, and the simpleperf summary |

The changed C files compile cleanly with `-Wall -Wextra` (NDK clang 21). The non-Android path of `plat_sdl2.c` was
syntax-checked with `__ANDROID__` undefined, but no desktop SDL2 build was made or run in this phase. Game screenshots were taken for checking but
are **not committed**: they show the game's copyrighted art. They are in the session scratch folder only.

## 10. Remaining defects and open points

1. ~~**Earth grav shafts do not lift the player** (§3.3). This blocks a normal new game and therefore character creation.
   Next: run the same save on a desktop build of the port to tell a recompiler/port bug from an Android one.~~
   *Closed in Phase 1E: test error; the lift shaft works on Android and desktop.*
2. **Not exercised:** ranged combat, enemies and AI, level transitions through the game's own elevators, the
   recruitment choices, audible sound, real USB/Bluetooth hardware, screen off/on, and low-memory process death while in
   the background.
3. **Touch taps cannot drive the menus** (§2.2). This is expected without touch controls, but it shapes the design below.
4. The Samsung "Esc as Back" prompt appears for every new keyboard. It comes from the OS, and the app cannot suppress it.
5. **Performance headroom is unknown** because of the 100 fps cap. `nd_call` and `maybe_yield` together take about a
   third of the game thread.
6. From Phase 1C, unchanged: the APK size (two 37.8 MB game libraries), and personal builds only.

## 11. Recommendation for the Shockolate controls (next phase)

**Go ahead.** Gameplay works with keyboard and mouse input, and the input path is now understood.

- **Feed the overlay's actions in below SDL, as `PlatEvent`s.** Send `PLAT_EV_KEY` with DirectInput codes (the
  `user.bnd`/`SHOCK.BND` bindings: `w` `a` `s` `d`, Space jump, Tab interface mode, `r` reload, `1`-`0` weapons, `:`
  command), `PLAT_EV_MOUSE_MOVE` with `dx`/`dy` for look, and `PLAT_EV_MOUSE_BUTTON` (left fires, right frobs). Do not
  rely on SDL's touch-to-mouse emulation, which clicks at stale positions.
- **Turn SDL's emulation off when the overlay is on:** `SDL_HINT_TOUCH_MOUSE_EVENTS=0`.
- **The menus and interface mode need an absolute pointer.** The game keeps the DirectInput mouse exclusive there and
  moves its own cursor by relative motion (`user32.c`, the `captured` virtual cursor). A tap therefore has to become a
  move from the current cursor to the target, plus a click. The cleanest way is a small "absolute pointer while captured"
  event in `user32.c`/`dinput.c`, the only change that touches shared code. The HID tool's home-then-move method shows
  that this mapping is exact.
- **Test with the HID tool and a real keyboard** next to the touch layer, so that every touch action can be compared with
  its keyboard or mouse equivalent.
- ~~**Keep the grav-shaft defect separate.** It is not an input problem, and it should be settled first (on desktop) so
  that touch testing can start from a normal new game.~~ *Settled in Phase 1E: there is no defect; touch testing can
  start from a normal new game.*
