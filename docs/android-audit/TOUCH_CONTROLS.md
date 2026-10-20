# Reusing the Shockolate touch controls for System Shock 2

Phase 0 audit, 2026-10-17. All inspection was read-only. Nothing in `<Shockolate Android checkout>` was modified, built
or run. Paths without a prefix are relative to `<Shockolate Android checkout>`. Paths prefixed `ndr:` are relative to
`<repo>`.

## 1. Which implementation is the accepted baseline

**Use the committed HEAD of the local `touch-controls` branch (`7502370`) of `<Shockolate Android checkout>`.**

| Candidate | Verdict | Evidence |
| --- | --- | --- |
| `touch-controls` HEAD, files under `AndroidProject/app/jni/touch/` | **Accepted baseline** | Alpha 3 was validated by the user on hardware (`docs/TOUCH_ALPHA_3.md:4`, `:172`). For Alpha 4: "Hardware validation is complete… no known Alpha 4 regression" (`docs/TOUCH_ALPHA_4.md:35`). Menu taps, the AMMO radial and the QA loadout have "user-reported hardware acceptance" (`docs/QA2_COORDINATED_QA.txt:5`, `:13-18`), commit `fa57a8b`. The last commit touching the touch dir is `9a40568` (2026-10-04). |
| Uncommitted working-tree changes in the touch dir (+451/−23, plus untracked `touch_assist.h` and two tests) | **Not accepted; do not import by default** | This is the AUDIO5 + MOBILE-POLISH1 batch: "hardware retest pending" (`docs/AUDIO5_MOBILE_POLISH1.md:3`), and its device checklist is unchecked. It adds an audio settings page, tap "assist" snapping, a hitbox overlay and radial polish. |
| Alpha 1.1 / 2.x | Superseded | These versions evolved in place in the same files. HEAD contains their final form. |
| `<local path>` (detached `0d71a89`, v1.0.0) | Not a touch baseline | It has no `jni/touch` directory and is controller-focused. Its docs still describe touch as "an optional pointer" (`docs/CONTROLS.md`). |
| `<local path>` | Design ancestor only | Its touch layer is a JavaScript DOM overlay (`port/web/site/input.js`, `buildTouchControls` ≈ line 388) that drives a virtual gamepad for a WASM build. It shares no C code with Shockolate. Design traits match, e.g. the 45% left stick zone (`input.js:486` vs `touch_editor.c:53`). |
| QA-only modules (`touch_visual_compare.h`, `qa_tools.c/h`, `qa_spawn.h`) | Exclude | SS1 horror-visual and teleport QA tooling. |

**Risk:** the 38 commits that hold the accepted touch work exist **only locally**. `origin/touch-controls` is still
at `0d71a89`. Import with `git show 7502370:<path>` (or `git archive 7502370 AndroidProject/app/jni/touch`), never
from the working tree. A backup or push of that branch is the owner's decision and was not done in this audit.

## 2. Source inventory (HEAD `7502370`)

Touch directory, excluding tests: 2,656 lines at HEAD.

