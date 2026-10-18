#!/usr/bin/env python3
"""Static checks behind LEGACY_SCP4_FEASIBILITY.md. Reads only; writes nothing but its report on stdout.

    python scp4_static_audit.py LEGACY_DIR SCP8_GAME_DIR [--pe]

LEGACY_DIR: a copy (or the original, read only) of the NewDark 2.47 + SCP Beta 4 installation, with its save_N folders.
SCP8_GAME_DIR: the NewDark 2.48 + SCP Beta 8 folder the current APK was built from (build/scp/game-scp).
--pe: also compare the native modules' PE headers and imports (needs pefile).

1. Every concrete-object -> archetype link in every save, resolved against Beta 4's and Beta 8's shockscp.gam.
2. Every script name used by the saves, Beta 4's gamesys and missions, and the quest notifier, and the module or
   Squirrel file that provides it (case-insensitive, as the engine looks them up).
3. The script modules each save and mission declares (ScrModules).
4. Traces of the other mods inside the saves: SS2 Rebirth's eng1 model, Vurt's Flora Earth DML, the quest notifier.
5. Gamesys DMLs: fingerprints and whether they create archetypes.
"""
import collections, glob, os, re, struct, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "legacy-save"))
from save_analysis import chunks, props, links, symnames          # noqa: E402  (tag-file readers)

def ci(root, rel):
    """Case-insensitive path lookup under root (Windows game folders)."""
    p = root
    for part in rel.replace("\\", "/").split("/"):
        hit = [e for e in os.listdir(p) if e.lower() == part.lower()] if os.path.isdir(p) else []
        if not hit: return None
        p = os.path.join(p, hit[0])
    return p

def script_names(path):
    c = chunks(path); out = set()
    for _, p in props(c.get("P$Scripts", b"")):
        for k in range(4):
            n = p[k * 32:(k + 1) * 32].split(b"\0")[0].decode("latin1")
            if n: out.add(n)
    return out

def nut_classes(path):
    return {c.lower() for c in re.findall(r"class\s+(\w+)\s+extends", open(path, encoding="latin1").read())} if path else set()

def has_name(blob, n):
    return re.search(rb"(?<![a-z0-9_])" + re.escape(n.lower().encode()) + rb"\x00", blob) is not None

def saves(legacy):
    return sorted(glob.glob(os.path.join(legacy, "save_*")), key=lambda p: int(p.rsplit("_", 1)[1]))

def mis_files(d):
    return [f for f in glob.glob(os.path.join(d, "*")) if f.lower().endswith(".mis")]

