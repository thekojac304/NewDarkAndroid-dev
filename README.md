# NewDarkAndroid: development history

> **Looking for NewDarkAndroid? This isn't the main repository.**
> The project lives at **[thekojac304/NewDarkAndroid](https://github.com/thekojac304/NewDarkAndroid)**. That repository
> has the current source, the source release
> ([v0.1.0-beta.1](https://github.com/thekojac304/NewDarkAndroid/releases/tag/v0.1.0-beta.1)), build instructions and
> feature list.

This repository is the **engineering archive** of NewDarkAndroid, an unofficial Android port of System Shock 2 (NewDark
2.48). It holds every development phase from the first ARM64 runtime bring-up to the v0.1.0-beta.1 release, with the
test reports, scripts and device screenshots behind each one. It is a reference, not a separate product, and it has no
downloads.

<p align="center"><img src="docs/images/gameplay-touch-controls.jpg" alt="System Shock 2 running in the port on a Galaxy S26 Ultra, with the touch controls" width="640"></p>

## What's here

| Branch | Contents |
| --- | --- |
| `release/v0.1.0-beta.1-prep` (default) | The development line at the release, plus release preparation: the full phase reports **with their evidence files** (screenshots, logs, driver scripts) in `docs/android-audit/` |
| `android-port` | The phase-by-phase development line, Phase 1A to Phase 3G |
| `public/v0.1.0-beta.1` | The single-commit source snapshot published as v0.1.0-beta.1 in the main repository |
| `main`, `android-port-audit`, tags `v0.1.0`–`v0.4.0` | Upstream [Owloeb/NewDarkRecompiled](https://github.com/Owloeb/NewDarkRecompiled), which this work starts from |

The code in the main repository's v0.1.0-beta.1 is the same as on these branches. The difference is that the main
repository leaves out the evidence files, which are kept here.

## The development phases

The reports are in [`docs/android-audit/`](docs/android-audit/):

| Phase | Report |
| --- | --- |
| 0. Audit and plan | `README.md`, `IMPLEMENTATION_PLAN.md`, `ANDROID_COMPATIBILITY.md` |
| 1A–1E. Runtime bring-up | ARM64 runtime and conformance, the Android app, first game execution, first playable gameplay, an Earth grav-shaft defect |
| 2A–2B. Touch and interface | Shockolate touch controls in System Shock 2, a frame-limit fix, scalable game interface, touchscreen polish |
| 3A–3B. Mods | NVScript recompiled into the port (`PHASE3A`), and the System Shock 2 Community Patch Beta 8 (`PHASE3B`) |
| 3C–3E. Compatibility | Legacy SCP Beta 4 saves and audio, the relative (trackpad) cursor, CROUCH following the game's binding, two NVScript versions in one APK |
| 3F–3G. Final touch work | Radial menus for weapons, ammo, psi, items and system actions; anchored fans; launcher and app icon |
| 4. Packaging | Android packaging and usability |

## Notes on this history

- **Redactions:** phone serial numbers and local user paths were replaced with `<device-serial>` and `<user>`. Because
  of that, commits after the upstream baseline have different hashes from the original private history. Upstream
  commits and tags keep their original hashes and signatures.
- **No game code:** there is no System Shock 2 code, generated C, game data or APK in this repository. The screenshots
  show the game running on the developer's own copy.
- **Old name:** this repository used to be called `NewDarkAndroid-dev`. Old links redirect here.

## License and credits

Licensing is mixed, per directory; see [LICENSING.md](LICENSING.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
The port stands on [Owloeb/NewDarkRecompiled](https://github.com/Owloeb/NewDarkRecompiled) and on
[Shockolate](https://github.com/Interrupt/systemshock)'s touch controls. System Shock 2 was made by Looking Glass Studios,
and Nightdive Studios hold the rights. None of them is involved in this project.
