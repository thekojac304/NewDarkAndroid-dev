# Phase 3F: radial menus for weapons, ammunition, psi, items and system actions

2026-10-19. Branch `android-port`, from `4e4c482` (Phase 3E, clean). Commits: `4b6dba2` (game state and commands),
`1f5666e` (touch core hooks, settings), `00a4851` (radial menus, renderer, tests), `e75fbd3` (unread logs, a fix, test
tools), `09a1c7a` (this report, desktop results), and the device results with two renderer fixes (the commit after it).
Evidence: `phase3f/` (`desk-*` desktop, `device-*` phone).

Labels used below: **Verified (desktop)** = run against real save data in the desktop build of the same code, with the
game's own results read back and captured; **Verified (device)** = on the Galaxy S26 Ultra through the real touchscreen
path (HID touchscreen driven by a tool, not a hand); **Automated** = PC unit tests; **Needs your hands** = feel, reach and
comfort, which only a person can judge.

## Summary

| Feature | Result |
| --- | --- |
| Hold `<` / `>`: wheel of the weapons carried | **Implemented, verified (desktop)**: only carried weapons, in the game's own weapon order, current one marked, weapons the character cannot equip dimmed (the game's own check); selection runs `equip_weapon`. Taps unchanged |
| Hold AMMO: ammunition types / settings | **Implemented, verified (desktop)**: only ammunition the game says loads into the gun in hand, with counts; the gun's two settings by the game's own names (NORM/BURST, NORM/OVER, …). Ammo uses `cycle_ammo`'s own load; settings use `wpn_setting_toggle` (different actions, as required) |
| Psi powers | **Implemented, verified (desktop, disposable test character)**: hold AMMO with the psi amp equipped → the known powers; selecting never activates (psi points unchanged); FIRE then activates it |
| Hypos and patches | **Implemented, verified (desktop)**: new ITEM button; items under the game's `Patches` archetype only (no quest items), with counts; `use_obj`; the game's own rules apply (a full-health hypo is refused by the game) |
| Audio logs | **Implemented**: PLAY LOG (`play_unread_log`, "Play an unread log"), STOP LOG (`stop_email`). PLAY LOG is offered only when there are unread logs (counted exactly as the command selects them). With no unread logs: verified (desktop). Playing an actual unread log: **not exercised** (no save had one; §9) |
| Quicksave / quickload | **Implemented, verified (desktop)**: the game's `quicksave`/`quickload` (its quicksave slot 13 per `cam_ext.cfg`); quickload asks first (on by default, configurable) and is offered only when the quicksave slot exists. Quicksave → changes → quickload restored the saved state |
| Lean | **Not implemented** (§9): a held action; a release-to-select wheel cannot hold it, and a permanent button was not wanted |
| One APK, both SCP versions | Same build; Beta 4 and Beta 8 both verified on desktop (each with its own NVScript) |
| Device (S26 Ultra, HID touchscreen) | **Verified (device)**: weapon, AMMO (ammo, settings, psi), ITEM and system wheels on Beta 4 and Beta 8, at 1280×720, 800×600 and 1024×768; taps unchanged; stick + wheel; Home with a wheel open; cursor modes; settings and layout editor. Saves, game folders and your settings byte-identical afterwards (§12) |

## 1. Audit (Milestone A)

### 1.1 Shockolate radials (read-only)

The accepted `touch-controls` commit `7502370` (already imported in Phase 2A) has the whole wheel: hold timer
(320 ms), angle-to-segment selection with an inner cancel circle and a 1.25× outer limit, `touch_radial_refresh` for live
availability, rendering (`touch_render.c` `radial()`), haptics on open and select, cancellation through `touch_cancel`,
and three wheel preferences in `touch-settings.cfg`. Its content came from System Shock's own engine
(`touch_game_radial_snapshot/select` in Shockolate's `input.c`). The port had the core but `radial_populate = NULL`, so
holds did nothing. No new framework was written: Phase 3F supplies the populate/select side for System Shock 2 and
draws the same wheel with the port's GLES primitives.

### 1.2 Commands and bindings

The engine's command table (SS2.exe 2.48, `0x823628`…) has, among others: `equip_weapon <name>` ("searches your inv
for a particular weapon and equips it"), `cycle_weapon ±1`, `cycle_ammo`, `wpn_setting_toggle`, `psi_power 1-5`,
`select_psipower`, `use_obj <name>`, `quickuse/quickbind N`, `play_unread_log`, `stop_email`, `quicksave`, `quickload`.
The two installations' `user.bnd` differ (Beta 4: `v`, `keypad_minus` → setting toggle, `z` → play unread log, `x` →
lean forward; Beta 8: `x` → back), so **no Phase 3F action presses a key**: every action is the command itself, run
through the engine's command interpreter (§2), so bindings cannot break it. The existing tap actions keep their keys.

