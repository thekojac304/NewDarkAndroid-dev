package local.newdarkandroid.ss2;

import android.content.Context;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;

import org.libsdl.app.SDLSurface;

/**
 * The SDL surface of a game run with touch controls. Touchscreen events go to the native touch controls as raw surface
 * pixels with the Android event time (port/backends/android/touch/touch_surface.c), instead of SDL's finger events,
 * whose coordinates SDL rewrites. Mouse events (a physical mouse, DeX) still go to SDL unchanged.
 *
 * This is Shockolate's SDLSurface.java patch (commit 7502370) as a subclass, so SDL's own Java sources stay unmodified.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
public class TouchSurface extends SDLSurface {
    // Native queues these; it never runs game code on the UI thread.
    private static native void onNativeSurfaceTouch(int device, int finger, int action,
            float x, float y, int width, int height, long timestamp);

    public TouchSurface(Context context) {
        super(context);
    }

    @Override
    public boolean onTouch(View v, MotionEvent event) {
        int source = event.getSource();
        if (source == InputDevice.SOURCE_MOUSE || source == (InputDevice.SOURCE_MOUSE | InputDevice.SOURCE_TOUCHSCREEN))
            return super.onTouch(v, event);
        int touchDevId = event.getDeviceId();
        if (touchDevId < 0) touchDevId -= 1;   // as SDLSurface: -1 is SDL's synthetic id
        final int pointerCount = event.getPointerCount();
        int action = event.getActionMasked();
        int i = -1;
        switch (action) {
            case MotionEvent.ACTION_MOVE:
                for (i = 0; i < pointerCount; i++)
                    onNativeSurfaceTouch(touchDevId, event.getPointerId(i), action,
                            event.getX(i), event.getY(i), (int) mWidth, (int) mHeight, event.getEventTime());
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_DOWN:
                i = 0;   // primary pointer up/down: the index is always zero
                /* fallthrough */
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_POINTER_DOWN:
                if (i == -1) i = event.getActionIndex();
                onNativeSurfaceTouch(touchDevId, event.getPointerId(i), action,
                        event.getX(i), event.getY(i), (int) mWidth, (int) mHeight, event.getEventTime());
                break;
            case MotionEvent.ACTION_CANCEL:
                // A cancellation is never a tap or a release-to-click.
                onNativeSurfaceTouch(touchDevId, 0, action, 0, 0, (int) mWidth, (int) mHeight, event.getEventTime());
                break;
            default:
                break;
        }
        return true;
    }
}
