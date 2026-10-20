# Phase 1E results: Earth grav-shaft defect isolation

2026-10-17. Goal: find out why the Earth tram station's grav shafts did not lift the player on Android (Phase 1D
§3.3), and whether the defect is Android-specific or in the shared recompiled runtime.

**Out of scope, and not done:** no touch controls, no SCP, no changes to Shockolate, the lifter, `runtime/` or the
generated code. The Steam installation was only read. Nothing was pushed.

## Summary

**There is no grav-shaft defect. The Phase 1D report was a test error.**

- Earth has two shafts side by side. **The one at x = −30 is a 20 % low-gravity descent shaft.** The lift is the one at
  **x = −24**.
- Every Phase 1D attempt was in the descent shaft. The second attempt, meant for the other shaft, re-entered the same
  disc. A high, slow jump that falls back is exactly what a 20 % gravity room should do.
- Walked into, the **x = −24 lift shaft carries the player from the floor (z ≈ 5.5) to the top section (z ≈ 52.8) in
  about 6 s**, and the player can walk out of the upper exit.
- This works on Android in all three tested modes:
  - headless with a script;
  - windowed with a script;
  - windowed with real HID keyboard and mouse input.
- **It behaves the same on a Windows desktop build** of the same recompiled code. The positions match within
  0.17 units over 156 samples, which is sampling jitter.

| Question from the brief | Answer |
| --- | --- |
| Android-specific, or the shared runtime? | **Neither.** Both builds behave the same and correctly. |
| Does the player rise automatically? | **Yes**, in the lift shaft (room 43, `RoomGrav` −10). No, in the descent shaft (room 44, `RoomGrav` 20), which is correct. |
| Standing at the centre | Lift: rises with no input, accelerating, and stops at the top section (room 45, normal gravity). Descent shaft: stays on the floor. |
| Jumping inside | Descent shaft: rises about 12 units (z 5.5 → 17.6) over about 2 s and floats back down, against about 2 units for a jump in normal gravity. |
| Does upward movement continue? | Yes, until the top section: z 52.8–53.6 on both platforms |
| Can the player reach the upper exit? | Yes. Walking out of the top reaches y ≈ 41, z ≈ 53.0 on the upper floor, where a jump rises about 2.4 units and lands again. |
| Root cause | No defect. Wrong shaft tested in Phase 1D, identified from the mission's room database (§3). |
| Fix | **None needed, none made.** |
| Should it block touch integration? | **No.** |

## 1. Baselines

| Item | Where | sha256 |
| --- | --- | --- |
| Phase 1D APK, unchanged | `build/android/baseline/phase1d/app-debug-phase1d.apk` | `419de6c0…fe6a94c` |
| Device saves at the start of Phase 1E | `build/android/baseline/phase1d/saves/` (save_0 "shaft": Earth, save_1 "medsci"), plus `SS2.log` and `user.bnd` | `earth.MIS` `262f459e…cd15f776`, `medsci1.MIS` `b192e744…a5211529` |
| Phase 1E APK (adds only the opt-in `SS2PORT_INPUT_SCRIPT`) | `build/android/baseline/phase1e/app-debug-phase1e.apk` (version name unchanged, 0.3.0-phase1d) | `504aeeb7adaf70cac4eb6db6993667408b29ce0c6f95573349d941b6b5d35dd0` |
| Windows desktop hosts | `build/android/baseline/phase1e/ss2port-win-sdl2.exe`, `ss2port-win-null.exe` | `13eb014d…a4b38f6`, `0135232e…5f599cb` |

- At the end, the device saves hash as at the start, and the profile's `user.bnd` is unchanged. The test scripts and the
  HID test device were removed.
- No file in the Steam folder is newer than the start of this phase.
- The generated code that was probed temporarily (§3.2) was restored to its original hash, `nd_f051.c`
  `74d95a3c…`, and every binary was rebuilt from it. No probe string is left in any library.

## 2. Desktop comparison

### 2.1 Build

**The Windows desktop host is built from the same lifted C as Android** (`build/android/game/gen`: the default lift of
the same Steam `SS2.exe`, `allobjs.osm`, `Squirrel.osm`, `lgvid.dll`). Any difference could therefore only come from
the host or the platform. Same CMake options as Android: `PORT_PREEMPT=ON`, `PORT_VIDEO=ON`, `PORT_SHADERS=ON`,
`PORT_PPL=OFF`, `RelWithDebInfo`.

