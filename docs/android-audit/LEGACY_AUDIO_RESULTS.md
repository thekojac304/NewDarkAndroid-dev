# Phase 3D-A: silent legacy app (SCP Beta 4) — root cause and fix

2026-10-18. Branch `android-port`, started at `1c4b8bb` (clean tree). Galaxy S26 Ultra (SM-S948U1, Android 17 / API 37).
Problem reported by the player: the legacy app `local.newdarkandroid.ss2.legacytest` plays no sound at all (menu
music, footsteps, weapons, UI, voices, ambience), while everything else works.

Labels: **Verified on device** (measured here), **Inference**, **Needs your ears** (only a person listening can confirm).

## Summary

| | |
| --- | --- |
| Root cause | **Verified.** The profile's `cam.cfg`, copied from the PC, has `sfx_device 4`: the Audio menu's "Hardware Acceleration: **OpenAL**". The PC install has `OpenAL32.dll` and `wrap_oal.dll` next to `ss2.exe`. The port has no OpenAL. NewDark logs `OpenAL not found on system or failed to load`, the host logs `LoadLibraryA(openal32.dll): not available on this host`, and then **the game opens no sound device at all**: no `DirectSound` object, 0 audio callbacks, no Android audio player |
| Not the cause | The music mods (their files are found and opened), the DirectSound mixer, SDL, Android routing, volume settings, audio focus. All of them work once a device is opened |
| Fix | The launcher (shared code, both apps) changes a `sfx_device 4` line in the `cam.cfg` the game reads to `sfx_device 1` (Hardware Acceleration Off, the game's own default and what the Beta 8 profiles use) at every game start. One line changes; the original is kept as `cam.cfg.openal`. No change to the engine, the host, the mixer, the mods or the saves |
| Result | **Verified on device:** DirectSound opens, 215–216 callbacks per 5 s (44,100 / 1,024 = 43.07 per s), peak level 12,000–19,700 in every window of menu and gameplay, 0 silent callbacks in gameplay, Android shows the game's OpenSL ES player `started` on the speaker with 0 underruns. Home pauses it and the icon resumes it |
| Audible? | **Needs your ears.** Nobody listened. The phone's media volume was **2 of 15** during these tests (not muted); turn it up before judging (§8) |

## 1. Starting state

| Item | State |
| --- | --- |
| Git | `android-port` at `1c4b8bb` ("Phase 3C: legacy SCP Beta 4 runtime"), clean working tree |
| Legacy app | `0.8.1-import-fix-legacytest-play`, `base.apk` `192824dc…` = `build/android/baseline/legacytest/` (checksums OK) |
| Regular app | `0.8.1-import-fix-play`, `base.apk` `2a5ad353…` (the accepted SCP Beta 8 build) |
| Storage | 20 GB free on `/data` |
| Processes | Only the regular app's idle launcher and `io.japp.blackscreen`; no game running |
| Legacy profile `files/profiles/scp4` | 181 files hashed (`profile-scp4-before.txt` under `build/android/legacy-audio/`), 161 of them in `save_0`…`save_14`. No `touch-settings.cfg` in the legacy app (defaults; the layout was never edited there) |
| Regular app data | `profiles/{ss2,ss2scp,legacy247-test}`, `touch-settings.cfg`, `shared_prefs`: 220 files hashed |

## 2. Baseline (before any change)

Every test run used a **disposable write folder** `files/audiotest/<run>` holding copies of the profile's `cam.cfg`,
`user.cfg`, `user.bnd` and two saves, selected with `SS2PORT_WRITE_DIR` (the real profile was not used and not
written). Driver: `legacy-audio/audio_run.py` (input script, `SS2PORT_PROFILE=1`, `dumpsys audio` and AudioFlinger
samples while the game runs). Each run: main menu, Load Game, slot 0 (Hydroponics), walk 3 s, quit. All evidence:
`legacy-audio/runs-summary.txt`.

Run `base-orig` (profile configuration unchanged):

| Signal path step | Observed |
| --- | --- |
| Game configuration | `sfx_device 4`, `sfx_eax 1`, `sfx_channels 48`, `sfx_vol_music -497`, `sfx_vol_2d 0`, `sfx_vol_3d 0`, `master_volume 0` |
| Sound driver start | SS2.log: `OpenAL Init...` / `OpenAL not found on system or failed to load, install AL redist package or chose other sound driver`. Port: `LoadLibraryA(openal32.dll): not available on this host` |
| DirectSound | **Never created**: no `DirectSound: mixing to …` line (it is printed when the first sound buffer is created) |
| SDL audio | **Never opened**: `audio 0 callbacks, peak 0` in all 7 profile windows |
| Android | **No audio player** for the game's process in `dumpsys audio` |
| Everything else | Normal: three script modules, the save loads, the player walks, `exit 0` |

So the earliest failure is step 1 of the chain: the game never asks for a sound device it can get. The mixer, SDL and
Android are never reached. The user's own last session before this phase (`SS2.log` of 16:39) shows the same two
OpenAL lines.

## 3. One variable at a time

Only the `sfx_device` line of the copied `cam.cfg` changed (diff of the pushed file: line 21 only).

| `sfx_device` | Audio menu label | DirectSound | Callbacks / 5 s | Peak | Android player |
| --- | --- | --- | --- | --- | --- |
| 4 (original) | OpenAL | no (OpenAL load fails) | 0 | 0 | none |
| 0 | — | no | 0 | 0 | none |
| 3 | — | no | 0 | 0 | — |
| **1** | **Off** (screenshot `legacy-audio/options-audio-hw-off.jpg`) | **yes** | **215–234** | **12,172–18,312** | **started** |
| 2 | On | yes, with 3D buffers (asks them for EAX via `IKsPropertySet`, gets "not available": EAX off) | 215–216 | 12,742–18,312 | — |

The Steam 2.48 `CAM.CFG` has no `sfx_device` line; the game writes `1` itself, which is where the Beta 8 profiles'
`sfx_device 1` comes from. **1 was chosen**: the game's default, Hardware Acceleration Off (the engine mixes 3D sound
itself), and the path every earlier phase measured. 2 also works but sends the 3D work to the port's simpler
DirectSound 3D model, which no phase has exercised in play. On the PC, OpenAL did the 3D work; neither choice
reproduces OpenAL's EFX reverb (`sfx_eax 1`), which the port never had.