### 1.3 Readable state: what the engine exposes

Found by disassembling the commands above and NewDark's documented script services (`doc/squirrel_script/API-reference_
services.txt`, `doc/script_services.txt`), then checked at run time:

| State | How it is read | Source of the method |
| --- | --- | --- |
| Inventory | `IContainSys` iteration of the player (`+0x50/+0x60/+0x64`; iterator `+4` contain type, `+12` object) | `equip_weapon`, `use_obj`, `cycle_ammo` |
| Archetype, inheritance, names | `ITraitManager` `+0x18`, `+0x40`; `IObjectSystem` `+0x2c`, `+0x30` | same commands |
| Equipped weapon | `IShockPlayer::GetEquip(player, 0)` | `ShockGame.Equipped` |
| Can equip | `IShockPlayer` slot `+0x50` (`obj`, no message) | the check `Equip` itself makes first |
| Gun in hand | `IPlayerGun +0x20` | `ShockGame.PlayerGun` |
| Broken/unresearched | `ObjState` property | `equip_weapon` |
| Ammunition for a gun | engine `0x45a8b0` (gun, object) → projectile; `0x5c2e90` current projectile | `cycle_ammo`, `ShockGame.AmmoLoad` |
| Setting | `GunState` +8; names from object strings `SHead1/SHead2` | `wpn_setting_toggle` |
| Psi | `PsiPowerDesc`/`PsiPower2Desc` bits; `PsiState` field 0 (selected); psi points | `ShockGame.GrantPsiPower`, `SetSelectedPsiPower`, `GetPlayerPsiPoints` |
| Counts | `StackCount` | engine |
| Unread logs | each deck's log property (+4 bits) and the engine's read flags | `play_unread_log` |
| Labels | object strings `objshort`, string table `psihelp` | the HUD and psi MFD |

No offset table was guessed: every address is one the engine's own code uses for the same purpose, and is checked
before use (§2). All of it is read-only.

**Checkpoint A**: everything requested has a reliable data source except lean (held, §9).

## 2. Architecture

```
touch (look mode) --hold 400 ms--> Shockolate wheel (touch_controls/touch_radial)
   populate/select (touch_ss2.c) --services--> touch_ss2_android.c --> core/ss2game.c
                                                            reads + queued actions
   PeekMessageA from the engine's per-frame event pump (return 0x6a82ef) --> ss2g_message_pump: run them
