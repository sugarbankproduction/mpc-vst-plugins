/* Library parts and user libraries (desktop editor only, 2026-10-30; docs/PATCHES.md, parts.h). Everything here runs
 * on the window's thread: files are read and written only here, never in the audio path.
 *
 * EFFECT PRESETS (Moog One manual p97-98, p101): the settings of one effect slot (the focused Synth's SYNTH EFFECT,
 *   MASTER FX 1, MASTER FX 2) saved by name with notes and the time, as <library>\Effects\<name>.oneishfx. A preset
 *   keeps its type by name and its values by the control's name without the slot's prefix ("time", "fb"), so a preset
 *   saved from one slot loads into another that has its type (the master slots have the reverbs; the Synth effect
 *   the Synth-only types). LOAD: the slot's controls to their defaults, the preset's type, then its values; INIT: the
 *   controls to their defaults, the type kept. ON and the sends are never touched. Each is one undo step. A few of Moo
 *   Won's own (FACTORY, below) are listed for the common types; they are part of the program, not files.
 * MATRIX PRESETS (p73-74): the focused Synth's rows in use (a row is free when its SOURCE is Off and VIA None) saved
 *   by name with notes and the time, as <library>\Matrices\<name>.oneishmtx. ADD puts its rows in the free rows (as
 *   many as there are); REPLACE puts them in rows 1, 2 ... and empties the rest. One undo step.
 * SYNTH NAMES (p12-13, the HOME screen's timbre names): a click on a Synth's head on the SETUP page names it (up to
 *   12 characters). The engine keeps the name in the sound's state (src/synths.h "syn_name"), so it is saved with
 *   patches and projects and undone like any edit; a sound saved before has none and shows "SYNTH 1". The names show
 *   under the SYNTH 1/2/3 buttons, on the SETUP page and on the home line under the patch name.
 * USER LIBRARIES (p134-135, the instrument's user spaces): separate collections of user patches, each a folder with
 *   its own snapshots, sets, tags and effect and matrix presets. "Default" is the patch folder of before (untouched);
 *   the others are folders in <Documents>\ONE-ish\Libraries (ONEISH_LIB_DIR overrides it). SETTINGS > LIBRARY:
 *   < > switch, NEW EMPTY, NEW + FACTORY (every factory preset as a user patch, to edit freely), RENAME, COPY SOUND TO
 *   (the sound playing as a patch in another library). The library in use is a setting (Settings.txt).
 * DUPLICATE NAMES (p126): what IMPORT and COPY SOUND TO do with a same-named patch: ASK (as before), OVERWRITE, AUTO
 *   RENAME ("Name 2"), IGNORE. A setting on SETTINGS > LIBRARY. */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "library.h"
#include "hostrate.h"
#include "factory_tags.h"   /* (presets/make_presets.py) the factory presets' tags, for NEW + FACTORY */
#include <shlobj.h>

#define FX_EXT ".oneishfx"
#define MX_EXT ".oneishmtx"
static const char FX_HEADER[] = "ONE-ish effect", MX_HEADER[] = "ONE-ish matrix";
static const char *const SLOT_PFX[3] = {"modfx_", "dly_", "rev_"};
static const char *const SLOT_NAME[PS_COUNT] = {"SYNTH FX", "MASTER FX 1", "MASTER FX 2", "MATRIX"};
static const char *const MX_SFX[] = {"src", "dst", "amt", "via", "via_amt", "xf", "xf_amt", "on"};
enum { MX_F = sizeof MX_SFX / sizeof MX_SFX[0], MX_ROWS = 16, PER = ROWS - 2 };
enum { A_SAVE, A_LOAD, A_INIT, A_NOTES, A_DELETE, A_ALL, A_ADD, A_REPLACE, A_COUNT };
static const char *const ACT_NAME[A_COUNT] = {"SAVE", "LOAD", "INIT", "NOTES", "DELETE", "ALL TYPES", "ADD", "REPLACE"};
static const int FX_ACTS[] = {A_SAVE, A_LOAD, A_INIT, A_NOTES, A_DELETE, A_ALL};
static const int MX_ACTS[] = {A_SAVE, A_ADD, A_REPLACE, A_NOTES, A_DELETE};
enum { PL_PREV, PL_NEXT, PL_NEW, PL_NEW_FACTORY, PL_RENAME, PL_COPY, PL_DUP_DOWN, PL_DUP_UP, PL_COUNT };
static const char *const DUP_NAME[DUP_COUNT] = {"Ask", "Overwrite", "Auto Rename", "Ignore"};

/* Moo Won's own effect presets (original settings): a type, then values as "control=value" in the control's units */
static const struct { const char *name, *type, *notes, *vals; } FACTORY[] = {
    {"Slow Wide Chorus", "Chorus", "A slow, wide chorus for pads and strings.", "rate=20;depth=70;spread=90"},
    {"Thick Chorus", "Dual Chorus", "Two choruses at once: thick, for basses and leads.", "rate=40;depth=55"},
    {"Lush Ensemble", "Ensemble", "String-machine shimmer.", "rate=35;depth=60"},
    {"Slapback", "Stereo Delay", "One short repeat, a little apart left and right.", "time=35;time_r=38;fb=10;tone=70"},
    {"Long Dub", "Tape Delay", "Long, dark repeats that wear away.", "time=80;fb=60;tone=40;wow=40;flutter=30"},
    {"Wide Ping Pong", "Ping Pong", "Repeats bouncing left and right.", "time=65;fb=45;tone=60"},
    {"Jet Flange", "Flanger", "A slow jet sweep.", "rate=15;depth=80;fb=70"},
    {"Slow Phase", "6-Stage Phaser", "A slow, gentle phaser.", "rate=15;depth=70;fb=30"},
    {"Crushed", "Bit Crusher", "Fewer bits and a lower sample rate: lo-fi grit.", "bits=6;srate=45"},
    {"Small Room", "Room", "A small, close room.", "decay=25;size=30;pre=5;damp=50"},
    {"Big Hall", "Hall", "A large hall with a long tail.", "decay=75;size=85;pre=30;damp=35"},
    {"Dark Plate", "Plate", "A plate with the top rolled off.", "decay=55;damp=70;tone=55"},
    {"Deep Space", "Space", "A vast, slowly moving space.", "decay=90;size=95;mod=70"},
};
enum { NFACTORY = sizeof FACTORY / sizeof FACTORY[0] };

#define PA(e) (&(e)->pa)
static void redraw(ed_t *e) { if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE); }
static void say(ed_t *e, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(PA(e)->msg, sizeof PA(e)->msg, fmt, ap);
    va_end(ap);
    PA(e)->msg_at = GetTickCount();
    snprintf(e->pt.msg, sizeof e->pt.msg, "%s", PA(e)->msg);   /* (the help line shows the last action's result) */
    e->pt.msg_at = PA(e)->msg_at;
    redraw(e);
}
static const char *message(ed_t *e) {
    return PA(e)->msg[0] && GetTickCount() - PA(e)->msg_at < 6000 ? PA(e)->msg : NULL;
}

/* ---- params ---- */
static int key_index(const char *k) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) return i;
    return -1;
}
static float norm_of(const param_t *p, const char *val) {   /* a value in the param's units (an option: its label) */
    if (p->nopts > 1) {
        for (int k = 0; k < p->nopts; k++)
            if (!_stricmp(val, p->opts[k])) return k / (float)(p->nopts - 1);
        int k = atoi(val);
        k = k < 0 ? 0 : k >= p->nopts ? p->nopts - 1 : k;
        return k / (float)(p->nopts - 1);
    }
    float v = (float)atof(val), span = p->max - p->min, x = span != 0 ? (v - p->min) / span : 0;
    return x < 0 ? 0 : x > 1 ? 1 : x;
}
static void value_text(ed_t *e, int i, char *out, int n) {   /* param i's value now, in its units */
    const param_t *p = &PARAMS[i];
    float v = e->fx->getParameter(e->fx, i);
    if (p->nopts > 1) snprintf(out, n, "%s", p->opts[(int)lroundf(v * (p->nopts - 1))]);
    else snprintf(out, n, "%g", p->min + v * (p->max - p->min));
}
static int option(ed_t *e, int i) {
    return PARAMS[i].nopts > 1 ? (int)lroundf(e->fx->getParameter(e->fx, i) * (PARAMS[i].nopts - 1)) : 0;
}
/* many params at once, as one undo step (the host told of each, as a knob does) */
static void apply(ed_t *e, const int *idx, const float *v, int n, const char *label) {
    lib_touch(e, label);
    lib_hold(e, 1);
    for (int k = 0; k < n; k++) {
        int i = idx[k];
        if (i < 0 || e->fx->getParameter(e->fx, i) == v[k]) continue;
        if (e->host) e->host(e->fx, audioMasterBeginEdit, i, 0, NULL, 0);
        e->fx->setParameter(e->fx, i, v[k]);
        if (e->host) {
            e->host(e->fx, audioMasterAutomate, i, 0, NULL, v[k]);
            e->host(e->fx, audioMasterEndEdit, i, 0, NULL, 0);
        }
    }
    lib_hold(e, -1);
    redraw(e);
}

