/* The library around the user patches (desktop editor only; docs/PATCHES.md). UI thread only, except
 * lib_midi_events, which the host calls with its MIDI (the audio thread): it only notes a program change for the
 * window's thread to load (lib_idle), so no file is touched there.
 *
 * Edit safety (audit package 5):
 *   UNDO / REDO   every edit from the window (knobs, buttons, lists, the matrix, the SETUP page, the step editor,
 *                 the randomiser, loads) is a step; a knob drag, a burst of mouse-wheel clicks or a randomise is one.
 *                 A step is the engine's whole state (all three Synths, the sequences) and which patch it was, so
 *                 undoing a load brings back the sound and its name. UNDO_MAX steps; Ctrl+Z / Ctrl+Y in the window,
 *                 UNDO / REDO on the SNAPSHOTS page. The host's own automation is not a step.
 *   COMPARE       plays the saved version of the patch; again: the edit. Loading another sound ends it (the edit is
 *                 autosaved), editing while comparing edits the saved version (the hidden edit becomes a snapshot).
 *   SNAPSHOT      timestamped saves of the sound with a note, in <patches>\Snapshots; browse, load, change the note,
 *                 delete, save one over its patch or as a new patch.
 *   AUTOSAVE      a snapshot of unsaved edits ("-auto") whenever another sound loads; the newest AUTOSAVE_KEEP kept.
 * Tags and notes (package 12a): TYPE (fixed list), CATEGORY, MOOD, GROUP (editable lists, <patches>\tags.oneishtags)
 *   and notes per patch, chosen in the save box, changed on the TAGS page; one filter per kind narrows the PATCHES page
 *   and PREV / NEXT; SHOW NOTES; the home line under the patch name shows the tags and the host's tempo.
 * Performance sets (package 12b): 128 sets of 64 patches (banks A-H x 1-8) in <patches>\sets.oneishsets; the SET page
 *   (the bank buttons preview a bank, a number loads, holding a number assigns the current patch), ALL SETS, ARRANGE
 *   (up, down, copy, delete, add, name, notes); MIDI bank select MSB 0 + LSB n and program p: list position 128n + p
 *   (factory presets, then user patches A to Z); MSB 1 + LSB n: set n + 1, program 0-63 = A1-H8. */
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "library.h"

#define SNAP_EXT ".oneishsnap"
static const char SNAP_HEADER[] = "ONE-ish snapshot", SETS_HEADER[] = "ONE-ish sets", TAGS_HEADER[] = "ONE-ish tags";
static const char *const TAG_KEY[TAG_KINDS] = {"type", "category", "mood", "group"};
static const char *const TAG_TITLE[TAG_KINDS] = {"TYPE", "CATEGORY", "MOOD", "GROUP"};
static const char *const TYPES[] = {"Single", "Layer", "Split", "Multi"};
static const char *const DEF_CAT[] = {"Bass", "Lead", "Pad", "Keys", "Strings", "Brass", "Bells", "Pluck", "Arp",
                                      "Sequence", "Drum", "FX"};
static const char *const DEF_MOOD[] = {"Bright", "Dark", "Warm", "Soft", "Aggressive", "Dirty", "Clean", "Evolving"};
static const char *const DEF_GROUP[] = {"Favourites", "Live", "Studio", "Work in progress"};
enum { PER_SNAPS = ROWS, PER_TAGS = ROWS - 1, PER_SETS = 2 * ROWS };
enum { NSNAP_ACT = 8, NTAG_ACT = 7, NARR_ACT = 7 };
enum { SA_TAKE, SA_LOAD, SA_NOTES, SA_DELETE, SA_SAVE_OVER, SA_SAVE_NEW, SA_UNDO, SA_REDO };
enum { TA_TAG, TA_ADD, TA_RENAME, TA_DELETE, TA_CLEAR, TA_NOTES, TA_SHOW };
enum { AA_UP, AA_DOWN, AA_COPY, AA_DELETE, AA_ADD, AA_NAME, AA_NOTES };
enum { SS_PREV, SS_NEXT, SS_ASSIGN, SS_CLEAR, NSET_SIDE };
static const char *const SNAP_ACT[NSNAP_ACT] = {"TAKE", "LOAD", "NOTES", "DELETE", "SAVE OVER", "SAVE NEW", "UNDO",
                                                "REDO"};
static const char *const TAG_ACT[NTAG_ACT] = {"TAG PATCH", "ADD", "RENAME", "DELETE", "CLEAR", "NOTES", "SHOW NOTES"};
static const char *const ARR_ACT[NARR_ACT] = {"UP", "DOWN", "COPY", "DELETE", "ADD", "NAME", "NOTES"};
/* the library's pages, as tabs on the title line */
static const int TAB_SCR[] = {SCR_PATCHES, SCR_SET, SCR_SETS, SCR_ARRANGE, SCR_SNAPS, SCR_TAGS};
static const char *const TAB_NAME[] = {"PATCHES", "SET", "ALL SETS", "ARRANGE", "SNAPSHOTS", "TAGS"};
static const short TAB_W[] = {52, 30, 52, 54, 66, 38};
enum { NTABS = sizeof TAB_SCR / sizeof TAB_SCR[0] };

#define LIB(e) ((e)->lib)
static void fresh(ed_t *e);
#define N_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ---- small helpers ---- */
static void say(ed_t *e, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pt_vsay(e, fmt, ap);
    va_end(ap);
}
static void redraw(ed_t *e) { if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE); }
static char *dup(const char *s) { return s ? strdup(s) : NULL; }
static void trim(char *s) {
    char *a = s;
    while (*a == ' ' || *a == '\t') a++;
    size_t n = strlen(a);
    while (n && (a[n - 1] == ' ' || a[n - 1] == '\t' || a[n - 1] == '\r')) n--;
    memmove(s, a, n);
    s[n] = 0;
}
static int param_key(const char *k) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) return i;
    return -1;
}
static int nfactory(ed_t *e) { return e->fx->numPrograms; }
static void prog_name(ed_t *e, int p, char *out) {
    char nm[64] = "";
    wrapped_call(e->fx, 29 /* effGetProgramNameIndexed */, p, 0, nm, 0);
    snprintf(out, PATCH_NAME_MAX + 1, "%s", nm);
}

/* notes on one line: "\n" a new line, "\s" a ';' (the patch format splits on it), "\\" a backslash */
static void esc(const char *in, char *out, int n) {
    int k = 0;
    for (; *in && k < n - 3; in++) {
        if (*in == '\r') continue;
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

/* the engine's whole state, malloc'd (NULL: none) */
static char *state_now(ed_t *e) {
    void *chunk = NULL;
    intptr_t len = wrapped_call(e->fx, effGetChunk, 0, 0, &chunk, 0);
    return len > 0 && chunk ? strdup((const char *)chunk) : NULL;
}

/* the value of "key=" in a text of lines (the first one), into out; 0 if none */
static int line_value(const char *t, const char *key, char *out, int n) {
    size_t kl = strlen(key);
    for (const char *s = t; *s;) {
        const char *eol = strchr(s, '\n');
        int len = eol ? (int)(eol - s) : (int)strlen(s);
        if (len > (int)kl && !strncmp(s, key, kl) && s[kl] == '=') {
            snprintf(out, n, "%.*s", len - (int)kl - 1, s + kl + 1);
            trim(out);
            return 1;
        }
        s += len + (eol ? 1 : 0);
    }
    if (n > 0) out[0] = 0;
    return 0;
}

/* text with every line starting "key=" for a key in keys[] removed and add inserted after the line starting with
 * `after` (or at the end); malloc'd */
static char *replace_lines(const char *t, const char *const *keys, int nkeys, const char *after, const char *add) {
    size_t cap = strlen(t) + strlen(add) + 16;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t used = 0;
    int put = 0;
    for (const char *s = t; *s;) {
        const char *eol = strchr(s, '\n');
        size_t len = eol ? (size_t)(eol - s) : strlen(s);
        int drop = 0;
        for (int k = 0; k < nkeys; k++) {
            size_t kl = strlen(keys[k]);
            if (len > kl && !strncmp(s, keys[k], kl) && s[kl] == '=') drop = 1;
        }
        if (!drop) {
            memcpy(out + used, s, len);
            used += len;
            out[used++] = '\n';
            if (!put && after && !strncmp(s, after, strlen(after))) {
                memcpy(out + used, add, strlen(add));
                used += strlen(add);
                put = 1;
            }
        }
        s += len + (eol ? 1 : 0);
    }
    if (!put) { memcpy(out + used, add, strlen(add)); used += strlen(add); }
    out[used] = 0;
    return out;
}

/* ---- takes: a sound and who it is ---- */
static void take_free(take_t *t) {
    free(t->state);
    free(t->base_state);
    free(t->base);
    memset(t, 0, sizeof *t);
}
static void take_now(ed_t *e, take_t *t, const char *what) {
    lib_t *L = LIB(e);
    memset(t, 0, sizeof *t);
    t->state = state_now(e);
    t->base_state = dup(L->base_state);
    t->base = malloc(sizeof e->pt.base);
    if (t->base) memcpy(t->base, e->pt.base, sizeof e->pt.base);
    t->bank = e->pt.bank;
    t->prog = e->pt.prog;
    t->forced = e->pt.forced_edit;
    snprintf(t->name, sizeof t->name, "%s", e->pt.name);
    t->meta = L->cur;
    snprintf(t->what, sizeof t->what, "%s", what ? what : "");
}
static void take_apply(ed_t *e, const take_t *t) {
    lib_t *L = LIB(e);
    if (!t->state) return;
    L->applying++;
    pt_apply_state(e, t->state);
    e->pt.bank = t->bank;
    e->pt.prog = t->prog;
    e->pt.forced_edit = t->forced;
    snprintf(e->pt.name, sizeof e->pt.name, "%s", t->name);
    if (t->base) memcpy(e->pt.base, t->base, sizeof e->pt.base);
    free(L->base_state);
    L->base_state = dup(t->base_state);
    L->cur = t->meta;
    e->pt.edited = patches_edited_now(e);
    e->sq.dirty = 1;   /* the step editor reads the sequence again */
    L->applying--;
    if (e->host) e->host(e->fx, audioMasterUpdateDisplay, 0, 0, NULL, 0);
    redraw(e);
}
static void push(take_t *a, int *n, take_t *t) {   /* t moved onto the stack (the oldest goes when full) */
    if (*n == UNDO_MAX) {
        take_free(&a[0]);
        memmove(a, a + 1, sizeof *a * (UNDO_MAX - 1));
        (*n)--;
    }
    a[(*n)++] = *t;
    memset(t, 0, sizeof *t);
}
static void clear_stack(take_t *a, int *n) {
    for (int k = 0; k < *n; k++) take_free(&a[k]);
    *n = 0;
}

/* ---- life ---- */
lib_t *lib_new(ed_t *e) {
    lib_t *L = calloc(1, sizeof *L);
    if (!L) return NULL;
    L->sel_kind = L->sel_idx = L->snap_sel = L->cur_slot = L->hold_slot = L->arr_sel = -1;
    L->ui_thread = GetCurrentThreadId();
    e->lib = L;
    return L;
}
void lib_free(ed_t *e) {
    lib_t *L = LIB(e);
    if (!L) return;
    clear_stack(L->undo, &L->nundo);
    clear_stack(L->redo, &L->nredo);
    take_free(&L->before);
    take_free(&L->stash);
    free(L->base_state);
    free(L->last_snap);
    free(L->lm);
    free(L->fview);
    free(L->uview);
    free(L->snaps);
    free(L->sets);
    free(L);
    e->lib = NULL;
}

/* ---- snapshots ---- */
static int snap_dir(char *out, int n, int create) {
    char d[MAX_PATH];
    if (!patch_dir(d, sizeof d, create)) return 0;
    if (snprintf(out, n, "%s\\Snapshots", d) >= n) return 0;
    if (create) CreateDirectoryA(out, NULL);
    return 1;
}
static int by_file_desc(const void *a, const void *b) { return -strcmp(((const snap_t *)a)->file, ((const snap_t *)b)->file); }
static void snap_read_head(const char *t, snap_t *s) {
    char v[NOTES_MAX * 2 + 8], of[64];
    line_value(t, "time", s->when, sizeof s->when);
    line_value(t, "auto", v, sizeof v);
    s->autosave = atoi(v) != 0;
    s->of_bank = 'N';
    s->of_prog = 0;
    s->of_name[0] = 0;
    if (line_value(t, "of", of, sizeof of) && of[0] && of[1] == ',') {
        s->of_bank = of[0];
        s->of_prog = atoi(of + 2);
        const char *c = strchr(of + 2, ',');
        snprintf(s->of_name, sizeof s->of_name, "%s", c ? c + 1 : "");
    }
    line_value(t, "notes", v, sizeof v);
    char full[NOTES_MAX + 1];
    unesc(v, full, sizeof full);
    for (char *c = full; *c; c++)
        if (*c == '\n') *c = ' ';
    snprintf(s->note, sizeof s->note, "%s", full);
}
static void snaps_rescan(ed_t *e) {
    lib_t *L = LIB(e);
    char dir[MAX_PATH], pat[MAX_PATH + 16], path[2 * MAX_PATH];
    L->nsnaps = 0;
    if (!snap_dir(dir, sizeof dir, 0)) return;
    snprintf(pat, sizeof pat, "%s\\*" SNAP_EXT, dir);
    WIN32_FIND_DATAA f;
    HANDLE h = FindFirstFileA(pat, &f);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            size_t l = strlen(f.cFileName), x = strlen(SNAP_EXT);
            if ((f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || l <= x || l >= sizeof L->snaps[0].file ||
                _stricmp(f.cFileName + l - x, SNAP_EXT))
                continue;
            if (L->nsnaps == L->capsnaps) {
                int cap = L->capsnaps ? 2 * L->capsnaps : 64;
                void *q = realloc(L->snaps, cap * sizeof *L->snaps);
                if (!q) break;
                L->snaps = q;
                L->capsnaps = cap;
            }
            snap_t *s = &L->snaps[L->nsnaps];
            memset(s, 0, sizeof *s);
            snprintf(s->file, sizeof s->file, "%s", f.cFileName);
            snprintf(path, sizeof path, "%s\\%s", dir, f.cFileName);
            char *t = pt_read(path);
            if (!t) continue;
            snap_read_head(t, s);
            free(t);
            L->nsnaps++;
        } while (FindNextFileA(h, &f));
        FindClose(h);
    }
    qsort(L->snaps, L->nsnaps, sizeof *L->snaps, by_file_desc);   /* newest first (the names are timestamps) */
    if (L->snap_sel >= L->nsnaps) L->snap_sel = L->nsnaps - 1;
}
static int snap_path(ed_t *e, int i, char *out, int n) {
    lib_t *L = LIB(e);
    char dir[MAX_PATH];
    return i >= 0 && i < L->nsnaps && snap_dir(dir, sizeof dir, 0) && snprintf(out, n, "%s\\%s", dir, L->snaps[i].file) < n;
}
/* the newest autosaves kept, older ones deleted (the snapshots you took are never deleted) */
static void prune_autosaves(ed_t *e) {
    lib_t *L = LIB(e);
    char path[2 * MAX_PATH];
    snaps_rescan(e);
    int autos = 0;
    for (int i = 0; i < L->nsnaps; i++) {
        if (!L->snaps[i].autosave) continue;
        if (++autos > AUTOSAVE_KEEP && snap_path(e, i, path, sizeof path)) DeleteFileA(path);
    }
    if (autos > AUTOSAVE_KEEP) snaps_rescan(e);
}
/* a snapshot of take t with a note; its file name, or 0 */
static int snap_write(ed_t *e, const take_t *t, int autosave, const char *note) {
    lib_t *L = LIB(e);
    char dir[MAX_PATH], file[64], path[2 * MAX_PATH], stamp[40], when[40], en[NOTES_MAX * 2 + 8];
    if (!t->state) return 0;
    if (!snap_dir(dir, sizeof dir, 1)) { say(e, "Could not make the snapshot folder: %s.", pt_why(GetLastError())); return 0; }
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(stamp, sizeof stamp, "%04u%02u%02u-%02u%02u%02u-%03u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
             st.wSecond, st.wMilliseconds);
    snprintf(when, sizeof when, "%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
             st.wSecond);
    for (int k = 1; k < 1000; k++) {
        if (k == 1) snprintf(file, sizeof file, "%s%s" SNAP_EXT, stamp, autosave ? "-auto" : "");
        else snprintf(file, sizeof file, "%s-%d%s" SNAP_EXT, stamp, k, autosave ? "-auto" : "");
        snprintf(path, sizeof path, "%s\\%s", dir, file);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) break;
    }
    esc(note ? note : "", en, sizeof en);
    size_t cap = strlen(t->state) + strlen(en) + 512;
    char *text = malloc(cap);
    if (!text) return 0;
    int used = snprintf(text, cap, "%s 1\ntime=%s\nof=%c,%d,%s\nauto=%d\nnotes=%s\n", SNAP_HEADER, when,
                        t->bank == BANK_FACTORY ? 'F' : t->bank == BANK_USER ? 'U' : 'N', t->prog, t->name, autosave, en);
    for (const char *s = t->state; *s;) {
        const char *end = strchr(s, ';');
        int len = end ? (int)(end - s) : (int)strlen(s);
        if (len && strncmp(s, "v=", 2) && strncmp(s, "ui_patch=", 9))
            used += snprintf(text + used, cap - used, "%.*s\n", len, s);
        s += len + (end ? 1 : 0);
    }
    int ok = pt_write(path, text);
    free(text);
    if (!ok) { say(e, "Could not write the snapshot: %s.", pt_why(GetLastError())); return 0; }
    free(L->last_snap);
    L->last_snap = dup(t->state);
    if (autosave) prune_autosaves(e);
    else snaps_rescan(e);
    return 1;
}
/* an autosave of the edit in t (not when it is exactly the newest snapshot already) */
static void autosave(ed_t *e, const take_t *t, const char *why) {
    lib_t *L = LIB(e);
    if (!t->state || (L->last_snap && !strcmp(L->last_snap, t->state))) return;
    snap_write(e, t, 1, why);
}

