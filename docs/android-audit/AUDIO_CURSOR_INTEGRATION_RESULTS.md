# Phase 3D: legacy audio repair and relative touch cursor — integration results

2026-10-18. Branch `android-port`, from `1c4b8bb` (clean) to `0f684fb` plus this report. Galaxy S26 Ultra (SM-S948U1,
Android 17 / API 37, SM8850). Details: [LEGACY_AUDIO_RESULTS.md](LEGACY_AUDIO_RESULTS.md) (milestone A) and
[RELATIVE_CURSOR_RESULTS.md](RELATIVE_CURSOR_RESULTS.md) (milestone B).

How each result was obtained:

| Label | Meaning |
| --- | --- |
| **Device** | Measured on the phone (logs, Android audio state, screenshots) |
| **Device (HID)** | On the phone through the real touchscreen input path, driven by a test HID device, not by a hand |
| **Automated** | PC unit tests |
| **Human** | Confirmed by a person (the player) |
| **Inference** | Reasoned from code or data, not observed |
| **Untested** | Not run in this phase |

## 1. Audio: root cause and fix

**Root cause (Device).** The legacy profile's `cam.cfg` comes from the PC, where the Audio menu's "Hardware
Acceleration" was set to **OpenAL** (`sfx_device 4`; `OpenAL32.dll` sits next to the PC's `ss2.exe`). The port has no
OpenAL. NewDark tries to load `openal32.dll`, fails (`OpenAL not found on system or failed to load`), and then opens
**no sound device at all**: no DirectSound object, 0 audio callbacks, no Android audio player. Every sound was silent,
music mods or not. The regular Beta 8 profiles have `sfx_device 1` (the game's default), which is why that app was
not affected.

**Fix.** The launcher (shared by both apps, `SoundDevice.java`) changes an `sfx_device 4` line in the `cam.cfg` the game
reads to `sfx_device 1` (DirectSound, Hardware Acceleration Off) at every game start, keeping the original as
`cam.cfg.openal`. One line; no engine, host, mixer, mod or save change; native libraries byte-identical for that
milestone. The game's menu cannot select OpenAL on Android (it tries and skips it), so only a PC configuration can
bring the setting back, and the next start repairs it.

**Before → after (Device), legacy app, slot 0:** 0 → 215–233 callbacks per 5 s (44.1 kHz stereo, 1,024-frame buffers),
peak 0 → 12,000–19,900, gameplay 0 silent buffers, Android OpenSL ES player none → `started`, AudioFlinger track
active with 0 underruns, routed to the speaker. Home pauses the player and the icon resumes it.

## 2. Is it audible?

**Not verified by a person.** Everything up to the Android speaker track is measured; nobody listened, for either
app. The phone's media volume was **2 of 15** during the tests (not muted, no Bluetooth device) — turn it up before
judging. The listening checklist is §9.

## 3. Cursor styles

| Style | Behaviour | Status |
| --- | --- | --- |
| **Direct** (default) | Unchanged: the cursor goes to the finger, a tap clicks there, a drag presses at the start point | Automated (169 unchanged checks) + Device (HID): taps and an inventory drag after switching back |
| **Relative** (trackpad) | A touch moves nothing; a drag moves the cursor from where it is (1:1 on screen at speed 1, also on 4:3 menus); a tap clicks at the cursor; two taps = two clicks; hold still 0.5 s = left button down, the drag carries it, lift releases; USE and mouse look unchanged | Automated (176 new checks) + Device (HID) at 1280×720, 800×600, 1024×768 |

Each relative gesture starts from the game's real cursor (`user32_cursor_pos`), so it continues from where a USB mouse,
the game or a direct tap left it (**Device (HID)** with a second test device acting as a USB mouse). A 1-pixel drift
found on the device (the host's window mapping rounds down) was fixed before acceptance; afterwards five rapid taps
clicked at exactly the same pixel.

## 4. Touch settings changes

- SET → MORE (page 2): **CURSOR DIRECT / CURSOR RELATIVE - TRACKPAD** and **CURSOR SPEED − x.xx +** (0.25–4.00, step
  0.25) at the top; the five existing rows (haptics, haptic strength, three wheels) remain, slightly tighter.
- `touch-settings.cfg`: two new keys, `cursor_relative` and `cursor_speed`, in the **same file version 3**. Older files
  load as Direct at speed 1 with every other value kept (Automated); older builds ignore the keys. Reset → Direct/1.
- Persisted across a game restart and across `adb install -r` (Device (HID)).
- The legacy app had no settings file before the tests; the test file was removed afterwards, so it is back to
  Direct with the default layout. The Beta 8 app's settings were never touched.

## 5. Tests and regressions

| Test | Result | Label |
| --- | --- | --- |
| Conformance on the phone, default lift | **0 / 511** failed | Device |
| … `--cache-regs --abi-check` | **0 / 511**, ABI check: 0 call sites broke the contract | Device |
| … `--write-dir` (write folder / VFS) | **0 / 113**, game folder unchanged | Device |
| SS2 touch bridge suite | **0 / 345** failed (169 existing + 176 new), no warnings | Automated |
| Shockolate core suites from `7502370` (`touch_controls`, `alpha11`, `alpha2`, `alpha3`, `alpha4`, `ammo`) | all PASS, expectations unchanged | Automated |
| `SoundDeviceTest` (sound-driver rewrite) | 26 checks PASS; on the real `cam.cfg` exactly 1 line differs | Automated |
| Module identity | Unchanged checks. Legacy build: NVScript 1.2.4 descriptor (size 0xa8000, CRC 0x83eb1ef4). Regular build (compiled, not installed): Beta 8 descriptor (0xb2000, CRC 0x7426e52a). Lift reused ("already lifted") in both | Device + build |
| Native script modules | allobjs, NVScript 1.2.4 (78 imports bound, `DllMain -> 1`), Squirrel 1.02 loaded; Quest Note Notifier initialized; monolog empty | Device |
| Startup and normal exit | Final run: `verdict=PASS`, `exit 0` after 40.6 s; bounded runs throughout ended `exit 0` (except force-stopped test runs) | Device |
| Saves | Slot 0 (Hydroponics) loads; the 161 original save files are byte-identical to the session start | Device |
| Inventory, PDA, map, save/load screens, LOOK/CURSOR | Worked in both cursor styles (§3 of the cursor report) | Device (HID) |
| Lifecycle | Home → icon resumes the same process with audio paused/resumed; Home during a held drag releases the button; stale finger does nothing | Device / Device (HID) |
| Level transition (deck change) | **Untested** in this phase | — |
| Regular Beta 8 app on the phone | Not reinstalled: `2a5ad353…`, profiles, touch settings and preferences byte-identical to the start (220 files) | Device |

The final integrated run (installed final APK, real profile, normal launcher path; evidence
`integration-3d/final-run.txt`, `final-run-inventory.jpg`): NewDark 2.48, the SCP Beta 4 `mod_path` with all 17 mods
in order, `sfx_device 1`, DirectSound at 44.1 kHz, three script modules, the notifier, slot 0 loaded, inventory open,
touch controls on with defaults (Direct), audio 215–233 callbacks / 5 s, `exit 0`.

## 6. APK identities

| Build | Version | Commit | APK sha256 (play / dev) | `libss2game.so` | Where |
| --- | --- | --- | --- | --- | --- |
| Accepted legacy (start) | 0.8.1-import-fix-legacytest | `1c4b8bb` | `192824dc…` / `8d60f4d3…` | `ea4b8b20…` | `build/android/baseline/legacytest/` (kept) |
| Audio milestone | 0.8.2-audio-legacytest (code 11) | `16ff614` | `c8212078…` / `919f4839…` | `ea4b8b20…` (identical) | `build/android/baseline/legacytest-audio/` |
| **Final, installed** | **0.8.3-cursor-legacytest (code 12)** | **`0f684fb`** | **`59d5f447…`** / `b4464610…` | `a7c01712…` | `build/android/baseline/legacytest-cursor/` |
| Regular Beta 8 (on the phone) | 0.8.1-import-fix | — | `2a5ad353…` | unchanged | not reinstalled |

`libSDL2.so` `655d92a2…` in all three legacy builds. Full hashes in each folder's `SHA256SUMS`. No game assets are in
git or in any APK; the APKs stay local.

## 7. Commits

| Commit | Content |
| --- | --- |
| `16ff614` | Phase 3D-A: legacy app audio — an OpenAL `sfx_device` becomes DirectSound (`SoundDevice.java`, launcher call, desktop test, docs, evidence) |
| `0f684fb` | Phase 3D-B: optional relative (trackpad) cursor style (bridge, settings, panel, `user32_cursor_pos`, 176 tests, docs, device evidence) |
| (this commit) | This report and the final-run evidence |

Nothing was pushed or published.

## 8. Remaining limitations

- Audibility of both apps: **needs the player's ears** (§9).
- No OpenAL and no EFX/EAX reverb on the port (there never was); 3D sound is the engine's own software mixing.
- Relative cursor comfort (speed, the 0.5 s hold) is **untested by a hand**; no acceleration curve.
- Deck changes (level transitions), replicators, psi and long sessions remain untested, as after Phase 3C.
- `cam.cfg.openal` keeps only the first original if a PC `cam.cfg` with OpenAL is copied in again later.
- The Black Screen overlay app (`io.japp.blackscreen`) was running but not covering the screen during these tests; if
  it is up, it takes all touches.

Unchanged by this phase: the LaunchBox and Steam installations (no file newer than the session start; only read),
Shockolate (`7502370`, its working tree untouched), SCP Beta 4, the mod order, the 15 original saves, the Beta 8 app and
its data, the module identity checks.

## 9. Acceptance checklist for the player (about 15 minutes, "SS2 Legacy test")

Sound (turn the **media volume up** first):
1. Start the game: the main-menu music plays.
2. Menu clicks; load your latest save: ambience, footsteps, a weapon swing, an item pickup, an audio log or voice.
3. Home for 5 s, then the icon: sound stops and comes back cleanly.

Cursor:
4. Default is DIRECT and behaves as before.
5. SET → MORE → CURSOR → RELATIVE - TRACKPAD → SAVE / DONE. Slide anywhere: the arrow moves from where it was; tap
   anywhere: it clicks under the arrow. Try CURSOR SPEED 0.75–2.00.
6. In game, MODE → CURSOR: hold still on an item about half a second, drag it to another slot or an equipment slot, lift.
7. PDA e-mail, the save screen (Done to leave), then MODE → LOOK: looking around must not click or move the cursor.
8. Switch back to DIRECT if preferred.

Report anything silent, crackling, stuck or jumping, and when it happened.