```

- **`port/core/ss2game.c`** (MIT, game-specific, isolated). Finds the script manager from `ScriptModuleInit`
  (`modules.c` wraps that export), the documented `ShockGame` and `Debug` services through it, and the engine systems by
  QueryInterface on the application aggregate, as the engine's commands do.
- **Checked before anything is called**: 9 interface IDs byte for byte (LG GUID formula from NewDark's docs), each
  interface's vtable address, the first 8 bytes of the 5 engine functions called directly, the 14 names of the engine's
  weapon table, and that each property call's vtable slot is engine code. Any mismatch logs one line and turns the module
  off; the buttons then keep their plain actions.
- **Where it runs**: only inside `PeekMessageA` when it returns to the engine's per-frame event pump (`0x6a8210`,
  called by a frame-loop client), the stage where the engine's own key bindings run their commands, never from drawing,
  loading screens or nested pumps; not re-entrant; the CPU state is saved and restored around it. The engine never
  reads the keyboard through DirectInput (measured: only window messages and `GetAsyncKeyState`), so that is the right
  point.
- **Actions** (only when an entry is chosen, re-checked against a fresh read first): `equip_weapon <archetype>`;
  `cycle_ammo`'s load function with the chosen clip and the same flag (2: from the inventory); `wpn_setting_toggle`;
  `ShockGame.SetSelectedPsiPower`; `use_obj <archetype>`; `play_unread_log`, `stop_email`, `quicksave`, `quickload`,
  `select_psipower`. Commands go through `Debug.Command` (the documented service; it calls the engine's
  `CommandExecute`, which applies each command's own mode checks). After each action the state is read again and the
  outcome shown as a notice ("PISTOL", "CANNOT EQUIP ASSAULT RIFLE", "USED MEDICAL HYPO", …).
- **Touch side** (GPL, the Shockolate core with opt-in hooks): `radial_for_action` (which wheel a held button opens),
  `radial_hold_ms` (400), empty wheels never open (a notice says why), up to 16 entries (more than 8: a larger wheel),
  `TOUCH_RADIAL_SYSTEM`, a full name per entry (shown above the wheel while highlighted). Without the hooks Shockolate's
  behaviour is unchanged (its six suites pass, §11).

| Button | Tap (unchanged) | Hold 400 ms in look mode |
| --- | --- | --- |
| `<` / `>` | wheel notch down / up (`cycle_weapon`) | weapons |
| AMMO | B (`cycle_ammo`); cursor mode: right click | ammunition + settings, or psi powers with the amp |
| ITEM (new, left bar under AMMO) | a notice ("HOLD ITEM FOR HYPOS AND PATCHES") | hypos and patches |
| II | Esc | system: QUICKSAVE, PLAY LOG, STOP LOG, QUICKLOAD, PSI PANEL |

Conventions: the finger that held the button owns the wheel; moving highlights (dimmed entries cannot be highlighted);
release on a segment selects, in the centre or beyond 1.25× the radius cancels; a finger that slides off the button
before 400 ms cancels without any action; opening never runs the tap action; the stick keeps walking; look ownership ends
when a wheel opens; FIRE and USE do not act while one is open; focus loss, Home, MODE and the game leaving shoot mode
close it with nothing selected; the open wheel is re-read every 250 ms; a light haptic per segment entered (when haptics
are on). The game is not paused.

## 3. Weapon wheel (Milestone B)

| Check (desktop, disposable write folders) | Result |
| --- | --- |
| Beta 4 slot 0 (wrench in hand) | WRENCH (current), PISTOL, SHOTGUN, ASSAULT RIFLE (dimmed), LASER PISTOL |
| Select pistol | `equip_weapon Pistol`; slot holds the pistol; game: "Pistol equipped." (`desk-b4-equip-refuse-hypo.jpg`) |
| Select the dimmed assault rifle (forced through the test hook) | game: "Item requires Standard 6"; nothing changed: the dimming matched the game |
| Melee / conventional / energy / psi amp | wrench, pistol, laser pistol, psi amp each equipped and current afterwards |
| Removal | after removing the shotgun the wheel had 3 entries; with every weapon removed: no wheel, "NO WEAPONS CARRIED"; choosing a weapon removed meanwhile: "WEAPON NO LONGER CARRIED", nothing done (`desk-removal-empty.log`) |
| All 15 Beta 4 saves | 3 to 5 weapons each, all real inventory (`desk-b4-15-saves-scan.log`) |
| Beta 8 | wrench only (early save); after adding a pistol: two entries, pistol equipped |

Inventory is never reordered: when the weapon changes, the game's own `equip_weapon` swaps them as the number keys do.

## 4. AMMO wheel (Milestone C)

| Gun | Entries (desktop) | Result |
| --- | --- | --- |
| Pistol | STANDARD ×115 (current), ANTI-PERS. ×66, ANTI-ARMOR ×33, NORM (current), BURST | ANTI-PERS. → HUD "Anti-Pers."; BURST → game "Setting changed to: BURST" (`desk-b4-ammo-burst-laser.jpg`); AP 33 → 9 left (24 loaded, old clip back in the inventory) |
| Shotgun (slots 4, 13) | two shell types + settings | listed |
| Laser pistol | NORM, OVER only (no ammunition types) | OVER was current in that save; the setting is read from the gun |
| Wrench (melee) | no wheel: "NO AMMO FOR THIS WEAPON" | |
| No weapon | "NO WEAPON" | |
| Psi amp | psi powers (§5) | |

The ammunition list is the game's own compatibility test per inventory clip, so only ammunition that loads into that gun
appears. Opening the wheel consumes nothing; cancelling changes nothing; ammo and settings are separate actions.

## 5. Psi wheel (Milestone D)

The player's saves have no psi; a **disposable test character** was made in a copy of Beta 4 slot 3 with test-only
script lines (only reachable from `SS2PORT_INPUT_SCRIPT`, never from touch): `give Psi Amp`, `grant_psi 1/3/6/12`
(`ShockGame.GrantPsiPower`, what the game's psi stations use; the shipping executable has no command for it, and its
`ubermensch` only maxes stats), `cmd psi_full`. The original saves were never touched.

| Step | Result (`desk-psi-select-fire.*`) |
| --- | --- |
| Equip the amp | weapon wheel lists PSIONIC AMPLIFIER first; the game selected power 1 itself; HUD "Psi Shield" |
| Hold AMMO | "PSI POWERS - 15 PSI": SCREEN (current), KINETIC, CRYOKINESIS, REGENERATE: exactly the 4 granted |
| Select CRYOKINESIS | `PsiState` 6, HUD "Cryokinesis", psi points still 15 (selected, not activated) |
| FIRE | 15 → 14: the normal control activates it, at its normal cost |

Selecting through the wheel needs the amp equipped (that is when AMMO offers powers, as the game's own `cycle_ammo`
opens the psi panel with the amp). Without the amp, the system wheel's PSI PANEL opens the game's psi panel. Psi on
Beta 8 was not exercised separately (same engine and service; the psi code does not touch NVScript).

## 6. Item wheel (Milestone E)

| Check | Result |
| --- | --- |
| Beta 4 slot 0 | MEDICAL HYPO ×20, ANTI-RADIATION HYPO ×12, PSI HYPO ×2, MEDICAL KIT ×3, ANTI-TOXIN HYPO ×7, SPEED BOOSTER ×1; no keys, implants or quest items |
| Use MEDICAL HYPO | `use_obj Med Patch`; count 20 → 19, health 48 → 50 (`desk-b4-equip-refuse-hypo.jpg`) |
| Full health (Beta 8) | the game refused: count unchanged, notice "MEDICAL HYPO NOT USED" |
| No items | "NO HYPOS OR PATCHES CARRIED", no wheel |
| Last of a stack used | the entry disappears at the next read |

## 7. System wheel (Milestone F)

| Check | Result |
| --- | --- |
| QUICKSAVE (Beta 4 and Beta 8) | `save_13` appeared in the disposable write folder; game messages "Saving game..." / "Game saved!" |
| QUICKSAVE → ammo AP + BURST → QUICKLOAD | after the load: STANDARD and NORM again, i.e. the quicksave's state (`desk-quicksave-change-quickload.*`) |
| QUICKLOAD with no quicksave | entry dimmed, "QUICKLOAD: NO QUICKSAVE YET" |
| QUICKLOAD question | automated (§11): LOAD loads, CANCEL or any other touch cancels, 6 s timeout, focus loss cancels |
| PLAY LOG | with all logs read (every save checked): dimmed, "NO UNREAD LOGS"; the command did nothing |
| STOP LOG | runs `stop_email` |

Quicksave writes the game's configured quicksave slot (`shock_quicksave_slot 13` in both installations' `cam_ext.cfg`),
labelled "Quicksave" in the load list; it never writes another slot.

## 8. Settings and layout (Milestone G)

- One new visible control, **ITEM**, in the left letterbox bar under AMMO (`.045, .47`), off the game's picture at 16:9;
  movable and resizable in the layout editor like the others; hidden while its wheel is off or without game state.
- SET → MORE: WEAPON WHEEL (`< >`), ITEM BUTTON, SYSTEM WHEEL (II), QUICKLOAD CONFIRM; all on by default. Stored as
  `weapon_wheel`, `consumable_wheel`, `explosive_wheel` (the existing keys), plus `quickload_confirm` and `game_radials`.
- **Migration (non-destructive)**: a file saved before Phase 3F (no `game_radials`) keeps every value except the three
  wheel keys, which did nothing in System Shock 2, and ITEM's position, which was never shown; those take the defaults.
  Once saved by this build, the player's choices are kept. Older builds ignore the new keys. Shockolate's own files and
  defaults are unchanged (the game defaults come from a hook only the SS2 glue sets).

## 9. Not done, and why

- **Lean**: `+leanleft`/`+leanright` are held actions; a wheel selects on release and cannot hold them, and the brief
  did not want permanent buttons. Not implemented.
- **Playing an unread log** was not exercised: none of the saves has one (logs play on pickup), the game has no command
  that adds an unread log, and the count is never written. The command and the count follow the engine's own handler.
- **Beta 8 psi, ammo types and settings** were not repeated on Beta 8 (same engine paths; Beta 8 covered weapons,
  ammo listing, items, quicksave/quickload).
- Icons: entries are text (the game's short names); no weapon icons. Long names (ANTI-RADIATION HYPO) are drawn smaller
  to fit their segment; the full name is the title while highlighted.

## 10. Defects found and fixed

- A read with no game running (main menu) released interfaces from an uninitialised struct → a call to address 0.
  Found on desktop when a save failed to load; fixed in `e75fbd3`.
- The PC's display changed to a 760×1417 portrait screen mid-phase, so the desktop build offered only 640×480;
  `desk_game.py` gained `DESK_RES`.
- Earlier reports said Shockolate's core suites pass; built as Shockolate builds them **with asserts enabled**, three of
  them fail against the SS2 layout table (SPRINT hidden, SS2 positions) — also at `4e4c482`. They test Shockolate's layout,
  so `run_shockolate_suites.sh` builds them with Shockolate's table (`TOUCH_SHOCKOLATE_LAYOUT`): all six pass, before
  and after Phase 3F. (Zig's `cc` defines `NDEBUG` at `-O2`; the script passes `-UNDEBUG`.)

## 11. Automated tests

| Suite | Result |
| --- | --- |
| Touch bridge (`tests/run.sh`) | **0 of 615** failed (380 before + 235 new) |
| Shockolate core suites (`phase3f/run_shockolate_suites.sh`) | touch_controls, alpha11, alpha2, alpha3, alpha4, ammo: **all pass** (asserts on) |

New bridge tests: 400 ms hold (closed at 380, open at 410); short taps of `<` `>` AMMO II unchanged, one notch each, and
390 ms is still a tap; no key, button or wheel event from any wheel gesture; centre, outside and slide-off cancel; stick
held while a wheel opens and selects, look ownership ends, another finger on a segment does nothing, FIRE blocked;
focus loss, game leaving shoot mode and MODE close the wheel with nothing selected and nothing stuck; AMMO in cursor mode
still right-clicks at once; empty wheel never opens and shows why; unavailable entries cannot be chosen; live refresh;
refused selections and game results as notices; haptics per segment, none when off; system wheel, quickload question
(LOAD, CANCEL, touch elsewhere, 6 s, focus loss, confirm off), II plain Esc when its wheel is off; ITEM placement in the
bar at 16:9 with no overlap, its notice, its wheel, hidden when off or without game state; 14 entries on a larger wheel;
settings: Shockolate files unchanged, a Phase 3E file migrated with everything else kept, the player's later choices
kept, older builds read the new file, page 2 rows on screen and not overlapping, RESET gives the SS2 defaults.

Binding resolution is unchanged (CROUCH, Phase 3E); the new actions do not use bindings.

## 12. On the phone

Galaxy S26 Ultra (SM-S948U1), app `local.newdarkandroid.ss2` updated in place from 0.9.1-unified-play (`19aacb27…`,
pulled and kept in `build/android/phase3f-preserve/`) to 0.9.2-radials-play. Driver `phase3f/radial_dev.py`:
`uhid_input -t` makes Android see an external HID touchscreen, so contacts go Android → `TouchSurface` → the bridge as a
finger does; each run used a **disposable write folder** (`files/radialtest/NAME`, a copy of one save slot and the
profile's configuration) that was removed afterwards. Logs: `phase3f/device-*.log`.

**Before** (and compared **after**): installed APK sha256, a tar of the app's internal profiles and settings (399 files),
sha256 manifests of all internal files and of the three game folders (8,207 files). Free space 19 GB.

| Check | Result |
| --- | --- |
| Settings migration | Your file (`d2102308…`, Phase 3E, wheels stored off, relative cursor) loaded as "weapon wheel on, ITEM on, system wheel on, quickload confirm on (defaults: the file predates them)"; cursor relative kept; ITEM shown in the left bar |
| Weapon wheel (Beta 4, 1280×720) | 4 carried weapons, PISTOL active (`device-b4-weapon-wheel.jpg`); laser pistol selected → `equip_weapon Laser Pistol`; back to the pistol from `<` |
| AMMO: settings | laser: NORM/OVER only (`device-b4-laser-settings.jpg`); NORM → "Setting changed" |
| AMMO: ammunition | pistol → ANTI-PERS., HUD "Anti-Pers." (`device-b4-pistol-antipers.jpg`); 1024×768 shotgun SLUG ×76 / PELLET ×41 / NORM / TRIPLE, PELLET loaded (`device-1024x768-shotgun-ammo.jpg`); Beta 8 pistol → ANTI-ARMOR (`device-b8-pistol-antiarmor.jpg`) |
| Psi (800×600, disposable test character) | amp equipped from the weapon wheel; AMMO → 4 known powers, title "PROJECTED CRYOKINESIS" from the game's strings (`device-800x600-psi-wheel.jpg`); selected with psi 15 → 15; one FIRE tap → 14 |
| ITEM | 5 consumables with counts (`device-b4-item-wheel.jpg`), MEDICAL HYPO used; Beta 8 with none: no wheel, "NO HYPOS OR PATCHES CARRIED"; Beta 8 at full health: the game refused the hypo |
| System | QUICKSAVE wrote `save_13` in the disposable folder only; QUICKLOAD question (`device-final-glyph-and-label-fixes.png`): a touch elsewhere cancelled, LOAD loaded; QUICKLOAD dimmed and unselectable with no quicksave (Beta 8) |
| Taps unchanged | `>` tap: pistol → shotgun (`device-b4-tap-next-shotgun.jpg`); AMMO tap: key B; II tap: Esc (menu), again: back |
| Stick + wheel | W held from stick down to stick up, across a `<` wheel that equipped the pistol; no other input |
| Home with a wheel open and the stick held | W released at once, nothing selected, the lifted fingers did nothing; the same game came back in LOOK |
| Cursor mode | MODE → interface (inventory), AMMO drawn as USE, ITEM hidden (`device-b8-cursor-mode-ammo-is-use.jpg`); AMMO tap and a 0.7 s hold both a right click on press, no wheel |
| Cursor styles | relative (yours): drag moved the cursor, tap clicked there; Direct (switched for the test): a tap at 1170,400 placed the cursor at game pixel 640,266 exactly, then clicked |
| Settings panel | page 2 with the four new rows, all on screen (`device-settings-page2.jpg`); CANCEL left the file byte-identical |
| Layout editor | ITEM selected and dragged like the other controls (`device-editor-item-drag.jpg`); CANCEL restored it |
| Beta 8 | NVScript identified as version `nv` (Beta 8) in the same app; weapons, ammo, items, quicksave as above |
| Native crashes, stuck inputs | none in any run; every key and button down had its up |
| Afterwards | 399 internal files (your 15 Beta 4 saves, the Beta 8 saves, settings) and 8,207 game files **byte-identical** to before; the legacy test app untouched |

Your touch settings were saved twice during the Direct-cursor test and then restored to their exact original bytes
(`d2102308…`) from the tar backup.

**Found on the phone and fixed** (final APK): the 3×5 font had no `:` or `?` ("QUICKLOAD?", "SETTING: BURST"), and the
layout editor named ITEM by its file key ("EXPLOSIVE"); it now shows each button's label.

Not done on the phone: PDA screens (the cursor path is unchanged since Phase 3D), a real finger's comfort (§14).

## 13. Build

| | |
| --- | --- |
| Version | **0.9.2-radials** (code 15), `local.newdarkandroid.ss2` (no new app) |
| Play APK (**installed**) | 47,521,394 bytes, sha256 `390c597d1b0fa116e1b407bb125fe4cb40cc5a3bf3ab3bdd10fc16375c9def09` |
| Dev APK | 96,348,872 bytes, sha256 `8d611442cfcb5509d46453124ac13a11e1b21aa39e87d44a44acc329af3c33c5` |
| `libss2game.so` in the play APK | 45,991,704 bytes, sha256 `fc11ddb4b1ebf75dca29b7d1344c8f757fec8428d3a69499dcd8236939d686af` |
| Where | `build/android/baseline/phase3f-radials/` (`SHA256SUMS`); Phase 3E's `19aacb27…` stays in `baseline/phase3e-unified/` |
| Built with | Phase 3E's command (same lifts, same NVScript identities); clean Gradle build |

## 14. Your acceptance checklist

1. In a game (LOOK mode), tap `<` and `>`: the weapon changes as before. Hold either: the weapon wheel; slide to a weapon
   and lift: equipped. Lift in the centre: nothing happens.
2. Hold AMMO with a gun: its ammunition with counts and its two settings; pick another ammunition, then the other
   setting. With a melee weapon: a notice instead.
3. Hold ITEM (left edge, under AMMO): your hypos and patches; use one when hurt.
4. Hold II: QUICKSAVE; then hold II → QUICKLOAD → LOAD. Also try CANCEL.
5. Walk with the stick while holding `>`: you keep walking while the wheel is open.
6. SET → MORE: the four new rows; switch one off, SAVE, and check the button acts plainly again.
7. Comfort: hold time (0.4 s), wheel size, labels, reach of ITEM and II. **Needs your hands.**
