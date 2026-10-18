package local.newdarkandroid.ss2;

import java.io.File;
import java.nio.file.Files;

/** Desktop test for SoundDevice (no Android needed):
 *  javac -d build/sdt android/app/src/main/java/local/newdarkandroid/ss2/SoundDevice.java android/tools/SoundDeviceTest.java
 *  java -cp build/sdt local.newdarkandroid.ss2.SoundDeviceTest [a real cam.cfg to check]  */
public final class SoundDeviceTest {
    static int fails;
    static void eq(String what, String got, String want) {
        if (!got.equals(want)) { fails++; System.out.println("FAIL " + what + "\n  got:  " + got.replace("\r", "\\r").replace("\n", "\\n") + "\n  want: " + want.replace("\r", "\\r").replace("\n", "\\n")); }
        else System.out.println("ok   " + what);
    }
    static void yes(String what, boolean c) { if (!c) { fails++; System.out.println("FAIL " + what); } else System.out.println("ok   " + what); }

    public static void main(String[] a) throws Exception {
        eq("OpenAL line, CRLF", SoundDevice.withoutOpenAL("sfx_channels 48\r\nsfx_device 4\r\nsfx_eax 1\r\n"), "sfx_channels 48\r\nsfx_device 1\r\nsfx_eax 1\r\n");
        eq("OpenAL line, LF, last line without newline", SoundDevice.withoutOpenAL("a 1\nsfx_device 4"), "a 1\nsfx_device 1");
        eq("case and blanks", SoundDevice.withoutOpenAL("  SFX_Device\t4  \r\n"), "  SFX_Device\t1  \r\n");
        eq("comment after the value", SoundDevice.withoutOpenAL("sfx_device 4 ; OpenAL\r\n"), "sfx_device 1 ; OpenAL\r\n");
        eq("every OpenAL line (the last one wins in the game)", SoundDevice.withoutOpenAL("sfx_device 4\nx\nsfx_device 4\n"), "sfx_device 1\nx\nsfx_device 1\n");
        for (String v : new String[] { "0", "1", "2", "3", "40", "4.0", "44" })
            eq("sfx_device " + v + " unchanged", SoundDevice.withoutOpenAL("sfx_device " + v + "\r\n"), "sfx_device " + v + "\r\n");
        eq("commented-out line unchanged", SoundDevice.withoutOpenAL(";sfx_device 4\r\n"), ";sfx_device 4\r\n");
        eq("other variable unchanged", SoundDevice.withoutOpenAL("sfx_device_x 4\r\nmy_sfx_device 4\r\n"), "sfx_device_x 4\r\nmy_sfx_device 4\r\n");
        eq("no sfx_device: unchanged", SoundDevice.withoutOpenAL("game shock\r\n"), "game shock\r\n");

        File d = Files.createTempDirectory("sdt").toFile(), prof = new File(d, "profile"), game = new File(d, "game");
        prof.mkdirs(); game.mkdirs();
        byte[] latin = "; caf\u00e9\r\nsfx_device 4\r\n".getBytes("ISO-8859-1");
        File gcfg = new File(game, "CAM.CFG"); Files.write(gcfg.toPath(), latin);
        File pcfg = new File(prof, "cam.cfg");
        String r = SoundDevice.apply(pcfg, gcfg);
        yes("game folder copy seeds the profile", r != null && pcfg.isFile());
        eq("seeded profile copy: one line changed, non-UTF-8 byte kept", new String(Files.readAllBytes(pcfg.toPath()), "ISO-8859-1"), "; caf\u00e9\r\nsfx_device 1\r\n");
        yes("game folder copy untouched", java.util.Arrays.equals(Files.readAllBytes(gcfg.toPath()), latin));
        yes("no .openal backup for a seeded copy", !new File(prof, "cam.cfg.openal").exists());
        yes("second start: nothing to do", SoundDevice.apply(pcfg, gcfg) == null);

        Files.write(pcfg.toPath(), latin);
        r = SoundDevice.apply(pcfg, gcfg);
        yes("profile copy fixed", r != null && new String(Files.readAllBytes(pcfg.toPath()), "ISO-8859-1").equals("; caf\u00e9\r\nsfx_device 1\r\n"));
        File keep = new File(prof, "cam.cfg.openal");
        yes("original kept byte for byte as cam.cfg.openal", keep.isFile() && java.util.Arrays.equals(Files.readAllBytes(keep.toPath()), latin));
        Files.write(pcfg.toPath(), "sfx_device 4\r\n".getBytes("ISO-8859-1"));       // the player picks OpenAL again in the menu
        SoundDevice.apply(pcfg, gcfg);
        yes("backup is never overwritten", java.util.Arrays.equals(Files.readAllBytes(keep.toPath()), latin));
        yes("no profile and no game folder cam.cfg: nothing", SoundDevice.apply(new File(d, "none/cam.cfg"), null) == null);
        yes("no temporary file left", !new File(prof, "cam.cfg.tmp").exists());

        if (a.length > 0) {                     // a real cam.cfg: exactly one line may differ
            String in = new String(Files.readAllBytes(new File(a[0]).toPath()), "ISO-8859-1"), out = SoundDevice.withoutOpenAL(in);
            String[] x = in.split("\n", -1), y = out.split("\n", -1); int diff = 0;
            for (int i = 0; i < x.length; i++) if (!x[i].equals(y[i])) { diff++; System.out.println("     line " + (i + 1) + ": [" + x[i].trim() + "] -> [" + y[i].trim() + "]"); }
            yes(a[0] + ": " + diff + " line(s) changed, same length " + (in.length() == out.length()), diff <= 1 && in.length() == out.length());
        }
        System.out.println(fails == 0 ? "ALL PASS" : fails + " FAILED");
        System.exit(fails == 0 ? 0 : 1);
    }
}
