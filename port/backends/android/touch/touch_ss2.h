/* touch_ss2.h: the Shockolate touch controls (touch_controls.c) driving System Shock 2 through the portable host's
 * platform events. No SDL, JNI or GL here (touch_ss2_android.c is the glue), so it is tested on the PC (tests/).
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TOUCH_SS2_H
#define TOUCH_SS2_H
#include <stdbool.h>
#include <stdint.h>
#include "plat.h"
#include "touch_controls.h"

/* DirectInput scan codes of the stock System Shock 2 bindings (user.bnd of NewDark 2.48, which replaces SHOCK.BND). */
enum {
    SS2_DIK_ESC = 0x01, SS2_DIK_TAB = 0x0F, SS2_DIK_W = 0x11, SS2_DIK_R = 0x13, SS2_DIK_A = 0x1E,
    SS2_DIK_S = 0x1F, SS2_DIK_D = 0x20, SS2_DIK_LSHIFT = 0x2A, SS2_DIK_B = 0x30, SS2_DIK_SPACE = 0x39
};
#define SS2T_QUEUE 256
#define SS2T_MIN_HOLD_NS 60000000ull       /* a press lasts at least this long, so a 100 fps game polls it down */
#define SS2T_SHOOT_NS 250000000ull         /* the game recentred its cursor on 3 frames this recently: shoot (mouse look) mode;
                                              a single recentring (a menu or level change does one) is not enough */
#define SS2T_FOLLOW_NS 600000000ull        /* after the player's own MODE toggle, wait this long before following the game */
#define SS2T_STICK_ON .30f                 /* stick deflection that presses a movement key ... */
#define SS2T_STICK_OFF .20f                /* ... and that releases it again (hysteresis) */
#define SS2T_HOLD_MS 500u                  /* relative cursor: a finger held still this long presses the left button (a drag) */
/* Phase 3F radial menus */
#define SS2T_RADIAL_HOLD_MS 400u           /* holding < > AMMO ITEM or II this long opens its wheel */
#define SS2T_WHEEL_REFRESH_NS 250000000ull /* an open wheel reads the game again this often (entries can become unavailable) */
#define SS2T_TOAST_NS 1800000000ull        /* a notice stays up this long */
#define SS2T_CONFIRM_NS 6000000000ull      /* the quickload question goes away by itself after this long (no quickload) */
#define SS2T_SYS_QUICKLOAD 4               /* the system wheel's quickload id (core/ss2game.h SS2G_SYS_QUICKLOAD) */

