/* Drawing for the desktop editor (see editor.c): the panels, knobs, LED lists, buttons, the oscillators' wave
 * screens, the centre console's screen, the left-hand controller and the keyboard. All drawn from scratch: GDI for
 * text, GDI+ (gfx.h) for anti-aliased shapes. UI thread only. */
#include <stdio.h>
#include <string.h>
#include "editor.h"
#include "gfx.h"

/* ---- help text: what the control under the mouse does ---- */
static const char *help_for(int i) {
    static const struct { const char *key, *text; } H[] = {
        {"_src", "Mod source: what moves the destination (an envelope, an LFO, velocity, the mod wheel, aftertouch, the key played, or a random value per note)."},
        {"_dst", "Mod destination: what the source moves. Pitch in semitones, cutoffs in octaves, levels and mix in % of their range."},
        {"_amt", "Mod amount: how far and which way (drag left/right; minus turns the source upside down). Double-click resets to 0."},
        {"_via", "Mod via: scales this row by a second source. LFO 1 -> Pitch via Mod Wheel = vibrato that the wheel brings in."},
        {"_svf", "Sends this mixer channel to the state-variable filter. With the Ladder button too it feeds both; with neither it is silent."},
        {"_lad", "Sends this mixer channel to the ladder filter. With the SVF button too it feeds both; with neither it is silent."},
#define OSC_HELP(n) \
        {"o" #n "_wave", "Tri: Wave Angle shapes a triangle (left: saw-like, centre: triangle, right: ramp). Saw: Wave Angle slows the saw's drop for a darker tone."}, \
        {"o" #n "_mix", "Blends this oscillator's two waves: 0% the triangle/saw only, 100% the pulse only. The little screen shows the result."}
        OSC_HELP(1), OSC_HELP(2), OSC_HELP(3),
#undef OSC_HELP
        {"_oct", "Octave: the oscillator's footage, 32' (two octaves down) to 2' (two up). Click to step, right-click for the list."},
        {"_angle", "Wave Angle: the shape of the triangle/saw wave (see the Wave button)."},
        {"_pw", "Pulse width: 50% is a square (hollow); narrower or wider sounds thinner and nasal."},
        {"_freq", "Tunes this oscillator up or down by up to 7 semitones, smoothly (7 = a fifth)."},
        {"_beat", "Detunes by a fixed number of Hz, so the beating stays at the same speed up and down the keyboard."},
        {"ring_src", "Which two oscillators the ring modulator multiplies (their triangle/saw waves): metallic, bell-like tones."},
        {"fm_amt", "Linear FM: one oscillator wobbles another's frequency at audio rate, for bells, brass and clangs."},
        {"fm_route", "Which oscillator modulates which: 1 > 2, 1 > 3 or 3 > 1. The modulator can be silent in the mixer."},
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

static const char *zone_help(int z) {
    switch (z) {
    case Z_KEYS: return "Keyboard: click a key to play it (lower on the key = louder), drag across to slide between notes. "
                        "The octave buttons shift it.";
    case Z_PITCH: return "Pitch wheel: drag up or down to bend; it springs back to the centre. The BEND knob sets the range.";
    case Z_MODW: return "Mod wheel (MIDI CC 1): drag up to bring in the mod matrix's Mod Wheel rows (vibrato in most presets).";
    case Z_PAD: return "X/Y pad: drag inside it. Up/down sends aftertouch (the matrix source Aftertouch; back to 0 when you let go). "
                       "Left/right sends MIDI CC 2, which the engine does not use yet.";
    case Z_OCT_DOWN: case Z_OCT_UP: return "Octave: shifts the on-screen keyboard down or up, two octaves each way.";
    case Z_ENCODER: return "Preset knob: drag up/down or scroll to step through the presets; click it for the list.";
    case Z_PREV: case Z_NEXT: return "Previous / next preset.";
    case Z_NAME: return "Click the preset name for the list of presets.";
    case Z_TAB_MATRIX: return "MATRIX: the 8 mod matrix rows on the screen.";
    case Z_TAB_MORE: return "MORE: parameters that have no place on the panel yet (click again for the next page).";
    }
    return NULL;
}

/* ---- drawing ---- */
#define C_PANEL_BG 0xFF1E1E20
#define C_LINE 0xFF6A6A6E
#define C_STRIP 0xFFC8C9CB
#define T_LABEL RGB(214, 214, 216)
#define T_HOT RGB(255, 255, 255)
#define T_VALUE RGB(140, 140, 146)
#define T_DARK RGB(24, 24, 26)
#define C_SCREEN 0xFF0B1621
#define T_LCD RGB(150, 215, 232)
#define T_LCD_DIM RGB(90, 130, 145)

static void text(HDC dc, const RECT *r, const char *s, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    RECT rr = *r;
    DrawTextA(dc, s, -1, &rr, fmt | DT_NOPREFIX);
}
static void text_c(HDC dc, int cx, int top, int w, int h, const char *s, HFONT f, COLORREF c) {   /* centred */
    RECT r = {cx - w / 2, top, cx + w / 2, top + h};
    text(dc, &r, s, f, c, DT_CENTER | DT_TOP | DT_SINGLELINE);
}

static void wood(int x, int y, int w, int h, int vertical) {   /* grain along the board */
    int n = vertical ? w : h;
    for (int t = 0; t < n; t++) {
        float g = 12.0f * sinf(t * 0.31f + 2.3f * sinf(t * 0.043f)) + 7.0f * sinf(t * 1.73f) + 4.0f * sinf(t * 4.1f);
        int r = 188 + (int)g, gg = 112 + (int)(g * 0.75f), b = 48 + (int)(g * 0.4f);
        DWORD c = 0xFF000000u | (DWORD)(r << 16) | (DWORD)(gg << 8) | (DWORD)b;
        if (vertical) gfx_rect((float)(x + t), (float)y, 1.0f, (float)h, c);
        else gfx_rect((float)x, (float)(y + t), (float)w, 1.0f, c);
    }
}

static void draw_panels(HDC dc) {
    for (int p = 0; p < NPANELS; p++) {
        const lay_panel_t *q = &PANELS[p];
        float x = (float)sc(q->x), y = (float)sc(q->y), w = (float)sc(q->w), h = (float)sc(q->h);
        if (q->style == 0) {   /* a silver-edged block */
            gfx_rect_grad(x, y, w, h, 0xFFC4C6C9, 0xFF8E9094, 0);
            float b = (float)sc(6);
            gfx_rect(x + b, y + b, w - 2 * b, h - 2 * b, C_PANEL_BG);
            gfx_frame(x + b, y + b, w - 2 * b, h - 2 * b, 1.0f, 0xFF0C0C0C);
        } else if (q->style == 1) {   /* a module: hairline frame and a light title strip */
            gfx_frame(x + 0.5f, y + 0.5f, w - 1, h - 1, 1.0f, C_LINE);
            float sh = (float)sc(13);
            const char *bar = strchr(q->title, '|');
            char a[64];
            snprintf(a, sizeof a, "%.*s", bar ? (int)(bar - q->title) : 63, q->title);
            if (bar) {   /* two titles side by side (the two filters) */
                float half = w / 2;
                gfx_rect(x, y, half - 1, sh, C_STRIP);
                gfx_rect(x + half + 1, y, half - 1, sh, C_STRIP);
                RECT r1 = {(int)x, (int)y, (int)(x + half), (int)(y + sh)}, r2 = {(int)(x + half), (int)y, (int)(x + w), (int)(y + sh)};
                text(dc, &r1, a, font_title, T_DARK, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                text(dc, &r2, bar + 1, font_title, T_DARK, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                gfx_rect(x, y, w, sh, C_STRIP);
                RECT r = {(int)x, (int)y, (int)(x + w), (int)(y + sh)};
                text(dc, &r, a, font_title, T_DARK, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        } else {   /* a row caption */
            RECT r = {(int)x, (int)y, (int)(x + w), (int)(y + h)};
            text(dc, &r, q->title, font_tiny, T_VALUE, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

static void knob(float cx, float cy, float r, float v, int bipolar) {   /* a black skirt with a silver cap */
    for (int t = 0; t <= 10 && r > 10 * L.s; t++) {   /* a scale, except on the smallest knobs */
        gpt_t a = gfx_polar(cx, cy, r + 3.0f * L.s + 1, -135.0f + 27.0f * t), b = gfx_polar(cx, cy, r + 6.5f * L.s + 1, -135.0f + 27.0f * t);
        gfx_line(a.x, a.y, b.x, b.y, 1.0f, 0xFF77777B);
    }
    float a = -135.0f + 270.0f * v;
    gfx_arc(cx, cy, r + 1.8f * L.s + 0.5f, bipolar ? 0.0f : -135.0f, a, 1.6f, 0xC0E8E8E8);
    gpt_t sk[60];
    for (int k = 0; k < 60; k++) {   /* knurled skirt */
        float ang = k * 6.0f, rr = r * (0.955f + 0.045f * cosf(ang * 12.0f * 3.14159265f / 180.0f));
        sk[k] = gfx_polar(cx, cy, rr, ang);
    }
    gfx_poly(sk, 60, 0xFF0D0D0D);
    gfx_circle(cx, cy, r * 0.97f, 1.0f, 0xFF333336);
    float cr = r * 0.66f;
    gfx_disc(cx, cy + 1, cr + 1.2f, 0x90000000);
    gfx_disc_grad(cx, cy, cr, 0xFFF3F3F3, 0xFF707074);
    for (int k = 1; k <= 3; k++) gfx_circle(cx, cy, cr * k / 4.0f, 0.6f, 0x22FFFFFF);   /* brushed rings */
    gfx_circle(cx, cy, cr, 1.0f, 0xFF3A3A3C);
    gpt_t p0 = gfx_polar(cx, cy, cr * 0.25f, a), p1 = gfx_polar(cx, cy, cr * 0.92f, a);
    gfx_line(p0.x, p0.y, p1.x, p1.y, r > 20 ? 2.4f : 1.8f, 0xFF2A2A2C);
    gpt_t d = gfx_polar(cx, cy, r * 0.83f, a);
    gfx_disc(d.x, d.y, r > 20 ? 2.4f : 1.7f, 0xFFF4F4F4);
}

static void draw_knob(HDC dc, ed_t *e, int i) {
    const RECT *b = &L.ctl[i];
    int d = L.size[i], small = L.kind[i] == K_SMALL, lh = sc(12), vh = small ? 0 : sc(11);
    int content = d + sc(9) + lh + vh, top = b->top + (rh(b) - content) / 2, cx = (b->left + b->right) / 2;
    float v = e->fx->getParameter(e->fx, i);
    knob((float)cx, top + d / 2.0f + sc(4), d / 2.0f, v, PARAMS[i].min < 0 && PARAMS[i].max > 0);
    int tw = rw(b) > sc(76) ? rw(b) : sc(76), ly = top + d + sc(10);
    text_c(dc, cx, ly, tw, lh + 2, L.label[i], font_label, i == e->hover ? T_HOT : T_LABEL);
    if (!small) {
        char val[64];
        param_text(e, i, val, sizeof val);
        text_c(dc, cx, ly + lh, tw, vh + 2, val, font_value, T_VALUE);
    }
}

static void led(float cx, float cy, int on) {
    float r = 2.8f * L.s + 0.4f;
    if (on) {
        gfx_disc(cx, cy, r * 2.6f, 0x30FFE6B0);
        gfx_disc(cx, cy, r * 1.7f, 0x50FFEBC0);
        gfx_disc(cx, cy, r, 0xFFFFF8E8);
    } else {
        gfx_disc(cx, cy, r, 0xFF2A2A2C);
        gfx_circle(cx, cy, r, 0.8f, 0xFF4E4E52);
    }
}

static void red_button(const RECT *r, int hot) {
    gfx_rect_grad((float)r->left, (float)r->top, (float)rw(r), (float)rh(r), hot ? 0xFFF04040 : 0xFFD42A26, 0xFF8A1210, 0);
    gfx_line((float)r->left + 1, (float)r->top + 0.5f, (float)r->right - 1, (float)r->top + 0.5f, 1.0f, 0x60FFFFFF);
}

/* an LED list: one lit LED beside each option, a red button under it that steps through them */
void leds_geom(int i, int *x0, int *y0, int *row, RECT *button) {
    const RECT *b = &L.ctl[i];
    int n = PARAMS[i].nopts, content = n * sc(10) + sc(4) + sc(12) + sc(3) + sc(12);
    *row = sc(10);
    *y0 = b->top + (rh(b) - content) / 2;
    *x0 = (b->left + b->right) / 2 - sc(16);
    int cx = (b->left + b->right) / 2, by = *y0 + n * *row + sc(4);
    SetRect(button, cx - sc(13), by, cx + sc(13), by + sc(12));
}

static void draw_leds(HDC dc, ed_t *e, int i) {
    int x0, y0, row, cur = option_of(e, i);
    RECT bt;
    leds_geom(i, &x0, &y0, &row, &bt);
    for (int k = 0; k < PARAMS[i].nopts; k++) {
        int y = y0 + k * row;
        led((float)x0, y + row / 2.0f, k == cur);
        RECT tr = {x0 + sc(7), y - sc(1), x0 + sc(52), y + row + sc(1)};
        text(dc, &tr, L.leds[i] ? L.leds[i][k] : PARAMS[i].opts[k], font_tiny, k == cur ? T_LABEL : T_VALUE,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    red_button(&bt, i == e->hover);
    int cx = (L.ctl[i].left + L.ctl[i].right) / 2;
    text_c(dc, cx, bt.bottom + sc(3), sc(76), sc(14), L.label[i], font_label, i == e->hover ? T_HOT : T_LABEL);
}

static void draw_toggle(HDC dc, ed_t *e, int i) {   /* a black button that lights up white when on */
    const RECT *b = &L.ctl[i];
    int side = L.size[i] == 1;   /* the label beside the button (rows of envelope options) or under it */
    int cx = (b->left + b->right) / 2, bw = sc(side ? 18 : 22), bh = sc(side ? 10 : 11), content = side ? bh : bh + sc(4) + sc(12);
    int top = b->top + (rh(b) - content) / 2, on = option_of(e, i) == 1;
    float x = side ? b->left + 1.0f : cx - bw / 2.0f, y = (float)top;
    if (on) {
        gfx_rect(x - 3, y - 3, bw + 6.0f, bh + 6.0f, 0x30FFFFF0);
        gfx_rect_grad(x, y, (float)bw, (float)bh, 0xFFFFFFFA, 0xFFD8D8D0, 0);
    } else {
        gfx_rect_grad(x, y, (float)bw, (float)bh, 0xFF2C2C2E, 0xFF101012, 0);
    }
    gfx_frame(x, y, (float)bw, (float)bh, 1.0f, i == e->hover ? 0xFFB0B0B4 : 0xFF4A4A4E);
    if (side) {
        RECT r = {(int)x + bw + sc(5), b->top, b->right + sc(6), b->bottom};
        text(dc, &r, L.label[i], font_label, i == e->hover ? T_HOT : T_LABEL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        text_c(dc, cx, top + bh + sc(4), sc(76), sc(14), L.label[i], font_label, i == e->hover ? T_HOT : T_LABEL);
    }
}

static void draw_button(HDC dc, ed_t *e, int i) {   /* a long option list: its value in a little window, a red button */
    const RECT *b = &L.ctl[i];
    int cx = (b->left + b->right) / 2, ww = rw(b) - sc(4), content = sc(15) + sc(4) + sc(12) + sc(3) + sc(12);
    int top = b->top + (rh(b) - content) / 2;
    RECT win = {cx - ww / 2, top, cx + ww / 2, top + sc(15)};
    gfx_rect((float)win.left, (float)win.top, (float)rw(&win), (float)rh(&win), C_SCREEN);
    char val[64];
    param_text(e, i, val, sizeof val);
    text(dc, &win, val, font_tiny, T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT bt = {cx - sc(13), win.bottom + sc(4), cx + sc(13), win.bottom + sc(16)};
    red_button(&bt, i == e->hover);
    text_c(dc, cx, bt.bottom + sc(3), sc(76), sc(14), L.label[i], font_label, i == e->hover ? T_HOT : T_LABEL);
}

static void draw_octave(HDC dc, ed_t *e, int i) {   /* a rotary switch with its footages around it */
    const RECT *b = &L.ctl[i];
    int n = PARAMS[i].nopts, cur = option_of(e, i), d = L.size[i], cx = (b->left + b->right) / 2;
    float r = d / 2.0f, cy = b->top + sc(14) + r, spread = 150.0f;
    float a = n > 1 ? -spread / 2 + spread * cur / (n - 1) : 0;
    for (int k = 0; k < n; k++) {
        float ak = n > 1 ? -spread / 2 + spread * k / (n - 1) : 0;
        gpt_t p = gfx_polar((float)cx, cy, r + sc(11), ak);
        RECT tr = {(int)p.x - sc(14), (int)p.y - sc(7), (int)p.x + sc(14), (int)p.y + sc(7)};
        text(dc, &tr, PARAMS[i].opts[k], font_tiny, k == cur ? T_HOT : T_VALUE, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    knob((float)cx, cy, r, (a + 135.0f) / 270.0f, 0);
    text_c(dc, cx, (int)(cy + r + sc(8)), sc(76), sc(14), L.label[i], font_label, i == e->hover ? T_HOT : T_LABEL);
}

static void draw_wave(ed_t *e, const lay_wave_t *w) {   /* the oscillator's wave, from its wave/angle/pw/mix */
    float x = (float)sc(w->x), y = (float)sc(w->y), ww = (float)sc(w->w), hh = (float)sc(w->h);
    gfx_rect(x, y, ww, hh, 0xFF08121C);
    gfx_frame(x, y, ww, hh, 1.2f, 0xFFB9C6CE);
    if (w->param[0] < 0 || w->param[1] < 0 || w->param[2] < 0 || w->param[3] < 0) return;
    int saw = option_of(e, w->param[0]) == 1;
    float angle = real_of(e, w->param[1]) / 100.0f, pw = real_of(e, w->param[2]) / 100.0f, mix = real_of(e, w->param[3]) / 100.0f;
    float a = saw ? 1.0f - (0.03f + 0.25f * angle) : 1.0f - angle;
    a = a < 0.02f ? 0.02f : a > 0.98f ? 0.98f : a;
    enum { N = 160 };
    gpt_t pts[N + 1];
    float px = x + 4, pw_ = ww - 8, mid = y + hh / 2, amp = hh / 2 - 5;
    for (int k = 0; k <= N; k++) {
        float t = fmodf(2.0f * k / N, 1.0f);
        if (k == N) t = 1.0f;
        float tri = t < a ? -1.0f + 2.0f * t / a : 1.0f - 2.0f * (t - a) / (1.0f - a);
        float pul = t < pw ? 1.0f : -1.0f;
        float vv = tri + mix * (pul - tri);
        pts[k].x = px + pw_ * k / N;
        pts[k].y = mid - vv * amp;
    }
    gfx_lines(pts, N + 1, 3.2f, 0x4087DFFF);
    gfx_lines(pts, N + 1, 1.3f, 0xFFA8ECFF);
}

static void draw_screen(HDC dc, ed_t *e) {
    RECT *s = &L.screen;
    gfx_rect((float)s->left - 3, (float)s->top - 3, (float)rw(s) + 6, (float)rh(s) + 6, 0xFF050505);
    gfx_rect((float)s->left, (float)s->top, (float)rw(s), (float)rh(s), C_SCREEN);
    char name[64] = "", title[96];
    int prog = (int)e->fx->dispatcher(e->fx, effGetProgram, 0, 0, NULL, 0);
    e->fx->dispatcher(e->fx, effGetProgramNameIndexed, prog, 0, name, 0);
    snprintf(title, sizeof title, "%03d  %s", prog + 1, name);
    gfx_frame((float)L.name.left, (float)L.name.top, (float)rw(&L.name), (float)rh(&L.name), 1.2f, 0xFFDCE6EE);
    text(dc, &L.name, title, font_big, e->hover == Z_NAME ? T_HOT : T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &L.prev, "<", font_big, T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &L.next, ">", font_big, T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (e->page == 0) {
        text(dc, &L.tabline, "MOD MATRIX:  SOURCE moves DESTINATION by AMOUNT x VIA", font_tiny, T_LCD_DIM,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        static const char *const head[] = {"#", "SOURCE", "DESTINATION", "AMOUNT", "VIA"};
        static const int col[] = {0, 16, 82, 172, 254, 324};
        for (int c = 0; c < 5; c++) {
            RECT h = {L.head.left + sc(col[c]), L.head.top, L.head.left + sc(col[c + 1]), L.head.bottom};
            text(dc, &h, head[c], font_tiny, T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    } else {
        char t[96];
        int pages = MORE_PAGES;
        snprintf(t, sizeof t, "MORE  -  %d parameter%s not on the panel%s", NOTHER, NOTHER == 1 ? "" : "s",
                 pages > 1 ? "  (click MORE for the next page)" : "");
        text(dc, &L.tabline, t, font_tiny, T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    for (int r = 0; r < ROWS; r++)
        if (r % 2) gfx_rect((float)L.row[r].left, (float)L.row[r].top, (float)rw(&L.row[r]), (float)rh(&L.row[r]), 0xFF0F1E2A);
    if (e->page == 0) {
        for (int r = 0; r < NMATRIX && r < ROWS; r++) {
            int active = option_of(e, MATRIX[r][0]) > 0 && option_of(e, MATRIX[r][1]) > 0;
            char num[8];
            snprintf(num, sizeof num, "%d", r + 1);
            RECT nr = {L.row[r].left + sc(2), L.row[r].top, L.row[r].left + sc(16), L.row[r].bottom};
            text(dc, &nr, num, font_screen, active ? T_LCD : T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
    for (int i = 0; i < NPARAMS; i++) {
        RECT c, nm;
        int active = 1;
        if (L.where[i] == W_MATRIX && e->page == 0) {
            c = L.ctl[i];
            for (int r = 0; r < NMATRIX && r < ROWS; r++)
                for (int k = 0; k < 4; k++)
                    if (MATRIX[r][k] == i) active = option_of(e, MATRIX[r][0]) > 0 && option_of(e, MATRIX[r][1]) > 0;
        } else if (L.where[i] == W_MORE && e->page == 1) {
            int j = 0;
            while (j < NOTHER && OTHER[j] != i) j++;
            if (j / MORE_PER_PAGE != e->more_page) continue;
            more_rects(j % MORE_PER_PAGE, &nm, &c);
            text(dc, &nm, PARAMS[i].name, font_screen, i == e->hover ? T_HOT : T_LCD_DIM,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            continue;
        }
        COLORREF col = active ? T_LCD : T_LCD_DIM;
        char val[64];
        param_text(e, i, val, sizeof val);
        if (L.kind[i] == K_AMOUNT) {
            float v = e->fx->getParameter(e->fx, i);
            int vw = sc(36), cy = (c.top + c.bottom) / 2;
            float tl = (float)c.left, tr = (float)(c.right - vw - sc(2));
            gfx_rect(tl, cy - 3.0f * L.s, tr - tl, 6.0f * L.s, 0xFF1C2C36);
            int bip = PARAMS[i].min < 0 && PARAMS[i].max > 0;
            float from = bip ? (tl + tr) / 2 : tl, to = tl + v * (tr - tl);
            gfx_rect(from < to ? from : to, cy - 3.0f * L.s, fabsf(to - from), 6.0f * L.s, active ? 0xFF96D7E8 : 0xFF5A8291);
            RECT vr = {c.right - vw, c.top, c.right, c.bottom};
            text(dc, &vr, val, font_screen, col, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        } else {
            if (i == e->hover) gfx_frame((float)c.left, (float)c.top, (float)rw(&c), (float)rh(&c), 1.0f, 0xFF96D7E8);
            RECT tr = {c.left + sc(3), c.top, c.right, c.bottom};
            text(dc, &tr, val, font_screen, col, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
    /* help: what the control under the mouse does */
    char help[480];
    const char *zh = zone_help(e->hover);
    if (e->hover >= 0 && e->hover < NPARAMS) {
        char val[64];
        param_text(e, e->hover, val, sizeof val);
        const char *h = help_for(e->hover);
        snprintf(help, sizeof help, "%s: %s%s%s", PARAMS[e->hover].name, val, h ? "\n" : "", h ? h : "");
    } else if (zh) {
        snprintf(help, sizeof help, "%s", zh);
    } else {
        snprintf(help, sizeof help, "Drag a knob up/down (Shift: fine), scroll to nudge, double-click to reset. Click a "
                                    "red button to step, an LED to pick, right-click for a list. Play the keys below.");
    }
    gfx_line((float)L.help.left, L.help.top - 4.0f, (float)L.help.right, L.help.top - 4.0f, 1.0f, 0xFF27404E);
    text(dc, &L.help, help, font_screen, T_LCD, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
}

static void panel_button(HDC dc, const RECT *r, const char *label, int lit, int hot) {   /* console buttons */
    int bw = sc(30), bh = sc(13), cx = (r->left + r->right) / 2;
    float x = cx - bw / 2.0f, y = (float)(r->top + sc(6));
    if (lit) {
        gfx_rect(x - 3, y - 3, bw + 6.0f, bh + 6.0f, 0x30FFFFF0);
        gfx_rect_grad(x, y, (float)bw, (float)bh, 0xFFFFFFFA, 0xFFD8D8D0, 0);
    } else {
        gfx_rect_grad(x, y, (float)bw, (float)bh, 0xFF2C2C2E, 0xFF101012, 0);
    }
    gfx_frame(x, y, (float)bw, (float)bh, 1.0f, hot ? 0xFFB0B0B4 : 0xFF4A4A4E);
    text_c(dc, cx, (int)y + bh + sc(5), rw(r) + sc(10), sc(14), label, font_label, hot ? T_HOT : T_LABEL);
}

static void draw_console(HDC dc, ed_t *e) {
    draw_screen(dc, e);
    panel_button(dc, &L.tab_matrix, "MATRIX", e->page == 0, e->hover == Z_TAB_MATRIX);
    panel_button(dc, &L.tab_more, NOTHER ? "MORE" : "MORE (0)", e->page == 1, e->hover == Z_TAB_MORE);
    panel_button(dc, &L.bprev, "PREV", 0, e->hover == Z_PREV);
    panel_button(dc, &L.bnext, "NEXT", 0, e->hover == Z_NEXT);
    int cx = (L.encoder.left + L.encoder.right) / 2, r = sc(31);
    int prog = (int)e->fx->dispatcher(e->fx, effGetProgram, 0, 0, NULL, 0), np = e->fx->numPrograms;
    knob((float)cx, (float)(L.encoder.top + sc(8) + r), (float)r, np > 1 ? (float)prog / (np - 1) : 0.0f, 0);
    text_c(dc, cx, L.encoder.top + sc(14) + 2 * r, sc(80), sc(14), "PRESET", font_label, e->hover == Z_ENCODER ? T_HOT : T_LABEL);
    text(dc, &L.brand, "ONE-ish", font_brand, RGB(170, 172, 176), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void wheel(HDC dc, const RECT *r, float pos, int centred, const char *label, int hot) {   /* pos -1..1 */
    float x = (float)r->left, y = (float)r->top, w = (float)rw(r), h = (float)rh(r);
    gfx_rect(x - 3, y - 3, w + 6, h + 6, 0xFF050506);
    gfx_rect_grad(x, y, w / 2, h, 0xFF4A4A4E, 0xFFD6D6DA, 1);
    gfx_rect_grad(x + w / 2, y, w / 2, h, 0xFFD6D6DA, 0xFF505054, 1);
    float gy = y + h / 2 - pos * (h / 2 - 10 * L.s);   /* the grip moves with the wheel */
    for (int k = -3; k <= 3; k++) {
        float yy = gy + k * 3.2f * L.s;
        if (yy > y + 2 && yy < y + h - 2) gfx_line(x + 2, yy, x + w - 2, yy, 1.0f, 0x90202022);
    }
    gfx_rect(x, gy - 1.2f * L.s, w, 2.4f * L.s, 0xFF1A1A1C);
    if (centred) gfx_line(x - 5, y + h / 2, x - 2, y + h / 2, 1.0f, 0xFF9A9A9E);
    text_c(dc, r->left + rw(r) / 2, r->bottom + sc(4), sc(60), sc(14), label, font_label, hot ? T_HOT : T_LABEL);
}

static void draw_controller(HDC dc, ed_t *e) {
    for (int k = 0; k < 5; k++) {
        float x = L.oct_leds.left + rw(&L.oct_leds) * (k + 0.5f) / 5;
        led(x, (float)(L.oct_leds.top + sc(5)), e->oct == k - 2);
        static const char *const lab[] = {"-2", "-1", "0", "+1", "+2"};
        text_c(dc, (int)x, L.oct_leds.top + sc(10), sc(22), sc(13), lab[k], font_tiny, T_VALUE);
    }
    red_button(&L.oct_down, e->hover == Z_OCT_DOWN);
    red_button(&L.oct_up, e->hover == Z_OCT_UP);
    float ty = (L.oct_down.top + L.oct_down.bottom) / 2.0f, t = 3.5f * L.s;
    gpt_t l[3] = {{L.oct_down.left - 5 * L.s - t, ty}, {L.oct_down.left - 5 * L.s, ty - t}, {L.oct_down.left - 5 * L.s, ty + t}};
    gpt_t g[3] = {{L.oct_up.right + 5 * L.s + t, ty}, {L.oct_up.right + 5 * L.s, ty - t}, {L.oct_up.right + 5 * L.s, ty + t}};
    gfx_poly(l, 3, 0xFFB4B4B8);
    gfx_poly(g, 3, 0xFFB4B4B8);
    text_c(dc, (L.oct_down.left + L.oct_up.right) / 2, L.oct_down.bottom + sc(3), sc(80), sc(14), "OCTAVE", font_label, T_LABEL);
    wheel(dc, &L.pitch, e->bend, 1, "PITCH", e->hover == Z_PITCH);
    wheel(dc, &L.modw, e->wheel * 2 - 1, 0, "MOD", e->hover == Z_MODW);
    RECT *p = &L.pad;
    gfx_rect((float)p->left, (float)p->top, (float)rw(p), (float)rh(p), 0xFF0B0B0C);
    gfx_frame((float)p->left, (float)p->top, (float)rw(p), (float)rh(p), 1.0f, e->hover == Z_PAD ? 0xFF8A8A8E : 0xFF48484C);
    for (int k = 1; k < 4; k++) {
        float gx = p->left + rw(p) * k / 4.0f, gy = p->top + rh(p) * k / 4.0f;
        gfx_line(gx, p->top + 2.0f, gx, p->bottom - 2.0f, 0.8f, 0x20FFFFFF);
        gfx_line(p->left + 2.0f, gy, p->right - 2.0f, gy, 0.8f, 0x20FFFFFF);
    }
    float px = p->left + e->pad_x * rw(p), py = p->bottom - e->pad_y * rh(p);
    gfx_line(px, (float)p->top + 1, px, (float)p->bottom - 1, 0.8f, e->pad_on ? 0x80A8ECFF : 0x30A8ECFF);
    gfx_line((float)p->left + 1, py, (float)p->right - 1, py, 0.8f, e->pad_on ? 0x80A8ECFF : 0x30A8ECFF);
    gfx_disc(px, py, 4.0f * L.s, e->pad_on ? 0xFFDDF6FF : 0x6088A8B4);
    text_c(dc, (p->left + p->right) / 2, p->bottom + sc(4), sc(80), sc(14), "X/Y PAD", font_label, T_LABEL);
}

static void draw_keys(HDC dc, ed_t *e) {
    RECT *k = &L.keys;
    float kw = rw(k) / (float)KEYS_WHITE, top = (float)k->top, h = (float)rh(k);
    int base = KEY_BASE + 12 * e->oct;
    gfx_rect((float)k->left - 2, top - 4, (float)rw(k) + 4, h + 6, 0xFF0A0A0A);
    for (int j = 0; j < KEYS_WHITE; j++) {
        float x = k->left + j * kw;
        int n = note_of_white(j, base), down = e->key == n;
        gfx_rect_grad(x + 0.5f, top, kw - 1.5f, h, down ? 0xFFB8C0C6 : 0xFFF6F6F3, down ? 0xFFA6AEB4 : 0xFFDADAD6, 0);
        gfx_rect(x + 0.5f, top + h - 5 * L.s, kw - 1.5f, 5 * L.s, down ? 0xFF9AA2A8 : 0xFFC8C8C4);
        if (j % 7 == 0) {
            char lab[8];
            snprintf(lab, sizeof lab, "C%d", n / 12 - 1);
            text_c(dc, (int)(x + kw / 2), (int)(top + h - sc(22)), (int)kw, sc(13), lab, font_tiny, RGB(150, 150, 150));
        }
    }
    static const int has_black[] = {1, 1, 0, 1, 1, 1, 0};
    for (int j = 0; j < KEYS_WHITE - 1; j++) {
        if (!has_black[j % 7]) continue;
        float bw = kw * 0.58f, x = k->left + (j + 1) * kw - bw / 2;
        int n = note_of_white(j, base) + 1, down = e->key == n;
        gfx_rect(x, top, bw, h * 0.62f, 0xFF050505);
        gfx_rect_grad(x + 1.5f, top, bw - 3, h * 0.62f - 5 * L.s, down ? 0xFF505054 : 0xFF2A2A2C, down ? 0xFF38383C : 0xFF121214, 0);
        gfx_rect(x + 2.5f, top + h * 0.62f - 7 * L.s, bw - 5, 3 * L.s, down ? 0xFF606064 : 0xFF3A3A3E);
    }
    gfx_rect_grad((float)k->left, top, (float)rw(k), 7 * L.s, 0xC0000000, 0x00000000, 0);   /* shadow under the panel lip */
}

void paint(ed_t *e, HDC out) {
    HDC dc = CreateCompatibleDC(out);
    HBITMAP bm = CreateCompatibleBitmap(out, L.w, L.h);
    HGDIOBJ obm = SelectObject(dc, bm);
    gfx_begin(dc);
    gfx_rect(0, 0, (float)L.w, (float)L.h, 0xFF111112);
    int cheek = sc(26);
    wood(0, 0, cheek, L.h, 1);
    wood(L.w - cheek, 0, cheek, L.h, 1);
    wood(cheek, sc(550), L.w - 2 * cheek, L.h - sc(550), 0);
    gfx_rect((float)cheek, (float)sc(550), (float)(L.w - 2 * cheek), 3 * L.s, 0x70000000);
    for (int side = 0; side < 2; side++) {   /* the cheeks' inner edges */
        float x = side ? (float)(L.w - cheek) : (float)cheek;
        gfx_line(x, 0, x, (float)L.h, 1.0f, 0x80301808);
    }
    draw_panels(dc);
    for (int w = 0; w < NWAVES; w++) draw_wave(e, &WAVES[w]);
    for (int c = 0; c < NCTLS; c++) {
        int i = CTLS[c].param;
        switch (L.kind[i]) {
        case K_KNOB: case K_SMALL: case K_BIG: draw_knob(dc, e, i); break;
        case K_OCT: draw_octave(dc, e, i); break;
        case K_LEDS: draw_leds(dc, e, i); break;
        case K_TOGGLE: draw_toggle(dc, e, i); break;
        default: draw_button(dc, e, i); break;
        }
    }
    draw_console(dc, e);
    draw_controller(dc, e);
    draw_keys(dc, e);
    gfx_end();
    BitBlt(out, 0, 0, L.w, L.h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, obm);
    DeleteObject(bm);
    DeleteDC(dc);
}