| File | Lines (HEAD) | Kind | Dependencies |
| --- | --- | --- | --- |
| `touch_controls.c` | 415 | Game-agnostic core: finger ownership, roles, floating stick, look, fire/use, mode toggle, radial hold timer, movie-skip tap, per-source key/button aggregation | `math.h`, `string.h` |
| `touch_controls.h` | 147 | State, `TouchOutput` edge-callback API (`TouchEmit`) | `touch_settings.h`, `touch_radial.h`. Enums are SS1-flavoured: `TouchAction` (CONSUMABLE, EXPLOSIVE, TARGET_ID, AMMO), the QA deck |
| `touch_editor.c` | 238 | Layout geometry from settings, hit tests, settings panel and drag layout editor | core headers |
| `touch_radial.c/.h` | 57 / 39 | Generic wheel (hold 320 ms, ≤7 entries, angle selection) | none. `TOUCH_AMMO_*` macros are SS1-specific |
| `touch_settings.c/.h` | 130 / 38 | `touch-settings.cfg` schema v3: load, validate, migrate, atomic save | POSIX `fsync`/`rename`. The `TouchControlId` enum and `touch_defaults[]` (c:15-29) are SS1 control names |
| `touch_target_types.h` | 11 | 4-value target enum | none |
| `touch_render.c` | 311 | Whole overlay: vector shapes plus a built-in 3×5 bitmap font | **SDL_Renderer 2D API only** (27 `SDL_Render*` calls), in output-pixel coordinates |
| `touch_surface.c/.h` | 55 / 18 | Raw Android touches → registered SDL user event | JNI symbol `Java_org_libsdl_app_SDLSurface_onNativeSurfaceTouch` (c:48-54) plus patched `SDLSurface.java:196-275` |
| `touch_shock_bridge.c/.h` | 562 / 35 | **SS1-specific**: game-state detection, mode switching, lifecycle, viewport, haptics JNI | `player.h`, `physics.h`, `mainloop.h`, `mouselook.h`, `input.h`, many Shockolate globals (`touch_shock_bridge.c:36-40`) |
| `touch_cursor_target.h` | 34 | SS1 UI region classifier | `region.h` (`LGRegion`) |

The engine-side adapters live outside the touch dir and are all SS1. `SourceSnapshot/src/Libraries/INPUT/Source/sdl_events.c:420-595`
holds `touch_input_*` over Shock's mouse/key queues. `GameSrc/input.c:132-246` is the radial snapshot/select, and
`input.c:3548` is `touch_cursor_target_at`. `GameSrc/invent.c:774` is the weapon slot.

Java and build pieces that matter for reuse:

- `ShockolateActivity extends SDLActivity` has `touchHaptic(II)V` (`:51-62`), landscape lock and immersive/cutout
  policy.
- `LauncherActivity` (398 lines) is the SAF importer. It uses `ACTION_OPEN_DOCUMENT_TREE`, does a recursive
  `DocumentsContract` copy into `filesDir/res.importing` with path-traversal guards and fsync, and swaps atomically to
  `filesDir/res` with a backup.
- Build: SDL 2.32.10 built from source, vendored and patched SDL Java sources, NDK `29.0.14206865`, CMake 3.22.1,
  compileSdk 34, minSdk/targetSdk 33, arm64-v8a only (`AndroidProject/app/build.gradle`, `docs/BUILD.md:36`).

## 3. How the accepted controls work (summary)

- **Event source:** the patched `SDLSurface.java` routes touchscreen `MotionEvent`s to `onNativeSurfaceTouch`.
  `touch_surface.c` pushes them as a custom SDL event. SDL's touch↔mouse synthesis is disabled
  (`touch_shock_bridge.c:355-356`), and `SDL_FINGER*` events are ignored. App, window and display lifecycle events
  cancel all touch state (`:589-614`).
- **Multitouch/ownership:** 16 slots keyed by `(touchId, fingerId)`. The role is fixed at touch-down and stray
  move/up events never claim a role. Move, look and cursor each have a single owner. Fire and use are ref-counted
  (`touch_controls.c:184-260`).
- **Movement:** a floating stick in the left 45% (`touch_editor.c:53`), normalised and clamped
  (`touch_controls.c:119-125`). Shockolate writes it as **analog** velocity into the physics control bank
  (`touch_shock_bridge.c:621-640`).
- **Look:** right-side drag, plus drag-while-firing. Float deltas are scaled by sensitivity X/Y and invert-Y, then
  consumed as integer relative mouse motion (`touch_controls.c:113-117`, `:347-350`).
- **Fire/use/actions:** mouse buttons and keys go through per-source aggregation, so touch never releases a physical
  press. Other actions dispatch through the controller action table.
