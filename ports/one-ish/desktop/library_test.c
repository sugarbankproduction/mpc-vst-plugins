/* load_test's checks of the library (desktop/library.c), through the window as a person would use it: clicks on
 * COMPARE and SNAPSHOT, the library's page tabs and buttons, a set's numbers held down, knob drags, the mouse wheel,
 * Ctrl+Z / Ctrl+Y, and the dialogs answered by a helper thread. TEST_LIB only reads back what happened (and where a
 * button is, so the clicks land on it). The patches are the ones load_test's patch tests left in its temporary
 * folder (Bright Pad, Shared Sound). Pictures of the new pages with the editor's pictures. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "layout.h"
#include "params.h"
#include "patches.h"
#include "seq_page.h"
#define TEST_FOCUS 0x4F314643   /* as editor.h */

typedef struct AEffect AEffect;
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(AEffect *, float **, float **, int32_t);
    void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect *, float **, float **, int32_t);
    void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t);
    char future[56];
};
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} ev_t;

extern double host_tempo;   /* load_test.c's host: the tempo it reports (0: none) */
static int fails;
static AEffect *fx;
static HWND child;
static float S;
static const char *pics;
static int W, H;
static const char *folder;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}
static intptr_t lt(int op, float opt, char *buf) {
    static char b[1024];
    b[0] = 0;
    intptr_t r = fx->dispatcher(fx, 50, TEST_LIB, op, buf ? buf : b, opt);
    return r;
}
static intptr_t pt(int op, const char *in, char *out, float opt) {
    static char b[1024];
    snprintf(b, sizeof b, "%s", in ? in : "");
    intptr_t r = fx->dispatcher(fx, 50, TEST_PATCH, op, b, opt);
    if (out) snprintf(out, 1024, "%s", b);
    return r;
}
static int key(const char *k) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) return i;
    return -1;
}
static void snap(float *v) { for (int i = 0; i < NPARAMS; i++) v[i] = fx->getParameter(fx, i); }
static int differs(const float *v) {
    for (int i = 0; i < NPARAMS; i++)
        if (fx->getParameter(fx, i) != v[i]) return i;
    return -1;
}
static void title(char *t) { pt(PT_TITLE, NULL, t, 0); }
static int title_starts(const char *want) {
    char t[1024];
    title(t);
    if (strncmp(t, want, strlen(want))) { printf("     title is \"%s\", expected \"%s...\"\n", t, want); return 0; }
    return 1;
}
static void tick(void) { SendMessageA(child, WM_TIMER, 1, 0); }
static int steps(void) { return (int)(lt(LT_STEPS, 0, NULL) % 1000); }
static int redos(void) { return (int)(lt(LT_STEPS, 0, NULL) / 1000); }
static int lib_screen_is(int s) { return lt(LT_SCREEN, 0, NULL) == s; }

/* a picture of the window (24-bit BMP, as load_test's) */
static void shot(const char *suffix) {
    if (!pics) return;
    char path[512];
    const char *dot = strrchr(pics, '.');
    snprintf(path, sizeof path, "%.*s%s.bmp", dot ? (int)(dot - pics) : (int)strlen(pics), pics, suffix);
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bm = CreateCompatibleBitmap(screen, W, H);
    HGDIOBJ old = SelectObject(dc, bm);
    SendMessageA(child, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
    BITMAPINFOHEADER bi = {sizeof bi, W, H, 1, 24, BI_RGB, 0, 0, 0, 0, 0};
    int stride = (W * 3 + 3) & ~3;
    unsigned char *px = malloc((size_t)stride * H);
    FILE *f = px ? fopen(path, "wb") : NULL;
    if (f) {
        GetDIBits(dc, bm, 0, H, px, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
        BITMAPFILEHEADER fh = {0x4d42, (DWORD)(sizeof fh + sizeof bi + stride * H), 0, 0, sizeof fh + sizeof bi};
        fwrite(&fh, sizeof fh, 1, f); fwrite(&bi, sizeof bi, 1, f); fwrite(px, (size_t)stride * H, 1, f);
        fclose(f);
        printf("     wrote %s\n", path);
    }
    free(px);
    SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc); ReleaseDC(NULL, screen);
}

/* ---- the mouse and keys ---- */
static void click_px(int x, int y) {
    LPARAM at = MAKELPARAM(x, y);
    SendMessageA(child, WM_MOUSEMOVE, 0, at);
    SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, at);
    SendMessageA(child, WM_LBUTTONUP, 0, at);
}
static int where(int z, int *x, int *y) {   /* the button or entry z on the window now (TEST_LIB tells where) */
    intptr_t v = lt(LT_ZONE, (float)z, NULL);
    if (v < 0) { printf("     (zone %d is not on the window now)\n", z); return 0; }
    *x = (int)(v & 0xFFFF);
    *y = (int)(v >> 16);
    return 1;
}
static int click_z(int z) {
    int x, y;
    if (!where(z, &x, &y)) return 0;
    click_px(x, y);
    return 1;
}
static void hold_z(int z, int ms) {   /* held down, the window's timer ticking meanwhile */
    int x, y;
    if (!where(z, &x, &y)) return;
    LPARAM at = MAKELPARAM(x, y);
    SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, at);
    for (int t = 0; t < ms; t += 50) { Sleep(50); tick(); }
    SendMessageA(child, WM_LBUTTONUP, 0, at);
}
static void click_base(float x, float y) { click_px((int)lroundf(x * S), (int)lroundf(y * S)); }
static int ctl_center(const char *k, int *x, int *y) {
    int i = key(k);
    for (int c = 0; c < NCTLS; c++)
        if (CTLS[c].param == i) {
            *x = (int)lroundf((CTLS[c].x + CTLS[c].w / 2.0f) * S);
            *y = (int)lroundf((CTLS[c].y + CTLS[c].h / 2.0f) * S);
            return 1;
        }
    return 0;
}
static void drag_knob(const char *k, int dy) {   /* a knob dragged up by dy pixels, in steps */
    int x, y;
    if (!ctl_center(k, &x, &y)) return;
    SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    for (int s = 1; s <= 8; s++) SendMessageA(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, y - dy * s / 8));
    SendMessageA(child, WM_LBUTTONUP, 0, MAKELPARAM(x, y - dy));
}
static void click_ctl(const char *k) {
    int x, y;
    if (ctl_center(k, &x, &y)) click_px(x, y);
}
static void ctrl_key(int vk, int shift) {   /* Ctrl (+ Shift) + a key, in the window */
    BYTE ks[256], was[256];
    GetKeyboardState(was);
    memcpy(ks, was, sizeof ks);
    ks[VK_CONTROL] = 0x80;
    ks[VK_SHIFT] = shift ? 0x80 : 0;
    SetKeyboardState(ks);
    SendMessageA(child, WM_KEYDOWN, vk, 0);
    SetKeyboardState(was);
}

