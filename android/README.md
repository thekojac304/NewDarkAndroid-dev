# Android app (`android/`)

This is a minimal Android shell around the portable host. It runs the game-free conformance program (Phase 1B) and,
when built with `--game`, the recompiled game from your own NewDark 2.48 install (Phase 1C). An APK built with `--game`
contains code generated from your game files: it is a personal build, never to be distributed.

| Part | What it is |
| --- | --- |
| `LauncherActivity` (main process) | The launcher (Phase 3G: a game launcher, drawn by `LauncherUi`: dark blue-black HUD, teal accents, chamfered panels, no images). Main screen: the title and icon; the **Installation** card (the selected game folder `games/NAME` or `imported/NAME` by its display name, its folder, how many saved games its own write folder holds, READY or what is wrong); **Change** (choose among the folders that have an `SS2.exe`) and **Rename** (a display name, kept in the `game_labels` preferences and settable with `--es label_game NAME --es label TEXT`; holding the card does the same; the name is only shown, it never selects modules or saves); one **PLAY** button; then **Controls guide** (a short reference), **Interface size**, **Import a game folder...** (`GameImport`) and **Advanced** (closed at first: version, device and folders, the dev build's diagnostics, the last runs' verdicts and the last run's log). It prepares a run directory, starts `NewDarkActivity`, and records the verdict, preflight checks and log once the run's process has ended. It refuses to start an `SS2.exe` other than the one the game was recompiled from, and says why. |
| `GameFolders` | Where game folders live (`games/` from adb, `imported/` from the importer), names, the `SS2.exe` check (`BuildConfig.GAME_EXE`), and recovery of interrupted imports |
| `GameImport` | Copies a folder picked with Android's folder picker (no storage permission) into a new `imported/NAME/`, keeping its layout (modded installs too). Scans first and asks: the `SS2.exe` and recompiled-module check, free space, and whether to bring the installation's saves into the new game's own write folder. Staging folders and a commit journal make an interrupted import invisible and recoverable. Never replaces a game folder, save or setting (docs/android-audit/GAME_IMPORT_FIX.md) |
| `NewDarkActivity` (`:game` process) | An `SDLActivity`. SDL loads `libSDL2.so` and the library named by the `lib` extra, then calls its `SDL_main` (`port/backends/android/`) with the `args` extra. A log overlay shows progress. |
| `build_native.py` | Builds SDL 2 and the native libraries with the NDK, and stages them with their assets in `build/android/`. With `--game SS2.exe` it also lifts the game (default lift) and builds `libss2null.so` (headless) and `libss2game.so` (SDL2 + OpenGL ES). |
| `tools/` | Test tools that run outside the app: `uhid_input.c`, a USB keyboard and mouse made from adb ([tools/README.md](tools/README.md)). |
| Gradle | Packages only. Java plus SDL's Java sources, taken straight from the SDL tree. There is no `externalNativeBuild`: the recompiled game is lifted and built on the PC first. |

Every native run is a new `:game` process, because `port_exit` ends the process. The launcher survives exits and
crashes and reports each one.

## Build

Requirements:

- Android SDK with NDK `29.0.14206865`, CMake `3.22.1` and build-tools 34. `ANDROID_HOME` must be set.
- JDK 17.
- The venv from `port/tests/run_conformance.py`: Python with `pefile`, `capstone` and `ziglang`.
- SDL 2.32.10 source in `build/deps/SDL2`. Alternatively set `SDL2_SOURCE`, or pass `--sdl2` / `-Psdl2.dir`.

```sh
git clone -b release-2.32.10 https://github.com/libsdl-org/SDL build/deps/SDL2
python android/build_native.py          # about 1.5 min: SDL2, conformance.exe, two lifts, two libraries
python android/build_native.py --no-rebuild-guest --game "/path/to/SS2.exe"   # + the game: ~8 min the first time (lifts), then seconds
python android/build_native.py --no-rebuild-guest --game "/path/to/SS2.exe" --nvscript "/path/to/DMM/SCP_beta8/NVScript.osm"   # + NVScript, for SCP
python android/build_native.py --game-only --game "/path/to/SS2.exe" --nvscript "<SCP Beta 8 NVScript.osm>" --nvscript "<NVScript 1.2.4 (SCP Beta 4)>"   # both, in one APK
cd android && ./gradlew assembleDevDebug    # app/build/outputs/apk/dev/debug/app-dev-debug.apk: everything (diagnostics, tests)
cd android && ./gradlew assemblePlayDebug   # app/build/outputs/apk/play/debug/app-play-debug.apk: the game only, about half the size
```

**Several NVScript versions in one app** (Phase 3E, docs/android-audit/PHASE3E_UNIFIED_APK_RESULTS.md). `--nvscript` is
repeatable. Each file must be one of the versions listed by sha256 in `port/nvscript_versions.json`, which fixes its
generated-code prefix and guest address (Beta 8: `nv` at `0x30400000`; 1.2.4: `nv124` at `0x30500000`), so the versions
coexist. At run time the host loads whichever version the installation's file is, by its identity (size, ImageBase,
entry point, code CRC), and refuses a file that is none of them. One game folder per installation then works in the
same app: `games/ss2scp` (SCP Beta 8) and `games/scp4` (SCP Beta 4), each with its own `profiles/NAME`.

