/* Touch Alpha 1. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TOUCH_CONTROLS_H
#define TOUCH_CONTROLS_H

#include <stdbool.h>
#include <stdint.h>
#include "touch_settings.h"
#include "touch_radial.h"

#define TOUCH_MAX_FINGERS 16
typedef enum { TOUCH_GAMEPLAY, TOUCH_CURSOR } TouchMode;
typedef enum {
    TOUCH_NONE, TOUCH_MOVE, TOUCH_LOOK, TOUCH_FIRE, TOUCH_USE,
    TOUCH_CURSOR_OWNER, TOUCH_MODE_TOGGLE, TOUCH_ACTION, TOUCH_COMPARE_MODE, TOUCH_COMPARE_POST
} TouchRole;
typedef enum {
    TOUCH_JUMP, TOUCH_CROUCH, TOUCH_WEAPON_PREV, TOUCH_WEAPON_NEXT,
    TOUCH_CONSUMABLE, TOUCH_EXPLOSIVE, TOUCH_SPRINT, TOUCH_TARGET_ID,
    TOUCH_PAUSE, TOUCH_AMMO, TOUCH_ACTION_COUNT
} TouchAction;
typedef enum { TOUCH_DOWN, TOUCH_MOTION, TOUCH_UP, TOUCH_ABORT } TouchPhase;
#define TOUCH_TAP_MS 350u
#define TOUCH_MENU_TAP_MS 1000u
typedef enum {
    TOUCH_FIRE_DOWN, TOUCH_FIRE_UP, TOUCH_USE_PRESS,
    TOUCH_POINTER_DOWN, TOUCH_POINTER_MOVE, TOUCH_POINTER_UP,
    TOUCH_MODE_CHANGED, TOUCH_CANCEL, TOUCH_ACTION_DOWN, TOUCH_ACTION_UP,
    TOUCH_POINTER_TAP, TOUCH_UI_CHANGED, TOUCH_SETTINGS_SAVE,
    TOUCH_RADIAL_SELECT, TOUCH_HAPTIC, TOUCH_QA_COMMAND, TOUCH_VISUAL_COMPARE
} TouchOutput;
/* Outputs are ordered edges, not polled booleans: even a down/up between
   game polls reaches the original input queue. Cancellation invalidates them. */
typedef void (*TouchEmit)(void *user, TouchOutput output, float x, float y);
typedef struct { float x, y, radius; } TouchCircle;
typedef struct { float x, y, w, h; } TouchRect;
typedef struct {
    float width, height, move_radius, move_split;
    TouchCircle fire, use, settings, stick;
    TouchCircle action[TOUCH_ACTION_COUNT];
    TouchRect toggle;
} TouchLayout;
typedef struct {
    int64_t touch, finger; /* exact SDL_TouchID / SDL_FingerID values */
    bool active, owns_look;
    TouchRole role;
    TouchAction action;
    float x, y, start_x, start_y;
    uint32_t down_ms;
    bool dragging;
    bool radial_pending, radial_cancelled, radial_opened;
} TouchFinger;
typedef enum { TOUCH_UI_CLOSED, TOUCH_UI_SETTINGS, TOUCH_UI_EDITOR, TOUCH_UI_QA } TouchPanel;
typedef enum {
    TU_EDIT, TU_OPACITY_DOWN, TU_OPACITY_UP, TU_X_DOWN, TU_X_UP,
    TU_Y_DOWN, TU_Y_UP, TU_INVERT, TU_STICK_DOWN, TU_STICK_UP,
    TU_RESET, TU_CANCEL, TU_SAVE, TU_PREV, TU_NEXT, TU_SCALE_DOWN,
    TU_SCALE_UP, TU_BACK, TU_DOCK, TU_PAGE, TU_HAPTICS,
    TU_HAPTIC_STRENGTH, TU_WHEEL_WEAPON, TU_WHEEL_CONSUMABLE,
    TU_WHEEL_EXPLOSIVE, TU_QA, TU_QA_GOD, TU_QA_HEAL, TU_QA_LOADOUT, TU_QA_CLOSE,
    TU_QA_DECK_FIRST, TU_QA_DECK_LAST=TU_QA_DECK_FIRST+12, TU_COUNT
} TouchUiAction;
typedef int (*TouchRadialPopulate)(void *user, TouchRadialCategory category,
                                    TouchRadialEntry entries[TOUCH_RADIAL_MAX_ENTRIES]);
