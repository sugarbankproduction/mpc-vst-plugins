/* load_test's checks of IMPORT .M1P and REPORT (2026-10-31, m1p_import.c): plain clicks on the PATCHES page's
 * buttons, with the file dialog replaced by the files load_test names (TEST_PATCH PT_M1P_PICK); the files are our own
 * synthetic ones in test/m1p/ (made-up values; no Moog preset is in the project). Checks: the patches made, their
 * tags and sound, the message line, the report and REPORT, a file that isn't a preset, DUPLICATE NAMES. */
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
enum { DUP_RENAME_ = 2, DUP_IGNORE_ = 3 };   /* (parts.h DUP_) */

static int fails;
static AEffect *fx;
static HWND child;
static float S;
static const char *pics;
static int W, H;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}
static intptr_t lt(int op, float opt) {
    static char b[1024];
    b[0] = 0;
    return fx->dispatcher(fx, 50, TEST_LIB, op, b, opt);
}
static intptr_t pt(int op, const char *in, char *out, float opt) {
    static char b[1024];
    snprintf(b, sizeof b, "%s", in ? in : "");
    intptr_t r = fx->dispatcher(fx, 50, TEST_PATCH, op, b, opt);
    if (out) snprintf(out, 1024, "%s", b);
    return r;
}
static void click_px(int x, int y) {
    LPARAM at = MAKELPARAM(x, y);
    SendMessageA(child, WM_MOUSEMOVE, 0, at);
    SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, at);
    SendMessageA(child, WM_LBUTTONUP, 0, at);
}
static int click_zone(int z) {   /* a PATCHES page zone's middle (TEST_LIB LT_ZONE says where) */
    intptr_t v = lt(LT_ZONE, (float)z);
    if (v < 0) { printf("     (zone %d is not on the window now)\n", z); return 0; }
    click_px((int)(v & 0xFFFF), (int)(v >> 16));
    return 1;
}
static float realk(const char *k) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) {
            float v = fx->getParameter(fx, i);
            return PARAMS[i].nopts > 1 ? roundf(v * (PARAMS[i].nopts - 1)) : PARAMS[i].min + v * (PARAMS[i].max - PARAMS[i].min);
        }
    return -1e9f;
}
static void shot(const char *suffix) {
    if (!pics) return;
    char path[512];
    const char *dot = strrchr(pics, '.');
    snprintf(path, sizeof path, "%.*s%s.bmp", dot ? (int)(dot - pics) : (int)strlen(pics), pics, suffix);
    HDC sdc = GetDC(NULL), dc = CreateCompatibleDC(sdc);
    HBITMAP bm = CreateCompatibleBitmap(sdc, W, H);
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
    SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc); ReleaseDC(NULL, sdc);
}
static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *b = calloc(1, 4 << 20);
    if (b) fread(b, 1, (4 << 20) - 1, f);
    fclose(f);
    return b;
}

