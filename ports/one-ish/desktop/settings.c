/* The SETTINGS page and the settings file (settings.h), after the Moog One's SETTINGS screen (manual p124-139): global
 * settings that belong to the instrument, not to a sound, so loading a sound or a project never changes them.
 *
 * Tabs (the buttons on the page's first line):
 *   GLOBAL   PRESET AUTOLOAD (the PRESET knob, PREV and NEXT load as they step; off: they only point, and a click on
 *            the knob or the name loads), RECEIVE MIDI CLOCK (off: the host's tempo and song position are ignored and
 *            the Synths run at TEMPO), MIDI I/O FROM PRESET (off: loading a preset or patch keeps the MIDI channels:
 *            MIDI IN (ALL) and each Synth's channel), the CC numbers of the SUSTAIN, EXP 1 and EXP 2 pedals, RESTORE
 *            DEFAULT SETTINGS
 *   LIBRARY  where the patches and this file live, how many there are, the version
 *   TUNING   MASTER FREQUENCY and MASTER FINE TUNE (the shared params: they stay part of the sound, as before)
 *
 * The file: Settings.txt beside the Patches folder (%USERPROFILE%\Documents\ONE-ish\Settings.txt), or the path in the
 * environment variable ONEISH_SETTINGS (load_test). Plain text, "key=value" per line after the header line
 * "ONE-ish settings 1"; unknown keys are skipped, values out of range are clamped. Read once per process when the
 * first instance starts, written after every change (to a .tmp file, then over the old one). UI thread only: the
 * audio thread never touches it. One set for every instance: a change goes to all of them.
 *
 * The engine gets the MIDI settings as a "midicfg" command (the chunk call, as the step editor's commands: the
 * wrapper's engine lock, never the audio path's own work): src/midi_in_run.h. PRESET AUTOLOAD and MIDI I/O FROM PRESET
 * are the window's own (patches.c and editor.c ask here). */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include <shellapi.h>
#include <shlobj.h>

static const char HEADER[] = "ONE-ish settings";
enum { FORMAT_VERSION = 1, FILE_MAX = 65536 };

/* ---- the settings ---- */
enum { SI_AUTOLOAD, SI_RX_CLOCK, SI_TEMPO, SI_MIDI_IO, SI_SUSTAIN, SI_EXP1, SI_EXP2, SI_COUNT };
enum { IK_ONOFF, IK_NUM };
typedef struct { const char *label, *key; int kind; float lo, hi, step, def; const char *unit, *help; } item_t;
static const item_t ITEM[SI_COUNT] = {
    {"PRESET AUTOLOAD", "autoload", IK_ONOFF, 0, 1, 1, 1, "",
     "PRESET AUTOLOAD: On, the PRESET knob, PREV and NEXT load each preset or patch as they step. Off, they only point "
     "at one (its name shows with >); click the PRESET knob or the name to load it."},
    {"RECEIVE MIDI CLOCK", "rx_clock", IK_ONOFF, 0, 1, 1, 1, "",
     "RECEIVE MIDI CLOCK: On, the host's tempo and song position drive synced LFOs, envelopes, effects, the arp and "
     "the sequencer. Off, they are ignored and everything runs free at TEMPO below."},
    {"TEMPO (CLOCK OFF)", "tempo", IK_NUM, 10, 280, 1, 120, " BPM",
     "TEMPO: the tempo ONE-ish keeps itself while RECEIVE MIDI CLOCK is off (10-280 BPM). Scroll or click < >; Shift "
     "for 0.1."},
    {"MIDI I/O FROM PRESET", "midi_io_preset", IK_ONOFF, 0, 1, 1, 1, "",
     "MIDI I/O LOAD FROM PRESET: On, a preset or patch you load brings its MIDI channels (MIDI IN (ALL) and each "
     "Synth's). Off, the channels you set stay as they are whatever you load. A project always restores everything."},
    {"SUSTAIN PEDAL CC", "sustain_cc", IK_NUM, 0, 119, 1, 64, "",
     "SUSTAIN PEDAL: the CC number your sustain pedal sends (64 on most). Its old number then does nothing."},
    {"EXP 1 PEDAL CC", "exp1_cc", IK_NUM, 0, 119, 1, 11, "",
     "EXP 1 PEDAL: the CC number of the pedal the mod matrix reads as Exp 1 (11, expression, on most)."},
    {"EXP 2 PEDAL CC", "exp2_cc", IK_NUM, 0, 119, 1, 4, "",
     "EXP 2 PEDAL: the CC number of the pedal the mod matrix reads as Exp 2 (4, foot controller, on most)."},
};
typedef struct { float v[SI_COUNT]; } settings_t;
static settings_t G;
static int g_loaded;

