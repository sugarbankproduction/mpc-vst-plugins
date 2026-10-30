/* load_test's checks of parts.c (2026-10-30): effect presets, matrix presets, the Synths' names, user libraries and
 * DUPLICATE NAMES, driven by plain clicks on the window (PRESETS, the PARTS page's buttons and list, the SETUP page's
 * heads, the SETTINGS page's LIBRARY rows) with the dialogs answered by a helper thread. TEST_PARTS / TEST_LIB /
 * TEST_PATCH only read back what happened and where a button is. Pictures of the new pages with the editor's. */
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
#include "parts.h"

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
enum { T_SCR_PARTS = -17, T_SCR_SETUP = -11, T_SCR_SETTINGS = -20, T_ST_LIBRARY = 2 };   /* as parts.h, editor.h, settings.h */
enum { Z_SUB_NEXT_ = 100014 };   /* (editor.h Z_SUB_NEXT: the screen's next sub-page) */
#define TEST_SETTINGS_ 0x4F315354
enum { STT_TAB_ = 8 };   /* (settings.h STT_TAB) */

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
static intptr_t pa(int op, float opt, char *buf) {
    static char b[1024];
    if (!buf) { b[0] = 0; buf = b; }
    return fx->dispatcher(fx, 50, TEST_PARTS, op, buf, opt);
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
static int key(const char *k) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) return i;
    return -1;
}
static float getk(const char *k) { int i = key(k); return i >= 0 ? fx->getParameter(fx, i) : -1; }
static void setk(const char *k, float v) { int i = key(k); if (i >= 0) fx->setParameter(fx, i, v); }
static float realk(const char *k) {
    int i = key(k);
    return i < 0 ? -1e9f : PARAMS[i].min + fx->getParameter(fx, i) * (PARAMS[i].max - PARAMS[i].min);
}
static const char *optk(const char *k) {
    int i = key(k);
    if (i < 0 || PARAMS[i].nopts < 2) return "";
    return PARAMS[i].opts[(int)lroundf(fx->getParameter(fx, i) * (PARAMS[i].nopts - 1))];
}
static void set_opt(const char *k, const char *label) {
    int i = key(k);
    for (int o = 0; i >= 0 && o < PARAMS[i].nopts; o++)
        if (!strcmp(PARAMS[i].opts[o], label)) fx->setParameter(fx, i, o / (float)(PARAMS[i].nopts - 1));
}
static int steps(void) { return (int)(lt(LT_STEPS, 0) % 1000); }
static void tick(void) { SendMessageA(child, WM_TIMER, 1, 0); }
static int screen(void) { return (int)lt(LT_SCREEN, 0); }

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

/* ---- clicks: on a zone's middle (TEST_PARTS / TEST_LIB tell where), on the panel in base units ---- */
static void click_px(int x, int y) {
    LPARAM at = MAKELPARAM(x, y);
    SendMessageA(child, WM_MOUSEMOVE, 0, at);
    SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, at);
    SendMessageA(child, WM_LBUTTONUP, 0, at);
}
static int click_at(intptr_t v, int z) {
    if (v < 0) { printf("     (zone %d is not on the window now)\n", z); return 0; }
    click_px((int)(v & 0xFFFF), (int)(v >> 16));
    return 1;
}
static int click_p(int z) { return click_at(pa(PAT_ZONE, (float)z, NULL), z); }   /* a parts.c zone */
static int click_l(int z) { return click_at(lt(LT_ZONE, (float)z), z); }           /* a library zone */
static void click_base(const short zz[4]) { click_px((int)lroundf((zz[0] + zz[2] / 2.0f) * S), (int)lroundf((zz[1] + zz[3] / 2.0f) * S)); }
static int click_more(int page) {
    for (int k = 0; k < NMORES; k++)
        if (MORES[k].page == page && page >= 0) {
            click_px((int)lroundf((MORES[k].x + MORES[k].w * 0.7f) * S), (int)lroundf((MORES[k].y + MORES[k].h * 0.3f) * S));
            return 1;
        }
    return 0;
}
static void ctrl_key(int vk) {
    BYTE ks[256], was[256];
    GetKeyboardState(was);
    memcpy(ks, was, sizeof ks);
    ks[VK_CONTROL] = 0x80;
    ks[VK_SHIFT] = 0;
    SetKeyboardState(ks);
    SendMessageA(child, WM_KEYDOWN, vk, 0);
    SetKeyboardState(was);
}