/* ---- an effect slot's controls: their own indexes (the focused Synth's for the Synth effect) ---- */
static int slot_type(ed_t *e, int s) {
    char k[32];
    snprintf(k, sizeof k, "%stype", SLOT_PFX[s]);
    int i = key_index(k);
    return i < 0 ? -1 : synth_param(i, e->focus);
}
static int slot_params(ed_t *e, int s, int *out) {   /* every control but ON and TYPE; how many */
    int n = 0;
    size_t pl = strlen(SLOT_PFX[s]);
    for (int i = 0; i < NPARAMS; i++) {
        const char *k = PARAMS[i].key;
        if (strncmp(k, SLOT_PFX[s], pl) || !strcmp(k + pl, "on") || !strcmp(k + pl, "type")) continue;
        out[n++] = synth_param(i, e->focus);
    }
    return n;
}
static const char *suffix(int s, int i) {   /* a slot control's name without the prefix (Synth 2's too) */
    const char *k = PARAMS[base_of(i)].key;
    return k + strlen(SLOT_PFX[s]);
}
static int type_option(ed_t *e, int s, const char *type) {   /* the slot's option for a type name, -1: it has none */
    int t = slot_type(e, s);
    if (t < 0) return -1;
    for (int k = 0; k < PARAMS[t].nopts; k++)
        if (!_stricmp(PARAMS[t].opts[k], type)) return k;
    return -1;
}
static const char *type_now(ed_t *e, int s) {
    int t = slot_type(e, s);
    return t < 0 ? "" : PARAMS[t].opts[option(e, t)];
}

/* ---- a matrix row's params (the focused Synth's) ---- */
static int mx_index(ed_t *e, int r, int f) {
    static int base[MX_ROWS][MX_F], ready;
    if (!ready) {
        for (int a = 0; a < MX_ROWS; a++)
            for (int b = 0; b < MX_F; b++) {
                char k[32];
                snprintf(k, sizeof k, "mod%d_%s", a + 1, MX_SFX[b]);
                base[a][b] = key_index(k);
            }
        ready = 1;
    }
    return base[r][f] < 0 ? -1 : synth_param(base[r][f], e->focus);
}
static int row_free(ed_t *e, int r) {   /* SOURCE Off and VIA None: the row does nothing */
    int s = mx_index(e, r, 0), v = mx_index(e, r, 3);
    return (s < 0 || option(e, s) == 0) && (v < 0 || option(e, v) == 0);
}
static int rows_used(ed_t *e) {
    int n = 0;
    for (int r = 0; r < MX_ROWS; r++) n += !row_free(e, r);
    return n;
}

/* ---- files ---- */
static void esc(const char *in, char *out, int n) {   /* notes on one line: \n, \s (;), \\ */
    int k = 0;
    for (; *in && k < n - 3; in++) {
        if (*in == '\n') { out[k++] = '\\'; out[k++] = 'n'; }
        else if (*in == ';') { out[k++] = '\\'; out[k++] = 's'; }
        else if (*in == '\\') { out[k++] = '\\'; out[k++] = '\\'; }
        else out[k++] = *in;
    }
    out[k] = 0;
}
static void unesc(const char *in, char *out, int n) {
    int k = 0;
    for (; *in && k < n - 1; in++) {
        if (*in == '\\' && in[1]) {
            in++;
            out[k++] = *in == 'n' ? '\n' : *in == 's' ? ';' : *in;
        } else {
            out[k++] = *in;
        }
    }
    out[k] = 0;
}
static int kind_dir(int matrix, char *out, int n, int create) {   /* the library's Effects or Matrices folder */
    char d[MAX_PATH];
    if (!patch_dir(d, sizeof d, create)) return 0;
    if (snprintf(out, n, "%s\\%s", d, matrix ? "Matrices" : "Effects") >= n) return 0;
    if (create) {
        int r = SHCreateDirectoryExA(NULL, out, NULL);
        DWORD a = GetFileAttributesA(out);
        return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY));
    }
    return 1;
}
static int item_path(int matrix, const char *name, char *out, int n, int create) {
    char d[MAX_PATH];
    return kind_dir(matrix, d, sizeof d, create) && snprintf(out, n, "%s\\%s%s", d, name, matrix ? MX_EXT : FX_EXT) < n;
}
static int file_exists(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static int line_of(const char *t, const char *key, char *out, int n) {   /* the first "key=" line's value */
    size_t kl = strlen(key);
    for (const char *s = t; s && *s;) {
        while (*s == '\r' || *s == '\n') s++;
        if (!strncmp(s, key, kl) && s[kl] == '=') {
            const char *v = s + kl + 1;
            int len = (int)strcspn(v, "\r\n");
            snprintf(out, n, "%.*s", len, v);
            return 1;
        }
        s = strpbrk(s, "\r\n");
    }
    out[0] = 0;
    return 0;
}
static void now_text(char *out, int n) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(out, n, "%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
}

/* ---- the list: the slot's presets (factory first, then the files A to Z) ---- */
static int by_name(const void *a, const void *b) {
    const pitem_t *x = a, *y = b;
    if (x->factory != y->factory) return y->factory - x->factory;
    return _stricmp(x->name, y->name);
}
static pitem_t *add_item(ed_t *e) {
    parts_ui_t *P = PA(e);
    if (P->n == P->cap) {
        int cap = P->cap ? 2 * P->cap : 32;
        pitem_t *t = realloc(P->it, sizeof *t * (size_t)cap);
        if (!t) return NULL;
        P->it = t;
        P->cap = cap;
    }
    pitem_t *it = &P->it[P->n++];
    memset(it, 0, sizeof *it);
    return it;
}
static int type_shown(ed_t *e, const char *type) {   /* the slot has it, and the list shows it */
    int s = PA(e)->slot;
    return type_option(e, s, type) >= 0 && (PA(e)->all_types || !_stricmp(type, type_now(e, s)));
}
static void rescan(ed_t *e) {
    parts_ui_t *P = PA(e);
    int mx = P->slot == PS_MTX;
    P->n = 0;
    for (int k = 0; k < NFACTORY && !mx; k++) {
        if (!type_shown(e, FACTORY[k].type)) continue;
        pitem_t *it = add_item(e);
        if (!it) break;
        snprintf(it->name, sizeof it->name, "%s", FACTORY[k].name);
        snprintf(it->type, sizeof it->type, "%s", FACTORY[k].type);
        snprintf(it->notes, sizeof it->notes, "%s", FACTORY[k].notes);
        it->factory = 1;
    }
    char d[MAX_PATH], pat[MAX_PATH + 16];
    if (kind_dir(mx, d, sizeof d, 0)) {
        snprintf(pat, sizeof pat, "%s\\*%s", d, mx ? MX_EXT : FX_EXT);
        WIN32_FIND_DATAA f;
        HANDLE h = FindFirstFileA(pat, &f);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                char path[2 * MAX_PATH], v[NOTES_MAX * 2 + 8];
                snprintf(path, sizeof path, "%s\\%s", d, f.cFileName);
                char *t = pt_read(path);
                if (!t) continue;
                char nm[64], ty[64];
                size_t l = strlen(f.cFileName), x = strlen(mx ? MX_EXT : FX_EXT);
                snprintf(nm, sizeof nm, "%.*s", (int)(l > x ? l - x : l), f.cFileName);
                line_of(t, "type", ty, sizeof ty);
                if (strncmp(t, mx ? MX_HEADER : FX_HEADER, strlen(mx ? MX_HEADER : FX_HEADER)) ||
                    (!mx && !type_shown(e, ty)) || strlen(nm) > PATCH_NAME_MAX) {
                    free(t);
                    continue;
                }
                pitem_t *it = add_item(e);
                if (!it) { free(t); break; }
                snprintf(it->name, sizeof it->name, "%s", nm);
                snprintf(it->type, sizeof it->type, "%s", mx ? "" : ty);
                line_of(t, "time", it->when, sizeof it->when);
                line_of(t, "notes", v, sizeof v);
                unesc(v, it->notes, sizeof it->notes);
                for (const char *s = t; mx && (s = strstr(s, "[row]")); s += 5) it->rows++;
                free(t);
            } while (FindNextFileA(h, &f));
            FindClose(h);
        }
    }
    qsort(P->it, (size_t)P->n, sizeof *P->it, by_name);
    if (P->sel >= P->n) P->sel = -1;
}
static int item_find(ed_t *e, const char *name, int factory) {
    for (int k = 0; k < PA(e)->n; k++)
        if (PA(e)->it[k].factory == factory && !_stricmp(PA(e)->it[k].name, name)) return k;
    return -1;
}