/* ---- dialogs answered by a helper thread: the fields filled in, read back, then a button ---- */
typedef struct {
    const char *title, *text, *notes;     /* the box's title; its line (or note); the save box's notes */
    const char *combo[4];                 /* the save box's TYPE, CATEGORY, MOOD, GROUP (NULL: as they are) */
    int button;
    const char *pic;                      /* a picture of the box (suffix) */
} answer_t;
static void dialog_shot(HWND d, const char *suffix) {
    if (!pics) return;
    RECT r;
    GetWindowRect(d, &r);
    int w = r.right - r.left, h = r.bottom - r.top, stride = (w * 3 + 3) & ~3;
    char path[512];
    const char *dot = strrchr(pics, '.');
    snprintf(path, sizeof path, "%.*s%s.bmp", dot ? (int)(dot - pics) : (int)strlen(pics), pics, suffix);
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bm = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(dc, bm);
    PrintWindow(d, dc, 0);
    BITMAPINFOHEADER bi = {sizeof bi, w, h, 1, 24, BI_RGB, 0, 0, 0, 0, 0};
    unsigned char *px = malloc((size_t)stride * h);
    FILE *f = px ? fopen(path, "wb") : NULL;
    if (f) {
        GetDIBits(dc, bm, 0, h, px, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
        BITMAPFILEHEADER fh = {0x4d42, (DWORD)(sizeof fh + sizeof bi + stride * h), 0, 0, sizeof fh + sizeof bi};
        fwrite(&fh, sizeof fh, 1, f); fwrite(&bi, sizeof bi, 1, f); fwrite(px, (size_t)stride * h, 1, f);
        fclose(f);
        printf("     wrote %s\n", path);
    }
    free(px);
    SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc); ReleaseDC(NULL, screen);
}
static DWORD WINAPI answer(void *arg) {
    const answer_t *a = arg;
    for (int t = 0; t < 1000; t++, Sleep(10)) {
        HWND d = FindWindowA("#32770", a->title);
        if (!d || !IsWindowVisible(d)) continue;
        if (a->text) {
            char got[1200] = "";
            SetDlgItemTextA(d, 101, a->text);
            GetDlgItemTextA(d, 101, got, sizeof got);
            if (strcmp(got, a->text)) continue;
        }
        if (a->notes) SetDlgItemTextA(d, 114, a->notes);
        for (int k = 0; k < 4; k++)
            if (a->combo[k]) SendDlgItemMessageA(d, 110 + k, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)a->combo[k]);
        if (a->pic) { Sleep(150); dialog_shot(d, a->pic); }
        PostMessageA(d, WM_COMMAND, MAKEWPARAM(a->button, BN_CLICKED), 0);
        return 1;
    }
    return 0;
}
static int click_answered(int z, const answer_t *a) {
    HANDLE th = CreateThread(NULL, 0, answer, (void *)a, 0, NULL);
    int ok = click_z(z);
    WaitForSingleObject(th, 11000);
    DWORD found = 0;
    GetExitCodeThread(th, &found);
    CloseHandle(th);
    if (!found) printf("     (no \"%s\" window appeared)\n", a->title);
    return ok && found;
}
static const answer_t YES = {"ONE-ish", NULL, NULL, {0}, IDYES, NULL};

static int file_has(const char *name, const char *what) {   /* a file in the patch folder holds what */
    char path[MAX_PATH + 64], buf[8192];
    snprintf(path, sizeof path, "%s\\%s", folder, name);
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f) fclose(f);
    buf[n] = 0;
    return f && strstr(buf, what) != NULL;
}

/* each Synth's sequence text in the plugin's state (";seq=", ";s2_seq=", ";s3_seq="; "" when none) */
static void seq_texts(char t[3][SQT_BUF]) {
    void *chunk = NULL;
    intptr_t n = fx->dispatcher(fx, 23, 0, 0, &chunk, 0);   /* effGetChunk */
    for (int c = 0; c < 3; c++) {
        char k[16];
        snprintf(k, sizeof k, c ? ";s%d_seq=" : ";seq=", c + 1);
        const char *p = n > 0 && chunk ? strstr((const char *)chunk, k) : NULL;
        int m = 0;
        if (p) for (p += strlen(k); p[m] && p[m] != ';' && m < SQT_BUF - 1; m++) t[c][m] = p[m];
        t[c][m] = 0;
    }
}

