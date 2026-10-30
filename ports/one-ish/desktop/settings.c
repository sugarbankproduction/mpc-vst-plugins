/* The SETTINGS page and the settings file (settings.h), after the Moog One's SETTINGS screen (manual p124-139): global
 * settings that belong to the instrument, not to a sound, so loading a sound or a project never changes them.
 *
 * Tabs (the buttons on the page's first line):
 *   GLOBAL   PRESET AUTOLOAD (the PRESET knob, PREV and NEXT load as they step; off: they only point, and a click on
 *            the knob or the name loads), SHOW BANK PREVIEW (library.c's SET page: off, a bank shows only its numbers),
 *            RECEIVE MIDI CLOCK (off: the host's tempo and song position are ignored and the Synths run at TEMPO),
 *            MIDI I/O FROM PRESET (off: loading a preset or patch keeps the MIDI channels: MIDI IN (ALL) and each
 *            Synth's channel), the CC numbers of the SUSTAIN, EXP 1 and EXP 2 pedals
 *   MIDI     MIDI LEARN (manual p144-148): LEARN, click a control, move a knob or fader on the controller: that CC
 *            now moves the control (7-bit, or 14-bit when its LSB, CC + 32, follows). The map lists every mapping
 *            (7/14-BIT, DELETE, CLEAR ALL) and the reserved CCs. The map is a setting like the others (the manual
 *            keeps it with the global settings, not with a sound). NRPN input (the manual's table) needs nothing here.
 *   LIBRARY  where the patches and this file live, how many there are, the version, RESTORE DEFAULT SETTINGS
 *   TUNING   MASTER FREQUENCY and MASTER FINE TUNE (the shared params: they stay part of the sound, as before), and
 *            MICROTUNING (package 16, beyond the manual): the focused Synth's Scala scale (.scl) and keyboard map
 *            (.kbm), read here (scala.c) and kept in the sound's state by the engine (src/tuning.h)
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
#include "scala.h"
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

static const char HEADER[] = "ONE-ish settings";
enum { FORMAT_VERSION = 1, FILE_MAX = 65536 };

/* ---- the settings ---- */
enum { SI_AUTOLOAD, SI_BANK_PREVIEW, SI_RX_CLOCK, SI_TEMPO, SI_MIDI_IO, SI_SUSTAIN, SI_EXP1, SI_EXP2, SI_COUNT };
enum { IK_ONOFF, IK_NUM };
typedef struct { const char *label, *key; int kind; float lo, hi, step, def; const char *unit, *help; } item_t;
static const item_t ITEM[SI_COUNT] = {
    {"PRESET AUTOLOAD", "autoload", IK_ONOFF, 0, 1, 1, 1, "",
     "PRESET AUTOLOAD: On, the PRESET knob, PREV and NEXT load each preset or patch as they step. Off, they only point "
     "at one (its name shows with >); click the PRESET knob or the name to load it."},
    {"SHOW BANK PREVIEW", "bank_preview", IK_ONOFF, 0, 1, 1, 1, "",
     "SHOW BANK PREVIEW: On, a bank letter on the SET page shows the names of its eight patches. Off, only the numbers "
     "(and the one playing); a number still loads its patch."},
    {"RECEIVE MIDI CLOCK", "rx_clock", IK_ONOFF, 0, 1, 1, 1, "",
     "RECEIVE MIDI CLOCK: On, the host's tempo and song position drive synced LFOs, envelopes, effects, the arp and "
     "the sequencer. Off, they are ignored and everything runs free at TEMPO below."},
    {"TEMPO (CLOCK OFF)", "tempo", IK_NUM, 10, 280, 1, 120, " BPM",
     "TEMPO: the tempo Moo Won keeps itself while RECEIVE MIDI CLOCK is off (10-280 BPM). Scroll or click < >; Shift "
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
typedef struct { int cc, bits; char key[48]; } mapping_t;   /* a learned CC: the param's key, 7 or 14 bits */
typedef struct {
    float v[SI_COUNT];
    mapping_t map[MAP_MAX];   /* MIDI LEARN, in the order learned */
    int nmap;
} settings_t;
static settings_t G;
static int g_loaded;
/* (2026-10-30, parts.c) the user library in use ("" the default one) and DUPLICATE NAMES (DUP_*), on the LIBRARY tab:
 * lines "library=<name>" and "import_dup=<n>" of the file. RESTORE DEFAULT SETTINGS leaves both as they are. */
static char g_space[PATCH_NAME_MAX + 1];
static int g_dup;

static void defaults(settings_t *s) {
    for (int k = 0; k < SI_COUNT; k++) s->v[k] = ITEM[k].def;
    s->nmap = 0;
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
    if (!patch_home_dir(d, sizeof d, 0)) return 0;   /* (the default library's, whichever is in use: 2026-10-30) */
    char *slash = strrchr(d, '\\');   /* beside the Patches folder */
    if (slash) *slash = 0;
    return snprintf(out, n, "%s\\Settings.txt", d) < n;
}

static void file_text(const settings_t *s, char *out, int n) {
    int used = snprintf(out, n, "%s %d\r\n", HEADER, FORMAT_VERSION);
    for (int k = 0; k < SI_COUNT && used < n; k++)
        used += snprintf(out + used, n - used, "%s=%g\r\n", ITEM[k].key, s->v[k]);
    for (int k = 0; k < s->nmap && used < n; k++)   /* "map=<cc>,<param key>,<7|14>" */
        used += snprintf(out + used, n - used, "map=%d,%s,%d\r\n", s->map[k].cc, s->map[k].key, s->map[k].bits);
    if (s == &G && used < n) used += snprintf(out + used, n - used, "import_dup=%d\r\n", g_dup);
    if (s == &G && g_space[0] && used < n) snprintf(out + used, n - used, "library=%s\r\n", g_space);
}
static int map_add(const char *key, int cc, int bits);
static int map_drop_cc(int cc);

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
        if (!strcmp(line, "map")) {   /* a learned CC: kept when its param exists and its CC is free */
            int cc, bits, at = 0;
            char key[48];
            if (s == &G && sscanf(eq + 1, "%d,%47[^,],%d%n", &cc, key, &bits, &at) == 3 && at > 0) map_add(key, cc, bits);
            continue;
        }
        if (s == &G && !strcmp(line, "library")) {   /* (2026-10-30) a user library that still exists, else the default */
            char d[MAX_PATH];
            snprintf(g_space, sizeof g_space, "%s", patch_name_ok(eq + 1, NULL, 0) ? eq + 1 : "");
            if (g_space[0] && !space_dir(g_space, d, sizeof d, 0)) g_space[0] = 0;
            continue;
        }
        if (s == &G && !strcmp(line, "import_dup")) {
            int v = atoi(eq + 1);
            g_dup = v >= 0 && v < DUP_COUNT ? v : DUP_ASK;
            continue;
        }
        set_key(s, line, eq + 1);
    }
}

