/*
 * x11wmtest.c — does the X11 window manager honor the window-management
 * requests a Motif/Xt application makes?
 *
 * Purpose
 *   Settle, with evidence, whether Mutter (GNOME on RHEL 10, via Xwayland)
 *   honors or silently ignores the X11 requests a legacy Motif app relies on.
 *   Motif/Xt ultimately issue exactly these Xlib requests (XmNx/XmNy ->
 *   XConfigureWindow, XmNiconic -> XIconifyWindow, decoration control ->
 *   _MOTIF_WM_HINTS), so the window manager sees the same protocol traffic
 *   it would see from a real Motif application.
 *
 *   For every test the program records what it ASKED for and what the window
 *   manager actually DID (read back from the X server), and prints PASS/FAIL.
 *
 * Tests
 *   T1  Initial placement at an exact x,y (USPosition|PPosition)
 *   T2  Programmatic move + resize after mapping (XMoveResizeWindow)
 *   T3  Stays-above (_NET_WM_STATE_ABOVE) — control run, then with ABOVE set,
 *       verified against _NET_CLIENT_LIST_STACKING, not just the state atom
 *   T4  Iconify (XIconifyWindow) and restore, with position preserved
 *   T5  Reserved screen edge: DOCK window + _NET_WM_STRUT_PARTIAL,
 *       verified by the change in _NET_WORKAREA
 *   T6  Motif decoration hints (_MOTIF_WM_HINTS, decorations off)
 *
 * Build (RHEL 10, supported packages only)
 *   sudo dnf install gcc libX11-devel
 *   gcc -O2 -Wall -o x11wmtest x11wmtest.c -lX11
 *
 * Run — inside the logged-in GNOME session (it runs through Xwayland):
 *   ./x11wmtest            # NorthWest gravity (Motif/Xt default)
 *   ./x11wmtest --static   # StaticGravity: client origin must equal request
 *   ./x11wmtest --hold 30  # leave windows up 30 s for a screenshot
 *
 *   Exit status = number of FAILed checks (0 = everything honored).
 *
 * Notes on interpreting results
 *   - Gravity: with NorthWestGravity (the default, and what Xt uses), ICCCM
 *     says the window manager puts the FRAME's top-left at the requested
 *     position; the client area is then offset by the decoration size. With
 *     --static the CLIENT's top-left must land exactly on the request.
 *   - Requested positions are kept well inside the first monitor and below
 *     GNOME's top bar, so on-screen constraints should not apply. With
 *     display scaling enabled, X11 coordinates may be scaled; run on a 100%
 *     scale display first for an unambiguous result.
 *   - Run under the session you actually intend to ship on (gnome-shell vs
 *     gnome-kiosk): placement policy differs between them.
 */

#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>
#include <stdarg.h>

static Display *dpy;
static Window   root;
static int      scr;
static int      failures;
static int      use_static;

/* ---- atoms ------------------------------------------------------------ */
static Atom A_WM_STATE, A_NET_WM_STATE, A_NET_WM_STATE_ABOVE,
            A_NET_WM_STATE_HIDDEN, A_NET_ACTIVE_WINDOW,
            A_NET_CLIENT_LIST_STACKING, A_NET_FRAME_EXTENTS,
            A_NET_WM_WINDOW_TYPE, A_NET_WM_WINDOW_TYPE_DOCK,
            A_NET_WM_STRUT, A_NET_WM_STRUT_PARTIAL, A_NET_WORKAREA,
            A_NET_SUPPORTING_WM_CHECK, A_NET_WM_NAME, A_UTF8_STRING,
            A_MOTIF_WM_HINTS, A_NET_SUPPORTED;

