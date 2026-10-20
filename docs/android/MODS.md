# Mods and the System Shock 2 Community Patch (SCP)

## How mods work here

The game's code is recompiled **ahead of time on your PC**. That means:

| Kind of mod | Works? |
| --- | --- |
| Data: DML files, scripts (`.nut`), textures, models, sounds, missions, `cam_mod.ini` mod order | **Yes**, no rebuild; put them in the game folder as on PC |
| Native script modules (`.osm`) | **Only if that exact file was recompiled into your APK** at build time |
| NewDark modules `allobjs.osm`, `Squirrel.osm`, `lgvid.dll` | Yes, recompiled automatically from your install |
| Modified `SS2.exe` | No (only the stock 2.48 file is accepted) |
| D3D wrappers, ReShade, dgVoodoo, ASI loaders, other game-folder DLLs | Ignored (the port does its own graphics) |
| `fmsel.dll` and fan-mission selection | Not supported |
| Cutscene packs that are not AVI + Indeo 5 + PCM | Will not play |

Do not change the native modules on the phone without rebuilding.

## NVScript identities supported

The loader chooses the NVScript by the file's identity (size, ImageBase, entry point, code checksum), never by name or
label. Anything else is refused, with a message in the log.

| Identity | Use with | In the build as |
| --- | --- | --- |
| NVScript of **SCP Beta 8** (704,512 bytes) | SCP Beta 8 installation | `nv` |
| **NVScript 1.2.4** (2017; 662,016 bytes) | SCP Beta 4 installation | `nv124` |

Exact hashes: [`port/nvscript_versions.json`](../../port/nvscript_versions.json). To add another version, add an entry
there and rebuild. Both versions can live in one APK, with one game folder per installation.

## SCP notes

- SCP Beta 6 and later need NewDark 2.48. Start a **fresh game** with SCP; do not mix vanilla and SCP saves.
- SCP's self-check blocks play at the first gravity shaft on Earth if its script modules fail to load; seeing that
  message on the phone means NVScript was not built in, or is a different version.
- Beta 4 and Beta 8 were each started, played and saved on a device. See
  [SCP_COMPATIBILITY](../android-audit/SCP_COMPATIBILITY.md) and [MOD_COMPATIBILITY](../android-audit/MOD_COMPATIBILITY.md)
  for details and what was not tried.
- Other mods have **not** been tested in this beta.
