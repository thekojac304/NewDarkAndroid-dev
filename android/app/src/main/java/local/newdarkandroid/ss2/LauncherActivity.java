package local.newdarkandroid.ss2;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
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
        // Phase 1C: the recompiled game, run from games/ss2/ (read only); saves and configuration go to files/profiles/ss2/.
        // Arguments are built in startNext. From adb, --ei frames N sets a frame limit (bounded test runs; 0: none), and
        // --es env NAME=value,NAME=value adds settings (e.g. SS2PORT_PROFILE=1,SS2PORT_INPUT_DEBUG=1), and --es portargs
        // adds port arguments before the executable (e.g. "--trace-only GetKeyState,MapVirtualKeyA"). The game mode has
        // touch controls; --ez touch false turns them off (touches then reach SDL as before).
        { "game-null", "ss2null", "game, headless (null backend)", "" },
        { "game", "ss2game", "game, SDL2 + OpenGL ES", "" },
    };
    // The game folder: games/<name>/SS2.exe, with its own write folder profiles/<name>/ (saves, configuration), so a modded
    // installation (e.g. games/ss2scp, SCP) never shares saves with the vanilla one (games/ss2, the default). Chosen with
    // the Game folder button or --es game NAME (kept).
    static final String DEFAULT_GAME = "ss2";
    static boolean isGame(String m) { return m.startsWith("game"); }
    // Frame limit when none is given (0: none). The windowed game is played, so it runs until the player quits; only the
    // headless mode, which nobody can quit, stops by itself. Bounded windowed runs pass --ei frames N.
    static int defaultFrames(String m) { return m.equals("game-null") ? 600 : 0; }
    // Interface size (Phase 2B-1): NewDark's own d3d_disp_scaled_2d_overlay, the resolution the game draws its HUD,
    // inventory, PDA and in-game text at before scaling them over the 3D picture (which keeps the game resolution).
    // NewDark keeps the height and widens it to the screen's aspect; it ignores anything below 640x480, the size the SS2
    // interface was laid out for. Setting value (null: left to cam_ext.cfg or your own user.cfg), key, label.
    static final String[][] UI_SIZES = {
        { "640 480", "640x480", "largest (640x480, the original layout)" },
        { "720 540", "720x540", "large (720x540)" },
        { "800 600", "800x600", "medium (800x600)" },
        { null, "game", "game config (cam_ext.cfg, whole-number steps only)" },
    };
    static final String UI_MARK = "; NewDarkAndroid launcher, Interface size: it rewrites the next line on every game start";
    static final long START_GRACE_MS = 15000;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private SharedPreferences state;            // queue (comma-separated modes), pending (mode), launched (ms)
    private boolean resumed;
    private TextView info, status, results, log;
    private final List<Button> buttons = new ArrayList<>();
    private static final int REQUEST_IMPORT = 0x5353;
    private TextView importStatus;
    private volatile boolean importing;

    /** The picked folder is copied into games/NAME on a worker thread (GameImport); the new folder becomes the game folder. */
    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != REQUEST_IMPORT) return;
        Uri tree = data != null ? data.getData() : null;
        if (result != RESULT_OK || tree == null) { importStatus.setText("Import: no folder was picked."); return; }
        importing = true;
        importStatus.setText("Importing...");
        new Thread(() -> {
            String msg;
            try {
                GameImport.Result r = new GameImport(getContentResolver(), tree, m -> runOnUiThread(() -> importStatus.setText(m))).run(gamesDir());
                state.edit().putString("game", r.name).apply();
                msg = String.format(Locale.US, "Imported %d files (%.0f MB) into the game folder %s. It is now the selected game folder.", r.files, r.bytes / 1048576.0, r.name);
                Log.i(TAG, "import: " + msg);
            } catch (Exception e) {
                msg = "Import failed: " + e.getMessage() + " Nothing was changed.";
                Log.e(TAG, "import failed", e);
            }
            final String m = msg;
            runOnUiThread(() -> { importing = false; importStatus.setText(m); buildUi(); importStatus.setText(m); refresh(); });
        }, "game-import").start();
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        // Opened from the home screen while the task already exists (say a game started from adb is on top): show the
        // task as it is instead of another launcher on top of the game.
        Intent in = getIntent();
        if (!isTaskRoot() && Intent.ACTION_MAIN.equals(in.getAction()) && in.hasCategory(Intent.CATEGORY_LAUNCHER)) { finish(); return; }
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
        buttons.clear();                                                      // rebuilt after an import
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(40, 40, 40, 40);
        TextView title = text(22, false);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setText("System Shock 2 (NewDark, recompiled)");
        col.addView(title);
        // Normal play: one obvious button. Touch controls on, no frame limit, the Interface size below.
        Button play = new Button(this);
        play.setAllCaps(false);
        play.setTextSize(20);
        play.setText("Play");
        play.setOnClickListener(v -> { state.edit().putInt("frames", -1).putString("env", "").putString("portargs", "").putBoolean("touch", true).apply(); startQueue("game"); });
        col.addView(play);
        buttons.add(play);
        Button folder = new Button(this);
        folder.setAllCaps(false);
        folder.setText(gameLabel());
        folder.setOnClickListener(v -> {                                      // cycles through the folders that have SS2.exe
            List<String> g = installedGames(); if (g.isEmpty()) return;
            int i = g.indexOf(game());
            state.edit().putString("game", g.get((i + 1) % g.size())).apply();
            folder.setText(gameLabel());
        });
        col.addView(folder);
        Button size = new Button(this);
        size.setAllCaps(false);
        size.setText("Interface size: " + uiSize()[2]);
        size.setOnClickListener(v -> {                                        // cycles; applied at the next game start
            int i = 0; while (i < UI_SIZES.length && UI_SIZES[i] != uiSize()) i++;
            state.edit().putString("ui_size", UI_SIZES[(i + 1) % UI_SIZES.length][1]).apply();
            size.setText("Interface size: " + uiSize()[2]);
        });
        col.addView(size);
        Button imp = new Button(this);
        imp.setAllCaps(false);
        imp.setText("Import a game folder...");
        imp.setOnClickListener(v -> {                                          // Android's folder picker; no storage permission
            if (importing) return;
            try { startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_IMPORT); }
            catch (android.content.ActivityNotFoundException e) { importStatus.setText("This device has no folder picker. Copy the game with adb push (android/README.md)."); }
        });
        col.addView(imp);
        col.addView(importStatus = text(13, false));
        importStatus.setText("Game folders: " + (installedGames().isEmpty() ? "none yet. Import your System Shock 2 folder (NewDark 2.48, modded or not), or copy it with adb push." : String.join(", ", installedGames())));
        TextView diag = text(14, false);
        diag.setTypeface(Typeface.DEFAULT_BOLD);
        diag.setText(BuildConfig.DIAGNOSTICS ? "Diagnostics" : "About");
        col.addView(diag);
        col.addView(info = text(12, true));
        if (BuildConfig.DIAGNOSTICS) {                                        // the dev build: conformance and the headless game
            addButton(col, "Conformance: default lift", "default");
            addButton(col, "Conformance: --cache-regs --abi-check", "cr");
            addButton(col, "Conformance: both", "all");
            addButton(col, "Game: headless, 600 frames", "game-null");
        }
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
        b.setOnClickListener(v -> { state.edit().putInt("frames", -1).putString("env", "").putString("portargs", "").putBoolean("touch", true).apply(); startQueue(which); });
        col.addView(b);
        buttons.add(b);
    }

    /** Game files are copied here (IMPLEMENTATION_PLAN.md section 2, option A); later phases run the game from it. */
    private File gamesDir() {
        File ext = getExternalFilesDir(null), d = new File(ext != null ? ext : getFilesDir(), "games");
        d.mkdirs();
        return d;
    }

    private String game() { return state.getString("game", DEFAULT_GAME); }
    private String gameLabel() { return "Game folder: " + game() + (new File(new File(gamesDir(), game()), "SS2.exe").isFile() ? "" : " (no SS2.exe there)"); }
    private List<String> installedGames() {
        List<String> g = new ArrayList<>();
        File[] d = gamesDir().listFiles();
        if (d != null) for (File f : d) if (!f.getName().endsWith(".importing") && new File(f, "SS2.exe").isFile()) g.add(f.getName());   // not a half import
        java.util.Collections.sort(g);
        return g;
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
        if (which == null) return;
        String env = intent.getStringExtra("env"), pa = intent.getStringExtra("portargs");   // game runs: NAME=value settings; port arguments
        state.edit().putInt("frames", intent.getIntExtra("frames", -1))                // -1: each game mode's default
            .putString("env", env != null ? env.trim() : "").putString("portargs", pa != null ? pa.trim() : "")
            .putBoolean("touch", intent.getBooleanExtra("touch", true)).apply();        // --ez touch false: no touch controls
        String g = intent.getStringExtra("game");                                       // --es game NAME: games/NAME (kept)
        if (g != null && g.matches("[A-Za-z0-9._-]+")) state.edit().putString("game", g).apply();
        String size = intent.getStringExtra("uisize");                                  // --es uisize 640x480|720x540|800x600|game (kept)
        if (size != null) for (String[] u : UI_SIZES) if (u[1].equals(size)) state.edit().putString("ui_size", u[1]).apply();
        startQueue(which);
    }

    private void startQueue(String which) {
        settleDeadRun();
        if (state.getString("pending", null) != null || !state.getString("queue", "").isEmpty()) {
            Log.w(TAG, "a run is already in progress; ignoring " + which);
            return;
        }
        String q = which.equals("all") ? "default,cr" : which.equals("neg") ? "neg-exe,neg-crash" : which;
        for (String m : q.split(",")) if (mode(m) == null) { Log.e(TAG, "unknown mode " + m); return; }
        if (!BuildConfig.DIAGNOSTICS && !q.equals("game")) { Log.e(TAG, q + ": not in the play build (install the dev build for diagnostics)"); return; }
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
        File dir = runDir(m), exe = new File(new File(gamesDir(), game()), "SS2.exe"), profile = new File(getFilesDir(), "profiles/" + game());
        try {
            Files.deleteTree(dir);
            if (!dir.mkdirs()) throw new IOException("cannot create " + dir);
            if (!isGame(m)) Files.copy(getAssets().open("conformance/conformance.exe"), new File(dir, "conformance.exe"));
            else if (!exe.isFile()) throw new IOException("there is no System Shock 2 in the game folder \"" + game() + "\" (" + exe
                + " not found). Tap Import a game folder to copy your NewDark 2.48 installation, pick another Game folder, or copy one with adb push.");
            else if (!profile.isDirectory() && !profile.mkdirs()) throw new IOException("cannot create " + profile);
            if (isGame(m)) applyUiSize(new File(profile, "user.cfg"));
        } catch (IOException e) {
            state.edit().putString("queue", rest).apply();
            finishRun(m, "launcher could not prepare the run: " + e);
            handler.post(poll);
            return;
        }
        state.edit().putString("queue", rest).putString("pending", m).putString("last", m).putLong("launched", System.currentTimeMillis()).apply();
        int frames = state.getInt("frames", -1);
        if (frames < 0) frames = defaultFrames(m);
        Log.i(TAG, "starting " + m + ": lib" + x[1] + ".so in " + dir + (isGame(m) ? ", game folder " + game() + ", frame limit " + (frames > 0 ? frames : "none") : ""));
        List<String> args = new ArrayList<>(Arrays.asList(dir.getAbsolutePath(), "--verbose"));
        if (isGame(m)) {
            args.addAll(Arrays.asList("--android-env", "SS2PORT_WRITE_DIR=" + profile.getAbsolutePath()));
            if (!state.getBoolean("touch", true)) args.addAll(Arrays.asList("--android-env", "SS2PORT_TOUCH=0"));   // no touch controls
            for (String e : state.getString("env", "").split("[ ,]")) if (e.contains("=")) args.addAll(Arrays.asList("--android-env", e));
            for (String e : state.getString("portargs", "").split(" ")) if (!e.isEmpty()) args.add(e);
            if (frames > 0) args.addAll(Arrays.asList("--frames", Integer.toString(frames)));
            args.add(exe.getAbsolutePath());
        } else args.addAll(Arrays.asList(x[3].split(" ")));
        Intent i = new Intent(this, NewDarkActivity.class)
            .putExtra(NewDarkActivity.EXTRA_LIB, x[1])
            .putExtra(NewDarkActivity.EXTRA_TITLE, x[2])
            .putExtra(NewDarkActivity.EXTRA_OVERLAY, !m.equals("game"))      // the game's own picture needs no log on top
            .putExtra(NewDarkActivity.EXTRA_TOUCH, m.equals("game") && state.getBoolean("touch", true))   // touch controls (libss2game)
            .putExtra(NewDarkActivity.EXTRA_ARGS, args.toArray(new String[0]));
        startActivity(i);
        refresh();
        handler.postDelayed(poll, 250);
    }

    private String[] uiSize() {
        String k = state.getString("ui_size", UI_SIZES[0][1]);
        for (String[] u : UI_SIZES) if (u[1].equals(k)) return u;
        return UI_SIZES[0];
    }

    /** Puts the Interface size into the profile's user.cfg (which cam.cfg includes after cam_ext.cfg, so it wins). Only
     *  the marked line is the launcher's; anything else in the file stays as it is. */
    private void applyUiSize(File cfg) throws IOException {
        String old = cfg.isFile() ? Files.read(cfg) : "", v = uiSize()[0];
        if (old == null) throw new IOException("cannot read " + cfg);
        StringBuilder out = new StringBuilder();
        String[] lines = old.split("\r?\n", -1);
        for (int i = 0; i < lines.length; i++) {
            if (lines[i].equals(UI_MARK)) { i++; continue; }                  // the mark and its line: written again below
            if (i == lines.length - 1 && lines[i].isEmpty()) break;           // the file's final newline
            out.append(lines[i]).append("\r\n");
        }
        if (v != null) out.append(UI_MARK).append("\r\n").append("d3d_disp_scaled_2d_overlay ").append(v).append("\r\n");
        String now = out.toString();
        Log.i(TAG, "interface size: " + uiSize()[1] + (v != null ? " (d3d_disp_scaled_2d_overlay " + v + " in " + cfg + ")" : " (user.cfg not set)"));
        if (now.equals(old)) return;
        if (now.isEmpty()) { if (!cfg.delete()) throw new IOException("cannot delete " + cfg); return; }   // the launcher made it
        File tmp = new File(cfg.getPath() + ".tmp");
        Files.write(tmp, now, false);
        if (!tmp.renameTo(cfg)) throw new IOException("cannot replace " + cfg);
    }

    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            handler.removeCallbacks(this);
            if (state.getString("pending", null) == null || settleDeadRun()) { startNext(); return; }
            handler.postDelayed(this, 250);                                  // still running, or not started yet
        }
    };

    /** Reports the pending run if its process has ended (exited, crashed, or killed with am force-stop); true if it did. */
    private boolean settleDeadRun() {
        String m = state.getString("pending", null);
        if (m == null) return false;
        String st = lastLine(Files.read(new File(runDir(m), "status")));
        long age = System.currentTimeMillis() - state.getLong("launched", 0);
        if (gameProcessAlive() || (st == null && age < START_GRACE_MS)) return false;
        finishRun(m, st);
        return true;
    }

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
    private static final Pattern STOP = Pattern.compile("^\\[port\\] stopping after (\\d+) frames", Pattern.MULTILINE);

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
        boolean game = isGame(m);
        Matcher stop = STOP.matcher(runLog);
        boolean first = runLog.contains("[port] first frame presented"), hasStop = stop.find(), fatal = runLog.contains("[port] FATAL");
        int modules = count(runLog, ": loaded the recompiled module at");
        // A game run passes when it presented frames and ended by itself with exit 0: at its frame limit, or (no limit) on quit.
        boolean pass = game
            ? exit != null && exit == 0 && hasPre && pre.group(1).equals("0") && first && !fatal && abi == 0
            : exit != null && exit == 0 && hasConf && conf.group(1).equals("0") && !conf.group(2).equals("0")
              && hasPre && pre.group(1).equals("0") && hasExitp && exitp.group(1).equals("0") && abi == 0;

        String outcome = st == null ? "no native start recorded (library not loaded or process never started)"
            : st.startsWith("exit ") ? "process exit " + exit + (hasSecs ? " after " + secs.group(1) + " s" : "")
            : st.startsWith("signal ") ? "NATIVE CRASH: " + st + " (tombstone in logcat)"
            : st.startsWith("start ") ? "process ended without an exit status (killed?)" : st;
        StringBuilder r = new StringBuilder();
        r.append(title(m)).append(": ").append(pass ? "PASS" : "FAIL").append('\n');
        if (game) {
            r.append("  frames: ").append(first ? "first frame presented" : "none presented").append(hasStop ? "; stopped after " + stop.group(1) : "").append('\n');
            r.append("  recompiled modules loaded: ").append(modules).append(fatal ? "; FATAL error in the log" : "").append('\n');
            r.append("  ").append(outcome).append('\n');
        } else {
            r.append("  conformance: ").append(hasConf ? conf.group(1) + " of " + conf.group(2) + " checks failed" : "no result line").append('\n');
            r.append("  ExitProcess: ").append(hasExitp ? exitp.group(1) : "not reached").append("; ").append(outcome).append('\n');
        }
        r.append("  ABI violations: ").append(abi).append('\n');
        r.append("  preflight: ").append(hasPre ? pre.group(1) + " of " + pre.group(2) + " checks failed" : "did not run").append('\n');
        for (Matcher c = CHECK.matcher(runLog); c.find(); ) r.append("    ").append(c.group(1)).append(": ").append(c.group(2)).append('\n');
        Matcher sp = SPACE.matcher(runLog), th = THREAD.matcher(runLog);
        if (sp.find()) r.append("  guest address space: ").append(sp.group(1)).append('\n');
        if (th.find()) r.append("  game thread: ").append(th.group(1)).append('\n');
        String when = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date());
        r.append("  pid ").append(pid.find() ? pid.group(1) : "?").append(", finished ").append(when).append('\n');

        String line = game
            ? String.format(Locale.US, "RESULT mode=%s verdict=%s first_frame=%s frames=%s modules=%d fatal=%s preflight=%s status=\"%s\" pid=%s seconds=%s",
                m, pass ? "PASS" : "FAIL", first ? "yes" : "no", hasStop ? stop.group(1) : "?", modules, fatal ? "yes" : "no",
                hasPre ? pre.group(1) + "/" + pre.group(2) : "none", st, pid.find(0) ? pid.group(1) : "?", hasSecs ? secs.group(1) : "?")
            : String.format(Locale.US, "RESULT mode=%s verdict=%s conformance=%s preflight=%s abi_violations=%d exitprocess=%s status=\"%s\" pid=%s seconds=%s",
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
