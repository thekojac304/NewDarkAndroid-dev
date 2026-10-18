# Phase 3E-A: touch CROUCH follows the installation's own bindings

2026-10-18. Branch `android-port`, from `43dd5f8`. The player reported that crouching "appears not to work" in the
legacy app (SCP Beta 4, the original 17-mod installation). On-device results with the unified APK are in
[PHASE3E_UNIFIED_APK_RESULTS.md](PHASE3E_UNIFIED_APK_RESULTS.md) §1.

## Root cause (Verified)

The touch CROUCH button sent **Left Shift**, because the stock NewDark 2.48 `user.bnd` (the one Phase 2A was built and
tested against, also in the SCP Beta 8 installation) has `bind shift crouch`. The legacy installation's `user.bnd` is
the player's own (`d4b040ac…`, written by the game; the profile copy on the phone has the same bindings):

| Key | Stock 2.48 / SCP Beta 8 `user.bnd` | Legacy SCP Beta 4 `user.bnd` |
| --- | --- | --- |
| Shift | `crouch` (toggle) | **`+creepon`** (walk slowly while held) |
| C | — | **`crouch`** (toggle) |
| Ctrl | — | **`+crouchhold`** (crouched while held) |
| keypad 5 / keypad 1 | `crouch` / `+moveleftfast` | `+crouchhold` / `crouch` |

So in the legacy game CROUCH only made the player creep. Every other key the touch controls send (W A S D, Space, R, B,
Esc, Tab, mouse buttons, wheel) is bound the same way in both files.

Desktop check (`phase3e/desk_crouch.py`, the legacy game copy, a scratch copy of the profile, slot 0, standing still;
`phase3e/A1-desktop-legacy-c-shift-ctrl.jpg`, one row per key, columns: before, after a tap, after a second tap, while
held, after release):

| Key | Result |
| --- | --- |
| C | tap → crouched, second tap → standing, press → crouched and stays crouched after release: **toggle** |
| Shift | no change in any column: **not crouch** (creep) |
| Ctrl | taps do nothing lasting; crouched only **while held** |

A 80 ms press is enough for the toggle; press timing, touch hit-testing, input ownership and LOOK/CURSOR mode were not
involved (the button's key edges are logged and reach the game; the wrong key was sent).

## Fix

`touch_binds.c` (new, no SDL or host dependency) reads a bind file the way the game applies it: the last `bind KEY
COMMAND` of each key counts, `unbind` removes, any letter case, with or without quotes. It returns the key bound to
exactly `crouch`, else `+crouchhold`; it prefers Shift when Shift is one of them (the tested default keeps its
behaviour), then a plain key, then Shift/Ctrl/Alt, then a keypad key, and never a chord (`c+shift`).

`touch_ss2_android.c` maps `user.bnd` through the host's folder overlay (`vfs_map`: the profile's copy, which the game
itself reads and rewrites, before the game folder's), and sets the bridge's crouch key (`ss2t_set_crouch_key`) whenever
touch enters LOOK mode, so a binding changed in the game's Options applies when the menu closes. No `user.bnd` or no
crouch binding: Shift, as before. If the key changes while CROUCH is held, everything touch holds is released first.
The log says which key it chose:

```
[touch] CROUCH key from …/profiles/scp4/user.bnd: 'c' (crouch, toggle), DIK 0x2e
[touch] CROUCH key from …/games/ss2scp/user.bnd: 'shift' (crouch, toggle), DIK 0x2a
```

A toggle binding toggles on the press (as before); a held binding (`+crouchhold` only) keeps the key down exactly while
the finger is. Nothing in the game, the host input path or the bind files changes; no file is written.

## Tests

`port/backends/android/touch/tests/run.sh`: **0 of 380** checks failed (345 existing, unchanged, plus 35 new):

- `test_bind_parse`: the stock file keeps Shift; the legacy file gives C (the toggle, not keypad 1); held-only gives
  Ctrl before the keypad; last binding wins; `unbind`; chords and mouse names ignored; quotes and letter case; key name
  table (letters, digits, F1–F12, keypad, navigation keys).
- `test_crouch_key`: a 20 ms tap sends C (never Shift) held for the 60 ms minimum; CROUCH together with the stick and
  a look swipe; a held binding stays down over half a second of frames; changing the key while held releases it.

Run against the real files, the parser returns `c` for the legacy game folder's and profile's `user.bnd` and `shift`
for the Beta 8 game folder's and profile's.
