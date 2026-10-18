/* Android surface events bypass SDL's logical finger-coordinate rewrite.
   The payload is copied into SDL's queue; no pointers or cross-thread game state. */
#ifndef TOUCH_SURFACE_H
#define TOUCH_SURFACE_H
#include <SDL.h>
#include "touch_controls.h"
typedef struct {
    Uint32 type, timestamp;
    int64_t device, finger;
    float x, y;
    int width, height;
    TouchPhase phase;
    Uint32 sample_ms; /* Android event time; SDL overwrites the queue timestamp. */
} TouchSurfaceEvent;
void touch_surface_init(void);
bool touch_surface_read(const SDL_Event *event, TouchSurfaceEvent *surface);
bool touch_surface_failed(void);
#endif
