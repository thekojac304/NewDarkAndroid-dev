# Proposed GitHub text (not published)

## Repository

**Name suggestion:** `NewDarkAndroid` (a fork of Owloeb/NewDarkRecompiled; keep the fork relationship so upstream credit
stays visible).

**Description (<= 350 chars):**
Unofficial Android port of System Shock 2 (NewDark 2.48): touchscreen controls, radial menus, launcher, SCP support.
Source only. Recompiles YOUR copy of the game; no game files or APKs included. Not affiliated with Nightdive or Looking Glass.

**Topics:** `system-shock-2` `android` `newdark` `static-recompilation` `touch-controls` `sdl2` `fan-port` `retro-gaming`

**Website:** none. **Features:** enable Issues; disable Wiki/Projects/Discussions at first.

**Suggested issue-template notice:** "Do not attach game files, saves, APKs or generated code. Attach `SS2.log` and the
launcher's Advanced log only."

## Release announcement (v0.1.0-beta.1, source release)

> **NewDarkAndroid v0.1.0-beta.1: first public beta (source only)**
>
> An unofficial, community-made way to play System Shock 2 (NewDark 2.48) on an ARM64 Android phone, built on
> Owloeb's NewDarkRecompiled. You compile your own copy of the game into an app on your PC; this repository contains no
> game code, no game files and no APK, and you shouldn't share the APKs you build.
>
> **Highlights:** full touchscreen controls with a floating stick and swipe look; radial menus that open right under
> the button you hold (weapons, ammo, psi, hypos, system); a proper launcher with a game-folder importer; SCP Beta 4
> and Beta 8 side by side; saves and settings survive app updates.
>
> **Beta caveats:** tested on a single device (Galaxy S26 Ultra); no full-campaign playthrough yet; native mods must be
> recompiled in; debug-signed builds only; no gamepad support.
>
> Build guide: `docs/android/BUILDING.md`. Licensing (MIT / GPL-3.0 / LGPL-2.1, game not covered): `LICENSING.md`.
> Not affiliated with or endorsed by Nightdive Studios, Looking Glass Studios or any rights holder. Credits:
> Owloeb/NewDarkRecompiled, Shockolate and its Halo-derived touch design, SDL, FFmpeg.

## Recommendation

**Option B: public source repository plus a GitHub source release** (tag `v0.1.0-beta.1` attached to the source
archives and `SHA256SUMS.txt`). Not C: no APK, since it would contain generated System Shock 2 code. See the readiness report.
