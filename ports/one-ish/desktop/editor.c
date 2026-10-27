/* Desktop editor window for ONE-ish (Windows VST2 build only; the MPC uses its own touchscreen skin).
 * Laid out like the instrument's front panel: a left panel (LFOs, pitch modulation, the three oscillators, voices),
 * a centre console with a screen (presets, the mod matrix, help for the control under the mouse) and a right panel
 * (mixer, filters, envelopes, effects, output), between wooden end cheeks. Original artwork: no maker's names or marks.
 *
 * It sits in front of the shared wrapper: wrapper/vst2_wrap.c is compiled with -DVSTPluginMain=one_ish_wrapped_main,
 * and this file's VSTPluginMain adds the editor opcodes and passes everything else on. Panels and their controls come
 * from params.json's "sections" (build/desktop/sections.h), so a new parameter in a section appears here by itself.
 * The editor only talks to the plugin through the VST2 calls a host would make (setParameter, getParameter,
 * effGetParamDisplay, programs), and tells the host about every change (audioMasterAutomate). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "params.h"
#include "sections.h"

typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
typedef intptr_t (*dispatcher_t)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    dispatcher_t dispatcher;
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
typedef struct { int16_t top, left, bottom, right; } ERect;

enum { effSetProgram = 2, effGetProgram = 3, effGetParamLabel = 6, effGetParamDisplay = 7, effEditGetRect = 13,
       effEditOpen = 14, effEditClose = 15, effEditIdle = 19, effGetProgramNameIndexed = 29 };
enum { audioMasterAutomate = 0, audioMasterUpdateDisplay = 42, audioMasterBeginEdit = 43, audioMasterEndEdit = 44 };
#define effFlagsHasEditor 1

AEffect *one_ish_wrapped_main(audioMasterCallback host);   /* wrapper/vst2_wrap.c's VSTPluginMain, renamed */

/* ---- geometry ---- */
enum { CHEEK = 26, GAP = 10, PAD = 7, TITLE_H = 18, CELL_W = 56, CELL_H = 74, KNOB_D = 34, MARGIN = 12 };
enum { LEFT_W = 500, CENTER_W = 540, RIGHT_W = 966, HELP_H = 44, ROW_H = 24, PRESET_H = 46 };
#define WIN_W (CHEEK * 2 + MARGIN * 2 + LEFT_W + CENTER_W + RIGHT_W + GAP * 2)
enum { K_NONE, K_KNOB, K_MENU, K_TOGGLE, K_AMOUNT, K_MATRIX_MENU };
#define MAX_PANELS 64
#define MAX_ROWS 16

static struct {
    int ready, height;
    RECT ctl[NPARAMS];             /* each parameter's control */
    int kind[NPARAMS];
    char label[NPARAMS][24];       /* short label under a knob */
    RECT panel[MAX_PANELS];
    const char *panel_title[MAX_PANELS];
    int npanels;
    RECT screen, preset_name, preset_prev, preset_next, help, matrix_head;
    int nrows;
    RECT row[MAX_ROWS];
} L;

/* ---- per instance ---- */
typedef struct {
    AEffect *fx;
    audioMasterCallback host;
    HWND wnd;
    int hover, drag, drag_y, drag_x;
    float drag_from;
    float shown[NPARAMS];
    int shown_prog;
} ed_t;

#define MAX_INSTANCES 32
static ed_t *eds[MAX_INSTANCES];
static dispatcher_t wrapped_dispatch;
static HFONT font_title, font_label, font_value, font_screen, font_big;
static HINSTANCE dll;
static const char *CLASS_NAME = "ONEishEditor";

static ed_t *ed_of(AEffect *fx) {
    for (int i = 0; i < MAX_INSTANCES; i++)
        if (eds[i] && eds[i]->fx == fx) return eds[i];
    return NULL;
}

/* ---- layout ---- */
static int is_matrix(const char *t) { return !strncmp(t, "MOD ", 4) && t[4] >= '0' && t[4] <= '9'; }