/* ---- effect presets ---- */
static char *fx_text(ed_t *e, int s, const char *name, const char *notes) {
    static int idx[256];
    int n = slot_params(e, s, idx);
    size_t cap = 4096 + 2 * strlen(notes);
    char *out = malloc(cap), when[24], en[NOTES_MAX * 2 + 8];
    if (!out) return NULL;
    now_text(when, sizeof when);
    esc(notes, en, sizeof en);
    int used = snprintf(out, cap, "%s 1\r\nname=%s\r\ntype=%s\r\ntime=%s\r\nnotes=%s\r\n", FX_HEADER, name,
                        type_now(e, s), when, en);
    for (int k = 0; k < n && used < (int)cap - 80; k++) {
        char v[64];
        value_text(e, idx[k], v, sizeof v);
        used += snprintf(out + used, cap - used, "%s=%s\r\n", suffix(s, idx[k]), v);
    }
    return out;
}
/* the slot's controls at their defaults (the type to `type` unless NULL), then the text's "control=value" lines
 * (vals: ";" or line breaks between) */
static int fx_apply(ed_t *e, int s, const char *type, const char *vals, const char *label) {
    static int idx[257];
    static float v[257];
    int n = slot_params(e, s, idx);
    for (int k = 0; k < n; k++) v[k] = PARAMS[idx[k]].def;
    if (type) {
        int o = type_option(e, s, type), t = slot_type(e, s);
        if (o < 0) { say(e, "%s has no %s type: this preset can't load there.", SLOT_NAME[s], type); return 0; }
        memmove(idx + 1, idx, sizeof idx[0] * (size_t)n);   /* (the type first: the engine switches, then the values) */
        memmove(v + 1, v, sizeof v[0] * (size_t)n);
        idx[0] = t;
        v[0] = PARAMS[t].nopts > 1 ? o / (float)(PARAMS[t].nopts - 1) : 0;
        n++;
    }
    char *copy = strdup(vals ? vals : ""), *save = NULL;
    for (char *kv = copy ? strtok_r(copy, ";\r\n", &save) : NULL; kv; kv = strtok_r(NULL, ";\r\n", &save)) {
        char *eq = strchr(kv, '=');
        if (!eq) continue;
        *eq = 0;
        for (int k = 0; k < n; k++)
            if (idx[k] != slot_type(e, s) && !strcmp(suffix(s, idx[k]), kv)) { v[k] = norm_of(&PARAMS[idx[k]], eq + 1); break; }
    }
    free(copy);
    apply(e, idx, v, n, label);
    return 1;
}
static void fx_values(const char *t, char *out, int n) {   /* a file's control lines (not its name, type, time, notes) */
    int used = 0;
    out[0] = 0;
    for (const char *s = t; s && *s;) {
        while (*s == '\r' || *s == '\n') s++;
        int len = (int)strcspn(s, "\r\n");
        if (len && strchr(s, '=') && strchr(s, '=') - s < len && strncmp(s, "name=", 5) && strncmp(s, "type=", 5) &&
            strncmp(s, "time=", 5) && strncmp(s, "notes=", 6) && strncmp(s, FX_HEADER, sizeof FX_HEADER - 1))
            used += snprintf(out + used, used < n ? n - used : 0, "%.*s;", len, s);
        s += len;
    }
}
static int fx_load(ed_t *e, int k) {
    parts_ui_t *P = PA(e);
    const pitem_t *it = &P->it[k];
    char label[64];
    snprintf(label, sizeof label, "Load effect '%s'", it->name);
    if (it->factory) {
        for (int f = 0; f < NFACTORY; f++)
            if (!strcmp(FACTORY[f].name, it->name) && fx_apply(e, P->slot, FACTORY[f].type, FACTORY[f].vals, label)) {
                say(e, "%s: '%s' (%s) loaded.", SLOT_NAME[P->slot], it->name, it->type);
                return 1;
            }
        return 0;
    }
    char path[MAX_PATH];
    char *t = item_path(0, it->name, path, sizeof path, 0) ? pt_read(path) : NULL;
    if (!t) { say(e, "Could not open '%s'.", it->name); return 0; }
    static char vals[16384];
    char type[64];
    line_of(t, "type", type, sizeof type);
    fx_values(t, vals, sizeof vals);
    free(t);
    if (!fx_apply(e, P->slot, type, vals, label)) return 0;
    say(e, "%s: '%s' (%s) loaded.", SLOT_NAME[P->slot], it->name, type);
    return 1;
}

/* ---- matrix presets ---- */
static char *mx_text(ed_t *e, const char *name, const char *notes) {
    size_t cap = 8192 + 2 * strlen(notes);
    char *out = malloc(cap), when[24], en[NOTES_MAX * 2 + 8];
    if (!out) return NULL;
    now_text(when, sizeof when);
    esc(notes, en, sizeof en);
    int used = snprintf(out, cap, "%s 1\r\nname=%s\r\ntime=%s\r\nnotes=%s\r\n", MX_HEADER, name, when, en);
    for (int r = 0; r < MX_ROWS; r++) {
        if (row_free(e, r)) continue;
        used += snprintf(out + used, cap - used, "[row]\r\nrow=%d\r\n", r + 1);
        for (int f = 0; f < MX_F; f++) {
            int i = mx_index(e, r, f);
            if (i < 0) continue;
            char v[64];
            value_text(e, i, v, sizeof v);
            used += snprintf(out + used, cap - used, "%s=%s\r\n", MX_SFX[f], v);
        }
    }
    return out;
}
static void row_empty(ed_t *e, int r, int *idx, float *v, int *n) {   /* every field at its default, SOURCE / DEST / VIA off */
    for (int f = 0; f < MX_F; f++) {
        int i = mx_index(e, r, f);
        if (i < 0) continue;
        idx[*n] = i;
        v[*n] = f == 0 || f == 1 || f == 3 ? 0.0f : PARAMS[i].def;
        (*n)++;
    }
}
/* the preset's rows into the focused Synth: replace 0 into its free rows (in order), 1 into the rows they were saved
 * from ("row=", else the first one left; every other row emptied) */
static int mx_load(ed_t *e, int k, int replace) {
    const pitem_t *it = &PA(e)->it[k];
    char path[MAX_PATH];
    char *t = item_path(1, it->name, path, sizeof path, 0) ? pt_read(path) : NULL;
    if (!t) { say(e, "Could not open '%s'.", it->name); return 0; }
    const char *blk[MX_ROWS * 4];
    int nb = 0, taken[MX_ROWS] = {0}, at[MX_ROWS * 4];
    for (const char *s = strstr(t, "[row]"); s && nb < MX_ROWS * 4; s = strstr(s + 5, "[row]")) blk[nb++] = s;
    for (int b = 0; b < nb; b++) {   /* where each block goes (-1: no room) */
        at[b] = -1;
        if (!replace) continue;
        const char *rn = strstr(blk[b], "row=");
        int want = rn && (b + 1 == nb || rn < blk[b + 1]) ? atoi(rn + 4) - 1 : -1;
        if (want >= 0 && want < MX_ROWS && !taken[want]) { at[b] = want; taken[want] = 1; }
    }
    for (int b = 0; b < nb; b++) {
        if (at[b] >= 0) continue;
        for (int r = 0; r < MX_ROWS && at[b] < 0; r++)
            if (!taken[r] && (replace || row_free(e, r))) { at[b] = r; taken[r] = 1; }
    }
    static int idx[MX_ROWS * MX_F * 2];
    static float v[MX_ROWS * MX_F * 2];
    int n = 0, placed = 0, rows = nb;
    if (replace)
        for (int r = 0; r < MX_ROWS; r++) row_empty(e, r, idx, v, &n);
    for (int b = 0; b < nb; b++) {
        const char *s = blk[b];
        if (at[b] < 0) continue;
        placed++;
        int r = at[b], first = n;
        if (!replace) row_empty(e, r, idx, v, &n);
        const char *end = b + 1 < nb ? blk[b + 1] : NULL;
        for (const char *l = s + 5; l && *l && (!end || l < end);) {
            while (*l == '\r' || *l == '\n') l++;
            int len = (int)strcspn(l, "\r\n");
            char kv[128];
            snprintf(kv, sizeof kv, "%.*s", len, l);
            l += len;
            char *eq = strchr(kv, '=');
            if (!eq) continue;
            *eq = 0;
            for (int f = 0; f < MX_F; f++) {
                int i = mx_index(e, r, f);
                if (i < 0 || strcmp(kv, MX_SFX[f])) continue;
                for (int j = replace ? 0 : first; j < n; j++)
                    if (idx[j] == i) v[j] = norm_of(&PARAMS[i], eq + 1);
            }
        }
    }
    free(t);
    char label[64];
    snprintf(label, sizeof label, "%s matrix '%s'", replace ? "Replace with" : "Add", it->name);
    apply(e, idx, v, n, label);
    if (placed < rows)
        say(e, "Synth %d: %d of the %d rows of '%s' added; the matrix had no more free rows.", e->focus + 1, placed, rows,
            it->name);
    else
        say(e, "Synth %d: '%s' %s (%d rows).", e->focus + 1, it->name, replace ? "replaces the matrix" : "added", rows);
    return 1;
}