typedef struct { uint64_t due; PlatEvent ev; } Ss2Pending;
typedef struct Ss2Touch {
    TouchControls t;
    /* the game's picture inside the touch surface (letterboxed), and its size in game pixels */
    float pic_x, pic_y, pic_w, pic_h; int game_w, game_h;
    int ui_x, ui_y, ui_w, ui_h;            /* the engine's 2D picture in game pixels (ss2t_ui_rect); ui_w 0: the whole picture */
    uint64_t now;                         /* time of the current frame (ns) */
    /* touch-held keys and buttons (counts: two controls may hold one key) and the physical ones, for aggregation */
    uint8_t touch_key[256], phys_key[256], touch_btn[5], phys_btn[5];
    uint64_t pressed_at_key[256], pressed_at_btn[5];
    bool stick_key[4];                     /* W A S D held by the stick */
    Ss2Pending q[SS2T_QUEUE]; int qn;      /* events to emit, in order, each when due */
    bool syncing;                          /* the bridge is changing touch mode itself (no Tab to the game) */
    uint64_t user_toggle_at, recentred;
    uint64_t recent[3];                    /* the last three distinct recentring times the bridge saw */
    uint64_t move_hold_until;              /* a drag's press must reach the game a frame before the cursor moves on */
    bool game_shoot;
    int crouch_dik;                        /* the key CROUCH presses: the installation's own binding (ss2t_set_crouch_key), Shift by default */
    bool typing;                           /* the Android keyboard is up: in look mode only II, KBD, MODE and SET take a touch */
    /* relative (trackpad) cursor, settings.cursor_relative: the cursor finger moves the game's cursor from where it is */
    enum { SS2T_REL_IDLE, SS2T_REL_DOWN, SS2T_REL_MOVING, SS2T_REL_HELD } rel_state;
    float rel_x, rel_y;                    /* the game's cursor in game pixels (as placements); fractions add up over moves */
    float rel_last_x, rel_last_y;          /* the finger's last surface position taken into account */
    int rel_placed_x, rel_placed_y;        /* where the bridge last put the cursor: if the game reports another place, it moved */
    /* platform services (touch_ss2_android.c); may be NULL in tests */
    void (*save_settings)(struct Ss2Touch *s);
    void (*haptic)(struct Ss2Touch *s, int level);
    void (*keyboard)(struct Ss2Touch *s);  /* KBD: show or hide the Android keyboard */
    /* where the game's cursor is now, in the pixels of the bridge's placements (user32_cursor_pos): a relative gesture
       starts there, so it follows a physical mouse too. NULL: from where the bridge last put it (the picture centre at first) */
    void (*cursor_pos)(struct Ss2Touch *s, int *x, int *y);
    /* Phase 3F: the game's state for the radial menus (core/ss2game.c through touch_ss2_android.c). NULL, or game_on false
       (no game state: another executable, or not in a game yet): the buttons keep their plain actions, ITEM is hidden. */
    bool game_on;
    void (*game_refresh)(struct Ss2Touch *s);          /* read the game soon: a wheel may open */
    /* a wheel's entries: count (0: none, why says why), title: what the wheel is about */
    int  (*game_wheel)(struct Ss2Touch *s, int category, TouchRadialEntry *e, int max, char *title, size_t tn, char *why, size_t wn);
    bool (*game_select)(struct Ss2Touch *s, int category, int id);   /* queue the entry's action; false: refused */
    bool (*game_message)(struct Ss2Touch *s, char *out, size_t n);   /* the outcome of the last action, once */
    char wheel_title[64];                  /* what the open wheel is about */
    int last_highlight;                    /* the segment highlighted last frame (a light haptic when it changes) */
    uint64_t next_wheel_refresh;
    char toast[64]; uint64_t toast_until;  /* a short notice drawn over the game */
    uint64_t confirm_until;                /* the quickload question is up until then; 0: not */
} Ss2Touch;

/* Phase 3F: what the overlay draws besides the controls (touch_render_gles.c) */
typedef struct { const char *wheel_title; const char *toast; bool confirm; } Ss2Overlay;
void ss2t_overlay(const Ss2Touch *s, Ss2Overlay *o);
/* the quickload question's box (0), its LOAD button (1) and its CANCEL button (2), in surface pixels */
TouchRect ss2t_confirm_rect(const TouchControls *t, int which);

void ss2t_init(Ss2Touch *s);
/* Once per poll: surface size, whether touch is usable (window focused, foreground), the game picture rectangle in
   surface pixels, the game's resolution, and when the game last recentred its cursor (user32_cursor_recentred_ns). */
void ss2t_frame(Ss2Touch *s, uint64_t now_ns, float surface_w, float surface_h, bool enabled,
                float pic_x, float pic_y, float pic_w, float pic_h, int game_w, int game_h, uint64_t recentred_ns);
/* Where the engine drew its 2D picture this frame, in game pixels: a 4:3 menu fitted into a wider resolution, or the
   scaled HUD (d3d_disp_scaled_2d_overlay). The engine maps the whole window onto it, so a touched point is placed
   through it. w = 0, or the whole picture: no remapping. */
void ss2t_ui_rect(Ss2Touch *s, int x, int y, int w, int h);
void ss2t_finger(Ss2Touch *s, TouchPhase phase, int64_t device, int64_t finger, float x, float y, uint32_t ms);
void ss2t_cancel(Ss2Touch *s);             /* releases everything touch holds (lifecycle, focus, resize) */
/* The key the CROUCH button presses (a DirectInput code from the game's bind file, touch_binds.h; 0: back to Shift). If touch
   holds the old key, everything touch holds is released first. */
void ss2t_set_crouch_key(Ss2Touch *s, int dik);
/* A key or mouse button from a physical keyboard or mouse: returns true when it should reach the game (it does not
   when touch already holds the same key, so neither source can release the other's press). */
bool ss2t_physical(Ss2Touch *s, const PlatEvent *e);
/* The events now due (movement keys, look motion, actions, cursor), in order; returns how many were written. */
int  ss2t_flush(Ss2Touch *s, PlatEvent *out, int max);
#endif
