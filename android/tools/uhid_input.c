/* uhid_input.c: a USB keyboard and mouse for testing, made from adb (android/tools/README.md).
 *
 * Runs on the device as the shell user, which may open /dev/uhid. It creates one HID device with a keyboard (report 1)
 * and a mouse with 16-bit relative motion (report 2). Android sees an ordinary external keyboard and mouse: key events
 * come from a physical keyboard, and SDL's relative mouse mode gets pointer capture and raw motion, as with a real
 * Bluetooth or USB mouse. Nothing in the app knows about this tool.
 *
 * Commands, one per line, from stdin or from a command file (argument 1), which the tool follows like tail -f: it is
 * truncated at start, and every line appended to it later runs (echo tap esc >> FILE). SELinux denies the shell FIFOs
 * in /data/local/tmp, so a file is used.
 *   down KEY | up KEY | tap KEY [ms]       hold, release, or press for ms (default 80) — names: a-z 0-9 esc enter space
 *                                          tab backspace minus equal comma period slash semicolon apostrophe grave
 *                                          lbracket rbracket backslash f1-f12 up down left right home end pageup pagedown
 *                                          insert delete lshift rshift lctrl rctrl lalt ralt kp0-kp9 kpenter
 *   move DX DY [STEPS] [MS]                relative motion, split into STEPS reports over MS milliseconds
 *   btn N down|up | click [N] [ms]         mouse button N (1 left, 2 right, 3 middle)
 *   wheel N | sleep MS | release | quit    release lets go of every key and button
 * With -t as the first argument it is a touchscreen instead: a 5-contact HID multitouch digitizer, which Android handles
 * like any external touchscreen (several fingers at once, through the normal input stack):
 *   touch ID down X Y | touch ID move X Y | touch ID up      contact ID (0-4) at screen pixel X, Y
 *   swipe ID X0 Y0 X1 Y1 [STEPS] [MS]                      move a contact that is down, other contacts staying put
 *   screen W H                                             the pixel size X and Y refer to (default 2340 1080)
 *   rotate 90 | rotate 0                                   X, Y are landscape pixels of a portrait panel (Android maps an
 *                                                          external touchscreen in the display's natural orientation)
 * Each command is logged to stdout with a monotonic timestamp in seconds. */
#include <errno.h>
#include <fcntl.h>
#include <linux/uhid.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const uint8_t rdesc[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,             /* keyboard, report 1 */
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,   /* modifiers */
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                                                             /* reserved */
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x73, 0x05, 0x07, 0x19, 0x00, 0x29, 0x73, 0x81, 0x00, /* 6 keys */
    0xC0,
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00,   /* mouse, report 2 */
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,   /* 5 buttons */
    0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x06,   /* x, y */
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,                                       /* wheel */
    0xC0, 0xC0,
};

/* touchscreen (-t), report 3: 5 contacts of tip switch, contact id, 16-bit x and y; contact count; feature 4: maximum */
#define TC 0x09, 0x22, 0xA1, 0x02, 0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,            0x75, 0x07, 0x95, 0x01, 0x81, 0x03, 0x09, 0x51, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,            0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02,            0x05, 0x0D, 0xC0
static const uint8_t rdesc_touch[] = {
    0x05, 0x0D, 0x09, 0x04, 0xA1, 0x01, 0x85, 0x03,
    TC, TC, TC, TC, TC,
    0x09, 0x54, 0x15, 0x00, 0x25, 0x05, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
    0x85, 0x04, 0x09, 0x55, 0x25, 0x05, 0x75, 0x08, 0x95, 0x01, 0xB1, 0x02,
    0xC0,
};
#define CONTACTS 5
static struct { int down, x, y; } contact[CONTACTS];
static int screen_w = 2340, screen_h = 1080, rotate90, touch_mode;

static int fd;
static uint8_t mods, keys[6], buttons;
static double t0;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec / 1e9; }
static void msleep(int ms) { if (ms > 0) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&t, NULL); } }

