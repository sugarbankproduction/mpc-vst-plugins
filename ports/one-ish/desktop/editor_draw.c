/* Drawing for the desktop editor (see editor.c): the panels and their MORE triangles, knobs, LED lists, buttons, the
 * oscillators' wave screens, the centre console's screen (mod matrix, MORE pages), the left-hand controller, the
 * ribbon and the keyboard. All drawn from scratch: GDI for text, GDI+ (gfx.h) for anti-aliased shapes. UI thread only. */
#include <stdio.h>
#include <string.h>
#include "editor.h"
#include "gfx.h"

/* ---- help text: what the control under the mouse does (a key, or "_suffix" for every key ending in it) ---- */
static const char *help_for(int i) {
    static const struct { const char *key, *text; } H[] = {
        {"_fm_src", "Which oscillator frequency-modulates this filter's cutoff (with FM AMT)."},
        {"_src", "Mod source: what moves the destination (envelopes, LFOs, velocity, wheels, aftertouch, the key played, random values, expression pedals, a constant)."},
        {"_dst", "Mod destination: what the source moves. Pitch in semitones, cutoffs in octaves, levels and mix in % of their range."},
        {"_via_amt", "Via amount: how much the via source scales this row (100% = fully; minus turns it upside down)."},
        {"_xf_amt", "Transform amount: how strongly the transform reshapes the source (0% = not at all)."},
        {"_xf", "Transform: reshapes the source before it moves the destination. Limit Low/High clip one side; Squared and Cubed bend it into a curve."},
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
        {"_track", "Pitch tracking: how much this oscillator follows the keys (100% = normal, 0% = the same pitch on every key)."},
        {"_bend_up_off", "Bend up offset: added to the bend range for this oscillator only, so the oscillators can bend by different amounts."},
        {"_bend_dn_off", "Bend down offset: added to the bend range for this oscillator only."},
        {"_sync_src", "Which oscillator restarts this one when Hard Sync is on."},
        {"angle_lfo3", "LFO 3 moves the wave angle of all three oscillators: a moving, animated tone."},
        {"pw_lfo3", "LFO 3 moves the pulse width of all three oscillators (pulse-width modulation)."},
        {"ring_src", "Which two oscillators the ring modulator multiplies (their triangle/saw waves): metallic, bell-like tones."},
        {"fm_amt", "Linear FM: one oscillator wobbles another's frequency at audio rate, for bells, brass and clangs."},
        {"fm_route", "Which oscillator modulates which: 1 > 2, 1 > 3 or 3 > 1. The modulator can be silent in the mixer."},
        {"noise_a", "Noise envelope attack (with ENV on): how fast the noise fades in at each note."},
        {"noise_r", "Noise envelope release (with ENV on): how fast the noise fades out (at once if Sustain is off)."},
        {"noise_sus", "Noise sustain: on, the noise holds while the key is down; off, it fades out after the attack (a chiff)."},
        {"noise_env", "Noise envelope: on, the noise gets its own attack/release; off, it follows the amplifier envelope."},
        {"noise_color_mix", "Colour mix: blends the two noise colours picked with COLOR."},
        {"noise_color_pair", "Noise colours: red (dark), white (bright), purple (very bright). COLOR MIX blends the pair."},
        {"flt_route", "Parallel: the two filters side by side. Series: the SVF feeds the ladder (send sources to the SVF, mix to Ladder)."},
        {"flt_mix", "Balance between the two filters' outputs: 0% all SVF, 100% all ladder."},
        {"_kbd", "Keyboard tracking: how much the cutoff follows the keys (100% = an octave per octave, around middle C)."},
        {"_timing", "Sync timing: with Sync on, the LFO's tempo division straight, dotted or triplet."},
        {"_sync", "Sync: for an oscillator, restart with another oscillator (hard sync); for an LFO, an effect or the delay, lock to the host tempo."},
        {"_range", "LFO range: Slow for long sweeps, Low and Medium for faster wobbles."},
        {"_var", "Variation: each cycle's rate varies a little at random, like an analog LFO."},
        {"_polarity", "Bipolar swings above and below; Unipolar only upwards from 0."},
        {"_phase", "Start phase: where in its cycle the LFO starts when it is reset."},
        {"_delay", "Delay: how long after the note this starts."},
        {"_fade_in", "Fade in: how long the LFO takes to reach full depth after its delay."},
        {"_fade_out", "Fade out: how long the LFO takes to die away (0 = never)."},
        {"_repeats", "Repeats: how many cycles the LFO runs after each reset (Inf = keeps running)."},
        {"_smooth", "Smoothing: rounds off the LFO's steps and corners (S&H becomes a smooth random wander)."},
        {"flt_res", "Ladder resonance: emphasis at the cutoff. Near the top the filter sings on its own as a sine (self-oscillation)."},
        {"flt_drive", "Drive: pushes the ladder's input into saturation for a thicker, growling tone."},
        {"flt_slope", "How steeply the ladder cuts: 24 dB per octave is the classic; 6 dB is gentle."},
        {"voices", "How many notes can sound at once. Mono plays one, legato, last key wins."},
        {"hold", "Hold: notes keep sounding after you let go, until Hold is switched off."},
        {"note_priority", "Mono note priority: with several keys held in Mono, which one sounds (lowest, highest or the last pressed)."},
        {"steal_mode", "Voice stealing: when all voices are busy, which note makes way for a new one."},
        {"bend_up", "How far the pitch wheel bends up, in semitones."},
        {"bend_down", "How far the pitch wheel bends down, in semitones."},
        {"vel_curve", "Velocity curve: how hard you must play for loud notes (Soft: little effort; Hard: a firm touch)."},
        {"transpose", "Transposes everything you play, in semitones."},
        {"octave_shift", "Shifts everything you play by octaves (the keyboard's OCTAVE buttons only move the on-screen keys)."},
        {"key_center", "Key tracking centre: the note around which key tracking turns cutoffs and envelopes neither up nor down."},
        {"master_tune", "Master tune: the pitch of A, 440 Hz as standard."},
        {"fine_tune", "Fine tune, in cents."},
        {"modfx_type", "The synth effect: delays, flangers, choruses, phasers, a bit crusher or a resonator. Its MORE page shows its own controls."},
        {"modfx_ctrl1", "CTRL 1: moves the effect's most useful control (or the one set as its target on the MORE page), plus or minus."},
        {"modfx_ctrl2", "CTRL 2: moves a second effect control (its target is on the MORE page)."},
        {"modfx_c1_dst", "CTRL 1 target: the effect control CTRL 1 moves (Auto: the most useful one for the type)."},
        {"modfx_c2_dst", "CTRL 2 target: the effect control CTRL 2 moves (Auto: a second useful one for the type)."},
        {"modfx_gain", "Wet gain: the level of the effect's output (50% = as it comes)."},
        {"mfx_series", "Delay > Reverb: how much of the delay goes on into the reverb."},
        {"mfx_width", "Width of the master effects: 0% mono, 100% normal, 200% extra wide."},
        {"dly_pp", "Ping-pong: each echo crosses to the other side."},
        {"rev_freeze", "Freeze: the reverb holds its tail forever (and takes no new sound) while on."},
        {"pan", "Pan: where the sound sits between left and right."},
        {"spread", "Spread: spreads the voices across the stereo field."},
        {"drift", "Analog drift: slow, slight wandering of pitch and tone, different on every voice."},
        {"ribbon_dst", "What the ribbon moves (its own destination, apart from the mod matrix)."},
        {"ribbon_amt", "How far the ribbon moves its destination."},
        {"ribbon_mode", "Spring: lets go back to the origin when released. Hold: stays where you left it."},
        {"ribbon_origin", "Where the ribbon rests (its zero point)."},
        {"pitch_lfo", "LFO 1 to the pitch of all three oscillators (vibrato). The mod wheel also adds vibrato through matrix row 1."},
    };
    const char *key = PARAMS[i].key;
    size_t kl = strlen(key);
    if (!strncmp(key, "an_", 3)) return "Analog character: how much the voices differ from each other in this respect (0 = all exactly alike).";
    for (unsigned h = 0; h < sizeof H / sizeof H[0]; h++)   /* the key itself first, then the first matching ending */
        if (H[h].key[0] != '_' && !strcmp(key, H[h].key)) return H[h].text;
    for (unsigned h = 0; h < sizeof H / sizeof H[0]; h++) {
        size_t hl = strlen(H[h].key);
        if (H[h].key[0] == '_' && kl > hl && !strcmp(key + kl - hl, H[h].key)) return H[h].text;
    }
    return NULL;
}