**Side-by-side build** (no longer needed for SCP Beta 4; kept for experiments: for example a legacy SCP Beta 4 setup with NVScript 1.2.4, docs/android-audit/LEGACY_SCP4_RUNTIME_RESULTS.md).
`-Pside=NAME` gives the app the ID `local.newdarkandroid.ss2.NAME`, the launcher name `SS2 <label>` and the version suffix
`-NAME`, so it installs next to the regular app with its own games, profiles and settings. Build its native libraries into
their own folder and point Gradle at it (an absolute path):

```sh
python android/build_native.py --build build/android-scp4 --game "<2.48 SS2.exe>" --nvscript "<legacy NVScript.osm>"
cd android && ./gradlew -Pside=legacytest "-Pside.label=Legacy test" -Pnative.dir=C:/path/to/build/android-scp4 assemblePlayDebug
```

Without `-Pside` and `-Pnative.dir` the app is built exactly as before.

## Run

```sh
adb install -r android/app/build/outputs/apk/dev/debug/app-dev-debug.apk   # or the play APK; same package, data kept
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es autorun all   # or default, cr; neg = negative tests
adb logcat -s NewDarkLauncher ss2port                                                 # RESULT mode=... verdict=PASS ...
adb shell run-as local.newdarkandroid.ss2 cat files/results/history.txt
```

You can also tap the buttons on the screen. Each run's files are in `files/runs/<mode>/`: `run.log`, `status` and the
working files.

### The game (personal builds with `--game`)

```sh
D=/storage/emulated/0/Android/data/local.newdarkandroid.ss2/files/games
adb push "<SS2 folder>/." $D/ss2/                     # or each top-level entry; Bonus Content is not needed
adb shell chmod -R a+rX,go-w $D                        # adb push creates folders the app cannot enter (2770, owner shell)
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es autorun game-null   # headless, 600 frames
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es autorun game        # window, no frame limit (play)
#   --ei frames N sets a frame limit for a bounded test run (0: none; game-null defaults to 600, game to none)
#   --es env SS2PORT_PROFILE=1,SS2PORT_INPUT_DEBUG=1   extra settings for the game process (comma or space separated)
#   --es portargs "--trace-only GetKeyState"           extra port arguments, before the executable
#   --es uisize 640x480|720x540|800x600|game          Interface size (kept for later starts, like the launcher button)
#   --es game NAME                                    the game folder games/NAME (kept), with its own profile profiles/NAME
#   --es env SS2PORT_INPUT_SCRIPT=FILE,SS2PORT_SHOT_DIR=DIR   scripted input and frame captures (tests; work under overlays)
#   --es env SS2PORT_MODSTATS=1                       log how often the engine calls into each recompiled module
```

**Interface size** (launcher button, Phase 2B-1): the size of the game's HUD, inventory, PDA and in-game text. It sets
NewDark's own `d3d_disp_scaled_2d_overlay` in the profile's `user.cfg`, in one marked line the launcher rewrites at
every game start (the rest of `user.cfg` is left alone). `640x480` (the default) is the largest NewDark accepts and the
size the SS2 interface was laid out for; `game` leaves the setting to `cam_ext.cfg` (whole-number steps only) or your
own `user.cfg` line. The 3D picture keeps the game resolution. See docs/android-audit/PHASE2B_UI_SCALING.md.