static void defaults(settings_t *s) {
    for (int k = 0; k < SI_COUNT; k++) s->v[k] = ITEM[k].def;
}

/* CC numbers a pedal can't take: what the engine reads for other things (bank select, mod wheel, the pad, data entry,
 * the ribbon, sostenuto, NRPN and RPN) */
static int cc_fixed(int cc) {
    return cc == 0 || cc == 1 || cc == 2 || cc == 3 || cc == 6 || cc == 16 || cc == 17 || cc == 18 || cc == 32
           || cc == 38 || cc == 66 || (cc >= 96 && cc <= 101) || cc >= 120;
}
static int is_pedal(int k) { return k >= SI_SUSTAIN && k <= SI_EXP2; }

static float clampv(int k, float v) {
    const item_t *it = &ITEM[k];
    if (!isfinite(v)) v = it->def;
    if (v < it->lo) v = it->lo;
    if (v > it->hi) v = it->hi;
    if (it->kind == IK_ONOFF || is_pedal(k)) v = roundf(v);
    return v;
}

/* ---- the file ---- */
int settings_path(char *out, int n) {
    char d[MAX_PATH];
    DWORD k = GetEnvironmentVariableA("ONEISH_SETTINGS", d, sizeof d);
    if (k > 0 && k < sizeof d) return GetFullPathNameA(d, n, out, NULL) > 0;
    if (!patch_dir(d, sizeof d, 0)) return 0;
    char *slash = strrchr(d, '\\');   /* beside the Patches folder */
    if (slash) *slash = 0;
    return snprintf(out, n, "%s\\Settings.txt", d) < n;
}

static void file_text(const settings_t *s, char *out, int n) {
    int used = snprintf(out, n, "%s %d\r\n", HEADER, FORMAT_VERSION);
    for (int k = 0; k < SI_COUNT && used < n; k++)
        used += snprintf(out + used, n - used, "%s=%g\r\n", ITEM[k].key, s->v[k]);
}

static int set_key(settings_t *s, const char *key, const char *val) {   /* 1: a known key */
    for (int k = 0; k < SI_COUNT; k++)
        if (!strcmp(key, ITEM[k].key)) {
            char *end;
            double d = strtod(val, &end);
            if (end != val) s->v[k] = clampv(k, (float)d);
            return 1;
        }
    return 0;
}

static void parse(settings_t *s, char *t) {
    char *save = NULL;
    for (char *line = strtok_r(t, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#' || !strncmp(line, HEADER, sizeof HEADER - 1)) continue;
        *eq = 0;
        set_key(s, line, eq + 1);
    }
}

static void load_file(void) {
    char path[MAX_PATH];
    defaults(&G);
    g_loaded = 1;
    if (!settings_path(path, sizeof path)) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    static char t[FILE_MAX];
    size_t n = fread(t, 1, sizeof t - 1, f);
    fclose(f);
    t[n] = 0;
    parse(&G, t);
}

static int save_file(void) {
    char path[MAX_PATH], tmp[MAX_PATH + 8], dir[MAX_PATH];
    static char t[FILE_MAX];
    if (!settings_path(path, sizeof path)) return 0;
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '\\');
    if (slash) { *slash = 0; SHCreateDirectoryExA(NULL, dir, NULL); }
    file_text(&G, t, sizeof t);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    int ok = fwrite(t, 1, strlen(t), f) == strlen(t);
    ok &= fclose(f) == 0;
    return ok && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

/* ---- into the engine ---- */
static void say(ed_t *e, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->st.msg, sizeof e->st.msg, fmt, ap);
    va_end(ap);
    e->st.msg_at = GetTickCount();
    if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE);
}
const char *settings_message(ed_t *e) {
    return e->st.msg[0] && GetTickCount() - e->st.msg_at < 6000 ? e->st.msg : NULL;
}