static const char *zone_help(int z) {
    if (z >= Z_PATCH_ACT) return patches_zone_help(z);   /* the PATCHES page */
    if (z >= Z_PAGE && z < Z_PAGE + NPAGES) return "MORE: shows this module's other controls on the screen (click again for its next page, if any).";
    switch (z) {
    case Z_KEYS: return "Keyboard: click a key to play it (lower on the key = louder), drag across to slide between notes. "
                        "The octave buttons shift it.";
    case Z_RIBBON: return "Ribbon: touch and slide along it. It sends MIDI CC 16 (position, left to right) and CC 17 (touched). "
                          "Its settings: the triangle at its right end.";
    case Z_PITCH: return "Pitch wheel: drag up or down to bend; it springs back to the centre. BEND + and BEND - set the range.";
    case Z_MODW: return "Mod wheel (MIDI CC 1): drag up to bring in the mod matrix's Mod Wheel rows (vibrato in most presets).";
    case Z_PAD: return "X/Y pad: drag inside it. Up/down sends aftertouch (the matrix source Aftertouch; back to 0 when you let go). "
                       "Left/right sends MIDI CC 2.";
    case Z_OCT_DOWN: case Z_OCT_UP: return "Octave: shifts the on-screen keyboard down or up, two octaves each way.";
    case Z_ENCODER: return "Preset knob: drag up/down or scroll to step through the factory presets, then your own "
                           "patches; click it for the patch list.";
    case Z_PREV: case Z_NEXT: return "Previous / next patch: the factory presets (F), then your own (U).";
    case Z_NAME: return "The patch playing now (F: factory, U: yours; * = changed since it was loaded or saved). Click "
                        "for the patch list, where you can also save, rename, delete, import and export.";
    case Z_TAB_MATRIX: return "MATRIX: the 16 mod matrix rows, 8 at a time (click again, or scroll over the screen, for the others).";
    case Z_TAB_MORE: return "MORE: the list of every module's MORE page. The small triangles on the modules open them directly.";
    case Z_TAB_ANALOG: return "ANALOG: the analog character page (drift and per-voice differences).";
    case Z_SUB_PREV: case Z_SUB_NEXT: return "The previous / next part of this page.";
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

/* the MORE triangles in the modules' corners: black, rimmed; lit while their page is on the screen */
static void draw_mores(ed_t *e) {
    for (int k = 0; k < NMORES; k++) {
        const RECT *r = &L.more[k];
        int open = e->screen == MORES[k].page, hot = e->hover == Z_PAGE + MORES[k].page;
        gpt_t t[3] = {{(float)r->left, (float)r->top}, {(float)r->right, (float)r->top}, {(float)r->right, (float)r->bottom}};
        if (open) {
            gpt_t g[3] = {{t[0].x - 3, t[0].y - 2}, {t[1].x + 2, t[1].y - 2}, {t[2].x + 2, t[2].y + 3}};
            gfx_poly(g, 3, 0x40FFFFF0);
        }
        gfx_poly(t, 3, open ? 0xFFF4F4EE : 0xFF050506);
        gfx_line(t[0].x, t[0].y, t[1].x, t[1].y, 1.0f, hot ? 0xFFD0D0D4 : 0xFF5A5A5E);
        gfx_line(t[0].x, t[0].y, t[2].x, t[2].y, 1.0f, hot ? 0xFFD0D0D4 : 0xFF3A3A3E);
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
    if (!small) {   /* the value no wider than its cell (and a little), so neighbours' values don't run together */
        char val[64];
        param_text(e, i, val, sizeof val);
        RECT vr = {cx - (rw(b) + sc(8)) / 2, ly + lh, cx + (rw(b) + sc(8)) / 2, ly + lh + vh + 2};
        text(dc, &vr, val, font_value, T_VALUE, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
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

/* ---- the centre screen ---- */
static void screen_amount(HDC dc, ed_t *e, int i, RECT c, int active, int compact) {   /* a bar and the value */
    char val[64];
    param_text(e, i, val, sizeof val);
    float v = e->fx->getParameter(e->fx, i);
    int bip = PARAMS[i].min < 0 && PARAMS[i].max > 0;
    COLORREF col = active ? T_LCD : T_LCD_DIM;
    DWORD fill = active ? 0xFF96D7E8 : 0xFF5A8291;
    if (compact) {   /* the matrix: the value, a thin bar under it */
        float tl = (float)c.left, tr = (float)c.right, by = c.bottom - 2.0f * L.s;
        gfx_rect(tl, by, tr - tl, 2.0f * L.s, 0xFF1C2C36);
        float from = bip ? (tl + tr) / 2 : tl, to = tl + v * (tr - tl);
        gfx_rect(from < to ? from : to, by, fabsf(to - from) + 1, 2.0f * L.s, fill);
        RECT vr = {c.left, c.top, c.right, c.bottom - sc(2)};
        text(dc, &vr, val, font_cell, col, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    } else {
        int vw = sc(46), cy = (c.top + c.bottom) / 2;
        float tl = (float)c.left, tr = (float)(c.right - vw - sc(3));
        gfx_rect(tl, cy - 3.0f * L.s, tr - tl, 6.0f * L.s, 0xFF1C2C36);
        float from = bip ? (tl + tr) / 2 : tl, to = tl + v * (tr - tl);
        gfx_rect(from < to ? from : to, cy - 3.0f * L.s, fabsf(to - from), 6.0f * L.s, fill);
        RECT vr = {c.right - vw, c.top, c.right, c.bottom};
        text(dc, &vr, val, font_cell, col, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (i == e->hover && e->hit_screen) gfx_frame((float)c.left - 1, (float)c.top, (float)rw(&c) + 2, (float)rh(&c), 1.0f, 0x6096D7E8);
}

static void screen_menu(HDC dc, ed_t *e, int i, RECT c, int active, HFONT f) {   /* an option list: its value */
    char val[64];
    param_text(e, i, val, sizeof val);
    if (i == e->hover && e->hit_screen) gfx_frame((float)c.left, (float)c.top, (float)rw(&c), (float)rh(&c), 1.0f, 0xFF96D7E8);
    RECT tr = {c.left + sc(3), c.top, c.right, c.bottom};
    text(dc, &tr, val, f, active ? T_LCD : T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void title_line(HDC dc, ed_t *e, const char *t, int subs) {
    RECT r = L.tabline;
    if (subs > 1) r.right = L.sub_prev.left - sc(4);
    text(dc, &r, t, font_tiny, T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (subs <= 1) return;
    char n[16];
    if (e->screen == SCR_MATRIX) snprintf(n, sizeof n, "%d-%d", e->sub * ROWS + 1, e->sub * ROWS + ROWS);   /* rows */
    else snprintf(n, sizeof n, "%d / %d", e->sub + 1, subs);
    RECT nr = {L.sub_prev.right, L.tabline.top, L.sub_next.left, L.tabline.bottom};
    text(dc, &nr, n, font_tiny, T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &L.sub_prev, "<", font_screen, e->hover == Z_SUB_PREV ? T_HOT : T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &L.sub_next, ">", font_screen, e->hover == Z_SUB_NEXT ? T_HOT : T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void draw_matrix(HDC dc, ed_t *e, int subs) {
    title_line(dc, e, "MOD MATRIX:  SOURCE moves DESTINATION by AMOUNT x VIA", subs);
    static const char *const head[] = {"#", "SOURCE", "DESTINATION", "AMOUNT", "VIA", "V.AMT", "XFORM", "X.AMT"};
    for (int c = 0; c < MX_COLS + 1; c++) {
        RECT h = {L.head.left + sc(MXC[c]), L.head.top, L.head.left + sc(MXC[c + 1]) - sc(4), L.head.bottom};
        int right = c == 1 + MX_AMT || c == 1 + MX_VIA_AMT || c == 1 + MX_XF_AMT;
        text(dc, &h, head[c], font_tiny, T_LCD_DIM, (right ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE);
    }
    for (int r = 0; r < ROWS; r++) {
        int m = e->sub * ROWS + r;
        if (m >= NMATRIX) break;
        const short *row = MATRIX[m];
        int active = option_of(e, row[MX_SRC]) > 0 && option_of(e, row[MX_DST]) > 0;
        char num[8];
        snprintf(num, sizeof num, "%d", m + 1);
        RECT nr = matrix_cell(r, 0);
        nr.right += sc(4);
        text(dc, &nr, num, font_cell, active ? T_LCD : T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        for (int k = 0; k < MX_COLS; k++) {
            int i = row[k];
            if (i < 0) continue;
            RECT c = matrix_cell(r, k + 1);
            if (PARAMS[i].nopts) screen_menu(dc, e, i, c, active, font_cell);
            else screen_amount(dc, e, i, c, active, 1);
        }
    }
}

static void draw_page(HDC dc, ed_t *e, int subs) {
    int items[512], n = page_items(e, e->screen, items);
    char t[128];
    snprintf(t, sizeof t, "MORE:  %s", PAGES[e->screen].title);
    title_line(dc, e, t, subs);
    text(dc, &L.head, PAGES[e->screen].note, font_tiny, T_LCD_DIM, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    for (int j = e->sub * SLOTS; j < n && j < (e->sub + 1) * SLOTS; j++) {
        const lay_item_t *it = &ITEMS[items[j]];
        RECT nm, c;
        slot_rects(j % SLOTS, &nm, &c);
        text(dc, &nm, it->label, font_cell, it->param == e->hover ? T_HOT : T_LCD_DIM,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (PARAMS[it->param].nopts) screen_menu(dc, e, it->param, c, 1, font_cell);
        else screen_amount(dc, e, it->param, c, 1, 0);
    }
}

static void draw_index(HDC dc, ed_t *e, int subs) {
    title_line(dc, e, "MORE:  every module's second-layer controls", subs);
    text(dc, &L.head, "Click a page (or a module's triangle on the panel).", font_tiny, T_LCD_DIM,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    for (int p = e->sub * SLOTS; p < NPAGES && p < (e->sub + 1) * SLOTS; p++) {
        RECT nm, c;
        slot_rects(p % SLOTS, &nm, &c);
        RECT all = {nm.left, nm.top + sc(2), c.right, nm.bottom - sc(2)};
        int hot = e->hover == Z_PAGE + p;
        gfx_frame((float)all.left, (float)all.top, (float)rw(&all), (float)rh(&all), 1.0f, hot ? 0xFF96D7E8 : 0xFF27404E);
        RECT tr = {all.left + sc(6), all.top, all.right - sc(16), all.bottom};
        text(dc, &tr, PAGES[p].title, font_cell, hot ? T_HOT : T_LCD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT ar = {all.right - sc(16), all.top, all.right - sc(4), all.bottom};
        text(dc, &ar, ">", font_cell, T_LCD_DIM, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

static void draw_screen(HDC dc, ed_t *e) {
    RECT *s = &L.screen;
    gfx_rect((float)s->left - 3, (float)s->top - 3, (float)rw(s) + 6, (float)rh(s) + 6, 0xFF050505);
    gfx_rect((float)s->left, (float)s->top, (float)rw(s), (float)rh(s), C_SCREEN);
    char title[96];
    patches_title(e, title, sizeof title);   /* "F 001  Init", "U  Bright Pad *" (patches.c) */
    gfx_frame((float)L.name.left, (float)L.name.top, (float)rw(&L.name), (float)rh(&L.name), 1.2f, 0xFFDCE6EE);
    text(dc, &L.name, title, font_big, e->hover == Z_NAME ? T_HOT : T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &L.prev, "<", font_big, T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text(dc, &L.next, ">", font_big, T_LCD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (e->screen >= NPAGES) e->screen = SCR_MATRIX;
    int subs = screen_subs(e);
    if (e->sub >= subs) e->sub = subs - 1;   /* the page got shorter (another effect type) */
    for (int r = 0; r < ROWS; r++)
        if (r % 2) gfx_rect((float)L.row[r].left, (float)L.row[r].top, (float)rw(&L.row[r]), (float)rh(&L.row[r]), 0xFF0F1E2A);
    if (e->screen == SCR_MATRIX) draw_matrix(dc, e, subs);
    else if (e->screen == SCR_INDEX) draw_index(dc, e, subs);
    else if (e->screen == SCR_PATCHES) {
        title_line(dc, e, "PATCHES:  factory presets and your own sounds", subs);
        patches_draw(dc, e);
    }
    else draw_page(dc, e, subs);
    /* help: what the control under the mouse does */
    char help[480];
    const char *zh = zone_help(e->hover), *pm = patches_message(e);
    if (pm) {   /* what the last patch operation did (or why it couldn't) */
        snprintf(help, sizeof help, "%s", pm);
    } else if (e->hover >= 0 && e->hover < NPARAMS) {
        char val[64];
        param_text(e, e->hover, val, sizeof val);
        const char *h = help_for(e->hover);
        snprintf(help, sizeof help, "%s: %s%s%s", PARAMS[e->hover].name, val, h ? "\n" : "", h ? h : "");
    } else if (zh) {
        snprintf(help, sizeof help, "%s", zh);
    } else if (e->screen == SCR_PATCHES) {
        patches_help(e, help, sizeof help);
    } else if (e->screen == SCR_MATRIX) {
        snprintf(help, sizeof help, "Each row: SOURCE x AMOUNT moves DESTINATION. VIA scales it by a second source "
                                    "(V.AMT: how much); XFORM reshapes the source (X.AMT: how strongly). Click a value "
                                    "for its list, drag an amount sideways. MATRIX again: rows 9-16.");
    } else {
        snprintf(help, sizeof help, "Drag a knob up/down (Shift: fine), scroll to nudge, double-click to reset. Click a "
                                    "red button to step, an LED to pick, right-click for a list. A module's corner "
                                    "triangle shows its MORE page here. Play the keys and the ribbon below.");
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
    panel_button(dc, &L.tab_matrix, "MATRIX", e->screen == SCR_MATRIX, e->hover == Z_TAB_MATRIX);
    panel_button(dc, &L.tab_more, "MORE", e->screen >= 0 && e->screen != PG_ANALOG ? 1 : e->screen == SCR_INDEX,
                 e->hover == Z_TAB_MORE);
    panel_button(dc, &L.tab_analog, "ANALOG", PG_ANALOG >= 0 && e->screen == PG_ANALOG, e->hover == Z_TAB_ANALOG);
    panel_button(dc, &L.bprev, "PREV", 0, e->hover == Z_PREV);
    panel_button(dc, &L.bnext, "NEXT", 0, e->hover == Z_NEXT);
    int cx = (L.encoder.left + L.encoder.right) / 2, r = sc(31);
    int pos = patches_pos(e), np = patches_total(e);   /* factory presets, then user patches */
    knob((float)cx, (float)(L.encoder.top + sc(8) + r), (float)r, np > 1 && pos > 0 ? (float)pos / (np - 1) : 0.0f, 0);
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

/* the ribbon: a slim black strip in a brushed-silver rim over the keys, a glowing marker where it is touched */
static void draw_ribbon(HDC dc, ed_t *e) {
    RECT *r = &L.ribbon;
    float x = (float)r->left, y = (float)r->top, w = (float)rw(r), h = (float)rh(r), b = 2.5f * L.s;
    gfx_rect(x - 2, y - 2, w + 4, h + 4, 0xFF0A0A0A);
    gfx_rect_grad(x, y, w, h, 0xFFE2E3E5, 0xFF8C8E92, 0);
    gfx_rect_grad(x + b, y + b, w - 2 * b, h - 2 * b, 0xFF050506, 0xFF1C1C1E, 0);
    for (int k = 1; k < 24; k++) {   /* faint tick marks along it, a longer one at the centre */
        float tx = x + b + (w - 2 * b) * k / 24.0f, th = k == 12 ? h - 2 * b - 2 : (h - 2 * b) * 0.3f;
        gfx_line(tx, y + h - b - 1, tx, y + h - b - 1 - th, 1.0f, k == 12 ? 0x50FFFFFF : 0x26FFFFFF);
    }
    RECT lr = {(int)(x + b + sc(6)), (int)y, (int)(x + sc(80)), (int)(y + h)};
    text(dc, &lr, "RIBBON", font_tiny, e->hover == Z_RIBBON ? T_LABEL : RGB(96, 96, 100), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    float mx = x + b + e->ribbon * (w - 2 * b), my = y + h / 2;
    if (e->ribbon_on) {
        gfx_rect(mx - 9 * L.s, y + b, 18 * L.s, h - 2 * b, 0x3070D8FF);
        gfx_rect(mx - 4 * L.s, y + b, 8 * L.s, h - 2 * b, 0x6090E4FF);
        gfx_line(mx, y + b, mx, y + h - b, 2.0f * L.s, 0xFFE0F8FF);
        gfx_disc(mx, my, 3.0f * L.s, 0xFFFFFFFF);
    } else {
        gfx_line(mx, y + b + 1, mx, y + h - b - 1, 1.2f, 0x6088A8B4);
    }
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
    gfx_rect_grad((float)k->left, top, (float)rw(k), 7 * L.s, 0xC0000000, 0x00000000, 0);   /* shadow under the lip */
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
    draw_mores(e);
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
    draw_ribbon(dc, e);
    draw_keys(dc, e);
    gfx_end();
    BitBlt(out, 0, 0, L.w, L.h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, obm);
    DeleteObject(bm);
    DeleteDC(dc);
}
