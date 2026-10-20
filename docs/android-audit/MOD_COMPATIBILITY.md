# Mod compatibility of the portable host

Phase 0 audit, 2026-10-17. This is a static analysis of NewDarkRecompiled `02f77da` plus a read-only inspection of
the local modded install at `<LaunchBox install>`. No mod has been run on the portable host
in this audit.

## 1. The key distinction: data, scripts and native code

The portable host runs the **original engine logic, recompiled**. Everything NewDark does with mod *data* happens
inside the recompiled `SS2.exe`. That includes resolving `cam_mod.ini` paths, searching `resname_base`/`load_path`,
opening `.crf` archives, loading `.mis`/`.gam` files, applying DMLs and loading `sq_scripts/*.nut`. The host only
supplies the Win32 file API underneath (`port/win32/kernel32_file.c`, `port/core/vfs.c`). Data and script mods
should therefore behave as on Windows, **provided the file API behaves like Windows**.

**Native code** is different. A `.osm` script module or `.dll` is x86 machine code. The portable host cannot execute
x86. It can only run modules that were **recompiled at build time from the exact file**:

- `port/core/modules.c:56-98`: `mod_load` accepts a module only if its name is in the build's `recomp_mods[]` table
  and its image size, image base (`:67`) and code CRC after relocation (`:84`) match what was lifted. Any other
  module makes `LoadLibraryA` fail (`port/win32/kernel32.c:322-327`). The portable host has **no fallback** to the
  original binary. Only the Windows host can fall back (`host/win_host.c:1359-1392`).
- The build recompiles a fixed list: `allobjs.osm` (Data), `Squirrel.osm` (osm), `lgvid.dll` (game root)
  (`port/build.py:35-36`), plus `SS2.exe`. `fmsel.dll` is lifted for the Windows host but deliberately not loaded by
  the portable host (`modules.c:59`).
- `SS2.exe` itself must match by size and CRC, or the host refuses to start (`port/core/host.c:400-403`).

This audit does not propose disabling or bypassing any of these identity checks.

## 2. General compatibility matrix

Categories: **(1)** confirmed compatible from existing evidence, **(2)** likely compatible but not yet tested,
**(3)** needs a portable-host fix, **(4)** needs a larger implementation, **(5)** unsupported or incompatible.

