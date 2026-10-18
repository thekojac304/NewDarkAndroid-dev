package local.newdarkandroid.ss2;

import android.content.Context;
import android.system.ErrnoException;
import android.system.Os;
import android.system.StructStat;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.zip.CRC32;

/**
 * Where game folders live (GAME_IMPORT_FIX.md). Two roots in the app's external files folder, both read by the game:
 *
 *   games/NAME/      copied with adb push. adb creates games/ as the shell account (uid 2000), so the app can read it
 *                    but cannot create anything in it; it is never written by the app.
 *   imported/NAME/   the app's own imports (GameImport), created by the app, so always writable by it. adb can still
 *                    read and write it (the shell is in the folder's group, ext_data_rw).
 *
 * A name is unique across both roots and the write folders (profiles/NAME/, internal storage), so a game folder always
 * has its own saves. Staging folders (NAME.importing) and commit journals (NAME.commit) are never game folders.
 */
final class GameFolders {
    static final String STAGING = ".importing", JOURNAL = ".commit";
    final File adb, imported, profiles;

    GameFolders(Context c) {
        File ext = c.getExternalFilesDir(null), base = ext != null ? ext : c.getFilesDir();
        adb = new File(base, "games");
        imported = new File(base, "imported");
        profiles = new File(c.getFilesDir(), "profiles");
        adb.mkdirs();                       // a fresh install: the app makes games/ itself (then it is the app's; adb push still works)
    }

    static boolean isGameName(String n) { return n.matches("[A-Za-z0-9][A-Za-z0-9._-]*") && !n.endsWith(STAGING) && !n.endsWith(JOURNAL); }

    /** The folder of game NAME: games/NAME if it holds an SS2.exe (adb copies keep working as before), else imported/NAME. */
    File dir(String name) {
        File a = new File(adb, name), i = new File(imported, name);
        return exe(a) == null && exe(i) != null ? i : a;
    }

    /** NAME's SS2.exe in any letter case (a GOG or Steam copy may have ss2.exe), or null. */
    static File exe(File dir) {
        String[] l = dir.list();
        if (l != null) for (String s : l) if (s.equalsIgnoreCase("SS2.exe")) { File f = new File(dir, s); if (f.isFile()) return f; }
        return null;
    }

    static File findIgnoreCase(File dir, String name) {
        String[] l = dir.list();
        if (l != null) for (String s : l) if (s.equalsIgnoreCase(name)) return new File(dir, s);
        return null;
    }

    // ---------------------------------------------------------------- presentation labels
    // A name for people ("System Shock 2 - SCP Beta 4 (Legacy)") per game folder, kept in the app's preferences, never
    // in the game folder or its write folder. Only shown: the game, its saves and which recompiled modules load (by the
    // files' identity, modules.c) do not depend on it.
    static final String LABELS = "game_labels";
    static String label(Context c, String name) { return c.getSharedPreferences(LABELS, Context.MODE_PRIVATE).getString(name, ""); }
    static void setLabel(Context c, String name, String label) {
        String l = label == null ? "" : label.replaceAll("[\r\n\t]", " ").trim();
        if (l.length() > 80) l = l.substring(0, 80);
        if (l.isEmpty()) c.getSharedPreferences(LABELS, Context.MODE_PRIVATE).edit().remove(name).apply();
        else c.getSharedPreferences(LABELS, Context.MODE_PRIVATE).edit().putString(name, l).apply();
        Log.i(LauncherActivity.TAG, "game folder " + name + ": label " + (l.isEmpty() ? "removed" : "\"" + l + "\""));
    }
    /** "label (NAME)", or NAME when it has no label. */
    static String shown(Context c, String name) { String l = label(c, name); return l.isEmpty() ? name : l + " (" + name + ")"; }

    /** Every game folder with an SS2.exe, sorted, each name once (games/ wins over imported/). */
    List<String> list() {
        List<String> g = new ArrayList<>();
        for (File root : new File[] { adb, imported }) {
            File[] d = root.listFiles();
            if (d != null) for (File f : d) if (isGameName(f.getName()) && exe(f) != null && !containsIgnoreCase(g, f.getName())) g.add(f.getName());
        }
        Collections.sort(g, String.CASE_INSENSITIVE_ORDER);
        return g;
    }

    private static boolean containsIgnoreCase(List<String> l, String s) { for (String x : l) if (x.equalsIgnoreCase(s)) return true; return false; }

    /** True when NAME (any case) is used by a game folder, a write folder, or an import in progress. */
    boolean taken(String name) {
        for (File root : new File[] { adb, imported, profiles }) {
            String[] l = root.list();
            if (l != null) for (String s : l)
                if (s.equalsIgnoreCase(name) || s.equalsIgnoreCase(name + STAGING) || s.equalsIgnoreCase(name + JOURNAL)) return true;
        }
        return false;
    }

