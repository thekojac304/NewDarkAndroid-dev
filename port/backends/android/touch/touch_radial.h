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
} TouchRadialSelector;
void touch_radial_close(TouchRadialSelector *r);
bool touch_radial_open(TouchRadialSelector *r, int owner, TouchRadialCategory category,
                       float width, float height, const TouchRadialEntry *entries, int count);
int touch_radial_highlight(TouchRadialSelector *r, int owner, float x, float y);
int touch_radial_release(TouchRadialSelector *r, int owner, float x, float y);
/* Preserve segment positions while updating live availability and counts. */
void touch_radial_refresh(TouchRadialSelector *r, const TouchRadialEntry *entries, int count);
#endif
