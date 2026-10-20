#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define MAX_ITEMS 8
typedef struct { char label[48]; char cmd[256]; } Item;

static Item items[MAX_ITEMS];
static int nitems;
static int dwell_ms = 1500, tol_px = 24, tol_dist = 14, post_touch_ms = 300;
static int select_ms = 900, menu_timeout_ms = 5000;
static int menu_r_pct = 22, menu_ri_pct = 8;   /* ring radius / item radius, % of width */
static int max_x = 1079, max_y = 1919;
static const char *PKG = "com.bariumana.hoverdwell";

enum { UNDECIDED, HOVER, TOUCH };

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return s;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---------- touchscreen discovery ---------- */
static int open_touch(void) {
    char path[64];
    for (int i = 0; i < 32; i++) {
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        unsigned char bits[ABS_MAX / 8 + 1];
        memset(bits, 0, sizeof bits);
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof bits), bits) >= 0 &&
            (bits[ABS_MT_DISTANCE / 8] & (1 << (ABS_MT_DISTANCE % 8)))) {
            struct input_absinfo ai;
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0 && ai.maximum > 0) max_x = ai.maximum;
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0 && ai.maximum > 0) max_y = ai.maximum;
            fprintf(stderr, "using %s (%dx%d)\n", path, max_x + 1, max_y + 1);
            return fd;
        }
        close(fd);
    }
    return -1;
}

/* ---------- config ---------- */
static void load_config(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "no config at %s\n", path); return; }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#' || line[0] == 0) continue;
        int v;
        if (!strncmp(line, "item ", 5)) {
            char *bar = strchr(line + 5, '|');
            if (bar && nitems < MAX_ITEMS) {
                *bar = 0;
                char *l = trim(line + 5), *c = trim(bar + 1);
                for (char *q = l; *q; q++) if (*q == '\'' || *q == '"' || *q == '\\') *q = ' ';
                snprintf(items[nitems].label, sizeof items[0].label, "%s", l);
                snprintf(items[nitems].cmd, sizeof items[0].cmd, "%s", c);
                nitems++;
            }
        }
        else if (sscanf(line, "dwell_ms=%d", &v) == 1) dwell_ms = v;
        else if (sscanf(line, "select_ms=%d", &v) == 1) select_ms = v;
        else if (sscanf(line, "menu_timeout_ms=%d", &v) == 1) menu_timeout_ms = v;
        else if (sscanf(line, "menu_radius_pct=%d", &v) == 1) menu_r_pct = v;
        else if (sscanf(line, "item_radius_pct=%d", &v) == 1) menu_ri_pct = v;
        else if (sscanf(line, "tol_px=%d", &v) == 1) tol_px = v;
        else if (sscanf(line, "tol_dist=%d", &v) == 1) tol_dist = v;
        else if (sscanf(line, "post_touch_ms=%d", &v) == 1) post_touch_ms = v;
    }
    fclose(f);
    fprintf(stderr, "loaded %d menu items\n", nitems);
}

/* ---------- state ---------- */
static int x, y, d;
static int active, kind = UNDECIDED, saw_dist, ignored, has_anchor, fired;
static int ax, ay, ad;
static long t0, last_touch_end;

static int menu_open, menu_cx, menu_cy, menu_hl = -1, menu_start_idx = -1, armed;
static long menu_deadline;

/* ---------- menu geometry ---------- */
static int ring_r(void)  { return (max_x + 1) * menu_r_pct / 100; }
static int item_r(void)  { return (max_x + 1) * menu_ri_pct / 100; }

static void item_pos(int i, int *px, int *py) {
    double a = -M_PI / 2 + 2 * M_PI * i / nitems;
    *px = menu_cx + (int)(ring_r() * cos(a));
    *py = menu_cy + (int)(ring_r() * sin(a));
}

/* returns item index, or -1 for "not on an item" */
static int idx_at(int fx, int fy) {
    long ri = item_r();
    for (int i = 0; i < nitems; i++) {
        int px, py;
        item_pos(i, &px, &py);
        long dx = fx - px, dy = fy - py;
        if (dx * dx + dy * dy < ri * ri) return i;
    }
    return -1;
}

/* ---------- talking to the overlay app ---------- */
static void send_menu(void) {
    char labels[MAX_ITEMS * 50];
    labels[0] = 0;
    for (int i = 0; i < nitems; i++) {
        if (i) strcat(labels, "|");
        strcat(labels, items[i].label);
    }
    char buf[1024];
    snprintf(buf, sizeof buf,
        "am broadcast -p %s -a com.bariumana.hoverdwell.SHOW --el t %ld --ei cx %d --ei cy %d "
        "--ei w %d --ei h %d --ei r %d --ei ri %d --ei hl %d --ei sel %d "
        "--es labels '%s' </dev/null >/dev/null 2>&1 &",
        PKG, now_ms(), menu_cx, menu_cy, max_x + 1, max_y + 1,
        ring_r(), item_r(), menu_hl, select_ms, labels);
    system(buf);
}

