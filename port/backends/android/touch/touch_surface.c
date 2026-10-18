#include "touch_surface.h"
#include <string.h>
#ifndef TOUCH_HEADLESS
#include <jni.h>
#endif

static SDL_atomic_t event_type, queue_failed;
_Static_assert(sizeof(TouchSurfaceEvent) <= sizeof(SDL_Event), "Surface event must fit SDL queue");

void touch_surface_init(void) {
    if (SDL_AtomicGet(&event_type)) return;
    Uint32 type = SDL_RegisterEvents(1);
    if (type != (Uint32)-1) SDL_AtomicSet(&event_type, (int)type);
}

bool touch_surface_read(const SDL_Event *event, TouchSurfaceEvent *surface) {
    int type = SDL_AtomicGet(&event_type);
    if (!type || event->type != (Uint32)type) return false;
    memcpy(surface, event, sizeof(*surface));
    return true;
}

bool touch_surface_failed(void) {
    if (!SDL_AtomicSet(&queue_failed, 0)) return false;
    SDL_FlushEvent((Uint32)SDL_AtomicGet(&event_type));
    return true;
}

static void surface_push(int device, int finger, int action, float x, float y,
                         int width, int height, Uint32 timestamp) {
    int type = SDL_AtomicGet(&event_type);
    if (!type) return; /* Game/SDL event loop not ready: wait for fresh fingers. */
    TouchPhase phase;
    switch (action) { /* Android MotionEvent action constants */
    case 0: case 5: phase = TOUCH_DOWN; break;
    case 1: case 6: phase = TOUCH_UP; break;
    case 2: phase = TOUCH_MOTION; break;
    case 3: phase = TOUCH_ABORT; break;
    default: return;
    }
    TouchSurfaceEvent surface = {(Uint32)type, timestamp, device, finger,
        x, y, width, height, phase, timestamp};
    SDL_Event event = {0};
    memcpy(&event, &surface, sizeof(surface));
    if (SDL_PushEvent(&event) != 1) SDL_AtomicSet(&queue_failed, 1);
}

#ifndef TOUCH_HEADLESS
/* NewDarkAndroid: the raw touches come from local.newdarkandroid.ss2.TouchSurface (an SDLSurface subclass) rather than
   from a patched org.libsdl.app.SDLSurface, so SDL's Java sources stay unmodified. */
JNIEXPORT void JNICALL Java_local_newdarkandroid_ss2_TouchSurface_onNativeSurfaceTouch(
    JNIEnv *env, jclass cls, jint device, jint finger, jint action,
    jfloat x, jfloat y, jint width, jint height, jlong timestamp) {
    (void)env; (void)cls;
    surface_push(device, finger, action, x, y, width, height, (Uint32)timestamp);
}
#endif