static int zone_of(const char *t) {   /* 0 left panel, 1 centre console (matrix), 2 right panel */
    static const char *const left[] = {"LFO", "PITCH MOD", "OSC", "VOICES", "PITCH BEND", "NOISE", "RING", "GLIDE",
                                       "UNISON", "ARP", "SEQ", "CLOCK", "CHORD", "POLY"};
    if (is_matrix(t)) return 1;
    for (unsigned i = 0; i < sizeof left / sizeof left[0]; i++)
        if (!strncmp(t, left[i], strlen(left[i]))) return 0;
    return 2;
}

static void short_label(const char *title, const char *name, char *out, int n) {
    const char *s = name;
    size_t tl = strlen(title);
    if (!_strnicmp(name, title, tl) && name[tl] == ' ') s = name + tl + 1;   /* "Osc 1 Wave" in OSC 1 -> "Wave" */
    snprintf(out, n, "%s", s);
    static const char *const tail[] = {" Level", " Filter", " Amt"};   /* "Osc 1 Level" in MIXER -> "Osc 1" */
    for (unsigned i = 0; i < sizeof tail / sizeof tail[0]; i++) {
        size_t ol = strlen(out), xl = strlen(tail[i]);
        if (ol > xl && !strcmp(out + ol - xl, tail[i])) out[ol - xl] = 0;
    }
}

static void flow(int zone, int x0, int y0, int width, int *bottom) {
    int x = x0, y = y0, row_bottom = y0;
    for (int s = 0; s < NSECTIONS; s++) {
        const section_t *sec = &SECTIONS[s];
        if (zone_of(sec->title) != zone || L.npanels >= MAX_PANELS) continue;
        int w = sec->n * CELL_W + 2 * PAD, h = TITLE_H + CELL_H + 2 * PAD;
        if (x + w > x0 + width && x > x0) { x = x0; y = row_bottom + GAP; }
        RECT r = {x, y, x + w, y + h};
        L.panel[L.npanels] = r;
        L.panel_title[L.npanels++] = sec->title;
        for (int k = 0; k < sec->n; k++) {
            int i = sec->params[k];
            RECT c = {x + PAD + k * CELL_W, y + PAD + TITLE_H, x + PAD + (k + 1) * CELL_W, y + PAD + TITLE_H + CELL_H};
            L.ctl[i] = c;
            L.kind[i] = PARAMS[i].nopts == 2 ? K_TOGGLE : PARAMS[i].nopts > 2 ? K_MENU : K_KNOB;
            short_label(sec->title, PARAMS[i].name, L.label[i], sizeof L.label[i]);
        }
        x += w + GAP;
        if (y + h > row_bottom) row_bottom = y + h;
    }
    *bottom = row_bottom;
}

static void layout(void) {
    if (L.ready) return;
    int top = MARGIN, lx = CHEEK + MARGIN, cx = lx + LEFT_W + GAP, rx = cx + CENTER_W + GAP, lb, rb;
    flow(0, lx, top, LEFT_W, &lb);
    flow(2, rx, top, RIGHT_W, &rb);
    /* centre console: preset browser, the mod matrix, help */
    int y = top;
    SetRect(&L.screen, cx, y, cx + CENTER_W, 0);
    SetRect(&L.preset_prev, cx + 10, y + 8, cx + 40, y + PRESET_H - 8);
    SetRect(&L.preset_next, cx + CENTER_W - 40, y + 8, cx + CENTER_W - 10, y + PRESET_H - 8);
    SetRect(&L.preset_name, cx + 46, y + 6, cx + CENTER_W - 46, y + PRESET_H - 6);
    y += PRESET_H + 30;   /* room for the matrix explanation line */
    SetRect(&L.matrix_head, cx + 8, y, cx + CENTER_W - 8, y + ROW_H);
    y += ROW_H;
    L.nrows = 0;
    for (int s = 0; s < NSECTIONS && L.nrows < MAX_ROWS; s++) {
        const section_t *sec = &SECTIONS[s];
        if (!is_matrix(sec->title) || sec->n < 4) continue;
        SetRect(&L.row[L.nrows], cx + 8, y, cx + CENTER_W - 8, y + ROW_H);
        /* columns: # | source | destination | amount | via */
        static const int col[] = {0, 26, 156, 306, 416, 524};
        for (int k = 0; k < 4; k++) {
            static const int which[] = {1, 2, 3, 4};   /* src, dst, amt, via -> columns 1..4 */
            int i = sec->params[k];
            RECT c = {cx + 8 + col[which[k]], y + 2, cx + 8 + col[which[k] + 1] - 6, y + ROW_H - 2};
            L.ctl[i] = c;
            L.kind[i] = k == 2 ? K_AMOUNT : K_MATRIX_MENU;
            snprintf(L.label[i], sizeof L.label[i], "%s", PARAMS[i].name);
        }
        L.nrows++;
        y += ROW_H;
    }
    y += 10;
    SetRect(&L.help, cx + 8, y, cx + CENTER_W - 8, y + HELP_H);
    int cb = y + HELP_H + 10;
    L.screen.bottom = cb;
    int b = lb > rb ? lb : rb;
    if (cb > b) b = cb;
    L.height = b + MARGIN;
    L.ready = 1;
}