static void send(ed_t *e) {   /* the MIDI settings, as the engine's "midicfg" command */
    char b[256];
    snprintf(b, sizeof b, "midicfg\nrx_clock=%d;tempo=%g;sustain=%d;exp1=%d;exp2=%d", (int)G.v[SI_RX_CLOCK],
             G.v[SI_TEMPO], (int)G.v[SI_SUSTAIN], (int)G.v[SI_EXP1], (int)G.v[SI_EXP2]);
    wrapped_call(e->fx, effSetChunk, 0, (intptr_t)strlen(b) + 1, b, 0);
}
static void redraw_one(ed_t *e) { if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE); }
static void apply_all(void) {   /* every instance of this process follows (the settings are global) */
    ed_each(send);
    ed_each(redraw_one);
}

void settings_init(ed_t *e) {   /* a new instance (VSTPluginMain, the host's UI thread) */
    if (!g_loaded) load_file();
    e->st.cue = -1;
    send(e);
}

/* one setting changed (from the page or load_test): kept, sent to every instance, saved */
static void change(ed_t *e, int k, float v) {
    v = clampv(k, v);
    if (is_pedal(k)) {   /* a pedal skips the numbers taken: fixed ones and the other pedals' */
        int dir = v >= G.v[k] ? 1 : -1, cc = (int)v;
        for (int tries = 0; tries < 128; tries++) {
            int taken = cc_fixed(cc);
            for (int p = SI_SUSTAIN; p <= SI_EXP2; p++) taken |= p != k && (int)G.v[p] == cc;
            if (!taken) break;
            cc += dir;
            if (cc < 0 || cc > 119) { cc = (int)G.v[k]; break; }
        }
        v = (float)cc;
    }
    if (v == G.v[k]) return;
    G.v[k] = v;
    apply_all();
    if (!save_file()) say(e, "Couldn't save the settings file (the folder may be read-only); they apply until you close.");
}

/* ---- PRESET AUTOLOAD (patches.c's browsing asks these) ---- */
int settings_autoload(void) { if (!g_loaded) load_file(); return G.v[SI_AUTOLOAD] > 0.5f; }
static void cue_check(ed_t *e) {   /* something else loaded meanwhile: the pointer goes */
    if (e->st.cue >= 0 && (patches_pos(e) != e->st.cue_base || e->st.cue >= patches_total(e))) e->st.cue = -1;
}
int settings_cue_from(ed_t *e, int pos) {
    cue_check(e);
    return e->st.cue >= 0 ? e->st.cue : pos;
}
int settings_cue_step(ed_t *e, int to) {   /* 1: pointed at, not loaded */
    if (settings_autoload()) return 0;
    int now = patches_pos(e);
    e->st.cue = to == now ? -1 : to;
    e->st.cue_base = now;
    redraw_one(e);
    return 1;
}
int settings_cue_title(ed_t *e, char *out, int n) {   /* the name line while pointing: "> F 004  Name" */
    cue_check(e);
    if (e->st.cue < 0) return 0;
    int nf = e->fx->numPrograms, c = e->st.cue;
    if (c < nf) {
        char nm[64] = "";
        wrapped_call(e->fx, effGetProgramNameIndexed, c, 0, nm, 0);
        snprintf(out, n, "> F %03d  %s ?", c + 1, nm);
    } else {
        snprintf(out, n, "> U  %s ?", e->pt.list[c - nf]);
    }
    return 1;
}
int settings_cue_load(ed_t *e) {   /* a click on the PRESET knob or the name: load what it points at (1) */
    cue_check(e);
    if (e->st.cue < 0) return 0;
    int c = e->st.cue;
    e->st.cue = -1;
    patches_load_pos(e, c);
    redraw_one(e);
    return 1;
}

