package local.newdarkandroid.ss2;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Outline;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.RadialGradient;
import android.graphics.Rect;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.RippleDrawable;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * The launcher's look (Phase 3G): a dark, slightly industrial HUD in blue-green. Original artwork drawn in code (no
 * images, no game assets): chamfered panels with a teal edge, a background of deep blue-black with a faint grid,
 * scanlines and corner brackets, and condensed type with wide tracking for labels.
 */
final class LauncherUi {
    private LauncherUi() {}

    // ---------------------------------------------------------------- palette
    static final int BG_TOP = 0xFF061116, BG_BOTTOM = 0xFF020608;
    static final int PANEL = 0xE60A171C, PANEL_EDGE = 0x6619D3C5, PANEL_PRESSED = 0xF0123038;
    static final int ACCENT = 0xFF19D3C5, ACCENT_LIGHT = 0xFF7FFFE9, ACCENT_DIM = 0xFF1C7F78, ACCENT_DEEP = 0xFF0E5652;
    static final int TEXT = 0xFFE3F4F2, TEXT_2 = 0xFF8FB3AF, TEXT_3 = 0xFF5F807C, ON_ACCENT = 0xFF021012;
    static final int WARN = 0xFFFFB25C, OK = 0xFF4DE3A0;

    static final Typeface CONDENSED = Typeface.create("sans-serif-condensed", Typeface.BOLD);
    static final Typeface CONDENSED_LIGHT = Typeface.create("sans-serif-condensed", Typeface.NORMAL);
    static final Typeface BODY = Typeface.create("sans-serif", Typeface.NORMAL);
    static final Typeface MONO = Typeface.MONOSPACE;