| Mod category | Code path | Category | Notes |
| --- | --- | --- | --- |
| Replacement textures (loose or in `.crf`) | Engine resource system → `CreateFileA`/`ReadFile`/`FindFirstFileA` → VFS | **(2)** | Formats are decoded by engine code. Uploads go through `dx/d3d9.c`. DXT is decoded on the CPU when the GPU lacks S3TC (`d3d9.c:117`), so large HD packs cost memory and load time. |
| Replacement sounds / music | Engine → DirectSound shim (software mixer) | **(2)** | `dx/dsound.c` resamples any rate and format the engine passes in. |
| Replacement models (`obj`, `mesh`, `fam`) | Engine | **(2)** | |
| Modified missions (`.mis`) | Engine | **(2)** | |
| Modified gamesys (`.gam`) | Engine | **(2)** | |
| `.crf` resource archives | Engine (ZIP reader in `SS2.exe`) | **(2)** | |
| Loose resource files | Engine + VFS case-insensitive resolution (`vfs.c:69-83`) | **(2)** | Windows-style mixed case is handled. |
| Custom resource directories / `mod_path` / `uber_mod_path` | Engine reads `cam_mod.ini`; directories are probed through the VFS | **(2)**, with a **(3)** caveat | **Relative** paths (`.\DMM\...`, `osm+ubermod`) work. **Absolute** Windows paths (`<local path>`) map to `<game>/Games/SS2/DMM/x`, because every drive letter is the game folder (`vfs.c:52`). Such paths would silently not resolve. Fix (3): strip a configurable Windows install prefix in `vfs.c`, or rewrite the copied `cam_mod.ini` into the write directory. The local install uses relative paths only. |
| `cam_mod.ini`, `install.cfg`, `cam_ext.cfg`, `user.cfg` | Engine (`GetPrivateProfileString`-style and its own parser) | **(2)** | Writes go to `SS2PORT_WRITE_DIR` when set (`vfs.c:97-106`). |
| Load priority / search order | Engine | **(2)** | Purely engine logic. Order: `uber_mod_path` → FM → `mod_path` → `resname_base` (local NewDark docs `Docs\newdark\cam_mod.ini`). |
| DML gameplay mods | Engine | **(1)** for gamesys DML | **Evidence:** on the Vita portable host, the gamesys DML `AI_Efficiency` "loaded fine and made enemies visibly dumber" (`port/backends/vita/PERF.md`, *What did not help*). Upstream also states that DMLs work unchanged on both hosts (`docs/MODDING.md`). Mission DMLs, `dbmods\` folders and `FINGERPRINT` blocks are **(2)**. |
| Squirrel `.nut` scripts | Recompiled `Squirrel.osm` (lifted at 0x30000000, `build.py:35`) | **(2)** | The Squirrel VM and compiler run as recompiled code. Its `setjmp`/`longjmp` error path is supported (`runtime/rt.h:83-109`), but on arm64 needs fix B1 (ANDROID_COMPATIBILITY.md). The file must be byte-identical to the one lifted. Because the build lifts **the user's own** `osm\squirrel.osm`, a version updated by SS2Tool matches by construction, as long as the APK is built from the same install that is copied to the phone. |
| Additional `.osm` modules (NVScript, Public Scripts `script.osm`, others) | Not recompiled → `LoadLibraryA` fails | **(4)** | The engine logs `Failed to load script module` and every script class from that module is missing, so objects silently lose behaviour. **This directly affects SCP and SS2Tool installs (NVScript); see §3.** |
| A mod's own copy of `allobjs.osm`/`Squirrel.osm` with different bytes | Refused by the CRC check (`modules.c:67`, `:84`) | **(5)** as shipped, **(4)** if lifted | Supportable only by lifting that exact file, i.e. building from the modded install. |
| Native DLL mods (D3D wrappers, ReShade, dgVoodoo, `ddfix`-style, ASI loaders) | The host implements D3D9/DirectSound/DirectInput itself and ignores game-folder DLLs | **(5)** | Irrelevant on Android. Their visual goals would need host work (out of scope). `OpenAL32.dll` in the game folder is never loaded (not a recompiled module). NewDark's choice of sound API is **Unknown** until `SS2.log` is checked on the port; the DirectSound path is the one implemented. |
| Modified executables (`SS2.exe` patches) | `host.c:400-403` refuses a different exe | **(5)** | In principle a different exe could be lifted. But `--fastpaths` and per-pixel lighting hook fixed 2.48 addresses (`port/README.md:212`, `core/fastpaths.c`), and the lifter's verification was done on retail 2.48 (`NEWDARK.md`). Treat as unsupported. |
| Cutscene replacement packs | Recompiled `lgvid.dll` → built-in `ffmpeg.dll` stand-in | **(2)** for AVI/Indeo 5/PCM, **(5)** otherwise | MPEG-4/H.264/MP4/MKV/ADPCM won't play; the log says why (`README.md`, Known Issues). |
| Fan missions via `fmsel.dll` | Refused (`modules.c:59`) | **(5)** | Out of scope. A fixed `fm` entry in `cam_mod.ini` is probably usable (engine path) but untested. |
| Mods that write into their own folders | VFS write overlay (`vfs.c:145-161`) | **(2)** | Writes land in the write directory, so the game folder stays unchanged. Directory listings come from one side only (`kernel32_file.c:146-158`). |

## 3. Executable-module compatibility in detail

### Modules present in the local modded install (read-only inspection)

| File | SHA-256 | Toolchain (PE header) | Imports | Recompiled today? |
| --- | --- | --- | --- | --- |
| `ss2.exe` | `4199aac1…5a55` | — | — | **No: this install is NewDark 2.47** (`ss2.log`: "Patch Final 2.47"). NewDarkRecompiled targets 2.48 (`README.md`, `NEWDARK.md`). |
| `Data\allobjs.osm` | `096c3a1e…a645` | MSVC linker 4.20 (1999) | KERNEL32, USER32 | Yes. The hash equals the one in `NEWDARK.md` ("byte-identical to the 1999 module"). |
| `osm\squirrel.osm` (FileVersion 1.0.1.0, 2018-04-06) | `50f569ad…26f0` | MSVC 2008 (linker 9.0), static CRT, relocatable | KERNEL32 (99), WSOCK32 (13) | Yes, lifted from whatever file the user has. |
| `osm\NVScript.osm` (1.2.4.0, Nameless Voice 2011; file date 2017-09-12) | `abed4ee2…9a6a` | **MSVC 2008 (linker 9.0), static CRT, has relocations** (`.reloc` 36 KB), image 0xA8000 | KERNEL32 (77, incl. `RaiseException`, `RtlUnwind`, `TlsAlloc`), USER32 (1) | **No** |
| `osm\script.osm` = `script-ss2.osm` (Public Scripts) | `0d50ee42…eca4` | GNU ld 2.56 (MinGW), base 0x11200000 | KERNEL32 (20), `msvcrt.dll` (42) | **No** (not used by SCP; may be used by fan missions) |

(The toolchain and import columns come from a pure-Python PE header parse in this audit. No disassembly was done.)

### What the module loader does with a mod's version

- **Different bytes of a recompiled module** → refused → `LoadLibrary` fails. No fallback on the portable host.
- **Unknown module name** → `LoadLibrary` fails with error 126 (`kernel32.c:326`).
- **A module listed in `recomp_mods[]` but absent from the install** → "could not be opened" (`modules.c:63`).

### Recompiling additional, legally obtained modules (the NVScript route)

The infrastructure is generic, and NVScript.osm looks like a good candidate:

- `build.py` lifts each entry of `MODULES` with `lift.py --iat-indirect [--rebase]` and describes it with
  `host/gen_moddata.py` (size, base, code CRC, entry). It generates `recomp_mods[]` (`build.py:107-120`). The host
  supports up to 16 modules (`modules.c:18`).
- NVScript.osm was built with the **same toolchain profile as `squirrel.osm`** (MSVC 2008, static CRT, relocations),
  which the lifter already handles, including its CRT. It is relocatable, so it can be lifted at a free rebase
  address. For example, `0x30400000` sits after `lgvid.dll` at `0x30300000` (`build.py:35`), and a low base would be
  needed for `MODULES_LOW` (`:36`).
- Work items (Phase 4):
  1. Add `("NVScript.osm", "osm", "nv", <base>)` to `MODULES`/`MODULES_LOW`, keeping the name match
     case-insensitive (`modules.c:19-24`).
  2. Run the upstream verification harness on it: differential function test and instruction fuzz
     (`harness/difftest.py`, `harness/insnfuzz.py`).
  3. Check its imports against the shim tables (missing shims abort with a clear message when first called,
     `modules.c:44-48`, `host.c:220`).
  4. Look for SSE paths (the static CRT gates them on CPUID, which reports no SSE, `rt.h:43-48`).
  5. Look for C++ `throw` (unsupported: "a `throw` stops the game", `README.md`).
- This adds a **matching recompile of the user's own file**. It bypasses no identity check: the CRC check then
  protects NVScript exactly as it protects Squirrel. It is a natural upstream contribution (the upstream roadmap
  item 1 is "Shakedown … with popular mods").
- MinGW-built modules such as Public Scripts import `msvcrt.dll` rather than MSVCR90. They would need a review of
  which `msvcrt` exports are shimmed. Defer until a concrete mod needs them.

**Native mods that cannot work under the portable host**, regardless of effort within this project's scope: any
module that relies on unsupported C++ exceptions or SEH at run time, uses SSE without a CPUID-gated fallback, calls
Win32/COM APIs the host does not and should not emulate (shell, GDI UI, networking beyond stubs), or patches engine
code in memory. Generic x86 emulation is explicitly out of scope.

## 4. Mod search-path assessment

- Mod path resolution, priorities, DML application and `.nut` loading order are **engine code**: identical to Windows
  by construction.
- Host-side requirements, all present:
  - case-insensitive resolution of every path component (`vfs.c:69-83`);
  - wildcard `FindFirstFileA`/`FindNextFileA` (`kernel32_file.c:126-162`) with Windows wildcard semantics
    (`vfs.c:117-126`);
  - `.`/`..` and trailing-dot normalisation (`vfs.c:50-67`);
  - current-directory handling (`vfs.c:107-112`).
- Missing directories in a path list (e.g. `ubermod` in `uber_mod_path osm+ubermod`, which does not exist locally)
  fail cleanly (`kernel32_file.c:155`).
- **Gaps:**
  1. Absolute Windows paths in configs (see the matrix).
  2. A single-sided directory listing when a directory exists in both the write folder and the game folder.
  3. Performance: directory listings are cached, but on FUSE-backed shared storage every first listing and open is a
     slower round trip. Prefer app-specific or internal storage (IMPLEMENTATION_PLAN.md §2).

## 5. Required fixes, in priority order

1. **Recompile NVScript.osm** (Phase 4, **(4)**). This is required by SCP and by SS2Tool-patched installs (see
   SCP_COMPATIBILITY.md).
2. **Absolute-path mapping** in `vfs.c` (**(3)**, only if a real install needs it; the local one does not).
3. **Merged directory listing** for the write overlay (**(3)**, only if observed).
4. Log `LoadLibrary` failures of `.osm` files at `port_log` level (not just `port_debug`, `kernel32.c:326`), so a
   missing native module is obvious in the Android log. This is a one-line diagnostic change.

## 6. Limitations to document for players

- Mods that ship their own `.osm` or `.dll` work only if those exact files were present when the APK was built
  **and** have been added to the recompiled-module list. Today that covers only `allobjs.osm`, `Squirrel.osm` and
  `lgvid.dll`.
- Do not change the native modules on the phone without rebuilding. Data-only changes (DML, `.nut`, assets,
  `cam_mod.ini` order) need no rebuild.
- `fmsel.dll` and FM selection, non-Indeo cutscene packs, D3D wrappers and exe patches are unsupported.
