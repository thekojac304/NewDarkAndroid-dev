#!/usr/bin/env python3
"""Offline check of System Shock 2 saves against an SCP install (LEGACY_SAVE_COMPATIBILITY.md §4). Reads only.

    python save_analysis.py OLD.gam NEW.gam GAME_DIR SAVES_DIR

OLD.gam / NEW.gam: the shockscp.gam the saves were made with, and the one that will load them.
GAME_DIR: the new installation (DMM/SCP_beta8/{obj,mesh}, Data/res/{OBJ,MESH}.CRF, patch/, patch_ext.crf).
SAVES_DIR: a folder holding save_N/ folders.

For every save it prints the SAVEDESC name, how many concrete objects link to an archetype, the links whose archetype
ID means something else (or nothing) in NEW.gam, and the model names no file in GAME_DIR provides.
"""
import collections, glob, os, struct, sys, zipfile

def chunks(path):
    """Dark tag file: u32 TOC offset; TOC = u32 count, then {char name[12]; u32 offset; u32 size}; 24-byte chunk header."""
    b = open(path, "rb").read()
    toc = struct.unpack_from("<I", b, 0)[0]
    out = {}
    for i in range(struct.unpack_from("<I", b, toc)[0]):
        name, off, ln = struct.unpack_from("<12sII", b, toc + 4 + i * 20)
        out[name.split(b"\0")[0].decode("latin1")] = b[off + 24: off + 24 + ln]
    return out

def props(data):
    """Property chunk: {i32 obj; u32 size; data[size]}."""
    i = 0
    while i + 8 <= len(data):
        obj, sz = struct.unpack_from("<iI", data, i)
        yield obj, data[i + 8: i + 8 + sz]
        i += 8 + sz

def links(data):
    """Link chunk: {u32 id; i32 source; i32 dest; i16 flavor}."""
    for i in range(0, len(data) - 13, 14):
        yield struct.unpack_from("<Iiih", data, i)[1:3]

def symnames(gam):
    return {o: p[4:4 + struct.unpack_from("<I", p, 0)[0]].split(b"\0")[0].decode("latin1")
            for o, p in props(chunks(gam)["P$SymName"])}

def models(game):
    base = lambda n: os.path.basename(n).lower().rsplit(".", 1)[0]
    have = {"fx_particle"}                      # the engine's particle model, not a file
    for d in ("DMM/SCP_beta8/obj", "DMM/SCP_beta8/mesh"):
        if os.path.isdir(os.path.join(game, d)): have |= {base(f) for f in os.listdir(os.path.join(game, d))}
    for crf in ("Data/res/OBJ.CRF", "Data/res/MESH.CRF", "patch/obj.crf", "patch/mesh.crf", "patch_ext.crf"):
        p = os.path.join(game, crf)
        if os.path.isfile(p): have |= {base(n) for n in zipfile.ZipFile(p).namelist()}
    return have

def main(old_gam, new_gam, game, saves):
    old, new, have = symnames(old_gam), symnames(new_gam), models(game)
    reused = {o for o in old if o in new and old[o] != new[o]}
    print(f"archetypes: old {len(old)}, new {len(new)}; same ID and name {sum(1 for o in old if new.get(o) == old[o])}; "
          f"ID now another name {len(reused)}: " + ", ".join(f"{o} {old[o]!r}->{new[o]!r}" for o in sorted(reused)))
    for s in sorted(glob.glob(os.path.join(saves, "save_*")), key=lambda p: int(p.rsplit("_", 1)[1])):
        n, bad, miss = 0, collections.Counter(), collections.defaultdict(set)
        for f in glob.glob(os.path.join(s, "*")):
            b = os.path.basename(f)
            if b == "CAMPAIGN" or not b.lower().endswith(".mis"): continue
            c = chunks(f)
            for src, dst in links(c["L$MetaProp"]):
                if src > 0 and dst < 0:
                    n += 1
                    if dst in reused or dst not in new: bad[(b, src, old.get(dst, dst), new.get(dst))] += 1
            for o, p in props(c.get("P$ModelName", b"")):
                m = p.split(b"\0")[0].decode("latin1").lower()
                if m and m not in have: miss[m].add(b)
        desc = chunks(os.path.join(s, "CAMPAIGN"))["SAVEDESC"][:64].split(b"\0")[0].decode("latin1")
        print(f"{os.path.basename(s)} {desc!r}: {n} archetype links; remapped {sorted(bad) or 'none'}; "
              f"missing models {dict((k, sorted(v)) for k, v in miss.items()) or 'none'}")

if __name__ == "__main__":
    if len(sys.argv) != 5: sys.exit(__doc__)
    main(*sys.argv[1:])