static int send_ev(struct uhid_event *e) {
    if (write(fd, e, sizeof *e) != (ssize_t)sizeof *e) { fprintf(stderr, "uhid write: %s\n", strerror(errno)); return -1; }
    return 0;
}
static void input(const uint8_t *d, uint16_t n) {
    struct uhid_event e; memset(&e, 0, sizeof e); e.type = UHID_INPUT2; e.u.input2.size = n; memcpy(e.u.input2.data, d, n); send_ev(&e);
}
static void kb_report(void) { uint8_t r[9] = { 1, mods, 0 }; memcpy(r + 3, keys, 6); input(r, sizeof r); }
static void mouse_report(int dx, int dy, int wheel) {
    uint8_t r[7] = { 2, buttons, (uint8_t)dx, (uint8_t)(dx >> 8), (uint8_t)dy, (uint8_t)(dy >> 8), (uint8_t)wheel }; input(r, sizeof r);
}

/* key name -> HID usage; modifiers are 0xE0-0xE7 */
static int usage_of(const char *k) {
    static const struct { const char *n; int u; } t[] = {
        { "enter", 0x28 }, { "esc", 0x29 }, { "backspace", 0x2A }, { "tab", 0x2B }, { "space", 0x2C }, { "minus", 0x2D },
        { "equal", 0x2E }, { "lbracket", 0x2F }, { "rbracket", 0x30 }, { "backslash", 0x31 }, { "semicolon", 0x33 },
        { "apostrophe", 0x34 }, { "grave", 0x35 }, { "comma", 0x36 }, { "period", 0x37 }, { "slash", 0x38 }, { "capslock", 0x39 },
        { "printscreen", 0x46 }, { "scrolllock", 0x47 }, { "pause", 0x48 }, { "insert", 0x49 }, { "home", 0x4A }, { "pageup", 0x4B },
        { "delete", 0x4C }, { "end", 0x4D }, { "pagedown", 0x4E }, { "right", 0x4F }, { "left", 0x50 }, { "down", 0x51 }, { "up", 0x52 },
        { "kpenter", 0x58 }, { "lctrl", 0xE0 }, { "lshift", 0xE1 }, { "lalt", 0xE2 }, { "lgui", 0xE3 }, { "rctrl", 0xE4 },
        { "rshift", 0xE5 }, { "ralt", 0xE6 }, { "rgui", 0xE7 },
    };
    if (k[0] >= 'a' && k[0] <= 'z' && !k[1]) return 0x04 + (k[0] - 'a');
    if (k[0] >= '1' && k[0] <= '9' && !k[1]) return 0x1E + (k[0] - '1');
    if (!strcmp(k, "0")) return 0x27;
    if (k[0] == 'f' && k[1] >= '1' && k[1] <= '9') { int n = atoi(k + 1); if (n >= 1 && n <= 12) return 0x3A + n - 1; }
    if (!strncmp(k, "kp", 2) && k[2] >= '0' && k[2] <= '9' && !k[3]) return k[2] == '0' ? 0x62 : 0x59 + (k[2] - '1');
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) if (!strcmp(k, t[i].n)) return t[i].u;
    return -1;
}
static int key(const char *name, int down) {
    int u = usage_of(name); if (u < 0) { printf("unknown key %s\n", name); return -1; }
    if (u >= 0xE0) { if (down) mods |= (uint8_t)(1u << (u - 0xE0)); else mods &= (uint8_t)~(1u << (u - 0xE0)); }
    else {
        int i, free_slot = -1, at = -1;
        for (i = 0; i < 6; i++) { if (keys[i] == u) at = i; if (!keys[i] && free_slot < 0) free_slot = i; }
        if (down && at < 0 && free_slot >= 0) keys[free_slot] = (uint8_t)u;
        if (!down && at >= 0) keys[at] = 0;
    }
    kb_report(); return 0;
}

