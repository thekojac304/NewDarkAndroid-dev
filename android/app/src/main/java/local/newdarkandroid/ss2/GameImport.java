package local.newdarkandroid.ss2;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;

/**
 * Copies a System Shock 2 folder the player picks with Android's folder picker (ACTION_OPEN_DOCUMENT_TREE, no storage
 * permission) into the app's game folders, games/NAME/, keeping its relative layout: a modded installation (DMM/,
 * cam_mod.ini, OSM/, misdml/) copies as it is. The copy goes to games/NAME.importing/ first and is renamed into place
 * only when every file arrived whole, so a failed or interrupted import never leaves a half folder that looks complete.
 * An existing game folder is never replaced: a taken name gets a number. Saves and settings are not touched; they live
 * in the app's private write folders (profiles/NAME/), which this does not create or change.
 *
 * Adapted from Shockolate's LauncherActivity importer (commit 7502370): tree walk, name checks and depth limit.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
final class GameImport {
    interface Progress { void update(String message); }

    static final class Result { String name; long files, bytes; }

    private static final int MAX_DEPTH = 32;
    private static final String[] PROJECTION = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE };

    private final ContentResolver resolver;
    private final Uri tree;
    private final Progress progress;
    private final Result result = new Result();
    private long lastReport;

    GameImport(ContentResolver resolver, Uri tree, Progress progress) {
        this.resolver = resolver; this.tree = tree; this.progress = progress;
    }

    /** Runs on a worker thread. Throws with a message meant for the player. */
    Result run(File gamesDir) throws IOException {
        String root = DocumentsContract.getTreeDocumentId(tree);
        String rootName = displayName(root);
        if (!hasChild(root, "SS2.exe"))
            throw new IOException("The folder \"" + rootName + "\" has no SS2.exe. Pick the System Shock 2 folder itself "
                + "(the one with SS2.exe, Data and cam_mod.ini in it), patched to NewDark 2.48.");
        String base = rootName.toLowerCase(Locale.US).replaceAll("[^a-z0-9._-]+", "-").replaceAll("^[-.]+|-+$", "");
        if (base.isEmpty()) base = "ss2";
        String name = base;
        for (int n = 2; new File(gamesDir, name).exists() || new File(gamesDir, name + ".importing").exists(); n++) name = base + "-" + n;
        File staging = new File(gamesDir, name + ".importing"), dest = new File(gamesDir, name);
        try {
            copyDir(root, staging, 0);
            if (!new File(staging, findName(staging, "SS2.exe")).isFile()) throw new IOException("SS2.exe did not arrive");
            if (!staging.renameTo(dest)) throw new IOException("could not rename " + staging + " to " + dest);
        } catch (IOException | RuntimeException e) {
            Files.deleteTree(staging);
            throw e instanceof IOException ? (IOException) e : new IOException(e.toString(), e);
        }
        result.name = name;
        return result;
    }

    private static String findName(File dir, String name) {
        String[] l = dir.list();
        if (l != null) for (String s : l) if (s.equalsIgnoreCase(name)) return s;
        return name;
    }

    private String displayName(String doc) {
        try (Cursor c = resolver.query(DocumentsContract.buildDocumentUriUsingTree(tree, doc), PROJECTION, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(1);
        } catch (RuntimeException ignored) {}
        return "ss2";
    }

    private boolean hasChild(String parent, String name) throws IOException {
        try (Cursor c = resolver.query(DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent), PROJECTION, null, null, null)) {
            if (c == null) throw new IOException("the selected folder cannot be read");
            while (c.moveToNext()) if (name.equalsIgnoreCase(c.getString(1)) && !DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))) return true;
        } catch (RuntimeException e) {
            throw new IOException("the selected folder cannot be read: " + e, e);
        }
        return false;
    }

    private static File child(File dir, String name) throws IOException {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..") || name.indexOf('/') >= 0 || name.indexOf('\0') >= 0)
            throw new IOException("unusable file name in the selected folder: " + name);
        return new File(dir, name);
    }

    private void copyDir(String doc, File to, int depth) throws IOException {
        if (depth > MAX_DEPTH) throw new IOException("the folder is nested too deeply");
        if (!to.isDirectory() && !to.mkdirs()) throw new IOException("cannot create " + to);
        try (Cursor c = resolver.query(DocumentsContract.buildChildDocumentsUriUsingTree(tree, doc), PROJECTION, null, null, null)) {
            if (c == null) throw new IOException("cannot list a folder inside the selection");
            while (c.moveToNext()) {
                File t = child(to, c.getString(1));
                if (t.exists()) throw new IOException("two entries named " + c.getString(1) + " (differing only in case?) in " + to.getName());
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))) copyDir(c.getString(0), t, depth + 1);
                else copyFile(c.getString(0), t, c.isNull(3) ? -1 : c.getLong(3));
            }
        } catch (RuntimeException e) {
            throw new IOException("reading the selected folder failed: " + e, e);
        }
    }

    private void copyFile(String doc, File to, long size) throws IOException {
        long n = 0;
        byte[] buf = new byte[1 << 20];
        try (InputStream in = resolver.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, doc));
             FileOutputStream out = new FileOutputStream(to)) {
            if (in == null) throw new IOException("cannot open " + to.getName());
            for (int r; (r = in.read(buf)) > 0; ) { out.write(buf, 0, r); n += r; result.bytes += r; report(); }
            out.getFD().sync();
        }
        if (size >= 0 && n != size) throw new IOException(to.getName() + " was copied incompletely (" + n + " of " + size + " bytes)");
        result.files++;
        report();
    }

    private void report() {
        long now = System.currentTimeMillis();
        if (now - lastReport < 250) return;
        lastReport = now;
        progress.update(String.format(Locale.US, "Importing: %d files, %.0f MB", result.files, result.bytes / 1048576.0));
    }
}