/* ---- MIDI I/O LOAD FROM PRESET off: a load keeps the MIDI channels ---- */
static const char *const MIDI_IO_KEY[] = {"midi_ch", "syn_ch", "s2_syn_ch", "s3_syn_ch"};
enum { NMIDI_IO = sizeof MIDI_IO_KEY / sizeof MIDI_IO_KEY[0] };
static int index_of(const char *key) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, key)) return i;
    return -1;
}
int settings_midi_keep(ed_t *e, float *keep) {   /* before a load: 1 when the channels must stay (keep[NMIDI_IO]) */
    if (!g_loaded) load_file();
    if (G.v[SI_MIDI_IO] > 0.5f) return 0;
    for (int k = 0; k < NMIDI_IO; k++) {
        int i = index_of(MIDI_IO_KEY[k]);
        keep[k] = i >= 0 ? e->fx->getParameter(e->fx, i) : -1.0f;
    }
    return 1;
}
void settings_midi_put(ed_t *e, const float *keep) {   /* after it: the channels back, the host told */
    for (int k = 0; k < NMIDI_IO; k++) {
        int i = index_of(MIDI_IO_KEY[k]);
        if (i < 0 || keep[k] < 0.0f || e->fx->getParameter(e->fx, i) == keep[k]) continue;
        e->fx->setParameter(e->fx, i, keep[k]);
        if (!e->host) continue;
        e->host(e->fx, audioMasterBeginEdit, i, 0, NULL, 0);
        e->host(e->fx, audioMasterAutomate, i, 0, NULL, keep[k]);
        e->host(e->fx, audioMasterEndEdit, i, 0, NULL, 0);
    }
}

static int restore_defaults(ed_t *e) {
    defaults(&G);
    apply_all();
    int ok = save_file();
    say(e, ok ? "Restored the default settings (your patches and sounds are unchanged)."
              : "Restored the default settings, but couldn't save the settings file.");
    return ok;
}

/* ---- the page ---- */
enum { Z_TAB = Z_SET_BASE, Z_ROW = Z_SET_BASE + 100, Z_BTN = Z_SET_BASE + 500 };   /* Z_ROW + row * 4 + part */
enum { PART_MAIN, PART_DOWN, PART_UP };
enum { B_RESTORE, B_OPEN_PATCHES, B_OPEN_SETTINGS, B_COUNT };
static const char *const TAB_NAME[ST_TABS] = {"GLOBAL", "LIBRARY", "TUNING"};
static const char *const TUNING_KEY[] = {"master_tune", "fine_tune"};
enum { NTUNING = sizeof TUNING_KEY / sizeof TUNING_KEY[0] };

int settings_subs(ed_t *e) { (void)e; return 1; }

void settings_title(ed_t *e, char *out, int n) {
    snprintf(out, n, "SETTINGS:  %s", TAB_NAME[e->st.tab]);
}

void settings_open(ed_t *e) {
    e->screen = SCR_SETTINGS;
    e->sub = 0;
    redraw_one(e);
}

