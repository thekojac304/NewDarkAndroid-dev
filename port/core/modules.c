/* modules.c: loader for the recompiled DLLs (allobjs.osm, Squirrel.osm, lgvid.dll, fmsel.dll).
 *
 * The engine loads them with LoadLibrary; here the file is read from the game folder, mapped into guest memory at the
 * address it was recompiled for (relocated if the file's own base differs), checked against the CRC of the code that was
 * lifted, its imports are bound to host functions, and its recompiled entry points are found through the module's
 * lookup table. A module whose bytes differ (a mod's copy, another version) is refused, as on the Windows host.
 *
 * Several versions of one module can be built in (two NVScript.osm, for two SCP installations): each has its own
 * descriptor, prefix and guest range. LoadLibrary picks the one whose identity the file on disk has (SizeOfImage,
 * ImageBase, entry point, and the CRC of its code mapped at that descriptor's base), never by a label or by order; a
 * file that matches none is refused, never replaced by another version.
 *
 * The address each module was lifted at is fixed in the generated code, so the host reserves those ranges up front. On
 * hosts with a small guest address space, lift the modules at low bases (build_win.py's MODULES table) to keep the
 * space compact. */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "core.h"
#include "recomp_mod.h"
#include "ss2game.h"

typedef struct { int refs, reserved, unloading; } ModSt;
static ModSt st[16];
static int ieq(const char *a, const char *b) { while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) a++, b++; return !*a && !*b; }
static const char *base_name(const char *n) { const char *b = strrchr(n, '\\'), *b2 = strrchr(n, '/'); if (b2 > b) b = b2; return b ? b + 1 : n; }
static int is_named(int i, const char *n) { return ieq(base_name(n), recomp_mods[i]->name); }
static int find_by_name(const char *n) {        /* the loaded version of this module if one is, else the first built in */
    int first = -1;
    for (int i = 0; recomp_mods[i] && i < 16; i++) if (is_named(i, n)) { if (st[i].refs) return i; if (first < 0) first = i; }
    return first;
}
static const char *variant(const RecompModDesc *d) { return d->variant ? d->variant : "?"; }
static int find_by_handle(uint32_t h) { for (int i = 0; recomp_mods[i] && i < 16; i++) if (st[i].refs && recomp_mods[i]->base == h) return i; return -1; }
uint32_t mod_handle_of(const char *n) { int k = find_by_name(n); return k >= 0 && st[k].refs ? recomp_mods[k]->base : 0; }
uint32_t mod_highest_end(void) { uint32_t e = 0; for (int i = 0; recomp_mods[i] && i < 16; i++) if (recomp_mods[i]->base + recomp_mods[i]->size > e) e = recomp_mods[i]->base + recomp_mods[i]->size; return e; }
void mod_reserve_all(void) {
    for (int i = 0; recomp_mods[i] && i < 16; i++) {
        const RecompModDesc *d = recomp_mods[i];
        st[i].reserved = vm_mark(d->base & ~0xFFFFu, d->base + d->size, d->name);
        if (!st[i].reserved) port_warn("%s cannot be loaded: its range %08x-%08x is not available", d->name, d->base, d->base + d->size);
    }
}
static uint32_t code_crc(const RecompModDesc *d) {
    static uint32_t tab[256]; uint32_t crc = 0;
    if (!tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; tab[i] = c; }
    for (unsigned r = 0; r < d->ncode; r++) {
        const uint8_t *q = GP(d->code_lo[r]); size_t len = d->code_hi[r] - d->code_lo[r];
        uint32_t c = ~crc; while (len--) c = tab[(c ^ *q++) & 0xFF] ^ (c >> 8); crc = ~c;
    }
    return crc;
}
uint32_t mod_containing(uint32_t a) {          /* GetModuleHandleExW(FROM_ADDRESS): the loaded module that holds a */
    for (int i = 0; recomp_mods[i] && i < 16; i++) if (st[i].refs && a >= recomp_mods[i]->base && a < recomp_mods[i]->base + recomp_mods[i]->size) return recomp_mods[i]->base;
    return 0;
}

