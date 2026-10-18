# Phase 4 results: Android packaging and usability (Milestone D)

2026-10-18 (overnight session). Goal: make the app comfortable for normal personal play (one obvious way to start,
choosing an installation, importing game files, stability across the Android lifecycle, a leaner APK) without turning
the launcher into a project of its own and without losing the diagnostics.

**Status: PARTIAL.** Launching, game-folder selection, lifecycle and stability, failure reporting and the leaner APK
are done and tested on the S26 Ultra. **The in-app folder import is implemented but was not exercised on the device:**
it needs Android's folder picker, which nobody could operate, because a third-party overlay held the phone's
touch input all night (PHASE2B2_RESULTS.md). The `adb push` workflow stays documented and verified.

| Item | Result |
| --- | --- |
| D1 Launching | **Done.** A large **Play** button first: touch controls on, no frame limit, Interface size (default largest). Game folder and Interface size under it, diagnostics below. Clear error when a folder has no game |
| D2 Import | **Implemented, not device-tested.** **Import a game folder...**: Android's folder picker, copy into `games/NAME/` through `NAME.importing/`, never over an existing folder |
| D3 Stability | **Tested** (scripted input; no touch): Home/resume, audio pause/resume, surface recreation, 6.5-minute SCP soak (`phase4/soak-scp-profile.log.txt`), crash and missing-file reporting, save/load. Touch after resume not tested |
| D4 Packaging | **Done.** `dev` (everything, 88.6 MB) and `play` (the game only, **43.7 MB**) builds of the same app |
| APKs | `0.8.0-phase4`: dev `89f28ce444d1685c649155d60f811862cb67624c0ecf605264ec542681f5b3db`, play `3b4ed73db444e550ad13613ed1ddd438d171e5e942e3176f725d2af2c49b9f1c`, in `build/android/baseline/phase4/` with `libss2game.so` (`6c319481…`, unchanged since Phase 3A) |

## 1. Launching (D1)

The launcher (`LauncherActivity`) now reads, top to bottom:

1. **Play**: starts the game in the selected game folder with touch controls, **no frame limit**, and the chosen
   Interface size. Bounded runs stay available to tests (`--ei frames N`).
2. **Game folder: NAME**: cycles through `games/*/` folders that contain `SS2.exe` (not half imports). Each has its
   own write folder `profiles/NAME/` (saves, `cam.cfg`, `user.cfg`, logs), so vanilla and modded games never mix.
   `--es game NAME` from adb.
3. **Interface size** (Phase 2B-1; default *largest*, 640×480).
4. **Import a game folder...** and a line listing the installed game folders (or how to get one).
5. Diagnostics (dev build only): conformance runs, the headless game, the run log of the last run.

Errors are written for the player. Example (seen on the device when a copied folder was not readable yet): "there is
no System Shock 2 in the game folder "ss2scp-nonv" (…/SS2.exe not found). Tap Import a game folder to copy your
NewDark 2.48 installation, pick another Game folder, or copy one with adb push." A native crash is reported with its
signal, a missing executable with its exit code (§3).

Settings persist: game folder, Interface size, touch layout (`touch-settings.cfg`), saves and game settings stay in
the app's private storage across launches, updates and the dev ↔ play switch (same package, same signing key).

## 2. Game-file import (D2)

`GameImport.java`, adapted from Shockolate's importer (GPL-3.0, like the touch code):

- `ACTION_OPEN_DOCUMENT_TREE`: the player picks their System Shock 2 folder; no storage permission is requested.
- The folder must contain `SS2.exe` (any case), or nothing is copied and the message says what to pick.
- Destination `games/NAME/` from the folder's name; a name already in use gets `-2`, `-3`, …: **an existing game
  folder is never replaced**, and saves (`profiles/`) are never touched.
- The whole tree is copied with its relative layout (`DMM/`, `cam_mod.ini`, `OSM/`, `misdml/`, `.crf` archives):
  modded installations come over as they are. Names are checked (no `..`, no separators), depth is limited, two
  entries differing only in case are refused.