/* ---- undo ---- */
void lib_touch(ed_t *e, const char *label) {
    lib_t *L = LIB(e);
    if (!L || L->applying) return;
    fresh(e);
    L->touched_at = GetTickCount();
    if (L->comparing) {   /* editing while comparing: the saved version is edited, the hidden edit is kept */
        L->comparing = 0;
        autosave(e, &L->stash, "The edit COMPARE was hiding (the saved version was edited instead)");
        take_free(&L->stash);
        say(e, "COMPARE off: you are editing the saved version. The edit it was hiding is kept as a snapshot.");
    }
    if (L->open) return;
    take_now(e, &L->before, "");
    if (!L->before.state) return;
    L->open = 1;
    snprintf(L->label, sizeof L->label, "%s", label ? label : "");
    for (int i = 0; i < NPARAMS; i++) L->before_vals[i] = e->fx->getParameter(e->fx, i);
}
void lib_commit(ed_t *e) {
    lib_t *L = LIB(e);
    if (!L || !L->open || L->hold || L->in_load) return;
    L->open = 0;
    char *now = state_now(e);
    if (!now || !strcmp(now, L->before.state)) { free(now); take_free(&L->before); return; }
    free(now);
    if (L->label[0]) {
        snprintf(L->before.what, sizeof L->before.what, "%s", L->label);
    } else {   /* named after the first control that moved (Synth 2's "S2 Cutoff" ...), else a step edit */
        snprintf(L->before.what, sizeof L->before.what, "a step edit");
        for (int i = 0; i < NPARAMS; i++)
            if (e->fx->getParameter(e->fx, i) != L->before_vals[i]) {
                snprintf(L->before.what, sizeof L->before.what, "%s", PARAMS[i].name);
                break;
            }
    }
    push(L->undo, &L->nundo, &L->before);
    clear_stack(L->redo, &L->nredo);
}
void lib_hold(ed_t *e, int d) {
    lib_t *L = LIB(e);
    if (!L) return;
    if (d > 0) { L->hold++; return; }
    if (L->hold > 0 && --L->hold == 0) lib_commit(e);
}
static void compare_back(ed_t *e) {   /* COMPARE off: the edit again */
    lib_t *L = LIB(e);
    L->comparing = 0;
    take_apply(e, &L->stash);
    take_free(&L->stash);
}
void lib_end_compare(ed_t *e) {
    if (LIB(e) && LIB(e)->comparing) compare_back(e);
}
static int undo_redo(ed_t *e, int redo) {
    lib_t *L = LIB(e);
    if (!L || L->busy || L->hold) return 0;
    lib_commit(e);
    if (L->comparing) compare_back(e);
    take_t *from = redo ? L->redo : L->undo, *to = redo ? L->undo : L->redo;
    int *nfrom = redo ? &L->nredo : &L->nundo, *nto = redo ? &L->nundo : &L->nredo;
    if (!*nfrom) { say(e, redo ? "Nothing to redo." : "Nothing to undo."); return 0; }
    take_t t = from[--*nfrom], cur;
    take_now(e, &cur, t.what);
    if (*nto == UNDO_MAX) {   /* (the other stack is full: its oldest goes) */
        take_free(&to[0]);
        memmove(to, to + 1, sizeof *to * (UNDO_MAX - 1));
        (*nto)--;
    }
    to[(*nto)++] = cur;
    take_apply(e, &t);
    say(e, "%s %s.  (%d to undo, %d to redo; Ctrl+Z / Ctrl+Y)", redo ? "Redid" : "Undid", t.what, L->nundo, L->nredo);
    take_free(&t);
    return 1;
}
int lib_undo(ed_t *e) { return undo_redo(e, 0); }
int lib_redo(ed_t *e) { return undo_redo(e, 1); }

/* another sound is about to load: the unsaved edit (or the one COMPARE hides) is autosaved, and the load is a step */
void lib_before_load(ed_t *e, const char *what) {
    lib_t *L = LIB(e);
    if (!L) return;
    if (L->in_load || L->applying) { L->in_load++; return; }
    fresh(e);
    lib_commit(e);   /* (an edit still open is its own step) */
    L->in_load = 1;
    L->open = 0;
    take_free(&L->before);
    char why[96];
    snprintf(why, sizeof why, "Autosave before loading %s", what);
    if (L->comparing) {
        L->comparing = 0;
        autosave(e, &L->stash, why);
        L->before = L->stash;   /* undo brings the edit back, not the saved version */
        memset(&L->stash, 0, sizeof L->stash);
    } else {
        take_now(e, &L->before, "");
        if (patches_edited_now(e)) autosave(e, &L->before, why);
    }
    if (!L->before.state) return;
    L->open = 1;
    snprintf(L->label, sizeof L->label, "loading %s", what);
}
void lib_after_load(ed_t *e) {
    lib_t *L = LIB(e);
    if (!L || !L->in_load) return;
    if (--L->in_load || L->applying) return;
    lib_commit(e);
}

/* ---- COMPARE ---- */
static void lib_compare(ed_t *e) {
    lib_t *L = LIB(e);
    fresh(e);
    lib_commit(e);
    if (L->comparing) {
        compare_back(e);
        say(e, "COMPARE off: your edit again.");
        return;
    }
    if (!L->base_state) {
        say(e, "Nothing saved to compare with: this sound has no saved version (SAVE AS keeps one).");
        return;
    }
    char *now = state_now(e);
    if (!now) return;
    int same = !strcmp(now, L->base_state);
    free(now);
    if (same) { say(e, "No changes to compare: the sound is as it was saved."); return; }
    take_now(e, &L->stash, "");
    L->applying++;
    pt_apply_state(e, L->base_state);
    L->applying--;
    for (int i = 0; i < NPARAMS; i++) e->pt.base[i] = e->fx->getParameter(e->fx, i);   /* (what plays is the saved one) */
    e->pt.forced_edit = 0;
    e->pt.edited = patches_edited_now(e);
    e->sq.dirty = 1;
    L->comparing = 1;
    say(e, "COMPARE: the saved version is playing. COMPARE again: back to your edit.");
}
int lib_comparing(ed_t *e) { return LIB(e) && LIB(e)->comparing; }

/* A host may pick a program or restore a project from another thread (even the audio one): there nothing here
 * allocates or reads a file; the saved version and the tags are fetched on the window's thread later (fresh()). */
static int on_ui(lib_t *L) { return GetCurrentThreadId() == L->ui_thread; }
void lib_set_base(ed_t *e) {
    lib_t *L = LIB(e);
    if (!L) return;
    if (!on_ui(L)) { L->stale |= 1; return; }
    free(L->base_state);
    L->base_state = state_now(e);
}
void lib_drop_base(ed_t *e) {
    lib_t *L = LIB(e);
    if (!L || !on_ui(L)) return;
    free(L->base_state);
    L->base_state = NULL;
}
void lib_saved_base(ed_t *e) {
    lib_t *L = LIB(e);
    if (!L) return;
    if (!on_ui(L)) { L->stale |= 1; return; }
    free(L->base_state);
    L->base_state = pt_saved_state(e);
}
static void fresh(ed_t *e) {   /* the window's thread: what a host's call on another thread left to do */
    lib_t *L = LIB(e);
    if (!L || !L->stale || !on_ui(L)) return;
    int s = L->stale;
    L->stale = 0;
    if (s & 1) lib_saved_base(e);
    if (s & 2) lib_identity(e);
}
void lib_host_restored(ed_t *e) {
    lib_t *L = LIB(e);
    if (!L || !on_ui(L)) return;
    if (L->comparing) { L->comparing = 0; take_free(&L->stash); }
    if (L->open && !L->in_load) { L->open = 0; take_free(&L->before); }
}

/* ---- tags and notes ---- */
void lib_meta_read(const char *text, meta_t *m) {
    char v[NOTES_MAX * 2 + 8];
    memset(m, 0, sizeof *m);
    for (int k = 0; k < TAG_KINDS; k++) {
        line_value(text, TAG_KEY[k], v, sizeof v);
        snprintf(m->tag[k], sizeof m->tag[k], "%s", v);
    }
    line_value(text, "notes", v, sizeof v);
    unesc(v, m->notes, sizeof m->notes);
}
void lib_meta_lines(const meta_t *m, char *out, int n) {
    int used = 0;
    out[0] = 0;
    for (int k = 0; k < TAG_KINDS; k++)
        if (m->tag[k][0]) used += snprintf(out + used, used < n ? n - used : 0, "%s=%s\n", TAG_KEY[k], m->tag[k]);
    if (m->notes[0] && used < n) {
        char en[NOTES_MAX * 2 + 8];
        esc(m->notes, en, sizeof en);
        snprintf(out + used, n - used, "notes=%s\n", en);
    }
}
/* the factory presets' tags (written for ONE-ish's own presets) */
static const struct { const char *name, *type, *cat, *mood; } FACTORY_TAGS[] = {
    {"Fat Bass", "Single", "Bass", "Dark"}, {"Pluck Bass", "Single", "Bass", "Clean"},
    {"Acid Line", "Single", "Bass", "Aggressive"}, {"Sync Lead", "Single", "Lead", "Aggressive"},
    {"Brass Stack", "Single", "Brass", "Bright"}, {"Warm Pad", "Single", "Pad", "Warm"},
    {"Glass Keys", "Single", "Keys", "Bright"}, {"Hollow Strings", "Single", "Strings", "Soft"},
    {"Wobble Bass", "Single", "Bass", "Aggressive"}, {"Self-Osc Whistle", "Single", "FX", "Clean"},
    {"Noise Hat", "Single", "Drum", "Bright"}, {"Notch Sweep", "Single", "FX", "Evolving"},
    {"FM Bell", "Single", "Bells", "Bright"}, {"Ring Clang", "Single", "Bells", "Dirty"},
    {"PWM Strings", "Single", "Strings", "Warm"}, {"Angle Lead", "Single", "Lead", "Bright"},
    {"Unison Lead", "Single", "Lead", "Aggressive"}, {"Vowel Pair", "Single", "Pad", "Evolving"},
    {"FM Growl", "Single", "Bass", "Dirty"}, {"Wind Spread", "Single", "FX", "Soft"},
    {"Pulse Pad", "Single", "Pad", "Warm"}, {"Morph Motion", "Single", "Pad", "Evolving"},
    {"Ensemble Strings", "Single", "Strings", "Warm"}, {"Tape Echo Lead", "Single", "Lead", "Warm"},
    {"Crushed Keys", "Single", "Keys", "Dirty"}, {"Vintage Poly Pad", "Single", "Pad", "Warm"},
    {"Arp Cascade", "Single", "Arp", "Bright"}, {"Euclid Turing Bells", "Single", "Sequence", "Evolving"},
    {"Macro String", "Single", "Strings", "Clean"}, {"Macro Wave Pad", "Single", "Pad", "Evolving"},
    {"Macro FM Keys", "Single", "Keys", "Clean"}, {"Modal Bells", "Single", "Bells", "Soft"},
    {"Grain Cloud", "Single", "FX", "Evolving"}, {"Tape Bass", "Single", "Bass", "Warm"},
    {"Seq Bass Line", "Single", "Sequence", "Dark"}, {"Seq Chord Stabs", "Single", "Sequence", "Bright"},
    {"Split Bass + Lead", "Split", "Lead", "Bright"}, {"Layer Pad + Pluck", "Layer", "Pad", "Warm"},
    {"Split Bass, Pad + Bells", "Split", "Pad", "Soft"}};