/* Static TLS (IMAGE_TLS_DIRECTORY). MSVC 2015 and later use it in every module for thread-safe local statics: the code
 * reads fs:[2Ch] (the TEB's TLS pointer array), takes entry [_tls_index] and uses the data there. Windows gives each
 * thread a copy of the template for modules loaded with LoadLibrary too, so the loader does the same: slot k's copy lives
 * in each thread's TEB block at TEB + 0x2000 + k * 0x1000 (the block is 64 KB; TlsAlloc slots are at TEB + 0xE10, the
 * pointer array at TEB + 0x1000), for threads that exist at load time and for every thread started later. */
#define TLS_SLOTS 8
#define TLS_BLOCK 0x1000u
static struct { uint32_t start, len; int mod; } tls_slot[TLS_SLOTS];
static void tls_fill(uint32_t teb, int k) {
    uint32_t b = teb + 0x2000u + TLS_BLOCK * (uint32_t)k;
    memset(GP(b), 0, TLS_BLOCK); memcpy(GP(b), GP(tls_slot[k].start), tls_slot[k].len); WR32(teb + 0x1000u + 4u * (uint32_t)k, b);
}
static int tls_new;                            /* the slot being set up by mod_load, for thread_foreach_teb */
static void tls_fill_new(uint32_t teb) { tls_fill(teb, tls_new); }
void mod_tls_thread(uint32_t teb) { for (int k = 0; k < TLS_SLOTS; k++) if (tls_slot[k].len || tls_slot[k].start) tls_fill(teb, k); }
void thread_foreach_teb(void (*fn)(uint32_t teb));
static int tls_attach(int m, const RecompModDesc *d, uint32_t dir) {
    uint32_t rva = RD32(dir + 8 * 9), sz = RD32(dir + 8 * 9 + 4); if (!rva || sz < 24) return 1;
    uint32_t t = d->base + rva, start = RD32(t), end = RD32(t + 4), idx = RD32(t + 8), cbs = RD32(t + 12), zero = RD32(t + 16);
    if (end < start || end - start + zero > TLS_BLOCK) { port_log("%s: its static TLS (%u bytes) is larger than the loader supports", d->name, end - start + zero); return 0; }
    if (cbs && RD32(cbs)) { port_log("%s: TLS callbacks are not supported", d->name); return 0; }
    int k = 0; while (k < TLS_SLOTS && (tls_slot[k].len || tls_slot[k].start)) k++;
    if (k == TLS_SLOTS) { port_log("%s: no static TLS slot left", d->name); return 0; }
    tls_slot[k].start = start; tls_slot[k].len = end - start; tls_slot[k].mod = m;
    if (!start) tls_slot[k].start = d->base;   /* an empty template still needs a slot (never 0, so the slot counts as used) */
    WR32(idx, (uint32_t)k); tls_new = k; thread_foreach_teb(tls_fill_new);
    port_log("%s: static TLS slot %d (%u bytes per thread)", d->name, k, end - start + zero);
    return 1;
}
static void tls_detach(int m) { for (int k = 0; k < TLS_SLOTS; k++) if (tls_slot[k].mod == m && (tls_slot[k].len || tls_slot[k].start)) tls_slot[k].start = tls_slot[k].len = 0; }

static uint32_t import_thunk(const char *dll, const char *fn) {
    uint32_t a = port_proc(fn); if (a) return a;
    size_t l = strlen(dll) + strlen(fn) + 2; char *nm = malloc(l); snprintf(nm, l, "%s!%s", dll, fn);
    return g_thunk(NULL, 0, nm);       /* dies with this name if the module ever calls it */
}
static uint8_t *slurp(const char *host, long *n) {
    int err; PlatFile *f = plat_fs_open(host, PLAT_READ, &err); if (!f) return NULL;
    PlatStat s; if (plat_fs_fstat(f, &s)) { plat_fs_close(f); return NULL; }
    uint8_t *d = malloc((size_t)s.size + 1); int64_t got = plat_fs_read(f, d, s.size); plat_fs_close(f);
    if (got != (int64_t)s.size) { free(d); return NULL; }
    *n = (long)s.size; return d;
}
/* Maps the file's image at d's base, applies its relocations and returns the CRC of its code as mapped there (the
 * identity gen_moddata.py recorded). The image stays mapped; the caller clears it when the CRC is not d's. */