/* ---- undo / redo ---- */
static void undo_tests(void) {
    char what[300], buf[1024];
    static float saved[NPARAMS], now[NPARAMS];
    int cut = key("flt_cut"), s2cut = key("s2_flt_cut");
    pt(PT_LOAD, "Bright Pad", NULL, 0);
    snap(saved);
    lt(LT_FORGET, 0, NULL);   /* (an empty history: the earlier tests filled it) */
    int s0 = steps();
    float v0 = fx->getParameter(fx, cut);
    drag_knob("flt_cut", 60);   /* a drag of eight moves: one step */
    float v1 = fx->getParameter(fx, cut);
    lt(LT_LABEL, 0, buf);
    snprintf(what, sizeof what, "UNDO: a knob drag (cutoff %.3f -> %.3f) is one undo step (%d -> %d), \"%s\"", v0, v1, s0,
             steps(), buf);
    check(v1 != v0 && steps() == s0 + 1 && strstr(buf, PARAMS[cut].name) != NULL, what);
    ctrl_key('Z', 0);
    snprintf(what, sizeof what, "UNDO: Ctrl+Z puts every value back exactly (first difference: %d) and the patch is "
             "not edited", differs(saved));
    check(differs(saved) < 0 && !pt(PT_EDITED, NULL, NULL, 0) && title_starts("U  Bright Pad"), what);
    ctrl_key('Y', 0);
    check(fx->getParameter(fx, cut) == v1 && title_starts("U  Bright Pad *"), "REDO: Ctrl+Y brings the drag back");
    ctrl_key('Z', 1);   /* Ctrl+Shift+Z is redo too: nothing left to redo */
    check(fx->getParameter(fx, cut) == v1, "REDO: Ctrl+Shift+Z with nothing to redo changes nothing");
    ctrl_key('Z', 0);
    check(differs(saved) < 0, "UNDO: back to the saved sound");

    /* the mouse wheel: a burst of clicks, one step once it pauses */
    int x, y;
    ctl_center("flt_res", &x, &y);
    POINT sp = {x, y};
    ClientToScreen(child, &sp);
    s0 = steps();
    for (int k = 0; k < 4; k++) SendMessageA(child, WM_MOUSEWHEEL, MAKEWPARAM(0, 120), MAKELPARAM(sp.x, sp.y));
    Sleep(450);
    tick();
    snprintf(what, sizeof what, "UNDO: four mouse-wheel clicks on a knob, then a pause: one step (%d -> %d)", s0, steps());
    check(steps() == s0 + 1 && differs(saved) >= 0, what);
    ctrl_key('Z', 0);
    check(differs(saved) < 0, "UNDO: the wheel's four clicks undone together");

    /* a button */
    s0 = steps();
    click_ctl("o2_sync");
    snprintf(what, sizeof what, "UNDO: a button click is a step (%d -> %d)", s0, steps());
    check(steps() == s0 + 1 && differs(saved) >= 0, what);
    ctrl_key('Z', 0);
    check(differs(saved) < 0, "UNDO: the button is back");

    /* Synth 2's knob, with Synth 2 in focus */
    fx->dispatcher(fx, 50, TEST_FOCUS, 2, NULL, 1.0f);
    float c1 = fx->getParameter(fx, cut), c2 = fx->getParameter(fx, s2cut);
    drag_knob("flt_cut", -50);
    int moved = fx->getParameter(fx, s2cut) != c2 && fx->getParameter(fx, cut) == c1;
    ctrl_key('Z', 0);
    snprintf(what, sizeof what, "UNDO with Synth 2 in focus: its cutoff moved (Synth 1's not) and is put back (%d)", differs(saved));
    check(moved && differs(saved) < 0, what);
    fx->dispatcher(fx, 50, TEST_FOCUS, 1, NULL, 0.0f);

    /* a step edit on the SEQ page: a double-click makes a note, undo takes it away */
    char seq0[SQT_BUF], seq1[SQT_BUF];
    {
        static char b[SQT_BUF];
        int zx = (int)lroundf((ZONE_TAB_SEQ[0] + ZONE_TAB_SEQ[2] / 2.0f) * S), zy = (int)lroundf((ZONE_TAB_SEQ[1] + ZONE_TAB_SEQ[3] / 2.0f) * S);
        click_px(zx, zy);
        fx->dispatcher(fx, 50, TEST_SEQ, SQT_TEXT, b, 0);
        snprintf(seq0, sizeof seq0, "%s", b);
        float cx = ZONE_SCREEN[0] + 8 + 24 + 9.5f * (ZONE_SCREEN[2] - 16 - 24) / 16.0f, cy = ZONE_SCREEN[1] + 6 + 36 + 20 + 18 + 22 * 2 + 11;
        LPARAM at = MAKELPARAM((int)lroundf(cx * S), (int)lroundf(cy * S));
        s0 = steps();
        SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, at);
        SendMessageA(child, WM_LBUTTONUP, 0, at);
        SendMessageA(child, WM_LBUTTONDBLCLK, MK_LBUTTON, at);
        SendMessageA(child, WM_LBUTTONUP, 0, at);
        fx->dispatcher(fx, 50, TEST_SEQ, SQT_TEXT, b, 0);
        snprintf(seq1, sizeof seq1, "%s", b);
        int s1 = steps();
        ctrl_key('Z', 0);
        tick();
        fx->dispatcher(fx, 50, TEST_SEQ, SQT_TEXT, b, 0);
        snprintf(what, sizeof what, "UNDO: a note made on the SEQ page is one step (%d -> %d) and Ctrl+Z takes it away",
                 s0, s1);
        check(strcmp(seq0, seq1) && s1 == s0 + 1 && !strcmp(b, seq0), what);
    }

    /* (2026-10-29) the SEQ page's GEN view, Synth 2 in focus: its KEY changed there (Synth 1's stays), GENERATE fills
     * Synth 2's sequence only, in its key and scale, as one undo step; Ctrl+Z takes it back; RE-ROLL makes another */
    {
        char t1[3][SQT_BUF], t2[3][SQT_BUF], t3[3][SQT_BUF], t4[3][SQT_BUF];
        float x0 = ZONE_SCREEN[0] + 8, ty = ZONE_SCREEN[1] + 6 + 36 + 9, help = ZONE_SCREEN[1] + 6 + 36 + 20 + 18 + 176 + 8;
        float bw = (ZONE_SCREEN[2] - 16 - 4 * 7) / 8.0f;
        int kr = key("key_root"), kr2 = key("s2_key_root"), ks2 = key("s2_key_scale");
        float k1 = fx->getParameter(fx, kr), k2 = fx->getParameter(fx, kr2);
        fx->dispatcher(fx, 50, TEST_FOCUS, 2, NULL, 1.0f);   /* Synth 2 */
        tick();
        click_base(x0 + 152 + 22, ty);   /* the step editor's GEN view */
        POINT sp = {(LONG)lroundf((x0 + 80) * S), (LONG)lroundf((help + 10) * S)};   /* KEY, in the values row */
        ClientToScreen(child, &sp);
        SendMessageA(child, WM_MOUSEWHEEL, MAKEWPARAM(0, 120), MAKELPARAM(sp.x, sp.y));
        SendMessageA(child, WM_MOUSEWHEEL, MAKEWPARAM(0, 120), MAKELPARAM(sp.x, sp.y));
        int key2 = (int)lroundf(fx->getParameter(fx, kr2) * 11), scale2 = (int)lroundf(fx->getParameter(fx, ks2) * (KS_COUNT - 1));
        snprintf(what, sizeof what, "GEN: two wheel clicks on KEY move Synth 2's key up a tone (%d -> %d), Synth 1's stays",
                 (int)lroundf(k2 * 11), key2);
        check(key2 == ((int)lroundf(k2 * 11) + 2) % 12 && fx->getParameter(fx, kr) == k1, what);
        seq_texts(t1);
        s0 = steps();
        click_base(x0 + bw + 2, help + 24 + 9);   /* GENERATE */
        seq_texts(t2);
        int s1 = steps(), in_key = 1, notes = 0;
        sq_seq_t q;
        sq_clear(&q);
        static char b[SQT_BUF];
        fx->dispatcher(fx, 50, TEST_SEQ, SQT_TEXT, b, 0);   /* (the focused Synth's) */
        int dec = sq_decode(b, &q, 4096, 4096);
        for (int s = 0; s < SQ_STEPS; s++)
            for (int l = 0; l < SQ_NL; l++)
                if (sq_has(&q.n[s][l])) {
                    int pc = ((q.n[s][l].note - key2) % 12 + 12) % 12, in = 0;
                    for (int i = 0; i < KS_SIZE[scale2]; i++) in |= KS_STEPS[scale2][i] == pc;
                    in_key &= in;
                    notes++;
                }
        snprintf(what, sizeof what, "GEN: GENERATE fills Synth 2's sequence (%d notes, all in its key and scale: %d) and only "
                 "Synth 2's (Synth 1 %s, Synth 3 %s), one undo step (%d -> %d)", notes, in_key,
                 strcmp(t1[0], t2[0]) ? "changed" : "kept", strcmp(t1[2], t2[2]) ? "changed" : "kept", s0, s1);
        check(dec && notes > 0 && in_key && strcmp(t1[1], t2[1]) && !strcmp(t1[0], t2[0]) && !strcmp(t1[2], t2[2]) &&
              s1 == s0 + 1, what);
        if (pics) shot("_seq_gen");
        ctrl_key('Z', 0);
        tick();
        seq_texts(t3);
        snprintf(what, sizeof what, "GEN: Ctrl+Z takes the generated sequence back in one step (%d), the key change stays (%d)",
                 !strcmp(t3[1], t1[1]), (int)lroundf(fx->getParameter(fx, kr2) * 11) == key2);
        check(!strcmp(t3[1], t1[1]) && (int)lroundf(fx->getParameter(fx, kr2) * 11) == key2, what);
        click_base(x0 + 3 * bw + 6, help + 24 + 9);   /* RE-ROLL */
        click_base(x0 + 3 * bw + 6, help + 24 + 9);   /* (twice: a new seed each time) */
        seq_texts(t4);
        check(strcmp(t4[1], t1[1]) && strcmp(t4[1], t2[1]) && !strcmp(t4[0], t1[0]),
              "GEN: RE-ROLL makes another pattern for Synth 2 (and leaves Synth 1's alone)");
        click_base(x0 + 56 + 22, ty);   /* NOTES again */
        fx->dispatcher(fx, 50, TEST_FOCUS, 1, NULL, 0.0f);
    }

    /* a load is a step: undo brings back the patch and its name (and whether it was edited) */
    pt(PT_LOAD, "Bright Pad", NULL, 0);   /* (the step editor's clicks mark the sound edited) */
    click_base(ZONE_SCREEN[0] + 200, ZONE_SCREEN[1] + 23);   /* the patch name: PATCHES */
    check(lt(LT_SCREEN, 0, NULL) == SCR_PATCHES, "the patch name opens PATCHES, with the library's tabs");
    click_z(Z_PATCH_ITEM + 4);
    int loaded = title_starts("F 005");
    ctrl_key('Z', 0);
    snprintf(what, sizeof what, "UNDO: loading factory preset 5 from the list, then Ctrl+Z: Bright Pad again, exactly (%d, edited %d)",
             differs(saved), (int)pt(PT_EDITED, NULL, NULL, 0));
    check(loaded && differs(saved) < 0 && title_starts("U  Bright Pad") && !pt(PT_EDITED, NULL, NULL, 0), what);
    ctrl_key('Y', 0);
    check(title_starts("F 005"), "REDO: the load again");
    pt(PT_LOAD, "Bright Pad", NULL, 0);

    /* the randomiser's RANDOMISE is one step */
    {
        float x0 = ZONE_SCREEN[0] + 8.0f, x1 = ZONE_SCREEN[0] + ZONE_SCREEN[2] - 8.0f, top = ZONE_SCREEN[1] + 6.0f + 36 + 20 + 18;
        float half = (x1 - x0 - 8) / 2;
        click_base(ZONE_TAB_RANDOM[0] + ZONE_TAB_RANDOM[2] / 2.0f, ZONE_TAB_RANDOM[1] + ZONE_TAB_RANDOM[3] / 2.0f);
        s0 = steps();
        click_base(x0 + half / 2, top + 22);
        lt(LT_LABEL, 0, buf);
        snprintf(what, sizeof what, "UNDO: RANDOMISE (every param it moved) is one step (%d -> %d, \"%s\")", s0, steps(), buf);
        check(steps() == s0 + 1 && !strcmp(buf, "Randomise") && differs(saved) >= 0, what);
        ctrl_key('Z', 0);
        check(differs(saved) < 0, "UNDO: the random sound undone in one go, every value exact");
    }

    /* the history's length */
    for (int k = 0; k < 105; k++) click_ctl("o2_sync");
    snprintf(what, sizeof what, "UNDO: the history keeps the last 100 steps (%d)", steps());
    check(steps() == 100, what);
    int n = 0;
    while (lt(LT_UNDO, 0, NULL) && n < 200) n++;
    snap(now);
    snprintf(what, sizeof what, "UNDO: 100 undos, then nothing more (%d), Bright Pad's button back to %s", n,
             differs(saved) < 0 ? "as saved" : "a different state");
    check(n == 100 && redos() == 100, what);
    pt(PT_LOAD, "Bright Pad", NULL, 0);
    click_base(ZONE_TAB_MATRIX[0] + 20, ZONE_TAB_MATRIX[1] + 20);
}