static void load_file(void) {
    char path[MAX_PATH];
    defaults(&G);
    g_loaded = 1;
    g_space[0] = 0;
    g_dup = DUP_ASK;
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

static void send(ed_t *e) {   /* the MIDI settings and the learned map, as the engine's "midicfg" command */
    static char b[256 + MAP_MAX * 64];
    int used = snprintf(b, sizeof b, "midicfg\nrx_clock=%d;tempo=%g;sustain=%d;exp1=%d;exp2=%d;map=",
                        (int)G.v[SI_RX_CLOCK], G.v[SI_TEMPO], (int)G.v[SI_SUSTAIN], (int)G.v[SI_EXP1], (int)G.v[SI_EXP2]);
    for (int k = 0; k < G.nmap && used < (int)sizeof b; k++)
        used += snprintf(b + used, sizeof b - used, "%s%d:%s:%d", k ? "," : "", G.map[k].cc, G.map[k].key, G.map[k].bits);
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

/* one setting changed (from the page or load_test): kept, sent to every instance, saved (save 0: not yet) */
static void change_ex(ed_t *e, int k, float v, int save);
static void change(ed_t *e, int k, float v) { change_ex(e, k, v, 1); }
static int unsaved;   /* a change not in the file yet (a TEMPO drag) */
static void change_ex(ed_t *e, int k, float v, int save) {
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
    if (v == G.v[k] && !(save && unsaved)) return;
    G.v[k] = v;
    int gone = is_pedal(k) ? map_drop_cc((int)v) : 0;   /* a pedal on a learned CC: the pedal wins */
    apply_all();
    if (gone) say(e, "CC %d was a learned mapping; it is the pedal's now (the mapping is gone).", (int)v);
    unsaved = !save;
    if (save && !save_file()) say(e, "Couldn't save the settings file (the folder may be read-only); they apply until you close.");
}

/* the console's TEMPO readout (hub.c, 2026-10-30): the same two settings as this page's RECEIVE MIDI CLOCK (HOST: on;
 * OWN: off) and TEMPO, so both places always show one value */
int settings_tempo(float *bpm) {
    if (!g_loaded) load_file();
    if (bpm) *bpm = G.v[SI_TEMPO];
    return G.v[SI_RX_CLOCK] < 0.5f;
}
void settings_tempo_set(ed_t *e, int own, float bpm, int save) {
    if (!g_loaded) load_file();
    change_ex(e, SI_TEMPO, roundf(bpm * 10.0f) / 10.0f, 0);
    change_ex(e, SI_RX_CLOCK, own ? 0.0f : 1.0f, save);   /* (saves both, when asked to) */
}

/* ---- (2026-10-30) USER LIBRARIES and DUPLICATE NAMES (parts.c) ---- */
int settings_space(char *out, int n) { if (!g_loaded) load_file(); snprintf(out, n, "%s", g_space); return 1; }
int settings_space_set(const char *name) {   /* 1: saved */
    if (!g_loaded) load_file();
    snprintf(g_space, sizeof g_space, "%s", name ? name : "");
    return save_file();
}
int settings_import_dup(void) { if (!g_loaded) load_file(); return g_dup; }
int settings_import_dup_set(int v) {
    if (!g_loaded) load_file();
    g_dup = v >= 0 && v < DUP_COUNT ? v : DUP_ASK;
    return save_file();
}

/* ---- PRESET AUTOLOAD (patches.c's browsing asks these) ---- */
int settings_autoload(void) { if (!g_loaded) load_file(); return G.v[SI_AUTOLOAD] > 0.5f; }
int settings_bank_preview(void) { if (!g_loaded) load_file(); return G.v[SI_BANK_PREVIEW] > 0.5f; }   /* library.c */
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
static const char *const MIDI_IO_KEY[] = {"midi_ch", "syn_ch", "s2_syn_ch", "s3_syn_ch",
                                          /* (2026-10-30) MIDI OUT: on, what it sends, its channel, per Synth */
                                          "mout_on", "s2_mout_on", "s3_mout_on", "mout_send", "s2_mout_send",
                                          "s3_mout_send", "mout_ch", "s2_mout_ch", "s3_mout_ch"};
enum { NMIDI_IO = sizeof MIDI_IO_KEY / sizeof MIDI_IO_KEY[0] };
_Static_assert(NMIDI_IO <= SETTINGS_MIDI_KEEP, "the callers' keep[] holds them all");
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

/* ---- MIDI LEARN: the map (manual p144-148) ---- */
static int map_find_cc(int cc) {   /* the mapping that uses this CC (its own, or a 14-bit pair's LSB), or -1 */
    for (int k = 0; k < G.nmap; k++)
        if (G.map[k].cc == cc || (G.map[k].bits == 14 && G.map[k].cc + 32 == cc)) return k;
    return -1;
}
static int map_find_key(const char *key) {
    for (int k = 0; k < G.nmap; k++)
        if (!strcmp(G.map[k].key, key)) return k;
    return -1;
}
static void map_remove(int k) {
    if (k < 0 || k >= G.nmap) return;
    memmove(&G.map[k], &G.map[k + 1], sizeof G.map[0] * (size_t)(G.nmap - k - 1));
    G.nmap--;
}
static int map_drop_cc(int cc) {   /* 1: a mapping used it and is gone */
    int k = map_find_cc(cc);
    map_remove(k);
    return k >= 0;
}
/* why a CC can't be learned (NULL: it can). The same list as the engine's (src/midi_in.h mi_fixed), plus the pedals */
static const char *reserved_why(int cc) {
    if (cc < 0 || cc > 119) return "a channel mode message";
    if (cc == (int)G.v[SI_SUSTAIN]) return "the sustain pedal";
    if (cc == (int)G.v[SI_EXP1] || cc == (int)G.v[SI_EXP2]) return "an expression pedal";
    switch (cc) {
    case 0: case 32: return "bank select";
    case 1: return "the mod wheel";
    case 2: case 3: case 18: return "the X/Y pad";
    case 6: case 38: return "NRPN data";
    case 16: case 17: return "the ribbon";
    case 66: return "the sostenuto pedal";
    }
    if (cc >= 96 && cc <= 101) return "NRPN and RPN numbers";
    return NULL;
}
static int learnable(int i) {   /* a param a CC can move */
    return i >= 0 && i < NPARAMS && !PARAMS[i].string_display && PARAMS[i].nopts != 1 && !PARAMS[i].momentary;
}
/* a mapping (replacing whatever used the CC, its LSB, or the param). 14-bit only on CC 0-31 with a free LSB */
static int map_add(const char *key, int cc, int bits) {
    int i = index_of(key);
    if (!learnable(i) || reserved_why(cc)) return 0;
    if (bits != 14 || cc > 31 || reserved_why(cc + 32)) bits = 7;
    map_remove(map_find_key(key));
    map_remove(map_find_cc(cc));
    if (bits == 14) map_remove(map_find_cc(cc + 32));
    if (G.nmap >= MAP_MAX) return 0;
    snprintf(G.map[G.nmap].key, sizeof G.map[0].key, "%s", key);
    G.map[G.nmap].cc = cc;
    G.map[G.nmap].bits = bits;
    G.nmap++;
    return 1;
}
static void param_label(int i, char *out, int n) {   /* "S1 Ladder Cutoff", "S2 ...", or a shared one's own name */
    if (shared_param(i) || synth_of(i)) snprintf(out, n, "%s", PARAMS[i].name);
    else snprintf(out, n, "S1 %s", PARAMS[i].name);
}
static void map_changed(ed_t *e) {
    apply_all();
    if (!save_file()) say(e, "Couldn't save the settings file (the folder may be read-only); the map applies until you close.");
}

/* the host's MIDI (any thread: the host's events, before the engine): the last CC, for LEARN. A CC's LSB (CC + 32)
 * right after it marks it 14-bit. NRPN and RPN traffic is not a control to learn. */
void settings_midi_seen(ed_t *e, const void *events) {
    const struct { int32_t n; intptr_t reserved; VstMidiEvent *ev[1]; } *list = events;
    for (int k = 0; k < list->n; k++) {
        const VstMidiEvent *m = list->ev[k];
        if (!m || m->type != 1 || (m->midiData[0] & 0xF0) != 0xB0) continue;
        int cc = m->midiData[1] & 127;
        if (cc == 6 || cc == 38 || (cc >= 96 && cc <= 101)) continue;
        unsigned w = __atomic_load_n(&e->st.seen, __ATOMIC_RELAXED);
        if (cc >= 32 && cc < 64 && (int)(w & 127) == cc - 32) w |= 128;
        else w = (((w >> 8) + 1) << 8) | (unsigned)cc;
        __atomic_store_n(&e->st.seen, w, __ATOMIC_RELEASE);
    }
}

static void learn_cancel(ed_t *e) { e->st.learn = 0; e->st.heard_at = 0; }
static void learn_arm(ed_t *e) {
    e->st.learn = 1;
    e->st.heard_at = 0;
    e->st.tab = ST_MIDI;
    settings_open(e);
    say(e, "LEARN: click the control on the panel you want to move from your controller.");
}
int settings_learn_pick(ed_t *e, int i) {   /* a control clicked while LEARN waits for one: 1 taken (not edited) */
    if (e->st.learn != 1) return 0;
    char nm[64];
    if (!learnable(i)) { say(e, "That control can't be moved by a CC. Click another, or CANCEL."); return 1; }
    e->st.learn = 2;
    e->st.learn_param = i;
    e->st.seen0 = __atomic_load_n(&e->st.seen, __ATOMIC_ACQUIRE);
    e->st.heard_at = 0;
    param_label(i, nm, sizeof nm);
    int k = map_find_key(PARAMS[i].key);
    if (k >= 0) say(e, "%s: now on CC %d (%d-bit). Move a knob or fader on your controller to change it.", nm,
                    G.map[k].cc, G.map[k].bits);
    else say(e, "%s: now move a knob or fader on your controller.", nm);
    e->st.tab = ST_MIDI;
    settings_open(e);
    return 1;
}
/* the timer (every 50 ms): a CC heard since LEARN picked its control is taken 250 ms later (its LSB may follow).
 * force: at once (load_test). Returns the learn stage. */
static int learn_tick(ed_t *e, int force) {
    if (e->st.learn != 2) return e->st.learn;
    unsigned w = __atomic_load_n(&e->st.seen, __ATOMIC_ACQUIRE);
    if (w >> 8 == e->st.seen0 >> 8) return 2;   /* nothing new */
    if (!e->st.heard_at) e->st.heard_at = GetTickCount() | 1;
    if (!force && GetTickCount() - e->st.heard_at < 250) return 2;
    int cc = (int)(w & 127), bits = w & 128 ? 14 : 7;
    const char *why = reserved_why(cc);
    if (why) {
        say(e, "CC %d is reserved (%s). Move another knob or fader, or CANCEL.", cc, why);
        e->st.seen0 = w;
        e->st.heard_at = 0;
        return 2;
    }
    char nm[64];
    int i = e->st.learn_param;
    param_label(i, nm, sizeof nm);
    learn_cancel(e);
    if (!map_add(PARAMS[i].key, cc, bits)) { say(e, "The map is full (%d mappings): DELETE one first.", MAP_MAX); return 0; }
    map_changed(e);
    say(e, "CC %d (%d-bit) now moves %s. LEARN again for another.", cc, G.map[map_find_key(PARAMS[i].key)].bits, nm);
    return 0;
}
void settings_tick(ed_t *e) { learn_tick(e, 0); }

static void map_bits(ed_t *e, int k) {   /* 7 <-> 14 bit (14 only on CC 0-31, its LSB free) */
    mapping_t m = G.map[k];
    if (m.bits == 14) G.map[k].bits = 7;
    else if (m.cc > 31 || reserved_why(m.cc + 32)) {
        say(e, "CC %d can only be 7-bit: 14-bit needs a CC from 0 to 31 (its LSB is that CC + 32).", m.cc);
        return;
    } else {
        map_add(m.key, m.cc, 14);   /* (whatever used its LSB goes) */
    }
    map_changed(e);
}

static int restore_defaults(ed_t *e) {
    learn_cancel(e);
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
enum { B_RESTORE, B_OPEN_PATCHES, B_OPEN_SETTINGS, B_LEARN, B_CLEAR_MAP, B_LOAD_SCL, B_CLEAR_SCL, B_LOAD_KBM,
       B_CLEAR_KBM, B_COUNT };
enum { TUN_ROW_SCL = 3, TUN_ROW_KBM = 4 };   /* the TUNING tab's rows for the focused Synth's scale and keyboard map */
static const char *const TAB_NAME[ST_TABS] = {"GLOBAL", "MIDI", "LIBRARY", "TUNING"};
static const char *const TUNING_KEY[] = {"master_tune", "fine_tune"};
enum { NTUNING = sizeof TUNING_KEY / sizeof TUNING_KEY[0] };
enum { MAP_ROWS = ROWS - 2 };   /* the MIDI tab: LEARN, then this many mappings per sub-page, then the reserved CCs */

int settings_subs(ed_t *e) { return e->st.tab == ST_MIDI && G.nmap > MAP_ROWS ? (G.nmap + MAP_ROWS - 1) / MAP_ROWS : 1; }

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
static RECT button_rect(int b) {   /* RESTORE, then the OPEN buttons on the right: LIBRARY's last three rows */
    RECT r = L.row[ROWS - 1], nm, d, u;
    if (b == B_RESTORE) row_parts(ROWS - 3, &nm, &r, &d, &u);
    else if (b == B_LEARN) { r = L.row[0]; r.right = r.left + sc(96); InflateRect(&r, -sc(4), -sc(2)); }
    else if (b == B_CLEAR_MAP) { r.left = r.right - sc(76); InflateRect(&r, -sc(4), -sc(2)); }
    else if (b >= B_LOAD_SCL) {   /* LOAD and CLEAR at the right of the scale's and the map's rows */
        r = L.row[b <= B_CLEAR_SCL ? TUN_ROW_SCL : TUN_ROW_KBM];
        int clear = b == B_CLEAR_SCL || b == B_CLEAR_KBM, bw = sc(66);
        r.right -= sc(4) + (clear ? 0 : bw + sc(6));
        r.left = r.right - bw;
        InflateRect(&r, 0, -sc(2));
    }
    else row_parts(b == B_OPEN_PATCHES ? ROWS - 2 : ROWS - 1, &nm, &r, &d, &u);
    return r;
}
static int button_shown(ed_t *e, int b) {
    return b >= B_LOAD_SCL ? e->st.tab == ST_TUNING
         : b >= B_LEARN ? e->st.tab == ST_MIDI : e->st.tab == ST_LIBRARY;
}

/* ---- MICROTUNING (package 16): the focused Synth's Scala scale and keyboard map, saved with its sound ----
 * The file is read and checked here (scala.c), on the window's thread; the engine gets the compact text through the
 * chunk call ("tunscl<n>" / "tunkbm<n>", src/synths.h) and keeps it in the sound's state. */
static void tun_names(ed_t *e, char *scl, char *kbm, int n) {   /* the names, from the engine's state */
    void *chunk = NULL;
    scl[0] = kbm[0] = 0;
    intptr_t len = wrapped_call(e->fx, effGetChunk, 0, 0, &chunk, 0);
    if (len <= 0 || !chunk) return;
    for (int w = 0; w < 2; w++) {
        char key[24];
        if (e->focus) snprintf(key, sizeof key, ";s%d_tun_%s=", e->focus + 1, w ? "kbm" : "scl");
        else snprintf(key, sizeof key, ";tun_%s=", w ? "kbm" : "scl");
        const char *p = strstr((const char *)chunk, key);
        if (!p) continue;
        p += strlen(key);
        snprintf(w ? kbm : scl, n, "%.*s", (int)strcspn(p, "|;"), p);
    }
}
static int tun_load(ed_t *e, const char *path, int kbm) {   /* 1: loaded */
    static char text[65536], out[4096];
    char err[160];
    FILE *f = fopen(path, "rb");
    if (!f) { say(e, "Couldn't open that file."); return 0; }
    size_t n = fread(text, 1, sizeof text - 1, f);
    int whole = feof(f);
    fclose(f);
    text[n] = 0;
    if (!whole) { say(e, "That file is too big to be a %s.", kbm ? "keyboard map" : "scale"); return 0; }
    if (!(kbm ? kbm_compact : scl_compact)(text, path, out, sizeof out, err, sizeof err)) {
        say(e, "Couldn't use that %s: %s.", kbm ? "keyboard map" : "scale", err);
        return 0;
    }
    static char cmd[4200];
    snprintf(cmd, sizeof cmd, "%s%d\n%s", kbm ? "tunkbm" : "tunscl", e->focus + 1, out);
    wrapped_call(e->fx, effSetChunk, 0, (intptr_t)strlen(cmd) + 1, cmd, 0);
    char nm[64], s[64], k[64];
    snprintf(nm, sizeof nm, "%.*s", (int)strcspn(out, "|"), out);
    tun_names(e, s, k, sizeof s);
    if (kbm && !s[0]) say(e, "Keyboard map '%s' kept for Synth %d; it applies once a scale is loaded.", nm, e->focus + 1);
    else say(e, "%s '%s' now tunes Synth %d (saved with the sound).", kbm ? "Keyboard map" : "Scale", nm, e->focus + 1);
    return 1;
}
static void tun_clear_one(ed_t *e, int kbm) {
    char cmd[32];
    snprintf(cmd, sizeof cmd, "%s%d\n", kbm ? "tunkbm" : "tunscl", e->focus + 1);
    wrapped_call(e->fx, effSetChunk, 0, (intptr_t)strlen(cmd) + 1, cmd, 0);
    say(e, kbm ? "Synth %d: the keyboard map is back to the default." : "Synth %d: back to the usual 12-tone tuning.",
        e->focus + 1);
}
static void tun_pick(ed_t *e, int kbm) {   /* LOAD: the file dialog, then the file */
    char path[MAX_PATH] = "";
    OPENFILENAMEA o = {0};
    o.lStructSize = sizeof o;
    o.hwndOwner = e->wnd ? GetAncestor(e->wnd, GA_ROOT) : NULL;
    o.lpstrFilter = kbm ? "Scala keyboard maps (*.kbm)\0*.kbm\0All files\0*.*\0"
                        : "Scala scales (*.scl)\0*.scl\0All files\0*.*\0";
    o.lpstrFile = path;
    o.nMaxFile = sizeof path;
    o.lpstrTitle = kbm ? "Load a keyboard map" : "Load a scale";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&o)) tun_load(e, path, kbm);
}
/* a mapping's row on the MIDI tab (row 1 .. MAP_ROWS): CC, the control, 7/14-BIT, DELETE */
static void map_parts(int row, RECT *cc, RECT *name, RECT *bits, RECT *del) {
    RECT r = L.row[row];
    int bw = sc(60);
    SetRect(cc, r.left + sc(4), r.top, r.left + sc(54), r.bottom);
    SetRect(del, r.right - sc(4) - bw, r.top + sc(2), r.right - sc(4), r.bottom - sc(2));
    SetRect(bits, del->left - sc(6) - bw, del->top, del->left - sc(6), del->bottom);
    SetRect(name, cc->right + sc(4), r.top, bits->left - sc(6), r.bottom);
}
static int map_at(ed_t *e, int row) {   /* the mapping shown on a row, or -1 */
    int k = e->sub * MAP_ROWS + row - 1;
    return row >= 1 && row <= MAP_ROWS && k < G.nmap ? k : -1;
}
static int tuning_index(int row) { return row < NTUNING ? index_of(TUNING_KEY[row]) : -1; }

int settings_hit(ed_t *e, POINT pt) {
    if (e->st.tab == ST_LIBRARY) {   /* (2026-10-30) USER LIBRARY and DUPLICATE NAMES (parts.c) */
        int z = parts_lib_hit(e, pt);
        if (z >= 0) return z;
    }
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
    if (e->st.tab == ST_MIDI)
        for (int row = 1; row <= MAP_ROWS; row++) {
            RECT c, nm, bits, del;
            if (map_at(e, row) < 0) continue;
            map_parts(row, &c, &nm, &bits, &del);
            if (PtInRect(&bits, pt)) return Z_ROW + row * 4 + PART_DOWN;
            if (PtInRect(&del, pt)) return Z_ROW + row * 4 + PART_UP;
            RECT all = {c.left, c.top, nm.right, c.bottom};
            if (PtInRect(&all, pt)) return Z_ROW + row * 4 + PART_MAIN;
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
                           "Put every setting back to how Moo Won came (autoload, MIDI clock, channels from presets, "
                           "pedal numbers)? Your patches and sounds are not touched.",
                           "Moo Won", MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
            restore_defaults(e);
        if (b == B_OPEN_PATCHES || b == B_OPEN_SETTINGS) open_folder(e, b == B_OPEN_SETTINGS);
        if (b == B_LOAD_SCL || b == B_LOAD_KBM) tun_pick(e, b == B_LOAD_KBM);
        if (b == B_CLEAR_SCL || b == B_CLEAR_KBM) tun_clear_one(e, b == B_CLEAR_KBM);
        if (b == B_LEARN) {   /* LEARN, or CANCEL while learning */
            if (e->st.learn) { learn_cancel(e); say(e, "LEARN cancelled: nothing changed."); }
            else learn_arm(e);
        }
        if (b == B_CLEAR_MAP && G.nmap
            && MessageBoxA(e->wnd ? GetAncestor(e->wnd, GA_ROOT) : NULL, "Delete every learned MIDI CC mapping?",
                           "Moo Won", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
            G.nmap = 0;
            map_changed(e);
        }
    } else if (z >= Z_ROW && e->st.tab == ST_MIDI) {
        int row = (z - Z_ROW) / 4, part = (z - Z_ROW) % 4, k = map_at(e, row);
        if (k >= 0 && part == PART_DOWN) map_bits(e, k);
        if (k >= 0 && part == PART_UP) { map_remove(k); map_changed(e); }
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

/* the MIDI tab: LEARN and what it waits for, the mappings, the reserved CCs */
static void draw_midi(HDC dc, ed_t *e) {
    static const char *const STAGE[3] = {
        "LEARN, a control on the panel, then a knob on your controller.",
        "Now click the control to learn.",
        "Now move a knob or fader on your controller."};
    button(dc, button_rect(B_LEARN), e->st.learn ? "CANCEL" : "LEARN", e->st.learn != 0, e->hover == Z_BTN + B_LEARN);
    RECT st = L.row[0];
    st.left = button_rect(B_LEARN).right + sc(8);
    label(dc, st, STAGE[e->st.learn < 0 || e->st.learn > 2 ? 0 : e->st.learn], font_cell, e->st.learn ? S_HOT : S_DIM,
          DT_LEFT);
    for (int row = 1; row <= MAP_ROWS; row++) {
        int k = map_at(e, row), z = Z_ROW + row * 4;
        RECT c, nm, bits, del;
        map_parts(row, &c, &nm, &bits, &del);
        if (k < 0) {
            if (row == 1 && !G.nmap) label(dc, nm, "No CCs learned yet.", font_cell, S_DIM, DT_LEFT);
            continue;
        }
        char t[64], name[80];
        int i = index_of(G.map[k].key);
        snprintf(t, sizeof t, G.map[k].bits == 14 ? "CC %d+%d" : "CC %d", G.map[k].cc, G.map[k].cc + 32);
        if (i >= 0) param_label(i, name, sizeof name);
        else snprintf(name, sizeof name, "%s (not in this version)", G.map[k].key);
        int hot = e->hover >= z && e->hover < z + 3;
        label(dc, c, t, font_cell, hot ? S_HOT : S_LCD, DT_LEFT);
        label(dc, nm, name, font_cell, hot ? S_HOT : S_LCD, DT_LEFT);
        button(dc, bits, G.map[k].bits == 14 ? "14-BIT" : "7-BIT", G.map[k].bits == 14, e->hover == z + PART_DOWN);
        button(dc, del, "DELETE", 0, e->hover == z + PART_UP);
    }
    RECT rs = L.row[ROWS - 1];
    rs.left += sc(4);
    rs.right = button_rect(B_CLEAR_MAP).left - sc(6);
    char res[160];
    snprintf(res, sizeof res, "Reserved: 0-3 6 16-18 32 38 66 96-101 120+, pedals %d %d %d",
             (int)G.v[SI_SUSTAIN], (int)G.v[SI_EXP1], (int)G.v[SI_EXP2]);
    label(dc, rs, res, font_tiny, S_DIM, DT_LEFT);
    button(dc, button_rect(B_CLEAR_MAP), "CLEAR ALL", 0, e->hover == Z_BTN + B_CLEAR_MAP);
}

void settings_draw(HDC dc, ed_t *e) {   /* under the title line, which editor_draw.c draws */
    for (int t = 0; t < ST_TABS; t++) button(dc, tab_rect(t), TAB_NAME[t], e->st.tab == t, e->hover == Z_TAB + t);
    char v[MAX_PATH + 32];
    if (e->st.tab == ST_MIDI) {
        draw_midi(dc, e);
    } else if (e->st.tab == ST_GLOBAL) {
        for (int k = 0; k < SI_COUNT; k++) {
            const item_t *it = &ITEM[k];
            if (is_pedal(k)) snprintf(v, sizeof v, "CC %d", (int)G.v[k]);
            else snprintf(v, sizeof v, "%g%s", G.v[k], it->unit);
            value_row(dc, e, k, it->label, v, it->kind == IK_NUM, G.v[k] > 0.5f);
        }
    } else if (e->st.tab == ST_LIBRARY) {
        char pd[MAX_PATH];
        if (!patch_dir(pd, sizeof pd, 0)) snprintf(pd, sizeof pd, "(no Documents or AppData folder found)");
        patches_rescan(e);
        parts_lib_draw(dc, e);   /* (2026-10-30, parts.c) rows 0-2: USER LIBRARY, its buttons, DUPLICATE NAMES */
        const char *name[2] = {"PATCH FOLDER", "VERSION"};
        char val[2][MAX_PATH + 96];
        snprintf(val[0], sizeof val[0], "%s", pd);
        snprintf(val[1], sizeof val[1], "%s %d.%d.%d, %d params, built %s", PLUG_NAME, PLUG_VERSION / 1000,
                 PLUG_VERSION / 100 % 10, PLUG_VERSION % 100, NPARAMS, __DATE__);
        for (int r = 0; r < 2; r++) {
            RECT nm, vr, d, u;
            row_parts(3 + r, &nm, &vr, &d, &u);
            label(dc, nm, name[r], font_cell, S_DIM, DT_LEFT);
            label(dc, vr, val[r], font_cell, S_LCD, r == 0 ? DT_PATH_ELLIPSIS | DT_LEFT : DT_LEFT);
        }
        static const char *const SIDE[3] = {"Every setting on this page:", "Your patches:", "This settings file:"};
        for (int r = 0; r < 3; r++) {
            RECT a = L.row[ROWS - 3 + r];
            a.left += sc(4);
            a.right = button_rect(B_OPEN_PATCHES).left - sc(4);
            label(dc, a, SIDE[r], font_cell, S_DIM, DT_LEFT);
        }
        button(dc, button_rect(B_RESTORE), "RESTORE DEFAULTS", 0, e->hover == Z_BTN + B_RESTORE);
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
        char scl[64], kbm[64], h[96];
        tun_names(e, scl, kbm, sizeof scl);
        RECT hd = L.row[2];
        hd.left += sc(4);
        snprintf(h, sizeof h, "SYNTH %d MICROTUNING (Scala files, saved with the sound)", e->focus + 1);
        label(dc, hd, h, font_tiny, S_DIM, DT_LEFT);
        for (int w = 0; w < 2; w++) {
            RECT nm, vr, d, u;
            row_parts(w ? TUN_ROW_KBM : TUN_ROW_SCL, &nm, &vr, &d, &u);
            vr.right = button_rect(w ? B_LOAD_KBM : B_LOAD_SCL).left - sc(6);
            const char *val = w ? (kbm[0] ? kbm : scl[0] ? "default (key 60 = degree 0)" : "-")
                                : scl[0] ? scl : "none: the usual 12-tone tuning";
            label(dc, nm, w ? "KEYBOARD MAP" : "SCALE", font_cell, S_DIM, DT_LEFT);
            label(dc, vr, val, font_cell, (w ? kbm[0] : scl[0]) ? S_HOT : S_LCD, DT_LEFT);
            button(dc, button_rect(w ? B_LOAD_KBM : B_LOAD_SCL), w ? "LOAD .KBM" : "LOAD .SCL", 0,
                   e->hover == Z_BTN + (w ? B_LOAD_KBM : B_LOAD_SCL));
            button(dc, button_rect(w ? B_CLEAR_KBM : B_CLEAR_SCL), "CLEAR", 0,
                   e->hover == Z_BTN + (w ? B_CLEAR_KBM : B_CLEAR_SCL));
        }
        static const char *const INFO[3] = {
            "A scale tunes the Synth in focus (SYNTH 1/2/3 picks it).",
            "A keyboard map: which key plays which degree, the reference pitch.",
            "Master tune still applies. No scale: the usual tuning."};
        for (int r = 0; r < 3; r++) {
            RECT ir = L.row[5 + r];
            ir.left += sc(4);
            label(dc, ir, INFO[r], font_tiny, S_DIM, DT_LEFT);
        }
    }
}

const char *settings_zone_help(ed_t *e, int z) {
    if (z < Z_SET_BASE || z >= Z_SET_END) return NULL;
    if (z >= Z_TAB && z < Z_TAB + ST_TABS) {
        static const char *const H[ST_TABS] = {
            "GLOBAL: how the instrument behaves: preset autoload, the MIDI clock, channels from presets, the pedals.",
            "MIDI: MIDI LEARN. Map your controller's knobs and faders (their CCs) to any control on the panel.",
            "LIBRARY: your user libraries (switch, new, rename, copy a sound to another), what IMPORT does with a "
            "same-named patch, where your patches are, the version.",
            "TUNING: the master frequency (the A above middle C), the master fine tune, and microtuning (Scala scales "
            "and keyboard maps) for the Synth in focus."};
        return H[z - Z_TAB];
    }
    if (z == Z_BTN + B_RESTORE) return "RESTORE DEFAULT SETTINGS: every setting on this page back to how Moo Won came. "
                                       "Patches, presets and sounds are not changed.";
    if (z == Z_BTN + B_OPEN_PATCHES) return "Opens the folder with your patch files in Explorer.";
    if (z == Z_BTN + B_OPEN_SETTINGS) return "Opens the folder with the settings file in Explorer.";
    if (z == Z_BTN + B_LEARN) return "LEARN: click it, then a control on the panel, then move a knob or fader on your "
                                     "controller: that CC moves the control from now on. Click again to CANCEL.";
    if (z == Z_BTN + B_CLEAR_MAP) return "CLEAR ALL: deletes every learned mapping (asks first).";
    if (z == Z_BTN + B_LOAD_SCL) return "LOAD .SCL: a Scala scale for the Synth in focus (thousands are free online, "
                                        "e.g. the Scala archive). It is kept in the sound: save the patch or project.";
    if (z == Z_BTN + B_LOAD_KBM) return "LOAD .KBM: a Scala keyboard map: which key plays degree 0, the reference key "
                                        "and frequency, and which keys play which degrees (or none).";
    if (z == Z_BTN + B_CLEAR_SCL) return "CLEAR: the Synth in focus goes back to the usual 12-tone tuning.";
    if (z == Z_BTN + B_CLEAR_KBM) return "CLEAR: the default keyboard map (key 60 plays degree 0 at middle C's pitch).";
    int row = (z - Z_ROW) / 4, part = (z - Z_ROW) % 4;
    if (e->st.tab == ST_MIDI && z >= Z_ROW)
        return part == PART_DOWN ? "7-BIT / 14-BIT: 14-bit uses the CC (0-31) as the coarse part and CC + 32 as the "
                                   "fine part, for smooth sweeps from controllers that send both."
             : part == PART_UP ? "DELETE: this CC no longer moves the control."
                               : "A learned mapping: this CC, from any MIDI channel, moves this control (and the host "
                                 "sees it as automation). NRPN messages after the instrument's table always work too.";
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
    if (e->st.tab == ST_MIDI)
        snprintf(out, n, "MIDI LEARN: a CC moves one control (learning a CC or a control again moves it). The map is "
                         "kept with these settings, for every sound. NRPN after the instrument's table always works.");
    else if (e->st.tab == ST_LIBRARY)
        snprintf(out, n, "USER LIBRARY: separate collections of your patches (with their snapshots, sets, tags and "
                         "effect and matrix presets); < > switch. DUPLICATE NAMES: what IMPORT and COPY SOUND TO do "
                         "when a patch of that name is there.");
    else if (e->st.tab == ST_TUNING)
        snprintf(out, n, "Tuning, all part of the sound: master frequency and fine tune (click < > or scroll, Shift "
                         "finer), and the focused Synth's own scale and keyboard map (LOAD a Scala file).");
    else
        snprintf(out, n, "Settings kept apart from the sounds: loading a sound or a project never changes them. Click "
                         "a value to change it (or scroll over it). They are saved at once, for every Moo Won.");
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
    case STT_LEARN: learn_arm(e); return 1;
    case STT_LEARN_PICK: return settings_learn_pick(e, (int)lroundf(opt)) && e->st.learn == 2;
    case STT_LEARN_TICK: return learn_tick(e, 1);
    case STT_MAP: {
        int used = 0;
        buf[0] = 0;
        for (int k = 0; k < G.nmap && used < 1000; k++)
            used += snprintf(buf + used, 1024 - used, "%d,%s,%d;", G.map[k].cc, G.map[k].key, G.map[k].bits);
        return G.nmap;
    }
    case STT_MAP_BITS: { int k = (int)lroundf(opt); if (k < 0 || k >= G.nmap) return 0; map_bits(e, k); return 1; }
    case STT_MAP_DELETE: { int k = (int)lroundf(opt); if (k < 0 || k >= G.nmap) return 0; map_remove(k); map_changed(e); return 1; }
    case STT_MAP_CLEAR: G.nmap = 0; map_changed(e); return 1;
    case STT_MESSAGE: snprintf(buf, 1024, "%s", e->st.msg); return 1;
    case STT_TUN_LOAD: { char p[MAX_PATH]; snprintf(p, sizeof p, "%s", buf); return tun_load(e, p, opt > 0.5f); }
    case STT_TUN_CLEAR: tun_clear_one(e, opt > 0.5f); return 1;
    case STT_TUN_NAMES: {
        char s[64], k[64];
        tun_names(e, s, k, sizeof s);
        snprintf(buf, 1024, "%s|%s", s, k);
        return 1;
    }
    }
    return 0;
}