/* ---- the PARTS page's buttons ---- */
static int need_sel(ed_t *e, int user) {
    parts_ui_t *P = PA(e);
    if (P->sel < 0 || P->sel >= P->n) { say(e, "Click a preset in the list first."); return 0; }
    if (user && P->it[P->sel].factory) { say(e, "'%s' is one of Moo Won's own presets: it can't be changed.", P->it[P->sel].name); return 0; }
    return 1;
}
static void save_part(ed_t *e) {
    parts_ui_t *P = PA(e);
    int mx = P->slot == PS_MTX;
    char name[PATCH_NAME_MAX + 1], notes[NOTES_MAX + 1] = "", path[MAX_PATH], q[200];
    if (P->sel >= 0 && P->sel < P->n && !P->it[P->sel].factory) {
        snprintf(name, sizeof name, "%s", P->it[P->sel].name);
        snprintf(notes, sizeof notes, "%s", P->it[P->sel].notes);
    } else {
        snprintf(name, sizeof name, "%s", mx ? "My Matrix" : type_now(e, P->slot));
    }
    if (mx && !rows_used(e)) { say(e, "Synth %d's matrix has no rows in use: nothing to save.", e->focus + 1); return; }
    if (!lib_ask(e, DK_PNAME, mx ? "Save matrix preset" : "Save effect preset",
                 mx ? "A name for this matrix (up to 32 characters):" : "A name for this effect's settings (up to 32 characters):",
                 name, sizeof name))
        return;
    if (!item_path(mx, name, path, sizeof path, 1)) { say(e, "Could not make the folder for the presets."); return; }
    if (file_exists(path)) {
        snprintf(q, sizeof q, "A %s preset named '%s' already exists.\nReplace it?", mx ? "matrix" : "effect", name);
        if (!pt_confirm(e, q)) return;
    }
    if (!lib_ask(e, DK_NOTES, "Notes", "Notes for this preset (optional):", notes, sizeof notes)) return;
    char *t = mx ? mx_text(e, name, notes) : fx_text(e, P->slot, name, notes);
    int ok = t && pt_write(path, t);
    free(t);
    if (!ok) { say(e, "Could not save '%s': %s.", name, pt_why(GetLastError())); return; }
    rescan(e);
    P->sel = item_find(e, name, 0);
    if (P->sel >= 0) e->sub = P->sel / PER;
    if (mx) say(e, "Saved Synth %d's matrix (%d rows) as '%s'.", e->focus + 1, rows_used(e), name);
    else say(e, "Saved %s's settings (%s) as '%s'.", SLOT_NAME[P->slot], type_now(e, P->slot), name);
}
static void notes_part(ed_t *e) {
    parts_ui_t *P = PA(e);
    if (!need_sel(e, 1)) return;
    int mx = P->slot == PS_MTX;
    char path[MAX_PATH], notes[NOTES_MAX + 1], name[PATCH_NAME_MAX + 1];
    snprintf(name, sizeof name, "%s", P->it[P->sel].name);
    char *t = item_path(mx, name, path, sizeof path, 0) ? pt_read(path) : NULL;
    if (!t) { say(e, "Could not open '%s'.", name); return; }
    char v[NOTES_MAX * 2 + 8], en[NOTES_MAX * 2 + 8];
    line_of(t, "notes", v, sizeof v);
    unesc(v, notes, sizeof notes);
    if (!lib_ask(e, DK_NOTES, "Notes", "Notes for this preset:", notes, sizeof notes)) { free(t); return; }
    esc(notes, en, sizeof en);
    size_t cap = strlen(t) + strlen(en) + 64;
    char *out = malloc(cap);
    int used = 0, done = 0;
    for (const char *s = t; out && *s;) {   /* the notes line replaced (or added after the header) */
        int len = (int)strcspn(s, "\r\n"), brk = (int)strspn(s + len, "\r\n");
        if (!strncmp(s, "notes=", 6) && !done) { used += snprintf(out + used, cap - used, "notes=%s\r\n", en); done = 1; }
        else used += snprintf(out + used, cap - used, "%.*s\r\n", len, s);
        s += len + brk;
    }
    if (out && !done) used += snprintf(out + used, cap - used, "notes=%s\r\n", en);
    int ok = out && pt_write(path, out);
    free(out);
    free(t);
    if (!ok) { say(e, "Could not write '%s'.", name); return; }
    rescan(e);
    P->sel = item_find(e, name, 0);
    say(e, "Notes of '%s' changed.", name);
}
static void delete_part(ed_t *e) {
    parts_ui_t *P = PA(e);
    if (!need_sel(e, 1)) return;
    char path[MAX_PATH], q[200], name[PATCH_NAME_MAX + 1];
    snprintf(name, sizeof name, "%s", P->it[P->sel].name);
    snprintf(q, sizeof q, "Delete the preset '%s'?", name);
    if (!pt_confirm(e, q)) return;
    if (!item_path(P->slot == PS_MTX, name, path, sizeof path, 0) || !DeleteFileA(path)) {
        say(e, "Could not delete '%s': %s.", name, pt_why(GetLastError()));
        return;
    }
    P->sel = -1;
    rescan(e);
    say(e, "Deleted '%s'.", name);
}
static void act(ed_t *e, int a) {
    parts_ui_t *P = PA(e);
    switch (a) {
    case A_SAVE: save_part(e); break;
    case A_LOAD: if (need_sel(e, 0)) fx_load(e, P->sel); break;
    case A_ADD: if (need_sel(e, 0)) mx_load(e, P->sel, 0); break;
    case A_REPLACE: if (need_sel(e, 0)) mx_load(e, P->sel, 1); break;
    case A_INIT: {
        char label[48];
        snprintf(label, sizeof label, "Init %s", SLOT_NAME[P->slot]);
        if (fx_apply(e, P->slot, NULL, NULL, label))
            say(e, "%s: %s's controls back to their defaults (ON and the sends as they were).", SLOT_NAME[P->slot],
                type_now(e, P->slot));
        break;
    }
    case A_NOTES: notes_part(e); break;
    case A_DELETE: delete_part(e); break;
    case A_ALL: P->all_types = !P->all_types; P->sel = -1; e->sub = 0; rescan(e); break;
    }
    redraw(e);
}