typedef struct {
    TouchMode mode, effective_mode;
    bool enabled, gameplay_available, pause_available, controller;
    TouchLayout layout;
    TouchFinger fingers[TOUCH_MAX_FINGERS];
    int move_owner, look_owner, cursor_owner;
    unsigned fire_count, use_count;
    unsigned action_count[TOUCH_ACTION_COUNT];
    bool sprint_latched;
    bool qa_enabled, qa_god, compare_enabled;
    int compare_mode, compare_post_enabled, compare_post_choice, compare_level;
    int compare_pressed; /* 0 MODE, 1 POST, -1 idle */
    int compare_notice; /* 0 MODE, 1 POST, -1 hidden */
    uint32_t compare_notice_until;
    const char *qa_status;
    TouchRadialSelector radial;
    TouchRadialPopulate radial_populate;
    float anchor_x, anchor_y, movement_x, movement_y, look_x, look_y;
    float cursor_slop, look_scale;
    float stick_reference_height;
    TouchSettings settings, saved_settings;
    TouchPanel panel;
    int selected, ui_action, settings_page;
    bool ui_owned, ui_drag, toolbar_top, save_failed;
    int64_t ui_device, ui_finger;
    float ui_start_x, ui_start_y, ui_offset_x, ui_offset_y;
    TouchEmit emit;
    void *user;
} TouchControls;

void touch_init(TouchControls *t, TouchEmit emit, void *user);
void touch_cancel(TouchControls *t);
void touch_configure(TouchControls *t, float width, float height,
                     bool enabled, bool gameplay_available, bool controller);
void touch_set_mode(TouchControls *t, TouchMode mode);
void touch_layout_refresh(TouchControls *t);
TouchRect touch_control_bounds(const TouchControls *t, int id);
bool touch_control_hit(const TouchControls *t, int id, float x, float y);
void touch_editor_position(TouchControls *t, int id, float x, float y);
void touch_ui_open(TouchControls *t);
void touch_ui_action(TouchControls *t, TouchUiAction action);
TouchRect touch_ui_bounds(const TouchControls *t, TouchUiAction action);
void touch_ui_finger(TouchControls *t, TouchPhase phase, int64_t device, int64_t finger,
                     float x, float y);
void touch_finger(TouchControls *t, TouchPhase phase, int64_t device, int64_t finger,
                  float surface_x, float surface_y, uint32_t timestamp_ms);
TouchRect touch_compare_bounds(const TouchControls *t, int button);
int touch_compare_hit(const TouchControls *t, float x, float y);
void touch_tick(TouchControls *t, uint32_t timestamp_ms);
/* Sole surface -> game transform; clamp only after gameplay ownership. */
void touch_map_cursor(float x, float y, TouchRect viewport, float scale_x,
                      float scale_y, int width, int height, float *gx, float *gy);
void touch_consume_look(TouchControls *t, int *dx, int *dy);
/* Per-axis keyboard/controller precedence, at normal walking strength. */
void touch_merge_axes(const TouchControls *t, int walk_strength, int *x, int *y);

/* Raw-surface tap recognizer for movie playback. It never owns gameplay input. */
typedef struct {
    TouchFinger fingers[TOUCH_MAX_FINGERS];
    unsigned active_count;
    bool blocked, overflow;
} TouchMovieTap;
void touch_movie_tap_reset(TouchMovieTap *tap);
bool touch_movie_tap_event(TouchMovieTap *tap, TouchPhase phase,
                           int64_t device, int64_t finger, float x, float y,
                           uint32_t timestamp_ms, float slop);

/* Independent source masks: release edges occur only when the aggregate goes
   up. Bit numbering is the original Shock left=0, right=1 convention. */
typedef enum { TOUCH_MOUSE_PHYSICAL, TOUCH_MOUSE_CONTROLLER, TOUCH_MOUSE_TOUCH } TouchMouseSource;
typedef struct { uint8_t source[3]; } TouchMouseButtons;
uint8_t touch_mouse_buttons(const TouchMouseButtons *b);
bool touch_mouse_button(TouchMouseButtons *b, TouchMouseSource source,
                        uint8_t bit, bool down);

typedef enum { TOUCH_KEY_PHYSICAL, TOUCH_KEY_CONTROLLER, TOUCH_KEY_TOUCH } TouchKeySource;
typedef struct { bool source[3]; } TouchKeySources;
bool touch_key_held(const TouchKeySources *k);
/* Returns true only when the aggregate key state changes. */
bool touch_key_source(TouchKeySources *k, TouchKeySource source, bool down);

struct SDL_Renderer;
void touch_draw(const TouchControls *t, struct SDL_Renderer *renderer);
#endif