static void init_atoms(void)
{
#define A(v, n) v = XInternAtom(dpy, n, False)
    A(A_WM_STATE, "WM_STATE");
    A(A_NET_WM_STATE, "_NET_WM_STATE");
    A(A_NET_WM_STATE_ABOVE, "_NET_WM_STATE_ABOVE");
    A(A_NET_WM_STATE_HIDDEN, "_NET_WM_STATE_HIDDEN");
    A(A_NET_ACTIVE_WINDOW, "_NET_ACTIVE_WINDOW");
    A(A_NET_CLIENT_LIST_STACKING, "_NET_CLIENT_LIST_STACKING");
    A(A_NET_FRAME_EXTENTS, "_NET_FRAME_EXTENTS");
    A(A_NET_WM_WINDOW_TYPE, "_NET_WM_WINDOW_TYPE");
    A(A_NET_WM_WINDOW_TYPE_DOCK, "_NET_WM_WINDOW_TYPE_DOCK");
    A(A_NET_WM_STRUT, "_NET_WM_STRUT");
    A(A_NET_WM_STRUT_PARTIAL, "_NET_WM_STRUT_PARTIAL");
    A(A_NET_WORKAREA, "_NET_WORKAREA");
    A(A_NET_SUPPORTING_WM_CHECK, "_NET_SUPPORTING_WM_CHECK");
    A(A_NET_WM_NAME, "_NET_WM_NAME");
    A(A_UTF8_STRING, "UTF8_STRING");
    A(A_MOTIF_WM_HINTS, "_MOTIF_WM_HINTS");
    A(A_NET_SUPPORTED, "_NET_SUPPORTED");
#undef A
}

/* ---- reporting -------------------------------------------------------- */
static void check(int ok, const char *test, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static void check(int ok, const char *test, const char *fmt, ...)
{
    va_list ap;
    printf("  [%s] %-4s ", ok ? "PASS" : "FAIL", test);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    if (!ok)
        failures++;
}

static void info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void info(const char *fmt, ...)
{
    va_list ap;
    printf("  [INFO]      ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

/* ---- event pumping ---------------------------------------------------- */
static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* Drain events until the server has been quiet for quiet_ms, or max_ms
 * has elapsed. Gives the window manager time to act on our requests. */
static void settle(int quiet_ms, int max_ms)
{
    long start = now_ms(), last = now_ms();
    int fd = ConnectionNumber(dpy);

    XSync(dpy, False);
    for (;;) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            last = now_ms();
        }
        long t = now_ms();
        if (t - last >= quiet_ms || t - start >= max_ms)
            break;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv = { 0, 50 * 1000 };
        select(fd + 1, &fds, NULL, NULL, &tv);
        XSync(dpy, False);
    }
}

static void wait_mapped(Window w, int max_ms)
{
    long start = now_ms();
    for (;;) {
        XWindowAttributes wa;
        XSync(dpy, False);
        if (XGetWindowAttributes(dpy, w, &wa) && wa.map_state == IsViewable)
            break;
        if (now_ms() - start > max_ms)
            break;
        settle(50, 100);
    }
    settle(400, 3000);
}

/* ---- property helpers ------------------------------------------------- */
static unsigned char *get_prop(Window w, Atom prop, Atom type,
                               unsigned long *n, int *format_out)
{
    Atom at;
    int fmt;
    unsigned long nitems, after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, w, prop, 0, 4096, False, type, &at, &fmt,
                           &nitems, &after, &data) != Success || !data) {
        *n = 0;
        return NULL;
    }
    *n = nitems;
    if (format_out)
        *format_out = fmt;
    return data;
}

static int has_state(Window w, Atom state)
{
    unsigned long n;
    Atom *a = (Atom *)get_prop(w, A_NET_WM_STATE, XA_ATOM, &n, NULL);
    int found = 0;
    for (unsigned long i = 0; i < n; i++)
        if (a[i] == state)
            found = 1;
    if (a)
        XFree(a);
    return found;
}

/* ICCCM WM_STATE: 0 Withdrawn, 1 Normal, 3 Iconic, -1 absent */
static long wm_state(Window w)
{
    unsigned long n;
    long *v = (long *)get_prop(w, A_WM_STATE, A_WM_STATE, &n, NULL);
    long s = (n >= 1) ? v[0] : -1;
    if (v)
        XFree(v);
    return s;
}

static const char *wm_state_name(long s)
{
    switch (s) {
    case 0: return "Withdrawn";
    case 1: return "Normal";
    case 3: return "Iconic";
    default: return "absent";
    }
}