- **Radials:** hold 320 ms → wheel. Release selects; a tap does the plain action (`touch_controls.c:128-168`, `:317-322`).
- **Cursor mode:** a manual MODE toggle, plus automatic CURSOR when a panel or menu is open, gameplay is unavailable
  or a controller is connected (`touch_controls.c:10-12`, `touch_shock_bridge.c:144-149`, `:482-485`). Drag and tap
  semantics are at `touch_controls.c:274-290`.
- **Customisation:** a native overlay panel provides layout editor, scale 0.6-1.8, opacity 0.25-1, look X/Y
  0.25-3, invert Y, stick size, haptics and wheel toggles. It persists to `touch-settings.cfg` in
  `SDL_AndroidGetInternalStoragePath()` (`touch_shock_bridge.c:84-97`).
- **Controller coexistence:** connecting a controller cancels and hides touch (`sdl_events.c:716`, `:1384`, `:1398`;
  `touch_render.c` returns early).
- **Rendering:** SDL_Renderer lines and rects over the letterboxed 4:3 SDL texture, after `SDL_RenderCopy` and before
  `SDL_RenderPresent` (`Shock.c:639`).

## 4. The integration target in NewDarkRecompiled

The prescribed architecture maps onto the portable host directly:

```
touch core (imported) → SS2 input bridge (new) → PlatEvent stream from plat_video_poll → win32/user32.c + dx/dinput.c → recompiled game
```

- The only input interface is `plat_video_poll(sink, user)`, which emits `PlatEvent`s: `PLAT_EV_KEY` with DirectInput
  scan codes, `MOUSE_MOVE` (absolute x/y plus relative dx/dy), `MOUSE_BUTTON`, `MOUSE_WHEEL`, `TEXT`, `FOCUS`, `QUIT`
  (`ndr:port/include/plat.h:94-132`).
- `win32/user32.c:193-231` turns them into window messages and DirectInput state. Nothing in the recompiled engine
  needs to change.
- **Feedback from the game to the backend** is limited to:
  - `plat_video_mouse_mode(relative, visible)`, which the host calls when the game captures or releases the pointer
    (`user32.c:333-353`, driven by DirectInput acquisition, `ShowCursor`, `ClipCursor` and `SetCursor`);
  - `plat_video_warp_mouse(x, y)`, called only when not captured (`user32.c:324-331`).
- **No joystick is offered** (`ndr:port/dx/dinput.c:4`, `winmm.c:129-133`). Analog movement is therefore not available
  through the current interface; see §6.
- The existing Vita backend is the closest precedent (`ndr:port/backends/vita/plat_vita.c:177-247`):
  - left stick → W/A/S/D with hysteresis;
  - right stick → relative mouse;
  - buttons → DIK keys or mouse buttons;
  - front touch → absolute cursor plus left click.

## 5. Reuse plan

### Import unchanged (from `7502370`)

- `touch_radial.c/.h` (optionally drop the `TOUCH_AMMO_*` macros).
- `touch_target_types.h`.
- `touch_settings.c` (the load/validate/migrate/atomic-save engine).
- `touch_surface.c/.h` plus the `SDLSurface.java` patch, provided the Android project uses SDL2 with the same
  `org.libsdl.app` package. The NewDark port should pin SDL 2.32.10 like the Shockolate build, so the patch applies
  verbatim.
- From `touch_controls.c`: the ownership, stick, look, fire/use, cursor drag/tap, radial hold, movie-tap and
  per-source aggregation logic. It is game-agnostic C11 and is tested headless with `-DTOUCH_HEADLESS`
  (`scripts/test-touch-linux.sh:7-8`).
- The host test suites for the above (`touch_controls_test`, `alpha11`/`alpha2`/`alpha3`/`alpha4`).
- The `LauncherActivity` SAF copy logic: traversal guards, fsync and the atomic swap.

### Adapt

