package local.newdarkandroid.ss2;

import android.content.Context;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.io.File;

/**
 * Runs one native program in this activity's own process (":game"). SDL loads libSDL2.so and the library named by the
 * "lib" extra and calls its SDL_main with the "args" extra (port/backends/android/android_main.c: args[0] is the run
 * directory). The native side writes run.log and status there and ends the process when the program exits, so every
 * run starts in a fresh process; LauncherActivity reads the result from the main process.
 */
public class NewDarkActivity extends SDLActivity {
    static final String EXTRA_LIB = "lib", EXTRA_ARGS = "args", EXTRA_TITLE = "title", EXTRA_OVERLAY = "overlay", EXTRA_TOUCH = "touch";

    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView overlay;
    private File log;
    private String title;

    @Override
    protected String[] getLibraries() {
        String lib = getIntent().getStringExtra(EXTRA_LIB);
        return new String[] { "SDL2", lib != null ? lib : "main" };
    }

    @Override
    protected String[] getArguments() {
        String[] a = getIntent().getStringArrayExtra(EXTRA_ARGS);
        return a != null ? a : new String[0];
    }

    /** Touch controls (game runs of a library built with them): raw touchscreen events go to them, not to SDL. */
    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return getIntent().getBooleanExtra(EXTRA_TOUCH, false) ? new TouchSurface(context) : super.createSDLSurface(context);
    }

    /** Called by the touch controls for accepted control edges when haptics are on (as ShockolateActivity's). */
    public void touchHaptic(int strength, int level) {
        if (!hasWindowFocus() || isFinishing()) return;
        Vibrator vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        if (vibrator == null || !vibrator.hasVibrator()) return;
        int safeStrength = Math.max(1, Math.min(3, strength));
        boolean confirmation = level >= 2;
        int duration = confirmation ? 20 : 10;
        int amplitude = Math.min(180, (confirmation ? 52 : 28) * safeStrength);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) vibrator.vibrate(VibrationEffect.createOneShot(duration, amplitude));
        else vibrator.vibrate(duration);
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        String[] args = getArguments();
        if (mBrokenLibraries || mLayout == null || args.length == 0) return;
        if (!getIntent().getBooleanExtra(EXTRA_OVERLAY, true)) {   // the game's window: landscape, nothing on top
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
            return;
        }
        // A status line over the SDL surface: the tail of the run log while the native program runs.
        log = new File(args[0], "run.log");
        title = getIntent().getStringExtra(EXTRA_TITLE);
        overlay = new TextView(this);
        overlay.setTypeface(Typeface.MONOSPACE);
        overlay.setTextSize(11);
        overlay.setTextColor(Color.WHITE);
        overlay.setGravity(Gravity.BOTTOM);
        overlay.setPadding(32, 32, 32, 32);
        mLayout.addView(overlay, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        handler.post(tail);
    }

    private final Runnable tail = new Runnable() {
        @Override
        public void run() {
            overlay.setText((title != null ? title : "native run") + "\n\n" + Files.tail(log, 40));
            handler.postDelayed(this, 200);
        }
    };

    @Override
    protected void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        super.onDestroy();
    }
}
