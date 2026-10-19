/* ss2game.c: System Shock 2 game state and commands for the touch radials (ss2game.h).
 *
 * How the game is reached. A script module's ScriptModuleInit gets the engine's script manager (IScriptMan), through
 * which the documented script services are found (NewDark's doc/squirrel_script/API-reference_services.txt: ShockGame,
 * Debug). The engine's own systems are found the way its own commands find them: QueryInterface on the application
 * aggregate. Everything this file calls, it first checks against SS2.exe 2.48: interface IDs byte for byte, the vtable
 * each interface has, the first bytes of each engine function, the names in the engine's weapon table. Any difference
 * turns the whole file off (logged once); the touch controls then keep their plain tap actions.
 *
 * What it reads (never writes): the player's inventory (IContainSys iteration, as the game's equip_weapon and use_obj
 * commands do), archetypes and their names (ITraitManager, IObjectSystem), the equipped weapon (IShockPlayer::GetEquip,
 * ShockGame.Equipped's call), whether the player can equip a weapon (the check IShockPlayer::Equip itself makes first,
 * asked with no message), the gun in hand (ShockGame.PlayerGun's call), properties (ObjState, StackCount,
 * ShockWeaponType, PlayerGunDesc, GunState, PsiPowerDesc/PsiPower2Desc, PsiState), which ammunition a gun takes (the
 * engine's own test, used by cycle_ammo and ShockGame.AmmoLoad), and strings from the game's own string tables.
 *
 * What it does, only when the player picks a radial entry, each the game's own way:
 *   weapon        equip_weapon <archetype>            (the command the number keys are bound to)
 *   ammunition    the load cycle_ammo performs, with the chosen inventory clip (same function, same flags)
 *   setting       wpn_setting_toggle                  (the command the HUD's SETTING button runs)
 *   psi power     ShockGame.SetSelectedPsiPower       (selects, never activates)
 *   item          use_obj <archetype>                 (the command "h" and "p" are bound to)
 *   system        play_unread_log, stop_email, quicksave, quickload, select_psipower
 * Commands run through the Debug script service's Command (the engine's command interpreter, with its own checks).
 * SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "core.h"
#include "ss2game.h"

/* ---------------------------------------------------------------- SS2.exe NewDark 2.48 (sha256 c7ff08c5...) */
#define APP_AGGREGATE    0x009ff87cu   /* the engine's aggregate (IUnknown *); its QueryInterface finds every system */
#define PLAYER_OBJ       0x00a3acb4u   /* the player's ObjID (0 outside a game) */
#define PLAYER_MODE_PTR  0x00a3ace0u   /* -> player state; +0x24 == 7: dead (equip_weapon, use_obj and cycle_ammo do nothing then) */
#define FRAME_PUMP_RET   0x006a82efu   /* PeekMessageA returns here in the engine's per-frame event pump (0x6a8210) */
#define WEAPON_TABLE     0x008235acu   /* the 14 names cycle_weapon steps through ("psi amp", "wrench", ...): wheel order */
#define CRT_FREE_IAT     0x00725338u   /* free(), as the engine frees strings it got from the object-string table */
#define STR_EMPTY        0x00a4654fu   /* the engine's shared empty string (never freed) */
#define LOG_PROPS        0x00879a60u   /* 9 property interfaces, one per deck: the player's logs of that deck (+4: a bit per log) */
#define LOG_READ         0x0083b96cu   /* the engine's "log read" flags, 4 bytes per log, 160 entries per deck (play_unread_log, 0x488330) */
enum { IID_CONTAIN = 0x0076ff40u, IID_TRAIT = 0x007678e8u, IID_OBJSYS = 0x007665acu, IID_PLAYER = 0x0074e24cu,
       IID_GUN = 0x0074f54cu, IID_PSI = 0x0075117cu, IID_OBJSTR = 0x00743050u,
       IID_SRV_SHOCKGAME = 0x0073c354u, IID_SRV_DEBUG = 0x0073c8c0u };
static const struct { uint32_t va; unsigned n; const char *what; } guids[] = {
    { IID_CONTAIN, 0x148, "IContainSys" }, { IID_TRAIT, 0x8f, "ITraitManager" }, { IID_OBJSYS, 0x90, "IObjectSystem" },
    { IID_PLAYER, 0x1af, "player" }, { IID_GUN, 0x18d, "player gun" }, { IID_PSI, 0x1d3, "player psi" }, { IID_OBJSTR, 0x194, "object strings" },
    { IID_SRV_SHOCKGAME, 0x108, "ShockGame service" }, { IID_SRV_DEBUG, 0xd7, "Debug service" },
};
/* the vtable each interface has in this executable (another one: another engine; off) */
enum { VT_CONTAIN = 0x0076fffcu, VT_TRAIT = 0x00767954u, VT_OBJSYS = 0x007658ccu, VT_PLAYER = 0x0074e2e4u, VT_GUN = 0x0074f61cu,
       VT_PSI = 0x007511f4u, VT_OBJSTR = 0x007430d0u, VT_SCRIPTMAN = 0x0077f1b0u, VT_SHOCKGAME = 0x00752c84u, VT_DEBUG = 0x0073ce2cu };
/* slots (4-byte vtable entries) */
enum { S_RELEASE = 2,
       S_CONTAIN_ITER_START = 20, S_CONTAIN_ITER_NEXT = 24, S_CONTAIN_ITER_END = 25,       /* sContainIter: +0 done, +4 type, +12 object */
       S_TRAIT_ARCHETYPE = 6, S_TRAIT_INHERITS = 16,
       S_OBJSYS_NAME = 11, S_OBJSYS_NAMED = 12,
       S_PLAYER_GET_EQUIP = 16, S_PLAYER_CAN_EQUIP = 20,
       S_GUN_GET = 8, S_OBJSTR_GET = 5, S_PROP_GET = 20,
       S_SCRIPTMAN_GET_SERVICE = 12, S_DEBUG_COMMAND = 6,
       S_SG_GET_PSI_POINTS = 9, S_SG_GRANT_PSI = 72, S_SG_SET_SELECTED_PSI = 101 };