static uint32_t map_image(const RecompModDesc *d, const uint8_t *file, long n) {
    uint32_t lf = *(const uint32_t *)(file + 0x3c); const uint8_t *nt = file + lf;
    uint32_t hdrs = *(const uint32_t *)(nt + 24 + 60);
    unsigned nsec = *(const uint16_t *)(nt + 6), optsz = *(const uint16_t *)(nt + 20); const uint8_t *sec = nt + 24 + optsz;
    if (hdrs > d->size) hdrs = d->size; if (hdrs > (uint32_t)n) hdrs = (uint32_t)n;
    memset(GP(d->base), 0, d->size); memcpy(GP(d->base), file, hdrs);
    for (unsigned i = 0; i < nsec; i++) {
        const uint8_t *s = sec + 40 * i; uint32_t vsz = *(const uint32_t *)(s + 8), va = *(const uint32_t *)(s + 12), rsz = *(const uint32_t *)(s + 16), raw = *(const uint32_t *)(s + 20);
        uint32_t cp = rsz < vsz || !vsz ? rsz : vsz; if ((uint64_t)raw + cp > (uint64_t)n) cp = raw < (uint32_t)n ? (uint32_t)n - raw : 0; if (va > d->size || cp > d->size - va) continue;   /* truncated file: no wrap-around */
        memcpy(GP(d->base + va), file + raw, cp);
    }
    uint32_t delta = d->base - d->file_base, ntg = d->base + RD32(d->base + 0x3c), dir = ntg + 24 + 96;      /* data directories */
    uint32_t reloc_rva = RD32(dir + 8 * 5), reloc_sz = RD32(dir + 8 * 5 + 4);
    if (delta && reloc_rva) {
        for (uint32_t p = d->base + reloc_rva, e = p + reloc_sz; p < e;) {
            uint32_t page = RD32(p), blk = RD32(p + 4); if (!blk) break;
            for (uint32_t q = p + 8; q < p + blk; q += 2) { uint16_t en = RD16(q); if ((en >> 12) == 3) WR32(d->base + page + (en & 0xFFF), RD32(d->base + page + (en & 0xFFF)) + delta); }
            p += blk;
        }
    }
    return code_crc(d);
}
uint32_t mod_load(CPU *c, const char *name) {
    int k = find_by_name(name); if (k < 0) return 0;
    const RecompModDesc *d = recomp_mods[k];
    if (ieq(d->name, "fmsel.dll")) { port_log("%s (the fan-mission selector, a GDI/shell user interface) is not loaded by the portable host", name); return 0; }
    if (st[k].refs) { st[k].refs++; port_debug("%s: LoadLibrary again (references %d, thread %u%s)", d->name, st[k].refs, cur_thread_id(), st[k].unloading ? ", during its unload" : ""); return d->base; }
    char hp[1200]; vfs_map(name, hp, sizeof hp); long n; uint8_t *file = slurp(hp, &n);
    if (!file) { port_log("%s: could not be opened (%s)", name, hp); return 0; }
    uint32_t lf = n >= 0x40 ? *(uint32_t *)(file + 0x3c) : 0;
    if (n < 0x40 || (uint64_t)lf + 24 + 0x60 + 8 * 16 > (uint64_t)n || memcmp(file + lf, "PE\0\0", 4)) {
        port_log("%s (%s) is not a Windows module (%ld bytes); refused", name, hp, n); free(file); return 0;
    }
    const uint8_t *nt = file + lf;
    uint32_t image_base = *(const uint32_t *)(nt + 24 + 28), size_of_image = *(const uint32_t *)(nt + 24 + 56), entry_rva = *(const uint32_t *)(nt + 24 + 16);
    /* every version of this module built in whose header identity the file has; then its code CRC at that version's base */
    int nver = 0, header_match = 0, ntried = 0, tried[16]; uint32_t crcs[16]; k = -1;
    for (int i = 0; recomp_mods[i] && i < 16; i++) nver += is_named(i, name);
    for (int i = 0; recomp_mods[i] && i < 16; i++) {
        if (!is_named(i, name)) continue;
        const RecompModDesc *v = recomp_mods[i];
        if (size_of_image != v->size || image_base != v->file_base || entry_rva != v->entry - v->base) continue;
        header_match++;
        if (!st[i].reserved) { port_log("%s: version %s has this header, but its range %08x-%08x could not be reserved", name, variant(v), v->base, v->base + v->size); continue; }
        uint32_t crc = map_image(v, file, n); tried[ntried] = i; crcs[ntried++] = crc;
        if (crc == v->crc) { k = i; break; }
        memset(GP(v->base), 0, v->size);
    }
    free(file);
    if (k < 0) {
        port_log("%s (%s): SizeOfImage 0x%x, ImageBase %08x, entry +0x%x. It is none of the %d recompiled version%s of %s, so it is refused; "
                 "no other version is used in its place:", name, hp, size_of_image, image_base, entry_rva, nver, nver == 1 ? "" : "s", recomp_mods[find_by_name(name)]->name);
        for (int i = 0; recomp_mods[i] && i < 16; i++) {
            if (!is_named(i, name)) continue;
            const RecompModDesc *v = recomp_mods[i]; char got[64] = "";
            for (int t = 0; t < ntried; t++) if (tried[t] == i) snprintf(got, sizeof got, "; this file's code CRC there: %08x", crcs[t]);
            port_log("  version %s: SizeOfImage 0x%x, ImageBase %08x, entry +0x%x, code CRC %08x at %08x%s", variant(v), v->size, v->file_base, v->entry - v->base, v->crc, v->base, got);
        }
        port_log("%s is a different version than the one that was recompiled (%s; a mod's copy?)", name, header_match ? "code checksum differs" : "its size, base or entry point differs");
        return 0;
    }
    d = recomp_mods[k];
    uint32_t ntg = d->base + RD32(d->base + 0x3c), dir = ntg + 24 + 96;
    if (nver > 1) port_log("%s (%s): identified as recompiled version %s (SizeOfImage 0x%x, ImageBase %08x, code CRC %08x at %08x), one of %d built in",
                           name, hp, variant(d), d->size, d->file_base, d->crc, d->base, nver);
    uint32_t irva = RD32(dir + 8), nb = 0;
    if (irva) for (uint32_t p = d->base + irva; RD32(p + 12); p += 20) {
        const char *dll = (const char *)GP(d->base + RD32(p + 12)); uint32_t ot = RD32(p) ? RD32(p) : RD32(p + 16), ft = RD32(p + 16);
        for (uint32_t i = 0; RD32(d->base + ot + 4 * i); i++, nb++) {
            uint32_t e = RD32(d->base + ot + 4 * i), a;
            if (e & 0x80000000u) { char on[32]; snprintf(on, sizeof on, "#%u", e & 0xFFFF); a = import_thunk(dll, on); } else a = import_thunk(dll, (const char *)GP(d->base + e + 2));
            WR32(d->base + ft + 4 * i, a);
        }
    }
    if (!tls_attach(k, d, dir)) { memset(GP(d->base), 0, d->size); return 0; }
    st[k].refs = 1;
    uint32_t ok = g_call(c, d->entry, 3, d->base, 1u, 0u);
    port_log("%s: loaded the recompiled module at %08x (%u imports bound), DllMain -> %u", d->name, d->base, nb, ok);
    if (!ok) { st[k].refs = 0; tls_detach(k); memset(GP(d->base), 0, d->size); return 0; }
    return d->base;
}
int mod_free(CPU *c, uint32_t h) {
    int k = find_by_handle(h);
    if (k < 0) { for (int i = 0; recomp_mods[i] && i < 16; i++) if (recomp_mods[i]->base == h) { port_log("%s: FreeLibrary on a module that is not loaded (thread %u)", recomp_mods[i]->name, cur_thread_id()); return 1; } return 0; }
    port_debug("%s: FreeLibrary (references %d, thread %u%s)", recomp_mods[k]->name, st[k].refs, cur_thread_id(), st[k].unloading ? ", during its own unload" : "");
    if (st[k].refs > 1) { st[k].refs--; return 1; }
    if (st[k].unloading) return 1;                          /* FreeLibrary again from inside DllMain(DETACH): already going away */
    st[k].unloading = 1;
    uint32_t r = g_call(c, recomp_mods[k]->entry, 3, h, 0u, 0u);   /* DllMain(DLL_PROCESS_DETACH) while the module still counts as loaded: its code must stay callable */
    st[k].unloading = 0; st[k].refs = 0; tls_detach(k); memset(GP(recomp_mods[k]->base), 0, recomp_mods[k]->size);
    port_debug("%s: unloaded (DllMain -> %u)", recomp_mods[k]->name, r);
    return 1;
}
const char *mod_describe(uint32_t t, char *buf, size_t n) {   /* for error messages: which module an address belongs to, and its state */
    for (int i = 0; recomp_mods[i] && i < 16; i++) {
        const RecompModDesc *d = recomp_mods[i]; if (t < d->base || t >= d->base + d->size) continue;
        snprintf(buf, n, " (inside %s: %s, references %d%s, %s)", d->name, st[i].refs ? "loaded" : "NOT loaded", st[i].refs,
                 st[i].unloading ? ", unloading" : "", d->lookup(t) ? "a recompiled function" : "not a recompiled function entry");
        return buf;
    }
    return "";
}
/* A script module's ScriptModuleInit(name, IScriptMan *, MPrintfProc, IMalloc *, IScriptModule **): the engine's script
 * manager is noted (core/ss2game.c finds the documented script services through it), then the module's own runs. */