## 4. Comparison with the regular SCP Beta 8 app

One bounded run of the regular app (unchanged APK `2a5ad353…`) with its **currently selected** game folder `ss2`, so the
player's selection did not change, against a disposable copy of its profile:

| | Regular app (Beta 8 setup) | Legacy app before | Legacy app after |
| --- | --- | --- | --- |
| Package / library | `local.newdarkandroid.ss2`, `libss2game.so` of the accepted build | `…ss2.legacytest`, `libss2game.so` `ea4b8b20…` | same library (unchanged, §6) |
| `sfx_device` | 1 | **4** | 1 |
| DirectSound / SDL | 44,100 Hz stereo, S16, 1,024 frames | not opened | 44,100 Hz stereo |
| Callbacks / 5 s, peak | 218, 11,366–31,076 (intro movie) | 0, 0 | 215–216, 12,000–19,700 |
| Android | OpenSL ES player, usage MEDIA, speaker | none | OpenSL ES player `started`, usage MEDIA, speaker, track active, 0 underruns, track volume 1.0 |
| Audio focus | not requested | — | not requested (same code; focus is not needed to play) |
| Game volumes | music −1,000, 2D/3D 0, master 0 | music −497, 2D/3D 0, master 0 | unchanged |

Both apps use the same SDL backend and DirectSound layer; nothing in the host differs by package. The only difference
that mattered was the configuration line. Whether the Beta 8 app is *audible* is also **not verified by a person** (PCM
and an active Android track are measured, as in Phase 1D).

## 5. Configuration files read (legacy)

