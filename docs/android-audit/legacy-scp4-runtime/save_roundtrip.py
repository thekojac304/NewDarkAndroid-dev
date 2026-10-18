#!/usr/bin/env python3
"""Compare the script and quest state of two System Shock 2 save folders (LEGACY_SCP4_RUNTIME_RESULTS.md). Reads only.

    python save_roundtrip.py SAVE_A SAVE_B [--detail N]

SAVE_A, SAVE_B: save_N/ folders (for example an original save and the save the port wrote right after loading it).

For every level file present in both it compares
  - SCRIPTSTATE script data: the records {object, script class, key, type, value} that script modules (NVScript,
    allobjs, Squirrel scripts) store with IScriptMan::SetScriptData. Pending timer messages (sScrTimerMsg), which hold
    run-time pointers and absolute times, are skipped;
  - QUEST_DB (mission quest variables) and, in CAMPAIGN, QUEST_CMP (campaign quest variables);
  - ScrModules (the script modules the level declares);
and prints the records only in A, only in B, and those whose value changed, grouped by script class.
"""
import argparse, collections, os, struct, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "legacy-save"))
from save_analysis import chunks, links, symnames       # Dark tag-file reader, L$MetaProp links, archetype names

def _rec(c, p):
    """One script-data record at p: (obj, class, key, type, value), next p; None if the bytes are not one."""
    if p + 12 > len(c): return None
    obj, nc = struct.unpack_from("<iI", c, p)
    if not 1 <= nc <= 64 or p + 8 + nc + 4 > len(c): return None
    cls = c[p + 8: p + 8 + nc]
    if not all(32 <= b < 127 for b in cls): return None
    q = p + 8 + nc; nk = struct.unpack_from("<I", c, q)[0]
    if not 1 <= nk <= 64 or q + 4 + nk + 4 > len(c): return None
    key = c[q + 4: q + 4 + nk]
    if not all(32 <= b < 127 for b in key): return None
    q += 4 + nk; t = struct.unpack_from("<I", c, q)[0]; q += 4
    if t == 0: v = None
    elif t == 1:
        if q + 4 > len(c): return None
        v = struct.unpack_from("<i", c, q)[0]; q += 4
    elif t == 2:
        if q + 4 > len(c): return None
        v = round(struct.unpack_from("<f", c, q)[0], 5); q += 4
    elif t == 3:
        if q + 4 > len(c): return None
        n = struct.unpack_from("<I", c, q)[0]
        if n > 4096 or q + 4 + n > len(c): return None
        v = c[q + 4: q + 4 + n].decode("latin1"); q += 4 + n
    elif t == 4:
        if q + 12 > len(c): return None
        v = tuple(round(x, 4) for x in struct.unpack_from("<3f", c, q)); q += 12
    else: return None
    return (obj, cls.decode(), key.decode(), t, v), q

def script_data(c):
    """The script-data section of a SCRIPTSTATE chunk: u32 count, then count records (after the pending timer
    messages; other sections follow). Taken as the largest count whose records all parse. Returns (records, offset of
    the count); ([], -1) if no such section is found."""
    best = ([], -1)
    for start in range(len(c) - 4):
        n = struct.unpack_from("<I", c, start)[0]
        if not len(best[0]) < n <= 100000: continue
        p, out = start + 4, []
        while len(out) < n:
            r = _rec(c, p)
            if not r: break
            out.append(r[0]); p = r[1]
        if len(out) == n: best = (out, start)
    return best

def quest(c):
    """QUEST_DB / QUEST_CMP: {u32 len; char name[len] (with its NUL); i32 value}..."""
    out, p = {}, 0
    while p + 4 <= len(c):
        n = struct.unpack_from("<I", c, p)[0]
        if not 1 <= n <= 64 or p + 4 + n + 4 > len(c): break
        out[c[p + 4: p + 4 + n].split(b"\0")[0].decode("latin1")] = struct.unpack_from("<i", c, p + 4 + n)[0]; p += 4 + n + 4
    return out, p == len(c)

