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
import android.view.View;
import android.widget.ImageView;
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
 * The launcher (Phase 1B's test screen; Phase 3G made it a game launcher, see buildUi). Runs the game-free conformance program in NewDarkActivity, one fresh
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
        { "640 480", "640x480", "Largest: 640 x 480, the original layout" },
        { "720 540", "720x540", "Large: 720 x 540" },
        { "800 600", "800x600", "Medium: 800 x 600" },
        { null, "game", "The game's own setting (cam_ext.cfg, whole steps only)" },
    };
    static final String UI_MARK = "; NewDarkAndroid launcher, Interface size: it rewrites the next line on every game start";
    static final long START_GRACE_MS = 15000;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private SharedPreferences state;            // queue (comma-separated modes), pending (mode), launched (ms)
    private boolean resumed;
    private TextView info, status, results, log;
    private final List<View> buttons = new ArrayList<>();
    private static final int REQUEST_IMPORT = 0x5353;
    private TextView importStatus;
    private TextView importButton;                         // the Import row's title (Phase 3G)
    private GameFolders folders;
    private GameImport shownChoice;                     // the import whose choice dialog this activity shows
    private GameImport.Stage shownStage;

    /** The picked folder is scanned on a worker thread (GameImport); the player then chooses, and it is copied. */
    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != REQUEST_IMPORT) return;
        Uri tree = data != null ? data.getData() : null;
        if (result != RESULT_OK || tree == null) { importStatus.setText("Import: no folder was picked."); return; }
        if (GameImport.busy()) return;
        Log.i(TAG, "import: picked " + tree);
        GameImport.start(this, tree);
        importPoll.run();
    }

    /** Follows the import while this activity is in front: progress, the choice dialog, the result. */
    private final Runnable importPoll = new Runnable() {
        @Override
        public void run() {
            handler.removeCallbacks(this);
            GameImport j = GameImport.current;
            if (j == null || importStatus == null) return;
            GameImport.Stage s = j.stage;
            boolean active = s == GameImport.Stage.SCANNING || s == GameImport.Stage.COPYING;
            setText(importButton, s == GameImport.Stage.COPYING ? (j.cancel ? "Cancelling..." : "Cancel import") : active ? "Reading the folder..." : "Import a game folder...");
            if (active) getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            else getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            if (s == GameImport.Stage.DONE || s == GameImport.Stage.FAILED) {
                if (shownStage != s) {                                          // once: the new game folder is listed and selected
                    shownStage = s;
                    buildUi();
                    importStatus.setText(j.message);
                    refresh();
                }
                return;
            }
            shownStage = s;
            setText(importStatus, j.status);
            if (s == GameImport.Stage.CHOOSING && shownChoice != j) { shownChoice = j; showImportChoice(j); }
            handler.postDelayed(this, 250);
        }
    };

    private static void setText(TextView t, String s) { if (!s.contentEquals(t.getText())) t.setText(s); }   // no redraw (and no busy UI) when nothing changed

    /** What the scan found, and the choice: import (with or without the saves) or not. Nothing has been written yet. */
    private void showImportChoice(GameImport j) {
        GameImport.Plan p = j.plan;
        List<String> others = folders.list();
        StringBuilder t = new StringBuilder();
        if (p.spaceProblem != null) t.append("Cannot import: ").append(p.spaceProblem).append("\n\n");
        if (p.exeProblem != null) t.append("Warning: this game will not start. ").append(Character.toUpperCase(p.exeProblem.charAt(0))).append(p.exeProblem.substring(1))
            .append(". Import it anyway only to keep its mods and saves on the phone until you replace its SS2.exe.\n\n");
        t.append(String.format(Locale.US, "Copies %,d files (%s) into a new game folder, %s. The selected folder is only read. Your other game folders (%s), their saves "
            + "and your touch settings are not changed.\n\n", p.gameFiles, GameImport.mb(p.gameBytes), p.name, others.isEmpty() ? "none" : String.join(", ", others)));
        if (!p.slots.isEmpty()) t.append(String.format(Locale.US, "Saved games: %d (%s, %s). If you import them, they go into %s's own save folder, which is new and empty: "
            + "no save is overwritten, and they never mix with another game folder's saves.\n\n", p.slots.size(), j.slotRange(), GameImport.mb(p.saveBytes), p.name));
        else t.append("Saved games: none in this folder.\n\n");
        if (!p.configs.isEmpty()) t.append("Its settings come along and are used as they are: ").append(String.join(", ", p.configs)).append(".\n\n");
        if (p.currentFiles > 0) t.append(String.format(Locale.US, "Not copied: current/ (%d files), the working state of the PC's last session; the game makes a new one.\n\n", p.currentFiles));
        for (String w : p.moduleWarnings) t.append("Note: ").append(w).append(".\n");
        if (p.free >= 0) t.append(String.format(Locale.US, "\nFree storage: %s.", GameImport.mb(p.free)));
        LinearLayout v = new LinearLayout(this);
        v.setOrientation(LinearLayout.VERTICAL);
        v.setPadding(56, 24, 56, 0);
        TextView msg = new TextView(this);
        msg.setText(t.toString().trim());
        msg.setTextIsSelectable(true);
        v.addView(msg);
        android.widget.CheckBox saves = new android.widget.CheckBox(this);
        saves.setText(String.format(Locale.US, p.slots.size() == 1 ? "Also import the saved game" : "Also import the %d saved games", p.slots.size()));
        saves.setChecked(true);
        if (!p.slots.isEmpty()) v.addView(saves);
        ScrollView sv = new ScrollView(this);
        sv.addView(v);
        android.app.AlertDialog.Builder b = new android.app.AlertDialog.Builder(this).setTitle("Import \"" + p.source + "\"?").setView(sv).setCancelable(false)
            .setNegativeButton("Cancel", (d, w) -> { j.choose(false, false); importPoll.run(); });
        if (p.spaceProblem == null)
            b.setPositiveButton(p.exeProblem != null ? "Import anyway" : "Import", (d, w) -> {
                Log.i(TAG, "import: confirmed, saves " + (p.slots.isEmpty() ? "none" : saves.isChecked() ? "included" : "not included"));
                j.choose(true, saves.isChecked());
                importPoll.run();
            });
        LauncherUi.style(b.show());
    }

    /** adb test settings for the importer (GAME_IMPORT_FIX.md), kept until cleared: --es import_test "free=BYTES,enospc=BYTES,kill=copy|commit" or "". */
    private void importTest(Intent in) {
        String t = in != null ? in.getStringExtra("import_test") : null;
        if (t == null) return;
        state.edit().putString("import_test", t.trim()).apply();
        Log.w(TAG, "import test settings: " + (t.trim().isEmpty() ? "cleared" : t.trim()));
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        // Opened from the home screen while the task already exists (say a game started from adb is on top): show the
        // task as it is instead of another launcher on top of the game.
        Intent in = getIntent();
        if (!isTaskRoot() && Intent.ACTION_MAIN.equals(in.getAction()) && in.hasCategory(Intent.CATEGORY_LAUNCHER)) { finish(); return; }
        state = getSharedPreferences("runs", MODE_PRIVATE);
        folders = new GameFolders(this);
        importTest(in);
        labelIntent(in);
        if (GameImport.current == null) {                                      // a fresh process: no import of ours is running
            String r = folders.recover();
            if (!r.isEmpty()) state.edit().putString("recovered", "An earlier import did not finish: " + r).apply();
        }
        buildUi();
        Log.i(TAG, "onCreate" + (saved != null ? " (restored)" : "") + ", autorun=" + getIntent().getStringExtra("autorun")
                + (BuildConfig.SIDE_LABEL.isEmpty() ? "" : ", side-by-side build \"" + BuildConfig.SIDE_LABEL + "\" " + getPackageName() + " " + versionName()));
        if (saved == null) autorun(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        importTest(intent);
        labelIntent(intent);
        if (intent.getStringExtra("label_game") != null && intent.getStringExtra("autorun") == null) buildUi();
        Log.i(TAG, "onNewIntent, autorun=" + intent.getStringExtra("autorun"));
        autorun(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        refresh();
        poll.run();
        importPoll.run();
    }

    @Override
    protected void onPause() {
        resumed = false;
        handler.removeCallbacks(poll);
        handler.removeCallbacks(importPoll);
        super.onPause();
    }

    // ---------------------------------------------------------------- UI (Phase 3G: a game launcher; the look is LauncherUi)
    // Main screen: the title, the selected installation, one Play button, then Controls guide, Interface size, Import and
    // Advanced. Advanced (closed at first) holds what the test screen used to show: version and folders, the dev build's
    // diagnostics, the last run's verdict and log.
    private TextView play, cardFamily, cardName, cardDetail, cardState, change;
    private LinearLayout advanced;
    private View sizeRow, advancedRow;
    private boolean advancedOpen;

    private int dp(float v) { return LauncherUi.dp(this, v); }

    private TextView text(float sp, boolean mono) {                           // the Advanced section's report text
        TextView t = LauncherUi.text(this, "", sp, LauncherUi.TEXT_2, mono ? LauncherUi.MONO : LauncherUi.BODY);
        t.setTextIsSelectable(true);
        t.setPadding(0, dp(6), 0, dp(6));
        return t;
    }

    private void buildUi() {
        buttons.clear();                                                      // rebuilt after an import
        getWindow().setStatusBarColor(LauncherUi.BG_TOP);
        getWindow().setNavigationBarColor(LauncherUi.BG_BOTTOM);
        android.util.DisplayMetrics dm = getResources().getDisplayMetrics();
        boolean wide = dm.widthPixels / dm.density >= 720;                    // a phone in landscape, a tablet: two columns
        LinearLayout left = LauncherUi.column(this), right = wide ? LauncherUi.column(this) : left;

        // -- title
        LinearLayout head = LauncherUi.row(this);
        ImageView icon = new ImageView(this);
        icon.setImageResource(R.mipmap.ic_launcher);
        head.addView(icon, new LinearLayout.LayoutParams(dp(64), dp(64)));
        LinearLayout words = LauncherUi.column(this);
        words.setPadding(dp(14), 0, 0, 0);
        TextView title = LauncherUi.text(this, "NEWDARK SS2", 30, LauncherUi.TEXT, LauncherUi.CONDENSED);
        title.setLetterSpacing(.14f);
        words.addView(title, LauncherUi.wrap());
        TextView sub = LauncherUi.text(this, "System Shock 2  ·  NewDark 2.48 for Android", 13, LauncherUi.TEXT_2, LauncherUi.BODY);
        sub.setPadding(0, dp(5), 0, 0);
        words.addView(sub, LauncherUi.wrap());
        TextView tag = LauncherUi.caption(this, "Unofficial fan port");
        tag.setTextColor(LauncherUi.TEXT_3);
        tag.setPadding(0, dp(7), 0, 0);
        words.addView(tag, LauncherUi.wrap());
        head.addView(words, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        left.addView(head, LauncherUi.fill());
        if (!BuildConfig.SIDE_LABEL.isEmpty()) {                               // a side-by-side build (build.gradle -Pside)
            TextView side = LauncherUi.text(this, BuildConfig.SIDE_LABEL + " build (" + getPackageName() + "): a separate app with its own games and saves",
                13, LauncherUi.WARN, LauncherUi.BODY);
            left.addView(side, LauncherUi.top(this, 12));
        }

        // -- the selected installation
        LinearLayout card = LauncherUi.panel(this);
        LinearLayout capRow = LauncherUi.row(this);
        capRow.addView(LauncherUi.caption(this, "Installation"), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        capRow.addView(cardState = LauncherUi.status(this, "", LauncherUi.OK), LauncherUi.wrap());
        card.addView(capRow, LauncherUi.fill());
        card.addView(cardFamily = LauncherUi.text(this, "", 13, LauncherUi.TEXT_2, LauncherUi.CONDENSED_LIGHT), LauncherUi.top(this, 12));
        cardName = LauncherUi.text(this, "", 24, LauncherUi.TEXT, LauncherUi.CONDENSED);
        LauncherUi.ellipsize(cardName, 2);
        card.addView(cardName, LauncherUi.top(this, 3));
        card.addView(cardDetail = LauncherUi.text(this, "", 13, LauncherUi.TEXT_2, LauncherUi.BODY), LauncherUi.top(this, 6));
        LinearLayout acts = LauncherUi.row(this);
        change = LauncherUi.outlineButton(this, "Change");
        change.setOnClickListener(v -> chooseInstallation());
        acts.addView(change, LauncherUi.wrap());
        TextView rename = LauncherUi.outlineButton(this, "Rename");
        rename.setOnClickListener(v -> editLabel());
        LinearLayout.LayoutParams rp = LauncherUi.wrap(); rp.leftMargin = dp(10);
        acts.addView(rename, rp);
        card.addView(acts, LauncherUi.top(this, 14));
        card.setOnLongClickListener(v -> { editLabel(); return true; });     // as the Game folder button: hold to name it
        left.addView(card, LauncherUi.top(this, 24));

        // -- Play: touch controls on, no frame limit, the Interface size below
        play = LauncherUi.primaryButton(this, "PLAY");
        play.setOnClickListener(v -> { state.edit().putInt("frames", -1).putString("env", "").putString("portargs", "").putBoolean("touch", true).apply(); startQueue("game"); });
        left.addView(play, LauncherUi.top(this, 16));
        buttons.add(play);
        importStatus = LauncherUi.text(this, "", 13, LauncherUi.TEXT, LauncherUi.BODY);   // import progress, why a start failed
        importStatus.setTextIsSelectable(true);
        importStatus.addTextChangedListener(new android.text.TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void afterTextChanged(android.text.Editable s) { importStatus.setVisibility(s.length() == 0 ? View.GONE : View.VISIBLE); }
        });
        importStatus.setVisibility(View.GONE);
        left.addView(importStatus, LauncherUi.top(this, 12));

        // -- controls, interface size, import, advanced
        LinearLayout menu = LauncherUi.panel(this);
        menu.setPadding(dp(14), dp(4), dp(14), dp(4));
        View guide = LauncherUi.menuRow(this, "Controls guide", "Moving, looking, the radial menus and the two cursor styles");
        guide.setOnClickListener(v -> controlsGuide());
        menu.addView(guide, LauncherUi.fill());
        menu.addView(LauncherUi.divider(this));
        sizeRow = LauncherUi.menuRow(this, "Interface size", uiSize()[2]);
        sizeRow.setOnClickListener(v -> chooseUiSize());
        menu.addView(sizeRow, LauncherUi.fill());
        menu.addView(LauncherUi.divider(this));
        View imp = LauncherUi.menuRow(this, "Import a game folder...", "Copy a System Shock 2 installation from this phone's storage");
        importButton = (TextView) imp.findViewWithTag("title");
        imp.setOnClickListener(v -> {                                          // Android's folder picker; no storage permission
            GameImport j = GameImport.current;
            if (j != null && j.stage == GameImport.Stage.COPYING) { j.cancel = true; Log.i(TAG, "import: cancel requested"); importPoll.run(); return; }
            if (GameImport.busy()) return;
            try { startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_IMPORT); }
            catch (android.content.ActivityNotFoundException e) { importStatus.setText("This device has no folder picker. Copy the game with adb push (android/README.md)."); }
        });
        menu.addView(imp, LauncherUi.fill());
        menu.addView(LauncherUi.divider(this));
        advancedRow = LauncherUi.menuRow(this, "Advanced", BuildConfig.DIAGNOSTICS ? "Diagnostics, logs and version details" : "Logs and version details");
        advancedRow.setOnClickListener(v -> showAdvanced(!advancedOpen));
        menu.addView(advancedRow, LauncherUi.fill());
        right.addView(menu, LauncherUi.top(this, wide ? 0 : 24));

        // -- advanced: what the test screen showed, closed until asked for
        advanced = LauncherUi.panel(this);
        advanced.addView(LauncherUi.caption(this, BuildConfig.DIAGNOSTICS ? "Diagnostics" : "About this build"), LauncherUi.fill());
        advanced.addView(info = text(11, true), LauncherUi.fill());
        if (BuildConfig.DIAGNOSTICS) {                                        // the dev build: conformance and the headless game
            addButton(advanced, "Conformance: default lift", "default");
            addButton(advanced, "Conformance: --cache-regs --abi-check", "cr");
            addButton(advanced, "Conformance: both", "all");
            addButton(advanced, "Game: headless, 600 frames", "game-null");
        }
        advanced.addView(LauncherUi.caption(this, "Last runs"), LauncherUi.top(this, 14));
        advanced.addView(status = text(14, false), LauncherUi.fill());
        status.setTextColor(LauncherUi.TEXT);
        advanced.addView(results = text(11, true), LauncherUi.fill());
        advanced.addView(LauncherUi.caption(this, "Log of the last run"), LauncherUi.top(this, 14));
        advanced.addView(log = text(10, true), LauncherUi.fill());
        right.addView(advanced, LauncherUi.top(this, 16));

        TextView foot = LauncherUi.text(this, "An unofficial, non-commercial fan project. It runs your own copy of System Shock 2 (NewDark 2.48) and is "
            + "not affiliated with or endorsed by the game's developers or publishers.\nVersion " + versionName() + ".", 11, LauncherUi.TEXT_3, LauncherUi.BODY);
        right.addView(foot, LauncherUi.top(this, 20));

        // -- layout: one centred column, or two side by side
        int pad = dp(wide ? 28 : 20), width = Math.min(dm.widthPixels - 2 * pad, dp(wide ? 1040 : 560));
        LinearLayout content = left;
        if (wide) {
            content = LauncherUi.row(this);
            content.setGravity(android.view.Gravity.TOP);
            content.addView(left, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
            LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
            p.leftMargin = dp(24);
            content.addView(right, p);
        }
        android.widget.FrameLayout holder = new android.widget.FrameLayout(this);
        holder.setPadding(0, dp(wide ? 24 : 36), 0, dp(32));
        holder.addView(content, new android.widget.FrameLayout.LayoutParams(width, android.widget.FrameLayout.LayoutParams.WRAP_CONTENT, android.view.Gravity.CENTER_HORIZONTAL));
        ScrollView sv = new ScrollView(this);
        sv.setFillViewport(true);
        sv.setVerticalScrollBarEnabled(false);
        sv.addView(holder);
        android.widget.FrameLayout root = new android.widget.FrameLayout(this);
        root.addView(new LauncherUi.Backdrop(this));
        root.addView(sv);
        setContentView(root);

        info.setText(String.format(Locale.US, "%s %s\n%s %s, Android %s (API %d), %s\nlauncher pid %d; runs in %s%s",
            getPackageName(), versionName(), Build.MANUFACTURER, Build.MODEL, Build.VERSION.RELEASE, Build.VERSION.SDK_INT,
            String.join(",", Build.SUPPORTED_ABIS), android.os.Process.myPid(), getPackageName(), GAME_PROCESS_SUFFIX)
            + "\ngame folders (adb push): " + folders.adb + "\nimported game folders: " + folders.imported + "\nwrite folders (saves, logs): " + folders.profiles);
        String recovered = state.getString("recovered", null);
        if (recovered != null) { importStatus.setText(recovered); state.edit().remove("recovered").apply(); }
        showAdvanced(advancedOpen);
        updateCard();
    }

    private void addButton(LinearLayout col, String label, String which) {
        TextView b = LauncherUi.outlineButton(this, label);
        b.setOnClickListener(v -> { state.edit().putInt("frames", -1).putString("env", "").putString("portargs", "").putBoolean("touch", true).apply(); startQueue(which); });
        col.addView(b, LauncherUi.top(this, 8));
        buttons.add(b);
    }

    private void showAdvanced(boolean open) {
        advancedOpen = open;
        if (advanced != null) advanced.setVisibility(open ? View.VISIBLE : View.GONE);
        View chevron = advancedRow != null ? advancedRow.findViewWithTag("chevron") : null;
        if (chevron != null) chevron.setRotation(open ? 90 : 0);
    }

    /** The installation card: its name (the label, without a leading "System Shock 2 —"), folder, saves and state. */
    private void updateCard() {
        if (cardName == null) return;
        List<String> games = folders.list();
        String g = game(), label = GameFolders.label(this, g), name = label.isEmpty() ? g : label, family = "";
        for (String p : new String[] { "System Shock 2 — ", "System Shock 2 – ", "System Shock 2 - " })
            if (name.startsWith(p) && name.length() > p.length()) { family = "System Shock 2"; name = name.substring(p.length()); break; }
        change.setVisibility(games.size() > 1 ? View.VISIBLE : View.GONE);
        if (games.isEmpty()) {
            cardFamily.setVisibility(View.GONE);
            cardName.setText("No game installed yet");
            cardDetail.setText("Import your System Shock 2 folder (NewDark 2.48, with or without mods) with Import a game folder below, or copy it with adb push.");
            cardState.setText("●  NOT INSTALLED");
            cardState.setTextColor(LauncherUi.WARN);
            return;
        }
        cardFamily.setText(family);
        cardFamily.setVisibility(family.isEmpty() ? View.GONE : View.VISIBLE);
        cardName.setText(name);
        File exe = GameFolders.exe(folders.dir(g));
        String problem = exe == null ? "No SS2.exe" : GameFolders.exeProblem(exe) != null ? "SS2.exe not supported" : null;
        int saves = 0;
        File[] slots = new File(folders.profiles, g).listFiles((d, n) -> n.matches("(?i)save_\\d+"));
        if (slots != null) for (File s : slots) if (s.isDirectory()) saves++;
        cardDetail.setText(String.format(Locale.US, "Folder %s  ·  %s", g,
            saves == 0 ? "no saved games yet" : saves == 1 ? "1 saved game" : saves + " saved games")
            + (problem == null ? "" : exe == null ? "\nThere is no SS2.exe in this folder." : "\nIts SS2.exe is not the NewDark 2.48 one this app was built for."));
        cardState.setText("●  " + (problem == null ? "READY" : problem.toUpperCase(Locale.ROOT)));
        cardState.setTextColor(problem == null ? LauncherUi.OK : LauncherUi.WARN);
    }

    /** Change: the installations, by name; picking one selects it (kept, as the Game folder button did). */
    private void chooseInstallation() {
        List<String> g = folders.list();
        if (g.isEmpty()) return;
        String[] items = new String[g.size()];
        for (int i = 0; i < items.length; i++) items[i] = GameFolders.shown(this, g.get(i));
        LauncherUi.style(new android.app.AlertDialog.Builder(this).setTitle("Choose an installation")
            .setSingleChoiceItems(items, g.indexOf(game()), (d, which) -> {
                state.edit().putString("game", g.get(which)).apply();
                Log.i(TAG, "game folder: " + g.get(which));
                updateCard();
                d.dismiss();
            })
            .setNegativeButton("Cancel", null).show());
    }

    private void chooseUiSize() {
        String[] items = new String[UI_SIZES.length];
        int now = 0;
        for (int i = 0; i < items.length; i++) { items[i] = UI_SIZES[i][2]; if (UI_SIZES[i] == uiSize()) now = i; }
        LauncherUi.style(new android.app.AlertDialog.Builder(this).setTitle("Interface size (HUD, inventory, PDA, text)")
            .setSingleChoiceItems(items, now, (d, which) -> {                   // applied at the next game start
                state.edit().putString("ui_size", UI_SIZES[which][1]).apply();
                LauncherUi.detail(sizeRow, uiSize()[2]);
                d.dismiss();
            })
            .setNegativeButton("Cancel", null).show());
    }

    /** A short reference to the touch controls. */
    private void controlsGuide() {
        String[][] parts = {
            { "Move and look", "Drag anywhere on the left half for the floating stick. Swipe the right half to look around. "
                + "FIRE shoots while held and aims as you drag; USE opens doors and picks things up; JUMP, CROUCH and RELOAD do what they say." },
            { "Tap or hold", "Tap < or > to switch weapons; hold either for the weapon wheel. Hold AMMO for ammunition and firing modes "
                + "(psi powers with the psi amp). Hold ITEM for hypos and patches. Tap II for the menu; hold it for quicksave, quickload and logs." },
            { "Radial menus", "A held button opens its wheel right where your finger is. Slide onto an entry and lift to choose it. "
                + "Lift on the button itself, or slide well past the wheel, to cancel. At the edge of the screen the entries fan out towards the middle." },
            { "Cursor", "MODE switches between LOOK and CURSOR (inventory, PDA, keypads). Direct cursor: tap what you want to click. "
                + "Relative cursor: drag like a trackpad, tap to click, hold still to pick something up and drag it. Choose the style in SET, MORE." },
            { "Settings in the game", "SET opens the touch settings: layout editor, button size and opacity, look speed, haptics, the cursor style and the wheels. "
                + "KBD shows the keyboard (save names, the console). A physical keyboard and mouse also work." },
        };
        LinearLayout col = LauncherUi.column(this);
        col.setPadding(dp(24), dp(8), dp(24), dp(8));
        for (String[] p : parts) {
            col.addView(LauncherUi.caption(this, p[0]), LauncherUi.top(this, 14));
            col.addView(LauncherUi.text(this, p[1], 14, LauncherUi.TEXT, LauncherUi.BODY), LauncherUi.top(this, 6));
        }
        ScrollView sv = new ScrollView(this);
        sv.addView(col);
        LauncherUi.style(new android.app.AlertDialog.Builder(this).setTitle("Controls guide").setView(sv).setPositiveButton("Close", null).show());
    }

    private String game() { return state.getString("game", DEFAULT_GAME); }
    /** Rename: a name for the selected installation, only shown here (blank removes it). */
    private void editLabel() {
        String name = game();
        android.widget.EditText e = new android.widget.EditText(this);
        e.setSingleLine(true); e.setText(GameFolders.label(this, name)); e.setHint("for example: System Shock 2 - SCP Beta 4 (Legacy)");
        LinearLayout box = LauncherUi.column(this);
        box.setPadding(dp(20), dp(4), dp(20), 0);
        box.addView(e, LauncherUi.fill());
        LauncherUi.style(new android.app.AlertDialog.Builder(this).setTitle("Name for \"" + name + "\"")
            .setMessage("Only shown in this launcher. The game, its saves and which script modules load do not depend on it.")
            .setView(box)
            .setPositiveButton("Save", (d, w) -> { GameFolders.setLabel(this, name, e.getText().toString()); updateCard(); })
            .setNegativeButton("Cancel", null).show());
    }
    /** adb: --es label_game NAME --es label TEXT names a game folder (a blank TEXT removes the name); kept. */
    private void labelIntent(Intent in) {
        String g = in != null ? in.getStringExtra("label_game") : null, l = in != null ? in.getStringExtra("label") : null;
        if (g != null && l != null && GameFolders.isGameName(g)) GameFolders.setLabel(this, g, l);
    }

    private String versionName() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; } catch (Exception e) { return "?"; }
    }

    private void refresh() {
        String pending = state.getString("pending", null), queue = state.getString("queue", "");
        boolean busy = pending != null || !queue.isEmpty();
        for (View b : buttons) { b.setEnabled(!busy); b.setAlpha(busy ? .45f : 1f); }
        if (play != null) setText(play, busy ? "RUNNING..." : "PLAY");
        status.setText(pending != null ? "Running: " + title(pending) + (queue.isEmpty() ? "" : " (then " + queue + ")") : "Idle");
        StringBuilder r = new StringBuilder();
        for (String[] m : MODES) {
            if (!BuildConfig.DIAGNOSTICS && !m[0].equals("game")) continue;   // the play build runs the game only
            String s = Files.read(resultFile(m[0]));
            r.append(s != null ? s : m[2] + ": not run yet\n").append('\n');
        }
        results.setText(r.toString().trim());
        String last = state.getString("last", null);
        log.setText(last != null ? Files.tail(new File(runDir(last), "run.log"), 300) : "");
        updateCard();
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
        File dir = runDir(m), gameDir = folders.dir(game()), found = GameFolders.exe(gameDir), exe = found != null ? found : new File(gameDir, "SS2.exe");
        File profile = new File(folders.profiles, game());
        try {
            Files.deleteTree(dir);
            if (!dir.mkdirs()) throw new IOException("cannot create " + dir);
            String bad;
            if (!isGame(m)) Files.copy(getAssets().open("conformance/conformance.exe"), new File(dir, "conformance.exe"));
            else if (!exe.isFile()) throw new IOException("there is no System Shock 2 in the game folder \"" + game() + "\" (" + exe
                + " not found). Tap Import a game folder to copy your NewDark 2.48 installation, pick another Game folder, or copy one with adb push.");
            else if ((bad = GameFolders.exeProblem(exe)) != null) throw new IOException("the game folder \"" + game() + "\" cannot be started: " + bad
                + ". Put the NewDark 2.48 SS2.exe into " + gameDir + ", or pick another Game folder.");
            else if (!profile.isDirectory() && !profile.mkdirs()) throw new IOException("cannot create " + profile);
            if (isGame(m)) applyUiSize(new File(profile, "user.cfg"), gameDir);
            if (isGame(m)) {                // an OpenAL sound driver (silent here) becomes DirectSound: SoundDevice.java
                File own = GameFolders.findIgnoreCase(profile, "cam.cfg");
                String sound = SoundDevice.apply(own != null ? own : new File(profile, "cam.cfg"), GameFolders.findIgnoreCase(gameDir, "cam.cfg"));
                if (sound != null) Log.i(TAG, sound);
            }
        } catch (IOException e) {
            if (isGame(m) && importStatus != null) importStatus.setText("Cannot start: " + e.getMessage());   // next to Play, not only in the results
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
     *  the marked line is the launcher's; anything else in the file stays as it is. The profile's user.cfg hides the game
     *  folder's (vfs.c reads the write folder first), so a profile without one starts from the game folder's copy: an
     *  imported installation keeps its own user.cfg settings. */
    private void applyUiSize(File cfg, File gameDir) throws IOException {
        File seed = cfg.isFile() ? null : GameFolders.findIgnoreCase(gameDir, "user.cfg");
        String old = cfg.isFile() ? Files.read(cfg) : seed != null && seed.isFile() ? Files.read(seed) : "", v = uiSize()[0];
        if (old == null) throw new IOException("cannot read " + (seed != null ? seed : cfg));
        if (seed != null && seed.isFile()) Log.i(TAG, "user.cfg: the new profile starts from " + seed);
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
        if (now.equals(old) && seed == null) return;
        if (now.isEmpty()) { if (cfg.exists() && !cfg.delete()) throw new IOException("cannot delete " + cfg); return; }   // the launcher made it
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