- Large files stream in 1 MB blocks, each file is `fsync`ed, and its size is compared with what the provider reported.
- Everything goes to `games/NAME.importing/` and is renamed into place only when complete; on any error the partial
  copy is deleted and the message ends "Nothing was changed."
- Progress (files, MB) is shown while copying; the imported folder becomes the selected game folder.

**Not tested on the device** (the picker could not be operated). It compiles and is in both APKs. The verified way
remains: `adb push "<SS2 folder>/." $D/NAME/` then `adb shell chmod -R a+rX,go-w $D` (`android/README.md`); this
session used it for `ss2scp`, `ss2scp-nonv` and `ss2test`.

## 3. Stability (D3), S26 Ultra, `dev` 0.7.0-phase3a (same native library as 0.8.0)

| Test | Result |
| --- | --- |
| Launch and shutdown | 35 game runs in the session ended with `exit(0)` (PHASE3B §4) |
| Home / resume (`KEYCODE_HOME` mid-game, 15 s, back through the launcher) | The game pauses: one 14.3 s frame and a 13.8 s audio gap in the profile; then **99.8 fps**, audio callbacks back to 213 per 5 s, memory 276–277 MB as before; the surface is recreated and the frame afterwards is correct (MedSci, HUD, touch overlay); movement input works after resume (position 98.67 → 94.42) |
| Sustained play (SCP, the UNN lobby, 390 s of walking and turning, `SS2PORT_PROFILE=1`) | **exit 0**; after loading 98.6–99.5 fps at the 100 fps cap, p99 ≈ 11 ms, **0 frames over 50 ms**, RSS **300–308 MB flat** (no growth), audio steady (215–217 callbacks / 5 s), NVScript ~6,150 calls / 20 s throughout; 150 distinct positions |
| Failure reporting (`--es autorun neg`) | `neg-exe`: FAIL `status="exit 2"`; `neg-crash`: FAIL `status="signal 11 code 1 addr 0x10"`: both reported with the reason |
| Save / load | SCP quicksave → move → quickload restored the position; menu load of a quicksave (PHASE3B) |
| `play` APK | Installed over `dev` (data kept: `profiles/ss2` intact); a vanilla save loads and plays (PASS, exit 0); `--es autorun default` is refused: "default: not in the play build (install the dev build for diagnostics)" |
| Not tested | Touch controls after resume, multitouch, surface changes from rotation (the activity is landscape-locked) |

## 4. Packaging (D4)

| | `dev` | `play` |
| --- | --- | --- |
| Native libraries | `libss2game.so` (42.1 MB), `libss2null.so` (42.1 MB, the headless game), `libss2conf.so`, `libss2conf_cr.so`, `libSDL2.so` | `libss2game.so`, `libSDL2.so` |
| Assets | `conformance/conformance.exe` | none |
| Launcher | Play + diagnostics | Play; diagnostics hidden and refused |
| APK size | 88.6 MB | **43.7 MB** |

- Both are the same package (`local.newdarkandroid.ss2`) and the debug signing key, so one replaces the other and keeps
  the app's data. Gradle: `assembleDevDebug`, `assemblePlayDebug` (or `assembleDebug` for both). `BuildConfig.DIAGNOSTICS`.
- Native libraries are already stripped by the Android Gradle plugin (598 MB on disk, 42 MB packaged) and stored
  uncompressed so they load in place.
- The builds stay **debuggable debug builds** on purpose: the test workflows (`run-as`, saves and logs) need it, and
  there is no release signing key. Nothing was published.
- The APKs contain code generated from the user's own `SS2.exe` and modules (and GPL touch code), and no game assets.
  They are personal builds and must not be distributed (README.md, licensing notes).

## 5. Build and use

```sh
build/venv/Scripts/python.exe android/build_native.py --no-rebuild-guest --game "<SS2>/SS2.exe" --game-backend all \
  --nvscript "<SCP>/NVScript.osm"
cd android && ./gradlew assemblePlayDebug      # app/build/outputs/apk/play/debug/app-play-debug.apk
adb install -r android/app/build/outputs/apk/play/debug/app-play-debug.apk
```

On the phone: **Game folder** → `ss2` (vanilla) or `ss2scp` (SCP) → **Play**.