/* properties: the engine keeps each property's interface in a global */
enum { P_OBJSTATE = 0x00879ae8u, P_STACKCOUNT = 0x0093d370u, P_WEAPONTYPE = 0x0093d3c4u, P_PLAYERGUNDESC = 0x0093d3acu,
       P_GUNSTATE = 0x0093d3bcu, P_PSIPOWER = 0x00879a5cu, P_PSIPOWER2 = 0x00879a90u, P_PSISTATE = 0x00879d08u };
/* engine functions called directly (no script service offers them); their first bytes are checked */
enum { F_AMMO_PROJECTILE = 0x0045a8b0u,  /* ecx = gun, (obj): the projectile this object loads into this gun, 0: none */
       F_GUN_PROJECTILE = 0x005c2e90u,   /* (gun): the projectile the gun is loaded with */
       F_LOAD_AMMO = 0x0045ac40u,        /* eax = gun, (clip, flags): cycle_ammo's load; flags 2 = from inventory */
       F_GET_STRING = 0x004b0590u,       /* ecx = key, edx = table, (buf, size, index): a string-table entry */
       F_QUICKSAVE_SLOT = 0x00472f90u }; /* (): the slot quicksave/quickload use (shock_quicksave_slot), -1: the default */
static const struct { uint32_t va; uint8_t b[8]; } fn_bytes[] = {
    { F_AMMO_PROJECTILE, { 0x6a, 0xff, 0x68, 0x58, 0x0f, 0x71, 0x00, 0x64 } }, { F_GUN_PROJECTILE, { 0xa1, 0x90, 0xb0, 0xa3, 0x00, 0x8b, 0x08, 0x8b } },
    { F_LOAD_AMMO, { 0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec } }, { F_GET_STRING, { 0x6a, 0xff, 0x68, 0xb1, 0xa4, 0x70, 0x00, 0x64 } },
    { F_QUICKSAVE_SLOT, { 0x83, 0xec, 0x08, 0x8d, 0x44, 0x24, 0x04, 0x50 } },
};
static const char *const weapon_table_names[14] = { "psi amp", "wrench", "pistol", "shotgun", "assault rifle", "laser pistol", "EMP Rifle",
    "Electro Shock", "Gren Launcher", "Stasis Field Generator", "Fusion Cannon", "Crystal Shard", "Viral Prolif", "Worm Launcher" };
#define PSI_NONE 40                     /* PsiState's "nothing selected"; powers 0, 8, 16, 24, 32 are the tiers' neural capacity */

static int lg_guid_at(uint32_t va, unsigned n) {
    static const uint8_t tail[8] = { 0x83, 0x48, 0x00, 0xaa, 0x00, 0xa8, 0x2b, 0x51 };
    if (!g_valid(va, 16)) return 0;
    uint32_t d1 = ((n & 0xffu) << 24) | ((n & 0xff00u) << 16) | (n & 0xffffu);
    return RD32(va) == d1 && RD16(va + 4) == (uint16_t)(0x7a80 + n) && RD16(va + 6) == (uint16_t)(0x11cf + n) && !memcmp(GP(va + 8), tail, 8);
}

/* ---------------------------------------------------------------- state */
static uint32_t script_man;
static int ok = -1;                     /* -1 not checked yet, 0 off, 1 checked */
static int debug = -1;                  /* SS2PORT_GAME_DEBUG=1: log each read and action */
static uint32_t scratch;                /* 4 KB of guest memory for arguments and results */
static CPU *cpu;                        /* set while running at the frame pump */
static int busy;