    /**
     * Finishes or removes imports that an earlier process did not complete (killed, crashed, phone switched off). An
     * import whose journal exists had every file copied and checked: its renames are finished. Any other staging folder
     * is an interrupted copy and is deleted. Only call while no import runs in this process. Returns what it did.
     */
    String recover() {
        StringBuilder done = new StringBuilder();
        File[] js = imported.listFiles((d, n) -> n.endsWith(JOURNAL));
        if (js != null) for (File j : js) {
            String name = j.getName().substring(0, j.getName().length() - JOURNAL.length());
            for (File root : new File[] { imported, profiles }) {
                File st = new File(root, name + STAGING), to = new File(root, name);
                if (st.isDirectory() && !to.exists() && st.renameTo(to)) done.append("finished the import of ").append(name).append(" (").append(root.getName()).append("). ");
            }
            j.delete();
        }
        for (File root : new File[] { imported, profiles }) {
            File[] st = root.listFiles((d, n) -> n.endsWith(STAGING));
            if (st != null) for (File s : st) { Files.deleteTree(s); done.append("removed the interrupted import ").append(root.getName()).append('/').append(s.getName()).append(". "); }
        }
        String r = done.toString().trim();
        if (!r.isEmpty()) Log.i(LauncherActivity.TAG, "import recovery: " + r);
        return r;
    }

    /** Owner and mode of a folder, for error messages and the log ("uid 2000 (adb shell), mode 2775"). */
    static String owner(File f) {
        try {
            StructStat s = Os.stat(f.getPath());
            return String.format(Locale.US, "uid %d%s, mode %o", s.st_uid, s.st_uid == 2000 ? " (the adb shell account)" : s.st_uid == android.os.Process.myUid() ? " (this app)" : "", s.st_mode & 07777);
        } catch (ErrnoException e) {
            return e.getMessage();
        }
    }

    // ---------------------------------------------------------------- what the recompiled game was built from

    /** BuildConfig.GAME_EXE: "size:crc32", or null when the build has no record. */
    static long[] expectedExe() {
        String[] p = BuildConfig.GAME_EXE.split(":");
        return p.length == 2 ? new long[] { Long.parseLong(p[0]), Long.parseLong(p[1], 16) } : null;
    }

    private static final Map<String, String> exeCache = new HashMap<>();

    /** Null if f is the SS2.exe this app was recompiled from (or the build has no record), else why not, for the player. */
    static String exeProblem(File f) {
        long[] want = expectedExe();
        if (want == null) return null;
        String key = f.getPath() + ":" + f.length() + ":" + f.lastModified();
        synchronized (exeCache) { if (exeCache.containsKey(key)) return exeCache.get(key); }
        String r;
        try (InputStream in = new FileInputStream(f)) { r = exeProblem(in, want); }
        catch (IOException e) { r = "its SS2.exe cannot be read (" + e.getMessage() + ")"; }
        synchronized (exeCache) { exeCache.put(key, r); }
        return r;
    }

    /** Reads a whole SS2.exe and compares it with what the game was recompiled from. */
    static String exeProblem(InputStream in, long[] want) throws IOException {
        CRC32 crc = new CRC32();
        byte[] buf = new byte[1 << 16], head = new byte[want[0] > 0 ? (int) Math.min(want[0] + (1 << 20), 16 << 20) : 0];
        long n = 0;
        for (int r; (r = in.read(buf)) > 0; ) {
            crc.update(buf, 0, r);
            if (n < head.length) System.arraycopy(buf, 0, head, (int) n, (int) Math.min(r, head.length - n));
            n += r;
        }
        if (n == want[0] && crc.getValue() == want[1]) return null;
        String v = peVersion(head, (int) Math.min(n, head.length));
        return String.format(Locale.US, "its SS2.exe is %s (%,d bytes, CRC32 %08x), but this app was recompiled from the NewDark 2.48 "
            + "SS2.exe (%,d bytes, CRC32 %08x) and can only run that exact file", v != null ? "NewDark " + v : "another version", n, crc.getValue(), want[0], want[1]);
    }

    /** The ProductVersion string of a PE version resource (NewDark: "2.47", "2.48"; its FileVersion is "1, 0, 0, 0"), or null. */
    static String peVersion(byte[] d, int n) {
        byte[] key = "ProductVersion".getBytes(java.nio.charset.StandardCharsets.UTF_16LE);
        outer:
        for (int i = 0; i + key.length < n; i++) {
            for (int k = 0; k < key.length; k++) if (d[i + k] != key[k]) continue outer;
            int p = i + key.length;
            while (p + 1 < n && d[p] == 0 && d[p + 1] == 0) p += 2;           // the key's terminator and padding
            StringBuilder s = new StringBuilder();
            for (; p + 1 < n && (d[p] != 0 || d[p + 1] != 0) && s.length() < 32; p += 2) s.append((char) ((d[p] & 0xff) | (d[p + 1] & 0xff) << 8));
            String v = s.toString().trim();
            if (!v.isEmpty() && Character.isDigit(v.charAt(0))) return v;
        }
        return null;
    }
}
