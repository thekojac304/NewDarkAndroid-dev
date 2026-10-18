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
} Ss2Touch;

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
/* A key or mouse button from a physical keyboard or mouse: returns true when it should reach the game (it does not
   when touch already holds the same key, so neither source can release the other's press). */
bool ss2t_physical(Ss2Touch *s, const PlatEvent *e);
/* The events now due (movement keys, look motion, actions, cursor), in order; returns how many were written. */
int  ss2t_flush(Ss2Touch *s, PlatEvent *out, int max);
#endif