static RECT tab_rect(int t) {   /* the tabs on the column-heads line */
    int gap = sc(6), bw = (rw(&L.head) - gap * (ST_TABS - 1)) / ST_TABS;
    RECT r = {L.head.left + t * (bw + gap), L.head.top, L.head.left + t * (bw + gap) + bw, L.head.bottom - sc(1)};
    return r;
}
static void row_parts(int row, RECT *name, RECT *val, RECT *down, RECT *up) {   /* a row: its label and its value */
    RECT r = L.row[row];
    int lw = rw(&r) * 46 / 100, aw = sc(18);
    SetRect(name, r.left + sc(4), r.top, r.left + lw, r.bottom);
    SetRect(val, r.left + lw + sc(4), r.top + sc(2), r.right - sc(4), r.bottom - sc(2));
    SetRect(down, val->left, val->top, val->left + aw, val->bottom);
    SetRect(up, val->right - aw, val->top, val->right, val->bottom);
}
static RECT button_rect(int b) {   /* RESTORE: GLOBAL's last row; the OPEN buttons: LIBRARY's last two, on the right */
    RECT r = L.row[ROWS - 1], nm, d, u;
    if (b == B_RESTORE) InflateRect(&r, -sc(4), -sc(2));
    else row_parts(b == B_OPEN_PATCHES ? ROWS - 2 : ROWS - 1, &nm, &r, &d, &u);
    return r;
}
static int button_shown(ed_t *e, int b) { return b == B_RESTORE ? e->st.tab == ST_GLOBAL : e->st.tab == ST_LIBRARY; }
static int tuning_index(int row) { return row < NTUNING ? index_of(TUNING_KEY[row]) : -1; }

int settings_hit(ed_t *e, POINT pt) {
    for (int t = 0; t < ST_TABS; t++) {
        RECT r = tab_rect(t);
        if (PtInRect(&r, pt)) return Z_TAB + t;
    }
    if (e->st.tab == ST_GLOBAL || e->st.tab == ST_TUNING) {
        int rows = e->st.tab == ST_GLOBAL ? SI_COUNT : NTUNING;
        for (int row = 0; row < rows; row++) {
            RECT nm, v, d, u;
            row_parts(row, &nm, &v, &d, &u);
            int num = e->st.tab == ST_TUNING || ITEM[row].kind == IK_NUM;
            if (num && PtInRect(&d, pt)) return Z_ROW + row * 4 + PART_DOWN;
            if (num && PtInRect(&u, pt)) return Z_ROW + row * 4 + PART_UP;
            RECT all = {nm.left, nm.top, v.right, nm.bottom};
            if (PtInRect(&all, pt)) return Z_ROW + row * 4 + PART_MAIN;
        }
    }
    for (int b = 0; b < B_COUNT; b++) {
        if (!button_shown(e, b)) continue;
        RECT r = button_rect(b);
        if (PtInRect(&r, pt)) return Z_BTN + b;
    }
    return -1;
}

static void tune_step(ed_t *e, int row, int dir, int fine) {   /* MASTER FREQUENCY 1 Hz (0.1), FINE TUNE 1 cent (0.1) */
    int i = tuning_index(row);
    if (i < 0) return;
    float span = PARAMS[i].max - PARAMS[i].min, cur = PARAMS[i].min + e->fx->getParameter(e->fx, i) * span;
    float step = fine ? 0.1f : 1.0f, to = roundf((cur + dir * step) / step) * step;
    float v = (to - PARAMS[i].min) / span;
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    if (e->host) e->host(e->fx, audioMasterBeginEdit, i, 0, NULL, 0);
    e->fx->setParameter(e->fx, i, v);
    if (e->host) {
        e->host(e->fx, audioMasterAutomate, i, 0, NULL, v);
        e->host(e->fx, audioMasterEndEdit, i, 0, NULL, 0);
    }
}

static void step_item(ed_t *e, int k, int dir, int fine) {
    const item_t *it = &ITEM[k];
    if (it->kind == IK_ONOFF) { change(e, k, G.v[k] > 0.5f ? 0.0f : 1.0f); return; }
    float step = fine && !is_pedal(k) ? 0.1f : it->step;
    change(e, k, roundf((G.v[k] + dir * step) / step) * step);
}

static void open_folder(ed_t *e, int settings) {
    char p[MAX_PATH];
    int ok = settings ? settings_path(p, sizeof p) : patch_dir(p, sizeof p, 1);
    if (ok && settings) {   /* the folder the file is in */
        char *slash = strrchr(p, '\\');
        if (slash) *slash = 0;
        SHCreateDirectoryExA(NULL, p, NULL);
    }
    if (!ok || (INT_PTR)ShellExecuteA(e->wnd, "open", p, NULL, NULL, SW_SHOWNORMAL) <= 32)
        say(e, "Couldn't open the folder.");
}

