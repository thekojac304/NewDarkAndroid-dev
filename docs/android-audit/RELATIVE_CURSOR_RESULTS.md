# Phase 3D-B: optional relative (trackpad) cursor

2026-10-18. Branch `android-port`, after `16ff614` (Phase 3D-A, audio). Galaxy S26 Ultra, legacy-test app
(`local.newdarkandroid.ss2.legacytest`, SCP Beta 4). Goal: a second cursor style that works like a laptop trackpad,
next to the existing direct-touch style, which stays the default.

Labels: **Verified on device (HID)** = run on the phone through the real touchscreen input path, driven by a test
tool, not by a hand; **Automated** = PC unit tests; **Needs your hands** = comfort and feel, which only a person can judge.

## Summary

| | |
| --- | --- |
| Styles | **Direct** (default, unchanged): the cursor goes to the finger. **Relative**: the finger moves the cursor from where it is |
| Where | Touch settings (SET) → MORE → **CURSOR DIRECT / CURSOR RELATIVE - TRACKPAD** and **CURSOR SPEED − 1.00 +** (0.25–4.00 in steps of 0.25). Saved in `touch-settings.cfg` with the rest; no separate build |
| Relative gestures | Drag past the slop: moves the cursor, no button. Tap: left click where the cursor is. Two taps: two clicks. Hold still 0.5 s: left button down at the cursor (haptic if haptics are on), the drag carries it, lifting releases it. USE (right click) unchanged |
| Device | **Verified on device (HID)** at 1280×720 (largest interface) and at 800×600 and 1024×768: menus, load screen, save screen, inventory, inventory → equipment, PDA e-mail and logs, map, LOOK/CURSOR, Home/resume, USB-mouse coexistence, speed change, rapid taps; direct style unchanged. Every button press had its release (29 of 29, none out of order, none held at the end) |
| Tests | Bridge suite **345 / 345** (169 existing, unchanged, + 176 new); Shockolate's 6 accepted core suites pass unchanged |
| Not done | Physical comfort with a real finger (**needs your hands**, §8). Optional acceleration (not added) |

## 1. Design

Everything is in the existing touch code; no new framework. The Shockolate core (`touch_controls.c`) still decides
which finger owns the cursor and reports it as before: a position on touch, then a tap (short, within the slop) or a
drag (past the slop). Only the SS2 bridge (`touch_ss2.c`) acts differently on those reports when Relative is on:

| Core report | Direct (unchanged) | Relative |
| --- | --- | --- |
| Touch (`POINTER_MOVE` on down) | cursor to the finger | nothing; the gesture starts from the game's own cursor |
| Past the slop (`POINTER_DOWN`) | left button down at the start point | becomes a move; no button |
| Drag (`POINTER_MOVE`) | cursor to the finger | cursor += finger motion × speed (window pixels, sub-pixel fractions kept), clamped to the picture |
| Lift (`POINTER_UP`) | cursor to the finger, button up | cursor stays; a held button is released |
| Tap (`POINTER_TAP`) | cursor to the start point, click | click where the cursor is |
| Held still 0.5 s (new, bridge timer) | — | cursor re-asserted, left button down; moves wait one minimum hold so the game picks up what is under the cursor first (the Phase 2A inventory-drag ordering) |
| Lifted after 0.35–0.5 s without moving | nothing (as before) | nothing |

- **Scale.** At speed 1 the cursor moves as far *on screen* as the finger: window pixels per surface pixel =
  `speed × game_w / picture_w`, and on a 4:3 menu fitted into a wide picture also `× game_w / menu_w`, because the
  engine maps the whole window onto the menu (`ss2t_ui_rect`, as the direct style uses).
- **Where the cursor is.** Each gesture starts from the game's cursor, read from the host (`user32_cursor_pos`, new):
  the exact last placement if nothing moved it since, otherwise the window position mapped back to placement pixels
  (rounded up, so placing it there gives the same position). So it continues from wherever the game, a USB/Bluetooth
  mouse or a direct tap left it.
- **No new state that can stick.** Focus loss, Home, picture changes, LOOK/CURSOR changes and opening the settings all
  go through the existing cancel, which now also ends a relative gesture (and releases a held button).
- **Mouse look** (LOOK mode) and its sensitivity are untouched; CURSOR SPEED is separate.

## 2. Changes