def files(d):        # the save's tag files only (save_14 also holds the player's own save_14.zip, which the game ignores)
    return {f.lower(): os.path.join(d, f) for f in os.listdir(d) if f.lower() == "campaign" or f.lower().endswith(".mis")}

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a"); ap.add_argument("b"); ap.add_argument("--detail", type=int, default=12)
    ap.add_argument("--gam", help="a shockscp.gam: also check that every object->archetype link (L$MetaProp) of both saves resolves in it, and list links that differ")
    ap.add_argument("--watch", default="", help="with --gam: FILE:OBJ,... objects whose archetypes to print, e.g. ops2.mis:1647,ops3.mis:1469")
    a = ap.parse_args()
    names = symnames(a.gam) if a.gam else None
    watch = [(w.split(":")[0].lower(), int(w.split(":")[1])) for w in a.watch.split(",") if w]
    fa, fb = files(a.a), files(a.b)
    print(f"A {a.a}\nB {a.b}")
    only = sorted(set(fa) ^ set(fb))
    if only: print("files in one only:", only)
    totals = collections.Counter()
    for f in sorted(set(fa) & set(fb)):
        ca, cb = chunks(fa[f]), chunks(fb[f])
        if f == "campaign":
            qa, oka = quest(ca.get("QUEST_CMP", b"")); qb, okb = quest(cb.get("QUEST_CMP", b""))
            diff = {k: (qa.get(k), qb.get(k)) for k in set(qa) | set(qb) if qa.get(k) != qb.get(k)}
            print(f"\n{f}: QUEST_CMP {len(qa)} / {len(qb)} variables (parsed fully: {oka}/{okb}); differ: {len(diff)}")
            for k, v in sorted(diff.items()): print(f"    {k}: {v[0]} -> {v[1]}")
            totals["quest_cmp_diff"] += len(diff); totals["quest_cmp"] += len(qa)
            continue
        if "SCRIPTSTATE" not in ca: continue
        if names is not None:
            la = {(s, d) for s, d in links(ca["L$MetaProp"]) if s > 0 and d < 0}; lb = {(s, d) for s, d in links(cb["L$MetaProp"]) if s > 0 and d < 0}
            unres = sorted({d for _, d in la | lb if d not in names})
            print(f"{f}: archetype links {len(la)} / {len(lb)}, unresolved in {os.path.basename(a.gam)}: {unres or 'none'}; "
                  f"only A {len(la - lb)}, only B {len(lb - la)}")
            for wf, wo in watch:
                if wf == f: print(f"    obj {wo}: A {[names.get(d, d) for s, d in sorted(la) if s == wo]}  B {[names.get(d, d) for s, d in sorted(lb) if s == wo]}")
            totals["arch_links"] += len(la); totals["arch_unresolved"] += len(unres); totals["arch_changed"] += len(la ^ lb)
        mods = (ca.get("ScrModules", b"").replace(b"\0", b" ").split(), cb.get("ScrModules", b"").replace(b"\0", b" ").split())
        qa, _ = quest(ca.get("QUEST_DB", b"")); qb, _ = quest(cb.get("QUEST_DB", b""))
        qd = {k: (qa.get(k), qb.get(k)) for k in set(qa) | set(qb) if qa.get(k) != qb.get(k)}
        ra, sa = script_data(ca["SCRIPTSTATE"]); rb, sb = script_data(cb["SCRIPTSTATE"])
        ka = {r[:3]: r[3:] for r in ra}; kb = {r[:3]: r[3:] for r in rb}
        gone = [k for k in ka if k not in kb]; new = [k for k in kb if k not in ka]
        changed = [k for k in ka if k in kb and ka[k] != kb[k]]
        print(f"\n{f}: modules {b' '.join(mods[0]).decode()} | {b' '.join(mods[1]).decode()}; QUEST_DB {len(qa)} vars, differ {len(qd)}; "
              f"script data {len(ra)} / {len(rb)} records (timer messages {sa} / {sb} bytes): only A {len(gone)}, only B {len(new)}, value changed {len(changed)}")
        for k, v in sorted(qd.items()): print(f"    quest {k}: {v[0]} -> {v[1]}")
        for label, keys, show in (("only A", gone, lambda k: ka[k]), ("only B", new, lambda k: kb[k]), ("changed", changed, lambda k: f"{ka[k][1]} -> {kb[k][1]}")):
            by = collections.Counter(k[1] + "." + k[2] for k in keys)
            for ck, n in by.most_common(a.detail): print(f"    {label}: {ck} x{n}" + (f"  e.g. obj {next(k[0] for k in keys if k[1] + '.' + k[2] == ck)}: {show(next(k for k in keys if k[1] + '.' + k[2] == ck))}" if n else ""))
        nv = sum(1 for k in ka if "NV" in k[1]); totals["records"] += len(ra); totals["nv"] += nv
        totals["only_a"] += len(gone); totals["only_b"] += len(new); totals["changed"] += len(changed); totals["quest_db_diff"] += len(qd)
        totals["nv_changed"] += sum(1 for k in changed + gone + new if "NV" in k[1])
    print("\ntotals:", dict(totals))

if __name__ == "__main__": main()
