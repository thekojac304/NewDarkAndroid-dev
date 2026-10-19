/* ss2game.h: read-only views of System Shock 2's game state, and the game's own commands, for the touch controls'
 * radial menus (port/backends/android/touch). Game-specific and isolated here: everything it uses belongs to SS2.exe
 * NewDark 2.48 (the one executable the port runs) and is checked before first use; on any other image it stays off.
 *
 * Nothing here runs from the touch code directly. The touch code asks for a refresh or queues an action; both happen
 * the next time the engine's per-frame event pump runs (ss2g_message_pump), on the game thread, where the game's own key
 * bindings run their commands. Reads never change the game; actions are the game's own commands and functions.
 * SPDX-License-Identifier: MIT */
#ifndef SS2GAME_H
#define SS2GAME_H
#include <stddef.h>
#include <stdint.h>

/* core/modules.c: a script module's ScriptModuleInit is about to get the engine's IScriptMan */
void ss2g_script_man(uint32_t script_man);
/* win32/user32.c, PeekMessageA: when the engine's per-frame event pump (a frame-loop client) asks for its window
   messages, the stage at which its own key bindings run commands, queued actions and state refreshes run here. Any other
   PeekMessage (loading screens, dialogs, nested pumps) is ignored. */
struct CPU;
void ss2g_message_pump(struct CPU *c);
/* win32/user32.c: "game ..." lines of SS2PORT_INPUT_SCRIPT (tests): "dump", "select WHEEL ID", "string TABLE KEY [N]"; for
   disposable test saves only: "cmd COMMAND [ARGS]", "give ARCHETYPE", "take ARCHETYPE", "grant_psi POWER" (never reached from touch) */
void ss2g_script(const char *line);

/* ---------------------------------------------------------------- for the touch radials */
#define SS2G_MAX 16
typedef enum { SS2G_WEAPONS, SS2G_AMMO, SS2G_ITEMS, SS2G_SYSTEM, SS2G_NWHEELS } Ss2gWheel;
typedef struct {
    int id;                    /* given back to ss2g_select */
    char label[24];            /* short, for the segment (upper case) */
    char name[64];             /* in full, for the wheel's title while the segment is highlighted */
    int count;                 /* < 0: none shown */
    uint8_t available, current;
} Ss2gEntry;
/* System wheel ids */
enum { SS2G_SYS_PLAY_LOG = 1, SS2G_SYS_STOP_LOG, SS2G_SYS_QUICKSAVE, SS2G_SYS_QUICKLOAD, SS2G_SYS_PSI_MENU };
/* Ammo wheel ids: an ammunition type, a weapon setting, a psi power (when the psi amp is the weapon) */
#define SS2G_AMMO_TYPE(i)   (0x10000 + (i))
#define SS2G_SETTING(i)     (0x20000 + (i))
#define SS2G_PSI(power)     (0x30000 + (power))

int  ss2g_available(void);             /* the engine's interfaces were found and checked (or not yet tried: 1) */
void ss2g_refresh(void);               /* read the game state at the next frame pump */
uint64_t ss2g_state_time(void);        /* plat_time_ns of the last good read; 0: none (no game running, or off) */
/* The entries of a wheel from the last read: returns how many (0: nothing to offer, -1: no game state). title: what the
   wheel is about ("WEAPONS", the weapon's name, "PSI POWERS"); empty: nothing to offer, with the reason in `why`. */
int  ss2g_wheel(Ss2gWheel w, Ss2gEntry *e, int max, char *title, size_t title_n, char *why, size_t why_n);
/* Queues the action for an entry; it runs at the next frame pump after checking the game state again. 0: refused now. */
int  ss2g_select(Ss2gWheel w, int id);
/* The outcome of the last action, once, for a short notice ("" when there is none). */
int  ss2g_message(char *out, size_t n);
#endif
