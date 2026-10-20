# Phase 2A results: Shockolate touchscreen controls in System Shock 2

2026-10-17. Goal: play System Shock 2 on the Galaxy S26 Ultra by touch alone, reusing the accepted Shockolate
(Halo-derived) touch controls rather than writing a new control system.

**Out of scope, and not done:**
- SCP and NVScript;
- the radial menus, which are disabled: a hold on AMMO cancels and a tap still acts;
- a new layout editor (Shockolate's own is included unchanged);
- gravity and physics work.

Shockolate and the Steam installation were only read. No generated recompiled C was changed. Nothing was pushed.

## Summary

**Phase 2A's success criterion is met.** System Shock 2 runs on the S26 Ultra with visible, working Shockolate touch
controls, and every core action in the brief was performed by touch on the device:

- **Gameplay:** movement, look, fire with drag-to-aim, use/frob, item pickup, jump, crouch, weapon cycling, reload, the
  in-game menu.
- **Cursor mode:** the interface, inventory drag to unequip and equip, the PDA and email, the save screen.
- **Lifecycle:** Home/resume and a game restart.
- **Earth:** walking into the lift shaft and riding it to the top.

Most of this used **genuine multitouch** (up to three simultaneous Android pointers) from a HID touchscreen made with
the test tool. The touchscreen itself has not been tried by a person (§8).

| Area | Result |
| --- | --- |
| Source | Shockolate `touch-controls` @ `750237049b2e9906f5e3345ff05522558b9a9f47`, still HEAD; the backup bundle verifies. Uncommitted polish was not used. |
| Reuse | The core (fingers, roles, stick, look, fire/use, cursor, MODE, settings and editor) is imported: +10 changed lines across 3 files. The renderer keeps Shockolate's drawing code on GLES 2. The Java patch became a subclass. Only the engine bridge is new (§2). |
| Rendering | The overlay draws above the game picture and letterbox bars, with no corruption of the game's render state or HUD. The settings panel renders as in Shockolate. |
| Movement | Floating stick → W/A/S/D with hysteresis. Diagonals work. +11.1 units forward per 1.5 s push, as with the keyboard. |
| Look | Relative mouse motion, scaled to game pixels. Smooth, no jumps; works after resume and after mode switches. |
| Fire, use, jump, crouch, weapons, reload, menu | All verified in game (§4) |
| Cursor mode | MODE sends Tab (`toggle_mouse`). The touch mode follows the game's own shoot/interface state, detected from its cursor recentring. Taps place the game's own cursor exactly (corners, centre) and click; drags drag. |
| Multitouch reliability | Move+look+fire, move+jump, rapid taps (8/8 balanced), fire finger across other regions, MODE with two fingers down, Home while holding: no stuck keys or buttons (§5) |
| Keyboard and mouse | Physical keyboard and mouse work alongside touch, and neither can release the other's held key or button |
| Automated tests | New bridge suite 76/76. Shockolate's 6 accepted core suites pass against our core. Conformance unchanged. The Phase 1E lift trace still matches (§6). |
| APK | `0.4.0-phase2a` (versionCode 4), sha256 `1f6f10927f6f2d9a2f0ef662b9e67bbf71395cd102fbd1264df0e1e060e6d1e2`, installed on the S26 Ultra |

## 1. Source revision and preservation

| | |
| --- | --- |
| NewDarkAndroid | `android-port`, clean at the start, parent `6e22ea9` (Phase 1E). The Phase 2A commit adds this file. |
| Phase 1E APK preserved | `build/android/baseline/phase1e/app-debug-phase1e.apk`, `504aeeb7…b5d35dd0` (unchanged) |
| Phase 2A APK | `build/android/baseline/phase2a/app-debug-phase2a.apk`, 117,870,220 bytes, `1f6f1092…60e6d1e2`, with `libss2game.so` (`a66775c1…61cef2a9`) and the test tool `uhid_input` (`963a7b38…d20c8e`) |
| Shockolate | `<Shockolate Android checkout>`, branch `touch-controls`, HEAD `7502370` (2026-10-04, "DECKLIGHT-CLOSE2"), the same as the Phase 0 audit's accepted baseline. No newer commits exist on any branch. 64 uncommitted working-tree changes (the AUDIO5 / MOBILE-POLISH1 batch, "hardware retest pending") were **not used**. |
| Shockolate untouched | It was read only with `git show` and `git archive 7502370`. HEAD is the same, and no file in its tree is newer than the start of this phase. |
| Backup | `<local path>`: `git bundle verify` OK ("complete history"), head `touch-controls` = `7502370`. All four files match `BACKUP-SHA256.txt`. |
| Steam install | Read only: no file newer than the start of the phase |

## 2. What was reused, adapted and written

`port/backends/android/touch/README.md` has the per-file table. In short:

| Piece | Status |
| --- | --- |
| `touch_controls.c/.h` (core: ownership in 16 slots, roles fixed at finger-down, floating stick, look accumulation, fire/use ref-counting, cursor tap/drag semantics, MODE toggle, aggregation helpers) | **Imported.** One added line: radius-0 controls cannot be hit. |
| `touch_editor.c` (layout, hit tests, settings panel, drag editor), `touch_radial.c/.h`, `touch_settings.h` | **Imported unchanged** |
| `touch_settings.c` (load, validate, migrate, atomic save; schema v3) | Imported. Only the defaults table changed (labels; 3 controls hidden; AMMO moved into the left bar). |
| `touch_surface.c/.h` (raw Android touches → SDL user event) | Imported. Only the JNI name changed. |
| `touch_render.c` (SDL_Renderer) → `touch_render_gles.c` | **Adapted.** The same drawing functions, colours, font and layout, on one batched GLES 2 triangle list; the GL state it changes is put back. |
| `SDLSurface.java` patch → `TouchSurface.java` | **Adapted** into an `SDLSurface` subclass, so SDL's Java stays stock. Mouse-source events still go to SDL. |
| `ShockolateActivity.touchHaptic` | Copied into `NewDarkActivity` (haptics are off by default) |
| `touch_shock_bridge.c` (SS1 engine internals) | **Replaced** by `touch_ss2.c` (SDL-free, unit-tested) and `touch_ss2_android.c` (SDL, JNI, GL glue) |

### Architecture

```
Android touchscreen ─ TouchSurface (raw surface px + event time) ─ touch_surface.c ─ SDL user event
  └─ touch_controls.c (Shockolate core)  ─ ordered edges ─  touch_ss2.c (SS2 bridge: timed queue, aggregation)
       └─ PlatEvents into plat_video_poll's sink ─ win32/user32.c + dx/dinput.c ─ the recompiled game
```

**Hooks in `plat_sdl2.c`** (+31 lines, all `#ifdef __ANDROID__`; weak references, so desktop and null builds are
unchanged):
- `android_touch_init` before `SDL_Init`, which turns SDL's touch-to-mouse emulation off;
- `android_touch_event` for each SDL event, plus lifecycle cancellation;
- `android_touch_filter` for physical keys and buttons;
- `android_touch_poll` after SDL's events;
- `android_touch_draw` in `gl_swap`, before the swap.

**Shared-code changes**, the two that a demonstrated limitation required:
- **`PlatEvent.abs`** (`plat.h`, `user32.c` +5 lines, `dinput.c` 1 line): puts the game's cursor at x, y even while the
  game holds the mouse captured. SS2 keeps the DirectInput mouse exclusive in its menus and interface (Phase 1D), so
  until now a touch could not place the cursor. A placed move produces no DirectInput motion.
- **`user32_cursor_recentred_ns()`** (`user32.c` +4 lines): the time of the game's last `SetCursorPos`. A trace on the
  Windows desktop build showed **357 `SetCursorPos` calls in shoot mode and none in interface mode**. This is the
  game's own, simple and reliable mode signal.

The input script also gained `place X Y` for testing.

## 3. Default touch mapping (SS2 bindings checked in the game's `user.bnd`)

| Control (position) | Sends | Binding |
| --- | --- | --- |
| Stick: floating, left 45 % | W / A / S / D held; press at 30 % deflection, release at 20 % | `+walkfast`, `+moveleftfast`, `+backfast`, `+moverightfast` |
| Look: right side, swipe | Relative mouse dx/dy, 1 game px per game-px width of swipe × Look X/Y | mouse look |
| FIRE (right bar) | Left button held; the finger also aims | `mouse1 +fire_weapon` |
| USE | Right click | `mouse2 frob_object` |
| JUMP / CROUCH | Space held / Shift | `+jump` / `crouch` (a toggle) |
| `<` / `>` | Wheel down / up | `mouse_wheel_down/up cycle_weapon ∓1` |
| RELOAD | R | `reload_gun` |
| AMMO (left bar) | B (gameplay); right click at the cursor (cursor mode, labelled USE) | `cycle_ammo` |
| II | Esc (menu; also skips cutscenes) | `sim_menu` |
| SET | Shockolate settings / layout editor (`files/touch-settings.cfg`) | |
| MODE (LOOK / CURSOR) | Tab | `toggle_mouse` |

- Every press is held at least 60 ms, so the 100 fps game sees it.
- A physical keyboard or mouse and touch are aggregated per key and button.

**A binding finding:** SS2's `user.bnd` replaces `SHOCK.BND`, which binds `y` / Shift+`y` to weapon cycling. A physical
`y` does nothing in game. Weapon cycling is on the mouse wheel. Using the wheel also avoids a Shift chord, which would
have toggled crouch (`bind shift crouch`).

## 4. On-device results (S26 Ultra, MedSci and Earth saves)

**How these were run.** Taps and multitouch came from the test tool's new HID touchscreen mode
(`android/tools/uhid_input.c -t`). Android classifies it as `TOUCH | TOUCH_MT | EXTERNAL`; its contacts arrive as real
pointers through `TouchSurface`, for example "finger 23/0, 23/1, 23/2" together. A HID keyboard bound `k` to
`log_player_pos` for position numbers, which also tested keyboard + touch.

| Test | Result | Evidence |
| --- | --- | --- |
| Overlay renders | ✓ | Over the intro (cursor set), and over MedSci with the full gameplay set; the HUD is intact |
| Skip the intro, main menu, Load Game by touch | ✓ | II → Esc; taps placed the cursor at (610,157), (360,98), (717,240) and clicked |
| Mode follows the game | ✓ | CURSOR in menus and loading; LOOK when MedSci starts; no flicker on single recentrings |
| Walk forward | ✓ | `W` down/up; position 88.21 → 99.32 |
| Look | ✓ | Swipes pitch and yaw the view (screenshots) |
| Pick up an item | ✓ | Crosshair hint "A very solid wrench.", touch USE → "Wrench picked up." |
| Equip and swing the wrench | ✓ | `>` (wheel) → wrench in hand. FIRE held during move + look → swing caught mid-frame. |
| Move + look + fire (3 fingers) | ✓ | `W` + `A` held (diagonal), fire down/up, then both keys up; moved +11.3 / +8.3 |
| Jump while moving | ✓ | z −11.99 → −9.62 mid-jump → −12.00, forward motion kept |
| Crouch | ✓ | Shift edges; the eye height drops and comes back on the second press (screenshots) |
| MODE → interface | ✓ | Tab; the game shows inventory, equipment and status bar; the overlay switches to the cursor set |
| PDA and email | ✓ | Tap at (561,570) → "A personal data assistant."; PDA opens; tap → the POLITO email opens |
| Inventory drag | ✓ (after a fix, §7) | Equipment → inventory slot unequips; inventory → equipment: "Wrench equipped." |
| Cursor corners and centre | ✓ exact | (0,0), (799,0), (0,599), (799,599), (400,300) from the screen corners and centre of the 1440×1080 picture at x = 450 |
| Back to gameplay without a camera jump | ✓ | The same view in interface mode and 0.5 s / 2 s after MODE (screenshots) |
| In-game menu and save screen | ✓ | II → menu (CURSOR automatically) → Save Game → select slot → Done → Continue → LOOK automatically. No save was written. |
| Earth lift by touch | ✓ | Stick out, strafe, back into the x = −24 shaft: z 5.5 → 14.7 → **52.8** |
| Settings saved and reloaded | ✓ | Opacity 90 %, Look X 1.1 → `touch-settings.cfg` written atomically → "(loaded)" after a game restart. Reset to defaults afterwards. |
| Physical keyboard / mouse with touch on | ✓ | HID typing (console), HID `w`/`a`, HID mouse look after resume, HID mouse homing + click on New Game |
| `--ez touch false` | ✓ | No overlay; touches reach SDL as finger + emulated mouse, as in Phase 1D |

## 5. Multitouch and lifecycle reliability (device, from the touch debug log)

| Test | Result |
| --- | --- |
| Rapid presses: 8 RELOAD taps of 25 ms, 40 ms apart | 8 downs, 8 ups |
| Finger crossing regions: FIRE dragged into the stick area | One fire down, one up at lift; the finger never became the stick |
| MODE pressed while the stick and look fingers are down | `W` up immediately, Tab sent; the stale fingers' lifts send nothing |
| Home while holding the stick + FIRE | `W` up and button up at focus loss. Same process on return; LOOK again; the stick moves the player. |
| Temporary focus loss, resize, orientation | Cancel and release everything (code path above; unit-tested) |
| Game restart | Overlay, settings and controls come back |
| Stuck keys or buttons, dropped releases, camera jumps, unintended clicks | None seen. Every down in the logs has its up. |
| Pointer capture | Look is relative mouse with capture as before; touch never enters SDL's relative accumulator (emulation off) |

## 6. Automated tests

| Suite | Result |
| --- | --- |
| **New: `port/backends/android/touch/tests/touch_ss2_test.c`** (bridge + core, PC) | **76/76**, covering: press/release order and 60 ms minimum hold; move + look + fire with independent release; stick ownership and hysteresis; Shift held by CROUCH across frames with a key pressed in between; physical Shift cannot release it; look scaling (144 surface px = 80 game px); cursor placed before the click at corners, centre and bars; a new tap waits for the previous release; a drag's press reaches the game before the cursor moves; focus-loss release; mode follow (a single recentring is ignored, MODE waits, then follows); wheel weapon cycling; physical/touch aggregation |
| Shockolate core suites from 7502370 (`touch_controls`, `alpha11`, `alpha2`, `alpha3`, `alpha4`, `ammo`) against **our** core | All pass, with output identical to the original core's |
| Conformance, Android `adb` | 0/511 (default), 0/511 with 0 ABI violations (`--cache-regs --abi-check`), `--write-dir` 0/113 |
| Conformance, native Windows | 0/511 and 0/113, both lift modes |
| In-app `all` | 0/511 twice |
| Phase 1E scripted lift trace (shared `user32`/`dinput` changed) | Windows z max 53.54, Android 53.55 (Phase 1E: 53.53 / 53.55) |
| Compile warnings | New files clean with `-Wall -Wextra`. The touch tool is clean too. |

## 7. Defects found during integration and fixed

1. **Mode flicker:** one recentring on a menu change briefly selected LOOK. Shoot mode now needs 3 recentred frames
   within 250 ms.
2. **AMMO over the game's UI** in cursor mode on this 19.5:9 screen: moved into the left letterbox bar.
3. **Weapon cycling dead:** `y` is unbound in the effective `user.bnd`. It now uses the wheel.
4. **Inventory drag picked up nothing:** the press and the first move reached the game in the same frame, so the cursor
   had already left the item. Cursor moves now wait one minimum hold after a drag's press. This is unit-tested.
5. **`--ez touch false`** would still have disabled SDL's touch emulation. The native side is now gated by
   `SS2PORT_TOUCH=0`.

## 8. Known limitations and usability issues

- **Fixed after Phase 2A:** the **Game: SDL2 window** button started the game with `--frames 3600`, so play ended
  after about 40 s. The windowed game now has no default limit ([PHASE2A_FRAME_LIMIT_FIX.md](PHASE2A_FRAME_LIMIT_FIX.md)).
- **Not tested by a person.** Every on-device check used the HID test touchscreen. That is genuine Android multitouch,
  but not a finger. Comfort, sensitivity and button reach need a human pass (checklist below).
- **Digital movement:** the stick drives W/A/S/D (always full speed). Shockolate's stick is analog; the host has no
  analog path (TOUCH_CONTROLS.md §6).
- **No use-item right click on inventory items:** the game's `user.bnd` has no interface-mode `frob_object_inv`
  binding. Equipping and using items works by drag to the equipment or player panel, which is the game's own way.
- **Radials off:** a hold on AMMO cancels instead of opening a wheel.
- **No text entry button yet.** Save names need a keyboard; the game's default names work.
- **Edge gestures:** Android's back gesture can start from a screen edge; Back is trapped (Phase 1D), and a cancelled
  gesture releases everything. System gesture exclusion zones are not set.
- **Safe areas:** the defaults sit in the letterbox bars and edges and were not checked against the camera cutout on
  other devices.
- The test tool's touchscreen needs `rotate 90` on this phone (Android maps external touchscreens in portrait).

## 9. Manual acceptance checklist (about 10 minutes, the S26 Ultra)

1. Install: `adb install -r build/android/baseline/phase2a/app-debug-phase2a.apk`. Start the game from the launcher's
   **Game: SDL2 window** button, or
   `adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es autorun game --ei frames 0`.
2. Tap **II** to skip the intro. Tap **Load Game** → **medsci** → **Load**.
3. With one thumb on the left, walk forward, back and sideways. With the other, swipe to look. Does it feel responsive?
   Note any accidental view jumps.
4. Hold **FIRE** and drag it to aim while walking. Tap **USE** on a door or item. Try **JUMP** and **CROUCH** (a toggle)
   while moving.
5. Tap **>** / **<** to cycle weapons, and **RELOAD**.
6. Tap the **LOOK** box (it becomes **CURSOR**): the inventory opens. Tap the PDA icon (bottom right), open an email,
   drag an item between inventory and equipment. Tap **CURSOR** again: back to looking, without a jump.
7. **II** → menu → **Save Game** → **Done** → **Continue**.
8. Press Home mid-move, return: nothing should be stuck held.
9. **SET**: try Look X/Y and opacity, then **SAVE / DONE**.
10. Report: stick size and position, look speed, button sizes, anything mis-tapped.

## 10. Build, install, launch

```sh
python android/build_native.py --no-rebuild-guest --game "<SS2>/SS2.exe" --game-only --game-backend sdl2   # libss2game.so with touch
cd android && ./gradlew assembleDebug                                                                    # app-debug.apk
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n local.newdarkandroid.ss2/.LauncherActivity --es autorun game --ei frames 0          # touch on (default)
#   --ez touch false: no touch controls;  --es env SS2PORT_TOUCH_DEBUG=1: log fingers and the events they send
sh port/backends/android/touch/tests/run.sh                                                              # bridge tests (CC=...)
```

## 11. Recommended Phase 2B scope

1. **A human usability pass** with the checklist, then tune the defaults: stick size, look speed, button positions for
   19.5:9, a FIRE/USE swap option.
2. **Text entry button** (`SDL_StartTextInput`) for save names and console commands.
3. **Analog movement**, if wanted: a DirectInput joystick device in `dx/dinput.c`, once it is shown that NewDark 2.48
   maps joystick axes to movement. Otherwise keep the digital stick.
4. **Radial wheels:** fixed key wheels (weapon slots 1-0, psi, quick-use F1-F12), using Shockolate's selector.
5. **System gesture exclusion** for the stick and FIRE areas, and a check against the cutout insets.
6. **Interface right click:** an Android bind file in the write folder (`frob_object_inv` in interface mode) if item use
   by right click is wanted, with the owner's consent, since it changes binds.
7. Upstream proposals: `PlatEvent.abs` and the recentre signal as small, documented host features.