static void close_menu(void) {
    if (!menu_open) return;
    menu_open = 0;
    char buf[256];
    snprintf(buf, sizeof buf,
        "am broadcast -p %s -a com.bariumana.hoverdwell.HIDE --el t %ld </dev/null >/dev/null 2>&1 &",
        PKG, now_ms());
    system(buf);
    fprintf(stderr, "menu closed\n");
}

static void open_menu(void) {
    int m = (max_x + 1) * (menu_r_pct + menu_ri_pct) / 100;   /* keep the whole ring on screen */
    menu_cx = clampi(x, m, max_x - m);
    menu_cy = clampi(y, m, max_y - m);
    menu_start_idx = idx_at(x, y);          /* finger may already sit on an item if clamped */
    armed = (menu_start_idx < 0);
    menu_hl = -1;
    menu_open = 1;
    menu_deadline = now_ms() + menu_timeout_ms;
    fprintf(stderr, "menu opened at %d,%d\n", menu_cx, menu_cy);
    send_menu();
}

static void menu_track(void) {
    int i = idx_at(x, y);
    if (!armed && i != menu_start_idx) armed = 1;
    int hl = (armed && i >= 0) ? i : -1;
    if (hl != menu_hl) { menu_hl = hl; send_menu(); }
}

static void on_dwell(void) {
    if (!nitems) { fprintf(stderr, "dwell at %d,%d (no items configured)\n", x, y); return; }
    if (!menu_open) { open_menu(); return; }
    int i = idx_at(x, y);
    if (i >= 0 && armed) {
        char buf[320];
        snprintf(buf, sizeof buf, "(%s) </dev/null >/dev/null 2>&1 &", items[i].cmd);
        fprintf(stderr, "run item %d: %s\n", i, items[i].label);
        system(buf);
    }
    close_menu();
}

/* ---------- dwell detection (unchanged logic) ---------- */
static void reset_dwell(void) { has_anchor = 0; fired = 0; }
static void mark_touch(void) { kind = TOUCH; reset_dwell(); close_menu(); }

static int moved(void) {
    return abs(x - ax) > tol_px || abs(y - ay) > tol_px || abs(d - ad) > tol_dist;
}

static void evaluate(void) {
    if (!active || kind != HOVER || ignored) return;
    long now = now_ms();
    int need = menu_open ? select_ms : dwell_ms;
    if (!has_anchor || moved()) {
        ax = x; ay = y; ad = d; t0 = now; has_anchor = 1; fired = 0;
    } else if (!fired && now - t0 >= need) {
        fired = 1;
        on_dwell();
    }
}

static void handle(struct input_event *e) {
    long now = now_ms();
    if (e->type == EV_ABS) {
        switch (e->code) {
        case ABS_MT_TRACKING_ID:
            if (e->value == -1) {
                if (kind == TOUCH) last_touch_end = now;
                active = 0; kind = UNDECIDED; ignored = 0;
                reset_dwell();
            } else {
                active = 1; kind = UNDECIDED; saw_dist = 0;
                ignored = (now - last_touch_end) < post_touch_ms;
                reset_dwell();
            }
            break;
        case ABS_MT_POSITION_X: x = e->value; break;
        case ABS_MT_POSITION_Y: y = e->value; break;
        case ABS_MT_DISTANCE:   d = e->value; saw_dist = 1; break;
        case ABS_MT_TOUCH_MAJOR:
        case ABS_MT_TOUCH_MINOR:
        case ABS_MT_WIDTH_MAJOR:
            if (active) mark_touch();
            break;
        }
    } else if (e->type == EV_KEY && e->code == BTN_TOUCH) {
        if (e->value == 1) mark_touch();
        else if (kind == TOUCH) last_touch_end = now;
    } else if (e->type == EV_SYN && e->code == SYN_REPORT) {
        if (active && kind == UNDECIDED) kind = saw_dist ? HOVER : TOUCH;
        if (menu_open && active && kind == HOVER && !ignored) {
            menu_deadline = now + menu_timeout_ms;
            menu_track();
        }
        evaluate();
    }
}

int main(int argc, char **argv) {
    load_config(argc > 1 ? argv[1] : "/data/adb/hoverdwell.conf");
    int fd = open_touch();
    if (fd < 0) { fprintf(stderr, "no hover-capable touchscreen found (need root?)\n"); return 1; }
    struct pollfd pfd = { fd, POLLIN, 0 };
    struct input_event ev[64];
    for (;;) {
        int timeout = -1;
        if (has_anchor && !fired && kind == HOVER && !ignored) {
            long left = t0 + (menu_open ? select_ms : dwell_ms) - now_ms();
            timeout = left > 0 ? (int)left : 0;
        }
        if (menu_open) {
            long dl = menu_deadline - now_ms();
            if (dl < 0) dl = 0;
            if (timeout < 0 || dl < timeout) timeout = (int)dl;
        }
        int r = poll(&pfd, 1, timeout);
        if (r == 0) {
            evaluate();
            if (menu_open && now_ms() >= menu_deadline) close_menu();
            continue;
        }
        if (r < 0) continue;
        ssize_t n = read(fd, ev, sizeof ev);
        if (n < 0) continue;
        for (size_t i = 0; i < (size_t)n / sizeof ev[0]; i++) handle(&ev[i]);
    }
}