int settings_press(ed_t *e, int z) {   /* 1: z was the page's */
    if (z < Z_SET_BASE || z >= Z_SET_END) return 0;
    int fine = GetKeyState(VK_SHIFT) < 0;
    if (z >= Z_TAB && z < Z_TAB + ST_TABS) {
        e->st.tab = z - Z_TAB;
        e->sub = 0;
    } else if (z >= Z_BTN) {
        int b = z - Z_BTN;
        if (b == B_RESTORE
            && MessageBoxA(e->wnd ? GetAncestor(e->wnd, GA_ROOT) : NULL,
                           "Put every setting back to how ONE-ish came (autoload, MIDI clock, channels from presets, "
                           "pedal numbers)? Your patches and sounds are not touched.",
                           "ONE-ish", MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
            restore_defaults(e);
        if (b == B_OPEN_PATCHES || b == B_OPEN_SETTINGS) open_folder(e, b == B_OPEN_SETTINGS);
    } else if (z >= Z_ROW) {
        int row = (z - Z_ROW) / 4, part = (z - Z_ROW) % 4, dir = part == PART_DOWN ? -1 : 1;
        if (e->st.tab == ST_GLOBAL && row < SI_COUNT && (part != PART_MAIN || ITEM[row].kind == IK_ONOFF))
            step_item(e, row, dir, fine);
        if (e->st.tab == ST_TUNING && part != PART_MAIN) tune_step(e, row, dir, fine);
    }
    redraw_one(e);
    return 1;
}

int settings_wheel(ed_t *e, int z, int dir, int fine) {   /* 1: used (a row's value steps) */
    if (e->screen != SCR_SETTINGS || z < Z_ROW || z >= Z_BTN) return 0;
    int row = (z - Z_ROW) / 4;
    if (e->st.tab == ST_GLOBAL && row < SI_COUNT) {
        if (ITEM[row].kind == IK_ONOFF) change(e, row, dir > 0 ? 1.0f : 0.0f);
        else step_item(e, row, dir, fine);
    }
    if (e->st.tab == ST_TUNING) tune_step(e, row, dir, fine);
    redraw_one(e);
    return 1;
}

#define S_LCD RGB(150, 215, 232)
#define S_DIM RGB(90, 130, 145)
#define S_HOT RGB(255, 255, 255)
#define S_FRAME RGB(0x27, 0x40, 0x4E)
#define S_FRAME_HOT RGB(0x96, 0xD7, 0xE8)
#define S_FILL_HOT RGB(0x16, 0x2C, 0x3A)
#define S_SEL RGB(0x1F, 0x46, 0x5A)
static void fill(HDC dc, RECT r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
static void frame(HDC dc, RECT r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FrameRect(dc, &r, b);
    DeleteObject(b);
}
static void label(HDC dc, RECT r, const char *s, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextA(dc, s, -1, &r, fmt | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}
static void button(HDC dc, RECT r, const char *s, int lit, int hot) {
    if (lit) fill(dc, r, S_SEL);
    else if (hot) fill(dc, r, S_FILL_HOT);
    frame(dc, r, hot || lit ? S_FRAME_HOT : S_FRAME);
    label(dc, r, s, font_cell, hot || lit ? S_HOT : S_LCD, DT_CENTER);
}

/* a row with a value: the label, then the value between < and > (numbers) or in a box that toggles (on/off) */
static void value_row(HDC dc, ed_t *e, int row, const char *name, const char *val, int num, int on) {
    RECT nm, v, d, u;
    row_parts(row, &nm, &v, &d, &u);
    int z = Z_ROW + row * 4, hot = e->hover >= z && e->hover < z + 3;
    label(dc, nm, name, font_cell, hot ? S_HOT : S_DIM, DT_LEFT);
    if (num) {
        label(dc, d, "<", font_screen, e->hover == z + PART_DOWN ? S_HOT : S_LCD, DT_CENTER);
        label(dc, u, ">", font_screen, e->hover == z + PART_UP ? S_HOT : S_LCD, DT_CENTER);
        RECT m = {d.right, v.top, u.left, v.bottom};
        if (hot) frame(dc, m, S_FRAME_HOT);
        label(dc, m, val, font_cell, S_LCD, DT_CENTER);
    } else {
        RECT box = {v.left, v.top, v.left + sc(70), v.bottom};
        button(dc, box, on ? "ON" : "OFF", on, hot);
    }
}

void settings_draw(HDC dc, ed_t *e) {   /* under the title line, which editor_draw.c draws */
    for (int t = 0; t < ST_TABS; t++) button(dc, tab_rect(t), TAB_NAME[t], e->st.tab == t, e->hover == Z_TAB + t);
    char v[MAX_PATH + 32];
    if (e->st.tab == ST_GLOBAL) {
        for (int k = 0; k < SI_COUNT; k++) {
            const item_t *it = &ITEM[k];
            if (is_pedal(k)) snprintf(v, sizeof v, "CC %d", (int)G.v[k]);
            else snprintf(v, sizeof v, "%g%s", G.v[k], it->unit);
            value_row(dc, e, k, it->label, v, it->kind == IK_NUM, G.v[k] > 0.5f);
        }
        button(dc, button_rect(B_RESTORE), "RESTORE DEFAULT SETTINGS", 0, e->hover == Z_BTN + B_RESTORE);
    } else if (e->st.tab == ST_LIBRARY) {
        char pd[MAX_PATH], sp[MAX_PATH];
        if (!patch_dir(pd, sizeof pd, 0)) snprintf(pd, sizeof pd, "(no Documents or AppData folder found)");
        if (!settings_path(sp, sizeof sp)) snprintf(sp, sizeof sp, "(none)");
        patches_rescan(e);
        const char *name[6] = {"PATCH FOLDER", "USER PATCHES", "FACTORY PRESETS", "SETTINGS FILE", "VERSION", "BUILT"};
        char val[6][MAX_PATH + 32];
        snprintf(val[0], sizeof val[0], "%s", pd);
        snprintf(val[1], sizeof val[1], "%d", e->pt.count);
        snprintf(val[2], sizeof val[2], "%d", e->fx->numPrograms);
        snprintf(val[3], sizeof val[3], "%s", sp);
        snprintf(val[4], sizeof val[4], "%s %d.%d.%d  (VST2, %d params)", PLUG_NAME, PLUG_VERSION / 1000,
                 PLUG_VERSION / 100 % 10, PLUG_VERSION % 100, NPARAMS);
        snprintf(val[5], sizeof val[5], "%s %s", __DATE__, __TIME__);
        for (int r = 0; r < 6; r++) {
            RECT nm, vr, d, u;
            row_parts(r, &nm, &vr, &d, &u);
            label(dc, nm, name[r], font_cell, S_DIM, DT_LEFT);
            label(dc, vr, val[r], font_cell, S_LCD, r == 0 || r == 3 ? DT_PATH_ELLIPSIS | DT_LEFT : DT_LEFT);
        }
        RECT a = L.row[6], b = L.row[7];
        a.left += sc(4);
        b.left += sc(4);
        a.right = b.right = button_rect(B_OPEN_PATCHES).left - sc(4);
        label(dc, a, "Your patches:", font_cell, S_DIM, DT_LEFT);
        label(dc, b, "This settings file:", font_cell, S_DIM, DT_LEFT);
        button(dc, button_rect(B_OPEN_PATCHES), "OPEN PATCH FOLDER", 0, e->hover == Z_BTN + B_OPEN_PATCHES);
        button(dc, button_rect(B_OPEN_SETTINGS), "OPEN SETTINGS FOLDER", 0, e->hover == Z_BTN + B_OPEN_SETTINGS);
    } else {
        static const char *const NAME[NTUNING] = {"MASTER FREQUENCY", "MASTER FINE TUNE"};
        for (int r = 0; r < NTUNING; r++) {
            int i = tuning_index(r);
            if (i < 0) continue;
            param_text(e, i, v, sizeof v);
            value_row(dc, e, r, NAME[r], v, 1, 0);
        }
    }
}

const char *settings_zone_help(ed_t *e, int z) {
    if (z < Z_SET_BASE || z >= Z_SET_END) return NULL;
    if (z >= Z_TAB && z < Z_TAB + ST_TABS) {
        static const char *const H[ST_TABS] = {
            "GLOBAL: how the instrument behaves: preset autoload, the MIDI clock, channels from presets, the pedals.",
            "LIBRARY: where your patches and the settings file are, how many patches, the version.",
            "TUNING: the master frequency (the A above middle C) and the master fine tune."};
        return H[z - Z_TAB];
    }
    if (z == Z_BTN + B_RESTORE) return "RESTORE DEFAULT SETTINGS: every setting on this page back to how ONE-ish came. "
                                       "Patches, presets and sounds are not changed.";
    if (z == Z_BTN + B_OPEN_PATCHES) return "Opens the folder with your patch files in Explorer.";
    if (z == Z_BTN + B_OPEN_SETTINGS) return "Opens the folder with the settings file in Explorer.";
    int row = (z - Z_ROW) / 4;
    if (e->st.tab == ST_GLOBAL && z >= Z_ROW && row < SI_COUNT) return ITEM[row].help;
    if (e->st.tab == ST_TUNING && z >= Z_ROW && row == 0)
        return "MASTER FREQUENCY: the pitch of the A above middle C, 400-480 Hz (440 standard). Click < > or scroll: "
               "1 Hz, Shift 0.1 Hz. Part of the sound (shared by its three Synths).";
    if (e->st.tab == ST_TUNING && z >= Z_ROW && row == 1)
        return "MASTER FINE TUNE: moves everything up or down by up to 100 cents (a semitone). Click < > or scroll: "
               "1 cent, Shift 0.1.";
    return NULL;
}

void settings_help(ed_t *e, char *out, int n) {
    if (e->st.tab == ST_LIBRARY)
        snprintf(out, n, "Your patches are files in the patch folder (PATCHES page: save, import, export). The "
                         "settings on this page are kept in the settings file, apart from any sound.");
    else if (e->st.tab == ST_TUNING)
        snprintf(out, n, "Tuning for the whole sound. Click < > or scroll over a value (Shift: finer).");
    else
        snprintf(out, n, "Settings kept apart from the sounds: loading a sound or a project never changes them. Click "
                         "a value to change it (or scroll over it). They are saved at once, for every ONE-ish.");
}

/* ---- load_test (effVendorSpecific TEST_SETTINGS; buf holds 1 KB) ---- */
intptr_t settings_test(ed_t *e, intptr_t op, char *buf, float opt) {
    switch (op) {
    case STT_GET: file_text(&G, buf, 1024); return 1;
    case STT_SET: {
        char k[64];
        const char *eq = strchr(buf, '=');
        if (!eq || eq - buf >= (int)sizeof k) return 0;
        snprintf(k, sizeof k, "%.*s", (int)(eq - buf), buf);
        for (int i = 0; i < SI_COUNT; i++)
            if (!strcmp(k, ITEM[i].key)) { change(e, i, (float)atof(eq + 1)); return 1; }
        return 0;
    }
    case STT_RESTORE: return restore_defaults(e);
    case STT_PATH: return settings_path(buf, 1024);
    case STT_RELOAD: load_file(); apply_all(); return 1;
    case STT_CUE: cue_check(e); return e->st.cue;
    case STT_CUE_LOAD: return settings_cue_load(e);
    case STT_TAB: e->st.tab = (int)lroundf(opt) % ST_TABS; settings_open(e); return 1;
    }
    return 0;
}