| File | Change |
| --- | --- |
| `port/backends/android/touch/touch_ss2.c/.h` | The relative gesture (§1): `rel_*` helpers, state, `SS2T_HOLD_MS 500`, the optional `cursor_pos` service |
| `touch_settings.c/.h` | `cursor_relative` (default false = Direct) and `cursor_speed` (default 1, 0.25–4): defaults, validation, load, save. **Same file version 3**: an older file loads as Direct at speed 1 with everything else kept; an older build ignores the two new keys (so a rollback keeps the layout) |
| `touch_controls.h`, `touch_editor.c` | Three settings actions (style, speed −/+). The panel's second page has the two cursor rows first; its five existing rows moved down slightly, all still there |
| `touch_render_gles.c` | Labels: CURSOR DIRECT / CURSOR RELATIVE - TRACKPAD, CURSOR SPEED x.xx |
| `touch_ss2_android.c` | Connects `user32_cursor_pos` (weak); logs the style and speed at start and on save |
| `port/win32/user32.c` | `user32_cursor_pos()`: read-only; remembers the last placement (§1). No change to how input reaches the game |
| `tests/touch_ss2_test.c` | 176 new checks (§4) |
| `README.md` (touch) | Mapping table, file table, tests |
| `android/app/build.gradle` | `versionCode 12`, `0.8.3-cursor` |

The lift, the generated game code, the engine and the module loader are unchanged (the native build reported every
module "already lifted"). Only `libss2game.so` (touch and `user32.c`) and the conformance libraries (`user32.c`)
were recompiled.

## 3. Defect found and fixed during device testing

**One-pixel drift.** The first device run showed the bridge placing the cursor at 1147,655 and the host reporting it
back as 1146,654: the host's window mapping rounds down. Each new gesture would have resynchronised one pixel up and
left, and repeated taps would have walked the cursor. Fixed in `user32_cursor_pos` (exact last placement while
unmoved; otherwise rounded up). After the fix, five rapid taps all clicked at exactly 222,360 on the device.

## 4. Automated tests

`port/backends/android/touch/tests/run.sh` (zig cc, `-Wall -Wextra`: no warnings): **0 of 345 checks failed**. The 169
existing checks are unchanged and still pass (the direct style is the default). New:

| Area | Checks |
| --- | --- |
| No jump | A touch sends nothing; motion within the slop sends nothing |
| Motion | 144 surface px = 80 game px at 800×600; vertical; sub-pixel steps add up (9 × 1 px = 5 game px) |
| Speed | ×2 and ×0.5; mouse look still 80 game px for 144 px with a different cursor speed |
| Tap | Clicks at the cursor, not at the finger, despite jitter; placed before the press; released after the minimum hold |
| Double and rapid taps | 2 and 5 clicks, each in order, nothing held, queue empty, cursor unmoved |
| Slow tap | 0.4 s without moving: nothing, nothing left behind |
| Hold | Not at 0.4 s; at 0.51 s button down at the cursor, haptic once (none when haptics are off); the drag waits a minimum hold; lift releases with the cursor kept; a hold lifted without moving is one click; moving first never becomes a hold |
| Cancel | Focus loss during a held drag releases at once, the stale finger then does nothing; focus loss while moving releases nothing and the next touch does not jump; a picture change releases |
| Fingers | A second finger does not move the cursor; USE with a third finger right-clicks without moving it; the cursor finger continues; lifting far outside (in the bar) stops at the edge with nothing held |
| Modes | Shoot mode during a held drag releases; MODE back to the cursor continues from the old position |
| Game cursor | Starts where the game reports its cursor; continues when the report matches; follows a mouse move; physical clicks still pass |
| Menu mapping | 1280×720 with the 640×480 menu at 160,0 960×720: 90 px = 80 window px = 90 screen px horizontally, 60 vertically; clamped at 0, then back 10 px moves at once; in game (whole picture) 90 px = 60 game px |
| Settings | Defaults Direct/1/v3; a v3 file without the keys loads Direct/1 with opacity, positions, look and stick kept; round trip; speed clamped (99 → 4) and invalid (NaN → 1); a v2 file migrates; the panel: the rows only on page 2, every page-2 box inside the screen and none overlapping, style toggle by touch, speed bounds 0.25–4, layout/opacity/look untouched, save writes the file, the next touch uses the style, a restart restores it, Reset → Direct/1, Cancel keeps the saved style, back to Direct places at the finger again |

**Shockolate core suites** (from `7502370`, extracted read-only with `git show`; Shockolate's working tree untouched),
built against this directory's core: `touch_controls` (10 groups), `alpha11`, `alpha2`, `alpha3` (persistence,
migration, editor, bounds), `alpha4` (settings v3, page 2), `ammo`: **all PASS**, unchanged expectations.

## 5. Device acceptance (S26 Ultra)

Driver `relative-cursor/cursor_dev.py`. `android/tools/uhid_input -t` makes Android see an external HID multitouch
touchscreen, so contacts go Android input → `TouchSurface` → `touch_surface.c` → the bridge exactly as a finger does.
Each run used a **disposable write folder** (copies of `cam.cfg` with the resolution, `user.cfg`, `user.bnd`, `save_0`,
`save_7`); the real profile was never used. `SS2PORT_TOUCH_DEBUG=1` logged every finger, placement and button
(`relative-cursor/touch-log-*.txt`). APK `0.8.3-cursor-legacytest-play`, `59d5f447…`.