/* test/m1p/<name> as a full path: from the folder load_test runs in (the repo's root), else beside the .exe */
static int test_file(const char *name, char *out, int n) {
    char rel[MAX_PATH];
    snprintf(rel, sizeof rel, "ports\\one-ish\\test\\m1p\\%s", name);
    if (GetFullPathNameA(rel, n, out, NULL) && GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return 1;
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof exe);
    char *sl = strrchr(exe, '\\');
    if (sl) *sl = 0;
    snprintf(rel, sizeof rel, "%s\\..\\..\\test\\m1p\\%s", exe, name);
    return GetFullPathNameA(rel, n, out, NULL) && GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

int m1p_tests(void *effect, HWND window, float scale, const char *bmp, int w, int h) {
    fx = effect;
    child = window;
    S = scale;
    pics = bmp;
    W = w;
    H = h;
    fails = 0;
    char basic[MAX_PATH], loud[MAX_PATH], bad[MAX_PATH], pick[3 * MAX_PATH + 4], dir[1024], msg[1024], what[1400];
    if (!test_file("basic.txt", basic, sizeof basic) || !test_file("loud.txt", loud, sizeof loud) ||
        !test_file("malformed.txt", bad, sizeof bad)) {
        check(0, "m1p: the synthetic test files in ports/one-ish/test/m1p are found");
        return fails;
    }
    pt(PT_DIR, NULL, dir, 0);
    if (lt(LT_SCREEN, 0) != SCR_PATCHES)   /* the patch name on the centre screen opens the PATCHES page */
        click_px((int)lroundf((ZONE_SCREEN[0] + 200) * S), (int)lroundf((ZONE_SCREEN[1] + 23) * S));
    if (lt(LT_SCREEN, 0) != SCR_PATCHES)
        click_px((int)lroundf((ZONE_SCREEN[0] + 200) * S), (int)lroundf((ZONE_SCREEN[1] + 23) * S));
    check(lt(LT_SCREEN, 0) == SCR_PATCHES, "m1p: the PATCHES page is open");
    int before = (int)pt(PT_COUNT, NULL, NULL, 0);
    /* IMPORT .M1P: a plain click, three files picked (the dialog's place taken by the pick) */
    snprintf(pick, sizeof pick, "%s|%s|%s", basic, bad, loud);
    pt(PT_M1P_PICK, pick, NULL, 0);
    check(click_zone(Z_PATCH_ACT + PA_M1P), "m1p: IMPORT .M1P is on the PATCHES page");
    int after = (int)pt(PT_COUNT, NULL, NULL, 0);
    snprintf(what, sizeof what, "m1p: two of the three files became user patches (%d -> %d)", before, after);
    check(after == before + 2, what);
    pt(PT_MESSAGE, NULL, msg, 0);
    snprintf(what, sizeof what, "m1p: the message line sums it up: \"%s\"", msg);
    check(strstr(msg, "Imported 2 of 3") && strstr(msg, "REPORT"), what);
    shot("_m1p_import");
    char path[MAX_PATH + 64];
    snprintf(path, sizeof path, "%s\\Test Brassy.oneish", dir);
    char *t = slurp(path);
    check(t && strstr(t, "ONE-ish patch 2\nname=Test Brassy\n") && strstr(t, "\ntype=Single\n") &&
          strstr(t, "\ncategory=Brass\n") && strstr(t, "\nmood=Bright\n") && strstr(t, "\ngroup=Imported\n") &&
          strstr(t, "\nnotes=Imported from a Moog One preset file.") && strstr(t, "\nflt_mode=1\n"),
          "m1p: 'Test Brassy' is a patch file with the preset's name and tags, GROUP Imported, notes and its sound");
    free(t);
    /* the report, and REPORT (with a pick set it only notes what it would open) */
    char rp[1024];
    check(pt(PT_M1P_REPORT, NULL, rp, 0) == 1, "m1p: the import report was written beside the patches");
    t = slurp(rp);
    check(t && strstr(t, "malformed.txt ==\r\nNot imported: it isn't a Moog One preset") &&
          strstr(t, "Imported as the patch 'Test Brassy'") && strstr(t, "/p/part/t/1/p/seq/made_up_key = 3") &&
          strstr(t, "Level check: too loud, so Volume was turned down"),
          "m1p: the report says what imported, why one didn't, what didn't map and the level check");
    free(t);
    check(click_zone(Z_PATCH_ACT + PA_REPORT), "m1p: REPORT is on the PATCHES page");
    char op[1024];
    check(pt(PT_M1P_OPENED, NULL, op, 0) == 1 && !_stricmp(op, rp), "m1p: REPORT opens the report");
    /* the sound: loaded, it plays what the file said */
    check(pt(PT_LOAD, "Test Brassy", NULL, 0) == PE_OK, "m1p: the imported patch loads");
    snprintf(what, sizeof what, "m1p: loaded, its Osc 1 is Tri at 16', its ladder HP, cutoff %.2f (want 64.83)",
             realk("flt_cut"));
    check(realk("o1_wave") == 0 && realk("o1_oct") == 1 && realk("flt_mode") == 1 && fabsf(realk("flt_cut") - 64.83f) < 0.2f,
          what);
    /* DUPLICATE NAMES: AUTO RENAME makes "Test Brassy 2"; IGNORE leaves it */
    check(pt(PT_M1P_IMPORT, basic, NULL, DUP_RENAME_) == 1, "m1p: imported again with AUTO RENAME");
    snprintf(path, sizeof path, "%s\\Test Brassy 2.oneish", dir);
    check(GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES, "m1p: ... as 'Test Brassy 2'");
    int n2 = (int)pt(PT_COUNT, NULL, NULL, 0);
    check(pt(PT_M1P_IMPORT, basic, NULL, DUP_IGNORE_) == 0 && pt(PT_COUNT, NULL, NULL, 0) == n2,
          "m1p: with IGNORE nothing is imported over it");
    pt(PT_M1P_PICK, "", NULL, 0);
    /* tidy up: the patches made here and the report go (the later tests count the patches) */
    static const char *const MADE[] = {"Test Brassy", "Test Brassy 2", "Test Loud"};
    for (unsigned k = 0; k < sizeof MADE / sizeof MADE[0]; k++) {
        snprintf(path, sizeof path, "%s\\%s.oneish", dir, MADE[k]);
        DeleteFileA(path);
    }
    DeleteFileA(rp);
    pt(PT_COUNT, NULL, NULL, 0);
    return fails;
}