| Piece | Change |
| --- | --- |
| `touch_controls.h` | Replace `TouchAction` with an SS2 set. Remove the QA, compare and audio fields. Keep the API shape. |
| `touch_settings.h` | New `TouchControlId` and `touch_defaults[]` for the SS2 layout. New schema version. Store the file under a NewDark-specific app path. |
| `touch_editor.c` | Remove the QA deck and audio panels. Keep the geometry and editor. |
| `touch_render.c` | Swap the 27 SDL_Renderer calls for a tiny GLES2 primitive layer (lines and filled quads in window pixels, with GL state save/restore), drawn in `glr_present` after the game quad and before `gl_swap()` (`ndr:port/backends/sdl2/gl_render.c:404-414`). With the render thread on, it must run on the render thread (`gl_thread.c`), so post the overlay as a deferred command. The NewDark host has no SDL_Renderer, so this rewrite is required. The drawing logic and the bitmap font stay. |
| Viewport mapping | Replace Shockolate's `SDL_RenderGetScale`/`SDL_RenderWindowToLogical` with the host's letterbox rectangle (`ndr:plat_sdl2.c:21-26` `letterbox`/`gl_present_rect`). |
| `LauncherActivity` | Import a whole SS2 folder (must contain `SS2.exe`, `Data/`, `osm/` or `OSM/`, `lgvid.dll`, `cam*.cfg`, `cam_mod.ini`, any `DMM`/mod folders) instead of `res/data` + `res/sound`. Validate by `SS2.exe` presence and size (see IMPLEMENTATION_PLAN.md §2). |
| `ShockolateActivity` | Becomes `NewDarkActivity extends SDLActivity`. Keep `touchHaptic`, landscape and immersive mode. Give the native thread a large stack (ANDROID_COMPATIBILITY.md, Main-thread stack). |

### New code (the SS2 bridge, replacing `touch_shock_bridge.c`, `sdl_events.c` adapters and `input.c` hooks)

1. **`touch_ss2_bridge.c`**: implements `TouchEmit` by producing `PlatEvent`s into the backend's `plat_video_poll`
   sink (keys as DIK codes, mouse dx/dy, buttons, wheel). It handles lifecycle cancel and controller suppression as
   Shockolate does.
2. **SS2 action table**: action → DIK key or mouse button. The upstream Vita table is a starting point: Space jump,
   C crouch, R reload, Tab shoot/interface mode, I inventory, Q/E lean, Esc menu, wheel for weapon cycling, R-trigger =
   left mouse (fire), L = right mouse (use/frob) (`ndr:plat_vita.c:15-22`, `:177-183`). The Vita comment notes that
   these must match the player's bind file. The table should be data, not code.
3. **Mode detection** (see §6) and the manual MODE toggle (kept).
4. **Radials for SS2**: the populate/select callbacks cannot read SS2's inventory, because there is no game API. The
   realistic first version offers fixed wheels of key commands (weapon slots, psi, PDA/log/map screens, quick-use).
   Item-aware wheels would need reverse-engineering of game state. **Out of scope** unless a concrete need appears.
5. **Text entry button**: an overlay button that toggles `SDL_StartTextInput`/`SDL_StopTextInput`, for save names and
   similar. SDL `TEXTINPUT` already becomes `PLAT_EV_TEXT` → `WM_CHAR` (`ndr:plat_sdl2.c:141`, `user32.c:204`).
6. **Game-controller mapping** via `SDL_GameController` (secondary requirement), modelled on the Vita table.

Estimated split by lines: about 55-65% of the accepted touch code is imported unchanged or with trivial edits (core,
radial, settings, surface, tests). About 15% needs adaptation (render, editor, enums). The bridge (≈560 lines in
Shockolate) is rewritten, and should end up smaller, since it emits events instead of calling into an engine.

## 6. SS2-specific mapping and the hard questions