static void frame_extents(Window w, long ext[4]) /* left right top bottom */
{
    unsigned long n;
    long *v = (long *)get_prop(w, A_NET_FRAME_EXTENTS, XA_CARDINAL, &n, NULL);
    for (int i = 0; i < 4; i++)
        ext[i] = (v && n >= 4) ? v[i] : 0;
    if (v)
        XFree(v);
}

/* Client-area origin in root coordinates, plus size. */
static void client_geom(Window w, int *x, int *y, int *wd, int *ht)
{
    Window child;
    XWindowAttributes wa;
    XSync(dpy, False);
    XTranslateCoordinates(dpy, w, root, 0, 0, x, y, &child);
    XGetWindowAttributes(dpy, w, &wa);
    *wd = wa.width;
    *ht = wa.height;
}

/* Position the test should compare against the request, per gravity. */
static void effective_pos(Window w, int *ex, int *ey, int *cx, int *cy,
                          int *wd, int *ht, long ext[4])
{
    client_geom(w, cx, cy, wd, ht);
    frame_extents(w, ext);
    if (use_static) {
        *ex = *cx;
        *ey = *cy;
    } else {
        *ex = *cx - (int)ext[0];
        *ey = *cy - (int)ext[2];
    }
}

/* index of w in _NET_CLIENT_LIST_STACKING (bottom-to-top), -1 if absent */
static int stack_index(Window w)
{
    unsigned long n;
    Window *list = (Window *)get_prop(root, A_NET_CLIENT_LIST_STACKING,
                                      XA_WINDOW, &n, NULL);
    int idx = -1;
    for (unsigned long i = 0; i < n; i++)
        if (list[i] == w)
            idx = (int)i;
    if (list)
        XFree(list);
    return idx;
}

static int workarea(long wa[4])
{
    unsigned long n;
    long *v = (long *)get_prop(root, A_NET_WORKAREA, XA_CARDINAL, &n, NULL);
    int ok = (v && n >= 4);
    for (int i = 0; i < 4; i++)
        wa[i] = ok ? v[i] : 0;
    if (v)
        XFree(v);
    return ok;
}

static int wm_supports(Atom a)
{
    unsigned long n;
    Atom *list = (Atom *)get_prop(root, A_NET_SUPPORTED, XA_ATOM, &n, NULL);
    int found = 0;
    for (unsigned long i = 0; i < n; i++)
        if (list[i] == a)
            found = 1;
    if (list)
        XFree(list);
    return found;
}