/* ---- the Synths' names ---- */
int synname_ok(const char *s, char *why, int n) {
    const char *bad = NULL;
    if (strlen(s) > SYNNAME_MAX) bad = "a Synth's name is at most 12 characters";
    for (const char *p = s; !bad && *p; p++)
        if ((unsigned char)*p < 32 || (unsigned char)*p > 126) bad = "use plain letters, numbers, spaces and punctuation";
        else if (strchr(";|\\", *p)) bad = "a Synth's name can't hold ; | or \\";
    if (bad && why) snprintf(why, n, "%s", bad);
    return bad == NULL;
}
int parts_name(ed_t *e, int c, char *out, int n) {
    char k[24];
    out[0] = 0;
    if (c) snprintf(k, sizeof k, "s%d_syn_name", c + 1);
    else snprintf(k, sizeof k, "syn_name");
    if (!e->hr || hr_peek(e->hr, k, out, n) <= 0) out[0] = 0;
    return out[0] != 0;
}
void parts_label(ed_t *e, int c, char *out, int n) {
    if (!parts_name(e, c, out, n)) snprintf(out, n, "SYNTH %d", c + 1);
}
int parts_names_line(ed_t *e, char *out, int n) {
    int used = 0;
    out[0] = 0;
    for (int c = 0; c < NSYNTHS; c++) {
        char nm[SYNNAME_MAX + 1];
        if (parts_name(e, c, nm, sizeof nm)) used += snprintf(out + used, used < n ? n - used : 0, "%sS%d %s", used ? "  " : "", c + 1, nm);
    }
    return out[0] != 0;
}
static void name_send(ed_t *e, int c, const char *name) {
    char cmd[64];
    snprintf(cmd, sizeof cmd, "synname%d\n%s", c + 1, name);
    wrapped_call(e->fx, effSetChunk, 0, (intptr_t)strlen(cmd) + 1, cmd, 0);
}
void parts_names_clear(ed_t *e) {
    char nm[SYNNAME_MAX + 1];
    for (int c = 0; c < NSYNTHS; c++)
        if (parts_name(e, c, nm, sizeof nm)) name_send(e, c, "");
}
static void name_synth(ed_t *e, int c) {
    char nm[SYNNAME_MAX + 1], prompt[96], title[32];
    parts_name(e, c, nm, sizeof nm);
    snprintf(title, sizeof title, "Name Synth %d", c + 1);
    snprintf(prompt, sizeof prompt, "A name for Synth %d (up to 12 characters; empty: no name):", c + 1);
    if (!lib_ask(e, DK_SYNNAME, title, prompt, nm, sizeof nm)) return;
    char label[32];
    snprintf(label, sizeof label, "Name Synth %d", c + 1);
    lib_touch(e, label);   /* one undo step (the name is part of the sound's state) */
    name_send(e, c, nm);
    lib_commit(e);
    e->pt.forced_edit = 1;   /* (the * and SAVE: the sound changed) */
    if (nm[0]) say(e, "Synth %d is '%s' now (saved with the sound).", c + 1, nm);
    else say(e, "Synth %d has no name now.", c + 1);
    redraw(e);
}
RECT parts_name_head(int c) {
    RECT h = setup_cell(0, c);
    h.top = L.head.top;
    h.bottom = L.head.bottom;
    h.left += sc(1);
    return h;
}
void parts_tick(ed_t *e) {
    parts_ui_t *P = PA(e);
    int changed = 0;
    for (int c = 0; c < NSYNTHS; c++) {
        char nm[SYNNAME_MAX + 1];
        parts_name(e, c, nm, sizeof nm);
        if (strcmp(nm, P->shown[c])) { snprintf(P->shown[c], sizeof P->shown[c], "%s", nm); changed = 1; }
    }
    if (changed) redraw(e);
}