/* ---- COMPARE, autosave and snapshots ---- */
static void edit_some(float a) {   /* a few changes in Synths 1 and 3 */
    fx->setParameter(fx, key("flt_cut"), a);
    fx->setParameter(fx, key("amp_r"), 1.0f - a);
    fx->setParameter(fx, key("s3_flt_res"), a / 2);
    fx->setParameter(fx, key("s3_syn_on"), 1.0f);
}
static void compare_tests(void) {
    char what[300], buf[1024];
    static float saved[NPARAMS], edit[NPARAMS];
    pt(PT_LOAD, "Bright Pad", NULL, 0);
    snap(saved);
    edit_some(0.23f);
    snap(edit);
    click_z(Z_COMPARE);
    tick();
    snprintf(what, sizeof what, "COMPARE: the saved version plays (first difference %d), the name says COMPARE", differs(saved));
    check(lt(LT_COMPARING, 0, NULL) == 1 && differs(saved) < 0 && title_starts("COMPARE  U  Bright Pad"), what);
    shot("_lib_compare");
    click_z(Z_COMPARE);
    snprintf(what, sizeof what, "COMPARE again: the edit back, in Synths 1 and 3 (first difference %d)", differs(edit));
    check(!lt(LT_COMPARING, 0, NULL) && differs(edit) < 0 && title_starts("U  Bright Pad *"), what);

    int autos = (int)lt(LT_AUTOS, 0, NULL);
    click_z(Z_COMPARE);
    click_base(ZONE_SCREEN[0] + 200, ZONE_SCREEN[1] + 23);   /* PATCHES, factory preset 2 */
    click_z(Z_PATCH_ITEM + 1);
    lt(LT_SNAP_INFO, 0, buf);
    snprintf(what, sizeof what, "COMPARE then loading another sound: COMPARE ends, the hidden edit is autosaved (%d -> %d: %s)",
             autos, (int)lt(LT_AUTOS, 0, NULL), buf);
    check(!lt(LT_COMPARING, 0, NULL) && lt(LT_AUTOS, 0, NULL) == autos + 1 && strstr(buf, "|U|Bright Pad|1|Autosave before loading") != NULL
          && title_starts("F 002"), what);
    click_z(Z_SNAPSHOT);
    check(lt(LT_SCREEN, 0, NULL) == SCR_SNAPS, "SNAPSHOT opens the SNAPSHOTS page");
    click_z(Z_SNAP_ITEM + 0);
    click_z(Z_SNAP_ACT + 1);   /* LOAD */
    snprintf(what, sizeof what, "the autosave LOADs back the edit (first difference %d), as Bright Pad edited", differs(edit));
    check(differs(edit) < 0 && title_starts("U  Bright Pad *"), what);

    /* editing while comparing: the saved version is edited, the hidden edit kept */
    edit_some(0.44f);   /* (an edit since the autosave) */
    autos = (int)lt(LT_AUTOS, 0, NULL);
    click_z(Z_COMPARE);
    drag_knob("flt_res", 30);
    check(!lt(LT_COMPARING, 0, NULL) && lt(LT_AUTOS, 0, NULL) == autos + 1,
          "a knob turned while comparing: COMPARE ends and the edit it hid becomes an autosave");
    pt(PT_LOAD, "Bright Pad", NULL, 0);
    autos = (int)lt(LT_AUTOS, 0, NULL);
    click_z(Z_COMPARE);
    check(!lt(LT_COMPARING, 0, NULL), "COMPARE with no changes says so and stays off");
    pt(PT_LOAD_POS, NULL, NULL, 0);
    pt(PT_LOAD, "Bright Pad", NULL, 0);
    check(lt(LT_AUTOS, 0, NULL) == autos, "no autosave when nothing was edited");
}
static void snapshot_tests(void) {
    char what[300], buf[1024];
    static float a[NPARAMS], b[NPARAMS];
    static const answer_t take1 = {"Snapshot", "first try", NULL, {0}, IDOK, NULL},
                          take2 = {"Snapshot", "second, brighter", NULL, {0}, IDOK, NULL},
                          note = {"Notes", "changed note", NULL, {0}, IDOK, NULL},
                          save_new = {"Save as", "From Snapshot", NULL, {0}, IDOK, NULL};
    pt(PT_LOAD, "Bright Pad", NULL, 0);
    click_z(Z_SNAPSHOT);
    if (lt(LT_SCREEN, 0, NULL) != SCR_SNAPS) click_z(Z_SNAPSHOT);
    int n0 = (int)lt(LT_SNAPS, 0, NULL);
    edit_some(0.61f);
    snap(a);
    click_answered(Z_SNAP_ACT + 0, &take1);   /* TAKE, with a note */
    lt(LT_SNAP_INFO, 0, buf);
    snprintf(what, sizeof what, "SNAPSHOTS: TAKE asks for a note and keeps the sound (%d -> %d: %s)", n0, (int)lt(LT_SNAPS, 0, NULL), buf);
    check(lt(LT_SNAPS, 0, NULL) == n0 + 1 && strstr(buf, "|U|Bright Pad|0|first try") != NULL && strlen(buf) > 19, what);
    edit_some(0.12f);
    snap(b);
    click_answered(Z_SNAP_ACT + 0, &take2);
    check(lt(LT_SNAPS, 0, NULL) == n0 + 2, "SNAPSHOTS: a second one, newest first");
    shot("_lib_snapshots");
    click_z(Z_SNAP_ITEM + 1);   /* the first one */
    click_z(Z_SNAP_ACT + 1);   /* LOAD */
    snprintf(what, sizeof what, "SNAPSHOTS: LOAD brings the first snapshot back exactly, all three Synths (%d)", differs(a));
    check(differs(a) < 0 && title_starts("U  Bright Pad *"), what);
    int sel = (int)lt(LT_SNAP_SEL, 0, NULL);
    click_answered(Z_SNAP_ACT + 2, &note);   /* NOTES */
    lt(LT_SNAP_INFO, (float)sel, buf);
    check(strstr(buf, "|changed note") != NULL, "SNAPSHOTS: NOTES changes the snapshot's note");
    int n1 = (int)lt(LT_SNAPS, 0, NULL);
    for (int i = 0; i < n1; i++) {   /* "second, brighter": DELETE it (asked) */
        lt(LT_SNAP_INFO, (float)i, buf);
        if (strstr(buf, "second, brighter")) { click_z(Z_SNAP_ITEM + i); break; }
    }
    click_answered(Z_SNAP_ACT + 3, &YES);
    check(lt(LT_SNAPS, 0, NULL) == n1 - 1, "SNAPSHOTS: DELETE (asked) removes it");
    for (int i = 0; i < n1 - 1; i++) {   /* "changed note": SAVE OVER Bright Pad */
        lt(LT_SNAP_INFO, (float)i, buf);
        if (strstr(buf, "changed note")) { click_z(Z_SNAP_ITEM + i); break; }
    }
    click_z(Z_SNAP_ACT + 4);
    pt(PT_LOAD_POS, NULL, NULL, 2);
    pt(PT_LOAD, "Bright Pad", NULL, 0);
    snprintf(what, sizeof what, "SNAPSHOTS: SAVE OVER puts the snapshot into Bright Pad (%d)", differs(a));
    check(differs(a) < 0 && !pt(PT_EDITED, NULL, NULL, 0), what);
    for (int i = 0; i < (int)lt(LT_SNAPS, 0, NULL); i++) {
        lt(LT_SNAP_INFO, (float)i, buf);
        if (strstr(buf, "changed note")) { click_z(Z_SNAP_ITEM + i); break; }
    }
    click_answered(Z_SNAP_ACT + 5, &save_new);   /* SAVE NEW */
    check(file_has("From Snapshot.oneish", "name=From Snapshot") && title_starts("U  From Snapshot"),
          "SNAPSHOTS: SAVE NEW saves it as a new patch (asks for the name)");
    int u = steps();
    click_z(Z_SNAP_ACT + 6);   /* UNDO, REDO on the page */
    int r = redos();
    click_z(Z_SNAP_ACT + 7);
    snprintf(what, sizeof what, "SNAPSHOTS: the UNDO and REDO buttons (%d steps; after UNDO %d to redo; after REDO %d)", u, r, redos());
    check(u > 0 && r >= 1 && redos() == r - 1, what);

    /* autosaves: one per loaded-over edit, the newest 32 kept */
    for (int k = 0; k < 36; k++) {
        fx->setParameter(fx, key("flt_cut"), 0.2f + k * 0.01f);
        pt(PT_LOAD_POS, NULL, NULL, (float)(k % 5));
    }
    snprintf(what, sizeof what, "AUTOSAVE: 36 edited sounds loaded over: the newest 32 autosaves kept (%d); the snapshots "
             "taken by hand stay", (int)lt(LT_AUTOS, 0, NULL));
    int hand = 0, all = (int)lt(LT_SNAPS, 0, NULL);
    for (int i = 0; i < all; i++) {
        lt(LT_SNAP_INFO, (float)i, buf);
        hand += strstr(buf, "|0|first try") || strstr(buf, "|0|changed note");
    }
    check(lt(LT_AUTOS, 0, NULL) == 32 && hand == 1, what);
}