The game folder stays read only (the app's uid has no write permission on it). Saves, configuration and `SS2.log` go
to `files/profiles/ss2/` through `SS2PORT_WRITE_DIR`. A file placed there (for example a `user.cfg`) is read in
preference to the game folder's copy. The game window is landscape and fullscreen, with no overlay. There are no touch
controls yet.

**Touch controls.** The `game` mode has the Shockolate touch controls ([port/backends/android/touch/README.md](../port/backends/android/touch/README.md)):
- a floating stick in the left 45 % of the screen and swipe-to-look on the right;
- FIRE (drag to aim), USE, JUMP, CROUCH, RELOAD, AMMO, weapon `<` `>` and II (menu, skip);
- SET, the settings and layout editor, saved to `files/touch-settings.cfg`;
- a LOOK / CURSOR toggle that also switches the game between shoot and interface mode, and follows the game when it
  switches by itself.

In cursor mode a tap puts the game's own cursor there and clicks; a drag drags. `--ez touch false` starts a game without
them; touches then reach SDL as before. Touches never become SDL mouse events while the controls are on.

**Input.** A physical keyboard and mouse (USB or Bluetooth) work through SDL, alongside the touch controls. Neither can
release a key or button the other holds. Mouse look uses Android pointer capture. For scripted tests without hardware,
`tools/uhid_input.c` makes Android see a real USB keyboard and mouse, or (`-t`) a multitouch touchscreen
([tools/README.md](tools/README.md)).

**Diagnostics** (off by default, set with `--es env`):

| Setting | Logs |
| --- | --- |
| `SS2PORT_PROFILE=1` | Every 5 s: fps, frame-time mean/median/p95/p99/max, frames over 50 and 100 ms, time in SwapWindow, resident memory, audio callbacks (count, longest gap, peak level, silent buffers) |
| `SS2PORT_INPUT_DEBUG=1` | Every key, text, mouse-button, finger, window and app event SDL delivers |
| `SS2PORT_MOUSE_DEBUG=1` | Mouse capture changes and every motion event |
| `SS2PORT_TOUCH_DEBUG=1` | Touch controls: every finger, and the keys, buttons and cursor placements they send |
| `SS2PORT_INPUT_SCRIPT=<file>` | Not a log: plays scripted input at set times, in `game` or headless `game-null`. Put the file where the app can read it, for example `run-as local.newdarkandroid.ss2 cp /data/local/tmp/x.txt files/x.txt`, then `--es env SS2PORT_INPUT_SCRIPT=/data/user/0/local.newdarkandroid.ss2/files/x.txt` (see `port/README.md` and docs/android-audit/PHASE1E_RESULTS.md) |

**Sound driver.** The port plays sound through DirectSound only. NewDark's `sfx_device` (the Audio menu's "Hardware
Acceleration": 1 Off, 2 On, 4 OpenAL) set to 4 makes the game load `openal32.dll`, which does not exist here, and then
play nothing at all. A PC installation that used OpenAL brings that setting along, so at every game start the launcher
changes an `sfx_device 4` line in the profile's `cam.cfg` (or in a profile copy of the game folder's) to
`sfx_device 1` and logs `sound: ...`. The first time a profile's own `cam.cfg` changes, its original is kept as
`cam.cfg.openal`. Nothing else in the file changes (`SoundDevice.java`; test: `tools/SoundDeviceTest.java`;
docs/android-audit/LEGACY_AUDIO_RESULTS.md).

**Lifecycle.** Home, then the app icon or Recents, resumes the running game where it was: same process, sound resumes
and mouse capture comes back. The launcher does not open a second copy on top. Back is passed to the game and does not
close it. If the game's activity is destroyed (for example cleared from its task), the game ends the process with
`exit 0`, after flushing its writes. It does not keep running without a window. A run killed with `am force-stop` is
reported as FAIL the next time the launcher starts a run.

## Storage layout

| Folder | Holds |
| --- | --- |
| `/storage/emulated/0/Android/data/local.newdarkandroid.ss2/files/games/<name>/` | Read-only game folders, copied with `adb push`. adb creates `games/` as the shell account (uid 2000), so the app can read it but not create anything in it |
| `/storage/emulated/0/Android/data/local.newdarkandroid.ss2/files/imported/<name>/` | Game folders made by **Import a game folder...**: created by the app, so always writable by it (adb can still read and write them). `<name>.importing/` and `<name>.commit` are imports in progress, never game folders |
| `files/profiles/<name>/` (internal storage) | The write folder (`SS2PORT_WRITE_DIR`): saves, configuration changes, logs. An import's saves (`save_N/`) go here |
| `files/` | App settings, `touch-settings.cfg` |

A name is used once across `games/`, `imported/` and `profiles/`; if both `games/NAME` and `imported/NAME` hold an
`SS2.exe`, `games/` is used. Uninstalling the app removes all of them.
