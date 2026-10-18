# Phase 3E-B: two NVScript versions in one build, chosen by the file's identity

2026-10-18. Branch `android-port`. One native build now holds both verified NVScript.osm builds, so one APK runs the
legacy SCP Beta 4 installation and the SCP Beta 8 installation. Overall results, migration and packaging:
[PHASE3E_UNIFIED_APK_RESULTS.md](PHASE3E_UNIFIED_APK_RESULTS.md). Log excerpts: `phase3e/B-module-selection-evidence.txt`.

## Supported identities (`port/nvscript_versions.json`)

| Version | File sha256 (bytes) | Prefix | Lifted at | SizeOfImage | ImageBase | Entry | Code CRC (at its base) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| SCP Beta 8 NVScript | `f15fc91b276e73ad52d5026a86a7d51b83e5644a20c3e58a53a10a4b52d4f11a` (704,512) | `nv` | `0x30400000` | `0xb2000` | `0x10000000` | `+0x5d989` | `7426e52a` |
| NVScript 1.2.4 (SCP Beta 4) | `abed4ee229dda567553b7d805f219008b6390c2d2da37459629eb28ac9ee6a9a` (662,016) | `nv124` | `0x30500000` | `0xa8000` | `0x10000000` | `+0x5f1e9` | `273bab35` |

The ranges `30400000–304b2000` and `30500000–305a8000` do not overlap each other or any other module (Squirrel
`30000000–302a0000`, lgvid `30300000–30309000`, allobjs at its own base). Both are reserved at start, as every built-in
module range is (`mod_reserve_all`). The prefixes keep the generated symbols apart (`nv_f000.c` … / `nv124_f000.c` …).
The Beta 8 lift is the same as before (its `src.sha1` matched: "already lifted"); 1.2.4 was lifted again at its new
address (4,288 functions, 10 s), because the Phase 3C legacy build had it at `0x30400000`.

## Loader (`port/core/modules.c`)

Before: the first descriptor with the file's name was used; size, ImageBase and code CRC were then checked against
that one descriptor only, so a second version of the same name could never load.

Now `LoadLibrary("…\NVScript.osm")`:

1. If a version of that name is already loaded, the reference count goes up (as before).
2. The file is read (through the folder overlay, as before) and must be a PE file.
3. Each built-in version **of that name** whose SizeOfImage, ImageBase **and entry point** equal the file's is tried:
   the image is mapped at that version's base, relocated, and the CRC of its code is computed. The first version whose
   CRC matches is used. A version that matched the header but not the CRC is cleared again.
4. If none matches, nothing is loaded, every compiled identity and the file's values are logged, and LoadLibrary
   fails (error 126). The engine then logs `Failed to load script module "nvscript.osm" (error code 126)`. **No other
   version is ever used in its place**, and the native x86 file is never executed.

The label in `nvscript_versions.json` and the variant name (`RecompModDesc.variant`, from gen_moddata.py) are only
logged; they never decide anything. With one version per name (every other module, and older builds), behaviour and
messages are as before, plus the entry-point check.

```
[port] C:\osm\nvscript.osm (…/games/scp4/osm/NVScript.osm): identified as recompiled version nv124 (SizeOfImage 0xa8000,
       ImageBase 10000000, code CRC 273bab35 at 30500000), one of 2 built in
[port] NVScript.osm: loaded the recompiled module at 30500000 (78 imports bound), DllMain -> 1
```

## Build (`port/build.py`, `android/build_native.py`, `port/CMakeLists.txt`)

- `--nvscript FILE` is repeatable. Each file must be called `NVScript.osm` (the engine loads it by that name) and is
  looked up by sha256 in `port/nvscript_versions.json` for its prefix and address. One unlisted file keeps the old
  behaviour (`nv` at `0x30400000`); several files must all be listed, and the same version twice is refused.
- Earlier NVScript lifts not in the current set are dropped from the generated folder (`drop_stale_nvscript`).
- CMake compiles every `nv*/` folder of the generated code.
- `--low-dll-bases` (PS Vita) accepts one NVScript, as before.
- Exact command used:

```sh
python android/build_native.py --game-only --game build/scp/game-scp/SS2.exe \
    --nvscript build/scp/SCP_beta8/NVScript.osm --nvscript build/scp4/modules/nvscript-1.2.4/NVScript.osm
```

Inputs: `SS2.exe` 2.48 `c7ff08c5…`, `allobjs.osm` `096c3a1e…`, `Squirrel.osm` 1.02 `d95b4415…`, `lgvid.dll` `64d55311…`,
and the two NVScript files above. No game file, generated C or library is in git.

## Launcher (`GameImport.java`)

`BuildConfig.GAME_MODULES` now lists `NVScript.osm:b2000:10000000` and `NVScript.osm:a8000:10000000`. The import check
groups them by name: a copy is accepted when its header matches any version, and the warning says "none of the 2
versions" otherwise (before, each entry would have warned about the other version).

## Validation

| Test | Where | Result |
| --- | --- | --- |
| SCP Beta 4 (game `scp4`): exact 1.2.4 selected | desktop, phone | `nv124` at `30500000`, 78 imports, `DllMain -> 1`, Quest Note Notifier initialized, monolog empty |
| SCP Beta 8 (game `ss2scp`): Beta 8 selected | desktop, phone | `nv` at `30400000`, 77 imports, static TLS slot, `DllMain -> 1` |
| Wrong code CRC (1 code byte flipped) | desktop | refused; both identities and the file's CRC at `30500000` (`e297f9b9`) logged |
| Wrong size (SizeOfImage + 0x1000) | desktop | refused ("size, base or entry point differs") |
| Unsupported module (Squirrel 1.01 renamed NVScript.osm) | desktop | refused (SizeOfImage `0x29f000`) |
| Not a PE file | desktop | refused ("not a Windows module (14 bytes)") |
| Missing (hard-linked game copy without NVScript.osm) | desktop | "could not be opened", engine error 126; the game copy verified unchanged afterwards (6,448 files) |
| Unload and reload in one process (quicksave, quickload ×2) | desktop | unloaded and identified as `nv124` again each time; notifier initialized 3 times; monolog empty |
| Switching installations between launches | phone | Beta 8, Beta 4, Beta 8, Beta 4 … runs in one app: each run logged its own version |
| In every refusal | desktop | the game kept running and exited 0; no substitution |

Device runs of both installations (loads, save and reload, grav shaft): PHASE3E_UNIFIED_APK_RESULTS.md §6–7.

## Not changed

The lifter, the runtime, the other modules' identities, the Windows host (`host/win_host.c` builds one NVScript at most
and still matches by name), and every existing check (size, ImageBase, code CRC; the SS2.exe check in `host.c`).
