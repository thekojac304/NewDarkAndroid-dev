package local.newdarkandroid.ss2;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Phase 1B test screen (later the launcher). Runs the game-free conformance program in NewDarkActivity, one fresh
 * ":game" process per run, and shows what each run reported. The run directory is files/runs/<mode>/ (conformance.exe,
 * run.log, status); results are kept in files/results/ and logged with the tag NewDarkLauncher.
 *
 * From adb: am start -n local.newdarkandroid.ss2/.LauncherActivity --es autorun all   (or default, cr, neg)
 */
public class LauncherActivity extends Activity {
    static final String TAG = "NewDarkLauncher";
    static final String GAME_PROCESS_SUFFIX = ":game";
    // mode, native library, title, port_main arguments (after --verbose)
    static final String[][] MODES = {
        { "default", "ss2conf", "conformance, default lift", "conformance.exe" },
        { "cr", "ss2conf_cr", "conformance, --cache-regs --abi-check", "conformance.exe" },
        // Negative tests, from adb only (--es autorun neg): both must be reported as FAIL with the reason.
        { "neg-exe", "ss2conf", "negative test: wrong executable (expect FAIL)", "missing.exe" },
        { "neg-crash", "ss2conf", "negative test: native crash (expect FAIL)", "--android-crash-test conformance.exe" },
    };
    static final long START_GRACE_MS = 15000;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private SharedPreferences state;            // queue (comma-separated modes), pending (mode), launched (ms)
    private boolean resumed;
    private TextView info, status, results, log;
    private final List<Button> buttons = new ArrayList<>();

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        state = getSharedPreferences("runs", MODE_PRIVATE);
        buildUi();
        Log.i(TAG, "onCreate" + (saved != null ? " (restored)" : "") + ", autorun=" + getIntent().getStringExtra("autorun"));
        if (saved == null) autorun(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        Log.i(TAG, "onNewIntent, autorun=" + intent.getStringExtra("autorun"));
        autorun(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        refresh();
        poll.run();
    }

    @Override
    protected void onPause() {
        resumed = false;
        handler.removeCallbacks(poll);
        super.onPause();
    }

    // ---------------------------------------------------------------- UI
    private TextView text(float sp, boolean mono) {
        TextView t = new TextView(this);
        t.setTextSize(sp);
        if (mono) t.setTypeface(Typeface.MONOSPACE);
        t.setTextIsSelectable(true);
        t.setPadding(0, 12, 0, 12);
        return t;
    }

    private void buildUi() {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(40, 40, 40, 40);
        TextView title = text(20, false);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setText("NewDark SS2: runtime conformance (Phase 1B)");
        col.addView(title);
        col.addView(info = text(12, true));
        addButton(col, "Run default lift", "default");
        addButton(col, "Run --cache-regs --abi-check", "cr");
        addButton(col, "Run both", "all");
        col.addView(status = text(15, false));
        status.setTypeface(Typeface.DEFAULT_BOLD);
        col.addView(results = text(12, true));
        TextView l = text(14, false);
        l.setText("Log of the last run");
        col.addView(l);
        col.addView(log = text(10, true));
        ScrollView sv = new ScrollView(this);
        sv.addView(col);
        setContentView(sv);
        info.setText(String.format(Locale.US, "%s %s\n%s %s, Android %s (API %d), %s\nlauncher pid %d; runs in %s%s",
            getPackageName(), versionName(), Build.MANUFACTURER, Build.MODEL, Build.VERSION.RELEASE, Build.VERSION.SDK_INT,
            String.join(",", Build.SUPPORTED_ABIS), android.os.Process.myPid(), getPackageName(), GAME_PROCESS_SUFFIX)
            + "\ngame folders (adb push): " + gamesDir() + "\nwrite folders (saves, logs): " + new File(getFilesDir(), "profiles"));
    }

    private void addButton(LinearLayout col, String label, String which) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setOnClickListener(v -> startQueue(which));
        col.addView(b);
        buttons.add(b);
    }

    /** Game files are copied here (IMPLEMENTATION_PLAN.md section 2, option A); later phases run the game from it. */
    private File gamesDir() {
        File ext = getExternalFilesDir(null), d = new File(ext != null ? ext : getFilesDir(), "games");
        d.mkdirs();
        return d;
    }