/* ---- EWMH client messages -------------------------------------------- */
static void send_root_msg(Window w, Atom type, long d0, long d1, long d2,
                          long d3)
{
    XEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = type;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = d0;
    ev.xclient.data.l[1] = d1;
    ev.xclient.data.l[2] = d2;
    ev.xclient.data.l[3] = d3;
    XSendEvent(dpy, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(dpy);
}

/* source indication 2 = pager/tool: avoids focus-stealing prevention
 * deciding the outcome, so we measure stacking policy, not focus policy. */
static void activate(Window w)
{
    send_root_msg(w, A_NET_ACTIVE_WINDOW, 2, CurrentTime, 0, 0);
}

static void set_above(Window w, int on)
{
    /* _NET_WM_STATE: action (0 remove, 1 add), prop1, prop2, source(1=app) */
    send_root_msg(w, A_NET_WM_STATE, on ? 1 : 0, (long)A_NET_WM_STATE_ABOVE,
                  0, 1);
}

/* ---- window creation -------------------------------------------------- */
static Window make_window(const char *title, int x, int y, int w, int h,
                          unsigned long bg, int gravity)
{
    XSetWindowAttributes swa;
    swa.background_pixel = bg;
    swa.event_mask = StructureNotifyMask | PropertyChangeMask | ExposureMask;

    Window win = XCreateWindow(dpy, root, x, y, (unsigned)w, (unsigned)h, 0,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWBackPixel | CWEventMask, &swa);

    XSizeHints *sh = XAllocSizeHints();
    sh->flags = USPosition | PPosition | USSize | PSize | PWinGravity;
    sh->x = x;
    sh->y = y;
    sh->width = w;
    sh->height = h;
    sh->win_gravity = gravity;
    XSetWMNormalHints(dpy, win, sh);
    XFree(sh);

    XStoreName(dpy, win, title);
    XChangeProperty(dpy, win, A_NET_WM_NAME, A_UTF8_STRING, 8,
                    PropModeReplace, (const unsigned char *)title,
                    (int)strlen(title));

    XClassHint ch = { (char *)"x11wmtest", (char *)"X11wmtest" };
    XSetClassHint(dpy, win, &ch);

    XWMHints wmh = { 0 };
    wmh.flags = InputHint | StateHint;
    wmh.input = True;
    wmh.initial_state = NormalState;
    XSetWMHints(dpy, win, &wmh);
    return win;
}

/* ---- environment report ----------------------------------------------- */
static void report_environment(void)
{
    const char *st = getenv("XDG_SESSION_TYPE");
    const char *wd = getenv("WAYLAND_DISPLAY");
    const char *de = getenv("XDG_CURRENT_DESKTOP");

    printf("Environment\n");
    info("DISPLAY=%s  XDG_SESSION_TYPE=%s  WAYLAND_DISPLAY=%s  DESKTOP=%s",
         DisplayString(dpy), st ? st : "-", wd ? wd : "-", de ? de : "-");
    info("X server vendor: \"%s\" release %d", ServerVendor(dpy),
         VendorRelease(dpy));
    info("root size: %dx%d", DisplayWidth(dpy, scr), DisplayHeight(dpy, scr));

    unsigned long n;
    Window *chk = (Window *)get_prop(root, A_NET_SUPPORTING_WM_CHECK,
                                     XA_WINDOW, &n, NULL);
    if (chk && n) {
        unsigned long ln;
        char *name = (char *)get_prop(chk[0], A_NET_WM_NAME, A_UTF8_STRING,
                                      &ln, NULL);
        info("window manager: %s", name ? name : "(unnamed)");
        if (name)
            XFree(name);
    } else {
        info("window manager: NONE detected — results below are meaningless");
    }
    if (chk)
        XFree(chk);

    struct { Atom a; const char *n; } adv[] = {
        { A_NET_WM_STATE_ABOVE, "_NET_WM_STATE_ABOVE" },
        { A_NET_CLIENT_LIST_STACKING, "_NET_CLIENT_LIST_STACKING" },
        { A_NET_FRAME_EXTENTS, "_NET_FRAME_EXTENTS" },
        { A_NET_WM_STRUT_PARTIAL, "_NET_WM_STRUT_PARTIAL" },
        { A_NET_WORKAREA, "_NET_WORKAREA" },
        { A_NET_ACTIVE_WINDOW, "_NET_ACTIVE_WINDOW" },
    };
    char buf[512] = "";
    for (size_t i = 0; i < sizeof adv / sizeof adv[0]; i++) {
        strcat(buf, adv[i].n);
        strcat(buf, wm_supports(adv[i].a) ? "=yes " : "=NO ");
    }
    info("WM advertises: %s", buf);
    info("gravity under test: %s", use_static
         ? "StaticGravity (client origin == request)"
         : "NorthWestGravity (frame origin == request; Xt/Motif default)");
    putchar('\n');
}

/* ---- main ------------------------------------------------------------- */
int main(int argc, char **argv)
{
    int hold = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--static"))
            use_static = 1;
        else if (!strcmp(argv[i], "--hold") && i + 1 < argc)
            hold = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: %s [--static] [--hold SECONDS]\n",
                    argv[0]);
            return 2;
        }
    }

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "cannot open X display (is DISPLAY set? run inside "
                        "the GNOME session)\n");
        return 2;
    }
    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    init_atoms();
    report_environment();

    int grav = use_static ? StaticGravity : NorthWestGravity;
    int ex, ey, cx, cy, w, h;
    long ext[4];

    /* T1 ---------------------------------------------------------------- */
    const int rx1 = 200, ry1 = 200, rw1 = 360, rh1 = 240;
    printf("T1  initial placement at exact x,y\n");
    Window tw = make_window("x11wmtest: subject", rx1, ry1, rw1, rh1,
                            0x3a6ea5, grav);
    XMapWindow(dpy, tw);
    wait_mapped(tw, 3000);
    effective_pos(tw, &ex, &ey, &cx, &cy, &w, &h, ext);
    info("frame extents l/r/t/b = %ld/%ld/%ld/%ld; client origin %d,%d",
         ext[0], ext[1], ext[2], ext[3], cx, cy);
    check(ex == rx1 && ey == ry1, "T1",
          "position requested %d,%d  got %d,%d", rx1, ry1, ex, ey);
    check(w == rw1 && h == rh1, "T1",
          "size     requested %dx%d  got %dx%d", rw1, rh1, w, h);

    /* T2 ---------------------------------------------------------------- */
    const int rx2 = 420, ry2 = 330, rw2 = 420, rh2 = 260;
    printf("\nT2  programmatic move + resize after map (XMoveResizeWindow)\n");
    XMoveResizeWindow(dpy, tw, rx2, ry2, rw2, rh2);
    settle(400, 3000);
    effective_pos(tw, &ex, &ey, &cx, &cy, &w, &h, ext);
    check(ex == rx2 && ey == ry2, "T2",
          "position requested %d,%d  got %d,%d", rx2, ry2, ex, ey);
    check(w == rw2 && h == rh2, "T2",
          "size     requested %dx%d  got %dx%d", rw2, rh2, w, h);

    /* T3 ---------------------------------------------------------------- */
    printf("\nT3  stays-above (_NET_WM_STATE_ABOVE)\n");
    Window peer = make_window("x11wmtest: peer", rx2 + 60, ry2 + 60, 360,
                              240, 0xa53a3a, NorthWestGravity);
    XMapWindow(dpy, peer);
    wait_mapped(peer, 3000);

    activate(peer);
    settle(400, 3000);
    int si = stack_index(tw), sp = stack_index(peer);
    info("control (no ABOVE, peer activated): subject idx %d, peer idx %d%s",
         si, sp, (sp > si) ? "  -> WM restacks normally"
                           : "  -> peer NOT raised; control inconclusive");

    set_above(tw, 1);
    settle(400, 3000);
    check(has_state(tw, A_NET_WM_STATE_ABOVE), "T3",
          "_NET_WM_STATE_ABOVE present on subject after request");
    si = stack_index(tw);
    sp = stack_index(peer);
    check(si > sp, "T3", "subject above peer after ABOVE set "
          "(stack idx subject %d, peer %d)", si, sp);

    activate(peer);
    settle(400, 3000);
    si = stack_index(tw);
    sp = stack_index(peer);
    check(si > sp, "T3", "subject STILL above after peer activated "
          "(stack idx subject %d, peer %d)", si, sp);

    set_above(tw, 0);
    settle(300, 2000);
    XDestroyWindow(dpy, peer);
    settle(300, 2000);

    /* T4 ---------------------------------------------------------------- */
    printf("\nT4  iconify (XIconifyWindow) and restore\n");
    int px, py;
    effective_pos(tw, &px, &py, &cx, &cy, &w, &h, ext);
    XIconifyWindow(dpy, tw, scr);
    settle(500, 3000);
    long s = wm_state(tw);
    check(s == 3, "T4", "WM_STATE after iconify = %s (want Iconic)",
          wm_state_name(s));
    info("_NET_WM_STATE_HIDDEN %s",
         has_state(tw, A_NET_WM_STATE_HIDDEN) ? "set" : "not set");

    /* ICCCM restore: map the window; EWMH restore: activate it. Do both,
     * as a Motif app's restore path typically does XMapRaised. */
    XMapRaised(dpy, tw);
    activate(tw);
    settle(500, 3000);
    s = wm_state(tw);
    check(s == 1, "T4", "WM_STATE after restore = %s (want Normal)",
          wm_state_name(s));
    effective_pos(tw, &ex, &ey, &cx, &cy, &w, &h, ext);
    check(ex == px && ey == py, "T4",
          "position preserved across iconify: before %d,%d  after %d,%d",
          px, py, ex, ey);

    /* T5 ---------------------------------------------------------------- */
    printf("\nT5  reserved screen edge (DOCK + _NET_WM_STRUT_PARTIAL)\n");
    long wa0[4], wa1[4];
    int have_wa = workarea(wa0);
    if (have_wa)
        info("workarea before: x=%ld y=%ld w=%ld h=%ld",
             wa0[0], wa0[1], wa0[2], wa0[3]);
    else
        info("_NET_WORKAREA not published by this WM — strut check limited "
             "to dock placement");

    const int dock_h = 60;
    const int sw = DisplayWidth(dpy, scr), sh = DisplayHeight(dpy, scr);
    const int dy = sh - dock_h;
    Window dock = make_window("x11wmtest: dock", 0, dy, sw, dock_h,
                              0x3aa55a, NorthWestGravity);
    XChangeProperty(dpy, dock, A_NET_WM_WINDOW_TYPE, XA_ATOM, 32,
                    PropModeReplace,
                    (unsigned char *)&A_NET_WM_WINDOW_TYPE_DOCK, 1);
    long strut[12] = { 0 };
    strut[3] = dock_h;        /* bottom */
    strut[10] = 0;            /* bottom_start_x */
    strut[11] = sw - 1;       /* bottom_end_x */
    XChangeProperty(dpy, dock, A_NET_WM_STRUT_PARTIAL, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)strut, 12);
    XChangeProperty(dpy, dock, A_NET_WM_STRUT, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)strut, 4);
    XMapWindow(dpy, dock);
    wait_mapped(dock, 3000);

    int dx_, dy_, dcx, dcy, dw, dh;
    long dext[4];
    use_static = 0; /* docks are undecorated; compare client origin */
    client_geom(dock, &dcx, &dcy, &dw, &dh);
    frame_extents(dock, dext);
    dx_ = dcx - (int)dext[0];
    dy_ = dcy - (int)dext[2];
    use_static = (grav == StaticGravity);
    check(dx_ == 0 && dy_ == dy, "T5",
          "dock position requested 0,%d  got %d,%d", dy, dx_, dy_);
    if (have_wa && workarea(wa1)) {
        info("workarea after:  x=%ld y=%ld w=%ld h=%ld",
             wa1[0], wa1[1], wa1[2], wa1[3]);
        long bottom0 = wa0[1] + wa0[3], bottom1 = wa1[1] + wa1[3];
        check(bottom0 - bottom1 >= dock_h, "T5",
              "workarea bottom shrank by %ld px (want >= %d)",
              bottom0 - bottom1, dock_h);
    }

    /* T6 ---------------------------------------------------------------- */
    printf("\nT6  Motif decoration hints (_MOTIF_WM_HINTS decorations=0)\n");
    const int rx6 = 260, ry6 = 260;
    Window nd = make_window("x11wmtest: undecorated", rx6, ry6, 300, 180,
                            0x8a5aa5, NorthWestGravity);
    long mh[5] = { 1L << 1 /* MWM_HINTS_DECORATIONS */, 0, 0, 0, 0 };
    XChangeProperty(dpy, nd, A_MOTIF_WM_HINTS, A_MOTIF_WM_HINTS, 32,
                    PropModeReplace, (unsigned char *)mh, 5);
    XMapWindow(dpy, nd);
    wait_mapped(nd, 3000);
    long nx[4];
    int ncx, ncy, nw, nh;
    client_geom(nd, &ncx, &ncy, &nw, &nh);
    frame_extents(nd, nx);
    check(nx[0] == 0 && nx[1] == 0 && nx[2] == 0 && nx[3] == 0, "T6",
          "frame extents l/r/t/b = %ld/%ld/%ld/%ld (want all 0)",
          nx[0], nx[1], nx[2], nx[3]);
    check(ncx == rx6 && ncy == ry6, "T6",
          "undecorated position requested %d,%d  got %d,%d",
          rx6, ry6, ncx, ncy);

    /* ------------------------------------------------------------------- */
    if (hold > 0) {
        printf("\nHolding windows for %d s (screenshot now)...\n", hold);
        settle(hold * 1000, hold * 1000);
    }

    XDestroyWindow(dpy, nd);
    XDestroyWindow(dpy, dock);
    XDestroyWindow(dpy, tw);
    XSync(dpy, False);
    XCloseDisplay(dpy);

    printf("\nResult: %d check(s) failed%s\n", failures,
           failures ? "" : " — every request was honored");
    return failures;
}