static void off(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void off(const char *fmt, ...) {
    char b[300]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (ok != 0) port_log("[ss2game] off: %s; the touch radials only offer their plain actions", b);
    ok = 0;
}
void ss2g_script_man(uint32_t sm) {
    if (sm && sm != script_man) port_log("[ss2game] script manager %08x (vtable %08x)", sm, g_valid(sm, 4) ? RD32(sm) : 0);
    if (sm) script_man = sm;
}
static int check_static(void) {
    if (ok >= 0) return ok;
    for (size_t i = 0; i < sizeof guids / sizeof *guids; i++)
        if (!lg_guid_at(guids[i].va, guids[i].n)) { off("the %s ID is not at %08x (not SS2.exe 2.48?)", guids[i].what, guids[i].va); return 0; }
    for (size_t i = 0; i < sizeof fn_bytes / sizeof *fn_bytes; i++)
        if (!g_valid(fn_bytes[i].va, 8) || memcmp(GP(fn_bytes[i].va), fn_bytes[i].b, 8)) { off("engine function %08x differs", fn_bytes[i].va); return 0; }
    for (int i = 0; i < 14; i++) {
        uint32_t p = RD32(WEAPON_TABLE + 4u * (uint32_t)i);
        if (!g_valid(p, 1) || strcmp(gs(p), weapon_table_names[i])) { off("weapon table entry %d is not '%s'", i, weapon_table_names[i]); return 0; }
    }
    if (!(scratch = g_alloc(4096))) { off("no guest memory"); return 0; }
    ok = 1; port_log("[ss2game] SS2.exe 2.48 interfaces, functions and weapon table found");
    return 1;
}
int ss2g_available(void) { return ok != 0; }

/* ---------------------------------------------------------------- calling the engine (game thread, inside a shim) */
static uint32_t vcall(uint32_t obj, unsigned slot, int n, ...) {     /* COM method: stdcall, this first */
    uint32_t a[12]; va_list ap; va_start(ap, n); a[0] = obj;
    for (int i = 0; i < n && i < 11; i++) a[1 + i] = va_arg(ap, uint32_t);
    va_end(ap);
    return g_callv(cpu, RD32(RD32(obj) + 4 * slot), n + 1, a);
}
static uint32_t fcall(uint32_t fn, uint32_t eax, uint32_t ecx, uint32_t edx, int n, ...) {   /* cdecl, some arguments in registers */
    uint32_t a[8]; va_list ap; va_start(ap, n);
    for (int i = 0; i < n && i < 8; i++) a[i] = va_arg(ap, uint32_t);
    va_end(ap);
    cpu->eax = eax; cpu->ecx = ecx; cpu->edx = edx;
    return g_callv(cpu, fn, n, a);
}
static uint32_t qi(uint32_t iid, uint32_t vt) {
    uint32_t agg = RD32(APP_AGGREGATE); if (!agg || !g_valid(agg, 4) || !g_valid(RD32(agg), 4)) return 0;
    WR32(scratch, 0);
    uint32_t p = vcall(agg, 0, 2, iid, scratch) == 0 ? RD32(scratch) : 0;
    if (p && !vt) { static int logged; if (!logged++) port_log("[ss2game] interface %08x: vtable %08x", iid, RD32(p)); }
    else if (p && RD32(p) != vt) { off("interface %08x has vtable %08x, expected %08x", iid, RD32(p), vt); vcall(p, S_RELEASE, 0); return 0; }
    return p;
}
static void release(uint32_t p) { if (p) vcall(p, S_RELEASE, 0); }
static uint32_t service(uint32_t iid, uint32_t vt) {             /* IScriptMan::GetService (AddRef'd) */
    if (!script_man || !g_valid(script_man, 4)) return 0;
    if (RD32(script_man) != VT_SCRIPTMAN) { off("the script manager has vtable %08x", RD32(script_man)); return 0; }
    uint32_t p = vcall(script_man, S_SCRIPTMAN_GET_SERVICE, 1, iid);
    if (p && RD32(p) != vt) { off("service %08x has vtable %08x, expected %08x", iid, RD32(p), vt); release(p); return 0; }
    return p;
}
/* scratch layout */
enum { SC_OUT = 0x000, SC_PROP = 0x010, SC_STR = 0x020, SC_BUF = 0x100, SC_NAME = 0x300, SC_CMD = 0x400, SC_ARG = 0x500, SC_SLOTS = 0x600 };
static uint32_t gstr(uint32_t at, const char *s) {                /* a C string in scratch memory */
    size_t n = strlen(s); if (n > 255) n = 255;
    memcpy(GP(scratch + at), s, n); WR8(scratch + at + (uint32_t)n, 0); return scratch + at;
}

typedef struct { uint32_t contain, trait, objsys, player, gun, psi, objstr; } Ifs;
static int ifs_get(Ifs *f) {
    memset(f, 0, sizeof *f);
    f->contain = qi(IID_CONTAIN, VT_CONTAIN); f->trait = qi(IID_TRAIT, VT_TRAIT); f->objsys = qi(IID_OBJSYS, VT_OBJSYS);
    f->player = qi(IID_PLAYER, VT_PLAYER); f->gun = qi(IID_GUN, VT_GUN); f->psi = qi(IID_PSI, VT_PSI); f->objstr = qi(IID_OBJSTR, VT_OBJSTR);
    return ok == 1 && f->contain && f->trait && f->objsys && f->player && f->gun && f->psi && f->objstr;
}
static void ifs_put(Ifs *f) { release(f->contain); release(f->trait); release(f->objsys); release(f->player); release(f->gun); release(f->psi); release(f->objstr); }

static int engine_code(uint32_t a) { return a >= 0x00401000u && a < 0x00723000u; }   /* SS2.exe's .text */
static int prop_ok(uint32_t global) {
    uint32_t p = RD32(global);
    return p && g_valid(p, 4) && g_valid(RD32(p), 4 * (S_PROP_GET + 1)) && engine_code(RD32(RD32(p) + 4 * S_PROP_GET));
}
static int prop_get(uint32_t global, uint32_t obj, uint32_t *v) {   /* IProperty::Get: an int, or a pointer to the property's data */
    *v = 0; if (!prop_ok(global)) return 0;
    WR32(scratch + SC_PROP, 0);
    int r = vcall(RD32(global), S_PROP_GET, 2, obj, scratch + SC_PROP) != 0;
    *v = RD32(scratch + SC_PROP); return r;
}
static const char *name_of(const Ifs *f, uint32_t obj) {          /* an archetype's name ("Pistol"); "" for unnamed objects */
    uint32_t s = vcall(f->objsys, S_OBJSYS_NAME, 1, obj); return s && g_valid(s, 1) ? (const char *)GP(s) : "";
}
static uint32_t named(const Ifs *f, const char *name) { return vcall(f->objsys, S_OBJSYS_NAMED, 1, gstr(SC_NAME, name)); }
static uint32_t archetype(const Ifs *f, uint32_t obj) { return vcall(f->trait, S_TRAIT_ARCHETYPE, 1, obj); }
static int inherits(const Ifs *f, uint32_t obj, uint32_t arch) { return arch && vcall(f->trait, S_TRAIT_INHERITS, 2, obj, arch) != 0; }
/* the object-string table (strings\<table>.str by the object's string name), as the HUD reads "SHead1" */
static void objstr(const Ifs *f, uint32_t obj, const char *table, char *out, size_t n) {
    out[0] = 0;
    memset(GP(scratch + SC_STR), 0, 16);
    uint32_t r = vcall(f->objstr, S_OBJSTR_GET, 3, scratch + SC_STR, obj, gstr(SC_NAME, table));
    uint32_t s = r == scratch + SC_STR ? RD32(scratch + SC_STR) : 0;
    if (s && s != STR_EMPTY && g_valid(s, 1)) {
        snprintf(out, n, "%s", gs(s));
        uint32_t fr = RD32(CRT_FREE_IAT); if (fr) g_callv(cpu, fr, 1, &s);
    }
}
/* a string-table entry (strings\<table>.str), as the engine reads its own messages */
static void game_string(const char *table, const char *key, int index, char *out, size_t n) {
    out[0] = 0; WR8(scratch + SC_BUF, 0);
    uint32_t k = gstr(SC_CMD, key), t = gstr(SC_ARG, table);
    fcall(F_GET_STRING, 0, k, t, 3, scratch + SC_BUF, 255u, (uint32_t)index);
    WR8(scratch + SC_BUF + 255, 0); snprintf(out, n, "%s", gs(scratch + SC_BUF));
}
static int command(const char *cmd, const char *arg) {            /* Debug.Command: the engine's command interpreter */
    uint32_t dbg = service(IID_SRV_DEBUG, VT_DEBUG); if (!dbg) return 0;
    uint32_t c = gstr(SC_CMD, cmd), a = gstr(SC_ARG, arg ? arg : ""), e = gstr(SC_NAME, "");
    uint32_t slots = scratch + SC_SLOTS;                         /* cScrStr is passed by reference: the address of a char * */
    WR32(slots, c); WR32(slots + 4, a); for (int i = 2; i < 8; i++) WR32(slots + 4u * (uint32_t)i, e);
    vcall(dbg, S_DEBUG_COMMAND, 8, slots, slots + 4, slots + 8, slots + 12, slots + 16, slots + 20, slots + 24, slots + 28);
    release(dbg);
    if (debug) port_log("[ss2game] command: %s %s", cmd, arg ? arg : "");
    return 1;
}
static void upper(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }
static void short_label(char *out, size_t n, const char *s) {     /* "%d standard bullets" -> "STANDARD BULLETS" */
    for (;;) { if (s[0] == '%' && s[1] == 'd') s += 2; else if (*s == ' ') s++; else break; }
    snprintf(out, n, "%s", s); upper(out);
}

/* ---------------------------------------------------------------- the game state */
typedef struct { uint32_t arch; int rank, count, current, can; char cmd_name[64], label[24], name[64]; } Weap;
typedef struct { uint32_t proj, obj; int count; char label[24], name[64]; } AmmoType;
typedef struct { uint32_t arch; int count; char cmd_name[64], label[24], name[64]; } Item;
static struct {
    int valid; uint64_t time; uint32_t player; int dead;
    int nweap; Weap weap[SS2G_MAX]; uint32_t weapon;               /* weapon: what the weapon slot holds */
    uint32_t gun; int gun_type; char gun_name[64];                 /* gun: the gun in hand (0: none, or a melee weapon) */
    uint32_t proj; int nammo; AmmoType ammo[10];
    int setting, nsettings; char setting_name[2][24];
    uint32_t psi_known[2]; int psi_selected, psi_points;
    int nitem; Item item[SS2G_MAX];
    int quick_slot, quick_exists;
    int unread_logs;                                               /* what play_unread_log would choose from */
} st;
static int want_refresh;

/* short wheel labels for the psi powers (strings\psihelp.str "Psi<n>" has the full names, shown as the wheel's title) */
static const char *const psi_short[40] = {
    "", "SCREEN", "DAMPENING", "KINETIC", "AGILITY", "CYBER", "CRYOKINESIS", "TAMPERING",
    "", "ANTI-ENTROPY", "ADRENALINE", "DECONTAM", "REGENERATE", "STRENGTH", "AMPLIFY", "LOCAL PYRO",
    "", "DUPLICATE", "CASCADE", "REFLECT", "TOXIN BLOCK", "MOTION SENSE", "PYROKINESIS", "HYPNOSIS",
    "", "PHOTONIC", "DETECTION", "SUPPRESSION", "ENDURANCE", "TRANSMUTE", "CIRCUITRY", "EXTENSION",
    "", "ADV REGEN", "SOMA", "TELEPORT", "RESTRUCTURE", "BARRIER", "DETONATION", "AURA" };
static char psi_full[40][64];           /* read once from the game's psihelp table */
static int psi_known(int p) { return p > 0 && p < PSI_NONE && (p % 8) && ((st.psi_known[p / 32] >> (p % 32)) & 1); }

static void read_state(void) {
    Ifs f; uint32_t pl = RD32(PLAYER_OBJ);
    memset(&st, 0, sizeof st); st.psi_selected = PSI_NONE;
    if (!pl) return;                                                /* no game running (menus) */
    if (!ifs_get(&f)) { ifs_put(&f); return; }
    st.player = pl;
    uint32_t mode = RD32(PLAYER_MODE_PTR); st.dead = mode && g_valid(mode + 0x24, 4) && RD32(mode + 0x24) == 7;
    uint32_t a_weapon = named(&f, "Weapon"), a_patches = named(&f, "Patches"), table_arch[14];
    for (int i = 0; i < 14; i++) table_arch[i] = named(&f, weapon_table_names[i]);
    st.weapon = vcall(f.player, S_PLAYER_GET_EQUIP, 2, pl, 0u);
    st.gun = vcall(f.gun, S_GUN_GET, 0);
    uint32_t v;
    if (st.gun) {
        st.gun_type = prop_get(P_WEAPONTYPE, st.gun, &v) ? (int)v : -1;
        objstr(&f, st.gun, "objshort", st.gun_name, sizeof st.gun_name);
        if (!st.gun_name[0]) snprintf(st.gun_name, sizeof st.gun_name, "%s", name_of(&f, archetype(&f, st.gun)));
        if (st.gun_type != 4) {
            st.proj = fcall(F_GUN_PROJECTILE, 0, 0, 0, 1, st.gun);
            if (prop_get(P_GUNSTATE, st.gun, &v) && v && g_valid(v + 8, 4)) st.setting = (int)RD32(v + 8);
            char a[24], b[24]; objstr(&f, st.gun, "SHead1", a, sizeof a); objstr(&f, st.gun, "SHead2", b, sizeof b);
            if (a[0] && b[0]) { st.nsettings = 2; snprintf(st.setting_name[0], 24, "%s", a); snprintf(st.setting_name[1], 24, "%s", b); upper(st.setting_name[0]); upper(st.setting_name[1]); }
        }
    }
    /* psi */
    if (prop_get(P_PSIPOWER, pl, &v)) st.psi_known[0] = v;
    if (prop_get(P_PSIPOWER2, pl, &v)) st.psi_known[1] = v;
    if (prop_get(P_PSISTATE, pl, &v) && v && g_valid(v, 4)) st.psi_selected = (int)RD32(v);
    uint32_t sg = service(IID_SRV_SHOCKGAME, VT_SHOCKGAME);
    if (sg) { st.psi_points = (int)vcall(sg, S_SG_GET_PSI_POINTS, 0); release(sg); }
    if (!psi_full[1][0]) for (int p = 1; p < PSI_NONE; p++) if (p % 8) {
        char s[256]; game_string("psihelp", "Psi", p, s, sizeof s);
        char *nl = strpbrk(s, "\r\n"); if (nl) *nl = 0;
        snprintf(psi_full[p], sizeof psi_full[p], "%s", s[0] ? s : psi_short[p]); upper(psi_full[p]);
    }
    /* the inventory, in the order the game holds it */
    uint32_t it = vcall(f.contain, S_CONTAIN_ITER_START, 1, pl); int guard = 0;
    while (it && g_valid(it, 16) && !RD32(it) && guard++ < 400) {
        uint32_t o = RD32(it + 12), arch = archetype(&f, o);
        if (inherits(&f, o, a_weapon)) {
            int k = 0; while (k < st.nweap && st.weap[k].arch != arch) k++;
            if (k == st.nweap && st.nweap < SS2G_MAX) {
                Weap *w = &st.weap[st.nweap++]; memset(w, 0, sizeof *w); w->arch = arch;
                snprintf(w->cmd_name, sizeof w->cmd_name, "%s", name_of(&f, arch));
                w->rank = 100; for (int i = 0; i < 14; i++) if (inherits(&f, o, table_arch[i])) { w->rank = i; break; }
                objstr(&f, o, "objshort", w->name, sizeof w->name); if (!w->name[0]) snprintf(w->name, sizeof w->name, "%s", w->cmd_name);
                upper(w->name); short_label(w->label, sizeof w->label, w->name);
            }
            if (k < SS2G_MAX) {
                Weap *w = &st.weap[k]; w->count++;
                if (o == st.weapon) w->current = 1;
                uint32_t state = 0; prop_get(P_OBJSTATE, o, &state);   /* 0 usable; broken or unresearched otherwise */
                if (!state && vcall(f.player, S_PLAYER_CAN_EQUIP, 2, o, 0u)) w->can = 1;
            }
        } else if (inherits(&f, o, a_patches)) {
            int k = 0; while (k < st.nitem && st.item[k].arch != arch) k++;
            if (k == st.nitem && st.nitem < SS2G_MAX) {
                Item *m = &st.item[st.nitem++]; memset(m, 0, sizeof *m); m->arch = arch;
                snprintf(m->cmd_name, sizeof m->cmd_name, "%s", name_of(&f, arch));
                objstr(&f, o, "objshort", m->name, sizeof m->name); if (!m->name[0]) snprintf(m->name, sizeof m->name, "%s", m->cmd_name);
                upper(m->name); short_label(m->label, sizeof m->label, m->name);
            }
            if (k < SS2G_MAX) st.item[k].count += prop_get(P_STACKCOUNT, o, &v) && (int)v > 0 ? (int)v : 1;
        }
        if (st.gun && st.gun_type != 4 && !prop_get(P_PLAYERGUNDESC, o, &v)) {   /* not a gun itself: does it load into this one? */
            uint32_t proj = fcall(F_AMMO_PROJECTILE, 0, st.gun, 0, 1, o);
            if (proj) {
                int k = 0; while (k < st.nammo && st.ammo[k].proj != proj) k++;
                if (k == st.nammo && st.nammo < 10) {
                    AmmoType *t = &st.ammo[st.nammo++]; memset(t, 0, sizeof *t); t->proj = proj; t->obj = o;
                    char s[64]; objstr(&f, proj, "objshort", s, sizeof s); short_label(t->label, sizeof t->label, s[0] ? s : name_of(&f, proj));
                    objstr(&f, o, "objshort", s, sizeof s); short_label(t->name, sizeof t->name, s[0] ? s : name_of(&f, archetype(&f, o)));
                }
                if (k < 10) st.ammo[k].count += prop_get(P_STACKCOUNT, o, &v) && (int)v > 0 ? (int)v : 1;
            }
        }
        vcall(f.contain, S_CONTAIN_ITER_NEXT, 1, it);
    }
    if (it) vcall(f.contain, S_CONTAIN_ITER_END, 1, it);
    /* weapons in the order cycle_weapon steps through them, others after */
    for (int i = 1; i < st.nweap; i++) for (int j = i; j > 0 && st.weap[j].rank < st.weap[j - 1].rank; j--) { Weap t = st.weap[j]; st.weap[j] = st.weap[j - 1]; st.weap[j - 1] = t; }
    /* unread logs, counted as play_unread_log selects them: the deck's log bits, not yet marked read */
    for (uint32_t d = 0; d < 9; d++) {
        uint32_t data = 0;
        if (!prop_get(LOG_PROPS + 4 * d, pl, &data) || !data || !g_valid(data + 4, 4)) continue;
        uint32_t mask = RD32(data + 4);
        for (uint32_t i = 0; i < 32; i++) if (((mask >> i) & 1) && !RD32(LOG_READ + 4 * (160 * d + i))) st.unread_logs++;
    }
    /* quickload: only when the quicksave slot holds a save */
    st.quick_slot = (int)fcall(F_QUICKSAVE_SLOT, 0, 0, 0, 0);
    if (st.quick_slot >= 0) {
        char w[32], h[1100]; PlatStat ps; snprintf(w, sizeof w, "save_%d", st.quick_slot);
        vfs_map(w, h, sizeof h); st.quick_exists = plat_fs_stat(h, &ps) == PLAT_OK && ps.is_dir;
    } else st.quick_exists = 1;
    ifs_put(&f);
    st.valid = ok == 1; st.time = plat_time_ns();
    if (debug) port_log("[ss2game] state: %d weapons (slot %d), gun %d type %d proj %d setting %d/%d, %d ammo types, psi %08x %08x sel %d pts %d, %d items, quick slot %d %s, %d unread logs",
                        st.nweap, (int)st.weapon, (int)st.gun, st.gun_type, (int)st.proj, st.setting, st.nsettings, st.nammo, st.psi_known[0], st.psi_known[1],
                        st.psi_selected, st.psi_points, st.nitem, st.quick_slot, st.quick_exists ? "exists" : "empty", st.unread_logs);
}

/* ---------------------------------------------------------------- wheels */
static void put(Ss2gEntry *e, int id, const char *label, const char *name, int count, int available, int current) {
    memset(e, 0, sizeof *e); e->id = id; snprintf(e->label, sizeof e->label, "%s", label); snprintf(e->name, sizeof e->name, "%s", name && *name ? name : label);
    upper(e->label); upper(e->name); e->count = count; e->available = (uint8_t)(available != 0); e->current = (uint8_t)(current != 0);
}
int ss2g_wheel(Ss2gWheel w, Ss2gEntry *e, int max, char *title, size_t tn, char *why, size_t wn) {
    int n = 0; if (title && tn) title[0] = 0; if (why && wn) why[0] = 0;
    if (!st.valid) { if (why) snprintf(why, wn, "NOT IN A GAME"); return -1; }
    if (max > SS2G_MAX) max = SS2G_MAX;
    switch (w) {
    case SS2G_WEAPONS:
        if (title) snprintf(title, tn, "WEAPONS");
        for (int i = 0; i < st.nweap && n < max; i++) { const Weap *x = &st.weap[i]; put(&e[n++], (int)-x->arch, x->label, x->name, x->count > 1 ? x->count : -1, x->current || x->can, x->current); }
        if (!n && why) snprintf(why, wn, "NO WEAPONS CARRIED");
        break;
    case SS2G_AMMO:
        if (st.gun && st.gun_type == 4) {                               /* the psi amp: its powers */
            if (title) snprintf(title, tn, "PSI POWERS - %d PSI", st.psi_points);
            for (int p = 1; p < PSI_NONE && n < max; p++) if (psi_known(p)) put(&e[n++], SS2G_PSI(p), psi_short[p], psi_full[p], -1, 1, p == st.psi_selected);
            if (!n && why) snprintf(why, wn, "NO PSI POWERS KNOWN");
            break;
        }
        if (!st.gun) { if (why) snprintf(why, wn, st.weapon ? "NO AMMO FOR THIS WEAPON" : "NO WEAPON"); break; }
        if (title) { snprintf(title, tn, "%s", st.gun_name); upper(title); }
        for (int i = 0; i < st.nammo && n < max; i++) { const AmmoType *t = &st.ammo[i]; put(&e[n++], SS2G_AMMO_TYPE((int)-t->proj), t->label, t->name, t->count, 1, t->proj == st.proj); }
        if (st.nsettings == 2) for (int s = 0; s < 2 && n < max; s++) {
            char nm[64]; snprintf(nm, sizeof nm, "SETTING: %s", st.setting_name[s]);
            put(&e[n++], SS2G_SETTING(s), st.setting_name[s], nm, -1, 1, s == st.setting);
        }
        if (!n && why) snprintf(why, wn, "NO OTHER AMMO");
        break;
    case SS2G_ITEMS:
        if (title) snprintf(title, tn, "ITEMS");
        for (int i = 0; i < st.nitem && n < max; i++) { const Item *m = &st.item[i]; put(&e[n++], (int)-m->arch, m->label, m->name, m->count, m->count > 0 && !st.dead, 0); }
        if (!n && why) snprintf(why, wn, "NO HYPOS OR PATCHES CARRIED");
        break;
    case SS2G_SYSTEM:
        if (title) snprintf(title, tn, "SYSTEM");
        put(&e[n++], SS2G_SYS_QUICKSAVE, "QUICKSAVE", "QUICKSAVE (THE QUICKSAVE SLOT)", -1, !st.dead, 0);
        { char nm[64]; snprintf(nm, sizeof nm, st.unread_logs ? "PLAY AN UNREAD LOG (%d UNREAD)" : "NO UNREAD LOGS", st.unread_logs);
          put(&e[n++], SS2G_SYS_PLAY_LOG, "PLAY LOG", nm, st.unread_logs ? st.unread_logs : -1, !st.dead && st.unread_logs > 0, 0); }
        put(&e[n++], SS2G_SYS_STOP_LOG, "STOP LOG", "STOP THE LOG OR E-MAIL PLAYING", -1, 1, 0);
        put(&e[n++], SS2G_SYS_QUICKLOAD, "QUICKLOAD", st.quick_exists ? "QUICKLOAD (THE QUICKSAVE SLOT)" : "QUICKLOAD: NO QUICKSAVE YET", -1, st.quick_exists, 0);
        if (n < max && ((st.psi_known[0] & ~0x01010101u) || (st.psi_known[1] & 0xfeu))) put(&e[n++], SS2G_SYS_PSI_MENU, "PSI PANEL", "OPEN THE PSI POWER PANEL", -1, !st.dead, 0);
        break;
    default: return -1;
    }
    return n;
}

/* ---------------------------------------------------------------- actions */
typedef struct { Ss2gWheel w; int id; uint32_t gun; } Action;
static Action queue[4]; static int nqueue;
static char message[64]; static int message_new;
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(message, sizeof message, fmt, ap); va_end(ap); message_new = 1;
    port_log("[ss2game] %s", message);
}
int ss2g_message(char *out, size_t n) { if (!message_new) { if (n) out[0] = 0; return 0; } message_new = 0; snprintf(out, n, "%s", message); return 1; }
void ss2g_refresh(void) { if (ok != 0) want_refresh = 1; }
uint64_t ss2g_state_time(void) { return st.valid ? st.time : 0; }
int ss2g_select(Ss2gWheel w, int id) {
    if (ok == 0 || !st.valid || nqueue == 4 || w < 0 || w >= SS2G_NWHEELS) return 0;
    queue[nqueue++] = (Action){ w, id, st.gun }; want_refresh = 1;
    return 1;
}
static const Weap *find_weap(uint32_t arch) { for (int i = 0; i < st.nweap; i++) if (st.weap[i].arch == arch) return &st.weap[i]; return NULL; }
static void run(const Action *a) {
    read_state();                                                   /* the game may have changed since the wheel opened */
    if (!st.valid) { say("NOT AVAILABLE NOW"); return; }
    if (a->w == SS2G_WEAPONS) {
        const Weap *x = find_weap((uint32_t)-a->id);
        if (!x) { say("WEAPON NO LONGER CARRIED"); return; }
        if (x->current) return;
        char label[24], cmd[64]; snprintf(label, sizeof label, "%s", x->label); snprintf(cmd, sizeof cmd, "%s", x->cmd_name);
        command("equip_weapon", cmd);
        read_state(); x = find_weap((uint32_t)-a->id);
        if (x && x->current) say("%s", label); else say("CANNOT EQUIP %s", label);
    } else if (a->w == SS2G_AMMO && a->id >= SS2G_PSI(0)) {
        int p = a->id - SS2G_PSI(0);
        if (st.gun_type != 4 || !psi_known(p)) { say("PSI POWER NOT AVAILABLE"); return; }
        uint32_t sg = service(IID_SRV_SHOCKGAME, VT_SHOCKGAME); if (!sg) return;
        vcall(sg, S_SG_SET_SELECTED_PSI, 1, (uint32_t)p); release(sg);
        read_state(); if (st.psi_selected == p) say("%s", psi_short[p]); else say("PSI POWER NOT SELECTED");
    } else if (a->w == SS2G_AMMO && a->id >= SS2G_SETTING(0)) {
        int s = a->id - SS2G_SETTING(0);
        if (!st.gun || st.gun != a->gun || st.nsettings != 2 || s < 0 || s > 1) { say("WEAPON CHANGED"); return; }
        if (st.setting == s) return;
        char name[24]; snprintf(name, sizeof name, "%s", st.setting_name[s]);
        command("wpn_setting_toggle", NULL);
        read_state(); if (st.setting == s) say("SETTING: %s", name); else say("SETTING NOT CHANGED");
    } else if (a->w == SS2G_AMMO) {
        uint32_t proj = (uint32_t)-(a->id - SS2G_AMMO_TYPE(0));
        if (!st.gun || st.gun != a->gun || st.gun_type == 4) { say("WEAPON CHANGED"); return; }
        if (st.dead || proj == st.proj) return;
        const AmmoType *t = NULL; for (int i = 0; i < st.nammo; i++) if (st.ammo[i].proj == proj) t = &st.ammo[i];
        if (!t) { say("NO SUCH AMMO CARRIED"); return; }
        char label[24]; snprintf(label, sizeof label, "%s", t->label);
        fcall(F_LOAD_AMMO, st.gun, 0, 0, 2, t->obj, 2u);              /* cycle_ammo's load: the clip from the inventory */
        read_state(); if (st.proj == proj) say("%s", label); else say("AMMO NOT CHANGED");
    } else if (a->w == SS2G_ITEMS) {
        const Item *m = NULL; for (int i = 0; i < st.nitem; i++) if (st.item[i].arch == (uint32_t)-a->id) m = &st.item[i];
        if (!m) { say("NONE LEFT"); return; }
        int before = m->count; char cmd[64], label[24]; snprintf(cmd, sizeof cmd, "%s", m->cmd_name); snprintf(label, sizeof label, "%s", m->label);
        command("use_obj", cmd);
        read_state(); int after = 0; for (int i = 0; i < st.nitem; i++) if (st.item[i].arch == (uint32_t)-a->id) after = st.item[i].count;
        if (after < before) say("USED %s", label); else say("%s NOT USED", label);
    } else if (a->w == SS2G_SYSTEM) {
        switch (a->id) {
        case SS2G_SYS_PLAY_LOG: {
            int before = st.unread_logs; command("play_unread_log", NULL); read_state();
            if (st.unread_logs < before) say("PLAYING LOG - %d UNREAD LEFT", st.unread_logs); else say("NO UNREAD LOGS");
            break; }
        case SS2G_SYS_STOP_LOG: command("stop_email", NULL); break;
        case SS2G_SYS_QUICKSAVE: command("quicksave", NULL); say("QUICKSAVE"); break;
        case SS2G_SYS_QUICKLOAD: if (!st.quick_exists) { say("NO QUICKSAVE YET"); break; } command("quickload", NULL); say("QUICKLOAD"); break;
        case SS2G_SYS_PSI_MENU: command("select_psipower", NULL); break;
        }
    }
}