def main(legacy, scp8, pe=False):
    b4gam, b8gam = ci(legacy, "DMM/SCP_beta4/shockscp.gam"), ci(scp8, "DMM/SCP_beta8/shockscp.gam")
    b4, b8 = symnames(b4gam), symnames(b8gam)
    print("## 1. Archetype links")
    reused = {o for o in b4 if o in b8 and b4[o] != b8[o]}
    tot, un4, re8 = 0, collections.Counter(), collections.Counter()
    for s in saves(legacy):
        for f in mis_files(s):
            for src, dst in links(chunks(f)["L$MetaProp"]):
                if src > 0 and dst < 0:
                    tot += 1
                    if dst not in b4: un4[(os.path.basename(s), os.path.basename(f), dst)] += 1
                    if dst in reused or dst not in b8: re8[(os.path.basename(s), os.path.basename(f), src, b4.get(dst), b8.get(dst))] += 1
    print(f"links {tot}; unresolved with Beta 4 gamesys: {len(un4)} {sorted(un4)[:5]}")
    print(f"meaning changes with Beta 8 gamesys: {len(re8)} " + "; ".join(f"{k[0]} {k[1]} obj {k[2]}: {k[3]} -> {k[4]}" for k in sorted(re8)[:12]))

    print("\n## 2. Script providers")
    used = collections.defaultdict(set)
    for s in saves(legacy):
        for f in mis_files(s): [used[n].add("saves") for n in script_names(f)]
    for n in script_names(b4gam): used[n].add("b4 gamesys")
    for f in mis_files(os.path.dirname(b4gam)): [used[n].add("b4 missions") for n in script_names(f)]
    used["zbQuestNotifier"].add("quest notifier DML")
    blobs = {"allobjs.osm": open(ci(legacy, "Data/allobjs.osm"), "rb").read().lower(),
             "NVScript 1.2.4": open(ci(legacy, "osm/NVScript.osm"), "rb").read().lower(),
             "Public Scripts (not loaded)": open(ci(legacy, "osm/script.osm"), "rb").read().lower()}
    nv8 = open(ci(scp8, "DMM/SCP_beta8/NVScript.osm"), "rb").read().lower()
    nuts = {"scpScript.nut (Beta 4)": nut_classes(ci(legacy, "DMM/SCP_beta4/sq_scripts/scpScript.nut")),
            "questnot.nut": nut_classes(ci(legacy, "DMM/ss2_quest_note_notifier/sq_scripts/questnot.nut")),
            "patch_ext allobjs.nut (SS2Tool)": nut_classes(ci(legacy, "patch_ext/sq_scripts/allobjs.nut"))}
    vanilla = script_names(ci(legacy, "Data/shock2.gam"))
    summary, unresolved, nv = collections.Counter(), [], []
    for n in sorted(used):
        prov = [k for k, b in blobs.items() if has_name(b, n)] + [k for k, c in nuts.items() if n.lower() in c]
        summary[" + ".join(prov) or "none"] += 1
        if "NVScript 1.2.4" in prov: nv.append((n, has_name(nv8, n)))
        if not prov or prov == ["Public Scripts (not loaded)"]:
            unresolved.append(f"{n} ({', '.join(sorted(used[n]))}{'; also in vanilla shock2.gam' if n in vanilla else ''})")
    print(f"distinct names {len(used)}"); [print(f"  {v:4} {k}") for k, v in summary.most_common()]
    print("no loaded provider:", "; ".join(unresolved))
    print(f"NVScript classes used {len(nv)}; also present by name in Beta 8's NVScript: {sum(ok for _, ok in nv)}")

    print("\n## 3. Declared script modules (ScrModules)")
    decl = collections.Counter()
    for f in [f for s in saves(legacy) for f in mis_files(s)] + mis_files(os.path.dirname(b4gam)):
        decl[tuple(sorted({x.decode().lower() for x in re.findall(rb"[A-Za-z_0-9]{3,}", chunks(f)["ScrModules"])}))] += 1
    [print(f"  {v:4} files: {', '.join(k)}") for k, v in decl.items()]

    print("\n## 4. Mod traces in the saves")
    e4 = chunks(ci(legacy, "DMM/SCP_beta4/earth.mis"))
    p4 = [struct.unpack_from("<3f", p) for o, p in props(e4["P$Position"]) if o == 885]
    print(f"Beta 4 earth.mis obj 885 at {tuple(round(x, 2) for x in p4[0])}; Vurt's Flora earth.mis.dml moves it to (-108.248, 30.4444, 62.95)")
    for s in saves(legacy):
        e = chunks([f for f in mis_files(s) if os.path.basename(f).lower() == "earth.mis"][0])
        pos = [tuple(round(x, 2) for x in struct.unpack_from("<3f", p)) for o, p in props(e["P$Position"]) if o == 885]
        rb = any(m.split(b"\0")[0] == b"crpsf_arm" for f in mis_files(s) if os.path.basename(f).lower().startswith("eng")
                 for o, m in props(chunks(f).get("P$ModelName", b"")))
        qn = [os.path.basename(f) for f in glob.glob(os.path.join(s, "*")) if b"zbQuestNotifier" in open(f, "rb").read()]
        desc = chunks(os.path.join(s, "CAMPAIGN"))["SAVEDESC"][:64].split(b"\0")[0].decode("latin1")
        print(f"  {os.path.basename(s):8} {desc:26} Flora obj885 {pos[0] if pos else '-'}  Rebirth crpsf_arm {'yes' if rb else 'no'}  quest notifier {qn or '-'}")

    print("\n## 5. Gamesys DMLs")
    for f in sorted(glob.glob(os.path.join(legacy, "**", "gamesys.dml"), recursive=True)):
        if "SS2ToolSyncBackup" in f: continue
        t = open(f, encoding="latin1").read()
        fp = re.findall(r"(!?FINGERPRINT)\s*\{([^}]*)\}", t)
        print(f"  {os.path.relpath(f, legacy)}: fingerprints {[(a, ' '.join(b.split())) for a, b in fp] or 'none'}; "
              f"creates archetypes: {bool(re.search(r'^\s*(CreateArch|Create)\b', t, re.M))}; #script {re.findall(r'#script\s+\"(\w+)\"', t)}")
    print(f"  archetype -4755 in Beta 4: {b4.get(-4755)!r}; in Beta 8: {b8.get(-4755)!r}  (SS2Tool's patch_ext DML is skipped only if it is named SCP)")

    if pe:
        import pefile
        print("\n## 6. Native modules")
        for rel, root in [("osm/NVScript.osm", legacy), ("DMM/SCP_beta8/NVScript.osm", scp8), ("osm/squirrel.osm", legacy),
                          ("osm/squirrel.osm", scp8), ("Data/allobjs.osm", legacy), ("Data/allobjs.osm", scp8), ("lgvid.dll", legacy), ("lgvid.dll", scp8)]:
            p = ci(root, rel); x = pefile.PE(p, fast_load=False); oh = x.OPTIONAL_HEADER
            ver = {k.decode(): v.decode(errors="replace") for fi in getattr(x, "FileInfo", []) for e in fi for st in getattr(e, "StringTable", []) for k, v in st.entries.items()}
            imps = sum(len(e.imports) for e in getattr(x, "DIRECTORY_ENTRY_IMPORT", []))
            import hashlib
            print(f"  {'legacy' if root == legacy else 'scp8  '} {rel:28} {hashlib.sha256(open(p, 'rb').read()).hexdigest()[:16]}  linker {oh.MajorLinkerVersion}.{oh.MinorLinkerVersion}  "
                  f"base {oh.ImageBase:#x} size {oh.SizeOfImage:#x}  TLS {oh.DATA_DIRECTORY[9].Size > 0}  imports {imps}  version {ver.get('FileVersion', '-')!r}")

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2: sys.exit(__doc__)
    main(*args, pe="--pe" in sys.argv)
