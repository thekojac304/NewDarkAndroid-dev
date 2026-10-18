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
void touch_settings_validate(TouchSettings *s);
TouchLoadResult touch_settings_load(TouchSettings *s, const char *path);
bool touch_settings_save(const TouchSettings *s, const char *path);
#endif