/* ---- dialogs answered in turn by a helper thread: a box's title, the text typed into it, the button ---- */
typedef struct { const char *title, *text; int button; } ans_t;
/* a visible box of this process with this title (other programs' boxes, other tests' among them, are not ours) */
typedef struct { const char *title; HWND found; } find_t;
static BOOL CALLBACK find_one(HWND w, LPARAM lp) {
    find_t *f = (find_t *)lp;
    DWORD pid = 0;
    char cls[16], t[128];
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(w)) return TRUE;
    GetClassNameA(w, cls, sizeof cls);
    GetWindowTextA(w, t, sizeof t);
    if (strcmp(cls, "#32770") || strcmp(t, f->title)) return TRUE;
    f->found = w;
    return FALSE;
}
static HWND find_box(const char *title) {
    find_t f = {title, NULL};
    EnumWindows(find_one, (LPARAM)&f);
    return f.found;
}
typedef struct { const ans_t *a; int n, done; } seq_t;
static DWORD WINAPI answer(void *arg) {
    seq_t *q = arg;
    for (int k = 0; k < q->n; k++) {
        int found = 0;
        for (int t = 0; t < 1000 && !found; t++, Sleep(10)) {
            HWND d = find_box(q->a[k].title);
            if (!d) continue;
            if (q->a[k].text) {
                char got[1200] = "";
                SetDlgItemTextA(d, 101, q->a[k].text);
                GetDlgItemTextA(d, 101, got, sizeof got);
                if (strcmp(got, q->a[k].text)) continue;
            }
            Sleep(150);   /* (the box ready for its buttons) */
            PostMessageA(d, WM_COMMAND, MAKEWPARAM(q->a[k].button, BN_CLICKED), 0);
            found = 1;
            for (int w = 0; w < 200 && IsWindow(d) && IsWindowVisible(d); w++) Sleep(10);   /* (it closes) */
        }
        if (!found) { printf("     (no \"%s\" window appeared)\n", q->a[k].title); return 0; }
        q->done++;
    }
    return 1;
}
/* a click on a parts.c zone (lib 0) or a library zone (lib 1) whose dialogs the thread answers; 1: all answered */
static int click_answered(int z, int lib, const ans_t *a, int n) {
    seq_t q = {a, n, 0};
    HANDLE th = CreateThread(NULL, 0, answer, &q, 0, NULL);
    int ok = lib ? click_l(z) : click_p(z);
    WaitForSingleObject(th, 15000);
    CloseHandle(th);
    return ok && q.done == n;
}

static int find_item(const char *name) {   /* the PARTS list's entry with this name (-1: none) */
    char b[1024];
    int n = (int)pa(PAT_COUNT, 0, NULL);
    for (int k = 0; k < n; k++) {
        pa(PAT_ITEM, (float)k, b);
        size_t l = strlen(name);
        if (!strncmp(b, name, l) && b[l] == '|') return k;
    }
    return -1;
}
static int select_item(const char *name) {   /* clicked in the list (its page shown first with the sub-page arrows) */
    int k = find_item(name);
    if (k < 0) { printf("     (no preset '%s' in the list)\n", name); return -1; }
    for (int tries = 0; tries < 20 && pa(PAT_ZONE, (float)(ZP_ITEM + k), NULL) < 0; tries++)
        click_at(pa(PAT_ZONE, (float)Z_SUB_NEXT_, NULL), Z_SUB_NEXT_);   /* the page's > (round to the first again) */
    click_p(ZP_ITEM + k);
    return (int)pa(PAT_SEL, 0, NULL) == k ? k : -1;
}
static int file_in(const char *dir, const char *name) {
    char p[MAX_PATH + 64];
    snprintf(p, sizeof p, "%s\\%s", dir, name);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}