/* ---- tags and notes ---- */
static void tag_tests(void) {
    char what[300], buf[1024];
    static const answer_t save = {"Save as", "Tagged Lead", "Use the mod wheel.", {"Single", "Lead", "Bright", "Live"}, IDOK, "_lib_save_box"},
                          add = {"New tag", "Ambient", NULL, {0}, IDOK, NULL},
                          ren = {"Rename tag", "Airy", NULL, {0}, IDOK, NULL},
                          ren2 = {"Rename tag", "Studio A", NULL, {0}, IDOK, NULL},
                          notes = {"Notes", "New notes\r\nline two", NULL, {0}, IDOK, NULL};
    pt(PT_LOAD, "Bright Pad", NULL, 0);
    click_base(ZONE_SCREEN[0] + 200, ZONE_SCREEN[1] + 23);
    if (lt(LT_SCREEN, 0, NULL) != SCR_PATCHES) click_base(ZONE_SCREEN[0] + 200, ZONE_SCREEN[1] + 23);
    click_answered(Z_PATCH_ACT + PA_SAVE_AS, &save);
    lt(LT_META, 0, buf);
    snprintf(what, sizeof what, "the save box sets the tags and notes: \"%s\"", buf);
    check(!strcmp(buf, "Single|Lead|Bright|Live|Use the mod wheel.") && title_starts("U  Tagged Lead"), what);
    check(file_has("Tagged Lead.oneish", "ONE-ish patch 2\nname=Tagged Lead\ntype=Single\ncategory=Lead\nmood=Bright\n"
                   "group=Live\nnotes=Use the mod wheel.\n"), "the file (version 2) holds them after its name");
    host_tempo = 120.0;
    Sleep(550);
    lt(LT_TICK, 0, NULL);
    lt(LT_HOME, 0, buf);
    snprintf(what, sizeof what, "the home line under the name: \"%s\"", buf);
    check(strstr(buf, "Single  |  Lead  |  Bright  |  Live  |  NOTES") && strstr(buf, "120.0 BPM"), what);

    /* the TAGS page: filters */
    click_z(Z_LIB_TAB + 5);
    check(lt(LT_SCREEN, 0, NULL) == SCR_TAGS, "the TAGS tab opens the tags page");
    click_z(Z_TAG_ITEM + 1000 * 1 + 1);   /* CATEGORY Lead */
    int v = (int)lt(LT_VIEW, 0, NULL);
    snprintf(what, sizeof what, "TAGS: CATEGORY Lead filters the list: %d factory presets, %d of yours", v / 1000, v % 1000);
    check(v == 5 * 1000 + 1, what);
    shot("_lib_tags");
    click_z(Z_LIB_TAB + 0);
    shot("_lib_patches_filtered");
    pt(PT_LOAD_POS, NULL, NULL, 4);   /* Sync Lead; NEXT goes to the next Lead */
    int p0 = (int)pt(PT_POS, NULL, NULL, 0), p1 = (int)pt(PT_STEP, NULL, NULL, 1);
    lt(LT_META, 0, buf);
    snprintf(what, sizeof what, "with the filter, NEXT steps to the next Lead (%d -> %d: %s)", p0, p1, buf);
    check(p1 > p0 + 1 && !strncmp(buf, "Single|Lead|", 12), what);
    click_z(Z_LIB_TAB + 5);
    click_z(Z_TAG_ITEM + 1000 * 1 + 1);   /* again: off */
    v = (int)lt(LT_VIEW, 0, NULL);
    check(v / 1000 == fx->numPrograms && v % 1000 == (int)pt(PT_COUNT, NULL, NULL, 0), "TAGS: a second click takes the filter off");

    /* the lists: add, rename, delete; TAG PATCH */
    click_z(Z_TAG_ITEM + 2000 + 0);   /* MOOD Bright (selected; filtered) */
    click_answered(Z_TAG_ACT + 1, &add);   /* ADD */
    lt(LT_TAGS, 2, buf);
    check(strstr(buf, ",Ambient") != NULL && file_has("tags.oneishtags", "mood=Ambient"), "TAGS: ADD puts a new MOOD tag in the list (saved)");
    click_z(Z_TAG_ACT + 4);   /* CLEAR */
    check(!(lt(LT_VIEW, 0, NULL) / 1000 != fx->numPrograms), "TAGS: CLEAR takes every filter off");
    lt(LT_TAGS, 2, buf);
    int amb = 0;
    for (char *c = buf; *c; c++) amb += *c == ',';   /* Ambient: the last */
    click_z(Z_TAG_ITEM + 2000 + amb);
    click_answered(Z_TAG_ACT + 2, &ren);
    lt(LT_TAGS, 2, buf);
    check(strstr(buf, "Airy") && !strstr(buf, "Ambient"), "TAGS: RENAME renames a tag");
    click_z(Z_TAG_ACT + 4);
    click_z(Z_LIB_TAB + 5);   /* (the first page of the lists again) */
    pt(PT_LOAD, "Tagged Lead", NULL, 0);
    click_z(Z_TAG_ITEM + 3000 + 2);   /* GROUP Studio */
    click_z(Z_TAG_ACT + 0);   /* TAG PATCH */
    lt(LT_META, 0, buf);
    check(strstr(buf, "|Studio|") && file_has("Tagged Lead.oneish", "group=Studio\n"), "TAGS: TAG PATCH puts GROUP Studio on the current patch");
    click_answered(Z_TAG_ACT + 2, &ren2);
    check(file_has("Tagged Lead.oneish", "group=Studio A\n"), "TAGS: renaming a tag renames it in every patch file that has it");
    click_answered(Z_TAG_ACT + 3, &YES);   /* DELETE (asked) */
    lt(LT_TAGS, 3, buf);
    check(!strstr(buf, "Studio A") && !file_has("Tagged Lead.oneish", "group=") , "TAGS: DELETE takes the tag off the list and the patches");
    click_z(Z_TAG_ACT + 4);
    click_answered(Z_TAG_ACT + 5, &notes);   /* NOTES */
    lt(LT_META, 0, buf);
    check(strstr(buf, "|New notes\nline two") && file_has("Tagged Lead.oneish", "notes=New notes\\nline two\n"),
          "TAGS: NOTES writes the patch's notes (two lines, kept on one line in the file)");
    click_z(Z_TAG_ACT + 6);   /* SHOW NOTES */
    check(lt(LT_NOTES_SHOWN, 0, buf) && strstr(buf, "line two"), "SHOW NOTES: the notes show where the help goes");
    SendMessageA(child, WM_MOUSEMOVE, 0, MAKELPARAM(2, 2));
    shot("_lib_tags_notes");
    click_z(Z_TAG_ACT + 6);
    host_tempo = 0;

    /* an old (version 1) patch with a category= line still loads */
    char path[MAX_PATH + 64];
    snprintf(path, sizeof path, "%s\\Old One.oneish", folder);
    FILE *f = fopen(path, "wb");
    if (f) { fputs("ONE-ish patch 1\nname=Old One\ncategory=Pad\nflt_cut=40\n", f); fclose(f); }
    int r = (int)pt(PT_LOAD, "Old One", NULL, 0);
    lt(LT_META, 0, buf);
    check(r == PE_OK && !strncmp(buf, "|Pad|", 5), "a version-1 patch file loads as before, its category= line as its CATEGORY");
}