static void factory_meta(ed_t *e, int p, meta_t *m) {
    char nm[PATCH_NAME_MAX + 1];
    memset(m, 0, sizeof *m);
    prog_name(e, p, nm);
    snprintf(m->tag[TAG_TYPE], sizeof m->tag[0], "Single");
    for (int k = 0; k < N_OF(FACTORY_TAGS); k++)
        if (!strcmp(nm, FACTORY_TAGS[k].name)) {
            snprintf(m->tag[TAG_TYPE], sizeof m->tag[0], "%s", FACTORY_TAGS[k].type);
            snprintf(m->tag[TAG_CAT], sizeof m->tag[0], "%s", FACTORY_TAGS[k].cat);
            snprintf(m->tag[TAG_MOOD], sizeof m->tag[0], "%s", FACTORY_TAGS[k].mood);
        }
}
/* TYPE from the three Synths: one on Single; their key ranges apart Split, all overlapping Layer, else Multi */
static const char *auto_type(ed_t *e) {
    int kon = param_key("syn_on"), klo = param_key("key_lo"), khi = param_key("key_hi");
    if (kon < 0 || klo < 0 || khi < 0) return "Single";
    int lo[NSYNTHS], hi[NSYNTHS], n = 0;
    for (int c = 0; c < NSYNTHS; c++) {
        if (e->fx->getParameter(e->fx, synth_param(kon, c)) < 0.5f) continue;
        int a = synth_param(klo, c), b = synth_param(khi, c);
        lo[n] = (int)lroundf(PARAMS[a].min + e->fx->getParameter(e->fx, a) * (PARAMS[a].max - PARAMS[a].min));
        hi[n] = (int)lroundf(PARAMS[b].min + e->fx->getParameter(e->fx, b) * (PARAMS[b].max - PARAMS[b].min));
        n++;
    }
    if (n <= 1) return "Single";
    int pairs = 0, over = 0;
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++) {
            pairs++;
            over += lo[a] <= hi[b] && lo[b] <= hi[a];
        }
    return over == 0 ? "Split" : over == pairs ? "Layer" : "Multi";
}
static int has_meta_line(const char *t) {
    char v[8];
    for (int k = 0; k < TAG_KINDS; k++)
        if (line_value(t, TAG_KEY[k], v, sizeof v)) return 1;
    return line_value(t, "notes", v, sizeof v);
}
/* the meta lines a save writes: the save box's choice; else what the file had; else the sound's own tags */
void lib_meta_for_save(ed_t *e, const char *kept, char *out, int n) {
    lib_t *L = LIB(e);
    char author[200] = "", lines[NOTES_MAX * 2 + 256];
    if (line_value(kept, "author", author, sizeof author)) {
        char a[220];
        snprintf(a, sizeof a, "author=%s\n", author);
        snprintf(author, sizeof author, "%s", a);
    }
    if (L && L->save_meta_set) {
        L->save_meta_set = 0;
        lib_meta_lines(&L->save_meta, lines, sizeof lines);
        snprintf(out, n, "%s%s", author, lines);
    } else if (has_meta_line(kept)) {
        snprintf(out, n, "%s", kept);
    } else {
        meta_t m;
        if (L) m = L->cur;
        else memset(&m, 0, sizeof m);
        if (!m.tag[TAG_TYPE][0]) snprintf(m.tag[TAG_TYPE], sizeof m.tag[0], "%s", auto_type(e));
        lib_meta_lines(&m, lines, sizeof lines);
        snprintf(out, n, "%s%s", author, lines);
    }
}
void lib_after_save(ed_t *e, const char *meta_text) {
    if (LIB(e)) lib_meta_read(meta_text, &LIB(e)->cur);
}
/* the current patch's tags and notes, from its file or the factory table */
void lib_identity(ed_t *e) {
    lib_t *L = LIB(e);
    if (!L) return;
    if (!on_ui(L)) { L->stale |= 2; return; }
    if (e->pt.bank == BANK_FACTORY) {
        factory_meta(e, e->pt.prog, &L->cur);
    } else if (e->pt.bank == BANK_USER) {
        char path[MAX_PATH];
        char *t = pt_patch_path(e->pt.name, path, sizeof path, 0) ? pt_read(path) : NULL;
        if (t) lib_meta_read(t, &L->cur);
        else memset(&L->cur, 0, sizeof L->cur);
        free(t);
    }
}
static const char *const META_KEYS[] = {"type", "category", "mood", "group", "notes"};
/* a patch file's tags and notes set to m (the rest of the file as it was) */
static int set_file_meta(ed_t *e, const char *name, const meta_t *m) {
    char path[MAX_PATH], lines[NOTES_MAX * 2 + 256];
    if (!pt_patch_path(name, path, sizeof path, 0)) return 0;
    char *t = pt_read(path);
    if (!t) { say(e, "Could not open '%s': %s.", name, pt_why(GetLastError())); return 0; }
    lib_meta_lines(m, lines, sizeof lines);
    char *nt = replace_lines(t, META_KEYS, N_OF(META_KEYS), "name=", lines);
    free(t);
    int ok = nt && pt_write(path, nt);
    if (!ok) say(e, "Could not change '%s': %s.", name, pt_why(GetLastError()));
    free(nt);
    return ok;
}

/* the tag lists (TYPE is fixed) */
static void lists_defaults(lib_t *L) {
    static const char *const *const D[TAG_KINDS] = {TYPES, DEF_CAT, DEF_MOOD, DEF_GROUP};
    static const int DN[TAG_KINDS] = {N_OF(TYPES), N_OF(DEF_CAT), N_OF(DEF_MOOD), N_OF(DEF_GROUP)};
    for (int k = 0; k < TAG_KINDS; k++) {
        L->nlist[k] = DN[k];
        for (int j = 0; j < DN[k]; j++) snprintf(L->list[k][j], sizeof L->list[k][j], "%s", D[k][j]);
    }
}
static int tags_path(char *out, int n, int create) {
    char d[MAX_PATH];
    return patch_dir(d, sizeof d, create) && snprintf(out, n, "%s\\tags.oneishtags", d) < n;
}
static void lists_read(ed_t *e) {
    lib_t *L = LIB(e);
    char path[MAX_PATH];
    lists_defaults(L);
    L->lists_read = 1;
    char *t = tags_path(path, sizeof path, 0) ? pt_read(path) : NULL;
    if (!t) return;
    for (int k = 1; k < TAG_KINDS; k++) L->nlist[k] = 0;
    for (char *s = t; *s;) {
        char *eol = strchr(s, '\n');
        if (eol) *eol = 0;
        char line[128];
        snprintf(line, sizeof line, "%s", s);
        trim(line);
        for (int k = 1; k < TAG_KINDS; k++) {
            size_t kl = strlen(TAG_KEY[k]);
            if (!strncmp(line, TAG_KEY[k], kl) && line[kl] == '=' && line[kl + 1] && L->nlist[k] < TAG_LIST_MAX)
                snprintf(L->list[k][L->nlist[k]++], TAG_LEN + 1, "%s", line + kl + 1);
        }
        s = eol ? eol + 1 : s + strlen(s);
    }
    free(t);
}
static void lists_ensure(ed_t *e) { if (!LIB(e)->lists_read) lists_read(e); }
static int lists_write(ed_t *e) {
    lib_t *L = LIB(e);
    char path[MAX_PATH], *text = malloc(TAG_KINDS * TAG_LIST_MAX * (TAG_LEN + 16) + 64);
    if (!text) return 0;
    int used = sprintf(text, "%s 1\n", TAGS_HEADER);
    for (int k = 1; k < TAG_KINDS; k++)
        for (int j = 0; j < L->nlist[k]; j++) used += sprintf(text + used, "%s=%s\n", TAG_KEY[k], L->list[k][j]);
    int ok = tags_path(path, sizeof path, 1) && pt_write(path, text);
    if (!ok) say(e, "Could not save the tag lists: %s.", pt_why(GetLastError()));
    free(text);
    return ok;
}
/* a tag or a set name: 1..max plain characters, none of ; = | , and no space at the ends */
static int text_ok(const char *s, int max, char *why, int n) {
    size_t len = strlen(s);
    const char *bad = NULL;
    if (!len) bad = "it is empty";
    else if ((int)len > max) bad = max == TAG_LEN ? "a tag is at most 24 characters" : "it is longer than 32 characters";
    else if (s[0] == ' ' || s[len - 1] == ' ') bad = "it starts or ends with a space";
    for (size_t k = 0; !bad && k < len; k++)
        if ((unsigned char)s[k] < 32 || (unsigned char)s[k] > 126 || strchr(";=|,\\", s[k])) bad = "use plain letters and numbers (not ; = | , \\)";
    if (bad && why) snprintf(why, n, "%s", bad);
    return !bad;
}

/* the user patches' tags (for the filters), read again only when a file changed */
static int meta_match(lib_t *L, char tags[TAG_KINDS][TAG_LEN + 1]) {
    for (int k = 0; k < TAG_KINDS; k++)
        if (L->filter[k][0] && _stricmp(L->filter[k], tags[k])) return 0;
    return 1;
}
int lib_filtering(ed_t *e) {
    lib_t *L = LIB(e);
    for (int k = 0; L && k < TAG_KINDS; k++)
        if (L->filter[k][0]) return 1;
    return 0;
}
static void views(ed_t *e) {
    lib_t *L = LIB(e);
    int nf = nfactory(e), need = nf > e->pt.count ? nf : e->pt.count;
    if (need > L->capview) {
        int *a = realloc(L->fview, (need + 1) * sizeof *a), *b = a ? realloc(L->uview, (need + 1) * sizeof *b) : NULL;
        if (a) L->fview = a;
        if (!b) { L->nfview = L->nuview = 0; return; }
        L->uview = b;
        L->capview = need;
    }
    L->nfview = L->nuview = 0;
    for (int p = 0; p < nf; p++) {
        meta_t m;
        factory_meta(e, p, &m);
        if (meta_match(L, m.tag)) L->fview[L->nfview++] = p;
    }
    for (int u = 0; u < e->pt.count && u < L->nlm; u++)
        if (meta_match(L, L->lm[u].tag)) L->uview[L->nuview++] = u;
}
void lib_list_meta(ed_t *e) {
    lib_t *L = LIB(e);
    if (!L) return;
    int n = e->pt.count;
    lmeta_t *a = n ? calloc(n, sizeof *a) : NULL;
    if (n && !a) return;
    for (int u = 0; u < n; u++) {
        char path[MAX_PATH];
        WIN32_FILE_ATTRIBUTE_DATA fa;
        snprintf(a[u].name, sizeof a[u].name, "%s", e->pt.list[u]);
        if (!pt_patch_path(a[u].name, path, sizeof path, 0) || !GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) continue;
        a[u].t = fa.ftLastWriteTime;
        int found = 0;
        for (int k = 0; k < L->nlm && !found; k++)
            if (!_stricmp(L->lm[k].name, a[u].name) && !CompareFileTime(&L->lm[k].t, &a[u].t)) {
                memcpy(a[u].tag, L->lm[k].tag, sizeof a[u].tag);
                found = 1;
            }
        if (found) continue;
        char *t = pt_read(path);
        if (!t) continue;
        meta_t m;
        lib_meta_read(t, &m);
        free(t);
        memcpy(a[u].tag, m.tag, sizeof a[u].tag);
    }
    free(L->lm);
    L->lm = a;
    L->nlm = n;
    views(e);
}
int lib_view_count(ed_t *e, int col) {
    lib_t *L = LIB(e);
    if (!L || !lib_filtering(e)) return col ? e->pt.count : nfactory(e);
    return col ? L->nuview : L->nfview;
}
int lib_view_pos(ed_t *e, int col, int k) {
    lib_t *L = LIB(e);
    int nf = nfactory(e);
    if (k < 0) return -1;
    if (!L || !lib_filtering(e)) return col ? (k < e->pt.count ? nf + k : -1) : (k < nf ? k : -1);
    return col ? (k < L->nuview ? nf + L->uview[k] : -1) : (k < L->nfview ? L->fview[k] : -1);
}
int lib_view_index(ed_t *e, int pos) {   /* where list position pos is in its column (-1: filtered out) */
    int nf = nfactory(e), col = pos >= nf, n = lib_view_count(e, col);
    for (int k = 0; k < n; k++)
        if (lib_view_pos(e, col, k) == pos) return k;
    return -1;
}
int lib_step_filtered(ed_t *e, int d) {
    if (!lib_filtering(e)) return 0;
    /* (PRESET AUTOLOAD off: from where the knob points, and only pointing: settings.c) */
    int nf = lib_view_count(e, 0), n = nf + lib_view_count(e, 1), pos = settings_cue_from(e, patches_pos(e)), at = -1;
    if (!n) { say(e, "No patch has the tags the filters ask for (TAGS: CLEAR)."); return 1; }
    for (int k = 0; k < n && at < 0; k++)
        if ((k < nf ? lib_view_pos(e, 0, k) : lib_view_pos(e, 1, k - nf)) == pos) at = k;
    int to = at < 0 ? (d > 0 ? 0 : n - 1) : at + d;
    if (to < 0) to = 0;
    if (to > n - 1) to = n - 1;
    int p = to < nf ? lib_view_pos(e, 0, to) : lib_view_pos(e, 1, to - nf);
    if (to != at && !settings_cue_step(e, p)) patches_load_pos(e, p);
    return 1;
}
int lib_load_pos(ed_t *e, int pos) { return patches_load_pos(e, pos); }

/* ---- dialogs: a line, notes, or the save box (name, tags and notes) ---- */
enum { DK_TAG, DK_SETNAME, DK_NOTES, DK_SAVE };
enum { ID_PROMPT = 100, ID_EDIT, ID_WHY, ID_COMBO = 110, ID_NOTES = 114, ID_CLABEL = 120, ID_NLABEL = 124 };
typedef struct { ed_t *e; int kind; const char *title, *prompt; char text[NOTES_MAX + 1]; meta_t meta; } dlg_t;

