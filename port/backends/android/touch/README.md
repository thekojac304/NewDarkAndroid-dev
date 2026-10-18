# Touch controls (`backends/android/touch/`)

The Shockolate touchscreen controls (System Shock, Halo-derived: floating stick, swipe look, FIRE with drag-to-aim,
USE, action buttons, a MODE toggle between look and cursor, a settings panel with a layout editor), driving System
Shock 2 through the portable host. **GPL-3.0-or-later** (as in Shockolate). The rest of NewDarkRecompiled is MIT, so an
APK that contains this directory is GPL as a whole. Personal builds are not distributed.

## Where the code comes from

Imported from `C:\Dev\system-shock-android`, branch `touch-controls`, commit
`750237049b2e9906f5e3345ff05522558b9a9f47` (`AndroidProject/app/jni/touch/`, the accepted baseline of the Phase 0
audit). It was taken with `git archive`; the working tree's uncommitted changes were not used.

| File | From 7502370 | Change |
| --- | --- | --- |
| `touch_controls.c/.h` | unchanged, except two lines and three settings actions | `in_circle` ignores radius-0 controls (hidden in the SS2 layout); KBD (the `target` action) takes a touch in cursor mode too, like AMMO (Phase 2B-2); `TU_CURSOR_STYLE`, `TU_CURSOR_SPEED_DOWN/UP` (Phase 3D) |
| `touch_radial.c/.h` | unchanged | |
| `touch_editor.c` | the three cursor actions; the settings panel's second page has the cursor rows first and its five rows a little tighter (Phase 3D) | |
| `touch_settings.c/.h` | defaults table, plus one function; `cursor_relative` and `cursor_speed` (Phase 3D) | SS2 labels: CONS → RELOAD, target → KBD; explosive and sprint hidden. Phase 2B-2 defaults: SET, AMMO/USE and KBD in the letterbox bars, MODE right of the EQUIP panel. `touch_settings_upgrade_layout` gives a stored layout that still has every control at the Phase 2A/2B-1 positions the new defaults (in memory). Keys and schema (v3) kept. Phase 3D adds two keys to the same version: `cursor_relative` (0 Direct, the default; 1 Relative) and `cursor_speed` (0.25-4, default 1). A file without them loads as Direct at speed 1, and an older build ignores them |
| `touch_surface.c/.h` | JNI name only | `Java_local_newdarkandroid_ss2_TouchSurface_onNativeSurfaceTouch` |
| `touch_render_gles.c` | from `touch_render.c` | the same drawing code, colours, layout and font; `SDL_Renderer` calls replaced by one batched GLES 2 triangle list; QA and visual-compare panels left out; the cursor style and speed labels (Phase 3D) |
| `android/.../TouchSurface.java` | from the `SDLSurface.java` patch | as a subclass (`NewDarkActivity.createSDLSurface`), so SDL's Java sources stay stock |
| `touch_ss2.c/.h` | new | the SS2 bridge, replacing `touch_shock_bridge.c` (which is System Shock engine code) |
| `touch_ss2_android.c` | new | SDL events, lifecycle, settings file, haptics (`NewDarkActivity.touchHaptic`, as `ShockolateActivity`'s), drawing |

## How it fits

```
Android touchscreen -> TouchSurface (raw surface pixels) -> touch_surface.c (SDL user event)
  -> touch_controls.c (fingers, roles, stick, look, fire/use, cursor, MODE)  [Shockolate core]
  -> touch_ss2.c (edges -> PlatEvents: keys, mouse motion, buttons, wheel, cursor placement)  [SS2 bridge]
  -> plat_video_poll's sink -> win32/user32.c + dx/dinput.c -> the recompiled game
```

- `plat_sdl2.c` (`#ifdef __ANDROID__`, weak references) calls `android_touch_init`, `android_touch_event` (each SDL
  event), `android_touch_filter` (physical keys and buttons), `android_touch_poll` (after the SDL events) and
  `android_touch_draw` (in `gl_swap`, before the swap).
- SDL's touch-to-mouse emulation is off. The launcher's `--ez touch false` turns the controls off (`SS2PORT_TOUCH=0`),
  and the game then gets SDL's touch events as before.
- Shared code: `PlatEvent.abs` (put the game's cursor at x, y even while the mouse is captured; `win32/user32.c`,
  `dx/dinput.c`), `user32_cursor_recentred_ns()` (when the game last recentred its cursor), and `d3d9_ui_rect()`
  (`dx/d3d9.c`: where the engine drew its 2D picture in the last frame; Phase 2B-1).
- **Cursor placement through the engine's 2D picture:** NewDark maps the window cursor onto its 2D picture by scaling
  the whole window. Its 640x480 menus are fitted into a wide resolution with bars (1280x720: 160,0 960x720), so a tap
  is placed through that rectangle (`ss2t_ui_rect`); in game, the HUD (scaled with `d3d_disp_scaled_2d_overlay` or not)
  covers the whole picture and nothing changes. At 4:3 resolutions the menus fill the picture too.

## SS2 mapping

The keys are the bindings of the game's stock `user.bnd`, which replaces `SHOCK.BND`. Only CROUCH follows a player's own
`user.bnd` (a 2023 SCP Beta 4 installation binds Shift to `+creepon`, C to `crouch` and Ctrl to `+crouchhold`); the other
keys are the same in every installation checked.

| Control | Sends |
| --- | --- |
| Stick | W / A / S / D held (`+walkfast`, `+moveleftfast`, `+backfast`, `+moverightfast`). Press at 30 % deflection, release at 20 %. |
| Look swipe | Relative mouse motion: 1 game pixel per game-pixel width of swipe, times Look X/Y |
| FIRE | Left button held (`+fire_weapon`). The finger also aims. |
| USE | Right click (`frob_object`) |
| JUMP / CROUCH | Space held (`+jump`) / the installation's own crouch key: the key `user.bnd` binds to `crouch` (a toggle), else to `+crouchhold` (held while the finger is down); Shift (the stock binding) when it is one of them or when nothing is bound. Read through the profile overlay (the copy the game reads and writes) each time touch enters LOOK mode; logged as `[touch] CROUCH key from ...` (`touch_binds.c`) |
| `<` / `>` | Mouse wheel down / up (`cycle_weapon -1` / `1`) |
| RELOAD / AMMO | R (`reload_gun`) / B (`cycle_ammo`). In cursor mode the AMMO button is a right click at the cursor, labelled USE. |
| II | Esc (`sim_menu`; also skips cutscenes) |
| KBD | Shows or hides Android's keyboard (SDL text input), in look and cursor mode. What it types reaches the game as key presses and characters, as from a physical keyboard: save names, the console (`:`), Backspace, Enter. Android's Back closes it; Esc is II. While it is up in look mode, only II, KBD, MODE and SET take a new touch, so typing cannot walk, turn or fire. |
| MODE (LOOK / CURSOR) | Tab (`toggle_mouse`): the game switches between shoot and interface mode |
| Cursor mode, **Direct** (default) | A tap places the game's cursor and clicks. A drag presses at the start point, moves and releases. |
| Cursor mode, **Relative** (trackpad; SET, MORE, CURSOR) | The touch moves nothing. A drag past the slop moves the cursor from where it is by the finger's motion times CURSOR SPEED (1 = as far on screen as the finger, also on 4:3 menus), with no button. A tap clicks where the cursor is. A finger held still for 0.5 s presses the left button there (a haptic if haptics are on); the drag that follows carries it, and lifting releases it. A gesture starts from the game's own cursor (`user32_cursor_pos`), so it goes on from where a mouse, the game or a direct tap left it. USE (right click) and mouse look are unchanged |

- Every press lasts at least 60 ms, so a 100 fps game sees it.
- A key or button a physical keyboard or mouse holds is never released by touch, and the other way round.
- **Mode:** the game recentres its cursor every frame in shoot mode and never in interface mode or menus. The touch mode
  follows that, needing 3 recentred frames within 250 ms. After the player's own MODE press it waits 600 ms, then
  follows the game again.

## Tests

`tests/run.sh` builds `tests/touch_ss2_test.c` with the bridge and the core and runs it on the PC, for example with
`CC="python -m ziglang cc"`. It checks press/release order and minimum hold, several fingers, stick ownership and
hysteresis, a modifier held across frames, relative look scaling, cursor placement before a click (corners, centre,
bars), placement through a 4:3 menu in a 16:9 game, drags, focus-loss release, mode following, weapon wheel, physical/touch aggregation, the KBD button and its typing lock-out, the default positions at 16:9, and the layout upgrade; and the relative cursor (Phase 3D): no jump on touch, delta accumulation and sub-pixel steps, speed, taps and double and rapid taps at the cursor, the hold press and its drag ordering, cancellation on focus loss and picture change, finger ownership with USE, LOOK/CURSOR changes, following the game's cursor, 4:3 menu mapping and edges, settings migration, persistence and the panel. Shockolate's own core
suites (`touch_controls`, `alpha11`, `alpha2`, `alpha3`, `alpha4`, `ammo` from 7502370) also pass against this
directory's core.