    private String versionName() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; } catch (Exception e) { return "?"; }
    }

    private void refresh() {
        String pending = state.getString("pending", null), queue = state.getString("queue", "");
        boolean busy = pending != null || !queue.isEmpty();
        for (Button b : buttons) b.setEnabled(!busy);
        status.setText(pending != null ? "Running: " + title(pending) + (queue.isEmpty() ? "" : " (then " + queue + ")") : "Idle");
        StringBuilder r = new StringBuilder();
        for (String[] m : MODES) {
            String s = Files.read(resultFile(m[0]));
            r.append(s != null ? s : m[2] + ": not run yet\n").append('\n');
        }
        results.setText(r.toString().trim());
        String last = state.getString("last", null);
        log.setText(last != null ? Files.tail(new File(runDir(last), "run.log"), 300) : "");
    }

    // ---------------------------------------------------------------- runs
    private static String[] mode(String m) {
        for (String[] x : MODES) if (x[0].equals(m)) return x;
        return null;
    }
    private static String title(String m) { String[] x = mode(m); return x != null ? x[2] : m; }
    private File runDir(String m) { return new File(getFilesDir(), "runs/" + m); }
    private File resultFile(String m) { File d = new File(getFilesDir(), "results"); d.mkdirs(); return new File(d, m + ".txt"); }

    private void autorun(Intent intent) {
        String which = intent != null ? intent.getStringExtra("autorun") : null;
        if (which != null) startQueue(which);
    }

    private void startQueue(String which) {
        if (state.getString("pending", null) != null || !state.getString("queue", "").isEmpty()) {
            Log.w(TAG, "a run is already in progress; ignoring " + which);
            return;
        }
        String q = which.equals("all") ? "default,cr" : which.equals("neg") ? "neg-exe,neg-crash" : which;
        for (String m : q.split(",")) if (mode(m) == null) { Log.e(TAG, "unknown mode " + m); return; }
        state.edit().putString("queue", q).apply();
        startNext();
    }

    /** Starts the next queued run, but only from the foreground and once no ":game" process is left. */
    private void startNext() {
        String q = state.getString("queue", "");
        if (q.isEmpty() || state.getString("pending", null) != null) { refresh(); return; }
        if (!resumed || gameProcessAlive()) { handler.postDelayed(poll, 250); return; }
        int comma = q.indexOf(',');
        String m = comma < 0 ? q : q.substring(0, comma), rest = comma < 0 ? "" : q.substring(comma + 1);
        String[] x = mode(m);
        File dir = runDir(m);
        try {
            Files.deleteTree(dir);
            if (!dir.mkdirs()) throw new IOException("cannot create " + dir);
            Files.copy(getAssets().open("conformance/conformance.exe"), new File(dir, "conformance.exe"));
        } catch (IOException e) {
            state.edit().putString("queue", rest).apply();
            finishRun(m, "launcher could not prepare the run: " + e);
            handler.post(poll);
            return;
        }
        state.edit().putString("queue", rest).putString("pending", m).putString("last", m).putLong("launched", System.currentTimeMillis()).apply();
        Log.i(TAG, "starting " + m + ": lib" + x[1] + ".so in " + dir);
        List<String> args = new ArrayList<>(Arrays.asList(dir.getAbsolutePath(), "--verbose"));
        args.addAll(Arrays.asList(x[3].split(" ")));
        Intent i = new Intent(this, NewDarkActivity.class)
            .putExtra(NewDarkActivity.EXTRA_LIB, x[1])
            .putExtra(NewDarkActivity.EXTRA_TITLE, x[2])
            .putExtra(NewDarkActivity.EXTRA_ARGS, args.toArray(new String[0]));
        startActivity(i);
        refresh();
        handler.postDelayed(poll, 250);
    }

    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            handler.removeCallbacks(this);
            String m = state.getString("pending", null);
            if (m == null) { startNext(); return; }
            String st = lastLine(Files.read(new File(runDir(m), "status")));
            boolean alive = gameProcessAlive();
            long age = System.currentTimeMillis() - state.getLong("launched", 0);
            if (alive || (st == null && age < START_GRACE_MS)) {            // still running, or not started yet
                handler.postDelayed(this, 250);
                return;
            }
            finishRun(m, st);
            startNext();
        }
    };

    private boolean gameProcessAlive() {
        ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        List<ActivityManager.RunningAppProcessInfo> ps = am.getRunningAppProcesses();
        if (ps != null) for (ActivityManager.RunningAppProcessInfo p : ps) if (p.processName.equals(getPackageName() + GAME_PROCESS_SUFFIX)) return true;
        return false;
    }

    private static String lastLine(String s) {
        if (s == null) return null;
        String[] l = s.trim().split("\n");
        return l.length == 0 || l[l.length - 1].isEmpty() ? null : l[l.length - 1].trim();
    }

    // ---------------------------------------------------------------- verdict
    private static final Pattern CONF = Pattern.compile("^conformance: (\\d+) of (\\d+) checks failed", Pattern.MULTILINE);
    private static final Pattern PRE = Pattern.compile("^\\[android\\] preflight: (\\d+) of (\\d+) checks failed", Pattern.MULTILINE);
    private static final Pattern CHECK = Pattern.compile("^\\[android\\] check (.+?): (OK|FAIL) \\((.*)\\)$", Pattern.MULTILINE);
    private static final Pattern SPACE = Pattern.compile("guest address space: (.*)$", Pattern.MULTILINE);
    private static final Pattern THREAD = Pattern.compile("^\\[android\\] game thread: (.*)$", Pattern.MULTILINE);
    private static final Pattern EXITP = Pattern.compile("^\\[port\\] ExitProcess\\((\\d+)\\)", Pattern.MULTILINE);
    private static final Pattern SECS = Pattern.compile("^\\[android\\] process exit\\(-?\\d+\\) after ([0-9.]+) s", Pattern.MULTILINE);
    private static final Pattern START = Pattern.compile("^start (\\d+)", Pattern.MULTILINE);

    private void finishRun(String m, String st) {
        File dir = runDir(m);
        String runLog = Files.read(new File(dir, "run.log")), statusText = Files.read(new File(dir, "status"));
        if (runLog == null) runLog = "";
        int abi = count(runLog, "ABI VIOLATION");
        File[] logs = dir.listFiles((d, n) -> n.endsWith(".log") && !n.equals("run.log"));
        if (logs != null) for (File f : logs) { String s = Files.read(f); if (s != null) abi += count(s, "ABI VIOLATION"); }

        Integer exit = null;
        if (st != null && st.startsWith("exit ")) try { exit = Integer.parseInt(st.substring(5).trim()); } catch (NumberFormatException ignored) {}
        Matcher conf = CONF.matcher(runLog), pre = PRE.matcher(runLog), exitp = EXITP.matcher(runLog), secs = SECS.matcher(runLog);
        boolean hasConf = conf.find(), hasPre = pre.find(), hasExitp = exitp.find(), hasSecs = secs.find();
        Matcher pid = START.matcher(statusText != null ? statusText : "");
        boolean pass = exit != null && exit == 0 && hasConf && conf.group(1).equals("0") && !conf.group(2).equals("0")
            && hasPre && pre.group(1).equals("0") && hasExitp && exitp.group(1).equals("0") && abi == 0;

        String outcome = st == null ? "no native start recorded (library not loaded or process never started)"
            : st.startsWith("exit ") ? "process exit " + exit + (hasSecs ? " after " + secs.group(1) + " s" : "")
            : st.startsWith("signal ") ? "NATIVE CRASH: " + st + " (tombstone in logcat)"
            : st.startsWith("start ") ? "process ended without an exit status (killed?)" : st;
        StringBuilder r = new StringBuilder();
        r.append(title(m)).append(": ").append(pass ? "PASS" : "FAIL").append('\n');
        r.append("  conformance: ").append(hasConf ? conf.group(1) + " of " + conf.group(2) + " checks failed" : "no result line").append('\n');
        r.append("  ExitProcess: ").append(hasExitp ? exitp.group(1) : "not reached").append("; ").append(outcome).append('\n');
        r.append("  ABI violations: ").append(abi).append('\n');
        r.append("  preflight: ").append(hasPre ? pre.group(1) + " of " + pre.group(2) + " checks failed" : "did not run").append('\n');
        for (Matcher c = CHECK.matcher(runLog); c.find(); ) r.append("    ").append(c.group(1)).append(": ").append(c.group(2)).append('\n');
        Matcher sp = SPACE.matcher(runLog), th = THREAD.matcher(runLog);
        if (sp.find()) r.append("  guest address space: ").append(sp.group(1)).append('\n');
        if (th.find()) r.append("  game thread: ").append(th.group(1)).append('\n');
        String when = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date());
        r.append("  pid ").append(pid.find() ? pid.group(1) : "?").append(", finished ").append(when).append('\n');

        String line = String.format(Locale.US, "RESULT mode=%s verdict=%s conformance=%s preflight=%s abi_violations=%d exitprocess=%s status=\"%s\" pid=%s seconds=%s",
            m, pass ? "PASS" : "FAIL", hasConf ? conf.group(1) + "/" + conf.group(2) : "none", hasPre ? pre.group(1) + "/" + pre.group(2) : "none", abi,
            hasExitp ? exitp.group(1) : "none", st, pid.find(0) ? pid.group(1) : "?", hasSecs ? secs.group(1) : "?");
        if (pass) Log.i(TAG, line); else Log.e(TAG, line);
        try {
            Files.write(resultFile(m), r.toString(), false);
            Files.write(new File(resultFile(m).getParentFile(), "history.txt"), when + " " + line + "\n", true);
        } catch (IOException e) {
            Log.e(TAG, "cannot save the result: " + e);
        }
        state.edit().remove("pending").apply();
        refresh();
    }

    private static int count(String s, String what) {
        int n = 0;
        for (int i = s.indexOf(what); i >= 0; i = s.indexOf(what, i + what.length())) n++;
        return n;
    }
}