static void to_crlf(const char *in, char *out, int n) {
    int k = 0;
    for (; *in && k < n - 2; in++) {
        if (*in == '\n') out[k++] = '\r';
        out[k++] = *in;
    }
    out[k] = 0;
}
static void from_edit(HWND d, int id, char *out, int n) {   /* plain text with \n lines */
    char t[NOTES_MAX * 2 + 8];
    GetDlgItemTextA(d, id, t, sizeof t);
    int k = 0;
    for (const char *s = t; *s && k < n - 1; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '\r') continue;
        out[k++] = c == '\n' ? '\n' : c == '\t' ? ' ' : (c < 32 || c > 126) ? '?' : (char)c;
    }
    out[k] = 0;
}
static INT_PTR CALLBACK dlg_proc(HWND d, UINT m, WPARAM wp, LPARAM lp) {
    dlg_t *a = (dlg_t *)GetWindowLongPtrA(d, DWLP_USER);
    if (m == WM_INITDIALOG) {
        a = (dlg_t *)lp;
        SetWindowLongPtrA(d, DWLP_USER, lp);
        SetWindowTextA(d, a->title);
        SetDlgItemTextA(d, ID_PROMPT, a->prompt);
        char t[NOTES_MAX * 2 + 8];
        to_crlf(a->text, t, sizeof t);
        SetDlgItemTextA(d, ID_EDIT, t);
        int lim = a->kind == DK_TAG ? TAG_LEN : a->kind == DK_NOTES ? NOTES_MAX : PATCH_NAME_MAX;
        SendDlgItemMessageA(d, ID_EDIT, EM_LIMITTEXT, lim, 0);
        if (a->kind == DK_SAVE) {
            lib_t *L = LIB(a->e);
            for (int k = 0; k < TAG_KINDS; k++) {
                HWND c = GetDlgItem(d, ID_COMBO + k);
                SendMessageA(c, CB_ADDSTRING, 0, (LPARAM) "(none)");
                for (int j = 0; j < L->nlist[k]; j++) SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)L->list[k][j]);
                LRESULT at = a->meta.tag[k][0] ? SendMessageA(c, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)a->meta.tag[k]) : 0;
                if (at == CB_ERR) at = SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)a->meta.tag[k]);   /* (a tag no list has) */
                SendMessageA(c, CB_SETCURSEL, at, 0);
            }
            to_crlf(a->meta.notes, t, sizeof t);
            SetDlgItemTextA(d, ID_NOTES, t);
            SendDlgItemMessageA(d, ID_NOTES, EM_LIMITTEXT, NOTES_MAX, 0);
        }
        SendDlgItemMessageA(d, ID_EDIT, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(d, ID_EDIT));
        return FALSE;
    }
    if (m == WM_COMMAND && LOWORD(wp) == IDOK && a) {
        char t[NOTES_MAX + 1], why[96] = "";
        from_edit(d, ID_EDIT, t, sizeof t);
        if (a->kind != DK_NOTES) trim(t);
        int ok = a->kind == DK_NOTES ? 1 : a->kind == DK_TAG ? text_ok(t, TAG_LEN, why, sizeof why) :
                 a->kind == DK_SETNAME ? text_ok(t, PATCH_NAME_MAX, why, sizeof why) : patch_name_ok(t, why, sizeof why);
        if (!ok) { SetDlgItemTextA(d, ID_WHY, why); return TRUE; }
        snprintf(a->text, sizeof a->text, "%s", t);
        if (a->kind == DK_SAVE) {
            for (int k = 0; k < TAG_KINDS; k++) {
                char v[64] = "";
                HWND c = GetDlgItem(d, ID_COMBO + k);
                LRESULT at = SendMessageA(c, CB_GETCURSEL, 0, 0);
                if (at > 0 && SendMessageA(c, CB_GETLBTEXTLEN, at, 0) < (LRESULT)sizeof v) SendMessageA(c, CB_GETLBTEXT, at, (LPARAM)v);
                snprintf(a->meta.tag[k], sizeof a->meta.tag[k], "%s", v);
            }
            from_edit(d, ID_NOTES, a->meta.notes, sizeof a->meta.notes);
        }
        EndDialog(d, 1);
        return TRUE;
    }
    if (m == WM_COMMAND && LOWORD(wp) == IDCANCEL) { EndDialog(d, 0); return TRUE; }
    return FALSE;
}
static int run_dialog(ed_t *e, dlg_t *a) {
    static DWORD buf[3072];
    memset(buf, 0, sizeof buf);
    DLGTEMPLATE *dt = (DLGTEMPLATE *)buf;
    int save = a->kind == DK_SAVE, notes = a->kind == DK_NOTES;
    short cy = save ? 184 : notes ? 132 : 76, by = cy - 21;
    dt->style = DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dt->cdit = save ? 15 : 5;
    dt->cx = 250;
    dt->cy = cy;
    WORD *w = (WORD *)(dt + 1);
    *w++ = 0;
    *w++ = 0;
    w = dlg_wstr(w, a->title);
    *w++ = 9;
    w = dlg_wstr(w, "Segoe UI");
    w = dlg_item(w, SS_LEFT, 8, 8, 234, 10, ID_PROMPT, 0x82, "");
    if (notes)
        w = dlg_item(w, WS_BORDER | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 8, 21, 234, 78,
                     ID_EDIT, 0x81, "");
    else
        w = dlg_item(w, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 8, 21, 234, 13, ID_EDIT, 0x81, "");
    if (save) {
        for (int k = 0; k < TAG_KINDS; k++) {
            short x = k % 2 ? 128 : 8, y = k < 2 ? 42 : 60;
            w = dlg_item(w, SS_LEFT, x, (short)(y + 2), 40, 10, (WORD)(ID_CLABEL + k), 0x82, k == 0 ? "Type" : k == 1 ? "Category" : k == 2 ? "Mood" : "Group");
            w = dlg_item(w, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, (short)(x + 42), y, 72, 120, (WORD)(ID_COMBO + k), 0x85, "");
        }
        w = dlg_item(w, SS_LEFT, 8, 80, 200, 10, ID_NLABEL, 0x82, "Notes (optional)");
        w = dlg_item(w, WS_BORDER | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 8, 92, 234, 54,
                     ID_NOTES, 0x81, "");
    }
    w = dlg_item(w, SS_LEFT, 8, (short)(by - 13), 234, 10, ID_WHY, 0x82, "");
    w = dlg_item(w, WS_TABSTOP | BS_DEFPUSHBUTTON, 138, by, 50, 14, IDOK, 0x80, "OK");
    dlg_item(w, WS_TABSTOP | BS_PUSHBUTTON, 192, by, 50, 14, IDCANCEL, 0x80, "Cancel");
    a->e = e;
    HINSTANCE inst = e->wnd ? (HINSTANCE)GetWindowLongPtrA(e->wnd, GWLP_HINSTANCE) : GetModuleHandleA(NULL);
    LIB(e)->busy++;
    INT_PTR r = DialogBoxIndirectParamA(inst, dt, pt_owner(e), dlg_proc, (LPARAM)a);
    LIB(e)->busy--;
    return r == 1;
}
static int ask(ed_t *e, int kind, const char *title, const char *prompt, char *text, int n) {
    static dlg_t a;
    memset(&a, 0, sizeof a);
    a.kind = kind;
    a.title = title;
    a.prompt = prompt;
    snprintf(a.text, sizeof a.text, "%s", text);
    if (!run_dialog(e, &a)) return 0;
    snprintf(text, n, "%s", a.text);
    return 1;
}
/* the save box: a name, the tags and notes; name and *m in and out */
int lib_save_dialog(ed_t *e, char *name, meta_t *m) {
    static dlg_t a;
    lists_ensure(e);
    memset(&a, 0, sizeof a);
    a.kind = DK_SAVE;
    a.title = "Save as";
    a.prompt = "A name for this sound (up to 32 characters), its tags and notes:";
    snprintf(a.text, sizeof a.text, "%s", name);
    a.meta = *m;
    if (!run_dialog(e, &a)) return 0;
    snprintf(name, PATCH_NAME_MAX + 1, "%s", a.text);
    *m = a.meta;
    return 1;
}
void lib_save_defaults(ed_t *e, meta_t *m) {   /* the save box starts from the sound's tags (TYPE as it plays) */
    lib_t *L = LIB(e);
    if (L) *m = L->cur;
    else memset(m, 0, sizeof *m);
    snprintf(m->tag[TAG_TYPE], sizeof m->tag[0], "%s", auto_type(e));
}

