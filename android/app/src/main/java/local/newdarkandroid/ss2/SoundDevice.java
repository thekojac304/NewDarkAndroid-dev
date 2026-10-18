package local.newdarkandroid.ss2;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Sound driver (Phase 3D). NewDark's cam.cfg variable sfx_device picks the sound driver; its Audio menu option
 * "Hardware Acceleration" writes it: 1 OFF (DirectSound, the game's own 3D mixing, and the default when cam.cfg has
 * none), 2 ON (DirectSound 3D buffers), 4 OpenAL. The port implements DirectSound only. With 4, NewDark asks for
 * openal32.dll, which is not available here, and then opens no sound device at all: every sound, music included, is
 * silent. A PC installation that played through OpenAL (OpenAL32.dll next to SS2.exe) arrives with sfx_device 4.
 *
 * At every game start the launcher turns that one line into sfx_device 1 in the cam.cfg the game reads: the profile's
 * (write folder) copy, or else the game folder's, which a new profile copy then hides (vfs.c reads the write folder
 * first; the game folder is never written). Every other byte stays as it was, and the first time a profile's own
 * cam.cfg is changed its original is kept next to it as cam.cfg.openal.
 */
final class SoundDevice {
    private SoundDevice() {}

    static final String OPENAL = "4", DIRECTSOUND = "1";
    // a whole line "sfx_device 4" (any case, blanks around, an optional ; comment); the line ending is not part of it
    private static final Pattern OPENAL_LINE = Pattern.compile("(?im)^([ \\t]*sfx_device[ \\t]+)" + OPENAL + "(?=[ \\t]*(?:;[^\\r\\n]*)?\\r?$)");

    /** The text with every OpenAL sfx_device line set to DirectSound (the same text when there is none). */
    static String withoutOpenAL(String cfg) {
        return OPENAL_LINE.matcher(cfg).replaceAll("$1" + DIRECTSOUND);
    }

    /** Applies withoutOpenAL to the cam.cfg the game will read. Returns what was done (for the log), or null if nothing
     *  needed to change. The text is handled as ISO-8859-1, so bytes that are not UTF-8 survive unchanged. */
    static String apply(File profileCfg, File gameDirCfg) throws IOException {
        File src = profileCfg.isFile() ? profileCfg : gameDirCfg != null && gameDirCfg.isFile() ? gameDirCfg : null;
        if (src == null) return null;
        String old = read(src), now = withoutOpenAL(old);
        if (now.equals(old)) return null;
        if (src == profileCfg) {
            File keep = new File(profileCfg.getPath() + ".openal");
            if (!keep.exists()) write(keep, old);
        }
        File tmp = new File(profileCfg.getPath() + ".tmp");
        write(tmp, now);
        // Android replaces the file in one step; the second try is for the desktop test (Windows will not rename over a file)
        if (!tmp.renameTo(profileCfg) && !(profileCfg.delete() && tmp.renameTo(profileCfg))) throw new IOException("cannot replace " + profileCfg);
        return "sound: " + src + " selects OpenAL (sfx_device " + OPENAL + "), which this port cannot play; "
            + profileCfg + " now has sfx_device " + DIRECTSOUND + " (DirectSound)" + (src == profileCfg ? ", the original is kept as " + profileCfg.getName() + ".openal" : "");
    }

    private static String read(File f) throws IOException {
        try (InputStream in = new FileInputStream(f)) {
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            byte[] buf = new byte[65536];
            for (int n; (n = in.read(buf)) > 0; ) b.write(buf, 0, n);
            return new String(b.toByteArray(), StandardCharsets.ISO_8859_1);
        }
    }

    private static void write(File f, String s) throws IOException {
        try (FileOutputStream o = new FileOutputStream(f)) {
            o.write(s.getBytes(StandardCharsets.ISO_8859_1));
            o.getFD().sync();
        }
    }
}