| Check | 1280×720, interface 640×480 (1.5×) | 800×600 | 1024×768 |
| --- | --- | --- | --- |
| Direct style unchanged | Tap on Load Game opens the load screen (before switching); after switching back: tap places at the finger, inventory drag presses at the start point and carries the item (`direct-mode-drag-regression.jpg`) | — | — |
| Switch style in the panel | SET → MORE → CURSOR: label changes; SAVE writes `cursor_relative=1` (`settings-page2-*.jpg`) | (loaded) | (loaded) |
| Persists across restarts and reinstall | `settings … (loaded); cursor relative` after a new process and after `adb install -r` | yes | yes |
| No jump on touch | First touch after start: the cursor moves from the game's 0,0, not to the finger | from 0,0 | from 0,0 |
| Across the display | 450 px swipe = 400 window px = 450 screen px on the 4:3 menu | — | — |
| Fine movement | 1 px finger steps → 1 window px steps (990 → 986) | — | — |
| Main menu selection | Hover Load Game (highlighted), tap anywhere → load screen, cursor unmoved (`menu-hover-then-tap.jpg`) | same | same |
| Load screen | Select Hydroponics, hover Load, tap → the level loads | same | same |
| Inventory drag (hold) | Healing gland moved two slots (`inventory-hold-drag-720p.jpg`) | same (`…-800x600.jpg`) | same (`…-1024x768.jpg`) |
| Inventory → equipment | Pistol dragged onto the weapon slot; the game swapped the wrench onto the cursor (`inventory-to-equip-720p.jpg`) | — | — |
| HUD, PDA | MAP opens; PDA opens; an e-mail opens; LOGS tab (`map-opened.jpg`, `pda-*.jpg`) | — | — |
| Save screen | Esc → Save Game; a slot selected (name field filled in); Done leaves without saving; no save created (`save-dialog-select-then-done.jpg`) | — | — |
| Back to LOOK | MODE → LOOK; a swipe turns the view, **0 cursor placements** (`look-mode-turn.jpg`) | — | — |
| Two fingers | Cursor finger moving + USE with a second finger: right click, no cursor movement; the cursor finger continues | — | — |
| Home during a held drag | `button 0 up` at once; the stale finger's lift in the background did nothing; icon → same process, next gesture continues from the cursor | — | — |
| USB mouse | A second uhid device (USB mouse) moved the cursor 578,360 → 848,495; the next touch continued from 848,495 (`usb-mouse-then-touch.jpg`) | — | — |
| Speed | 2.00 saved; 20 px steps = 35–36 window px; back to 1.00 | — | — |
| Rapid taps | 5 taps in ~0.45 s: 5 clicks, alternating, all at 222,360 | — | — |
| Stuck buttons | none (29 presses, 29 releases in the logs) | none | none |

Harness artifact, not a defect: one scripted swipe asked uhid for x = −10 (off the panel), which the tool wrapped into
a large jump; the bridge clamped the cursor at the edge, and the following click dropped a held item into the world
of the disposable save. The driver now keeps swipes on the screen. A real panel cannot report negative coordinates.

Seen during testing, by design: a finger that rests still for 0.5 s before moving presses the button (the hold). It
happened once in the first run because the test paused for a screenshot.

## 6. Settings and data

- The legacy app had no `touch-settings.cfg` before (defaults). The tests created one; it was **removed at the end**,
  so the app is back to Direct with the default layout. The Beta 8 app's settings were not touched (it was not
  reinstalled).
- `cursor_relative` is off unless the player turns it on; existing files keep every other setting.
- The real legacy profile (`files/profiles/scp4`) was not used by these runs: 176 of 176 files unchanged since the
  audio milestone, including the 161 save files.

## 7. Remaining limitations

- The 0.5 s hold and the 0.35 s tap limit are fixed (as Android's own long press); a slow, still touch between the two
  does nothing, as in the direct style.
- No acceleration curve; speed is linear (0.25–4).
- The relative cursor stops at the picture's edge; it does not enter the letterbox bars (the game has no cursor there).
- Level transitions were not repeated in this phase.

## 8. Checklist for your hands (about 10 minutes)

1. SET → MORE → tap CURSOR until it says RELATIVE - TRACKPAD → SAVE / DONE.
2. Main menu: slide a finger anywhere; the arrow moves from where it was. Tap anywhere: it clicks under the arrow.
3. Is 1.00 comfortable? Try CURSOR SPEED 1.50 and 2.00 for crossing the screen, 0.75 for small targets.
4. In game, MODE → CURSOR. Put the arrow on an item, hold still about half a second, then drag it to another slot and to
   an equipment slot; lift to drop.
5. Open the PDA and read an e-mail; open the save screen and pick a slot (Done to leave).
6. MODE → LOOK: looking around must not move or click the cursor.
7. While dragging an item, press Home; come back: nothing stays held.
8. Switch back to DIRECT if you prefer; everything behaves as before.
