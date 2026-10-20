/* Shared touch radial selector. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TOUCH_RADIAL_H
#define TOUCH_RADIAL_H
#include <stdbool.h>
#include <stdint.h>

/* NewDarkAndroid: 16 entries (System Shock's wheels never filled more than 7); a wheel of more than 8 is drawn larger */
#define TOUCH_RADIAL_MAX_ENTRIES 16
#define TOUCH_RADIAL_HOLD_MS 320u
typedef enum { TOUCH_RADIAL_WEAPON, TOUCH_RADIAL_CONSUMABLE,
               TOUCH_RADIAL_EXPLOSIVE, TOUCH_RADIAL_AMMO,
               TOUCH_RADIAL_SYSTEM,     /* NewDarkAndroid: System Shock 2's system wheel (quicksave, logs) */
               TOUCH_RADIAL_CATEGORY_COUNT } TouchRadialCategory;
/* Bind ammo choices to the equipped slot AND gun definition. A live refresh
   disables stale sectors if the weapon changes while a finger is held. */
#define TOUCH_AMMO_CONTEXT(slot, gun) (100 + ((slot) * 32 + (gun)) * 4)
#define TOUCH_AMMO_RELOAD_ID(slot, gun) TOUCH_AMMO_CONTEXT(slot, gun)
#define TOUCH_AMMO_TYPE_ID(slot, gun, type) (TOUCH_AMMO_CONTEXT(slot, gun) + 1 + (type))
#define TOUCH_AMMO_IS_RELOAD(id) (((id) - TOUCH_AMMO_CONTEXT(0, 0)) % 4 == 0)
typedef struct {
    int id;                    /* original weapon slot or inventory type */
    char label[24];
    bool available, current;
    unsigned count;
    bool no_count;             /* NewDarkAndroid: draw no count (weapons, settings, system entries) */
    char name[64];             /* NewDarkAndroid: the entry in full, shown above the wheel while it is highlighted */
} TouchRadialEntry;
typedef struct {
    bool open;
    int owner;
    TouchRadialCategory category;
    float cx, cy, inner_radius, outer_radius;
    int count, highlighted;
    TouchRadialEntry entries[TOUCH_RADIAL_MAX_ENTRIES];
    /* NewDarkAndroid Phase 3G: the arc the segments share, in radians clockwise from straight up. A whole wheel has
       span 2*pi (or 0, a selector set up without it) and starts half a segment left of the top, so segment zero is
       centred at the top as before; a fan (an anchored wheel at a screen edge) covers only the arc that is on the screen. */
    float start, span;
} TouchRadialSelector;
void touch_radial_close(TouchRadialSelector *r);
bool touch_radial_open(TouchRadialSelector *r, int owner, TouchRadialCategory category,
                       float width, float height, const TouchRadialEntry *entries, int count);
/* NewDarkAndroid Phase 3G: a wheel around the held button (anchor_x, anchor_y) instead of the screen centre. The holding
   finger (finger_x, finger_y) is always inside the cancel circle when it opens. Where a whole wheel would leave the
   screen, the segments are fanned over the widest arc that stays on it (at most a quarter turn each), and the wheel grows
   a little when that keeps the segments wider. Same validation and result as touch_radial_open. */
bool touch_radial_open_at(TouchRadialSelector *r, int owner, TouchRadialCategory category, float width, float height,
                          float anchor_x, float anchor_y, float finger_x, float finger_y,
                          const TouchRadialEntry *entries, int count);
/* The middle of segment i, as an angle on the screen (radians, 0 = right, y down), and the point there at distance d. */
float touch_radial_segment_angle(const TouchRadialSelector *r, int segment);
void touch_radial_segment_point(const TouchRadialSelector *r, int segment, float distance, float *x, float *y);
bool touch_radial_is_fan(const TouchRadialSelector *r);
int touch_radial_highlight(TouchRadialSelector *r, int owner, float x, float y);
int touch_radial_release(TouchRadialSelector *r, int owner, float x, float y);
/* Preserve segment positions while updating live availability and counts. */
void touch_radial_refresh(TouchRadialSelector *r, const TouchRadialEntry *entries, int count);
#endif
