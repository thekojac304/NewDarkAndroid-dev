/* Android touch preferences. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TOUCH_SETTINGS_H
#define TOUCH_SETTINGS_H
#include <stdbool.h>
#define TOUCH_SETTINGS_VERSION 3
#define TOUCH_SCALE_MIN .6f
#define TOUCH_SCALE_MAX 1.8f
#define TOUCH_OPACITY_MIN .25f
#define TOUCH_SENS_MIN .25f
#define TOUCH_SENS_MAX 3.0f
/* NewDarkAndroid: relative (trackpad) cursor speed; 1 = the cursor moves as far on screen as the finger */
#define TOUCH_CURSOR_SPEED_MIN .25f
#define TOUCH_CURSOR_SPEED_MAX 4.0f
#define TOUCH_CURSOR_SPEED_STEP .25f
typedef enum {
    TC_FIRE, TC_USE, TC_JUMP, TC_CROUCH, TC_PREV, TC_NEXT, TC_CONSUMABLE,
    TC_EXPLOSIVE, TC_SPRINT, TC_TARGET, TC_PAUSE, TC_MODE, TC_SETTINGS,
    TC_STICK, TC_AMMO, TC_COUNT
} TouchControlId;
typedef struct { float x, y, scale; } TouchPlacement;
typedef struct {
    unsigned version;
    float opacity, sensitivity_x, sensitivity_y;
    bool invert_y;
    bool haptics_enabled;
    unsigned haptic_strength; /* 1..3; default 1 (subtle) */
    bool wheel_enabled[3]; /* weapon, consumable, explosive */
    /* NewDarkAndroid: cursor style in cursor mode. Direct (false, the default): the cursor goes to the finger. Relative
       (true): a trackpad; the finger moves the cursor from where it is, a tap clicks there. New keys of the same file
       version: older files load as Direct at speed 1, and older builds ignore the keys. */
    bool cursor_relative;
    float cursor_speed;
    /* NewDarkAndroid Phase 3F (System Shock 2's radial menus; only with touch_settings_game set): ask before a quickload
       from the system wheel (default on). game_radials: the file has been saved by a build with the radials, so its wheel
       preferences are the player's own (before, they did nothing in System Shock 2: a file without the key keeps the
       game's defaults for them, and the ITEM button, never shown before, its default position). */
    bool quickload_confirm;
    bool game_radials;
    /* TC_STICK.scale is the single stick-size value used by both UIs. */
    TouchPlacement control[TC_COUNT];
} TouchSettings;
typedef struct {
    const char *key, *label;
    float x, y, radius; /* centers in surface fractions; radius in height */
} TouchDefault;
extern const TouchDefault touch_defaults[TC_COUNT];
typedef enum { TOUCH_LOAD_DEFAULTS, TOUCH_LOAD_OK, TOUCH_LOAD_MIGRATED,
               TOUCH_LOAD_INVALID, TOUCH_LOAD_FUTURE } TouchLoadResult;
void touch_settings_defaults(TouchSettings *s);
/* NewDarkAndroid: a game's own defaults on top of Shockolate's (NULL: none, the files and defaults Shockolate has) */
typedef struct {
    void (*defaults)(TouchSettings *s);   /* called at the end of touch_settings_defaults */
    int item_control;                     /* the control first shown by Phase 3F (its stored position is not the player's) */
} TouchSettingsGame;
extern const TouchSettingsGame *touch_settings_game;
void touch_settings_validate(TouchSettings *s);
TouchLoadResult touch_settings_load(TouchSettings *s, const char *path);
bool touch_settings_save(const TouchSettings *s, const char *path);
/* NewDarkAndroid: a loaded layout whose controls are all still at the previous release's default positions gets the
   current defaults (in memory; the file changes only when the player saves). Stick size, opacity and look speed are
   kept. Returns true when it did. A layout with any control moved or resized is left alone. */
bool touch_settings_upgrade_layout(TouchSettings *s);
#endif
