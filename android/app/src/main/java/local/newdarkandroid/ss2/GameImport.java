package local.newdarkandroid.ss2;

import android.content.ContentResolver;
import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.util.Log;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeSet;
import java.util.regex.Pattern;

/**
 * Copies a System Shock 2 folder the player picks with Android's folder picker (ACTION_OPEN_DOCUMENT_TREE, no storage
 * permission) into a new game folder, imported/NAME/ (GameFolders), keeping its relative layout: a modded installation
 * (DMM/, cam_mod.ini, OSM/, misdml/) copies as it is. See docs/android-audit/GAME_IMPORT_FIX.md.
 *
 * 1. Scan: the whole tree is listed first (names, sizes, letter-case clashes), SS2.exe and the recompiled modules are
 *    checked against what this app was built from, saves are found, free space is checked. Nothing is written.
 * 2. The player chooses (LauncherActivity): import or not, and whether to bring the installation's saves.
 * 3. Copy: the game goes to imported/NAME.importing/, saves (save_N/) to profiles/NAME.importing/, the new game's own write
 *    folder. Saves never go into the game folder, where the write folder would overlay them file by file (vfs.c) and mix
 *    two saves of one slot. current/ (the PC's working state) is not copied. Then a journal (imported/NAME.commit) is
 *    written and both folders are renamed into place; GameFolders.recover finishes that after a crash, and deletes any
 *    staging folder without a journal. So a half import never looks like a game, and NAME is new everywhere, so no game
 *    folder, save or setting that exists is ever replaced. The selected folder is only read.
 *
 * One import at a time, held in a static so a recreated activity finds it.
 * Adapted from Shockolate's LauncherActivity importer (commit 7502370): tree walk, name checks and depth limit.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
final class GameImport {
    enum Stage { SCANNING, CHOOSING, COPYING, DONE, FAILED }
    private enum Kind { GAME, SAVE, CURRENT }

    static volatile GameImport current;

    private static final int MAX_DEPTH = 32;
    static final long MARGIN = 256L << 20;                      // free space to leave on the phone after the import
    private static final Pattern SAVE = Pattern.compile("(?i)save_\\d+");
    private static final String[] PROJECTION = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE, DocumentsContract.Document.COLUMN_FLAGS };
    // Settings an installation brings. They stay in the game folder and are read from there (the write folder only holds
    // what the game changes later); listed so the player sees they come along.
    private static final String[] CONFIGS = { "cam_mod.ini", "user.cfg", "user.bnd", "cam.cfg", "cam_ext.cfg", "install.cfg" };

    static final class Entry {
        final String doc, rel; final boolean dir; final long size;
        Entry(String doc, String rel, boolean dir, long size) { this.doc = doc; this.rel = rel; this.dir = dir; this.size = size; }
    }

    /** What the scan found: shown to the player before anything is written. */
    static final class Plan {
        String source, name;
        final List<Entry> game = new ArrayList<>(), saves = new ArrayList<>();
        final TreeSet<String> slots = new TreeSet<>((a, b) -> Integer.compare(slotNo(a), slotNo(b)));
        long gameBytes, saveBytes, free = -1; int gameFiles, saveFiles, currentFiles;
        String exeProblem, spaceProblem; final List<String> moduleWarnings = new ArrayList<>(), configs = new ArrayList<>();
        static int slotNo(String s) { try { return Integer.parseInt(s.substring(5)); } catch (RuntimeException e) { return Integer.MAX_VALUE; } }
    }

    final Plan plan = new Plan();
    volatile Stage stage = Stage.SCANNING;
    volatile String status = "Import: reading the selected folder...", message;
    volatile boolean cancel, withSaves;
    private final Context app;
    private final ContentResolver resolver;
    private final Uri tree;
    private final GameFolders folders;
    private final Map<String, String> test = new HashMap<>();       // adb test settings (LauncherActivity, --es import_test)
    private long copied, files, lastReport;

    private GameImport(Context c, Uri tree) {
        app = c.getApplicationContext(); resolver = app.getContentResolver(); this.tree = tree; folders = new GameFolders(app);
        for (String kv : prefs().getString("import_test", "").split(",")) { int i = kv.indexOf('='); if (i > 0) test.put(kv.substring(0, i).trim(), kv.substring(i + 1).trim()); }
    }

    private SharedPreferences prefs() { return app.getSharedPreferences("runs", Context.MODE_PRIVATE); }

    static boolean busy() { GameImport j = current; return j != null && (j.stage == Stage.SCANNING || j.stage == Stage.COPYING); }

    /** Starts the scan on a worker thread; the result waits for choose(). */
    static GameImport start(Context c, Uri tree) {
        GameImport j = new GameImport(c, tree);
        current = j;
        if (!j.test.isEmpty()) Log.w(LauncherActivity.TAG, "import: test settings active: " + j.test);
        new Thread(() -> {
            try { j.scan(); j.status = "Import: waiting for your choice."; j.stage = Stage.CHOOSING; }
            catch (Exception e) { j.fail(e, false); }
        }, "game-import-scan").start();
        return j;
    }

    /** The player's answer: copy (with or without the saves), or not at all. */
    void choose(boolean go, boolean saves) {
        if (stage != Stage.CHOOSING) return;
        if (!go) { message = status = "Import cancelled. Nothing was copied."; stage = Stage.DONE; Log.i(LauncherActivity.TAG, "import: cancelled before copying"); return; }
        withSaves = saves && !plan.saves.isEmpty();
        stage = Stage.COPYING;
        new Thread(() -> {
            try { copy(); }
            catch (Exception e) { fail(e, true); }
        }, "game-import").start();
    }

    private void fail(Exception e, boolean wrote) {
        Log.e(LauncherActivity.TAG, "import failed", e);
        String why = e instanceof Cancelled ? "you cancelled it." : explain(e);
        message = status = "Import failed: " + why + "\n" + (wrote
            ? "Nothing was changed: the partial copy was removed, and your other game folders, saves and settings are as they were. The selected folder was only read."
            : "Nothing was copied.");
        stage = Stage.FAILED;
    }

    // ---------------------------------------------------------------- 1. scan

    private void scan() throws IOException {
        String root = DocumentsContract.getTreeDocumentId(tree);
        plan.source = displayName(root);
        Log.i(LauncherActivity.TAG, "import: scanning \"" + plan.source + "\" (" + tree + "); destination " + folders.imported
            + "; games/ is " + GameFolders.owner(folders.adb) + ", the app is uid " + android.os.Process.myUid());
        scanDir(root, "", 0, Kind.GAME);
        Entry exe = null;
        for (Entry e : plan.game) if (!e.dir && e.rel.equalsIgnoreCase("SS2.exe")) exe = e;
        if (exe == null)
            throw new IOException("the folder \"" + plan.source + "\" has no SS2.exe. Pick the System Shock 2 folder itself (the one with SS2.exe, "
                + "Data and cam_mod.ini in it), patched to NewDark 2.48.");
        long[] want = GameFolders.expectedExe();
        if (want != null) try (InputStream in = open(exe)) { plan.exeProblem = GameFolders.exeProblem(in, want); }
        checkModules();
        for (String c : CONFIGS) for (Entry e : plan.game) if (!e.dir && e.rel.equalsIgnoreCase(c)) plan.configs.add(e.rel);

        String base = plan.source.toLowerCase(Locale.US).replaceAll("[^a-z0-9._-]+", "-").replaceAll("^[-.]+|[-.]+$", "");
        if (!GameFolders.isGameName(base)) base = base.isEmpty() ? "ss2" : "ss2-" + base.replace('.', '-');
        String name = base;
        for (int n = 2; folders.taken(name); n++) name = base + "-" + n;
        plan.name = name;

        long need = plan.gameBytes + plan.saveBytes + MARGIN;
        folders.imported.mkdirs();
        plan.free = Math.min(free(folders.imported), free(folders.profiles.getParentFile()));   // both are on /data on current phones
        if (plan.free < need) plan.spaceProblem = String.format(Locale.US, "there is not enough free storage. The import needs %s (%s for the game, %s for its saves, "
            + "and %s left over for Android), but only %s is free. Free up %s and import again.", mb(need), mb(plan.gameBytes), mb(plan.saveBytes), mb(MARGIN), mb(plan.free), mb(need - plan.free));
        Log.i(LauncherActivity.TAG, String.format(Locale.US, "import: scan of \"%s\": %d files, %s; %d saves (%s) in %d files; current/ %d file(s) (not copied); free %s; name %s; exe %s; modules %s; space %s",
            plan.source, plan.gameFiles, mb(plan.gameBytes), plan.slots.size(), mb(plan.saveBytes), plan.saveFiles, plan.currentFiles, mb(plan.free), name,
            plan.exeProblem == null ? "OK" : plan.exeProblem, plan.moduleWarnings.isEmpty() ? "OK" : plan.moduleWarnings, plan.spaceProblem == null ? "OK" : "too little"));
    }

    private long free(File dir) {
        String t = test.get("free");
        return t != null ? Long.parseLong(t) : dir.getUsableSpace();
    }

    /** Lists doc (at rel) and below. Top-level save_N/ folders are saves, current/ is skipped, everything else is the game. */
    private void scanDir(String doc, String rel, int depth, Kind kind) throws IOException {
        if (depth > MAX_DEPTH) throw new IOException("the folder is nested too deeply (more than " + MAX_DEPTH + " levels) at " + rel + ".");
        Map<String, String> seen = new HashMap<>();
        try (Cursor c = resolver.query(DocumentsContract.buildChildDocumentsUriUsingTree(tree, doc), PROJECTION, null, null, null)) {
            if (c == null) throw new IOException("Android did not let the app list " + (rel.isEmpty() ? "the selected folder" : rel) + ". Pick the folder again.");
            while (c.moveToNext()) {
                String name = c.getString(1), r = rel.isEmpty() ? name : rel + "/" + name;
                checkName(name, rel);
                String clash = seen.put(name.toLowerCase(Locale.ROOT), name);
                if (clash != null)
                    throw new IOException("\"" + clash + "\" and \"" + name + "\" in " + (rel.isEmpty() ? "the selected folder" : rel) + " differ only in letter case. Android's "
                        + "storage (like Windows) cannot hold both, and the game would not know which one to use. Rename or remove one of them on the PC and import again.");
                boolean dir = DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2));
                if (!dir && !c.isNull(4) && (c.getInt(4) & DocumentsContract.Document.FLAG_VIRTUAL_DOCUMENT) != 0) {
                    Log.w(LauncherActivity.TAG, "import: skipping the virtual document " + r);
                    continue;
                }
                long size = dir || c.isNull(3) ? -1 : c.getLong(3);
                Kind k = kind;
                if (rel.isEmpty() && dir) k = SAVE.matcher(name).matches() ? Kind.SAVE : name.equalsIgnoreCase("current") ? Kind.CURRENT : Kind.GAME;
                if (k == Kind.CURRENT) { if (!dir) plan.currentFiles++; }
                else if (k == Kind.SAVE) {
                    if (rel.isEmpty()) plan.slots.add(name);
                    plan.saves.add(new Entry(c.getString(0), r, dir, size));
                    if (!dir) { plan.saveFiles++; plan.saveBytes += Math.max(size, 0); }
                } else {
                    plan.game.add(new Entry(c.getString(0), r, dir, size));
                    if (!dir) { plan.gameFiles++; plan.gameBytes += Math.max(size, 0); }
                }
                if (dir) scanDir(c.getString(0), r, depth + 1, k);
                long now = System.currentTimeMillis();
                if (now - lastReport > 250) { lastReport = now; status = String.format(Locale.US, "Import: reading the selected folder: %,d files found...", plan.gameFiles + plan.saveFiles + plan.currentFiles); }
            }
        } catch (SecurityException e) {
            throw new IOException("Android withdrew the app's access to the selected folder (" + e.getMessage() + "). Pick the folder again.", e);
        }
    }

    private static void checkName(String name, String rel) throws IOException {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..") || name.indexOf('/') >= 0 || name.indexOf('\0') >= 0)
            throw new IOException("the selected folder contains an unusable file name (\"" + name + "\" in " + (rel.isEmpty() ? "the top folder" : rel) + ").");
    }

    /** Copies of the recompiled modules (allobjs.osm, Squirrel.osm, lgvid.dll, NVScript.osm) in another version are not
     *  loaded by the game (modules.c): warn, by comparing SizeOfImage and ImageBase in the PE header. A build may hold
     *  several versions of one module (two NVScript.osm): a copy is fine when it has the header of any of them. */
    private void checkModules() {
        if (BuildConfig.GAME_MODULES.isEmpty()) return;
        Map<String, List<long[]>> versions = new LinkedHashMap<>();       // lower-case name -> {SizeOfImage, ImageBase} of each version
        Map<String, String> names = new HashMap<>();
        for (String m : BuildConfig.GAME_MODULES.split(",")) {
            String[] p = m.split(":");
            if (p.length != 3) continue;
            String lower = p[0].toLowerCase(Locale.ROOT);
            versions.computeIfAbsent(lower, k -> new ArrayList<>()).add(new long[] { Long.parseLong(p[1], 16), Long.parseLong(p[2], 16) });
            names.put(lower, p[0]);
        }
        for (Map.Entry<String, List<long[]>> v : versions.entrySet()) {
            String lower = v.getKey();
            List<String> other = new ArrayList<>();
            for (Entry e : plan.game) {
                String r = e.rel.toLowerCase(Locale.ROOT);
                if (e.dir || !(r.equals(lower) || r.endsWith("/" + lower))) continue;
                long[] pe = peHeader(e);
                if (pe == null) continue;
                boolean known = false;
                for (long[] k : v.getValue()) known |= pe[0] == k[0] && pe[1] == k[1];
                if (!known) other.add(e.rel);
            }
            if (other.isEmpty()) continue;                                     // one note per module, naming a few of the copies
            String where = String.join(", ", other.subList(0, Math.min(3, other.size()))) + (other.size() > 3 ? " and " + (other.size() - 3) + " more" : "");
            String name = names.get(lower);
            plan.moduleWarnings.add(name + " (" + where + ") is " + (v.getValue().size() > 1 ? "none of the " + v.getValue().size() + " versions" : "another version than the one")
                + " recompiled into this app, so the game will not load it"
                + (lower.equals("nvscript.osm") ? "; mods that need NVScript, such as SCP, will not work" : lower.equals("squirrel.osm") ? "; Squirrel scripts will not run" : ""));
        }
    }

    private long[] peHeader(Entry e) {
        byte[] h = new byte[4096];
        try (InputStream in = open(e)) {
            int n = 0;
            for (int r; n < h.length && (r = in.read(h, n, h.length - n)) > 0; ) n += r;
            if (n < 64) return null;
            int lf = le32(h, 0x3c);
            if (lf < 0 || lf + 24 + 60 > n || h[lf] != 'P' || h[lf + 1] != 'E') return null;
            return new long[] { le32(h, lf + 24 + 56) & 0xffffffffL, le32(h, lf + 24 + 28) & 0xffffffffL };
        } catch (IOException | RuntimeException x) {
            return null;
        }
    }

    private static int le32(byte[] b, int o) { return (b[o] & 0xff) | (b[o + 1] & 0xff) << 8 | (b[o + 2] & 0xff) << 16 | (b[o + 3] & 0xff) << 24; }

    // ---------------------------------------------------------------- 3. copy and commit

    private static final class Cancelled extends IOException { Cancelled() { super("cancelled"); } }

    private void copy() throws IOException {
        File gameStage = new File(folders.imported, plan.name + GameFolders.STAGING), game = new File(folders.imported, plan.name);
        File saveStage = new File(folders.profiles, plan.name + GameFolders.STAGING), profile = new File(folders.profiles, plan.name);
        File journal = new File(folders.imported, plan.name + GameFolders.JOURNAL);
        long total = total();
        Log.i(LauncherActivity.TAG, "import: copying \"" + plan.source + "\" into " + gameStage + (withSaves ? ", and " + plan.slots.size() + " saves into " + saveStage : " (saves not imported)"));
        try {
            if (plan.spaceProblem != null) throw new IOException(plan.spaceProblem);
            if (folders.taken(plan.name)) throw new IOException("the name " + plan.name + " was taken while you were choosing; import again.");
            mkdir(folders.imported, 0770, true);
            mkdir(gameStage, 0770, false);
            for (Entry e : plan.game) put(e, gameStage, 0770, total);
            if (withSaves) {
                mkdir(folders.profiles, 0700, true);
                mkdir(saveStage, 0700, false);
                for (Entry e : plan.saves) put(e, saveStage, 0700, total);
            }
            int want = plan.gameFiles + (withSaves ? plan.saveFiles : 0);
            if (GameFolders.exe(gameStage) == null) throw new IOException("SS2.exe did not arrive in the copy.");
            if (files != want) throw new IOException("only " + files + " of " + want + " files arrived.");
            Files.write(journal, plan.name + "\n", false);                      // from here on, recover() finishes the import
            if ("commit".equals(test.get("kill"))) { Log.w(LauncherActivity.TAG, "import: test kill after the journal"); android.os.Process.killProcess(android.os.Process.myPid()); }
            if (!gameStage.renameTo(game)) throw new IOException("could not rename " + gameStage + " to " + game + ".");
            if (withSaves && !saveStage.renameTo(profile)) {
                game.renameTo(gameStage);                                          // back out: no game without the saves it was promised
                throw new IOException("could not rename " + saveStage + " to " + profile + ".");
            }
            journal.delete();
        } catch (IOException | RuntimeException e) {
            journal.delete();
            Files.deleteTree(gameStage);
            Files.deleteTree(saveStage);
            throw e instanceof IOException ? (IOException) e : new IOException(e.toString(), e);
        }
        prefs().edit().putString("game", plan.name).apply();
        StringBuilder m = new StringBuilder(String.format(Locale.US, "Imported \"%s\": %,d files (%s) into the new game folder %s, which is now selected.", plan.source, plan.gameFiles, mb(plan.gameBytes), plan.name));
        if (withSaves) m.append(String.format(Locale.US, " Its %d saved games (%s) are in that game's own save folder.", plan.slots.size(), slotRange()));
        else if (!plan.saves.isEmpty()) m.append(String.format(Locale.US, " Its %d saved games were not imported; they are still in the original folder.", plan.slots.size()));
        if (plan.exeProblem != null) m.append(" It cannot be started with this app: ").append(plan.exeProblem).append('.');
        message = status = m.toString();
        stage = Stage.DONE;
        Log.i(LauncherActivity.TAG, "import: " + message);
    }

    private long total() { return plan.gameBytes + (withSaves ? plan.saveBytes : 0); }

    /** "save_0, save_3" for a few slots, "save_0 to save_14" for many. */
    String slotRange() { return plan.slots.size() <= 4 ? String.join(", ", plan.slots) : plan.slots.first() + " to " + plan.slots.last(); }

    private static void mkdir(File d, int mode, boolean mayExist) throws IOException {
        try {
            Os.mkdir(d.getPath(), mode);
        } catch (ErrnoException e) {
            if (e.errno == OsConstants.EEXIST && mayExist && d.isDirectory()) return;
            File p = d.getParentFile();
            throw new IOException("Android did not let the app create the folder " + d + " (" + e.getMessage() + ")"
                + (e.errno == OsConstants.EACCES && p != null ? "; its parent folder belongs to " + GameFolders.owner(p) + ", and this app is uid " + android.os.Process.myUid() : "") + ".", e);
        }
    }

    private void put(Entry e, File stage, int dirMode, long total) throws IOException {
        if (cancel) throw new Cancelled();
        File to = new File(stage, e.rel);
        if (e.dir) { mkdir(to, dirMode, false); return; }
        long n = 0, enospc = test.containsKey("enospc") ? Long.parseLong(test.get("enospc")) : -1;
        byte[] buf = new byte[1 << 20];
        try (InputStream in = open(e)) {
            FileOutputStream out;
            try { out = new FileOutputStream(to); }
            catch (FileNotFoundException x) { throw new IOException("cannot write " + to + " (" + x.getMessage() + ").", x); }
            try (FileOutputStream o = out) {
                for (int r; (r = in.read(buf)) > 0; ) {
                    if (cancel) throw new Cancelled();
                    if (enospc >= 0 && copied + r > enospc) throw new IOException("write failed: ENOSPC (No space left on device) [import_test]", new ErrnoException("write", OsConstants.ENOSPC));
                    o.write(buf, 0, r); n += r; copied += r;
                    if ("copy".equals(test.get("kill")) && copied > total / 2) { Log.w(LauncherActivity.TAG, "import: test kill halfway through the copy"); android.os.Process.killProcess(android.os.Process.myPid()); }
                    report(total);
                }
                o.getFD().sync();
            }
        } catch (SecurityException x) {
            throw new IOException("Android withdrew the app's access to the selected folder while copying " + e.rel + ". Pick the folder again.", x);
        }
        if (e.size >= 0 && n != e.size) throw new IOException(e.rel + " was copied incompletely (" + n + " of " + e.size + " bytes). Is the folder still being changed?");
        files++;
        report(total);
    }

    private InputStream open(Entry e) throws IOException {
        InputStream in;
        try { in = resolver.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, e.doc)); }
        catch (SecurityException x) { throw new IOException("Android did not let the app read " + e.rel + " in the selected folder (" + x.getMessage() + "). Pick the folder again.", x); }
        catch (FileNotFoundException x) { throw new IOException(e.rel + " is no longer in the selected folder (" + x.getMessage() + ").", x); }
        if (in == null) throw new IOException("cannot open " + e.rel + " in the selected folder.");
        return in;
    }

    private void report(long total) {
        long now = System.currentTimeMillis();
        if (now - lastReport < 250) return;
        lastReport = now;
        int all = plan.gameFiles + (withSaves ? plan.saveFiles : 0);
        status = String.format(Locale.US, "Importing \"%s\" as %s: %,d of %,d files, %s of %s (%d%%). Keep the app open; Cancel import stops and removes the copy.",
            plan.source, plan.name, files, all, mb(copied), mb(total), total > 0 ? (int) (copied * 100 / total) : 100);
    }

    /** A message for the player. Running out of space is recognised wherever it happens; the rest already says what failed. */
    private String explain(Throwable e) {
        for (Throwable t = e; t != null; t = t.getCause()) {
            String s = String.valueOf(t.getMessage());
            if ((t instanceof ErrnoException && ((ErrnoException) t).errno == OsConstants.ENOSPC) || s.contains("ENOSPC") || s.contains("No space left")) {
                long now = Math.max(0, free(folders.imported)), need = total() + MARGIN;          // after the partial copy was removed
                return String.format(Locale.US, "the phone ran out of storage space after %s of %s had been copied. %s is free now; the import needs %s%s.",
                    mb(copied), mb(total()), mb(now), mb(need), need > now ? ", so free up " + mb(need - now) + " and import again" : "");
            }
        }
        String s = e.getMessage();
        return s == null || s.isEmpty() ? e.toString() : s.endsWith(".") ? s : s + ".";
    }

    static String mb(long b) { return b >= 10L << 30 ? String.format(Locale.US, "%.1f GB", b / 1073741824.0) : String.format(Locale.US, "%,d MB", (b + (1 << 19)) >> 20); }

    private String displayName(String doc) {
        try (Cursor c = resolver.query(DocumentsContract.buildDocumentUriUsingTree(tree, doc), PROJECTION, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(1);
        } catch (RuntimeException ignored) {}
        return "ss2";
    }
}