- **Toolchain:** Zig `x86_64-windows-gnu` through the `.cmd` wrapper toolchain from Phase 1A. Upstream's
  `windows-zig.cmake` uses `/bin/sh` wrappers, which do not run on Windows (PHASE1A_RESULTS.md). The four wrappers are
  one line each, for example `@"…\build\venv\Scripts\python.exe" -m ziglang cc -target x86_64-windows-gnu %*`. The
  toolchain file sets `CMAKE_C_COMPILER`, `CMAKE_AR`, `CMAKE_RANLIB` and `CMAKE_RC_COMPILER` to them.
- **SDL2 for Windows:** a read-only copy of the x86-64 SDL 2.32.10 install that already existed in the Shockolate project
  (`system-shock-android/dist/audio1/host`, the same SDL version as Android). It was copied to
  `build/deps/sdl2-win-host/` (git-ignored). Shockolate was not modified, and nothing was downloaded.
- **Builds:** `build/desktop-win` (SDL2 backend) and `build/desktop-win-null` (headless). Each takes about 1 minute,
  with no warnings.

```sh
cmake -S port -B build/desktop-win-null -G Ninja -DPORT_GENERATED=<repo>/build/android/game/gen \
      -DPORT_BACKEND=null -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPORT_PREEMPT=ON -DPORT_VIDEO=ON -DPORT_SHADERS=ON \
      -DPORT_PPL=OFF -DCMAKE_TOOLCHAIN_FILE=<scratch>/windows-zig-native.cmake
# SDL2 backend: -DPORT_BACKEND=sdl2 -DSDL2_DIR=<repo>/build/deps/sdl2-win-host/lib/cmake/SDL2
cmake --build build/desktop-win-null
```

- The game runs from a copy of the install in `build/desktop-game/ss2/` (518 MB, without Bonus Content). Windows does
  not enforce the read-only rule, so the copy guarantees that a stray write cannot touch the Steam folder.
- Saves go to `build/desktop-game/write/` through `SS2PORT_WRITE_DIR`. It holds the Phase 1D Earth save as `save_0`,
  byte-identical to the device's.

### 2.2 How both platforms were driven identically

Driving a desktop window with injected Windows input would have taken over the owner's mouse and keyboard. Instead, a
small test-only feature, **`SS2PORT_INPUT_SCRIPT=<file>`** (`port/win32/user32.c`), reads timed input from a file:
keys, characters, relative mouse motion and buttons, in milliseconds after the first frame. It feeds them into the same
event path the platform backends use, before they reach the window messages and DirectInput. It works on every
backend, including `null`, so the **same script runs headless on Windows and Android**.

- **Position readout:** NewDark's own console command `log_player_pos` writes `PLAYER POS: x y z` to `SS2.log`. The
  script types `:bind k log_player_pos` once, at human speed, then presses `k` every 250 ms (100 ms while moving or
  jumping).
- **One finding along the way:** a key binding with Shift (here `:`) does not fire if Shift, the key and their releases
  all arrive in one input poll. The engine reads the modifier state between frames. Real typing is slower, so the script
  spaces keystrokes 80 ms apart. This is engine behaviour, not a port defect.
- **Scripts:** `phase1e/make_route_script.py` writes them. It reproduces both committed scripts line for line:
  - `phase1e/script-lift-shaft.txt`: load, step out to the platform, strafe to x = −24, back into the lift shaft, wait,
    walk out of the top exit;
  - `phase1e/script-descent-shaft.txt`: load and jump in the x = −30 shaft.

**Reproduction**

```sh
# Windows (null backend, no window):
cd build/desktop-game/run-null
SS2PORT_WRITE_DIR=<repo>\build\desktop-game\write SS2PORT_INPUT_SCRIPT=<repo>\docs\android-audit\phase1e\script-lift-shaft.txt \
  ..\..\desktop-win-null\ss2port.exe --verbose <repo>\build\desktop-game\ss2\SS2.exe
grep "PLAYER POS" ../write/SS2.log

# Android (save_0 must be the Phase 1D "shaft" save; it is preserved in build/android/baseline/phase1d/saves/):
adb push docs/android-audit/phase1e/script-lift-shaft.txt /data/local/tmp/lift.txt
adb shell "chmod 644 /data/local/tmp/lift.txt; run-as local.newdarkandroid.ss2 cp /data/local/tmp/lift.txt files/lift.txt"
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es autorun game-null --ei frames 0 \
  --es env SS2PORT_INPUT_SCRIPT=/data/user/0/local.newdarkandroid.ss2/files/lift.txt
adb exec-out run-as local.newdarkandroid.ss2 cat files/profiles/ss2/SS2.log | grep "PLAYER POS"
```