static void drain(void) {          /* uhid's own events (start, open, close); the touchscreen answers its feature request */
    struct pollfd p = { fd, POLLIN, 0 }; struct uhid_event e;
    while (poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) {
        if (read(fd, &e, sizeof e) <= 0) break;
        if (e.type == UHID_GET_REPORT) {
            struct uhid_event r; memset(&r, 0, sizeof r); r.type = UHID_GET_REPORT_REPLY; r.u.get_report_reply.id = e.u.get_report.id;
            if (touch_mode && e.u.get_report.rnum == 4) { r.u.get_report_reply.size = 2; r.u.get_report_reply.data[0] = 4; r.u.get_report_reply.data[1] = CONTACTS; }
            else r.u.get_report_reply.err = EIO;
            send_ev(&r);
        }
    }
}
/* one report with every contact that is down, plus the one just lifted (tip 0) */
static void touch_report(int lifted) {
    uint8_t r[2 + CONTACTS * 6]; memset(r, 0, sizeof r); r[0] = 3; int n = 0;
    for (int i = 0; i < CONTACTS; i++) {
        if (!contact[i].down && i != lifted) continue;
        int x = (int)((long)contact[i].x * 32767 / (screen_w - 1)), y = (int)((long)contact[i].y * 32767 / (screen_h - 1));
        if (rotate90) { int t = x; x = 32767 - y; y = t; }   /* the panel is portrait; X, Y are landscape screen pixels */
        uint8_t *c = r + 1 + n * 6; c[0] = contact[i].down ? 1 : 0; c[1] = (uint8_t)i;
        c[2] = (uint8_t)x; c[3] = (uint8_t)(x >> 8); c[4] = (uint8_t)y; c[5] = (uint8_t)(y >> 8); n++;
    }
    r[1 + CONTACTS * 6] = (uint8_t)n; input(r, sizeof r);
}