    static int dp(Context c, float v) { return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, c.getResources().getDisplayMetrics())); }

    // ---------------------------------------------------------------- text
    static TextView text(Context c, String s, float sp, int color, Typeface face) {
        TextView t = new TextView(c);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setTypeface(face);
        t.setIncludeFontPadding(false);
        t.setLineSpacing(0, 1.15f);
        return t;
    }
    /** A small spaced-out caption: "INSTALLATION", "ADVANCED". */
    static TextView caption(Context c, String s) {
        TextView t = text(c, s.toUpperCase(java.util.Locale.ROOT), 11, ACCENT, CONDENSED);
        t.setLetterSpacing(.22f);
        return t;
    }
    static LinearLayout.LayoutParams wrap() { return new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT); }
    static LinearLayout.LayoutParams fill() { return new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT); }
    static LinearLayout.LayoutParams top(Context c, float marginDp) { LinearLayout.LayoutParams p = fill(); p.topMargin = dp(c, marginDp); return p; }
    static LinearLayout column(Context c) { LinearLayout l = new LinearLayout(c); l.setOrientation(LinearLayout.VERTICAL); return l; }
    static LinearLayout row(Context c) { LinearLayout l = new LinearLayout(c); l.setOrientation(LinearLayout.HORIZONTAL); l.setGravity(Gravity.CENTER_VERTICAL); return l; }

    // ---------------------------------------------------------------- panels and buttons
    /** A panel: dark fill, a thin teal edge, the top-left and bottom-right corners cut, bright marks at the cuts. */
    static LinearLayout panel(Context c) {
        LinearLayout p = column(c);
        p.setBackground(new Chamfer(c, PANEL, PANEL_EDGE, true));
        int pad = dp(c, 16);
        p.setPadding(pad, dp(c, 14), pad, dp(c, 16));
        return p;
    }

    /** The one big action: filled teal, dark lettering. */
    static TextView primaryButton(Context c, String label) {
        TextView b = text(c, label, 22, ON_ACCENT, CONDENSED);
        b.setLetterSpacing(.32f);
        b.setGravity(Gravity.CENTER);
        b.setMinHeight(dp(c, 64));
        b.setPadding(dp(c, 16), dp(c, 12), dp(c, 16), dp(c, 12));
        Chamfer fill = new Chamfer(c, ACCENT, ACCENT_LIGHT, false);
        fill.gradient(ACCENT_LIGHT, ACCENT);
        b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x66FFFFFF), fill, new Chamfer(c, 0xFFFFFFFF, 0, false)));
        clickable(b);
        return b;
    }

    /** A secondary button: outlined, teal lettering. */
    static TextView outlineButton(Context c, String label) {
        TextView b = text(c, label.toUpperCase(java.util.Locale.ROOT), 13, ACCENT_LIGHT, CONDENSED);
        b.setLetterSpacing(.18f);
        b.setGravity(Gravity.CENTER);
        b.setMinHeight(dp(c, 40));
        b.setPadding(dp(c, 16), dp(c, 8), dp(c, 16), dp(c, 8));
        b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x5519D3C5), new Chamfer(c, 0x2219D3C5, ACCENT_DIM, false),
            new Chamfer(c, 0xFFFFFFFF, 0, false)));
        clickable(b);
        return b;
    }

    /** A menu row inside a panel: title, a line of detail, and a chevron. Returns the row; its title is tagged "title". */
    static LinearLayout menuRow(Context c, String title, String detail) {
        LinearLayout r = row(c);
        r.setPadding(dp(c, 4), dp(c, 12), dp(c, 4), dp(c, 12));
        r.setMinimumHeight(dp(c, 56));
        LinearLayout words = column(c);
        TextView t = text(c, title, 17, TEXT, CONDENSED);
        t.setTag("title");
        TextView d = text(c, detail, 13, TEXT_2, BODY);
        d.setTag("detail");
        d.setPadding(0, dp(c, 3), 0, 0);
        words.addView(t, wrap());
        words.addView(d, wrap());
        r.addView(words, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        TextView chevron = text(c, "›", 24, ACCENT, CONDENSED_LIGHT);
        chevron.setTag("chevron");
        chevron.setPadding(dp(c, 12), 0, dp(c, 4), 0);
        r.addView(chevron, wrap());
        r.setBackground(new RippleDrawable(ColorStateList.valueOf(0x3319D3C5), null, new Chamfer(c, 0xFFFFFFFF, 0, false)));
        clickable(r);
        return r;
    }
    static void detail(View row, String s) { TextView d = row.findViewWithTag("detail"); if (d != null && !s.contentEquals(d.getText())) d.setText(s); }

    /** A hairline divider between menu rows. */
    static View divider(Context c) {
        View v = new View(c);
        v.setBackgroundColor(0x2219D3C5);
        v.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, Math.max(1, dp(c, .75f))));
        return v;
    }

    /** A status dot and its words: "READY", "SS2.EXE MISSING". */
    static TextView status(Context c, String s, int color) {
        TextView t = text(c, "●  " + s.toUpperCase(java.util.Locale.ROOT), 12, color, CONDENSED);
        t.setLetterSpacing(.12f);
        return t;
    }

    private static void clickable(View v) {
        v.setClickable(true);
        v.setFocusable(true);
    }

    static void ellipsize(TextView t, int lines) { t.setMaxLines(lines); t.setEllipsize(TextUtils.TruncateAt.END); }

    /** A dialog in the launcher's look: the dark chamfered panel instead of the stock grey sheet, teal buttons. */
    static void style(android.app.AlertDialog d) {
        android.view.Window w = d.getWindow();
        Context c = d.getContext();
        if (w != null) w.setBackgroundDrawable(new android.graphics.drawable.InsetDrawable(new Chamfer(c, 0xFF0B191E, PANEL_EDGE, true), dp(c, 16)));
        for (int which : new int[] { android.content.DialogInterface.BUTTON_POSITIVE, android.content.DialogInterface.BUTTON_NEGATIVE,
                                     android.content.DialogInterface.BUTTON_NEUTRAL }) {
            android.widget.Button b = d.getButton(which);
            if (b != null) b.setTextColor(ACCENT);
        }
    }

    // ---------------------------------------------------------------- drawables
    /** A rectangle with its top-left and bottom-right corners cut, a fill (or a vertical gradient) and an optional edge. */
    static final class Chamfer extends Drawable {
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), edge = new Paint(Paint.ANTI_ALIAS_FLAG), mark = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path path = new Path();
        private final float cut, stroke;
        private final boolean marks;
        private int top = 0, bottom = 0;

        Chamfer(Context c, int fillColor, int edgeColor, boolean marks) {
            cut = dp(c, 10);
            stroke = Math.max(1, dp(c, 1));
            fill.setColor(fillColor);
            edge.setStyle(Paint.Style.STROKE);
            edge.setStrokeWidth(stroke);
            edge.setColor(edgeColor);
            mark.setStyle(Paint.Style.STROKE);
            mark.setStrokeWidth(stroke * 2);
            mark.setColor(ACCENT);
            mark.setStrokeCap(Paint.Cap.SQUARE);
            this.marks = marks;
        }
        void gradient(int topColor, int bottomColor) { top = topColor; bottom = bottomColor; }

        @Override protected void onBoundsChange(Rect b) {
            float h = stroke / 2, l = b.left + h, t = b.top + h, r = b.right - h, d = b.bottom - h, k = Math.min(cut, Math.min(b.width(), b.height()) / 3f);
            path.reset();
            path.moveTo(l + k, t); path.lineTo(r, t); path.lineTo(r, d - k); path.lineTo(r - k, d); path.lineTo(l, d); path.lineTo(l, t + k); path.close();
            if (top != 0) fill.setShader(new LinearGradient(0, b.top, 0, b.bottom, top, bottom, Shader.TileMode.CLAMP));
        }
        @Override public void draw(Canvas c) {
            c.drawPath(path, fill);
            if (edge.getColor() != 0) c.drawPath(path, edge);
            if (marks) {                                                      // short bright strokes along the two cuts
                Rect b = getBounds();
                float k = Math.min(cut, Math.min(b.width(), b.height()) / 3f), s = stroke;
                c.drawLine(b.left + s, b.top + k, b.left + k, b.top + s, mark);
                c.drawLine(b.right - k, b.bottom - s, b.right - s, b.bottom - k, mark);
                c.drawLine(b.left + k + s * 3, b.top + s, b.left + k + cut * 2.2f, b.top + s, mark);
            }
        }
        @Override public void getOutline(Outline o) { o.setConvexPath(path); }
        @Override public void setAlpha(int a) { fill.setAlpha(a); }
        @Override public void setColorFilter(ColorFilter f) { fill.setColorFilter(f); }
        @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
    }

    /** The screen behind everything: deep blue-black, a soft teal glow up top, a faint grid, scanlines, corner brackets. */
    static final class Backdrop extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG), glow = new Paint(), line = new Paint(), bracket = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final float grid, scan, arm, inset;

        Backdrop(Context c) {
            super(c);
            grid = dp(c, 28); scan = dp(c, 3); arm = dp(c, 22); inset = dp(c, 10);
            line.setStrokeWidth(Math.max(1, dp(c, .5f)));
            bracket.setStyle(Paint.Style.STROKE);
            bracket.setStrokeWidth(Math.max(1, dp(c, 1.5f)));
            bracket.setColor(0x8019D3C5);
        }
        @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
            paint.setShader(new LinearGradient(0, 0, 0, h, BG_TOP, BG_BOTTOM, Shader.TileMode.CLAMP));
            glow.setShader(new RadialGradient(w * .5f, h * .08f, Math.max(1, Math.max(w, h) * .55f), 0x2A19D3C5, 0x0019D3C5, Shader.TileMode.CLAMP));
        }
        @Override protected void onDraw(Canvas c) {
            int w = getWidth(), h = getHeight();
            c.drawRect(0, 0, w, h, paint);
            c.drawRect(0, 0, w, h, glow);
            line.setColor(0x0F19D3C5);                                        // the grid
            for (float x = (w % grid) / 2; x < w; x += grid) c.drawLine(x, 0, x, h, line);
            for (float y = grid; y < h; y += grid) c.drawLine(0, y, w, y, line);
            line.setColor(0x08000000 | 0x7FFFE9);                             // scanlines, barely there
            for (float y = 0; y < h; y += scan) c.drawLine(0, y, w, y, line);
            float i = inset, a = arm;                                         // corner brackets
            c.drawLine(i, i, i + a, i, bracket); c.drawLine(i, i, i, i + a, bracket);
            c.drawLine(w - i, i, w - i - a, i, bracket); c.drawLine(w - i, i, w - i, i + a, bracket);
            c.drawLine(i, h - i, i + a, h - i, bracket); c.drawLine(i, h - i, i, h - i - a, bracket);
            c.drawLine(w - i, h - i, w - i - a, h - i, bracket); c.drawLine(w - i, h - i, w - i, h - i - a, bracket);
        }
    }
}