/* ---- user libraries ---- */
static int libs_root(char *out, int n) {   /* the folder the libraries are in (not made) */
    DWORD k = GetEnvironmentVariableA("ONEISH_LIB_DIR", out, n);
    if (k > 0 && k < (DWORD)n) return 1;
    char d[MAX_PATH];
    if (!patch_home_dir(d, sizeof d, 0)) return 0;
    char *slash = strrchr(d, '\\');
    if (slash) *slash = 0;
    return snprintf(out, n, "%s\\Libraries", d) < n;
}
int space_dir(const char *name, char *out, int n, int create) {
    char root[MAX_PATH];
    if (!libs_root(root, sizeof root) || snprintf(out, n, "%s\\%s", root, name) >= n) return 0;
    if (create) SHCreateDirectoryExA(NULL, out, NULL);
    DWORD a = GetFileAttributesA(out);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static int by_iname(const void *a, const void *b) { return _stricmp(a, b); }
static int spaces(char (*out)[PATCH_NAME_MAX + 1], int max) {   /* "" (the default), then the others A to Z */
    int n = 0;
    out[n++][0] = 0;
    char root[MAX_PATH], pat[MAX_PATH + 8];
    if (!libs_root(root, sizeof root)) return n;
    snprintf(pat, sizeof pat, "%s\\*", root);
    WIN32_FIND_DATAA f;
    HANDLE h = FindFirstFileA(pat, &f);
    if (h == INVALID_HANDLE_VALUE) return n;
    do {
        if (!(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || f.cFileName[0] == '.' || !patch_name_ok(f.cFileName, NULL, 0) ||
            !_stricmp(f.cFileName, "Default"))
            continue;
        if (n < max) snprintf(out[n++], PATCH_NAME_MAX + 1, "%s", f.cFileName);
    } while (FindNextFileA(h, &f));
    FindClose(h);
    qsort(out + 1, (size_t)(n - 1), sizeof out[0], by_iname);
    return n;
}
static const char *space_label(const char *name) { return name[0] ? name : "Default"; }
static int lib_folder(const char *name, char *out, int n, int create) {   /* "" the default library's */
    return name[0] ? space_dir(name, out, n, create) : patch_home_dir(out, n, create);
}
static int count_patches(const char *name) {
    char d[MAX_PATH], pat[MAX_PATH + 16];
    if (!lib_folder(name, d, sizeof d, 0)) return 0;
    snprintf(pat, sizeof pat, "%s\\*.oneish", d);
    WIN32_FIND_DATAA f;
    HANDLE h = FindFirstFileA(pat, &f);
    int n = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do n += !(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY); while (FindNextFileA(h, &f));
    FindClose(h);
    return n;
}
static void space_follow(ed_t *e) {   /* every instance: the patches of the library now in use */
    lib_space_changed(e);
    if (e->pt.bank == BANK_USER) {   /* its patch file is in the other library: the sound stays, unsaved here */
        e->pt.bank = BANK_NONE;
        e->pt.forced_edit = 1;
        lib_drop_base(e);
    }
    patches_rescan(e);
    if (e->screen == SCR_PARTS) rescan(e);
    redraw(e);
}
static void space_use(ed_t *e, const char *name) {
    if (!settings_space_set(name)) say(e, "Couldn't save the settings file; the library is used until you close.");
    ed_each(space_follow);
    say(e, "Now using the library '%s' (%d patches).", space_label(name), count_patches(name));
}
static void space_step(ed_t *e, int d) {
    static char list[256][PATCH_NAME_MAX + 1];
    char cur[PATCH_NAME_MAX + 1];
    int n = spaces(list, 256), at = 0;
    settings_space(cur, sizeof cur);
    for (int k = 0; k < n; k++)
        if (!_stricmp(list[k], cur)) at = k;
    if (n < 2) { say(e, "There is only the Default library: NEW EMPTY or NEW + FACTORY makes another."); return; }
    space_use(e, list[(at + d + n) % n]);
}
static void clean_name(const char *in, char *out) {   /* a factory preset's name as a patch name */
    char t[PATCH_NAME_MAX + 1];
    snprintf(t, sizeof t, "%s", in);
    for (char *c = t; *c; c++)
        if ((unsigned char)*c < 32 || (unsigned char)*c > 126 || strchr("\\/:*?\"<>|;", *c)) *c = '-';
    size_t l = strlen(t);
    while (l && (t[l - 1] == ' ' || t[l - 1] == '.')) t[--l] = 0;
    char *s = t;
    while (*s == ' ') s++;
    snprintf(out, PATCH_NAME_MAX + 1, "%s", patch_name_ok(s, NULL, 0) ? s : "Preset");
}
static int write_factory(ed_t *e) {   /* every factory preset as a user patch of the library in use; how many */
    int done = 0;
    static char state[PATCH_STATE_MAX];
    for (int p = 0; p < e->fx->numPrograms; p++) {
        const char *pname = NULL;
        if (!patches_factory_state(p, state, sizeof state, &pname)) continue;
        char name[PATCH_NAME_MAX + 1], free_[PATCH_NAME_MAX + 1], path[MAX_PATH], meta[256] = "";
        clean_name(pname, name);
        if (pt_patch_path(name, path, sizeof path, 1) && file_exists(path) && patches_free_name(name, free_))
            snprintf(name, sizeof name, "%s", free_);
        if (p < NFACTORY_TAGS) {
            int u = snprintf(meta, sizeof meta, "type=%s\n", FACTORY_TAG[p].type);
            if (FACTORY_TAG[p].cat[0]) u += snprintf(meta + u, sizeof meta - u, "category=%s\n", FACTORY_TAG[p].cat);
            if (FACTORY_TAG[p].mood[0]) snprintf(meta + u, sizeof meta - u, "mood=%s\n", FACTORY_TAG[p].mood);
        }
        char *t = pt_build(name, meta, state);
        if (t && pt_patch_path(name, path, sizeof path, 1) && pt_write(path, t)) done++;
        free(t);
    }
    return done;
}
static void space_new(ed_t *e, int factory) {
    char name[PATCH_NAME_MAX + 1] = "", d[MAX_PATH];
    if (!lib_ask(e, DK_PNAME, factory ? "New library with the factory sounds" : "New empty library",
                 "A name for the new user library (up to 32 characters):", name, sizeof name))
        return;
    if (!_stricmp(name, "Default") || space_dir(name, d, sizeof d, 0)) {
        say(e, "There is a library named '%s' already: pick another name.", name);
        return;
    }
    if (!space_dir(name, d, sizeof d, 1)) { say(e, "Could not make the library's folder: %s.", pt_why(GetLastError())); return; }
    space_use(e, name);
    if (factory) {
        int n = write_factory(e);
        ed_each(space_follow);
        say(e, "New library '%s' with the %d factory sounds as patches of its own, in use now.", name, n);
    } else {
        say(e, "New empty library '%s', in use now. Your other patches are in their own libraries (< >).", name);
    }
}
static void space_rename(ed_t *e) {
    char cur[PATCH_NAME_MAX + 1], to[PATCH_NAME_MAX + 1], a[MAX_PATH], b[MAX_PATH];
    settings_space(cur, sizeof cur);
    if (!cur[0]) { say(e, "The Default library keeps its name (it is your patch folder). Switch to another to rename it."); return; }
    snprintf(to, sizeof to, "%s", cur);
    if (!lib_ask(e, DK_PNAME, "Rename library", "A new name for this user library:", to, sizeof to) || !strcmp(to, cur)) return;
    if (!_stricmp(to, "Default") || (_stricmp(to, cur) && space_dir(to, b, sizeof b, 0))) {
        say(e, "There is a library named '%s' already.", to);
        return;
    }
    if (!space_dir(cur, a, sizeof a, 0) || !libs_root(b, sizeof b)) return;
    snprintf(b + strlen(b), sizeof b - strlen(b), "\\%s", to);
    if (!MoveFileA(a, b)) { say(e, "Could not rename the library: %s.", pt_why(GetLastError())); return; }
    settings_space_set(to);
    ed_each(space_follow);
    say(e, "The library '%s' is '%s' now.", cur, to);
}
/* COPY SOUND TO: the sound playing as a patch in another library (its name and tags; DUPLICATE NAMES decides a clash) */
static void space_copy(ed_t *e) {
    static char list[256][PATCH_NAME_MAX + 1];
    char cur[PATCH_NAME_MAX + 1];
    int n = spaces(list, 256), pick = -1, map[256], m = 0;
    settings_space(cur, sizeof cur);
    for (int k = 0; k < n; k++)
        if (_stricmp(list[k], cur)) map[m++] = k;
    if (!m) { say(e, "There is no other library to copy to: NEW EMPTY makes one."); return; }
    if (PA(e)->answer > 0) {
        pick = PA(e)->answer <= m ? map[PA(e)->answer - 1] : -1;
        PA(e)->answer = 0;
    } else if (e->wnd) {
        HMENU menu = CreatePopupMenu();
        for (int j = 0; j < m; j++) AppendMenuA(menu, MF_STRING, (UINT_PTR)(j + 1), space_label(list[map[j]]));
        POINT p;
        GetCursorPos(&p);
        int r = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, e->wnd, NULL);
        DestroyMenu(menu);
        if (r > 0) pick = map[r - 1];
    }
    if (pick < 0) return;
    const char *to = list[pick];
    char name[PATCH_NAME_MAX + 1], dir[MAX_PATH], path[MAX_PATH + 64], meta[2 * NOTES_MAX + 256] = "", q[240];
    if (e->pt.bank == BANK_USER || e->pt.name[0]) snprintf(name, sizeof name, "%s", e->pt.name);
    else if (e->pt.bank == BANK_FACTORY && e->pt.prog >= 0) {
        char t[64] = "";
        wrapped_call(e->fx, effGetProgramNameIndexed, e->pt.prog, 0, t, 0);
        clean_name(t, name);
    } else snprintf(name, sizeof name, "Sound");
    if (!patch_name_ok(name, NULL, 0)) clean_name(name, name);
    if (!lib_folder(to, dir, sizeof dir, 1)) { say(e, "Could not open the library '%s'.", space_label(to)); return; }
    snprintf(path, sizeof path, "%s\\%s.oneish", dir, name);
    if (file_exists(path)) {
        int mode = settings_import_dup();
        if (mode == DUP_IGNORE) { say(e, "'%s' is in '%s' already: not copied (DUPLICATE NAMES is IGNORE).", name, space_label(to)); return; }
        if (mode == DUP_RENAME) {
            for (int k = 2; k < 1000; k++) {
                char t[PATCH_NAME_MAX + 1], sfx[8];
                int sl = snprintf(sfx, sizeof sfx, " %d", k);
                snprintf(t, sizeof t, "%.*s%s", PATCH_NAME_MAX - sl, name, sfx);
                snprintf(path, sizeof path, "%s\\%s.oneish", dir, t);
                if (patch_name_ok(t, NULL, 0) && !file_exists(path)) { snprintf(name, sizeof name, "%s", t); break; }
            }
        }
        if (mode == DUP_ASK) {
            snprintf(q, sizeof q, "The library '%s' has a patch named '%s' already.\nReplace it with this sound?",
                     space_label(to), name);
            if (!pt_confirm(e, q)) return;
        }
    }
    lib_end_compare(e);
    void *chunk = NULL;
    intptr_t len = wrapped_call(e->fx, effGetChunk, 0, 0, &chunk, 0);
    if (len <= 0 || !chunk) { say(e, "Could not read the sound from the engine."); return; }
    if (e->lib) lib_meta_lines(&e->lib->cur, meta, sizeof meta);
    char *t = pt_build(name, meta, (const char *)chunk);
    int ok = t && pt_write(path, t);
    free(t);
    if (!ok) { say(e, "Could not copy to '%s': %s.", space_label(to), pt_why(GetLastError())); return; }
    say(e, "Copied the sound as '%s' into the library '%s'.", name, space_label(to));
}

/* ---- the SETTINGS page's LIBRARY rows (settings.c draws the rest) ---- */
static void row_geom(int row, RECT *name, RECT *val) {   /* as settings.c's rows: the label, then the value */
    RECT r = L.row[row];
    int lw = rw(&r) * 46 / 100;
    SetRect(name, r.left + sc(4), r.top, r.left + lw, r.bottom);
    SetRect(val, r.left + lw + sc(4), r.top + sc(2), r.right - sc(4), r.bottom - sc(2));
}
static RECT lib_rect(int k) {
    RECT nm, v, r;
    if (k == PL_PREV || k == PL_NEXT || k == PL_DUP_DOWN || k == PL_DUP_UP) {
        row_geom(k <= PL_NEXT ? 0 : 2, &nm, &v);
        r = v;
        if (k == PL_PREV || k == PL_DUP_DOWN) r.right = r.left + sc(18);
        else r.left = r.right - sc(18);
        return r;
    }
    r = L.row[1];   /* NEW EMPTY, NEW + FACTORY, RENAME, COPY SOUND TO across the row */
    InflateRect(&r, -sc(4), -sc(2));
    int gap = sc(6), bw = (rw(&r) - 3 * gap) / 4, j = k - PL_NEW;
    r.left += j * (bw + gap);
    r.right = r.left + bw;
    return r;
}
int parts_lib_hit(ed_t *e, POINT pt) {
    for (int k = 0; k < PL_COUNT; k++) {
        RECT r = lib_rect(k);
        if (PtInRect(&r, pt)) return ZP_LIB + k;
    }
    return -1;
}
#define P_LCD RGB(150, 215, 232)
#define P_DIM RGB(90, 130, 145)
#define P_HOT RGB(255, 255, 255)
#define P_FRAME RGB(0x27, 0x40, 0x4E)
#define P_FRAME_HOT RGB(0x96, 0xD7, 0xE8)
#define P_FILL_HOT RGB(0x16, 0x2C, 0x3A)
#define P_SEL RGB(0x1F, 0x46, 0x5A)
static void fill(HDC dc, RECT r, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b); }
static void frame(HDC dc, RECT r, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FrameRect(dc, &r, b); DeleteObject(b); }
static void label(HDC dc, RECT r, const char *s, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextA(dc, s, -1, &r, fmt | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}
static void button(HDC dc, RECT r, const char *t, int live, int lit, int hot, HFONT f) {
    if (lit) fill(dc, r, P_SEL);
    else if (hot) fill(dc, r, P_FILL_HOT);
    frame(dc, r, hot || lit ? P_FRAME_HOT : P_FRAME);
    label(dc, r, t, f, !live ? P_DIM : hot || lit ? P_HOT : P_LCD, DT_CENTER);
}
static void arrows_row(HDC dc, ed_t *e, int row, int down, int up, const char *name, const char *val) {
    RECT nm, v, d = lib_rect(down), u = lib_rect(up);
    row_geom(row, &nm, &v);
    int hot = e->hover == ZP_LIB + down || e->hover == ZP_LIB + up;
    label(dc, nm, name, font_cell, hot ? P_HOT : P_DIM, DT_LEFT);
    label(dc, d, "<", font_screen, e->hover == ZP_LIB + down ? P_HOT : P_LCD, DT_CENTER);
    label(dc, u, ">", font_screen, e->hover == ZP_LIB + up ? P_HOT : P_LCD, DT_CENTER);
    RECT m = {d.right, v.top, u.left, v.bottom};
    if (hot) frame(dc, m, P_FRAME_HOT);
    label(dc, m, val, font_cell, P_LCD, DT_CENTER);
}
void parts_lib_draw(HDC dc, ed_t *e) {
    char cur[PATCH_NAME_MAX + 1], v[96];
    settings_space(cur, sizeof cur);
    snprintf(v, sizeof v, "%s  (%d patches)", space_label(cur), count_patches(cur));
    arrows_row(dc, e, 0, PL_PREV, PL_NEXT, "USER LIBRARY", v);
    static const char *const B[4] = {"NEW EMPTY", "NEW + FACTORY", "RENAME", "COPY SOUND TO"};
    for (int k = PL_NEW; k <= PL_COPY; k++)
        button(dc, lib_rect(k), B[k - PL_NEW], k != PL_RENAME || cur[0], 0, e->hover == ZP_LIB + k, font_cell);
    arrows_row(dc, e, 2, PL_DUP_DOWN, PL_DUP_UP, "DUPLICATE NAMES (IMPORT)", DUP_NAME[settings_import_dup()]);
}

/* ---- the PARTS page ---- */
static int acts(ed_t *e, const int **list) {
    if (PA(e)->slot == PS_MTX) { *list = MX_ACTS; return sizeof MX_ACTS / sizeof MX_ACTS[0]; }
    *list = FX_ACTS;
    return sizeof FX_ACTS / sizeof FX_ACTS[0];
}
static RECT head_btn(int n, int k, const RECT *line) {
    int gap = sc(4), bw = (rw(line) - gap * (n - 1)) / n;
    RECT r = {line->left + k * (bw + gap), line->top + sc(1), line->left + k * (bw + gap) + bw, line->bottom - sc(1)};
    return r;
}
static RECT item_rect(int row) {   /* list row 0 .. PER - 1 */
    RECT r = L.row[2 + row];
    r.top += sc(1);
    r.bottom -= sc(1);
    return r;
}
int parts_zone_rect(ed_t *e, int z, RECT *r) {
    parts_ui_t *P = PA(e);
    if (e->screen == SCR_PARTS) {
        if (z >= ZP_SLOT && z < ZP_SLOT + PS_COUNT) { *r = head_btn(PS_COUNT, z - ZP_SLOT, &L.head); return 1; }
        const int *list;
        int na = acts(e, &list);
        for (int k = 0; k < na; k++)
            if (z == ZP_ACT + list[k]) { RECT line = L.row[1]; *r = head_btn(na, k, &line); return 1; }
        if (z == Z_SUB_NEXT && parts_subs(e) > 1) { *r = L.sub_next; return 1; }   /* (load_test: the next page) */
        if (z >= ZP_ITEM && z < ZP_ITEM + P->n) {
            int row = z - ZP_ITEM - e->sub * PER;
            if (row < 0 || row >= PER) return 0;
            *r = item_rect(row);
            return 1;
        }
        return 0;
    }
    if (z == ZP_MORE && parts_more_page(e->screen) >= 0) { *r = parts_more_rect(); return 1; }
    if (z >= ZP_NAME && z < ZP_NAME + NSYNTHS && e->screen == SCR_SETUP) { *r = parts_name_head(z - ZP_NAME); return 1; }
    if (z >= ZP_LIB && z < ZP_LIB + PL_COUNT && e->screen == SCR_SETTINGS && e->st.tab == ST_LIBRARY) {
        *r = lib_rect(z - ZP_LIB);
        return 1;
    }
    return 0;
}
int parts_hit(ed_t *e, POINT pt) {   /* the PARTS page, the effects' PRESETS, the SETUP page's heads */
    RECT r;
    if (e->screen == SCR_PARTS) {
        for (int k = 0; k < PS_COUNT; k++)
            if (parts_zone_rect(e, ZP_SLOT + k, &r) && PtInRect(&r, pt)) return ZP_SLOT + k;
        for (int k = 0; k < A_COUNT; k++)
            if (parts_zone_rect(e, ZP_ACT + k, &r) && PtInRect(&r, pt)) return ZP_ACT + k;
        for (int k = e->sub * PER; k < PA(e)->n && k < (e->sub + 1) * PER; k++)
            if (parts_zone_rect(e, ZP_ITEM + k, &r) && PtInRect(&r, pt)) return ZP_ITEM + k;
        return -1;
    }
    if (parts_zone_rect(e, ZP_MORE, &r) && PtInRect(&r, pt)) return ZP_MORE;
    for (int c = 0; c < NSYNTHS; c++)
        if (parts_zone_rect(e, ZP_NAME + c, &r) && PtInRect(&r, pt)) return ZP_NAME + c;
    return -1;
}
int parts_subs(ed_t *e) { return PA(e)->n > PER ? (PA(e)->n + PER - 1) / PER : 1; }
void parts_open(ed_t *e, int slot) {
    parts_ui_t *P = PA(e);
    if (slot >= 0 && slot < PS_COUNT) P->slot = slot;
    P->sel = -1;
    e->screen = SCR_PARTS;
    e->sub = 0;
    rescan(e);
    redraw(e);
}
int parts_more_page(int p) {
#if defined(PG_SFX) && defined(PG_DELAY) && defined(PG_REVERB)
    if (p == PG_SFX) return PS_SFX;
    if (p == PG_DELAY) return PS_FX1;
    if (p == PG_REVERB) return PS_FX2;
#endif
    (void)p;
    return -1;
}
RECT parts_more_rect(void) {
    RECT r = {L.head.right - sc(58), L.head.top + sc(1), L.head.right, L.head.bottom - sc(1)};
    return r;
}
void parts_more_draw(HDC dc, ed_t *e) {
    button(dc, parts_more_rect(), "PRESETS", 1, 0, e->hover == ZP_MORE, font_tiny);
}
void parts_draw(HDC dc, ed_t *e) {
    parts_ui_t *P = PA(e);
    for (int k = 0; k < PS_COUNT; k++) {
        RECT r = head_btn(PS_COUNT, k, &L.head);
        button(dc, r, SLOT_NAME[k], 1, P->slot == k, e->hover == ZP_SLOT + k, font_tiny);
    }
    char info[160];
    RECT ir = L.row[0];
    ir.left += sc(4);
    if (P->slot == PS_MTX) {
        int u = rows_used(e);
        snprintf(info, sizeof info, "SYNTH %d MATRIX: %d rows in use, %d free.  A preset, then ADD or REPLACE.",
                 e->focus + 1, u, MX_ROWS - u);
    } else {
        char who[24];
        if (P->slot == PS_SFX) snprintf(who, sizeof who, "SYNTH %d EFFECT", e->focus + 1);
        else snprintf(who, sizeof who, "%s", SLOT_NAME[P->slot]);
        snprintf(info, sizeof info, "%s: %s.  %s", who, type_now(e, P->slot),
                 P->all_types ? "Every type's presets." : "This type's presets (ALL TYPES: every type's).");
    }
    label(dc, ir, info, font_tiny, P_DIM, DT_LEFT);
    const int *list;
    int na = acts(e, &list);
    for (int k = 0; k < na; k++) {
        int a = list[k], live = a == A_SAVE || a == A_INIT || a == A_ALL || P->sel >= 0;
        if ((a == A_NOTES || a == A_DELETE) && (P->sel < 0 || P->it[P->sel].factory)) live = 0;
        RECT line = L.row[1];
        button(dc, head_btn(na, k, &line), ACT_NAME[a], live, a == A_ALL && P->all_types, e->hover == ZP_ACT + a, font_tiny);
    }
    if (!P->n) {
        RECT r = item_rect(0);
        r.left += sc(4);
        label(dc, r, P->slot == PS_MTX ? "No matrix presets yet: SAVE keeps this Synth's matrix."
                                       : "No presets of this type yet: SAVE keeps the settings (ALL TYPES: every type's).",
              font_cell, P_DIM, DT_LEFT);
    }
    for (int k = e->sub * PER; k < P->n && k < (e->sub + 1) * PER; k++) {
        const pitem_t *it = &P->it[k];
        RECT r = item_rect(k - e->sub * PER);
        int hot = e->hover == ZP_ITEM + k;
        if (k == P->sel) fill(dc, r, P_SEL);
        else if (hot) fill(dc, r, P_FILL_HOT);
        if (hot) frame(dc, r, P_FRAME_HOT);
        RECT a = r, b = r, c = r;
        a.left += sc(4);
        a.right = r.left + rw(&r) * 44 / 100;
        b.left = a.right + sc(4);
        b.right = r.left + rw(&r) * 66 / 100;
        c.left = b.right + sc(4);
        c.right -= sc(4);
        char nm[48], ty[40];
        snprintf(nm, sizeof nm, "%s  %s", it->factory ? "F" : "U", it->name);
        if (P->slot == PS_MTX) snprintf(ty, sizeof ty, "%d rows", it->rows);
        else snprintf(ty, sizeof ty, "%s", it->type);
        COLORREF col = hot || k == P->sel ? P_HOT : P_LCD;
        label(dc, a, nm, font_cell, col, DT_LEFT);
        label(dc, b, ty, font_tiny, col, DT_LEFT);
        label(dc, c, it->factory ? "Moo Won" : it->when, font_tiny, P_DIM, DT_RIGHT);
    }
}
void parts_help(ed_t *e, char *out, int n) {
    parts_ui_t *P = PA(e);
    const char *m = message(e);
    if (m) { snprintf(out, n, "%s", m); return; }
    if (P->sel >= 0 && P->sel < P->n) {
        const pitem_t *it = &P->it[P->sel];
        snprintf(out, n, "%s%s%s: %s", it->name, it->when[0] ? ", saved " : "", it->when, it->notes[0] ? it->notes : "(no notes)");
        return;
    }
    if (P->slot == PS_MTX)
        snprintf(out, n, "Matrix presets: SAVE keeps Synth %d's whole matrix by name (with notes and the time). Click "
                         "one, then ADD (into the free rows) or REPLACE (all rows). One undo step each.", e->focus + 1);
    else
        snprintf(out, n, "Effect presets: SAVE keeps this effect's settings by name. Click one, then LOAD. INIT: the "
                         "type's defaults. F: Moo Won's own. ON and the sends are never changed. One undo step each.");
}
int parts_press(ed_t *e, int z) {
    if (z < Z_PARTS || z >= Z_PARTS_END) return 0;
    parts_ui_t *P = PA(e);
    if (z >= ZP_SLOT && z < ZP_SLOT + PS_COUNT) { parts_open(e, z - ZP_SLOT); return 1; }
    if (z >= ZP_ACT && z < ZP_ACT + A_COUNT) { act(e, z - ZP_ACT); return 1; }
    if (z >= ZP_ITEM && z < ZP_ITEM + 100000) {
        int k = z - ZP_ITEM;
        P->sel = k < P->n ? k : -1;
        PA(e)->msg[0] = 0;
        redraw(e);
        return 1;
    }
    if (z == ZP_MORE) { parts_open(e, parts_more_page(e->screen)); return 1; }
    if (z >= ZP_NAME && z < ZP_NAME + NSYNTHS) { name_synth(e, z - ZP_NAME); return 1; }
    if (z >= ZP_LIB && z < ZP_LIB + PL_COUNT) {
        int k = z - ZP_LIB;
        PA(e)->msg[0] = 0;
        switch (k) {
        case PL_PREV: case PL_NEXT: space_step(e, k == PL_NEXT ? 1 : -1); break;
        case PL_NEW: case PL_NEW_FACTORY: space_new(e, k == PL_NEW_FACTORY); break;
        case PL_RENAME: space_rename(e); break;
        case PL_COPY: space_copy(e); break;
        case PL_DUP_DOWN: case PL_DUP_UP: {
            int v = (settings_import_dup() + (k == PL_DUP_UP ? 1 : DUP_COUNT - 1)) % DUP_COUNT;
            if (!settings_import_dup_set(v)) say(e, "Couldn't save the settings file.");
            break;
        }
        }
        snprintf(e->st.msg, sizeof e->st.msg, "%s", PA(e)->msg);   /* (the SETTINGS page shows its own message) */
        e->st.msg_at = PA(e)->msg_at;
        redraw(e);
        return 1;
    }
    return 1;
}
const char *parts_zone_help(ed_t *e, int z) {
    if (z >= ZP_SLOT && z < ZP_SLOT + PS_COUNT) {
        static const char *const H[PS_COUNT] = {
            "SYNTH FX: presets of the Synth in focus's SYNTH EFFECT (SYNTH 1/2/3 picks the Synth).",
            "MASTER FX 1: presets of master effect 1 (fed by each Synth's SEND 1).",
            "MASTER FX 2: presets of master effect 2 (fed by each Synth's SEND 2).",
            "MATRIX: presets of the whole mod matrix of the Synth in focus."};
        return H[z - ZP_SLOT];
    }
    switch (z - ZP_ACT) {
    case A_SAVE: return PA(e)->slot == PS_MTX ? "SAVE: Synth's rows in use as a matrix preset (asks a name, then notes)."
                                              : "SAVE: this effect's type and settings as a preset (asks a name, then notes).";
    case A_LOAD: return "LOAD: the selected preset into this effect: its type and settings (ON and the sends stay). One undo step.";
    case A_INIT: return "INIT: this effect's settings back to the type's defaults (the type, ON and the sends stay). One undo step.";
    case A_NOTES: return "NOTES: change the selected preset's notes.";
    case A_DELETE: return "DELETE: removes the selected preset (asks first). Moo Won's own (F) can't be deleted.";
    case A_ALL: return "ALL TYPES: lit, the list shows the presets of every type this effect can be; LOAD switches the type.";
    case A_ADD: return "ADD: the selected preset's rows go into this Synth's free rows (a row is free when SOURCE is Off). One undo step.";
    case A_REPLACE: return "REPLACE: the selected preset's rows become rows 1, 2 ...; the other rows are emptied. One undo step.";
    }
    if (z >= ZP_ITEM && z < ZP_ITEM + 100000) return "Click to select this preset (its notes show here); then LOAD, ADD or REPLACE.";
    if (z == ZP_MORE) return "PRESETS: save and load this effect's settings by name, or INIT them (the library's PARTS page).";
    if (z >= ZP_NAME && z < ZP_NAME + NSYNTHS) return "Click to name this Synth (\"Bass\", \"Pad\"; up to 12 characters). The "
                                                     "name is saved with the sound and shows under its SYNTH button.";
    switch (z - ZP_LIB) {
    case PL_PREV: case PL_NEXT: return "USER LIBRARY: < > switch to the previous or next library of your patches (each with "
                                       "its own snapshots, sets, tags, effect and matrix presets). Default: your patch folder.";
    case PL_NEW: return "NEW EMPTY: a new user library with no patches (asks a name); it is used at once.";
    case PL_NEW_FACTORY: return "NEW + FACTORY: a new user library starting with every factory sound as a patch of its own to edit.";
    case PL_RENAME: return "RENAME: a new name for the library in use (not Default).";
    case PL_COPY: return "COPY SOUND TO: the sound playing, as a patch in another library (pick it from the list).";
    case PL_DUP_DOWN: case PL_DUP_UP:
        return "DUPLICATE NAMES: when IMPORT (or COPY SOUND TO) meets a patch of the same name: ASK, OVERWRITE it, AUTO "
               "RENAME the new one (\"Name 2\") or IGNORE it (nothing imported).";
    }
    return NULL;
}

/* ---- load_test (effVendorSpecific TEST_PARTS; buf holds 1 KB) ---- */
intptr_t parts_test(ed_t *e, intptr_t op, char *buf, float opt) {
    parts_ui_t *P = PA(e);
    int k = (int)lroundf(opt);
    RECT r;
    switch (op) {
    case PAT_ZONE:
        if (!parts_zone_rect(e, k, &r)) return -1;
        return ((r.left + r.right) / 2) | ((intptr_t)((r.top + r.bottom) / 2) << 16);
    case PAT_SLOT: return P->slot;
    case PAT_COUNT: rescan(e); return P->n;
    case PAT_ITEM:
        if (k < 0 || k >= P->n) return 0;
        snprintf(buf, 1024, "%s|%s|%c|%d|%s|%s", P->it[k].name, P->it[k].type, P->it[k].factory ? 'F' : 'U', P->it[k].rows,
                 P->it[k].when, P->it[k].notes);
        return 1;
    case PAT_SEL: return P->sel;
    case PAT_NAME: return parts_name(e, k, buf, 1024);
    case PAT_SPACE: return settings_space(buf, 1024);
    case PAT_SPACES: {
        static char list[256][PATCH_NAME_MAX + 1];
        int n = spaces(list, 256), u = 0;
        buf[0] = 0;
        for (int j = 0; j < n && u < 1000; j++) u += snprintf(buf + u, 1024 - u, "%s%s", j ? "|" : "", space_label(list[j]));
        return n;
    }
    case PAT_DUP: return settings_import_dup();
    case PAT_MESSAGE: snprintf(buf, 1024, "%s", P->msg); return 1;
    case PAT_DIR: return kind_dir(k, buf, 1024, 0);
    case PAT_IMPORT: { char p[MAX_PATH]; snprintf(p, sizeof p, "%s", buf); return patches_import_choice(e, p); }
    case PAT_ANSWER: P->answer = k; return 1;
    case PAT_LIBDIR: {
        static char list[256][PATCH_NAME_MAX + 1];
        int n = spaces(list, 256);
        return k >= 0 && k < n && lib_folder(list[k], buf, 1024, 0);
    }
    }
    return 0;
}