static int command(char *line) {
    char *a[10] = { 0 }; int n = 0;
    for (char *s = strtok(line, " \t\r\n"); s && n < 10; s = strtok(NULL, " \t\r\n")) a[n++] = s;
    if (!n || a[0][0] == '#') return 0;
    printf("%.3f %s%s%s%s%s\n", now() - t0, a[0], n > 1 ? " " : "", n > 1 ? a[1] : "", n > 2 ? " " : "", n > 2 ? a[2] : ""); fflush(stdout);
    if (!strcmp(a[0], "down") && n > 1) key(a[1], 1);
    else if (!strcmp(a[0], "up") && n > 1) key(a[1], 0);
    else if (!strcmp(a[0], "tap") && n > 1) { if (!key(a[1], 1)) { msleep(n > 2 ? atoi(a[2]) : 80); key(a[1], 0); } }
    else if (!strcmp(a[0], "move") && n > 2) {
        int dx = atoi(a[1]), dy = atoi(a[2]), steps = n > 3 ? atoi(a[3]) : 1, ms = n > 4 ? atoi(a[4]) : 0, sx = 0, sy = 0;
        if (steps < 1) steps = 1;
        for (int i = 1; i <= steps; i++) {
            int x = (int)((long)dx * i / steps), y = (int)((long)dy * i / steps);
            mouse_report(x - sx, y - sy, 0); sx = x; sy = y; msleep(ms / steps);
        }
    } else if (!strcmp(a[0], "btn") && n > 2) {
        int b = atoi(a[1]); if (b >= 1 && b <= 5) { if (!strcmp(a[2], "down")) buttons |= (uint8_t)(1u << (b - 1)); else buttons &= (uint8_t)~(1u << (b - 1)); mouse_report(0, 0, 0); }
    } else if (!strcmp(a[0], "click")) {
        int b = n > 1 ? atoi(a[1]) : 1; if (b < 1 || b > 5) b = 1;
        buttons |= (uint8_t)(1u << (b - 1)); mouse_report(0, 0, 0); msleep(n > 2 ? atoi(a[2]) : 80);
        buttons &= (uint8_t)~(1u << (b - 1)); mouse_report(0, 0, 0);
    } else if (!strcmp(a[0], "wheel") && n > 1) mouse_report(0, 0, atoi(a[1]));
    else if (!strcmp(a[0], "touch") && n > 2 && atoi(a[1]) >= 0 && atoi(a[1]) < CONTACTS) {
        int i = atoi(a[1]);
        if (!strcmp(a[2], "up")) { contact[i].down = 0; touch_report(i); }
        else if (n > 4) { contact[i].down = 1; contact[i].x = atoi(a[3]); contact[i].y = atoi(a[4]); touch_report(-1); }
    } else if (!strcmp(a[0], "swipe") && n > 5 && atoi(a[1]) >= 0 && atoi(a[1]) < CONTACTS) {
        int i = atoi(a[1]), x0 = atoi(a[2]), y0 = atoi(a[3]), x1 = atoi(a[4]), y1 = atoi(a[5]), steps = n > 6 ? atoi(a[6]) : 10, ms = n > 7 ? atoi(a[7]) : 200;
        if (steps < 1) steps = 1;
        for (int k = 0; k <= steps; k++) { contact[i].down = 1; contact[i].x = x0 + (x1 - x0) * k / steps; contact[i].y = y0 + (y1 - y0) * k / steps; touch_report(-1); msleep(ms / steps); }
    } else if (!strcmp(a[0], "screen") && n > 2) { screen_w = atoi(a[1]); screen_h = atoi(a[2]); }
    else if (!strcmp(a[0], "rotate") && n > 1) rotate90 = atoi(a[1]) == 90;
    else if (!strcmp(a[0], "sleep") && n > 1) msleep(atoi(a[1]));
    else if (!strcmp(a[0], "release")) {
        if (touch_mode) { for (int i = 0; i < CONTACTS; i++) if (contact[i].down) { contact[i].down = 0; touch_report(i); } }
        else { mods = 0; memset(keys, 0, sizeof keys); buttons = 0; kb_report(); mouse_report(0, 0, 0); }
    }
    else if (!strcmp(a[0], "quit")) return 1;
    else printf("unknown command %s\n", a[0]);
    drain(); return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0); t0 = now();
    fd = open("/dev/uhid", O_RDWR | O_CLOEXEC);
    if (fd < 0) { fprintf(stderr, "/dev/uhid: %s\n", strerror(errno)); return 1; }
    if (argc > 1 && !strcmp(argv[1], "-t")) { touch_mode = 1; argv++; argc--; }
    struct uhid_event e; memset(&e, 0, sizeof e); e.type = UHID_CREATE2;
    snprintf((char *)e.u.create2.name, sizeof e.u.create2.name, touch_mode ? "NewDark test touchscreen" : "NewDark test keyboard and mouse");
    if (touch_mode) { e.u.create2.rd_size = sizeof rdesc_touch; memcpy(e.u.create2.rd_data, rdesc_touch, sizeof rdesc_touch); }
    else { e.u.create2.rd_size = sizeof rdesc; memcpy(e.u.create2.rd_data, rdesc, sizeof rdesc); }
    e.u.create2.bus = 0x03; e.u.create2.vendor = 0x1209; e.u.create2.product = touch_mode ? 0x0002 : 0x0001; e.u.create2.version = 1;   /* BUS_USB; pid.codes test ids */
    if (send_ev(&e)) return 1;
    printf("device created; reading commands from %s\n", argc > 1 ? argv[1] : "stdin");

    char line[256];
    if (argc > 1) {                                /* follow the command file */
        int f = open(argv[1], O_RDONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (f < 0) { fprintf(stderr, "open %s: %s\n", argv[1], strerror(errno)); return 1; }
        fchmod(f, 0666);
        size_t len = 0; int done = 0;
        while (!done) {
            ssize_t r = read(f, line + len, sizeof line - 1 - len);
            if (r < 0) break;
            if (r == 0) { drain(); msleep(5); continue; }
            len += (size_t)r; line[len] = 0;
            char *nl;
            while (!done && (nl = memchr(line, '\n', len))) {
                *nl = 0; char cmd[256]; snprintf(cmd, sizeof cmd, "%s", line);
                len -= (size_t)(nl + 1 - line); memmove(line, nl + 1, len); line[len] = 0;
                done = command(cmd);
            }
            if (len == sizeof line - 1) len = 0;    /* an overlong line is dropped */
        }
    } else while (fgets(line, sizeof line, stdin)) if (command(line)) break;
    command((char[]){ "release" });
    memset(&e, 0, sizeof e); e.type = UHID_DESTROY; send_ev(&e); close(fd);
    printf("device destroyed\n");
    return 0;
}