| SS2 need | Plan | Status |
| --- | --- | --- |
| FPS movement | Stick → W/A/S/D key down/up with hysteresis (the Vita approach). **Feel change:** Shockolate's stick is analog (physics velocity). The NewDark host has no analog path, because DirectInput joysticks are not offered (`dinput.c:4`). Options: (a) digital first; (b) later, add a DirectInput joystick device to `dx/dinput.c` **if** NewDark 2.48 maps joystick axes to movement, which is Unknown and needs testing on Windows first. | Significant (to keep analog feel) |
| Look/aim | Look deltas → `PLAT_EV_MOUSE_MOVE` dx/dy. In captured mode user32 feeds them to DirectInput and the virtual cursor (`user32.c:205-213`). | Minor |
| Fire | Fire button → left mouse in shoot mode (SS2 convention, per the Vita table). | Minor |
| Use/frob | Use button → right mouse. | Minor |
| Reload, jump, crouch, lean, weapon switch | Keys via the action table. Weapon cycling via wheel events. | Minor |
| Inventory, PDA, logs, research, hacking | All are mouse-driven SS2 screens: interface mode plus cursor taps and drags. The Shockolate cursor-finger semantics (drag = press at start, move, release; tap = click) map directly to `MOUSE_MOVE` + `MOUSE_BUTTON`. | Minor, **but see absolute positioning** |
| Psi power selection | Psi selection is a UI interaction (or keys, per bind file): cursor mode taps or a fixed key wheel. | Minor |
| Contextual item use (drag item onto world/object) | Cursor drag. | Minor |
| Text entry | Keyboard button (new). | Minor |
| **Absolute cursor positioning** | In captured mode, user32 moves its virtual cursor only by `dx/dy` and ignores absolute `x/y` (`user32.c:207-210`). The Vita backend sends `dx = dy = 0` for touches while relative (`plat_vita.c:245`), so touch placement there only works when the game has not captured the mouse. If SS2's interface mode keeps DirectInput acquired, a tap cannot place the cursor without a small host change. That change would be an "absolute" flag on `PLAT_EV_MOUSE_MOVE` that sets the virtual cursor (`vcur_x/y`) directly, about 5 lines in `win32/user32.c`. It is shared code, so propose it upstream. | **Unknown → likely Minor shared-code change** |
| **Automatic gameplay/cursor detection** | The only game-derived signal is `plat_video_mouse_mode(relative, visible)`. Whether it differs between SS2 shoot mode and interface mode is **Unknown**. SS2 draws its own cursor and may keep DirectInput acquired in both. **Investigate in Phase 2** with the existing `SS2PORT_MOUSE_DEBUG=1` logging (capture changes, `GetCursorPos`/`SetCursorPos`; `plat_sdl2.c:86-89`, `user32.c:320-325`). If the signal is unreliable, keep the manual MODE toggle (required fallback), plus a bridge-side heuristic: pressing the Tab/inventory key flips the bridge mode. | Unknown |
| Menus / pause / cutscenes | Main menu and pause screens are cursor screens. Shockolate's "gameplay unavailable → CURSOR" rule needs an SS2 signal; the same investigation applies. Movie skip = Esc tap (reuse `TouchMovieTap`). | Unknown |

## 7. Integration risks

1. **Analog movement is lost** without new host work (above). This is the biggest feel difference from the
   Shockolate controls.
2. **Mode detection** may not be automatic. The manual toggle remains, as the brief allows.
3. **Overlay rendering must be re-implemented** on the host's GL path, and must respect the render thread. Pixel
   coordinates must use the full window including letterbox bars, as in Shockolate (`TOUCH_ALPHA_4.md:3`).
4. **Bind-file dependence:** keys only work if they match the player's SS2 binds. SCP or other mods may ship bind
   changes. Mitigation: make the action table data-driven. Optionally ship an Android `.bnd` placed in the write
   directory, which the VFS overlay reads before the game folder (`ndr:port/core/vfs.c:89-96`), leaving the game
   folder unchanged.
5. **SDL/Java coupling:** `touch_surface.c` depends on the patched `org.libsdl.app.SDLSurface`. Pin the same SDL 2.32.10.
6. **Source of truth:** the accepted commits are unpushed. A disk failure would lose them. Import from `7502370`.
