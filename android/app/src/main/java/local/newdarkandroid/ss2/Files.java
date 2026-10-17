package local.newdarkandroid.ss2;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/** Small file helpers shared by both activities. */
final class Files {
    private Files() {}

    static String read(File f) {
        try (InputStream in = new FileInputStream(f)) {
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            byte[] buf = new byte[65536];
            for (int n; (n = in.read(buf)) > 0; ) b.write(buf, 0, n);
            return b.toString(StandardCharsets.UTF_8.name());
        } catch (IOException e) {
            return null;
        }
    }

    static String tail(File f, int lines) {
        String s = read(f);
        if (s == null) return "";
        int end = s.length(), i = end;
        while (lines > 0 && i > 0) if (s.charAt(--i) == '\n' && i < end - 1) lines--;
        return s.substring(i == 0 ? 0 : i + 1);
    }

    static void write(File f, String s, boolean append) throws IOException {
        try (FileOutputStream o = new FileOutputStream(f, append)) {
            o.write(s.getBytes(StandardCharsets.UTF_8));
            o.getFD().sync();
        }
    }

    static void copy(InputStream in, File to) throws IOException {
        try (InputStream i = in; OutputStream o = new FileOutputStream(to)) {
            byte[] buf = new byte[65536];
            for (int n; (n = i.read(buf)) > 0; ) o.write(buf, 0, n);
        }
    }

    static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        f.delete();
    }
}