/* ---- performance sets ---- */
static int sets_path(char *out, int n, int create) {
    char d[MAX_PATH];
    return patch_dir(d, sizeof d, create) && snprintf(out, n, "%s\\sets.oneishsets", d) < n;
}
static void sets_read(ed_t *e) {
    lib_t *L = LIB(e);
    char path[MAX_PATH];
    L->sets_read = 1;
    L->nsets = 0;
    if (!L->sets) L->sets = calloc(SET_COUNT, sizeof *L->sets);
    if (!L->sets) return;
    char *t = sets_path(path, sizeof path, 0) ? pt_read(path) : NULL;
    if (!t) return;
    int cur = 0;
    set_t *s = NULL;
    for (char *p = t; *p;) {
        char *eol = strchr(p, '\n');
        if (eol) *eol = 0;
        char line[NOTES_MAX * 2 + 64];
        snprintf(line, sizeof line, "%s", p);
        trim(line);
        p = eol ? eol + 1 : p + strlen(p);
        if (!strcmp(line, "[set]")) {
            s = L->nsets < SET_COUNT ? &L->sets[L->nsets++] : NULL;
            if (s) memset(s, 0, sizeof *s);
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if (!s) { if (!strcmp(k, "current")) cur = atoi(v) - 1; continue; }
        if (!strcmp(k, "name")) snprintf(s->name, sizeof s->name, "%s", v);
        else if (!strcmp(k, "notes")) unesc(v, s->notes, sizeof s->notes);
        else if (k[0] >= 'A' && k[0] <= 'H' && k[1] >= '1' && k[1] <= '8' && !k[2] && (v[0] == 'F' || v[0] == 'U') &&
                 v[1] == ',') {
            slot_t *sl = &s->slot[(k[0] - 'A') * 8 + (k[1] - '1')];
            const char *c = strchr(v + 2, ',');
            sl->kind = v[0];
            sl->prog = atoi(v + 2);
            snprintf(sl->name, sizeof sl->name, "%s", c ? c + 1 : "");
        }
    }
    free(t);
    L->cur_set = cur >= 0 && cur < L->nsets ? cur : 0;
}
static int sets_write(ed_t *e) {
    lib_t *L = LIB(e);
    char path[MAX_PATH];
    size_t cap = 64 + (size_t)L->nsets * (SET_SLOTS * (PATCH_NAME_MAX + 16) + 2 * NOTES_MAX + 96);
    char *text = malloc(cap);
    if (!text) return 0;
    size_t used = snprintf(text, cap, "%s 1\ncurrent=%d\n", SETS_HEADER, L->cur_set + 1);
    for (int i = 0; i < L->nsets; i++) {
        set_t *s = &L->sets[i];
        char en[NOTES_MAX * 2 + 8];
        esc(s->notes, en, sizeof en);
        used += snprintf(text + used, cap - used, "[set]\nname=%s\nnotes=%s\n", s->name, en);
        for (int k = 0; k < SET_SLOTS; k++)
            if (s->slot[k].kind)
                used += snprintf(text + used, cap - used, "%c%d=%c,%d,%s\n", 'A' + k / 8, k % 8 + 1, s->slot[k].kind,
                                 s->slot[k].prog, s->slot[k].name);
    }
    int ok = sets_path(path, sizeof path, 1) && pt_write(path, text);
    if (!ok) say(e, "Could not save the performance sets: %s.", pt_why(GetLastError()));
    free(text);
    return ok;
}
static void sets_ensure(ed_t *e) {   /* read once; a first set to put patches in */
    lib_t *L = LIB(e);
    if (!L->sets_read) sets_read(e);
    if (L->sets && L->nsets == 0) {
        memset(&L->sets[0], 0, sizeof L->sets[0]);
        snprintf(L->sets[0].name, sizeof L->sets[0].name, "Set 1");
        L->nsets = 1;
        L->cur_set = 0;
    }
}
static int set_used(const set_t *s, int bank) {   /* filled slots (of one bank, or all with bank -1) */
    int n = 0;
    for (int k = 0; k < SET_SLOTS; k++) n += s->slot[k].kind && (bank < 0 || k / 8 == bank);
    return n;
}
static int slot_load(ed_t *e, int k) {
    lib_t *L = LIB(e);
    sets_ensure(e);
    if (!L->nsets || k < 0 || k >= SET_SLOTS) return PE_FAIL;
    slot_t *s = &L->sets[L->cur_set].slot[k];
    L->cur_slot = k;
    L->bank = k / 8;
    redraw(e);
    if (!s->kind) {
        say(e, "%c%d is empty: hold it down (or ASSIGN) to put the current patch there.", 'A' + k / 8, k % 8 + 1);
        return PE_FAIL;
    }
    if (s->kind == 'F') {
        int p = -1, nf = nfactory(e);
        for (int q = 0; q < nf && p < 0; q++) {   /* by name first: the factory list may have moved */
            char nm[PATCH_NAME_MAX + 1];
            prog_name(e, q, nm);
            if (!strcmp(nm, s->name)) p = q;
        }
        if (p < 0 && s->prog >= 0 && s->prog < nf) p = s->prog;
        if (p < 0) { say(e, "The factory preset '%s' is not in this version.", s->name); return PE_FAIL; }
        return patches_load_pos(e, p);
    }
    patches_rescan(e);
    int u = pt_user_index(e, s->name);
    if (u < 0) { say(e, "'%s' (%c%d) is no longer in your patches.", s->name, 'A' + k / 8, k % 8 + 1); return PE_FAIL; }
    return patches_load_pos(e, nfactory(e) + u);
}
static int slot_assign(ed_t *e, int k) {
    lib_t *L = LIB(e);
    sets_ensure(e);
    if (!L->nsets || k < 0 || k >= SET_SLOTS) return PE_FAIL;
    if (e->pt.bank == BANK_NONE) {
        say(e, "A set holds saved patches: SAVE AS this sound first, then hold %c%d again.", 'A' + k / 8, k % 8 + 1);
        return PE_NOT_USER;
    }
    slot_t *s = &L->sets[L->cur_set].slot[k];
    s->kind = e->pt.bank == BANK_FACTORY ? 'F' : 'U';
    s->prog = e->pt.bank == BANK_FACTORY ? e->pt.prog : 0;
    if (e->pt.bank == BANK_FACTORY) prog_name(e, e->pt.prog, s->name);
    else snprintf(s->name, sizeof s->name, "%s", e->pt.name);
    L->cur_slot = k;
    sets_write(e);
    say(e, "%c%d of '%s' now holds '%s'%s.", 'A' + k / 8, k % 8 + 1, L->sets[L->cur_set].name, s->name,
        e->pt.edited ? " (its saved version: SAVE keeps your changes)" : "");
    redraw(e);
    return PE_OK;
}

/* ---- MIDI program and bank change ---- */
typedef struct { int32_t n; intptr_t reserved; VstMidiEvent *ev[]; } vst_events_t;
void lib_midi_events(ed_t *e, const void *ptr) {   /* the host's thread (audio): only notes what to load */
    lib_t *L = LIB(e);
    if (!L || !ptr) return;
    const vst_events_t *list = ptr;
    for (int k = 0; k < list->n; k++) {
        const VstMidiEvent *m = list->ev[k];
        if (!m || m->type != 1) continue;
        int st = m->midiData[0] & 0xF0, d1 = m->midiData[1] & 127, d2 = m->midiData[2] & 127;
        if (st == 0xB0 && d1 == 0) L->msb = d2;
        else if (st == 0xB0 && d1 == 32) L->lsb = d2;
        else if (st == 0xC0) InterlockedExchange(&L->pending, 0x1000000 | L->msb << 16 | L->lsb << 8 | d1);
    }
}
static void program_change(ed_t *e, int msb, int lsb, int prog) {
    lib_t *L = LIB(e);
    if (msb == 1) {   /* a performance set: LSB the set, the program its slot A1..H8 */
        sets_ensure(e);
        if (lsb >= L->nsets || prog >= SET_SLOTS) {
            say(e, "MIDI asked for set %d, %c%d: there is no such set or slot.", lsb + 1, 'A' + prog / 8 % 8, prog % 8 + 1);
            return;
        }
        L->cur_set = lsb;
        slot_load(e, prog);
        return;
    }
    if (msb != 0) { say(e, "MIDI bank %d is not used (0: the patch list, 1: performance sets).", msb); return; }
    patches_rescan(e);
    int pos = lsb * 128 + prog;
    if (pos >= patches_total(e)) { say(e, "MIDI asked for patch %d of the list: there are %d.", pos + 1, patches_total(e)); return; }
    patches_load_pos(e, pos);
}
void lib_idle(ed_t *e) {   /* the window's thread: a program change waiting */
    lib_t *L = LIB(e);
    if (!L || L->busy || L->in_load) return;
    LONG v = InterlockedExchange(&L->pending, 0);
    if (v) program_change(e, v >> 16 & 127, v >> 8 & 127, v & 127);
}

/* ---- the screens' geometry ---- */
int lib_screen(int s) {
    for (int k = 0; k < NTABS; k++)
        if (TAB_SCR[k] == s) return 1;
    return 0;
}
static RECT tab_rect(int k) {
    int x = L.tabline.left;
    for (int j = 0; j < k; j++) x += sc(TAB_W[j] + 6);
    RECT r = {x, L.tabline.top + sc(1), x + sc(TAB_W[k]), L.tabline.bottom - sc(1)};
    return r;
}
static RECT head_btn(int n, int k) {
    int gap = sc(4), bw = (rw(&L.head) - gap * (n - 1)) / n;
    RECT r = {L.head.left + k * (bw + gap), L.head.top, L.head.left + k * (bw + gap) + bw, L.head.bottom - sc(1)};
    return r;
}
static RECT head_var(const char *const *labels, int n, int k) {   /* a row of buttons as wide as their names */
    int gap = sc(4), total = 0, x = L.head.left;
    for (int j = 0; j < n; j++) total += (int)strlen(labels[j]) + 3;
    float unit = (rw(&L.head) - gap * (n - 1)) / (float)total;
    for (int j = 0; j < k; j++) x += (int)((strlen(labels[j]) + 3) * unit) + gap;
    RECT r = {x, L.head.top, x + (int)((strlen(labels[k]) + 3) * unit), L.head.bottom - sc(1)};
    return r;
}
static RECT full_row(int r) {
    RECT x = L.row[r];
    x.top += sc(1);
    x.bottom -= sc(1);
    return x;
}
static RECT slot_row(int r) { RECT x = full_row(r); x.right = x.left + rw(&x) * 62 / 100; return x; }
static RECT side_row(int r) { RECT x = full_row(r); x.left += rw(&L.row[r]) * 62 / 100 + sc(8); return x; }
static RECT side_btn(int k) {
    RECT r = side_row(k == SS_ASSIGN ? 4 : k == SS_CLEAR ? 5 : 0);
    if (k == SS_PREV) r.right = r.left + sc(20);
    if (k == SS_NEXT) r.left = r.right - sc(20);
    if (k >= SS_ASSIGN) InflateRect(&r, -sc(4), -sc(1));
    return r;
}
static RECT list16(int i) {
    RECT nm, c;
    slot_rects(i, &nm, &c);
    RECT r = {nm.left, nm.top + sc(1), c.right, nm.bottom - sc(1)};
    return r;
}
static RECT tag_cell(int kind, int row) {
    RECT x = full_row(row);
    int w = rw(&L.row[0]) / TAG_KINDS;
    x.left = L.row[0].left + kind * w + sc(2);
    x.right = x.left + w - sc(4);
    return x;
}
/* where zone z is on the current screen (or the console); 0: not shown */
static int zone_rect(ed_t *e, int z, RECT *r) {
    lib_t *L_ = LIB(e);
    int s = e->screen;
    if (z == Z_COMPARE) { *r = L.compare; return !IsRectEmpty(r); }
    if (z == Z_SNAPSHOT) { *r = L.snapshot; return !IsRectEmpty(r); }
    if (!lib_screen(s)) return 0;
    if (z >= Z_LIB_TAB && z < Z_LIB_TAB + NTABS) { *r = tab_rect(z - Z_LIB_TAB); return 1; }
    if (s == SCR_PATCHES) return patches_zone_rect(e, z, r);
    if (s == SCR_SNAPS && z >= Z_SNAP_ACT && z < Z_SNAP_ACT + NSNAP_ACT) { *r = head_var(SNAP_ACT, NSNAP_ACT, z - Z_SNAP_ACT); return 1; }
    if (s == SCR_SNAPS && z >= Z_SNAP_ITEM && z < Z_SNAP_ITEM + L_->nsnaps) {
        int row = z - Z_SNAP_ITEM - e->sub * PER_SNAPS;
        if (row < 0 || row >= PER_SNAPS) return 0;
        *r = full_row(row);
        return 1;
    }
    if (s == SCR_TAGS && z >= Z_TAG_ACT && z < Z_TAG_ACT + NTAG_ACT) { *r = head_var(TAG_ACT, NTAG_ACT, z - Z_TAG_ACT); return 1; }
    if (s == SCR_TAGS && z >= Z_TAG_ITEM && z < Z_TAG_ITEM + TAG_KINDS * 1000) {
        int k = (z - Z_TAG_ITEM) / 1000, i = (z - Z_TAG_ITEM) % 1000, row = i - e->sub * PER_TAGS + 1;
        if (i >= L_->nlist[k] || row < 1 || row > PER_TAGS) return 0;
        *r = tag_cell(k, row);
        return 1;
    }
    if (s == SCR_ARRANGE && z >= Z_ARR_ACT && z < Z_ARR_ACT + NARR_ACT) { *r = head_var(ARR_ACT, NARR_ACT, z - Z_ARR_ACT); return 1; }
    if ((s == SCR_SETS || s == SCR_ARRANGE) && z >= Z_SETS_ITEM && z < Z_SETS_ITEM + L_->nsets) {
        int i = z - Z_SETS_ITEM - e->sub * PER_SETS;
        if (i < 0 || i >= PER_SETS) return 0;
        *r = list16(i);
        return 1;
    }
    if (s == SCR_SET && z >= Z_SET_BANK && z < Z_SET_BANK + 8) { *r = head_btn(8, z - Z_SET_BANK); return 1; }
    if (s == SCR_SET && z >= Z_SET_SIDE && z < Z_SET_SIDE + NSET_SIDE) { *r = side_btn(z - Z_SET_SIDE); return 1; }
    if (s == SCR_SET && z >= Z_SET_SLOT && z < Z_SET_SLOT + SET_SLOTS) {
        if ((z - Z_SET_SLOT) / 8 != L_->bank) return 0;
        *r = slot_row((z - Z_SET_SLOT) % 8);
        return 1;
    }
    return 0;
}
int lib_hit(ed_t *e, POINT pt) {
    lib_t *L_ = LIB(e);
    RECT r;
    for (int k = 0; k < NTABS; k++)
        if (zone_rect(e, Z_LIB_TAB + k, &r) && PtInRect(&r, pt)) return Z_LIB_TAB + k;
    switch (e->screen) {
    case SCR_PATCHES: return patches_hit(e, pt);
    case SCR_SNAPS:
        for (int k = 0; k < NSNAP_ACT; k++)
            if (zone_rect(e, Z_SNAP_ACT + k, &r) && PtInRect(&r, pt)) return Z_SNAP_ACT + k;
        for (int i = e->sub * PER_SNAPS; i < L_->nsnaps && i < (e->sub + 1) * PER_SNAPS; i++)
            if (zone_rect(e, Z_SNAP_ITEM + i, &r) && PtInRect(&r, pt)) return Z_SNAP_ITEM + i;
        break;
    case SCR_TAGS:
        for (int k = 0; k < NTAG_ACT; k++)
            if (zone_rect(e, Z_TAG_ACT + k, &r) && PtInRect(&r, pt)) return Z_TAG_ACT + k;
        for (int k = 0; k < TAG_KINDS; k++)
            for (int i = e->sub * PER_TAGS; i < L_->nlist[k] && i < (e->sub + 1) * PER_TAGS; i++)
                if (zone_rect(e, Z_TAG_ITEM + 1000 * k + i, &r) && PtInRect(&r, pt)) return Z_TAG_ITEM + 1000 * k + i;
        break;
    case SCR_ARRANGE:
        for (int k = 0; k < NARR_ACT; k++)
            if (zone_rect(e, Z_ARR_ACT + k, &r) && PtInRect(&r, pt)) return Z_ARR_ACT + k;
        __attribute__((fallthrough));   /* then the list, as ALL SETS */
    case SCR_SETS:
        for (int i = e->sub * PER_SETS; i < L_->nsets && i < (e->sub + 1) * PER_SETS; i++)
            if (zone_rect(e, Z_SETS_ITEM + i, &r) && PtInRect(&r, pt)) return Z_SETS_ITEM + i;
        break;
    case SCR_SET:
        for (int k = 0; k < 8; k++)
            if (zone_rect(e, Z_SET_BANK + k, &r) && PtInRect(&r, pt)) return Z_SET_BANK + k;
        for (int k = 0; k < NSET_SIDE; k++)
            if (zone_rect(e, Z_SET_SIDE + k, &r) && PtInRect(&r, pt)) return Z_SET_SIDE + k;
        for (int k = L_->bank * 8; k < L_->bank * 8 + 8; k++)
            if (zone_rect(e, Z_SET_SLOT + k, &r) && PtInRect(&r, pt)) return Z_SET_SLOT + k;
        break;
    }
    return -1;
}
int lib_subs(ed_t *e) {
    lib_t *L_ = LIB(e);
    int n = 1;
    switch (e->screen) {
    case SCR_PATCHES: return patches_subs(e);
    case SCR_SNAPS: n = (L_->nsnaps + PER_SNAPS - 1) / PER_SNAPS; break;
    case SCR_SETS: case SCR_ARRANGE: n = (L_->nsets + PER_SETS - 1) / PER_SETS; break;
    case SCR_TAGS:
        for (int k = 0; k < TAG_KINDS; k++) {
            int m = (L_->nlist[k] + PER_TAGS - 1) / PER_TAGS;
            if (m > n) n = m;
        }
        break;
    }
    return n > 1 ? n : 1;
}

/* ---- the pages: opening, the buttons ---- */
void lib_open(ed_t *e, int scr) {
    lib_t *L_ = LIB(e);
    if (scr == SCR_PATCHES) { patches_open(e); return; }
    lists_ensure(e);
    if (scr == SCR_SNAPS) snaps_rescan(e);
    if (scr == SCR_SET || scr == SCR_SETS || scr == SCR_ARRANGE) sets_ensure(e);
    e->screen = scr;
    e->sub = 0;
    if ((scr == SCR_SETS || scr == SCR_ARRANGE) && L_->nsets) e->sub = (scr == SCR_ARRANGE && L_->arr_sel >= 0 ? L_->arr_sel : L_->cur_set) / PER_SETS;
    redraw(e);
}
static int need_snap(ed_t *e) {
    lib_t *L_ = LIB(e);
    if (L_->snap_sel >= 0 && L_->snap_sel < L_->nsnaps) return 1;
    say(e, "Click a snapshot in the list first.");
    return 0;
}
static int snap_load(ed_t *e, int i) {
    lib_t *L_ = LIB(e);
    char path[2 * MAX_PATH], what[96];
    if (!snap_path(e, i, path, sizeof path)) return PE_FAIL;
    char *t = pt_read(path);
    if (!t) { say(e, "Could not open the snapshot: %s.", pt_why(GetLastError())); return PE_FAIL; }
    snap_t sn = L_->snaps[i];
    static char state[PATCH_STATE_MAX];
    int bad = 0, header = 0;
    pt_parse(t, state, sizeof state, &bad, &header);
    free(t);
    snprintf(what, sizeof what, "the snapshot of %s", sn.of_name[0] ? sn.of_name : "a sound");
    lib_before_load(e, what);
    pt_apply_state(e, state);
    int same = (sn.of_bank == 'U' && e->pt.bank == BANK_USER && !_stricmp(sn.of_name, e->pt.name)) ||
               (sn.of_bank == 'F' && e->pt.bank == BANK_FACTORY && e->pt.prog == sn.of_prog) ||
               (sn.of_bank == 'N' && e->pt.bank == BANK_NONE && !strcmp(sn.of_name, e->pt.name));
    if (!same) {   /* a snapshot of another patch: it becomes that patch, edited */
        patches_rescan(e);
        if (sn.of_bank == 'U' && pt_user_index(e, sn.of_name) >= 0) e->pt.bank = BANK_USER;
        else if (sn.of_bank == 'F' && sn.of_prog >= 0 && sn.of_prog < nfactory(e)) e->pt.bank = BANK_FACTORY;
        else e->pt.bank = BANK_NONE;
        e->pt.prog = sn.of_prog;
        snprintf(e->pt.name, sizeof e->pt.name, "%s", e->pt.bank == BANK_FACTORY ? "" : sn.of_name[0] ? sn.of_name : "Snapshot");
        e->pt.forced_edit = 1;
        lib_saved_base(e);   /* COMPARE: that patch's saved version */
        lib_identity(e);
    }
    free(L_->last_snap);
    L_->last_snap = state_now(e);
    e->pt.edited = patches_edited_now(e);
    e->sq.dirty = 1;
    lib_after_load(e);
    for (int k = 0; k < L_->nsnaps; k++)   /* (an autosave just made moved the list) */
        if (!strcmp(L_->snaps[k].file, sn.file)) L_->snap_sel = k;
    say(e, "Loaded the snapshot of %s from %s.", sn.of_name[0] ? sn.of_name : "this sound", sn.when);
    redraw(e);
    return PE_OK;
}
static void snap_take(ed_t *e) {
    lib_t *L_ = LIB(e);
    char note[NOTES_MAX + 1] = "";
    if (!ask(e, DK_NOTES, "Snapshot", "A note for this snapshot (optional):", note, sizeof note)) return;
    lib_commit(e);
    take_t t;
    take_now(e, &t, "");
    int ok = snap_write(e, &t, 0, note);
    take_free(&t);
    if (!ok) return;
    L_->snap_sel = 0;
    e->sub = 0;
    say(e, "Snapshot taken (%d in all). LOAD brings it back.", L_->nsnaps);
}
static void snap_notes(ed_t *e, int i) {
    char path[2 * MAX_PATH], note[NOTES_MAX + 1], v[NOTES_MAX * 2 + 8];
    if (!snap_path(e, i, path, sizeof path)) return;
    char *t = pt_read(path);
    if (!t) return;
    line_value(t, "notes", v, sizeof v);
    unesc(v, note, sizeof note);
    if (!ask(e, DK_NOTES, "Notes", "The snapshot's note:", note, sizeof note)) { free(t); return; }
    char line[NOTES_MAX * 2 + 16], en[NOTES_MAX * 2 + 8];
    esc(note, en, sizeof en);
    snprintf(line, sizeof line, "notes=%s\n", en);
    static const char *const K[] = {"notes"};
    char *nt = replace_lines(t, K, 1, "auto=", line);
    free(t);
    if (!nt || !pt_write(path, nt)) say(e, "Could not change the snapshot: %s.", pt_why(GetLastError()));
    else say(e, "The snapshot's note is changed.");
    free(nt);
    snaps_rescan(e);
}
static void snap_action(ed_t *e, int a) {
    lib_t *L_ = LIB(e);
    char path[2 * MAX_PATH];
    switch (a) {
    case SA_TAKE: snap_take(e); break;
    case SA_LOAD: if (need_snap(e)) snap_load(e, L_->snap_sel); break;
    case SA_NOTES: if (need_snap(e)) snap_notes(e, L_->snap_sel); break;
    case SA_DELETE:
        if (!need_snap(e)) break;
        if (!pt_confirm(e, "Delete this snapshot?\nIts file is removed.")) break;
        if (snap_path(e, L_->snap_sel, path, sizeof path) && DeleteFileA(path)) say(e, "Snapshot deleted.");
        else say(e, "Could not delete the snapshot: %s.", pt_why(GetLastError()));
        snaps_rescan(e);
        break;
    case SA_SAVE_OVER:   /* the snapshot's sound over the patch it was taken of (a factory preset: a new name) */
        if (!need_snap(e) || snap_load(e, L_->snap_sel) != PE_OK) break;
        if (e->pt.bank == BANK_USER) patches_save(e);
        else pt_ui_save_as(e);
        break;
    case SA_SAVE_NEW:
        if (need_snap(e) && snap_load(e, L_->snap_sel) == PE_OK) pt_ui_save_as(e);
        break;
    case SA_UNDO: lib_undo(e); break;
    case SA_REDO: lib_redo(e); break;
    }
    redraw(e);
}
static int need_tag(ed_t *e, int editable) {
    lib_t *L_ = LIB(e);
    if (L_->sel_kind < 0 || L_->sel_idx < 0 || L_->sel_idx >= L_->nlist[L_->sel_kind]) {
        say(e, "Click a tag first.");
        return 0;
    }
    if (editable && L_->sel_kind == TAG_TYPE) { say(e, "The TYPE list is fixed: Single, Layer, Split, Multi."); return 0; }
    return 1;
}
/* every user patch with tag kind = from changed to `to` ("" takes it off) */
static void retag_files(ed_t *e, int kind, const char *from, const char *to) {
    patches_rescan(e);
    for (int u = 0; u < e->pt.count; u++) {
        char path[MAX_PATH];
        if (!pt_patch_path(e->pt.list[u], path, sizeof path, 0)) continue;
        char *t = pt_read(path);
        if (!t) continue;
        meta_t m;
        lib_meta_read(t, &m);
        free(t);
        if (_stricmp(m.tag[kind], from)) continue;
        snprintf(m.tag[kind], sizeof m.tag[kind], "%s", to);
        set_file_meta(e, e->pt.list[u], &m);
    }
    lib_t *L_ = LIB(e);
    if (!_stricmp(L_->cur.tag[kind], from)) snprintf(L_->cur.tag[kind], sizeof L_->cur.tag[kind], "%s", to);
    if (!_stricmp(L_->filter[kind], from)) snprintf(L_->filter[kind], sizeof L_->filter[kind], "%s", to);
    patches_rescan(e);
}
static void tag_action(ed_t *e, int a) {
    lib_t *L_ = LIB(e);
    char name[TAG_LEN + 1], prompt[96], q[200];
    int k = L_->sel_kind, i = L_->sel_idx;
    switch (a) {
    case TA_TAG:   /* the selected tag on (or off) the current user patch */
        if (!need_tag(e, 0)) break;
        if (e->pt.bank != BANK_USER) { say(e, "Tags belong to a user patch: SAVE AS this sound first (the save box takes tags)."); break; }
        {
            meta_t m = L_->cur;
            int off = !_stricmp(m.tag[k], L_->list[k][i]);
            snprintf(m.tag[k], sizeof m.tag[k], "%s", off ? "" : L_->list[k][i]);
            if (set_file_meta(e, e->pt.name, &m)) {
                L_->cur = m;
                patches_rescan(e);
                say(e, off ? "'%s' is no longer tagged %s." : "'%s' is tagged %s.", e->pt.name, L_->list[k][i]);
            }
        }
        break;
    case TA_ADD:
        if (L_->nlist[k < 1 ? TAG_CAT : k] >= TAG_LIST_MAX) { say(e, "That list is full (%d tags).", TAG_LIST_MAX); break; }
        k = k < 1 ? TAG_CAT : k;
        name[0] = 0;
        snprintf(prompt, sizeof prompt, "A new %s tag (up to 24 characters):", TAG_TITLE[k]);
        if (!ask(e, DK_TAG, "New tag", prompt, name, sizeof name)) break;
        for (int j = 0; j < L_->nlist[k]; j++)
            if (!_stricmp(L_->list[k][j], name)) { say(e, "%s already has '%s'.", TAG_TITLE[k], name); return; }
        snprintf(L_->list[k][L_->nlist[k]++], TAG_LEN + 1, "%s", name);
        L_->sel_kind = k;
        L_->sel_idx = L_->nlist[k] - 1;
        e->sub = L_->sel_idx / PER_TAGS;
        lists_write(e);
        say(e, "Added the %s tag '%s'.", TAG_TITLE[k], name);
        break;
    case TA_RENAME:
        if (!need_tag(e, 1)) break;
        snprintf(name, sizeof name, "%s", L_->list[k][i]);
        snprintf(prompt, sizeof prompt, "A new name for the tag '%s':", L_->list[k][i]);
        if (!ask(e, DK_TAG, "Rename tag", prompt, name, sizeof name) || !strcmp(name, L_->list[k][i])) break;
        for (int j = 0; j < L_->nlist[k]; j++)
            if (j != i && !_stricmp(L_->list[k][j], name)) { say(e, "%s already has '%s'.", TAG_TITLE[k], name); return; }
        {
            char from[TAG_LEN + 1];
            snprintf(from, sizeof from, "%s", L_->list[k][i]);
            snprintf(L_->list[k][i], TAG_LEN + 1, "%s", name);
            lists_write(e);
            retag_files(e, k, from, name);
            say(e, "Renamed the tag '%s' to '%s' (and on every patch that has it).", from, name);
        }
        break;
    case TA_DELETE:
        if (!need_tag(e, 1)) break;
        snprintf(q, sizeof q, "Delete the tag '%s'?\nIt is taken off every patch that has it.", L_->list[k][i]);
        if (!pt_confirm(e, q)) break;
        {
            char from[TAG_LEN + 1];
            snprintf(from, sizeof from, "%s", L_->list[k][i]);
            memmove(L_->list[k][i], L_->list[k][i + 1], (size_t)(L_->nlist[k] - i - 1) * sizeof L_->list[k][0]);
            L_->nlist[k]--;
            L_->sel_idx = -1;
            lists_write(e);
            retag_files(e, k, from, "");
            say(e, "Deleted the tag '%s'.", from);
        }
        break;
    case TA_CLEAR:
        memset(L_->filter, 0, sizeof L_->filter);
        views(e);
        say(e, "Filters cleared: the PATCHES page lists everything.");
        break;
    case TA_NOTES:
        if (e->pt.bank != BANK_USER) { say(e, "Notes belong to a user patch: SAVE AS this sound first (the save box takes notes)."); break; }
        {
            meta_t m = L_->cur;
            snprintf(prompt, sizeof prompt, "Notes for '%s':", e->pt.name);
            if (!ask(e, DK_NOTES, "Notes", prompt, m.notes, sizeof m.notes)) break;
            if (set_file_meta(e, e->pt.name, &m)) { L_->cur = m; patches_rescan(e); say(e, "Notes saved with '%s'.", e->pt.name); }
        }
        break;
    case TA_SHOW:
        L_->show_notes = !L_->show_notes;
        e->pt.msg[0] = 0;   /* (the notes show at once, not after the last message) */
        break;
    }
    redraw(e);
}
static int need_set(ed_t *e) {
    lib_t *L_ = LIB(e);
    if (L_->arr_sel >= 0 && L_->arr_sel < L_->nsets) return 1;
    say(e, "Click a set in the list first.");
    return 0;
}
static void arr_action(ed_t *e, int a) {
    lib_t *L_ = LIB(e);
    char name[PATCH_NAME_MAX + 1], q[200];
    int i = L_->arr_sel;
    switch (a) {
    case AA_UP: case AA_DOWN: {
        if (!need_set(e)) break;
        int j = a == AA_UP ? i - 1 : i + 1;
        if (j < 0 || j >= L_->nsets) break;
        set_t t = L_->sets[i];
        L_->sets[i] = L_->sets[j];
        L_->sets[j] = t;
        if (L_->cur_set == i) L_->cur_set = j;
        else if (L_->cur_set == j) L_->cur_set = i;
        L_->arr_sel = j;
        e->sub = j / PER_SETS;
        sets_write(e);
        break;
    }
    case AA_COPY:
        if (!need_set(e)) break;
        if (L_->nsets >= SET_COUNT) { say(e, "All %d sets are in use.", SET_COUNT); break; }
        memmove(&L_->sets[i + 2], &L_->sets[i + 1], (size_t)(L_->nsets - i - 1) * sizeof *L_->sets);
        L_->sets[i + 1] = L_->sets[i];
        snprintf(name, sizeof name, "%.27s copy", L_->sets[i].name);
        snprintf(L_->sets[i + 1].name, sizeof L_->sets[0].name, "%s", name);
        L_->nsets++;
        if (L_->cur_set > i) L_->cur_set++;
        L_->arr_sel = i + 1;
        sets_write(e);
        say(e, "Copied '%s' to set %d.", L_->sets[i].name, i + 2);
        break;
    case AA_DELETE:
        if (!need_set(e)) break;
        snprintf(q, sizeof q, "Delete the set '%s'?\nIts patches stay in your patches; the sets after it move up.", L_->sets[i].name);
        if (!pt_confirm(e, q)) break;
        snprintf(name, sizeof name, "%s", L_->sets[i].name);
        memmove(&L_->sets[i], &L_->sets[i + 1], (size_t)(L_->nsets - i - 1) * sizeof *L_->sets);
        L_->nsets--;
        if (L_->cur_set > i || L_->cur_set >= L_->nsets) L_->cur_set = L_->cur_set > 0 ? L_->cur_set - 1 : 0;
        L_->arr_sel = i < L_->nsets ? i : L_->nsets - 1;
        sets_write(e);
        sets_ensure(e);
        say(e, "Deleted the set '%s'.", name);
        break;
    case AA_ADD:
        if (L_->nsets >= SET_COUNT) { say(e, "All %d sets are in use.", SET_COUNT); break; }
        snprintf(name, sizeof name, "Set %d", L_->nsets + 1);
        if (!ask(e, DK_SETNAME, "New set", "A name for the new set:", name, sizeof name)) break;
        memset(&L_->sets[L_->nsets], 0, sizeof *L_->sets);
        snprintf(L_->sets[L_->nsets].name, sizeof L_->sets[0].name, "%s", name);
        L_->arr_sel = L_->nsets++;
        e->sub = L_->arr_sel / PER_SETS;
        sets_write(e);
        say(e, "Added the set '%s' (set %d).", name, L_->nsets);
        break;
    case AA_NAME:
        if (!need_set(e)) break;
        snprintf(name, sizeof name, "%s", L_->sets[i].name);
        if (!ask(e, DK_SETNAME, "Set name", "A new name for the set:", name, sizeof name)) break;
        snprintf(L_->sets[i].name, sizeof L_->sets[0].name, "%s", name);
        sets_write(e);
        break;
    case AA_NOTES:
        if (!need_set(e)) break;
        if (!ask(e, DK_NOTES, "Notes", "Notes for the set:", L_->sets[i].notes, sizeof L_->sets[0].notes)) break;
        sets_write(e);
        break;
    }
    redraw(e);
}
int lib_press(ed_t *e, int z) {   /* 1: z was the library's */
    lib_t *L_ = LIB(e);
    if (!L_) return 0;
    if (z == Z_COMPARE) { lib_compare(e); redraw(e); return 1; }
    if (z == Z_SNAPSHOT) {
        if (e->screen == SCR_SNAPS) { e->screen = SCR_MATRIX; e->sub = 0; redraw(e); }
        else lib_open(e, SCR_SNAPS);
        return 1;
    }
    if (z >= Z_LIB_TAB && z < Z_LIB_TAB + NTABS) { lib_open(e, TAB_SCR[z - Z_LIB_TAB]); return 1; }
    if (z >= Z_SNAP_ACT && z < Z_SNAP_ACT + NSNAP_ACT) { snap_action(e, z - Z_SNAP_ACT); return 1; }
    if (z >= Z_TAG_ACT && z < Z_TAG_ACT + NTAG_ACT) { tag_action(e, z - Z_TAG_ACT); return 1; }
    if (z >= Z_ARR_ACT && z < Z_ARR_ACT + NARR_ACT) { arr_action(e, z - Z_ARR_ACT); return 1; }
    if (z >= Z_SET_BANK && z < Z_SET_BANK + 8) {   /* a bank's eight patches shown (nothing loads) */
        L_->bank = z - Z_SET_BANK;
        redraw(e);
        return 1;
    }
    if (z >= Z_SET_SIDE && z < Z_SET_SIDE + NSET_SIDE) {
        int k = z - Z_SET_SIDE;
        sets_ensure(e);
        if (k == SS_PREV || k == SS_NEXT) {
            L_->cur_set = (L_->cur_set + (k == SS_NEXT ? 1 : L_->nsets - 1)) % L_->nsets;
            L_->cur_slot = -1;
            sets_write(e);
        } else if (L_->cur_slot < 0) {
            say(e, "Click a number (A1 ... H8) first.");
        } else if (k == SS_ASSIGN) {
            slot_assign(e, L_->cur_slot);
        } else {
            memset(&L_->sets[L_->cur_set].slot[L_->cur_slot], 0, sizeof(slot_t));
            sets_write(e);
            say(e, "%c%d is empty now.", 'A' + L_->cur_slot / 8, L_->cur_slot % 8 + 1);
        }
        redraw(e);
        return 1;
    }
    if (z >= Z_SNAP_ITEM && z < Z_SNAP_ITEM + 10000) { L_->snap_sel = z - Z_SNAP_ITEM; redraw(e); return 1; }
    if (z >= Z_TAG_ITEM && z < Z_TAG_ITEM + TAG_KINDS * 1000) {   /* select it, and filter by it (again: no filter) */
        int k = (z - Z_TAG_ITEM) / 1000, i = (z - Z_TAG_ITEM) % 1000;
        if (i >= L_->nlist[k]) return 1;
        L_->sel_kind = k;
        L_->sel_idx = i;
        int off = !_stricmp(L_->filter[k], L_->list[k][i]);
        snprintf(L_->filter[k], sizeof L_->filter[k], "%s", off ? "" : L_->list[k][i]);
        patches_rescan(e);
        if (off) say(e, "%s filter off.", TAG_TITLE[k]);
        else say(e, "PATCHES lists only %s: %s (%d factory, %d yours).", TAG_TITLE[k], L_->list[k][i], lib_view_count(e, 0),
                 lib_view_count(e, 1));
        redraw(e);
        return 1;
    }
    if (z >= Z_SET_SLOT && z < Z_SET_SLOT + SET_SLOTS) {   /* held: assign; let go early: load (lib_release) */
        L_->hold_slot = z - Z_SET_SLOT;
        L_->hold_t0 = GetTickCount();
        L_->hold_done = 0;
        e->mode = M_LIB;
        if (e->wnd) SetCapture(e->wnd);
        redraw(e);
        return 1;
    }
    if (z >= Z_SETS_ITEM && z < Z_SETS_ITEM + SET_COUNT) {
        int i = z - Z_SETS_ITEM;
        if (i >= L_->nsets) return 1;
        if (e->screen == SCR_ARRANGE) { L_->arr_sel = i; redraw(e); return 1; }
        L_->cur_set = i;   /* ALL SETS: this set is the one played */
        L_->cur_slot = -1;
        L_->bank = 0;
        sets_write(e);
        lib_open(e, SCR_SET);
        return 1;
    }
    return 0;
}
void lib_release(ed_t *e) {
    lib_t *L_ = LIB(e);
    if (!L_ || L_->hold_slot < 0) return;
    int k = L_->hold_slot;
    L_->hold_slot = -1;
    if (!L_->hold_done) {
        if (GetTickCount() - L_->hold_t0 >= LONG_PRESS_MS) slot_assign(e, k);
        else slot_load(e, k);
    }
    L_->hold_done = 0;
    redraw(e);
}

/* ---- drawing (under the patch name; editor_draw.c draws the name and the help) ---- */
#define C_LCD RGB(150, 215, 232)
#define C_DIM RGB(90, 130, 145)
#define C_DARK RGB(52, 78, 90)
#define C_HOT RGB(255, 255, 255)
#define C_FRAME RGB(0x27, 0x40, 0x4E)
#define C_FRAME_HOT RGB(0x96, 0xD7, 0xE8)
#define C_FILL_HOT RGB(0x16, 0x2C, 0x3A)
#define C_SEL RGB(0x1F, 0x46, 0x5A)
static void fill(HDC dc, RECT r, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b); }
static void frame(HDC dc, RECT r, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FrameRect(dc, &r, b); DeleteObject(b); }
static void label(HDC dc, RECT r, const char *s, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextA(dc, s, -1, &r, fmt | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}
static void button(HDC dc, RECT r, const char *t, int live, int lit, int hot) {
    if (lit) fill(dc, r, C_SEL);
    else if (hot) fill(dc, r, C_FILL_HOT);
    frame(dc, r, hot || lit ? C_FRAME_HOT : C_FRAME);
    label(dc, r, t, font_tiny, !live ? C_DIM : hot || lit ? C_HOT : C_LCD, DT_CENTER);
}
static void draw_tabs(HDC dc, ed_t *e) {
    for (int k = 0; k < NTABS; k++) {
        RECT r = tab_rect(k);
        int cur = TAB_SCR[k] == e->screen, hot = e->hover == Z_LIB_TAB + k;
        if (cur) fill(dc, r, C_SEL);
        if (hot) frame(dc, r, C_FRAME_HOT);
        label(dc, r, TAB_NAME[k], font_tiny, cur || hot ? C_HOT : C_DIM, DT_CENTER);
    }
    int subs = lib_subs(e);
    if (subs <= 1) return;
    char n[16];
    snprintf(n, sizeof n, "%d / %d", e->sub + 1, subs);
    RECT nr = {L.sub_prev.right, L.tabline.top, L.sub_next.left, L.tabline.bottom};
    label(dc, nr, n, font_tiny, C_LCD, DT_CENTER);
    label(dc, L.sub_prev, "<", font_screen, e->hover == Z_SUB_PREV ? C_HOT : C_LCD, DT_CENTER);
    label(dc, L.sub_next, ">", font_screen, e->hover == Z_SUB_NEXT ? C_HOT : C_LCD, DT_CENTER);
}
static void draw_set(HDC dc, ed_t *e) {
    lib_t *L_ = LIB(e);
    sets_ensure(e);
    if (!L_->nsets) return;
    set_t *s = &L_->sets[L_->cur_set];
    for (int b = 0; b < 8; b++) {   /* the bank buttons: lit the one shown, dark an empty bank */
        RECT r = head_btn(8, b);
        char t[4] = {(char)('A' + b), 0};
        int used = set_used(s, b), lit = b == L_->bank, hot = e->hover == Z_SET_BANK + b;
        if (lit) fill(dc, r, C_SEL);
        else if (hot) fill(dc, r, C_FILL_HOT);
        frame(dc, r, hot || lit ? C_FRAME_HOT : C_FRAME);
        label(dc, r, t, font_cell, lit || hot ? C_HOT : used ? C_LCD : C_DARK, DT_CENTER);
    }
    for (int row = 0; row < 8; row++) {   /* the bank's numbers: lit the one playing, dark an empty one */
        int k = L_->bank * 8 + row, hot = e->hover == Z_SET_SLOT + k, held = L_->hold_slot == k;
        const slot_t *sl = &s->slot[k];
        RECT r = slot_row(row), tr = r, kr = r;
        char t[64];
        if (k == L_->cur_slot) fill(dc, r, C_SEL);
        if (hot || held) frame(dc, r, held && GetTickCount() - L_->hold_t0 >= LONG_PRESS_MS ? C_HOT : C_FRAME_HOT);
        tr.left += sc(6);
        snprintf(t, sizeof t, "%c%d", 'A' + L_->bank, row + 1);
        label(dc, tr, t, font_cell, sl->kind ? C_LCD : C_DARK, DT_LEFT);
        tr.left += sc(30);
        label(dc, tr, sl->kind ? sl->name : "(empty)", font_cell, sl->kind ? (k == L_->cur_slot || hot ? C_HOT : C_LCD) : C_DARK, DT_LEFT);
        kr.right -= sc(6);
        if (sl->kind) label(dc, kr, sl->kind == 'F' ? "FACTORY" : "USER", font_tiny, C_DIM, DT_RIGHT);
    }
    char t[64];
    RECT r0 = side_row(0), r1 = side_row(1), r2 = side_row(2), r7 = side_row(7);
    snprintf(t, sizeof t, "SET %d / %d", L_->cur_set + 1, L_->nsets);
    label(dc, r0, t, font_tiny, C_LCD, DT_CENTER);
    label(dc, side_btn(SS_PREV), "<", font_screen, e->hover == Z_SET_SIDE + SS_PREV ? C_HOT : C_LCD, DT_CENTER);
    label(dc, side_btn(SS_NEXT), ">", font_screen, e->hover == Z_SET_SIDE + SS_NEXT ? C_HOT : C_LCD, DT_CENTER);
    label(dc, r1, s->name, font_cell, C_HOT, DT_CENTER);
    snprintf(t, sizeof t, "%d of 64 used", set_used(s, -1));
    label(dc, r2, t, font_tiny, C_DIM, DT_CENTER);
    button(dc, side_btn(SS_ASSIGN), "ASSIGN", L_->cur_slot >= 0, 0, e->hover == Z_SET_SIDE + SS_ASSIGN);
    button(dc, side_btn(SS_CLEAR), "CLEAR", L_->cur_slot >= 0, 0, e->hover == Z_SET_SIDE + SS_CLEAR);
    label(dc, r7, "hold a number: assign", font_tiny, C_DIM, DT_CENTER);
}
static void draw_sets(HDC dc, ed_t *e, int arrange) {
    lib_t *L_ = LIB(e);
    sets_ensure(e);
    if (arrange)
        for (int k = 0; k < NARR_ACT; k++)
            button(dc, head_var(ARR_ACT, NARR_ACT, k), ARR_ACT[k], k == AA_ADD || L_->arr_sel >= 0, 0, e->hover == Z_ARR_ACT + k);
    else
        label(dc, L.head, "Click a set to play it (then A-H and 1-8 on SET).", font_tiny, C_DIM, DT_LEFT);
    for (int i = e->sub * PER_SETS; i < L_->nsets && i < (e->sub + 1) * PER_SETS; i++) {
        RECT r = list16(i - e->sub * PER_SETS), tr = r;
        int hot = e->hover == Z_SETS_ITEM + i, cur = i == L_->cur_set, sel = arrange && i == L_->arr_sel;
        char t[80], n[16];
        if (arrange ? sel : cur) fill(dc, r, C_SEL);
        if (hot || (arrange && cur)) frame(dc, r, hot ? C_FRAME_HOT : C_FRAME);
        tr.left += sc(6);
        tr.right -= sc(4);
        snprintf(t, sizeof t, "%03d   %s", i + 1, L_->sets[i].name);
        label(dc, tr, t, font_cell, hot || cur || sel ? C_HOT : C_LCD, DT_LEFT);
        snprintf(n, sizeof n, "%d", set_used(&L_->sets[i], -1));
        label(dc, tr, n, font_tiny, C_DIM, DT_RIGHT);
    }
}
static void draw_snaps(HDC dc, ed_t *e) {
    lib_t *L_ = LIB(e);
    for (int k = 0; k < NSNAP_ACT; k++) {
        int live = k == SA_TAKE ? 1 : k == SA_UNDO ? L_->nundo > 0 : k == SA_REDO ? L_->nredo > 0 : L_->snap_sel >= 0;
        button(dc, head_var(SNAP_ACT, NSNAP_ACT, k), SNAP_ACT[k], live, 0, e->hover == Z_SNAP_ACT + k);
    }
    if (!L_->nsnaps) {
        label(dc, full_row(0), "No snapshots yet: TAKE keeps the sound as it is now.", font_cell, C_DIM, DT_LEFT);
        label(dc, full_row(1), "Loading another sound autosaves unsaved edits here.", font_cell, C_DIM, DT_LEFT);
    }
    for (int i = e->sub * PER_SNAPS; i < L_->nsnaps && i < (e->sub + 1) * PER_SNAPS; i++) {
        const snap_t *s = &L_->snaps[i];
        RECT r = full_row(i - e->sub * PER_SNAPS), a = r, b = r, c = r;
        int hot = e->hover == Z_SNAP_ITEM + i, sel = i == L_->snap_sel;
        if (sel) fill(dc, r, C_SEL);
        if (hot) frame(dc, r, C_FRAME_HOT);
        a.left += sc(6);
        a.right = a.left + sc(112);
        b.left = a.right + sc(4);
        b.right = b.left + sc(108);
        c.left = b.right + sc(4);
        c.right -= sc(4);
        COLORREF col = hot || sel ? C_HOT : C_LCD;
        label(dc, a, s->when[0] ? s->when + 5 : "?", font_cell, col, DT_LEFT);   /* (without the year) */
        label(dc, b, s->of_name[0] ? s->of_name : s->of_bank == 'F' ? "factory preset" : "sound", font_cell, col, DT_LEFT);
        char t[SNAP_NOTE_SHORT + 16];
        snprintf(t, sizeof t, "%s%s", s->autosave ? "AUTO  " : "", s->note);
        label(dc, c, t, font_tiny, s->autosave ? C_DIM : col, DT_LEFT);
    }
}
static void draw_tags(HDC dc, ed_t *e) {
    lib_t *L_ = LIB(e);
    lists_ensure(e);
    for (int k = 0; k < NTAG_ACT; k++) {
        int live = k == TA_TAG ? L_->sel_idx >= 0 && e->pt.bank == BANK_USER :
                   k == TA_RENAME || k == TA_DELETE ? L_->sel_idx >= 0 && L_->sel_kind > TAG_TYPE :
                   k == TA_CLEAR ? lib_filtering(e) : k == TA_NOTES ? e->pt.bank == BANK_USER : 1;
        button(dc, head_var(TAG_ACT, NTAG_ACT, k), TAG_ACT[k], live, k == TA_SHOW && L_->show_notes, e->hover == Z_TAG_ACT + k);
    }
    for (int k = 0; k < TAG_KINDS; k++) {
        RECT h = tag_cell(k, 0), line;
        char t[64];
        snprintf(t, sizeof t, "%s%s", TAG_TITLE[k], L_->filter[k][0] ? "  (filter)" : "");
        label(dc, h, t, font_tiny, L_->filter[k][0] ? C_LCD : C_DIM, DT_LEFT);
        line = h;
        line.top = line.bottom - 1;
        fill(dc, line, C_FRAME);
        for (int i = e->sub * PER_TAGS; i < L_->nlist[k] && i < (e->sub + 1) * PER_TAGS; i++) {
            RECT r = tag_cell(k, i - e->sub * PER_TAGS + 1), tr = r;
            int hot = e->hover == Z_TAG_ITEM + 1000 * k + i, on = !_stricmp(L_->filter[k], L_->list[k][i]);
            int mine = !_stricmp(L_->cur.tag[k], L_->list[k][i]), sel = L_->sel_kind == k && L_->sel_idx == i;
            if (on) fill(dc, r, C_SEL);
            if (hot || sel) frame(dc, r, hot ? C_HOT : C_FRAME_HOT);
            if (mine) {   /* the current patch has this tag: a small square */
                int m = sc(4), cy = (r.top + r.bottom) / 2;
                RECT q = {r.left + sc(3), cy - m / 2, r.left + sc(3) + m, cy - m / 2 + m};
                fill(dc, q, C_LCD);
            }
            tr.left += sc(11);
            label(dc, tr, L_->list[k][i], font_cell, hot || on ? C_HOT : C_LCD, DT_LEFT);
        }
    }
}
void lib_draw(HDC dc, ed_t *e) {
    draw_tabs(dc, e);
    switch (e->screen) {
    case SCR_PATCHES: patches_draw(dc, e); break;
    case SCR_SET: draw_set(dc, e); break;
    case SCR_SETS: draw_sets(dc, e, 0); break;
    case SCR_ARRANGE: draw_sets(dc, e, 1); break;
    case SCR_SNAPS: draw_snaps(dc, e); break;
    case SCR_TAGS: draw_tags(dc, e); break;
    }
}

/* the line under the patch name: its tags and the host's tempo; 0 when there is nothing to show */
int lib_home_line(ed_t *e, char *out, int n) {
    lib_t *L_ = LIB(e);
    int used = 0;
    out[0] = 0;
    if (!L_) return 0;
    for (int k = 0; k < TAG_KINDS; k++)
        if (L_->cur.tag[k][0]) used += snprintf(out + used, used < n ? n - used : 0, "%s%s", used ? "  |  " : "", L_->cur.tag[k]);
    if (L_->cur.notes[0] && used < n) used += snprintf(out + used, n - used, "%sNOTES", used ? "  |  " : "");
    if (L_->tempo > 0 && used < n) snprintf(out + used, n - used, "%s%.1f BPM", used ? "      " : "", L_->tempo);
    return out[0] != 0;
}
int lib_notes_help(ed_t *e, char *out, int n) {   /* SHOW NOTES: the patch's notes where the help goes */
    lib_t *L_ = LIB(e);
    if (!L_ || !L_->show_notes) return 0;
    char t[64];
    patches_title(e, t, sizeof t);
    snprintf(out, n, "NOTES (%s): %s", t, L_->cur.notes[0] ? L_->cur.notes : "none. TAGS > NOTES writes some.");
    return 1;
}
void lib_help(ed_t *e, char *out, int n) {
    lib_t *L_ = LIB(e);
    switch (e->screen) {
    case SCR_PATCHES: {
        char h[400];
        patches_help(e, h, sizeof h);
        if (lib_filtering(e)) {
            char f[120] = "";
            int u = 0;
            for (int k = 0; k < TAG_KINDS; k++)
                if (L_->filter[k][0]) u += snprintf(f + u, sizeof f - u, "%s%s", u ? " + " : "", L_->filter[k]);
            snprintf(out, n, "Filtered by %s (TAGS: CLEAR shows everything). %s", f, h);
        } else {
            snprintf(out, n, "%s", h);
        }
        break;
    }
    case SCR_SET: {
        set_t *s = L_->nsets ? &L_->sets[L_->cur_set] : NULL;
        snprintf(out, n, "A-H show a bank's eight patches (nothing loads); a number loads its patch; hold a number (or "
                         "ASSIGN) to put the current patch there. MIDI: bank MSB 1, LSB %d, program 0-63.%s%s",
                 L_->cur_set, s && s->notes[0] ? "\nNotes: " : "", s ? s->notes : "");
        break;
    }
    case SCR_SETS:
        snprintf(out, n, "%d sets of 64 patches (up to %d). MIDI bank select MSB 1 with LSB = set - 1, then a program "
                         "change 0-63 = A1-H8, plays a set's patch; MSB 0 with LSB n: patch 128n + program of the list.",
                 L_->nsets, SET_COUNT);
        break;
    case SCR_ARRANGE:
        if (L_->arr_sel >= 0 && L_->arr_sel < L_->nsets && L_->sets[L_->arr_sel].notes[0])
            snprintf(out, n, "%s: %s", L_->sets[L_->arr_sel].name, L_->sets[L_->arr_sel].notes);
        else
            snprintf(out, n, "Click a set, then UP / DOWN move it, COPY doubles it, DELETE removes it, NAME and NOTES "
                             "change it. ADD makes a new empty set.");
        break;
    case SCR_SNAPS:
        if (L_->snap_sel >= 0 && L_->snap_sel < L_->nsnaps) {
            const snap_t *s = &L_->snaps[L_->snap_sel];
            snprintf(out, n, "%s of %s, %s: %s", s->autosave ? "Autosave" : "Snapshot", s->of_name[0] ? s->of_name : "a sound",
                     s->when, s->note[0] ? s->note : "(no note)");
        } else {
            snprintf(out, n, "%d snapshots. TAKE keeps the sound now with a note; loading another sound autosaves "
                             "unsaved edits (AUTO, the newest %d kept). Undo: %d steps, redo: %d (Ctrl+Z / Ctrl+Y).",
                     L_->nsnaps, AUTOSAVE_KEEP, L_->nundo, L_->nredo);
        }
        break;
    case SCR_TAGS:
        snprintf(out, n, "Click a tag to filter PATCHES by it (again: off; one per column). TAG PATCH puts the selected "
                         "tag on the current patch (the square marks its tags). ADD, RENAME, DELETE edit the lists.");
        break;
    }
}
const char *lib_zone_help(int z) {
    if (z == Z_COMPARE) return "COMPARE: hear the saved version of this patch; again: back to your edit. Loading another "
                               "sound ends it (your edit is autosaved as a snapshot).";
    if (z == Z_SNAPSHOT) return "SNAPSHOT: the snapshots page: timestamped saves of the sound with notes, the autosaves, "
                                "and UNDO / REDO of any edit (Ctrl+Z / Ctrl+Y in the window).";
    if (z >= Z_LIB_TAB && z < Z_LIB_TAB + NTABS) {
        static const char *const H[NTABS] = {"PATCHES: factory presets and your own patches.",
                                             "SET: the current performance set: banks A-H, numbers 1-8.",
                                             "ALL SETS: pick the performance set to play.",
                                             "ARRANGE: move, copy, delete, add, name the sets, and their notes.",
                                             "SNAPSHOTS: saves of the sound in progress, autosaves, undo and redo.",
                                             "TAGS: filter the patch list, tag the current patch, edit the tag lists, notes."};
        return H[z - Z_LIB_TAB];
    }
    if (z >= Z_SNAP_ACT && z < Z_SNAP_ACT + NSNAP_ACT) {
        static const char *const H[NSNAP_ACT] = {
            "TAKE: a snapshot of the sound as it is now (asks for a note).",
            "LOAD: the selected snapshot's sound (an unsaved edit is autosaved first).",
            "NOTES: change the selected snapshot's note.", "DELETE: removes the selected snapshot (asks first).",
            "SAVE OVER: loads the selected snapshot and saves it over the patch it was taken of.",
            "SAVE NEW: loads the selected snapshot and saves it as a new patch (asks for a name).",
            "UNDO: the last edit undone (Ctrl+Z).", "REDO: the last undo done again (Ctrl+Y)."};
        return H[z - Z_SNAP_ACT];
    }
    if (z >= Z_TAG_ACT && z < Z_TAG_ACT + NTAG_ACT) {
        static const char *const H[NTAG_ACT] = {
            "TAG PATCH: the selected tag on the current user patch (again: off).",
            "ADD: a new tag in the selected tag's list (CATEGORY when none).", "RENAME: the selected tag, on every patch.",
            "DELETE: the selected tag, off every patch (asks first).", "CLEAR: no filters: PATCHES lists everything.",
            "NOTES: the current user patch's notes.", "SHOW NOTES: the current patch's notes in the help area of every page."};
        return H[z - Z_TAG_ACT];
    }
    if (z >= Z_ARR_ACT && z < Z_ARR_ACT + NARR_ACT) {
        static const char *const H[NARR_ACT] = {"UP: the selected set one place up.", "DOWN: the selected set one place down.",
                                                "COPY: a copy of the selected set after it.",
                                                "DELETE: removes the selected set (asks first; its patches stay).",
                                                "ADD: a new empty set at the end (asks for a name).",
                                                "NAME: a new name for the selected set.", "NOTES: the selected set's notes."};
        return H[z - Z_ARR_ACT];
    }
    if (z >= Z_SET_BANK && z < Z_SET_BANK + 8) return "A bank: shows its eight patches below (nothing loads until you click a number).";
    if (z >= Z_SET_SIDE && z < Z_SET_SIDE + NSET_SIDE) {
        static const char *const H[NSET_SIDE] = {"The previous set.", "The next set.",
                                                 "ASSIGN: the current patch into the selected number.",
                                                 "CLEAR: empties the selected number."};
        return H[z - Z_SET_SIDE];
    }
    if (z >= Z_SNAP_ITEM && z < Z_SNAP_ITEM + 10000) return "Click to select this snapshot (LOAD, NOTES, DELETE, SAVE OVER, SAVE NEW act on it).";
    if (z >= Z_TAG_ITEM && z < Z_TAG_ITEM + TAG_KINDS * 1000) return "Click: filter the patch list by this tag (again: off) and select it.";
    if (z >= Z_SET_SLOT && z < Z_SET_SLOT + SET_SLOTS) return "Click: load this patch. Hold (0.6 s): put the current patch here.";
    if (z >= Z_SETS_ITEM && z < Z_SETS_ITEM + SET_COUNT) return "Click: this set (ALL SETS: play it; ARRANGE: select it).";
    return NULL;
}

/* ---- the window's timer (every 50 ms) ---- */
void lib_tick(ed_t *e) {
    lib_t *L_ = LIB(e);
    if (!L_) return;
    DWORD now = GetTickCount();
    fresh(e);
    if (L_->open && !L_->hold && !L_->in_load && e->mode == M_NONE && now - L_->touched_at >= UNDO_IDLE_MS) lib_commit(e);
    if (e->mode == M_LIB && L_->hold_slot >= 0 && !L_->hold_done && now - L_->hold_t0 >= LONG_PRESS_MS) {
        slot_assign(e, L_->hold_slot);   /* held long enough: assigned now, as the instrument does */
        L_->hold_done = 1;
    }
    lib_idle(e);
    static DWORD asked;
    if (e->host && now - asked >= 500) {   /* the host's tempo, for the home line */
        asked = now;
        const char *ti = (const char *)e->host(e->fx, 7 /* audioMasterGetTime */, 0, 1 << 10, NULL, 0);
        double t = 0;
        if (ti && (*(const int32_t *)(ti + 84) & (1 << 10))) t = *(const double *)(ti + 32);
        if (t != L_->tempo) {
            L_->tempo = t;
            if (e->wnd) InvalidateRect(e->wnd, &L.name, FALSE);
        }
    }
}

/* ---- load_test (effVendorSpecific TEST_LIB; buf holds 1 KB) ---- */
intptr_t lib_test(ed_t *e, intptr_t op, char *buf, float opt) {
    lib_t *L_ = LIB(e);
    int k = (int)lroundf(opt);
    RECT r;
    if (!L_) return 0;
    switch (op) {
    case LT_UNDO: return lib_undo(e);
    case LT_REDO: return lib_redo(e);
    case LT_STEPS: lib_commit(e); return L_->nundo + 1000 * L_->nredo;
    case LT_COMPARING: return L_->comparing;
    case LT_SNAPS: snaps_rescan(e); return L_->nsnaps;
    case LT_AUTOS: {
        snaps_rescan(e);
        int a = 0;
        for (int i = 0; i < L_->nsnaps; i++) a += L_->snaps[i].autosave;
        return a;
    }
    case LT_SNAP_INFO:
        if (k < 0 || k >= L_->nsnaps) return 0;
        snprintf(buf, 1024, "%s|%c|%s|%d|%s", L_->snaps[k].when, L_->snaps[k].of_bank, L_->snaps[k].of_name,
                 L_->snaps[k].autosave, L_->snaps[k].note);
        return 1;
    case LT_SNAP_SEL: return L_->snap_sel;
    case LT_ZONE:
        if (!zone_rect(e, k, &r)) return -1;
        return ((r.left + r.right) / 2) | ((intptr_t)((r.top + r.bottom) / 2) << 16);
    case LT_SCREEN: return e->screen;
    case LT_META:
        snprintf(buf, 1024, "%s|%s|%s|%s|%s", L_->cur.tag[0], L_->cur.tag[1], L_->cur.tag[2], L_->cur.tag[3], L_->cur.notes);
        return 1;
    case LT_VIEW: return lib_view_count(e, 1) + 1000 * lib_view_count(e, 0);
    case LT_SETS: sets_ensure(e); return L_->nsets + 1000 * L_->cur_set;
    case LT_SLOT:
        sets_ensure(e);
        if (k < 0 || k >= SET_SLOTS || !L_->nsets) return 0;
        snprintf(buf, 1024, "%c,%d,%s", L_->sets[L_->cur_set].slot[k].kind ? L_->sets[L_->cur_set].slot[k].kind : '-',
                 L_->sets[L_->cur_set].slot[k].prog, L_->sets[L_->cur_set].slot[k].name);
        return L_->sets[L_->cur_set].slot[k].kind != 0;
    case LT_SET_INFO:
        sets_ensure(e);
        if (k < 0 || k >= L_->nsets) return 0;
        snprintf(buf, 1024, "%s|%s", L_->sets[k].name, L_->sets[k].notes);
        return 1;
    case LT_IDLE: lib_idle(e); return 1;
    case LT_TAGS: {
        lists_ensure(e);
        if (k < 0 || k >= TAG_KINDS) return 0;
        int u = 0;
        buf[0] = 0;
        for (int j = 0; j < L_->nlist[k] && u < 1000; j++) u += snprintf(buf + u, 1024 - u, "%s%s", j ? "," : "", L_->list[k][j]);
        return L_->nlist[k];
    }
    case LT_HOME: return lib_home_line(e, buf, 1024);
    case LT_LABEL: lib_commit(e); snprintf(buf, 1024, "%s", L_->nundo ? L_->undo[L_->nundo - 1].what : ""); return L_->nundo;
    case LT_NOTES_SHOWN: return lib_notes_help(e, buf, 1024);
    case LT_TICK: lib_tick(e); return 1;
    case LT_FORGET: lib_commit(e); clear_stack(L_->undo, &L_->nundo); clear_stack(L_->redo, &L_->nredo); return 1;
    }
    return 0;
}
