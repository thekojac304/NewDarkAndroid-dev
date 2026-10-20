# Phase 3G: final UX polish (anchored radials, launcher, app icon)

2026-10-19. Branch `android-port`, from `baaa750` (Phase 3F, clean). Galaxy S26 Ultra (SM-S948U1, Android 17 / API 37).
Evidence: `phase3g/` (device screenshots, touch logs, the icon set and its preview, the device driver).

| Success criterion | Result |
| --- | --- |
| Radial menus open around the held button | **Done, verified on the phone and by tests.** Every wheel grows out of the button being held; at a screen edge it fans towards the middle |
| The launcher feels like a game launcher | **Done, verified on the phone.** Title, installation card, one PLAY button, a short menu; the diagnostics are behind Advanced |
| A proper Android icon (wrench, blue-green cyber motif) | **Done, verified in the phone's app drawer.** Adaptive icon with foreground, background and monochrome layers, original artwork |
| Gameplay, both SCP versions, saves, settings, profiles intact | **Verified.** Beta 4 loads `nv124`, Beta 8 loads `nv`; 345 save files, the touch settings and the folder labels byte-identical afterwards |

## 1. Anchored radial menus (Milestone A)

**Before:** every wheel opened at the screen centre (Shockolate's `touch_radial_open`), so the finger had to travel from
`<` `>` (bottom), AMMO / ITEM (left edge) or II (top-left corner) to the middle of the screen.

**Now** (`touch_radial_open_at`, `port/backends/android/touch/touch_radial.c`):

- The wheel is centred **exactly on the held button**. The finger that held it starts in the cancel circle (radius at
  least the finger's distance from the centre plus 3 % of the screen height), so lifting without sliding does nothing
  and there is no pointer jump: the same finger slides straight out into an entry.
- **Edges:** every default wheel button sits at an edge, where a whole wheel would leave the screen. The selector then
  samples all directions (2-degree steps) and fans the entries over the widest arc whose rim stays on the screen (1.5 %
  margin), centred in it, at most a quarter turn per entry. Because the screen is convex and the centre is on it, a
  segment whose rim is on the screen is wholly on it, so no entry or label is ever clipped.
- **Size:** if a fan's entries would be narrower than about half of a centred wheel's, the fan grows in 10 % steps, up to
  1.5x (and at most 45 % of the screen height). In practice only II's corner fan grows (4 entries over 108 degrees at
  1.5x). A fan of one or two entries keeps the normal size (fixed during device testing, `25a2584`).
- **Selection:** release on an entry selects it. Releasing in the cancel circle (on the button), beyond 1.25x the rim,
  or behind the fan cancels. Up to half an entry (20 degrees at most) past either end of the fan still counts as that
  end's entry. A button with room all round (a custom layout) gets the whole wheel with entry 0 at the top, as before.
- **Drawing** (`touch_render_gles.c`): a fan draws only its arc, its rim and both end edges. The title line (the
  highlighted entry in full) sits beyond the fan's middle: above `<` `>`, to the right of AMMO / ITEM, below-right of II.
  It is kept on the screen. "CANCEL" stays readable when the button touches the screen edge; the part of the cancel
  circle beyond the edge is simply not drawn, since nothing can touch there.
- **Scope:** opt-in through `TouchControls.radial_anchored`, set by the SS2 glue (`touch_ss2.c`). Without it, the
  Shockolate core behaves exactly as before (centred wheel), and its six accepted suites pass unchanged.

| Default button (2340x1080) | Fan | Centre, radii (px) | Arc |
| --- | --- | --- | --- |
| `>` (bottom) | upwards | 1287,972; 85 / 302 | 212 degrees (from 254 clockwise from up) |
| `<` | upwards, wider | 1287,842; 85 / 302 | 272 degrees |
| AMMO, ITEM (left edge) | rightwards | 105,356 / 105,508; 85 / 302 | 212 degrees |
| II (top-left corner) | down and right | 117,65; 85 / 454 (grown) | 108 degrees |

Unchanged: tap actions (`<` `>` one notch, AMMO B, II Esc, ITEM's notice), the 400 ms hold, the wheels' contents and
commands, direct and relative cursor, stick and look ownership, multitouch, Home/focus-loss cleanup, the settings file.

## 2. Launcher redesign (Milestone B)

`LauncherActivity` was the Phase 1B test screen grown by accretion: a plain Material list with the diagnostics, results
and a 300-line log on the main screen. It is now a game launcher. The look is drawn in code by `LauncherUi`, with no
images and no game assets: a deep blue-black backdrop with a faint teal grid, barely-visible scanlines and corner
brackets; chamfered panels with a thin teal edge and bright marks at the cut corners; condensed, tracked type for labels.

| Section | Contents |
| --- | --- |
| Title | The app icon, **NEWDARK SS2**, "System Shock 2 · NewDark 2.48 for Android", "UNOFFICIAL FAN PORT" |
| Installation card | The selected game folder by its display name (a leading "System Shock 2 —" becomes a small line above it), e.g. **SCP Beta 4 (Legacy)**; "Folder scp4 · 15 saved games"; READY, or what is wrong (no SS2.exe, unsupported SS2.exe, nothing installed yet). **Change** lists the installations by name; **Rename** sets the display name (holding the card still works) |
| PLAY | The one primary action (touch on, no frame limit, as before). Shows RUNNING... while a run is pending. A line under it shows import progress or why a start failed |
| Menu | **Controls guide** (move/look, tap vs hold, radial menus, the two cursor styles, settings in the game), **Interface size** (a chooser), **Import a game folder...**, **Advanced** |
| Advanced (closed at first) | What the old screen showed: package, version, device, folders; the dev build's conformance buttons; the last runs' verdicts; the last run's log |
| Footer | Unofficial, non-commercial; runs your own copy; not affiliated with or endorsed by the game's developers or publishers; the version |

- Phone in portrait: one centred column (at most 560 dp). At 720 dp or wider (a phone in landscape, a tablet): two
  columns, with the title, card and PLAY on the left and the menu and Advanced on the right.
- The dialogs (installation chooser, interface size, controls guide, rename, import) use the same dark panel.
- Theme: `Theme.Material.NoActionBar`; the status and navigation bars take the backdrop's colours.
- **Unchanged:** the run queue, verdicts and `history.txt`, every adb extra (`autorun`, `game`, `uisize`, `env`,
  `portargs`, `frames`, `touch`, `label_game`/`label`, `import_test`), the `runs` and `game_labels` preferences, the
  import logic, the Interface size values written to `user.cfg`, `SoundDevice`, the play/dev split.
- **Dev-only UI that remains:** the conformance and headless-game buttons (dev build, inside Advanced) and the raw log.

After: `phase3g/device-launcher-main-and-scrolled.jpg`, `device-launcher-dialogs.jpg`,
`device-launcher-switch-beta8-beta4.jpg`. No "before" screenshot was taken (the old screen: title, Play, Game folder,
Interface size and Import buttons, the folder list, then Diagnostics/About, results and the last run's log, all on one
plain Material page).

## 3. App icon (Milestone C)

The app had no icon (the system default). Now: an original adaptive icon. A light steel-teal open-end wrench lies on the
diagonal with a soft teal glow, over a HUD reticle (two brackets on the free diagonal, tick marks, a faint inner ring)
and two short circuit traces ending in nodes. Behind it is a deep blue-black field with a radial falloff and a faint teal
grid. There is no lettering, logo or game art; it is meant to evoke System Shock 2's wrench and interface without
copying them.

| File | Role |
| --- | --- |
| `res/drawable/ic_launcher_foreground.xml` | Wrench (gradient fill, dark outline, two glow strokes, a highlight), reticle, traces |
| `res/drawable/ic_launcher_background.xml` | Radial gradient field and grid |
| `res/drawable/ic_launcher_monochrome.xml` | One-colour wrench, reticle and traces for themed icons (Android 13+) |
| `res/mipmap-anydpi-v26/ic_launcher.xml`, `ic_launcher_round.xml` | The adaptive icon (`android:icon`, `android:roundIcon`). minSdk is 26, so no bitmap fallbacks are needed |
| `android/tools/make_icon.py` | Generates all of the above, plus SVG copies (`phase3g/icon/*.svg`), from one shape definition |
| `android/tools/icon_preview.py` | Previews: circle, squircle and rounded masks at 192/96/48/32 px, dark and light wallpapers, three themed tints, the 66 dp safe circle (`phase3g/icon/preview.png`) |

All content lies inside the 66 dp safe circle; the wrench's tip and end are at about 31 dp from the centre. On the
phone, One UI's app drawer shows it as "NewDark SS2" (`phase3g/device-icon-app-drawer.png`).

## 4. Tests

### Automated (PC)

| Suite | Result |
| --- | --- |
| Touch bridge (`port/backends/android/touch/tests/run.sh`) | **0 of 35,324** failed (615 before; the new checks are mostly geometry sweeps) |
| Shockolate core suites (`phase3f/run_shockolate_suites.sh`, asserts on) | touch_controls, alpha11, alpha2, alpha3, alpha4, ammo: **all pass** |

New tests: every wheel button of the default layout on 7 screen/game sizes (2340x1080 at 1280x720, 800x600 and
1024x768; 1920x1080; 1280x800; 1024x768; 1080x1080), with normal and large entry counts (up to 16). For each: the wheel
is centred on the button; the finger starts in the cancel circle with nothing highlighted; every entry's rim, edges and
label are on screen; a sweep from the first entry to the last meets each entry once, in order, with no gap; lifting
without a slide does nothing (no entry, no tap action); sliding the same finger to the last entry selects it; out past
the rim or back onto the button cancels. Fan directions: `<` `>` up, AMMO/ITEM right, II down-right. Custom layouts: the
weapon button in all four corners, at all four edges and in the middle, with 1, 6, 11 and 16 entries. In the middle it
gets a whole wheel with entry 0 at the top; a one-entry fan keeps the normal size. Also: walking with the stick moving
while a fan opens and selects; Home with a fan open (all released, nothing chosen); the centred wheel without the hook;
a selector without the new fields.

### On the phone (HID touchscreen through `uhid_input -t`, disposable write folders)

Driver: `phase3g/radial_dev3g.py`, Phase 3F's driver with targets from a port of `touch_radial_open_at`.

| Check | Result |
| --- | --- |
| Weapon fan on `>` (Beta 4, 1280x720) | Opened upward around `>`; same finger slid to PISTOL; `equip_weapon Pistol` (`device-b4-720-weapon-fan-next.jpg`) |
| AMMO fan, left edge | Fanned right; title beside it; ANTI-PERS. loaded (`device-b4-720-ammo-fan-left-edge.jpg`) |
| System fan from the top-left corner | Past the rim: nothing. Entry 0: `quicksave` (into the disposable folder only) (`device-b4-720-system-fan-corner.jpg`); at 1024x768: STOP LOG → `stop_email` (`device-b4-1024-system-fan.jpg`) |
| Wheel while walking | Stick held forward (W down) the whole time; `<` fan → SHOTGUN equipped; W released only when the stick finger lifted (`device-b4-720-weapon-fan-while-walking.jpg`) |
| ITEM | Lifted on the button: cancelled, no item used |
| Home with a fan open | Nothing selected, the stale finger did nothing, the same game came back in LOOK |
| Beta 8 (800x600, wrench only) | One-entry fan at normal size, WRENCH (`device-b8-800-one-entry-fan.jpg`); AMMO: "NO AMMO FOR THIS WEAPON", no wheel |
| Module selection | Beta 4: `nv124`; Beta 8: `nv` (run logs, `device-*-touch.log`) |
| Launcher PLAY (real tap) | `game folder scp4, frame limit none`; SCP Beta 4 main menu (`device-play-scp4-main-menu.jpg`); DirectSound 44.1 kHz stereo in the run log |
| Home / icon | Home, then the launcher icon intent: the same game process (pid 8952) came back to the front |
| Installation switching | Change → SCP Beta 8: card "SCP Beta 8 · Folder ss2scp · 2 saved games", preference `game` = `ss2scp`; back to Beta 4: `scp4` |
| Dialogs | Controls guide, chooser, interface size, rename: shown and dismissed; nothing written |
| Icon | In the app drawer, in One UI's squircle |
| Cursor | The radial runs used your settings (relative cursor); the cursor code is unchanged and covered by the bridge tests |

### Data safety

Before the phase: the installed APK (`390c597d…`, pulled), a tar of the app's internal files (excluding `files/runs`),
`shared_prefs`, and a sha256 manifest of 411 internal files (`build/android/phase3g-preserve/`). After all tests:
**411 files, none added or removed; all 345 save files, `touch-settings.cfg` (`d2102308…`) and `game_labels.xml`
byte-identical.** Only run bookkeeping changed (`results/*.txt`, `profiles/scp4/SS2.log`, `runs.xml`, Android's
`ActivityThread.IDS.xml`). Game folders: 8,207 files present. The legacy test app (0.8.3) was not touched. Free space:
19 GB.

Your own SCP Beta 4 session (started with Play at 21:45) was running when the phase began; it was closed by the first
install, after you confirmed there was nothing to save.

## 5. APKs (`build/android/baseline/phase3g-polish/`, `SHA256SUMS`)

| Build | Bytes | sha256 |
| --- | --- | --- |
| **Play 0.9.3-polish (code 16, installed)** | 47,547,543 | `782497f586edb4f84f1210d103c1725a3c0f12cdba874663af23bd32629dc8f3` |
| Dev 0.9.3-polish | 96,371,889 | `31bb5b61faa015a792ca801fc899659c4500a5111b72f1f14d0a81ee6d105604` |
| `libss2game.so` in the play APK | 45,994,840 | `fe7e8e930fcf15919ce2fa1af78cac7749bb90412d00789ebeda1b7a5c4b1d14` |
| `libSDL2.so` | 1,420,256 | `741436830e92d588fa6e94728bd04287e1059876b192477b4b8e741614abe2e6` (unchanged) |

Same package (`local.newdarkandroid.ss2`); an in-place update keeps all data. Built with Phase 3E's native command (same
lifts and NVScript identities) and a clean Gradle build. Previous baseline kept: Phase 3F play `390c597d…`
(`baseline/phase3f-radials/`, and the pulled copy in `phase3g-preserve/apk/`).

## 6. Commits

| Commit | |
| --- | --- |
| `130f54e` | Phase 3G-A: radial menus open around the held button |
| `b9bfb43` | Phase 3G-C: adaptive launcher icon |
| `d69b071` | Phase 3G-B: launcher redesign |
| `25a2584` | Phase 3G-A fix: one- and two-entry fans keep the normal size |
| (this commit) | Phase 3G-D: version 0.9.3-polish, this report, the device driver and evidence, index |

Nothing was pushed or published. Shockolate, the LaunchBox and Steam installations and the game-compatibility code
were not modified.

## 7. Remaining limitations

- **Feel is yours to judge:** fan size, reach to II's corner fan (it grows to 1.5x so its four entries stay wide), and
  whether up-fanning `<` `>` sit well under the thumb. The geometry constants are in `touch_radial_open_at` if you want
  them changed.
- A fan's entries sit where the screen has room, so II's fan is drawn over SET, AMMO and ITEM while it is open. The
  holding finger only ever selects wheel entries; another finger landing on those buttons acts as it did in Phase 3F.
- **Not exercised on the phone:** the two-column launcher (the phone stayed in portrait; it is code-checked only), the
  themed (monochrome) icon on One UI (previewed on PC only; turning on themed icons is a system setting), the import flow
  (unchanged code; the folder picker was not opened), direct cursor (unchanged; bridge-tested).
- The controls guide is text only; it does not show the actual layout.
- A settings file saved by Phase 3E (yours) still loads the wheel defaults with "(defaults: the file predates them)"; it
  is only rewritten when you press SAVE in SET.
- Conformance was not re-run: no runtime code outside the touch layer and the launcher changed.

## 8. Your acceptance checklist (about 10 minutes)

1. The app drawer shows **NewDark SS2** with the teal wrench icon. Open it: the new launcher, with SCP Beta 4 (Legacy)
   selected and "15 saved games".
2. **Change** → SCP Beta 8 → the card changes; Change → back to Beta 4.
3. **Controls guide** and **Interface size** open and close; Interface size still shows your choice.
4. **PLAY** → load a save. Hold `>`: the weapon wheel fans **upwards from the button**; slide the same finger onto a
   weapon and lift. Hold `>` again and lift without moving: nothing happens.
5. Hold **AMMO** (left edge): ammunition and settings fan to the right. Hold **ITEM**: hypos fan to the right.
6. Hold **II** (top-left): the system wheel fans down and right; slide past its rim and lift to cancel.
7. Walk with the stick and hold `<` at the same time: you keep walking while choosing.
8. Taps are unchanged: tap `>` (next weapon), tap AMMO (cycle ammo), tap II (menu).
9. Home, then the icon: the game comes back where it was.
10. Comfort: fan size and reach. **Needs your hands.**