/* ---- performance sets, and MIDI program / bank change ---- */
static void send_events(const unsigned char (*m)[3], int n) {
    static ev_t ev[8];
    struct { int32_t n; intptr_t reserved; void *e[8]; } list = {0};
    for (int k = 0; k < n; k++) {
        memset(&ev[k], 0, sizeof ev[k]);
        ev[k].type = 1;
        ev[k].byteSize = sizeof ev[k];
        memcpy(ev[k].midiData, m[k], 3);
        list.e[k] = &ev[k];
    }
    list.n = n;
    fx->dispatcher(fx, 25 /* effProcessEvents */, 0, 0, &list, 0);
}
static int pump_until(const char *want) {   /* the window's messages (the hidden window's timer) for up to 2 s */
    for (int t = 0; t < 200; t++) {
        MSG m;
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
        char tt[1024];
        title(tt);
        if (!strncmp(tt, want, strlen(want))) return 1;
        Sleep(10);
    }
    return title_starts(want);
}
static void set_tests(void) {
    char what[300], buf[1024];
    static const answer_t add = {"New set", "Live Show", NULL, {0}, IDOK, NULL},
                          name = {"Set name", "Live Show 2", NULL, {0}, IDOK, NULL},
                          notes = {"Notes", "Encore: B3", NULL, {0}, IDOK, NULL};
    click_base(ZONE_SCREEN[0] + 200, ZONE_SCREEN[1] + 23);
    if (!lib_screen_is(SCR_PATCHES)) click_base(ZONE_SCREEN[0] + 200, ZONE_SCREEN[1] + 23);
    click_z(Z_LIB_TAB + 1);
    check(lt(LT_SCREEN, 0, NULL) == SCR_SET && lt(LT_SETS, 0, NULL) == 1, "the SET tab: the current set (a first one made)");
    pt(PT_LOAD, "Tagged Lead", NULL, 0);
    hold_z(Z_SET_SLOT + 0, 750);   /* A1 held: assigned */
    lt(LT_SLOT, 0, buf);
    snprintf(what, sizeof what, "SET: holding A1 puts the current patch there (%s)", buf);
    check(!strcmp(buf, "U,0,Tagged Lead"), what);
    pt(PT_LOAD_POS, NULL, NULL, 3);
    click_z(Z_SET_BANK + 1);   /* bank B: its eight shown, nothing loads */
    check(title_starts("F 004"), "SET: bank B is shown (preview); the sound stays");
    hold_z(Z_SET_SLOT + 10, 750);
    lt(LT_SLOT, 10, buf);
    check(!strcmp(buf, "F,3,Acid Line"), "SET: holding B3 puts factory preset 4 there");
    click_z(Z_SET_BANK + 0);
    click_z(Z_SET_SLOT + 0);   /* A1: a click loads */
    check(title_starts("U  Tagged Lead"), "SET: a click on A1 loads its patch");
    click_z(Z_SET_SLOT + 1);   /* A2 (empty): selected */
    click_z(Z_SET_SIDE + 2);   /* ASSIGN */
    lt(LT_SLOT, 1, buf);
    int assigned = !strcmp(buf, "U,0,Tagged Lead");
    click_z(Z_SET_SIDE + 3);   /* CLEAR */
    check(assigned && !lt(LT_SLOT, 1, buf), "SET: ASSIGN fills the selected number, CLEAR empties it");
    click_z(Z_SET_SLOT + 0);
    shot("_lib_set");
    /* ARRANGE: add, name, notes, copy, up, down, delete */
    click_z(Z_LIB_TAB + 3);
    click_answered(Z_ARR_ACT + 4, &add);
    check(lt(LT_SETS, 0, NULL) % 1000 == 2, "ARRANGE: ADD makes a new set (asks its name)");
    click_z(Z_SETS_ITEM + 1);
    click_answered(Z_ARR_ACT + 5, &name);
    click_answered(Z_ARR_ACT + 6, &notes);
    lt(LT_SET_INFO, 1, buf);
    check(!strcmp(buf, "Live Show 2|Encore: B3"), "ARRANGE: NAME and NOTES change the selected set");
    click_z(Z_ARR_ACT + 2);   /* COPY */
    lt(LT_SET_INFO, 2, buf);
    check(lt(LT_SETS, 0, NULL) % 1000 == 3 && !strncmp(buf, "Live Show 2 copy|Encore", 23), "ARRANGE: COPY doubles it after itself");
    click_z(Z_ARR_ACT + 0);   /* UP: the copy to place 2 */
    lt(LT_SET_INFO, 1, buf);
    int up = !strncmp(buf, "Live Show 2 copy", 16);
    click_z(Z_ARR_ACT + 1);   /* DOWN again */
    lt(LT_SET_INFO, 2, buf);
    check(up && !strncmp(buf, "Live Show 2 copy", 16), "ARRANGE: UP and DOWN move the selected set");
    shot("_lib_arrange");
    click_answered(Z_ARR_ACT + 3, &YES);   /* DELETE (asked) */
    check(lt(LT_SETS, 0, NULL) % 1000 == 2, "ARRANGE: DELETE removes it");
    click_z(Z_LIB_TAB + 2);   /* ALL SETS: set 2 played */
    shot("_lib_sets");
    click_z(Z_SETS_ITEM + 1);
    check(lt(LT_SETS, 0, NULL) / 1000 == 1 && lt(LT_SCREEN, 0, NULL) == SCR_SET, "ALL SETS: a click makes that set current and shows it");
    click_z(Z_SET_SIDE + 0);   /* back to set 1 */
    check(lt(LT_SETS, 0, NULL) / 1000 == 0 && file_has("sets.oneishsets", "A1=U,0,Tagged Lead\n") &&
          file_has("sets.oneishsets", "B3=F,3,Acid Line\n") && file_has("sets.oneishsets", "name=Live Show 2\nnotes=Encore: B3\n"),
          "the sets are saved in sets.oneishsets");

    /* MIDI from the host: bank select and program change (loaded by the hidden window's timer) */
    pt(PT_LOAD_POS, NULL, NULL, 0);
    unsigned char pos40[3][3] = {{0xB0, 0, 0}, {0xB0, 32, 0}, {0xC0, (unsigned char)fx->numPrograms, 0}};
    send_events((const unsigned char (*)[3])pos40, 3);
    check(pump_until("U  Bright Pad"), "MIDI bank 0 / 0, program 40: the first user patch (by the hidden window's timer)");
    unsigned char set1[3][3] = {{0xB0, 0, 1}, {0xB0, 32, 0}, {0xC0, 10, 0}};
    send_events((const unsigned char (*)[3])set1, 3);
    check(pump_until("F 004  Acid Line"), "MIDI bank 1 / 0, program 10: set 1's B3");
    unsigned char far_[3][3] = {{0xB0, 0, 0}, {0xB0, 32, 1}, {0xC0, 0, 0}};
    send_events((const unsigned char (*)[3])far_, 3);
    pump_until("F 004  Acid Line");
    check(title_starts("F 004  Acid Line"), "MIDI asking for a patch past the end of the list changes nothing");
    pt(PT_LOAD_POS, NULL, NULL, 0);
}
int library_tests(void *effect, HWND window, float scale, const char *bmp, int w, int h, const char *patch_folder) {
    fx = effect;
    child = window;
    S = scale;
    pics = bmp;
    W = w;
    H = h;
    folder = patch_folder;
    fails = 0;
    undo_tests();
    compare_tests();
    snapshot_tests();
    tag_tests();
    set_tests();
    click_base(ZONE_TAB_MATRIX[0] + 20, ZONE_TAB_MATRIX[1] + 20);
    return fails;
}