static char *slurp(const char *dir, const char *name) {
    char p[MAX_PATH + 64];
    snprintf(p, sizeof p, "%s\\%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    static char b[65536];
    size_t n = fread(b, 1, sizeof b - 1, f);
    fclose(f);
    b[n] = 0;
    return b;
}
static const char *state_now(void) {
    void *chunk = NULL;
    intptr_t n = fx->dispatcher(fx, 23, 0, 0, &chunk, 0);
    return n > 0 && chunk ? (const char *)chunk : "";
}

/* ---- effect presets ---- */
static void effect_tests(void) {
    char what[400], dir[1024];
    pt(PT_LOAD_POS, NULL, NULL, 0);   /* Init */
    set_opt("modfx_type", "Chorus");
    setk("modfx_on", 1.0f);
    setk("modfx_rate", 0.3f);
    setk("modfx_depth", 0.8f);
    setk("modfx_mix", 0.4f);
    float rate = getk("modfx_rate"), depth = getk("modfx_depth"), mix = getk("modfx_mix");
    tick();
    int opened = click_more(PG_SFX);
    shot("_more_sfx_presets");
    opened = opened && click_p(ZP_MORE);
    snprintf(what, sizeof what, "EFFECT PRESETS: PRESETS on the SYNTH EFFECT page opens the PARTS page on SYNTH FX (screen %d, slot %d)",
             screen(), (int)pa(PAT_SLOT, 0, NULL));
    check(opened && screen() == T_SCR_PARTS && pa(PAT_SLOT, 0, NULL) == PS_SFX, what);
    check(find_item("Slow Wide Chorus") >= 0 && find_item("Big Hall") < 0,
          "EFFECT PRESETS: the list shows this type's presets (Moo Won's own Slow Wide Chorus, not Big Hall)");
    static const ans_t SAVE[] = {{"Save effect preset", "My Chorus", IDOK}, {"Notes", "Wide and slow", IDOK}};
    int saved = click_answered(ZP_ACT + 0, 0, SAVE, 2);
    pa(PAT_DIR, 0, dir);
    const char *t = slurp(dir, "My Chorus.oneishfx");
    snprintf(what, sizeof what, "EFFECT PRESETS: SAVE (a name, then notes) writes Effects\\My Chorus.oneishfx with its type, "
             "notes, time and values (%s)", dir);
    check(saved && t && strstr(t, "ONE-ish effect 1") && strstr(t, "type=Chorus") && strstr(t, "notes=Wide and slow") &&
          strstr(t, "time=20") && strstr(t, "\nrate=") && !strstr(t, "\non="), what);
    setk("modfx_rate", 0.9f);
    setk("modfx_depth", 0.1f);
    setk("modfx_mix", 0.9f);
    set_opt("modfx_type", "Flanger");
    pa(PAT_COUNT, 0, NULL);
    check(find_item("My Chorus") < 0, "EFFECT PRESETS: another type shown: My Chorus is not listed");
    click_p(ZP_ACT + 5);   /* ALL TYPES */
    int s0 = steps(), sel = select_item("My Chorus");
    click_p(ZP_ACT + 1);   /* LOAD */
    snprintf(what, sizeof what, "EFFECT PRESETS: ALL TYPES, a click on My Chorus, LOAD: the type and every value back "
             "(rate %.4f/%.4f, depth %.4f/%.4f, mix %.4f/%.4f), ON untouched, one undo step (%d -> %d)",
             getk("modfx_rate"), rate, getk("modfx_depth"), depth, getk("modfx_mix"), mix, s0, steps());
    check(sel >= 0 && !strcmp(optk("modfx_type"), "Chorus") && fabsf(getk("modfx_rate") - rate) < 1e-4f &&
          fabsf(getk("modfx_depth") - depth) < 1e-4f && fabsf(getk("modfx_mix") - mix) < 1e-4f && getk("modfx_on") > 0.5f &&
          steps() == s0 + 1, what);
    ctrl_key('Z');
    check(!strcmp(optk("modfx_type"), "Flanger") && fabsf(getk("modfx_rate") - 0.9f) < 1e-6f,
          "EFFECT PRESETS: Ctrl+Z takes the whole LOAD back (Flanger, its rate)");
    ctrl_key('Y');
    s0 = steps();
    click_p(ZP_ACT + 2);   /* INIT */
    int ri = key("modfx_rate");
    snprintf(what, sizeof what, "EFFECT PRESETS: INIT: the controls to their defaults (rate %.3f, default %.3f), the type "
             "(%s) and ON kept, one undo step", getk("modfx_rate"), ri >= 0 ? PARAMS[ri].def : -1, optk("modfx_type"));
    check(ri >= 0 && fabsf(getk("modfx_rate") - PARAMS[ri].def) < 1e-6f && !strcmp(optk("modfx_type"), "Chorus") &&
          getk("modfx_on") > 0.5f && steps() == s0 + 1, what);
    /* the preset into MASTER FX 1 (it has Chorus too), and a factory reverb into MASTER FX 2 */
    click_p(ZP_SLOT + PS_FX1);
    sel = select_item("My Chorus");
    float send = getk("mfx_send1");
    click_p(ZP_ACT + 1);
    snprintf(what, sizeof what, "EFFECT PRESETS: a Synth effect's preset loads into MASTER FX 1 (type %s, rate %.4f), its "
             "send untouched", optk("dly_type"), getk("dly_rate"));
    check(sel >= 0 && !strcmp(optk("dly_type"), "Chorus") && fabsf(getk("dly_rate") - rate) < 1e-4f && getk("mfx_send1") == send,
          what);
    click_p(ZP_SLOT + PS_FX2);
    sel = select_item("Big Hall");
    click_p(ZP_ACT + 1);
    snprintf(what, sizeof what, "EFFECT PRESETS: Moo Won's own Big Hall into MASTER FX 2 (type %s, decay %.1f, size %.1f)",
             optk("rev_type"), realk("rev_decay"), realk("rev_size"));
    check(sel >= 0 && !strcmp(optk("rev_type"), "Hall") && fabsf(realk("rev_decay") - 75) < 0.01f &&
          fabsf(realk("rev_size") - 85) < 0.01f, what);
    click_p(ZP_SLOT + PS_SFX);
    click_p(ZP_ACT + 5);   /* (ALL TYPES off again) */
    select_item("My Chorus");
    shot("_parts_fx");
    static const ans_t NOTES[] = {{"Notes", "Changed note", IDOK}};
    check(click_answered(ZP_ACT + 3, 0, NOTES, 1) && (t = slurp(dir, "My Chorus.oneishfx")) && strstr(t, "notes=Changed note"),
          "EFFECT PRESETS: NOTES changes the selected preset's notes in its file");
    /* every factory preset loads into a slot that has its type */
    int bad = 0;
    click_p(ZP_SLOT + PS_FX1);
    click_p(ZP_ACT + 5);   /* every type */
    int n = (int)pa(PAT_COUNT, 0, NULL), nf = 0;
    char b[1024];
    for (int k = 0; k < n; k++) {
        pa(PAT_ITEM, (float)k, b);
        if (!strstr(b, "|F|")) continue;
        nf++;
        char nm[64];
        snprintf(nm, sizeof nm, "%.*s", (int)strcspn(b, "|"), b);
        const char *ty = strchr(b, '|') + 1;
        if (select_item(nm) < 0) { bad++; continue; }
        click_p(ZP_ACT + 1);
        if (strncmp(optk("dly_type"), ty, strlen(optk("dly_type"))) || ty[strlen(optk("dly_type"))] != '|') bad++;
    }
    click_p(ZP_ACT + 5);
    snprintf(what, sizeof what, "EFFECT PRESETS: all %d of Moo Won's own load into MASTER FX 1 with their type (%d wrong)", nf, bad);
    check(nf >= 12 && !bad, what);
    pt(PT_LOAD_POS, NULL, NULL, 0);
}

/* ---- matrix presets ---- */
static void mx_snap(float *v) {
    static const char *const F[] = {"src", "dst", "amt", "via", "via_amt", "xf", "xf_amt", "on"};
    for (int r = 0; r < 16; r++)
        for (int f = 0; f < 8; f++) {
            char k[32];
            snprintf(k, sizeof k, "mod%d_%s", r + 1, F[f]);
            v[r * 8 + f] = getk(k);
        }
}
static int mx_used(void) {
    int n = 0;
    for (int r = 0; r < 16; r++) {
        char a[32], b[32];
        snprintf(a, sizeof a, "mod%d_src", r + 1);
        snprintf(b, sizeof b, "mod%d_via", r + 1);
        n += getk(a) > 0.001f || getk(b) > 0.001f;
    }
    return n;
}
static void matrix_tests(void) {
    char what[400], dir[1024], b[1024];
    static float orig[128], now[128];
    pt(PT_LOAD_POS, NULL, NULL, 0);
    set_opt("mod5_src", "LFO 2");
    set_opt("mod5_dst", "Pitch");
    setk("mod5_amt", 0.6f);
    int used = mx_used();
    mx_snap(orig);
    click_p(ZP_SLOT + PS_MTX);
    static const ans_t SAVE[] = {{"Save matrix preset", "Test Matrix", IDOK}, {"Notes", "Five rows", IDOK}};
    int saved = click_answered(ZP_ACT + 0, 0, SAVE, 2);
    pa(PAT_DIR, 1, dir);
    const char *t = slurp(dir, "Test Matrix.oneishmtx");
    int rows = 0;
    for (const char *s = t; s && (s = strstr(s, "[row]")); s += 5) rows++;
    int k = find_item("Test Matrix");
    if (k >= 0) pa(PAT_ITEM, (float)k, b);
    snprintf(what, sizeof what, "MATRIX PRESETS: SAVE writes Matrices\\Test Matrix.oneishmtx with the %d rows in use (%d), "
             "notes and the time (\"%s\")", used, rows, b);
    check(saved && t && rows == used && used >= 2 && strstr(t, "notes=Five rows") && strstr(t, "src=LFO 2") && k >= 0 &&
          strstr(b, "|20") && strstr(b, "Five rows"), what);
    /* REPLACE puts it back exactly, an emptied extra row too */
    setk("mod1_amt", 0.9f);
    set_opt("mod9_src", "Velocity");
    set_opt("mod9_dst", "Pitch");
    set_opt("mod5_src", "Off");
    int s0 = steps(), sel = select_item("Test Matrix");
    click_p(ZP_ACT + 7);   /* REPLACE */
    mx_snap(now);
    int diff = -1;
    for (int j = 0; j < 128 && diff < 0; j++)   /* (the rows in use; the free ones are emptied) */
        if ((orig[(j / 8) * 8] > 0.001f || orig[(j / 8) * 8 + 3] > 0.001f) && fabsf(now[j] - orig[j]) > 1e-4f) diff = j;
    snprintf(what, sizeof what, "MATRIX PRESETS: a click on it, REPLACE: every row as saved, the extra row emptied "
             "(first difference %d, rows in use %d), one undo step (%d -> %d)", diff, mx_used(), s0, steps());
    check(sel >= 0 && diff < 0 && mx_used() == used && getk("mod9_src") == 0.0f && steps() == s0 + 1, what);
    s0 = steps();
    click_p(ZP_ACT + 6);   /* ADD */
    int want = 2 * used > 16 ? 16 : 2 * used, after = mx_used();
    snprintf(what, sizeof what, "MATRIX PRESETS: ADD puts its %d rows in the free rows (%d -> %d in use), the rows there kept, "
             "one undo step", used, used, after);
    mx_snap(now);
    int kept = 1;
    for (int j = 0; j < 128; j++)
        if (orig[(j / 8) * 8] > 0.001f && fabsf(now[j] - orig[j]) > 1e-4f) kept = 0;
    check(after == want && kept && steps() == s0 + 1, what);
    ctrl_key('Z');
    check(mx_used() == used, "MATRIX PRESETS: Ctrl+Z takes the ADD back");
    shot("_parts_matrix");
    static const ans_t YES[] = {{"Moo Won", NULL, IDYES}};
    select_item("Test Matrix");
    check(click_answered(ZP_ACT + 4, 0, YES, 1) && !file_in(dir, "Test Matrix.oneishmtx") && find_item("Test Matrix") < 0,
          "MATRIX PRESETS: DELETE (asks first) removes the file");
    pt(PT_LOAD_POS, NULL, NULL, 0);
}

/* ---- the Synths' names ---- */
static int name_is(int c, const char *want) {
    char b[1024];
    pa(PAT_NAME, (float)c, b);
    if (strcmp(b, want)) printf("     Synth %d is named \"%s\", expected \"%s\"\n", c + 1, b, want);
    return !strcmp(b, want);
}
static void name_tests(void) {
    char what[400];
    pt(PT_LOAD_POS, NULL, NULL, 0);
    check(name_is(0, "") && name_is(1, "") && !strstr(state_now(), "syn_name"),
          "NAMES: a factory preset has no names, and its state no syn_name (old sounds: unchanged)");
    click_base(ZONE_TAB_SETUP);
    int s0 = steps();
    static const ans_t N2[] = {{"Name Synth 2", "Pad", IDOK}};
    int ok = screen() == T_SCR_SETUP && click_answered(ZP_NAME + 1, 0, N2, 1);
    snprintf(what, sizeof what, "NAMES: on SETUP a click on SYNTH 2's head names it 'Pad' (in the state: %s), one undo step "
             "(%d -> %d)", strstr(state_now(), ";s2_syn_name=Pad") ? "yes" : "NO", s0, steps());
    check(ok && name_is(1, "Pad") && strstr(state_now(), ";s2_syn_name=Pad") && steps() == s0 + 1, what);
    ctrl_key('Z');
    check(name_is(1, ""), "NAMES: Ctrl+Z takes the name back");
    ctrl_key('Y');
    check(name_is(1, "Pad"), "NAMES: Ctrl+Y brings it again");
    static const ans_t N1[] = {{"Name Synth 1", "Bass", IDOK}};
    click_answered(ZP_NAME + 0, 0, N1, 1);
    static const ans_t LONG[] = {{"Name Synth 3", "A Name Too Long", IDOK}, {"Name Synth 3", NULL, IDCANCEL}};
    click_answered(ZP_NAME + 2, 0, LONG, 2);
    check(name_is(0, "Bass") && name_is(2, ""), "NAMES: Synth 1 'Bass'; a name over 12 characters is refused (the box says why)");
    static const ans_t N3[] = {{"Name Synth 3", "Strings Lead", IDOK}};
    check(click_answered(ZP_NAME + 2, 0, N3, 1) && name_is(2, "Strings Lead"), "NAMES: a name of 12 characters is taken");
    tick();
    shot("_setup_names");
    lt(LT_HOME, 0);
    {
        static char hb[1024];
        fx->dispatcher(fx, 50, TEST_LIB, LT_HOME, hb, 0);
        snprintf(what, sizeof what, "NAMES: the home line shows them (\"%s\")", hb);
        check(strstr(hb, "S1 Bass") && strstr(hb, "S2 Pad"), what);
    }
    /* saved with a patch and a project; a factory preset has none */
    check(pt(PT_SAVE_AS, "Named Sound", NULL, 1) == PE_OK, "NAMES: SAVE AS 'Named Sound'");
    const char *f = slurp(folder, "Named Sound.oneish");
    check(f && strstr(f, "\nsyn_name=Bass") && strstr(f, "\ns2_syn_name=Pad"), "NAMES: the patch file holds both names");
    static char proj[65536];
    snprintf(proj, sizeof proj, "%s", state_now());
    pt(PT_LOAD_POS, NULL, NULL, 0);
    check(name_is(0, "") && name_is(1, ""), "NAMES: loading a factory preset: no names");
    pt(PT_LOAD, "Named Sound", NULL, 0);
    check(name_is(0, "Bass") && name_is(1, "Pad"), "NAMES: loading the patch brings them back");
    pt(PT_LOAD_POS, NULL, NULL, 0);
    fx->dispatcher(fx, 24, 0, (intptr_t)strlen(proj) + 1, proj, 0);   /* effSetChunk: the host restores the project */
    check(name_is(0, "Bass") && name_is(1, "Pad"), "NAMES: a project (the host's chunk) restores them");
    /* SAVE S2: Synth 2's sound alone keeps its name, as Synth 1's key */
    fx->dispatcher(fx, 50, 0x4F314643 /* TEST_FOCUS */, 2, NULL, 1.0f);
    check(pt(PT_SAVE_SYNTH, "Pad Alone", NULL, 1) == PE_OK && (f = slurp(folder, "Pad Alone.oneish")) &&
          strstr(f, "\nsyn_name=Pad") && !strstr(f, "s2_syn_name"), "NAMES: SAVE S2 keeps Synth 2's name with its sound");
    fx->dispatcher(fx, 50, 0x4F314643, 1, NULL, 0.0f);
    pt(PT_LOAD_POS, NULL, NULL, 0);
    char p1[MAX_PATH + 64], p2[MAX_PATH + 64];
    snprintf(p1, sizeof p1, "%s\\Named Sound.oneish", folder);
    snprintf(p2, sizeof p2, "%s\\Pad Alone.oneish", folder);
    DeleteFileA(p1);
    DeleteFileA(p2);
    pt(PT_COUNT, NULL, NULL, 0);
}

/* ---- user libraries and DUPLICATE NAMES ---- */
static int count(void) { return (int)pt(PT_COUNT, NULL, NULL, 0); }
static void library_tests2(void) {
    char what[400], b[1024], dir[1024], d0[1024];
    fx->dispatcher(fx, 50, TEST_SETTINGS_, STT_TAB_, b, (float)T_ST_LIBRARY);
    int n0 = count();
    pt(PT_DIR, NULL, d0, 0);
    static const ans_t NEW[] = {{"New empty library", "Empty Lib", IDOK}};
    int ok = screen() == T_SCR_SETTINGS && click_answered(ZP_LIB + 2, 0, NEW, 1);
    pa(PAT_SPACE, 0, b);
    pt(PT_DIR, NULL, dir, 0);
    snprintf(what, sizeof what, "LIBRARIES: SETTINGS > LIBRARY, NEW EMPTY 'Empty Lib': in use, no patches (%d), its own "
             "folder (%s)", count(), dir);
    check(ok && !strcmp(b, "Empty Lib") && count() == 0 && strstr(dir, "Empty Lib"), what);
    shot("_settings_library_new");
    click_p(ZP_LIB + 0);   /* < : back to Default */
    pa(PAT_SPACE, 0, b);
    pt(PT_DIR, NULL, dir, 0);
    snprintf(what, sizeof what, "LIBRARIES: < goes back to the Default library: the patches as before (%d of %d), the same "
             "folder", count(), n0);
    check(!b[0] && count() == n0 && !strcmp(dir, d0), what);
    static const ans_t NEWF[] = {{"New library with the factory sounds", "Factory Lib", IDOK}};
    ok = click_answered(ZP_LIB + 3, 0, NEWF, 1);
    pa(PAT_SPACE, 0, b);
    snprintf(what, sizeof what, "LIBRARIES: NEW + FACTORY 'Factory Lib': every factory sound as a patch of its own (%d of %d)",
             count(), fx->numPrograms);
    check(ok && !strcmp(b, "Factory Lib") && count() == fx->numPrograms, what);
    static const ans_t REN[] = {{"Rename library", "Factory Copy", IDOK}};
    ok = click_answered(ZP_LIB + 4, 0, REN, 1);
    pa(PAT_SPACE, 0, b);
    int nl = (int)pa(PAT_SPACES, 0, dir);
    snprintf(what, sizeof what, "LIBRARIES: RENAME 'Factory Copy' (libraries: %s)", dir);
    check(ok && !strcmp(b, "Factory Copy") && nl == 3 && !strcmp(dir, "Default|Empty Lib|Factory Copy") && count() == fx->numPrograms,
          what);
    click_p(ZP_LIB + 1);   /* > : Default (from the last one) */
    pa(PAT_SPACE, 0, b);
    check(!b[0] && count() == n0, "LIBRARIES: > from the last library goes round to Default");
    /* COPY SOUND TO, with each DUPLICATE NAMES choice */
    check(pt(PT_LOAD, "Bright Pad", NULL, 0) == PE_OK, "LIBRARIES: Bright Pad loaded in Default");
    pa(PAT_LIBDIR, 1, dir);   /* Empty Lib's folder */
    pa(PAT_ANSWER, 1, NULL);
    click_p(ZP_LIB + 5);
    check(file_in(dir, "Bright Pad.oneish") && count() == n0, "LIBRARIES: COPY SOUND TO 'Empty Lib' puts Bright Pad there "
                                                               "(Default unchanged)");
    while (pa(PAT_DUP, 0, NULL) != DUP_RENAME) click_p(ZP_LIB + 7);   /* the DUPLICATE NAMES > */
    pa(PAT_ANSWER, 1, NULL);
    click_p(ZP_LIB + 5);
    check(file_in(dir, "Bright Pad 2.oneish"), "DUPLICATE NAMES: AUTO RENAME (clicked with >): a second copy is 'Bright Pad 2'");
    click_p(ZP_LIB + 7);   /* IGNORE */
    pa(PAT_ANSWER, 1, NULL);
    click_p(ZP_LIB + 5);
    check(pa(PAT_DUP, 0, NULL) == DUP_IGNORE && !file_in(dir, "Bright Pad 3.oneish"), "DUPLICATE NAMES: IGNORE: nothing copied");
    shot("_settings_library");
    /* IMPORT with each choice: Empty Lib's Bright Pad into Default, which has one */
    char src[1100];
    snprintf(src, sizeof src, "%s\\Bright Pad.oneish", dir);
    snprintf(b, sizeof b, "%s", src);
    int r = (int)pa(PAT_IMPORT, 0, b);
    check(r == PE_EXISTS && count() == n0, "DUPLICATE NAMES: IGNORE: IMPORT of a same-named patch imports nothing");
    click_p(ZP_LIB + 7);   /* ASK (round from IGNORE) */
    static const ans_t NO[] = {{"Moo Won", NULL, IDNO}};
    seq_t q = {NO, 1, 0};
    HANDLE th = CreateThread(NULL, 0, answer, &q, 0, NULL);
    snprintf(b, sizeof b, "%s", src);
    r = (int)pa(PAT_IMPORT, 0, b);
    WaitForSingleObject(th, 15000);
    CloseHandle(th);
    snprintf(what, sizeof what, "DUPLICATE NAMES: ASK (as before): it asks (%d); No imports nothing (%d, %d of %d patches, "
             "choice %d)", q.done, r, count(), n0, (int)pa(PAT_DUP, 0, NULL));
    check(pa(PAT_DUP, 0, NULL) == DUP_ASK && q.done == 1 && r == PE_EXISTS && count() == n0, what);
    click_p(ZP_LIB + 7);   /* OVERWRITE */
    snprintf(b, sizeof b, "%s", src);
    r = (int)pa(PAT_IMPORT, 0, b);
    check(pa(PAT_DUP, 0, NULL) == DUP_OVERWRITE && r == PE_OK && count() == n0, "DUPLICATE NAMES: OVERWRITE: replaced, no question");
    click_p(ZP_LIB + 7);   /* AUTO RENAME */
    snprintf(b, sizeof b, "%s", src);
    r = (int)pa(PAT_IMPORT, 0, b);
    check(r == PE_OK && count() == n0 + 1 && file_in(d0, "Bright Pad 2.oneish"), "DUPLICATE NAMES: AUTO RENAME: imported as "
                                                                                 "'Bright Pad 2'");
    char p[MAX_PATH + 64];
    snprintf(p, sizeof p, "%s\\Bright Pad 2.oneish", d0);
    DeleteFileA(p);
    while (pa(PAT_DUP, 0, NULL) != DUP_ASK) click_p(ZP_LIB + 6);   /* back to ASK with < */
    check(count() == n0 && pa(PAT_DUP, 0, NULL) == DUP_ASK, "LIBRARIES: Default as it was; DUPLICATE NAMES back to ASK");
    lt(LT_FORGET, 0);
}

int parts_tests(void *effect, HWND window, float scale, const char *bmp, int w, int h, const char *patch_folder) {
    fx = effect;
    child = window;
    S = scale;
    pics = bmp;
    W = w;
    H = h;
    folder = patch_folder;
    fails = 0;
    lt(LT_FORGET, 0);   /* (an empty undo history: the steps counted below stay under its 100) */
    effect_tests();
    matrix_tests();
    name_tests();
    library_tests2();
    click_base(ZONE_TAB_MATRIX);
    return fails;
}