static void sh_script_module_init(CPU *c) {
    uint32_t real = g_targ, a[5] = { A(0), A(1), A(2), A(3), A(4) };
    ss2g_script_man(A(1)); RET(g_callv(c, real, 5, a));
}
uint32_t mod_export(uint32_t h, const char *name, int *is_mod) {
    int k = find_by_handle(h); *is_mod = k >= 0; if (k < 0) return 0;
    uint32_t b = recomp_mods[k]->base, nt = b + RD32(b + 0x3c), erva = RD32(nt + 24 + 96); if (!erva || !name) return 0;
    uint32_t ex = b + erva, nfn = RD32(ex + 20), nnm = RD32(ex + 24), fn = b + RD32(ex + 28), nm = b + RD32(ex + 32), ord = b + RD32(ex + 36);
    for (uint32_t i = 0; i < nnm; i++) if (!strcmp((const char *)GP(b + RD32(nm + 4 * i)), name)) {
        uint32_t o = RD16(ord + 2 * i), a = o < nfn ? b + RD32(fn + 4 * o) : 0;
        if (a && !strcmp(name, "_ScriptModuleInit@20")) return g_thunk_arg(sh_script_module_init, STD(5), "ScriptModuleInit (notes the script manager)", a);
        return a;
    }
    return 0;
}
/* Calls into a module from outside it (the engine calling a script's methods): the first one is logged, so a log shows
 * that a module's code runs, not only that it loaded. SS2PORT_MODSTATS=1 also logs each module's count every 20 s. */
