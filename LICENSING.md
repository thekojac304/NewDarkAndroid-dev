# Licensing

This repository mixes code under several licenses. **There is no single license for the whole tree.** Each directory
below states its terms; a file's own header wins over this table. (Maintainer's review of 2026-10-19; this is an
engineering summary, not legal advice. Items marked **owner decision** need the project owner to confirm.)

## What is licensed by this repository

| Path | License | Origin |
| --- | --- | --- |
| `lift*.py`, `host/`, `runtime/`, `tools/`, `harness/`, `symbols/`, `build*.sh`, `triage.sh`, `docs/*.md` (non-Android), most of `port/` | **MIT** (`LICENSE`) | [Owloeb/NewDarkRecompiled](https://github.com/Owloeb/NewDarkRecompiled) and our modifications to it |
| `port/backends/android/android_main.c`, `port/core/ss2game.*`, `port/nvscript_versions.json`, `android/` (app, `build_native.py`, tools), `docs/android/`, `docs/android-audit/`, `docs/release/` | **MIT**, same terms (contributions offered under the upstream MIT license: confirmed by the owner) | This project |
| `port/backends/android/touch/` | **GPL-3.0-or-later** (`LICENSES/GPL-3.0.txt`) | Derived from the Shockolate Android touch controls ([Interrupt/systemshock](https://github.com/Interrupt/systemshock), GPL-3.0-or-later; Halo-derived design). Files new to this project in that directory (`touch_ss2*.c/h`, `touch_surface.*`, `touch_render_gles.c`, `tests/`) link to the GPL core and are offered under the same license |
| `android/app/src/main/java/.../TouchSurface.java` | **GPL-3.0-or-later** | Derived from the Shockolate `SDLSurface` patch |
| `video/ffmpeg/` | **LGPL-2.1-or-later** (`video/ffmpeg/COPYING.LGPLv2.1`) | Unmodified FFmpeg Indeo 5 decoder (commit in `video/ffmpeg/FFMPEG_COMMIT`); `video/lavshim.*` and `video/ffmpeg/compat/` are the project's own glue (MIT) |
| App icon (`android/app/src/main/res/{drawable,mipmap-anydpi-v26}`, `android/tools/make_icon.py`), launcher look | Original artwork drawn in code for this project (wrench over a HUD reticle); contains no game art. Licensed MIT (owner decision, 2026-10-19) | This project |
| `docs/images/launcher.png` | Screenshot of the app's own launcher (no game content) | This project |

## What is NOT licensed by this repository, and is not in it

- **System Shock 2 and its engine** (`SS2.exe`, `.osm`, `.dll`, `.crf`, `.mis`, `.gam`, movies, sound, textures): the
  property of their rights holders. Not included. You supply your own copy.
- **Everything generated from them**: the lifted C (`build/**/gen`, `out/`), the object files, `libss2game.so`, and any
  APK containing them. The recompiler's MIT license **does not cover its output**; the upstream `LICENSE` says so
  explicitly, and the generated code is a derivative of the copyrighted executable. It is `.gitignore`d and must never
  be committed, uploaded or attached to a release. Mentioning that GPL-licensed code is linked into the same APK
  does not change this: the GPL cannot give anyone permission to copy System Shock 2's code.
- **Mods and NVScript** (SCP Beta 4/8, NVScript 1.2.4/Beta 8, DML, etc.): belong to their authors. The repository only
  records their sha256 identities (`port/nvscript_versions.json`).
- **Names and marks**: "System Shock", "System Shock 2", "Looking Glass", "Nightdive", "NewDark" and "Shockolate" are
  used descriptively and nominatively only.

## Consequences for distribution

| Artifact | Redistribution |
| --- | --- |
| This source tree, as listed in `docs/release/PUBLICATION_ALLOWLIST.txt` | **Yes.** MIT/GPL-3.0/LGPL-2.1 terms apply per directory; keep `LICENSE`, `LICENSES/`, `THIRD_PARTY_NOTICES.md` and the FFmpeg `COPYING` |
| A playable APK (contains generated game code, GPL touch code and LGPL FFmpeg) | **No.** No rights to the generated game code; and a GPL/LGPL-conforming binary release is impossible with proprietary code inside |
| A "stub" APK built without `--game` (launcher + SDL + conformance tests, no game code) | Technically distributable (GPL-3.0 applies to the whole APK; source offer needed), but useless to players and not recommended for this release |
| The recompiler tools alone | Yes (MIT) |

If you derive further work: keep GPL files GPL, keep MIT notices, never add game data or generated code.