/* ---- values ---- */
static float norm_default(int i) { return PARAMS[i].def; }

static void param_text(ed_t *e, int i, char *out, int n) {
    char disp[64] = "", lab[32] = "";
    e->fx->dispatcher(e->fx, effGetParamDisplay, i, 0, disp, 0);
    e->fx->dispatcher(e->fx, effGetParamLabel, i, 0, lab, 0);
    const char *d = disp;
    while (*d == ' ') d++;
    snprintf(out, n, lab[0] && PARAMS[i].nopts == 0 ? "%s %s" : "%s", d, lab);
}

static void set_value(ed_t *e, int i, float v) {
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    e->fx->setParameter(e->fx, i, v);
    if (e->host) e->host(e->fx, audioMasterAutomate, i, 0, NULL, v);
    InvalidateRect(e->wnd, NULL, FALSE);
}

static float option_norm(int i, int opt) { return PARAMS[i].nopts > 1 ? (float)opt / (PARAMS[i].nopts - 1) : 0.0f; }
static int option_of(ed_t *e, int i) {
    return PARAMS[i].nopts > 1 ? (int)lroundf(e->fx->getParameter(e->fx, i) * (PARAMS[i].nopts - 1)) : 0;
}

/* ---- help text: what the control under the mouse does ---- */
static const char *help_for(int i) {
    static const struct { const char *key, *text; } H[] = {
        {"_src", "Mod source: what moves the destination (an envelope, an LFO, velocity, the mod wheel, aftertouch, the key played, or a random value per note)."},
        {"_dst", "Mod destination: what the source moves. Pitch in semitones, cutoffs in octaves, levels and mix in % of their range."},
        {"_amt", "Mod amount: how far and which way (drag left/right; minus turns the source upside down). Double-click resets to 0."},
        {"_via", "Mod via: scales this row by a second source. LFO 1 -> Pitch via Mod Wheel = vibrato that the wheel brings in."},
        {"_route", "Which filter this source goes to: the ladder, the state-variable filter, or both."},
        {"flt_route", "Parallel: the two filters side by side. Series: the SVF feeds the ladder (send sources to the SVF, mix to Ladder)."},
        {"flt_mix", "Balance between the two filters' outputs: 0% all SVF, 100% all ladder."},
        {"_kbd", "Keyboard tracking: how much the cutoff follows the keys (100% = an octave per octave, around middle C)."},
        {"_sync", "Sync: for an oscillator, restart with oscillator 1 (hard sync); for an LFO or the delay, lock to the host tempo."},
        {"flt_res", "Ladder resonance: emphasis at the cutoff. Near the top the filter sings on its own as a sine (self-oscillation)."},
        {"flt_drive", "Drive: pushes the ladder's input into saturation for a thicker, growling tone."},
        {"flt_slope", "How steeply the ladder cuts: 24 dB per octave is the classic; 6 dB is gentle."},
        {"voices", "How many notes can sound at once. Mono plays one, legato, last key wins."},
        {"bend_range", "How far the pitch wheel bends, in semitones."},
        {"modfx_type", "The modulation effect: chorus, flanger or phaser (Off saves CPU)."},
        {"dly_pp", "Ping-pong: each echo crosses to the other side."},
        {"pitch_lfo", "LFO 1 to the pitch of all three oscillators (vibrato). The mod wheel also adds vibrato through matrix row 1."},
    };
    const char *key = PARAMS[i].key;
    size_t kl = strlen(key);
    for (unsigned h = 0; h < sizeof H / sizeof H[0]; h++) {
        size_t hl = strlen(H[h].key);
        if (H[h].key[0] == '_' ? kl > hl && !strcmp(key + kl - hl, H[h].key) : !strcmp(key, H[h].key)) return H[h].text;
    }
    return NULL;
}