static uint64_t calls[16], calls_logged[16]; static int first_logged[16];
static void mod_stats(void) {
    static int on = -1; static uint64_t next; if (on < 0) on = getenv("SS2PORT_MODSTATS") && atoi(getenv("SS2PORT_MODSTATS"));
    if (!on) return;
    uint64_t now = plat_time_ns(); if (!next) next = now + 20000000000ull; if (now < next) return; next = now + 20000000000ull;
    for (int i = 0; recomp_mods[i] && i < 16; i++) if (calls[i] != calls_logged[i]) {
        port_log("%s: %llu calls from outside it in the last 20 s (%llu in all)", recomp_mods[i]->name, (unsigned long long)(calls[i] - calls_logged[i]), (unsigned long long)calls[i]);
        calls_logged[i] = calls[i];
    }
}
int mod_call(CPU *c, uint32_t t) {
    for (int i = 0; recomp_mods[i] && i < 16; i++) {
        const RecompModDesc *d = recomp_mods[i]; if (!st[i].refs || t < d->base || t >= d->base + d->size) continue;
        guest_fn f = d->lookup(t);
        if (f) {
            if (!first_logged[i]) { first_logged[i] = 1; port_log("%s: first call into its code (%08x, thread %u)", d->name, t, cur_thread_id()); }
            calls[i]++; mod_stats(); f(c); return 1;
        }
    }
    return 0;
}