/* ---------------------------------------------------------------- tests: SS2PORT_INPUT_SCRIPT "game ..." lines */
static char pending_script[8][96]; static int npending;
void ss2g_script(const char *line) { if (npending < 8) snprintf(pending_script[npending++], 96, "%s", line); }
static const char *const wheel_names[] = { "weapons", "ammo", "items", "system" };
static void dump(void) {
    read_state();
    for (int w = 0; w < SS2G_NWHEELS; w++) {
        Ss2gEntry e[SS2G_MAX]; char title[64], why[64]; int n = ss2g_wheel((Ss2gWheel)w, e, SS2G_MAX, title, sizeof title, why, sizeof why);
        port_log("[ss2game] wheel %s: %d '%s'%s%s", wheel_names[w], n, title, why[0] ? " - " : "", why);
        for (int i = 0; i < n; i++) port_log("[ss2game]   %6d %-14s %-36s count %d%s%s", e[i].id, e[i].label, e[i].name, e[i].count, e[i].available ? "" : " UNAVAILABLE", e[i].current ? " CURRENT" : "");
    }
}
static void script_line(const char *l) {
    port_log("[ss2game] script: %s", l);
    char verb[16] = "", wn[16] = ""; int id = 0; sscanf(l, "%15s %15s %d", verb, wn, &id);
    if (!strcmp(verb, "dump")) { dump(); return; }
    if (!strcmp(verb, "select")) {
        int w = 0; while (w < SS2G_NWHEELS && strcmp(wn, wheel_names[w])) w++;
        if (w == SS2G_NWHEELS) { port_log("[ss2game] script: unknown wheel %s", wn); return; }
        read_state();
        if (!ss2g_select((Ss2gWheel)w, id)) port_log("[ss2game] script: refused");
        return;
    }
    /* test-only (disposable saves; never reached from touch): a console command, and ShockGame.GrantPsiPower, which the
       game's own psi upgrade stations use; the shipping executable has no command that grants a power */
    if (!strcmp(verb, "cmd")) {                                    /* "cmd COMMAND [ARGS]" */
        char cmd[32] = "", arg[64] = ""; sscanf(l, "%*s %31s %63[^\n]", cmd, arg); command(cmd, arg); return;
    }
    if (!strcmp(verb, "give")) {                                   /* "give ARCHETYPE": IObjectSystem::Create, ShockGame.AddInvObj */
        char name[64] = ""; sscanf(l, "%*s %63[^\n]", name); Ifs f; uint32_t o = 0;
        if (ifs_get(&f)) {
            uint32_t arch = named(&f, name);
            if (arch) o = vcall(f.objsys, 3, 2, arch, 1u);
            uint32_t sg = service(IID_SRV_SHOCKGAME, VT_SHOCKGAME);
            if (sg && o) { WR32(scratch + SC_OUT, o); vcall(sg, 46, 1, scratch + SC_OUT); }
            release(sg);
        }
        ifs_put(&f); port_log("[ss2game] test: gave %s (object %d)", name, (int)o); return;
    }
    if (!strcmp(verb, "take")) {                                   /* "take ARCHETYPE": ShockGame.DestroyInvObj on each one carried */
        char name[64] = ""; sscanf(l, "%*s %63[^\n]", name); Ifs f; int n = 0;
        if (ifs_get(&f)) {
            uint32_t arch = named(&f, name), pl = RD32(PLAYER_OBJ), objs[32]; int k = 0;
            uint32_t it = arch && pl ? vcall(f.contain, S_CONTAIN_ITER_START, 1, pl) : 0;
            while (it && !RD32(it) && k < 32) { uint32_t o = RD32(it + 12); if (inherits(&f, o, arch)) objs[k++] = o; vcall(f.contain, S_CONTAIN_ITER_NEXT, 1, it); }
            if (it) vcall(f.contain, S_CONTAIN_ITER_END, 1, it);
            uint32_t sg = service(IID_SRV_SHOCKGAME, VT_SHOCKGAME);
            for (int i = 0; sg && i < k; i++) { WR32(scratch + SC_OUT, objs[i]); vcall(sg, 6, 1, scratch + SC_OUT); n++; }
            release(sg);
        }
        ifs_put(&f); port_log("[ss2game] test: removed %d %s", n, name); return;
    }
    if (!strcmp(verb, "grant_psi")) {                              /* "grant_psi POWER" */
        int p = atoi(wn); uint32_t sg = service(IID_SRV_SHOCKGAME, VT_SHOCKGAME), pl = RD32(PLAYER_OBJ);
        if (sg && pl && p >= 0 && p < PSI_NONE) { WR32(scratch + SC_OUT, pl); vcall(sg, S_SG_GRANT_PSI, 2, scratch + SC_OUT, (uint32_t)p); }
        release(sg); port_log("[ss2game] test: granted psi power %d", p); return;
    }
    if (!strcmp(verb, "string")) {                                 /* "string TABLE KEY [INDEX]" */
        char t[32] = "", k[32] = "", out[256]; int idx = -1; sscanf(l, "%*s %31s %31s %d", t, k, &idx);
        game_string(t, k, idx, out, sizeof out); port_log("[ss2game] string %s %s %d: '%s'", t, k, idx, out); return;
    }
    port_log("[ss2game] script: unknown '%s'", l);
}

/* ---------------------------------------------------------------- the frame pump */
void ss2g_message_pump(CPU *c) {
    if (busy || RD32(c->esp) != FRAME_PUMP_RET) return;
    if (!want_refresh && !nqueue && !npending) return;
    if (debug < 0) { const char *e = getenv("SS2PORT_GAME_DEBUG"); debug = e && atoi(e); }
    if (!check_static()) { want_refresh = 0; nqueue = npending = 0; return; }
    CPU save = *c; cpu = c; busy = 1;
    for (int i = 0; i < npending; i++) script_line(pending_script[i]);
    npending = 0;
    Action q[4]; int n = nqueue; memcpy(q, queue, sizeof q); nqueue = 0;
    for (int i = 0; i < n && ok == 1; i++) run(&q[i]);
    if (want_refresh && ok == 1) read_state();
    want_refresh = 0;
    void *rc = c->rc; *c = save; c->rc = rc; cpu = NULL; busy = 0;
}