/* ---- drawing ---- */
#define C_PANEL RGB(18, 18, 19)
#define C_MODULE RGB(26, 26, 28)
#define C_LINE RGB(70, 70, 74)
#define C_TEXT RGB(225, 225, 228)
#define C_DIM RGB(150, 150, 156)
#define C_KNOB RGB(38, 38, 40)
#define C_ARC RGB(230, 230, 232)
#define C_TRACK RGB(58, 58, 62)
#define C_LED RGB(230, 40, 30)
#define C_SCREEN RGB(10, 20, 26)
#define C_LCD RGB(150, 215, 232)
#define C_LCD_DIM RGB(90, 130, 145)

static void fill(HDC dc, const RECT *r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, r, b);
    DeleteObject(b);
}

static void frame(HDC dc, const RECT *r, COLORREF c) {
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, r->left, r->top, r->right, r->bottom, 6, 6);
    SelectObject(dc, op); SelectObject(dc, ob);
    DeleteObject(p);
}

static void text(HDC dc, const RECT *r, const char *s, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    RECT rr = *r;
    DrawTextA(dc, s, -1, &rr, fmt | DT_NOPREFIX);
}

static void arc(HDC dc, int cx, int cy, int rad, float a0, float a1, COLORREF c, int width) {   /* angles in degrees, 0 = up, clockwise */
    HPEN p = CreatePen(PS_SOLID, width, c);
    HGDIOBJ op = SelectObject(dc, p);
    int steps = (int)(fabsf(a1 - a0) / 4) + 2;
    for (int k = 0; k <= steps; k++) {
        float a = (a0 + (a1 - a0) * k / steps) * 3.14159265f / 180.0f;
        int x = cx + (int)lroundf(rad * sinf(a)), y = cy - (int)lroundf(rad * cosf(a));
        if (k == 0) MoveToEx(dc, x, y, NULL);
        else LineTo(dc, x, y);
    }
    SelectObject(dc, op);
    DeleteObject(p);
}