| File | Sound-relevant content | Changed? |
| --- | --- | --- |
| profile `cam.cfg` (the PC original with only `game_screen_size` set to 1280×720 in Phase 3C) | `sfx_vol_music -497`, `sfx_vol_2d 0`, `sfx_channels 48`, `game_hardware 1`, **`sfx_device 4`**, `sfx_eax 1`, `sfx_vol_3d 0`, `reverse_stereo 0`, `master_volume 0` | **Only `sfx_device 4` → `1`** by the launcher (§6) |
| game folder `cam_ext.cfg` (the PC original) | `sfx_mixer_timeout 200` (SS2.log); no driver, device or mute setting | no |
| profile `user.cfg` (the PC `User.cfg` + the launcher's Interface size line) | nothing about sound | no |
| `install.cfg` | paths only | no |
| `snd_oal_device`, `snd_speaker_config`, `snd_decompress_on_load` | not set anywhere (defaults) | — |

## 6. Modded audio resources

| Mod | Files | Result |
| --- | --- | --- |
| `thelovebat-SS2-altMusic` | `snd/sfx/mloop1.ogg`, `mloop2.ogg` (menu loops), `snd/amb/elevator.ogg`: Ogg Vorbis, 44.1 kHz stereo | **Found and opened** through the VFS: `fopen("c:\dmm\thelovebat-SS2-altMusic\snd\sfx\mloop…", "rb")` returned a handle (`--trace-only` run `trace-dev1`). NewDark decodes Ogg itself (`snd_decompress_on_load` default 1) and hands PCM to DirectSound. Menu windows then show peak 12,742–18,312 with nothing else playing |
| `SS2_Vaxquis_VintageSongRemake` | 72 PCM `.wav` songs in `snd/songs` | Same `mod_path`, PCM WAV needs no decoder. Not opened in these runs (they are level music) |

No mod was disabled, moved or edited. The music mods were never the cause: with OpenAL selected nothing at all reaches
the mixer, mod or not.

## 7. Change

| File | Change |
| --- | --- |
| `android/app/src/main/java/local/newdarkandroid/ss2/SoundDevice.java` (new) | `withoutOpenAL()`: replaces each whole line `sfx_device 4` (any case, blanks, an optional `;` comment) with `sfx_device 1`; nothing else. `apply()`: works on the profile's `cam.cfg`, or else on the game folder's (then written as a new profile copy; the game folder is never written). The first time a profile's own file changes, the original goes to `cam.cfg.openal`, never overwritten later. Text is handled as ISO-8859-1, so every byte survives |
| `LauncherActivity.java` | Calls it at every game start, next to the Interface size step, and logs `sound: …` |
| `android/tools/SoundDeviceTest.java` (new) | Desktop test (javac/java, no Android): 26 checks, including `sfx_device 0/1/2/3/40/4.0/44` left alone, commented-out lines, CRLF/LF, a non-UTF-8 byte, the backup never overwritten, and the real legacy `cam.cfg`: exactly 1 line differs (line 21), same length. **ALL PASS** |
| `android/app/build.gradle` | `versionCode 11`, `versionName 0.8.2-audio` |
| `android/README.md` | "Sound driver" paragraph |

Why the launcher and not elsewhere: the setting is the game's own, written by its menu, and the menu cannot pick OpenAL
on Android anyway (§8.3), so only a configuration brought from a PC can carry it. The launcher already rewrites one
marked line of `user.cfg` at every start (Interface size), so this follows an existing pattern. It applies to both
apps, so a future import of any OpenAL-configured PC installation into the regular app is covered too. It does not
need a native rebuild, the host or engine code stays untouched, and the module identity checks are unchanged.

Not done: an OpenAL implementation (a new sound engine; out of scope), any change to the DirectSound mixer.

## 8. Device results after the fix

APK `0.8.2-audio-legacytest-play`, `c8212078…`, installed over the legacy app with `adb install -r`. Its native
libraries are byte-identical to the accepted legacy APK (`libss2game.so` `ea4b8b20…`, `libSDL2.so` `655d92a2…`); only
the launcher's Java code differs. Profile after install: 181 of 181 files identical.

### 8.1 First start on the real profile (run `fixed-real`, normal launcher path)

- Launcher log: `sound: …/profiles/scp4/cam.cfg selects OpenAL (sfx_device 4), which this port cannot play; … now has
  sfx_device 1 (DirectSound), the original is kept as cam.cfg.openal`.
- `cam.cfg` `5453ac74…` → `e796c880…`; `cam.cfg.openal` = `5453ac74…` (the original, byte for byte).
- SS2.log: `sfx_device 1`, no OpenAL lines. `DirectSound: mixing to 44100 Hz stereo`.
- All 161 save files (`save_0`…`save_14`) identical before and after. Changed besides the fix: `SS2.log` and the game's
  `current/` working copy (rewritten on every load, as before).

### 8.2 Measurements (5 s windows, `SS2PORT_PROFILE=1`)

| Scene | Before (`base-orig`) | After (`fixed-real`) |
| --- | --- | --- |
| Start-up and main menu (menu music) | 0 callbacks, peak 0 | 215 callbacks, peak 12,742 (78 silent: the first second, before the music starts) |
| Menu → Load Game | 0, 0 | 234, peak 19,743, 15 silent |
| Level load (Hydroponics, slot 0) | 0, 0 | 215, peak 18,061, 4 silent |
| Gameplay, standing and walking | 0, 0 | 215–216, peak 12,172–16,610, **0 silent** |
| Longest gap between callbacks | — | 40–41 ms (Android's burst scheduling, as in Phase 1D) |
| AudioFlinger track | none | `Active yes`, state `A`, 44,100 Hz stereo, **Underruns 0**, L/R volume 1.0, routed to `AUDIO_DEVICE_OUT_SPEAKER` (`legacy-audio/audioflinger-track-fixed-real.txt`) |
| Android media volume | — | `STREAM_MUSIC` index **2 of 15** on the speaker, not muted, no Bluetooth device (`stream-music-volume.txt`) |

RMS was not computed: the callback statistic is peak and silent-buffer count only, and adding RMS would have meant a
native rebuild for no diagnostic gain here.

### 8.3 Regression checks

| Check | Result |
| --- | --- |
| Audio after loading a save | **Verified** (slot 0, 8.2) |
| Audio in a new game process | **Verified**: every run is a new `:game` process; `fixed-real` and `lifecycle-real` both open DirectSound |
| Home → background → icon → resume (`lifecycle-real`) | **Verified**: player `started` (26 s) → `paused` while on the home screen (40 s) → `started` after the icon (56 s), same pid. Callbacks back to 214–216 per 5 s right after resume; one 15.9 s gap while away; `exit 0` |
| Pause (Options panel) | Menu music stops while the Options panel is open (peak 0 in the control run with no click) and comes back after Done (peak 16,728–19,304). Normal game behaviour, same with or without a setting change |
| Changing "Hardware Acceleration" in the menu | Cycles **Off → On → (tries OpenAL, fails, skips it) → On** (`hw-accel-cycle.png`). After two clicks and Done, music plays again (peak 16,728–19,304, 0 silent). The menu cannot select OpenAL on Android, and the launcher repairs a hand-edited `cam.cfg` at the next start |
| Level transitions (deck change) | **Untested** (no scripted route to an elevator panel, as in Phase 3C). A level load from the menu was tested |
| Individual sounds (footsteps, weapon, voice, UI clicks) | PCM is present while walking; which sounds they are **needs your ears** |
| Regular app | Not reinstalled. After the comparison run its `profiles/`, `touch-settings.cfg` and `shared_prefs` are byte-identical to the start (the test run's settings in `runs.xml` were put back to the original file, verified by its hash `902bc1b7…`). Its run history has one more line (the comparison run) |
| Conformance, touch, module identity | Native code unchanged (identical libraries), so not re-run for this milestone; run in the final integration pass |

## 9. Remaining uncertainties

- **Audibility is not verified by a person** for either app. Everything up to the Android speaker track is measured.
- The PC used OpenAL with EFX reverb (`sfx_eax 1`); on the port there is no reverb, as there never was. 3D sound is the
  engine's own software mixing (Hardware Acceleration Off).
- `sfx_device 2` (On) works but was only measured in the menu.
- If the player later copies a PC `cam.cfg` with OpenAL into the profile, the next start fixes it again; the
  `.openal` backup keeps only the first original.

## 10. Listening checklist (about 5 minutes, in "SS2 Legacy test")

Turn the **media volume up first** (it was 2 of 15). No Bluetooth headset, or note which one.

1. Start the game: the main-menu music (thelovebat's menu loop) plays.
2. Hover and click menu buttons: UI clicks.
3. Load your latest save: ambience, then footsteps when you walk.
4. Swing or fire a weapon; pick up an item; open the inventory: weapon, pickup and interface sounds.
5. Play an audio log or walk to a voiced event: voice is clear.
6. Press Home for 5 s, then the app icon: sound stops while away and comes back, no burst of stale audio.
7. Take an elevator to another deck: sound continues after the load.

Pass if all are heard without crackle. Note anything missing and when.

## 11. Files

`legacy-audio/`: `audio_run.py` (driver; disposable write folder unless `REAL=1`; `APP=regular` for the regular app),
`runs-summary.txt` (every run: script, SS2.log sound lines, port lines, profile windows, history, Android player
state), `audioflinger-track-fixed-real.txt`, `stream-music-volume.txt`, `options-audio-hw-off.jpg`,
`hw-accel-cycle.png`. Not in git: `build/android/legacy-audio/` (full logs, dumpsys captures, profile hash lists),
`build/android/baseline/legacytest-audio/` (the new APKs and `SHA256SUMS`: play `c8212078…`, dev `919f4839…`).