### 2.3 Results (clean builds, no probes)

| Run | Windows desktop (null) | Android S26 Ultra (`game-null`) |
| --- | --- | --- |
| Lift shaft (`script-lift-shaft.txt`) | 156 samples, z from 5.50 to **max 53.53**, ends at (−24.18, 38.94, 53.53) | 156 samples, z from 5.50 to **max 53.55**, ends at (−24.24, 38.90, 53.55) |
| Descent-shaft jump (`script-descent-shaft.txt`) | z 5.50 → **apex 17.59** → 5.51 | z 5.50 → **apex 17.54** → 5.51 |
| Difference per sample | | lift: max \|dz\| **0.17**, mean 0.016. Descent: max 0.07, mean 0.022 |

The full side-by-side tables are `phase1e/positions-lift-shaft.txt` and `phase1e/positions-descent-shaft.txt`. The
script steps are timed by wall clock, so the samples differ slightly in phase. That accounts for the small
differences.

**Rise in the lift** (Windows/Android, from `positions-lift-shaft.txt`; samples 65–91, 100 ms apart while keys are
held, then 250 ms):

6.00/6.00 → 7.03/7.05 → 8.33/8.31 → 10.62/10.64 → 13.72/13.83 → 17.69/17.73 → 22.40/22.41 → 27.94/27.89 →
34.26/34.35 → 41.40/41.48 → 49.25/49.42 → 52.65/52.70 → 52.83/52.85

The rise speeds up while the room's −10 % gravity applies, then stops in the normal-gravity top section.

**Further Android checks**

- **Windowed run** (SDL + GLES, same script): the same transitions and positions. Screenshots show the platform, the
  backward walk into the shaft, the rise between the shaft walls, and the view back down the shaft from the top ledge.
- **Real input path:** the HID keyboard and mouse tool (`android/tools/uhid_input.c`), through Android, SDL and the
  game, the way a player would. Loading the save by mouse, then `w`, `a` and `s` held as in the script, put the player at
  **(−24.80, 36.79, 52.76)**, at the top of the lift.

## 3. Investigation

### 3.1 Room data (from the original `EARTH.MIS`, read-only)

`P$RoomGrav` is set on three room archetypes: 227 → 20, 228 → 0, 270 → −10.

The `ROOM_DB` records (object id, room id, centre, six bounding planes) place them like this:

| Room | Archetype (`RoomGrav`) | x | y | z | Role |
| --- | --- | --- | --- | --- | --- |
| 44 | 227 (20 %) | −32.25 … −27.75 | 33.75 … 38.25 | 2.25 … 58.75 | **Descent shaft** (the "left" one from the platform) |
| 43 | 270 (−10 %) | −26.25 … −21.75 | 33.75 … 38.25 | 2.25 … 51.75 | **Lift shaft** |
| 45 | 228 (0 = unchanged) | −26.25 … −21.75 | 33.75 … 38.25 | 51.25 … 58.75 | Top of the lift, normal gravity |
| 38 / 33 | default | x of each shaft ± 2.75 | 31.75 … 34.25 | 1.75 … 11.25 | Floor-level doorways |
| 46 / 47 | default | x of each shaft ± 2.75 | 37.75 … 40.25 | 49.75 … 59.25 | Upper exits at the back |

The Phase 1D save was made at (−30.8, 36.8, 5.5): inside room 44, the descent shaft.

### 3.2 Engine code path (temporary probes, then removed)

- The game registers `RoomGrav` as a property object at global `0xa3ad60` (`0x588717`).
- Its one reader is a room-transition callback, `0x56bac0`. `0x56b880` registers it with the room system (the object
  at global `0xa3ad48`, callback array at `+0x80`).
- On every room change of an object with physics, it does `gravity /= factor(old room); gravity *= factor(new room)`,
  where `factor = RoomGrav / 100.0` and a missing or zero value counts as 1.0. The gravity is the float at `+0x14` of the
  physics object.

Probes were added to the generated C (build folder only; backup and restore by hash) at registration, callback entry and
the two gravity stores. The player is object 9. With the probes, both platforms logged the **same sequence**:

```
load:        room 227 (gravity 0.2)
walk out:    227 -> 26            gravity 1
platform:    26 -> 330 -> 26      gravity 1
lift shaft:  26 -> 270            gravity -0.1      (the lift)
top:         270 -> 228           gravity 1
upper exit:  228 -> 26            gravity 0 (see below)
```

**The transient 0 at the upper exit**

- It was the same on both platforms.
- Further probes showed that the gravity was *already* 0 before that callback ran. Some other engine code zeroes it
  while the player crosses the top ledge, most likely the ledge climb/mantle. The callback only carried the value
  through.
- The player's next jump on the upper floor rises 2.4 units and lands normally, so gravity is back to normal.
- Because both platforms do the same, it is not a port divergence. Whether the original game also passes through 0
  there was not checked: the original executable was not run.

### 3.3 The brief's checklist

| Area | Finding |
| --- | --- |
| x87 translation, float→int conversion, rounding and precision | No divergence. Both platforms run the same lifted C and the gravity factors are bit-identical in the probes (0.2, −0.1, 1). The per-sample positions agree to 0.17 or better, consistent with timing jitter. |
| Physics timing | Both run at the game's 100 fps cap. Phase 1D's `phys_freq`/`framerate_cap` change was irrelevant: the wrong shaft was being tested. |
| Guest memory and property loading | Property values and room assignments are identical: the same callback arguments and room objects, and the same physics-object behaviour. |
| Thread scheduling | Not a factor. The transitions happen on the game thread in the same order on both platforms. |
| Compiler settings | Android: clang 21 aarch64 with `-ffp-contract=off` on the game code. Windows: Zig clang x86-64. No difference in the results. |

## 4. Code changes and tests

| File | Change |
| --- | --- |
| `port/win32/user32.c` | `SS2PORT_INPUT_SCRIPT`: timed test input fed into the platform event path, on any backend. It does nothing unless the variable is set. |
| `port/README.md`, `android/README.md` | Document `SS2PORT_INPUT_SCRIPT` (and, in `port/README.md`, Phase 1D's `SS2PORT_PROFILE` and `SS2PORT_INPUT_DEBUG`) |
| `docs/android-audit/PHASE1D_RESULTS.md` | A correction notice, with the wrong grav-shaft statements struck through and pointing here. The original text is kept. |
| `docs/android-audit/phase1e/` | The script generator, both scripts, the position tables, and an excerpt of the Android lift run log |
| This report and `docs/android-audit/README.md` | |

**No gameplay fix:** no defect was found, so nothing in the game path changed. The Android APK differs from Phase 1D
only by the opt-in input script.

**Regression after the `user32.c` change**

| Suite | Result |
| --- | --- |
| Android `adb` | 0/511 (default), 0/511 with 0 ABI violations (`--cache-regs --abi-check`), `--write-dir` 0/113 |
| Native Windows host | 0/511 and `--write-dir` 0/113, in both lift modes |
| In-app `all` | 0/511 twice |
| Scripted lift runs | Repeated with clean builds: §2.3 |

**Regression coverage for the shafts:** the two scripts and the position tables are a reproducible check. Expect the
lift run to end above z = 52 and the descent jump to peak at about 17.5. They are not automated in CI: they need the
owner's game files and the Phase 1D save, neither of which can go in the repository.

## 5. Remaining uncertainty

- **The original `SS2.exe` was not run** on Windows for a three-way comparison. The two ports agree with each other and
  with the level's room data (the lift room has −10 % gravity and the player rises through it to the top), so it is
  unlikely they share a wrong behaviour. This was not proven against the original binary.
- The transient gravity 0 at the top ledge (§3.2) was not traced to its setter. It has no visible effect in play.
- Character creation (the three recruitment choices further into Earth) is still not exercised. The way there is now
  known to be open.
- `SS2PORT_INPUT_SCRIPT` events bypass SDL and the Android input stack. That path was therefore covered separately by
  the HID run (§2.3).

## 6. Recommendation

**The grav-shaft issue should not block touchscreen integration.** It is not a defect. The Earth level is traversable
on Android: floor to the top of the lift shaft and out the exit, with the same movement and gravity numbers as a desktop
build.

Two things from this phase are useful for the touch work:
- `SS2PORT_INPUT_SCRIPT` plus `log_player_pos` gives a deterministic, headless way to compare a movement sequence before
  and after the touch layer is added.
- A touch layer that sends a modified key binding (Shift + key) must hold the modifier across at least one frame, as a
  person typing does (§2.2).