static void draw_knob(HDC dc, ed_t *e, int i) {
    const RECT *r = &L.ctl[i];
    int cx = (r->left + r->right) / 2, cy = r->top + 4 + KNOB_D / 2, rad = KNOB_D / 2;
    float v = e->fx->getParameter(e->fx, i);
    int bipolar = PARAMS[i].min < 0 && PARAMS[i].max > 0;
    float a = -135 + 270 * v;
    arc(dc, cx, cy, rad + 3, -135, 135, C_TRACK, 2);
    arc(dc, cx, cy, rad + 3, bipolar ? 0 : -135, a, C_ARC, 2);
    HBRUSH b = CreateSolidBrush(C_KNOB);
    HPEN p = CreatePen(PS_SOLID, 1, RGB(90, 90, 94));
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    Ellipse(dc, cx - rad, cy - rad, cx + rad, cy + rad);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);
    float ar = a * 3.14159265f / 180.0f;
    HPEN wp = CreatePen(PS_SOLID, 2, RGB(245, 245, 245));
    op = SelectObject(dc, wp);
    MoveToEx(dc, cx + (int)(rad * 0.25f * sinf(ar)), cy - (int)(rad * 0.25f * cosf(ar)), NULL);
    LineTo(dc, cx + (int)(rad * 0.9f * sinf(ar)), cy - (int)(rad * 0.9f * cosf(ar)));
    SelectObject(dc, op);
    DeleteObject(wp);
    RECT lr = {r->left - 2, cy + rad + 5, r->right + 2, cy + rad + 19}, vr = {r->left - 2, lr.bottom, r->right + 2, lr.bottom + 14};
    text(dc, &lr, L.label[i], font_label, i == e->hover ? RGB(255, 255, 255) : C_TEXT, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    char val[64];
    param_text(e, i, val, sizeof val);
    text(dc, &vr, val, font_value, C_DIM, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void draw_button(HDC dc, ed_t *e, int i) {   /* option lists: a button showing the choice; two-way: an LED */
    const RECT *r = &L.ctl[i];
    RECT b = {r->left + 4, r->top + 10, r->right - 4, r->top + 32};
    fill(dc, &b, RGB(34, 34, 36));
    frame(dc, &b, i == e->hover ? RGB(200, 200, 204) : C_LINE);
    char val[64];
    param_text(e, i, val, sizeof val);
    RECT tr = b;
    if (L.kind[i] == K_TOGGLE) {
        int on = option_of(e, i) == 1;
        RECT led = {b.left + 5, b.top + 8, b.left + 11, b.top + 14};
        fill(dc, &led, on ? C_LED : RGB(70, 22, 18));
        tr.left += 12;
    }
    text(dc, &tr, val, font_value, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT lr = {r->left - 2, r->top + 40, r->right + 2, r->top + 56};
    text(dc, &lr, L.label[i], font_label, i == e->hover ? RGB(255, 255, 255) : C_TEXT, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void draw_matrix(HDC dc, ed_t *e) {
    static const char *const head[] = {"#", "SOURCE", "DESTINATION", "AMOUNT", "VIA"};
    static const int col[] = {0, 26, 156, 306, 416, 524};
    for (int c = 0; c < 5; c++) {
        RECT h = {L.matrix_head.left + col[c], L.matrix_head.top, L.matrix_head.left + col[c + 1], L.matrix_head.bottom};
        text(dc, &h, head[c], font_label, C_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    int row = 0;
    for (int s = 0; s < NSECTIONS; s++) {
        const section_t *sec = &SECTIONS[s];
        if (!is_matrix(sec->title) || sec->n < 4) continue;
        RECT rr = L.row[row];
        int active = option_of(e, sec->params[0]) > 0 && option_of(e, sec->params[1]) > 0;
        if (row % 2) { RECT band = rr; fill(dc, &band, RGB(14, 27, 34)); }
        char num[8];
        snprintf(num, sizeof num, "%d", row + 1);
        RECT nr = {rr.left + 4, rr.top, rr.left + 26, rr.bottom};
        text(dc, &nr, num, font_screen, active ? C_LCD : C_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        for (int k = 0; k < 4; k++) {
            int i = sec->params[k];
            RECT c = L.ctl[i];
            if (L.kind[i] == K_AMOUNT) {
                float v = e->fx->getParameter(e->fx, i);
                RECT track = {c.left, (c.top + c.bottom) / 2 - 3, c.right - 40, (c.top + c.bottom) / 2 + 3};
                fill(dc, &track, RGB(28, 44, 52));
                int mid = (track.left + track.right) / 2, pos = track.left + (int)(v * (track.right - track.left));
                RECT bar = {pos < mid ? pos : mid, track.top, pos < mid ? mid : pos, track.bottom};
                fill(dc, &bar, active ? C_LCD : C_LCD_DIM);
                char val[32];
                param_text(e, i, val, sizeof val);
                RECT vr = {c.right - 38, c.top, c.right, c.bottom};
                text(dc, &vr, val, font_screen, active ? C_LCD : C_LCD_DIM, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            } else {
                char val[64];
                param_text(e, i, val, sizeof val);
                if (i == e->hover) frame(dc, &c, C_LCD);
                RECT tr = {c.left + 4, c.top, c.right, c.bottom};
                text(dc, &tr, val, font_screen, active ? C_LCD : C_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
        row++;
    }
}

static void paint(ed_t *e, HDC out) {
    RECT all = {0, 0, WIN_W, L.height};
    HDC dc = CreateCompatibleDC(out);
    HBITMAP bm = CreateCompatibleBitmap(out, WIN_W, L.height);
    HGDIOBJ obm = SelectObject(dc, bm);
    fill(dc, &all, C_PANEL);
    for (int side = 0; side < 2; side++) {   /* wooden end cheeks: vertical grain in a few browns */
        int x0 = side ? WIN_W - CHEEK : 0;
        for (int x = 0; x < CHEEK; x++) {
            int g = (int)(10 * sinf(x * 1.7f) + 6 * sinf(x * 0.45f + side));
            RECT stripe = {x0 + x, 0, x0 + x + 1, L.height};
            fill(dc, &stripe, RGB(92 + g, 56 + g / 2, 30 + g / 3));
        }
    }
    for (int p = 0; p < L.npanels; p++) {
        fill(dc, &L.panel[p], C_MODULE);
        frame(dc, &L.panel[p], C_LINE);
        RECT t = {L.panel[p].left + 8, L.panel[p].top + 3, L.panel[p].right - 8, L.panel[p].top + TITLE_H + 2};
        text(dc, &t, L.panel_title[p], font_title, C_TEXT, DT_LEFT | DT_SINGLELINE);
    }
    for (int i = 0; i < NPARAMS; i++) {
        if (L.kind[i] == K_KNOB) draw_knob(dc, e, i);
        else if (L.kind[i] == K_MENU || L.kind[i] == K_TOGGLE) draw_button(dc, e, i);
    }
    /* centre console screen */
    fill(dc, &L.screen, RGB(30, 30, 32));
    RECT inner = {L.screen.left + 4, L.screen.top + 4, L.screen.right - 4, L.screen.bottom - 4};
    fill(dc, &inner, C_SCREEN);
    frame(dc, &inner, RGB(50, 70, 80));
    char name[64] = "";
    int prog = (int)e->fx->dispatcher(e->fx, effGetProgram, 0, 0, NULL, 0);
    e->fx->dispatcher(e->fx, effGetProgramNameIndexed, prog, 0, name, 0);
    char title[96];
    snprintf(title, sizeof title, "%03d  %s", prog + 1, name);
    text(dc, &L.preset_name, title, font_big, C_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &L.preset_prev, "<", font_big, C_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &L.preset_next, ">", font_big, C_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT expl = {L.matrix_head.left, L.preset_name.bottom + 6, L.matrix_head.right, L.matrix_head.top - 2};
    text(dc, &expl, "MOD MATRIX  -  each row: SOURCE moves DESTINATION by AMOUNT, scaled by VIA (if set)",
         font_label, C_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    draw_matrix(dc, e);
    /* help line */
    char help[400];
    if (e->hover >= 0) {
        char val[64];
        param_text(e, e->hover, val, sizeof val);
        const char *h = help_for(e->hover);
        snprintf(help, sizeof help, "%s: %s%s%s", PARAMS[e->hover].name, val, h ? "  -  " : "", h ? h : "");
    } else {
        snprintf(help, sizeof help, "Drag a knob up/down (Shift: fine), scroll to nudge, double-click to reset. Click a "
                                    "button or a matrix cell to pick from a list. < > or click the name: presets.");
    }
    text(dc, &L.help, help, font_label, C_LCD, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    BitBlt(out, 0, 0, WIN_W, L.height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, obm);
    DeleteObject(bm);
    DeleteDC(dc);
}

/* ---- interaction ---- */
static int hit(int x, int y) {
    POINT pt = {x, y};
    for (int i = 0; i < NPARAMS; i++)
        if (L.kind[i] != K_NONE && PtInRect(&L.ctl[i], pt)) return i;
    return -1;
}

static void pick_option(ed_t *e, int i) {   /* a popup menu of the parameter's options */
    HMENU m = CreatePopupMenu();
    int cur = option_of(e, i);
    for (int k = 0; k < PARAMS[i].nopts; k++)
        AppendMenuA(m, MF_STRING | (k == cur ? MF_CHECKED : 0), (UINT_PTR)(k + 1), PARAMS[i].opts[k]);
    POINT pt = {L.ctl[i].left, L.ctl[i].bottom};
    ClientToScreen(e->wnd, &pt);
    int choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, e->wnd, NULL);
    DestroyMenu(m);
    if (choice > 0) {
        if (e->host) e->host(e->fx, audioMasterBeginEdit, i, 0, NULL, 0);
        set_value(e, i, option_norm(i, choice - 1));
        if (e->host) e->host(e->fx, audioMasterEndEdit, i, 0, NULL, 0);
    }
}

static void set_program(ed_t *e, int p) {
    if (p < 0 || p >= e->fx->numPrograms) return;
    e->fx->dispatcher(e->fx, effSetProgram, 0, p, NULL, 0);
    if (e->host) e->host(e->fx, audioMasterUpdateDisplay, 0, 0, NULL, 0);
    InvalidateRect(e->wnd, NULL, FALSE);
}

static void pick_program(ed_t *e) {
    HMENU m = CreatePopupMenu();
    int cur = (int)e->fx->dispatcher(e->fx, effGetProgram, 0, 0, NULL, 0);
    for (int p = 0; p < e->fx->numPrograms; p++) {
        char name[64] = "", item[80];
        e->fx->dispatcher(e->fx, effGetProgramNameIndexed, p, 0, name, 0);
        snprintf(item, sizeof item, "%03d  %s", p + 1, name);
        AppendMenuA(m, MF_STRING | (p == cur ? MF_CHECKED : 0), (UINT_PTR)(p + 1), item);
    }
    POINT pt = {L.preset_name.left, L.preset_name.bottom};
    ClientToScreen(e->wnd, &pt);
    int choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, e->wnd, NULL);
    DestroyMenu(m);
    if (choice > 0) set_program(e, choice - 1);
}

static LRESULT CALLBACK wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    ed_t *e = (ed_t *)GetWindowLongPtrA(w, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lp;
        SetWindowLongPtrA(w, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        SetTimer(w, 1, 50, NULL);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        if (e) paint(e, dc);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:   /* drawing into a given DC: screenshots, thumbnails */
        if (e) paint(e, (HDC)wp);
        return 0;
    case WM_TIMER: {   /* values the host or a preset changed: redraw */
        if (!e) return 0;
        int changed = 0, prog = (int)e->fx->dispatcher(e->fx, effGetProgram, 0, 0, NULL, 0);
        for (int i = 0; i < NPARAMS; i++) {
            float v = e->fx->getParameter(e->fx, i);
            if (v != e->shown[i]) { e->shown[i] = v; changed = 1; }
        }
        if (prog != e->shown_prog) { e->shown_prog = prog; changed = 1; }
        if (changed) InvalidateRect(w, NULL, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!e) return 0;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (e->drag >= 0) {
            int i = e->drag;
            float scale = (wp & MK_SHIFT) ? 1.0f / 1000 : 1.0f / 200;
            float v = L.kind[i] == K_AMOUNT ? e->drag_from + (x - e->drag_x) * scale * 1.5f
                                            : e->drag_from + (e->drag_y - y) * scale;
            set_value(e, i, v);
            return 0;
        }
        int h = hit(x, y);
        if (h != e->hover) { e->hover = h; InvalidateRect(w, NULL, FALSE); }
        TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, w, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (e && e->drag < 0) { e->hover = -1; InvalidateRect(w, NULL, FALSE); }
        return 0;
    case WM_LBUTTONDOWN: {
        if (!e) return 0;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        POINT pt = {x, y};
        SetFocus(w);
        if (PtInRect(&L.preset_prev, pt)) { set_program(e, e->shown_prog - 1); return 0; }
        if (PtInRect(&L.preset_next, pt)) { set_program(e, e->shown_prog + 1); return 0; }
        if (PtInRect(&L.preset_name, pt)) { pick_program(e); return 0; }
        int i = hit(x, y);
        if (i < 0) return 0;
        if (L.kind[i] == K_KNOB || L.kind[i] == K_AMOUNT) {
            e->drag = i;
            e->drag_x = x;
            e->drag_y = y;
            e->drag_from = e->fx->getParameter(e->fx, i);
            SetCapture(w);
            if (e->host) e->host(e->fx, audioMasterBeginEdit, i, 0, NULL, 0);
        } else if (L.kind[i] == K_TOGGLE) {
            if (e->host) e->host(e->fx, audioMasterBeginEdit, i, 0, NULL, 0);
            set_value(e, i, option_of(e, i) ? 0.0f : 1.0f);
            if (e->host) e->host(e->fx, audioMasterEndEdit, i, 0, NULL, 0);
        } else {
            pick_option(e, i);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (e && e->drag >= 0) {
            if (e->host) e->host(e->fx, audioMasterEndEdit, e->drag, 0, NULL, 0);
            e->drag = -1;
            ReleaseCapture();
        }
        return 0;
    case WM_LBUTTONDBLCLK: {   /* back to the default */
        if (!e) return 0;
        int i = hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (i >= 0 && (L.kind[i] == K_KNOB || L.kind[i] == K_AMOUNT)) set_value(e, i, norm_default(i));
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!e) return 0;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(w, &pt);
        int i = hit(pt.x, pt.y), dir = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
        if (i < 0) return 0;
        float v = e->fx->getParameter(e->fx, i);
        if (PARAMS[i].nopts > 1) {
            int o = option_of(e, i) + dir;
            if (o >= 0 && o < PARAMS[i].nopts) set_value(e, i, option_norm(i, o));
        } else {
            float span = PARAMS[i].max - PARAMS[i].min;
            float step = PARAMS[i].int_display && span > 0 ? 1.0f / span : 0.01f;
            set_value(e, i, v + dir * step);
        }
        return 0;
    }
    case WM_DESTROY:
        KillTimer(w, 1);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static void init_window_class(void) {
    static int done;
    if (done) return;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void *)&wndproc, &dll);
    WNDCLASSA wc = {0};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = dll;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassA(&wc);
    font_title = CreateFontA(-12, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    font_label = CreateFontA(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    font_value = CreateFontA(-10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    font_screen = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
    font_big = CreateFontA(-20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    done = 1;
}

/* ---- the VST2 side ---- */
static intptr_t dispatcher(AEffect *fx, int32_t op, int32_t idx, intptr_t v, void *ptr, float opt) {
    ed_t *e = ed_of(fx);
    switch (op) {
    case effEditGetRect: {
        static ERect r;
        layout();
        r.top = 0; r.left = 0; r.bottom = (int16_t)L.height; r.right = (int16_t)WIN_W;
        if (ptr) *(ERect **)ptr = &r;
        return 1;
    }
    case effEditOpen: {
        if (!e || !ptr) return 0;
        layout();
        init_window_class();
        if (e->wnd) DestroyWindow(e->wnd);
        e->hover = e->drag = -1;
        e->wnd = CreateWindowExA(0, CLASS_NAME, "ONE-ish", WS_CHILD | WS_VISIBLE, 0, 0, WIN_W, L.height,
                                 (HWND)ptr, NULL, dll, e);
        return e->wnd != NULL;
    }
    case effEditClose:
        if (e && e->wnd) { DestroyWindow(e->wnd); e->wnd = NULL; }
        return 1;
    case effEditIdle:
        return 0;
    case 1: {   /* effClose: the instance goes away; forget it after the wrapper is done */
        if (e && e->wnd) { DestroyWindow(e->wnd); e->wnd = NULL; }
        intptr_t r = wrapped_dispatch(fx, op, idx, v, ptr, opt);
        for (int i = 0; i < MAX_INSTANCES; i++)
            if (eds[i] == e) { HeapFree(GetProcessHeap(), 0, e); eds[i] = NULL; }
        return r;
    }
    }
    return wrapped_dispatch(fx, op, idx, v, ptr, opt);
}

__declspec(dllexport) AEffect *VSTPluginMain(audioMasterCallback host) {
    AEffect *fx = one_ish_wrapped_main(host);
    if (!fx) return NULL;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (eds[i]) continue;
        ed_t *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *e);
        if (!e) break;
        e->fx = fx;
        e->host = host;
        e->hover = e->drag = -1;
        e->shown_prog = -1;
        eds[i] = e;
        wrapped_dispatch = fx->dispatcher;
        fx->dispatcher = dispatcher;
        fx->flags |= effFlagsHasEditor;
        break;
    }
    return fx;   /* no free slot: the plugin still works, just without its window */
}
