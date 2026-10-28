/* User patches for the desktop editor: save the sound under a name, browse factory presets and user patches together,
 * rename, delete, import and export. UI thread only (the window, and the host's calls that hosts make from it); the
 * audio thread never touches a file.
 *
 * Where: one file per patch in %USERPROFILE%\Documents\ONE-ish\Patches (the Documents folder Windows reports, made on
 * the first save), or %APPDATA%\ONE-ish\Patches when Documents is missing or can't be written. The environment
 * variable ONEISH_PATCH_DIR overrides both (load_test points it at a temporary folder).
 *
 * The file, <name>.oneish, plain text (docs/PATCHES.md):
 *     ONE-ish patch 1          the header (the format's version; a newer number still loads what it can)
 *     name=Bright Pad          the name (the file's name is what the list shows)
 *     author=...               optional, kept when the patch is saved over
 *     category=...             optional, kept when the patch is saved over
 *     flt_cut=42.5             then one parameter per line: its key and value as the engine's own state has them
 *     o1_wave=1                (an option's number; its label, e.g. o1_wave=Saw, works too)
 * Only values that differ from the defaults are written, and loading starts from the defaults, so a patch made today
 * still loads after parameters are added. Lines may also be separated by ';' (a pasted engine state works). Unknown
 * keys are skipped quietly; lines that make no sense are skipped and counted, and the screen says so.
 *
 * Loading: factory presets through the host's program list, as before (effSetProgram); user patches by parsing the
 * file into a clean engine state, restoring it the way a host restores a project (effSetChunk: defaults, then the
 * values, applied together so LINK CUTOFF can't pull one cutoff while the other loads), then telling the host about
 * every parameter that changed (audioMasterAutomate inside begin/end edit, as a knob does), so the host and the window
 * stay in step. The editor also adds the patch's identity to the project it saves ("ui_patch=" in effGetChunk; the
 * engine ignores the key), so a reopened project still shows "U Bright Pad". */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include <commdlg.h>
#include <shlobj.h>

#define EXT ".oneish"
static const char HEADER[] = "ONE-ish patch";
enum { FORMAT_VERSION = 1, FILE_MAX = 65536, PER = ROWS - 1 };   /* PER: list entries per column (row 0: its heading) */
enum { ID_PROMPT = 100, ID_EDIT, ID_WHY };
enum { effGetProgram_ = 3, effGetProgramNameIndexed_ = 29 };

/* ---- small helpers ---- */
static void redraw(ed_t *e) { if (e->wnd) InvalidateRect(e->wnd, NULL, FALSE); }

static void say(ed_t *e, const char *fmt, ...) {   /* the result of an operation, shown on the screen for a while */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->pt.msg, sizeof e->pt.msg, fmt, ap);
    va_end(ap);
    e->pt.msg_at = GetTickCount();
    redraw(e);
}

const char *patches_message(ed_t *e) {
    return e->pt.msg[0] && GetTickCount() - e->pt.msg_at < 6000 ? e->pt.msg : NULL;
}

static void trim(char *s) {   /* spaces and tabs off both ends */
    char *a = s;
    while (*a == ' ' || *a == '\t') a++;
    size_t n = strlen(a);
    while (n && (a[n - 1] == ' ' || a[n - 1] == '\t' || a[n - 1] == '\r')) n--;
    memmove(s, a, n);
    s[n] = 0;
}

static const char *why_io(DWORD err) {   /* a Windows error, in words */
    static char other[48];
    switch (err) {
    case ERROR_ACCESS_DENIED: case ERROR_WRITE_PROTECT: return "the folder is read-only or blocked";
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return "the disk is full";
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return "the file or folder is missing";
    case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION: return "another program has the file open";
    case ERROR_INVALID_DATA: return "the file is too big to be a patch";
    }
    snprintf(other, sizeof other, "Windows error %lu", (unsigned long)err);
    return other;
}

static int prog_of(ed_t *e) { return (int)wrapped_call(e->fx, effGetProgram_, 0, 0, NULL, 0); }

/* ---- names ---- */
/* a patch name: 1..32 plain characters, none that a file name can't hold or that the file format uses (';'), no
 * spaces at the ends, no dot at the end, not a name Windows reserves (CON, NUL, COM1 ...) */
int patch_name_ok(const char *s, char *why, int n) {
    static const char *const reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6",
                                           "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6",
                                           "LPT7", "LPT8", "LPT9"};
    const char *bad = NULL;
    size_t len = strlen(s);
    if (!len) bad = "the name is empty";
    else if (len > PATCH_NAME_MAX) bad = "the name is longer than 32 characters";
    else if (s[0] == ' ' || s[len - 1] == ' ') bad = "the name starts or ends with a space";
    else if (s[len - 1] == '.') bad = "the name ends with a dot";
    for (size_t k = 0; !bad && k < len; k++) {
        unsigned char c = (unsigned char)s[k];
        if (c < 32 || c > 126) bad = "use plain letters, numbers, spaces and punctuation";
        else if (strchr("\\/:*?\"<>|;", c)) bad = "a name can't hold any of \\ / : * ? \" < > | ;";
    }
    for (unsigned k = 0; !bad && k < sizeof reserved / sizeof reserved[0]; k++) {
        size_t r = strlen(reserved[k]);
        if (!_strnicmp(s, reserved[k], r) && (s[r] == 0 || s[r] == '.')) bad = "Windows reserves that name";
    }
    if (bad && why) snprintf(why, n, "%s", bad);
    return bad == NULL;
}

/* a usable name from a file's name: its stem, odd characters turned into '-', at most 32 characters */
static void name_from_path(const char *path, char *out) {
    const char *b = path;
    for (const char *c = path; *c; c++)
        if (*c == '\\' || *c == '/') b = c + 1;
    char t[MAX_PATH];
    snprintf(t, sizeof t, "%s", b);
    size_t l = strlen(t), x = strlen(EXT);
    if (l > x && !_stricmp(t + l - x, EXT)) t[l - x] = 0;
    for (char *c = t; *c; c++)
        if ((unsigned char)*c < 32 || (unsigned char)*c > 126 || strchr("\\/:*?\"<>|;", *c)) *c = '-';
    t[PATCH_NAME_MAX] = 0;
    trim(t);
    for (size_t n = strlen(t); n && (t[n - 1] == '.' || t[n - 1] == ' '); n--) t[n - 1] = 0;
    snprintf(out, PATCH_NAME_MAX + 1, "%s", patch_name_ok(t, NULL, 0) ? t : "Imported patch");
}

/* ---- the folder ---- */
static int is_dir(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static int is_file(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static int make_dir(const char *p) {   /* with every missing folder above it */
    int r = SHCreateDirectoryExA(NULL, p, NULL);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || is_dir(p);
}
static int known_dir(int csidl, char *out, int n) {
    char base[MAX_PATH];
    if (SHGetFolderPathA(NULL, csidl, NULL, 0, base) != S_OK) return 0;
    return snprintf(out, n, "%s\\ONE-ish\\Patches", base) < n;
}

/* the patch folder into out. create: make it if it isn't there yet (0 when it can't be made). Without create, the
 * folder it would be (1), or 0 when there is none at all. */
int patch_dir(char *out, int n, int create) {
    char d[MAX_PATH], a[MAX_PATH];
    DWORD k = GetEnvironmentVariableA("ONEISH_PATCH_DIR", d, sizeof d);
    if (k > 0 && k < sizeof d) {
        if (!GetFullPathNameA(d, n, out, NULL)) return 0;
        return !create || make_dir(out);
    }
    int hd = known_dir(CSIDL_PERSONAL, d, sizeof d), ha = known_dir(CSIDL_APPDATA, a, sizeof a);
    const char *pick = hd && is_dir(d) ? d : ha && is_dir(a) ? a : NULL;
    if (!pick && create) pick = hd && make_dir(d) ? d : ha && make_dir(a) ? a : NULL;
    if (!pick && !create) pick = hd ? d : ha ? a : NULL;
    if (!pick) return 0;
    return snprintf(out, n, "%s", pick) < n;
}

static int patch_path(const char *name, char *out, int n, int create) {
    char d[MAX_PATH];
    return patch_dir(d, sizeof d, create) && snprintf(out, n, "%s\\%s" EXT, d, name) < n;
}

/* ---- the list of user patches ---- */
static int by_name(const void *a, const void *b) { return _stricmp(a, b); }

void patches_rescan(ed_t *e) {
    patches_t *p = &e->pt;
    char dir[MAX_PATH], pat[MAX_PATH + 16];
    p->count = 0;
    if (!patch_dir(dir, sizeof dir, 0)) return;
    snprintf(pat, sizeof pat, "%s\\*" EXT, dir);
    WIN32_FIND_DATAA f;
    HANDLE h = FindFirstFileA(pat, &f);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        size_t l = strlen(f.cFileName), x = strlen(EXT);
        if ((f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || l <= x || _stricmp(f.cFileName + l - x, EXT)) continue;
        char nm[MAX_PATH];
        memcpy(nm, f.cFileName, l - x);
        nm[l - x] = 0;
        if (!patch_name_ok(nm, NULL, 0)) continue;   /* a file renamed by hand to an odd name: IMPORT tidies it */
        if (p->count == p->cap) {
            int cap = p->cap ? 2 * p->cap : 64;
            void *q = realloc(p->list, cap * sizeof *p->list);
            if (!q) break;
            p->list = q;
            p->cap = cap;
        }
        strcpy(p->list[p->count++], nm);
    } while (FindNextFileA(h, &f));
    FindClose(h);
    qsort(p->list, p->count, sizeof *p->list, by_name);
}

static int user_index(ed_t *e, const char *name) {
    for (int i = 0; i < e->pt.count; i++)
        if (!_stricmp(e->pt.list[i], name)) return i;
    return -1;
}

/* ---- files ---- */
static char *read_text(const char *path) {   /* the whole file (at most FILE_MAX bytes), NUL-terminated; free() it */
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *b = malloc(FILE_MAX + 1);
    size_t n = b ? fread(b, 1, FILE_MAX + 1, f) : 0;
    fclose(f);
    if (!b || n > FILE_MAX) { free(b); SetLastError(ERROR_INVALID_DATA); return NULL; }
    for (size_t k = 0; k < n; k++)
        if (!b[k]) b[k] = '\n';   /* stray zero bytes: the rest still reads */
    b[n] = 0;
    return b;
}

static int write_text(const char *path, const char *text) {   /* to <path>.tmp, then over the old file */
    char tmp[MAX_PATH + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    size_t len = strlen(text);
    int ok = fwrite(text, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    if (ok) ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        DWORD err = GetLastError();
        DeleteFileA(tmp);
        SetLastError(err);
    }
    return ok;
}

static const char *skip_bom(const char *t) { return !strncmp(t, "\xEF\xBB\xBF", 3) ? t + 3 : t; }

static int has_header(const char *t) {   /* the first line that isn't blank is the header */
    t = skip_bom(t);
    while (*t && isspace((unsigned char)*t)) t++;
    return !strncmp(t, HEADER, sizeof HEADER - 1);
}

/* ---- the format ---- */
typedef struct { char name[PATCH_NAME_MAX + 1]; int header, values, bad; } parsed_t;

static int param_of(const char *key) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, key)) return i;
    return -1;
}

/* a value for param i as the engine's state holds it (a number in its units; an option's number), or 0 */
static int value_of(int i, const char *v, char *out, int n) {
    const param_t *p = &PARAMS[i];
    char *end;
    double d = strtod(v, &end);
    if (end != v && !*end && isfinite(d)) {
        if (p->nopts) {
            long k = lround(d < 0 ? 0 : d > p->nopts - 1 ? p->nopts - 1 : d);
            snprintf(out, n, "%ld", k);
        } else if (d < p->min || d > p->max) {
            snprintf(out, n, "%g", d < p->min ? p->min : p->max);
        } else {
            snprintf(out, n, "%s", v);   /* as written: the engine reads back exactly what it saved */
        }
        return 1;
    }
    for (int k = 0; k < p->nopts; k++)
        if (!_stricmp(v, p->opts[k])) { snprintf(out, n, "%d", k); return 1; }
    return 0;
}

/* a patch file's text -> a clean engine state "v=1;key=value;..." (only lines that make sense); t is changed */
static void parse(char *t, char *state, int n, parsed_t *r) {
    memset(r, 0, sizeof *r);
    int used = snprintf(state, n, "v=1");
    char *save = NULL;
    for (char *line = strtok_r((char *)skip_bom(t), "\r\n;", &save); line; line = strtok_r(NULL, "\r\n;", &save)) {
        trim(line);
        if (!line[0] || line[0] == '#') continue;
        if (!strncmp(line, HEADER, sizeof HEADER - 1)) { r->header = 1; continue; }
        char *eq = strchr(line, '=');
        if (!eq || eq == line) { r->bad++; continue; }
        *eq = 0;
        char *key = line, *val = eq + 1, v[64];
        trim(key);
        trim(val);
        if (!strcmp(key, "name")) { snprintf(r->name, sizeof r->name, "%s", val); continue; }
        if (!strcmp(key, "seq")) {   /* the step sequencer's steps (src/stepseq.h text): passed on as they are */
            int len = snprintf(NULL, 0, ";seq=%s", val);
            if (used + len >= n) { r->bad++; continue; }
            used += snprintf(state + used, n - used, ";seq=%s", val);
            r->values++;
            continue;
        }
        if (!strcmp(key, "author") || !strcmp(key, "category") || !strcmp(key, "v") || !strcmp(key, "ui_patch"))
            continue;
        int i = param_of(key);
        if (i < 0) continue;   /* a parameter this version doesn't have: skipped quietly */
        if (!value_of(i, val, v, sizeof v)) { r->bad++; continue; }
        int len = snprintf(NULL, 0, ";%s=%s", key, v);
        if (used + len >= n) { r->bad++; continue; }
        used += snprintf(state + used, n - used, ";%s=%s", key, v);
        r->values++;
    }
}

/* the text with its name line set to name (one name line, right after the header; the header added if missing) */
static void set_name_line(const char *t, const char *name, char *out, int n) {
    int used = 0, headed = 0;
    out[0] = 0;
    const char *s = skip_bom(t);
    while (*s && used < n) {
        const char *eol = strchr(s, '\n');
        int len = eol ? (int)(eol - s) : (int)strlen(s);
        char line[1024];
        snprintf(line, sizeof line, "%.*s", len, s);
        trim(line);
        s += len + (eol ? 1 : 0);
        if (!strncmp(line, "name=", 5) || !strncmp(line, "name =", 6)) continue;
        if (!line[0] && !s[0]) break;
        used += snprintf(out + used, n - used, "%s\n", line);
        if (!headed && !strncmp(line, HEADER, sizeof HEADER - 1)) {
            headed = 1;
            used += snprintf(out + used, n - used, "name=%s\n", name);
        }
    }
    if (!headed) {   /* no header: put one, and the name, in front */
        char *rest = strdup(out);
        snprintf(out, n, "%s %d\nname=%s\n%s", HEADER, FORMAT_VERSION, name, rest ? rest : "");
        free(rest);
    }
}

/* the author= and category= lines of an older copy (kept when a patch is saved over) */
static void keep_meta(const char *t, char *out, int n) {
    int used = 0;
    out[0] = 0;
    while (*t && used < n) {
        const char *eol = strchr(t, '\n');
        int len = eol ? (int)(eol - t) : (int)strlen(t);
        char line[512];
        snprintf(line, sizeof line, "%.*s", len, t);
        trim(line);
        if (!strncmp(line, "author=", 7) || !strncmp(line, "category=", 9))
            used += snprintf(out + used, n - used, "%s\n", line);
        t += len + (eol ? 1 : 0);
    }
}

/* the file for a sound: header, name, kept meta lines, then the state one key=value per line ("v=" left out) */
static char *build_text(const char *name, const char *meta, const char *state) {
    size_t cap = strlen(state) + strlen(meta) + 256;
    char *out = malloc(cap);
    if (!out) return NULL;
    int used = snprintf(out, cap, "%s %d\nname=%s\n%s", HEADER, FORMAT_VERSION, name, meta);
    for (const char *s = state; *s;) {
        const char *end = strchr(s, ';');
        int len = end ? (int)(end - s) : (int)strlen(s);
        if (len && strncmp(s, "v=", 2) && strncmp(s, "ui_patch=", 9))
            used += snprintf(out + used, cap - used, "%.*s\n", len, s);
        s += len + (end ? 1 : 0);
    }
    return out;
}

/* ---- the sound ---- */
static int current_state(ed_t *e, char *buf, int n) {   /* the engine's state, without the editor's tag */
    void *chunk = NULL;
    intptr_t len = wrapped_call(e->fx, effGetChunk, 0, 0, &chunk, 0);
    if (len <= 0 || !chunk) return 0;
    snprintf(buf, n, "%s", (const char *)chunk);
    return 1;
}

static void mark_clean(ed_t *e) {   /* what is loaded now is what the patch holds */
    for (int i = 0; i < NPARAMS; i++) e->pt.base[i] = e->fx->getParameter(e->fx, i);
    e->pt.forced_edit = e->pt.edited = 0;
    redraw(e);
}

int patches_edited_now(ed_t *e) {
    if (e->pt.forced_edit) return 1;
    for (int i = 0; i < NPARAMS; i++)
        if (e->fx->getParameter(e->fx, i) != e->pt.base[i]) return 1;
    return 0;
}

/* WM_TIMER, after e->shown was refreshed: is the sound edited, has a message run out? 1: redraw */
int patches_tick(ed_t *e) {
    patches_t *p = &e->pt;
    int was = p->edited;
    p->edited = p->forced_edit;
    for (int i = 0; i < NPARAMS && !p->edited; i++)
        if (e->shown[i] != p->base[i]) p->edited = 1;
    int expired = p->msg[0] && !patches_message(e);
    if (expired) p->msg[0] = 0;
    return was != p->edited || expired;
}

/* a clean state into the engine (as a host restores a project), then the host told of every value that changed */
static void apply_state(ed_t *e, const char *state) {
    static float before[NPARAMS];
    for (int i = 0; i < NPARAMS; i++) before[i] = e->fx->getParameter(e->fx, i);
    wrapped_call(e->fx, effSetChunk, 0, (intptr_t)strlen(state) + 1, (void *)state, 0);
    for (int i = 0; i < NPARAMS; i++) {
        float v = e->fx->getParameter(e->fx, i);
        if (v == before[i] || !e->host) continue;
        e->host(e->fx, audioMasterBeginEdit, i, 0, NULL, 0);
        e->host(e->fx, audioMasterAutomate, i, 0, NULL, v);
        e->host(e->fx, audioMasterEndEdit, i, 0, NULL, 0);
    }
    redraw(e);
}

static void follow(ed_t *e) {   /* the list page shows the part with the current patch */
    int pos = patches_pos(e), nf = e->fx->numPrograms;
    if (e->screen == SCR_PATCHES && pos >= 0) e->sub = (pos < nf ? pos : pos - nf) / PER;
}

static void load_factory(ed_t *e, int prog) {
    if (prog != prog_of(e)) {
        e->fx->dispatcher(e->fx, effSetProgram, 0, prog, NULL, 0);   /* the host's program list, as before */
    } else {   /* the wrapper skips re-picking the current program: put its values back ourselves */
#ifdef NPRESETS
        static char state[PATCH_STATE_MAX];
        int used = snprintf(state, sizeof state, "v=1");
        for (int k = 0; k < PRESETS[prog].n && used < (int)sizeof state; k++)
            used += snprintf(state + used, sizeof state - used, ";%s=%s", PARAMS[PRESETS[prog].values[k].param].key,
                             PRESETS[prog].values[k].value);
        if (used < (int)sizeof state) apply_state(e, state);
#endif
    }
    e->pt.bank = BANK_FACTORY;
    e->pt.prog = prog;
    e->pt.name[0] = 0;
    e->pt.msg[0] = 0;
    mark_clean(e);
    if (e->host) e->host(e->fx, audioMasterUpdateDisplay, 0, 0, NULL, 0);
    follow(e);
}

int patches_load_user(ed_t *e, const char *name_in) {
    char name[PATCH_NAME_MAX + 1], path[MAX_PATH];
    snprintf(name, sizeof name, "%s", name_in);
    if (!patch_path(name, path, sizeof path, 0)) { say(e, "No patch folder found."); return PE_FAIL; }
    char *t = read_text(path);
    if (!t) { say(e, "Could not open '%s': %s.", name, why_io(GetLastError())); return PE_FAIL; }
    static char state[PATCH_STATE_MAX];
    parsed_t r;
    parse(t, state, sizeof state, &r);
    free(t);
    apply_state(e, state);
    e->pt.bank = BANK_USER;
    snprintf(e->pt.name, sizeof e->pt.name, "%s", name);
    mark_clean(e);
    follow(e);
    if (r.bad || !r.header) {
        say(e, "Loaded '%s', but %d line%s of the file made no sense%s; the rest loaded, the others are at their "
               "defaults.", name, r.bad, r.bad == 1 ? "" : "s", r.header ? "" : " (and it has no 'ONE-ish patch' line)");
        return PE_BADFILE;
    }
    e->pt.msg[0] = 0;
    return PE_OK;
}

/* ---- browsing: factory presets first, then the user patches ---- */
int patches_total(ed_t *e) { return e->fx->numPrograms + e->pt.count; }

int patches_pos(ed_t *e) {
    if (e->pt.bank == BANK_FACTORY) return e->pt.prog;
    if (e->pt.bank == BANK_USER) {
        int u = user_index(e, e->pt.name);
        return u < 0 ? -1 : e->fx->numPrograms + u;
    }
    return -1;
}

int patches_load_pos(ed_t *e, int pos) {
    int nf = e->fx->numPrograms;
    if (pos < 0 || pos >= patches_total(e)) return PE_FAIL;
    if (pos < nf) { load_factory(e, pos); return PE_OK; }
    return patches_load_user(e, e->pt.list[pos - nf]);
}

void patches_step(ed_t *e, int d) {
    patches_rescan(e);
    int pos = patches_pos(e), n = patches_total(e), to = pos < 0 ? (d > 0 ? 0 : n - 1) : pos + d;
    if (to < 0) to = 0;
    if (to > n - 1) to = n - 1;
    if (to != pos) patches_load_pos(e, to);
}

void patches_title(ed_t *e, char *out, int n) {
    patches_t *p = &e->pt;
    const char *mark = p->edited ? " *" : "";
    if (p->bank == BANK_FACTORY) {
        char nm[64] = "";
        wrapped_call(e->fx, effGetProgramNameIndexed_, p->prog, 0, nm, 0);
        snprintf(out, n, "F %03d  %s%s", p->prog + 1, nm, mark);
    } else if (p->bank == BANK_USER) {
        snprintf(out, n, "U  %s%s", p->name, mark);
    } else {
        snprintf(out, n, "-  %s%s", p->name[0] ? p->name : "Unsaved sound", mark);
    }
}

/* ---- the operations (no dialogs here: patches_action asks, load_test calls these directly) ---- */
int patches_save_as(ed_t *e, const char *name_in, int overwrite) {
    char name[64], why[96], path[MAX_PATH];
    snprintf(name, sizeof name, "%s", name_in);
    trim(name);
    if (!patch_name_ok(name, why, sizeof why)) { say(e, "Can't use that name: %s.", why); return PE_BADNAME; }
    if (!patch_path(name, path, sizeof path, 1)) {
        say(e, "Could not make the patch folder: %s.", why_io(GetLastError()));
        return PE_FAIL;
    }
    int exists = is_file(path);
    if (exists && !overwrite) return PE_EXISTS;
    static char state[PATCH_STATE_MAX];
    if (!current_state(e, state, sizeof state)) { say(e, "Could not read the sound from the engine."); return PE_FAIL; }
    char meta[512] = "";
    if (exists) {
        char *old = read_text(path);
        if (old) keep_meta(old, meta, sizeof meta);
        free(old);
    }
    char *text = build_text(name, meta, state);
    if (!text || !write_text(path, text)) {
        DWORD err = text ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
        free(text);
        say(e, "Could not save '%s': %s.", name, why_io(err));
        return PE_FAIL;
    }
    free(text);
    patches_rescan(e);
    int u = user_index(e, name);
    e->pt.bank = BANK_USER;
    snprintf(e->pt.name, sizeof e->pt.name, "%s", u >= 0 ? e->pt.list[u] : name);
    mark_clean(e);
    follow(e);
    say(e, "Saved '%s'.", e->pt.name);
    return PE_OK;
}

int patches_save(ed_t *e) {
    if (e->pt.bank != BANK_USER) return PE_NOT_USER;
    char name[PATCH_NAME_MAX + 1];
    snprintf(name, sizeof name, "%s", e->pt.name);
    return patches_save_as(e, name, 1);
}

static int need_user(ed_t *e, const char *what) {
    if (e->pt.bank == BANK_USER) return 1;
    say(e, "%s works on a user patch: pick one from USER first.", what);
    return 0;
}

int patches_rename(ed_t *e, const char *to_in, int overwrite) {
    if (!need_user(e, "RENAME")) return PE_NOT_USER;
    char to[64], from[PATCH_NAME_MAX + 1], why[96], src[MAX_PATH], dst[MAX_PATH];
    snprintf(to, sizeof to, "%s", to_in);
    trim(to);
    snprintf(from, sizeof from, "%s", e->pt.name);
    if (!patch_name_ok(to, why, sizeof why)) { say(e, "Can't use that name: %s.", why); return PE_BADNAME; }
    if (!strcmp(from, to)) return PE_OK;
    if (!patch_path(from, src, sizeof src, 0) || !patch_path(to, dst, sizeof dst, 0)) return PE_FAIL;
    int case_only = !_stricmp(from, to);
    if (!case_only && is_file(dst) && !overwrite) return PE_EXISTS;
    char *t = read_text(src);
    if (!t) { say(e, "Could not open '%s': %s.", from, why_io(GetLastError())); return PE_FAIL; }
    size_t cap = strlen(t) + 256;
    char *text = malloc(cap);
    int ok = text != NULL;
    if (ok) {
        set_name_line(t, to, text, (int)cap);
        ok = write_text(case_only ? src : dst, text);
        if (ok && case_only) ok = MoveFileA(src, dst);   /* the same file, its name's case changed */
        else if (ok) DeleteFileA(src);
    }
    DWORD err = GetLastError();
    free(t);
    free(text);
    if (!ok) { say(e, "Could not rename '%s': %s.", from, why_io(err)); return PE_FAIL; }
    patches_rescan(e);
    snprintf(e->pt.name, sizeof e->pt.name, "%s", to);
    follow(e);
    say(e, "Renamed '%s' to '%s'.", from, to);
    return PE_OK;
}

int patches_delete(ed_t *e) {
    if (!need_user(e, "DELETE")) return PE_NOT_USER;
    char path[MAX_PATH];
    if (!patch_path(e->pt.name, path, sizeof path, 0) || !DeleteFileA(path)) {
        say(e, "Could not delete '%s': %s.", e->pt.name, why_io(GetLastError()));
        return PE_FAIL;
    }
    patches_rescan(e);
    e->pt.bank = BANK_NONE;
    e->pt.forced_edit = e->pt.edited = 1;
    say(e, "Deleted '%s'. The sound is still loaded: SAVE AS keeps it.", e->pt.name);
    return PE_OK;
}

void patches_import_name(const char *src, char *out) { name_from_path(src, out); }

int patches_import(ed_t *e, const char *src, int overwrite) {
    char name[PATCH_NAME_MAX + 1], dst[MAX_PATH], full_src[MAX_PATH], full_dst[MAX_PATH];
    char *t = read_text(src);
    if (!t) { say(e, "Could not open that file: %s.", why_io(GetLastError())); return PE_FAIL; }
    if (!has_header(t)) {
        free(t);
        say(e, "That file is not a ONE-ish patch (its first line isn't 'ONE-ish patch').");
        return PE_BADFILE;
    }
    name_from_path(src, name);
    if (!patch_path(name, dst, sizeof dst, 1)) {
        free(t);
        say(e, "Could not make the patch folder: %s.", why_io(GetLastError()));
        return PE_FAIL;
    }
    if (GetFullPathNameA(src, sizeof full_src, full_src, NULL) && GetFullPathNameA(dst, sizeof full_dst, full_dst, NULL) &&
        !_stricmp(full_src, full_dst)) {
        free(t);
        say(e, "'%s' is already in your patches.", name);
        return PE_OK;
    }
    if (is_file(dst) && !overwrite) { free(t); return PE_EXISTS; }
    size_t cap = strlen(t) + 256;
    char *text = malloc(cap);
    int ok = text != NULL;
    if (ok) {
        set_name_line(t, name, text, (int)cap);
        ok = write_text(dst, text);
    }
    DWORD err = GetLastError();
    free(t);
    free(text);
    if (!ok) { say(e, "Could not import '%s': %s.", name, why_io(err)); return PE_FAIL; }
    patches_rescan(e);
    say(e, "Imported '%s' into your patches (USER). Click it to load it.", name);
    return PE_OK;
}

int patches_export(ed_t *e, const char *dst) {
    char name[PATCH_NAME_MAX + 1];
    static char state[PATCH_STATE_MAX];
    if (!current_state(e, state, sizeof state)) { say(e, "Could not read the sound from the engine."); return PE_FAIL; }
    name_from_path(dst, name);
    char *text = build_text(name, "", state);
    if (!text || !write_text(dst, text)) {
        DWORD err = text ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
        free(text);
        say(e, "Could not export: %s.", why_io(err));
        return PE_FAIL;
    }
    free(text);
    say(e, "Exported the sound to %s", dst);
    return PE_OK;
}

/* ---- the host's side: programs and projects ---- */
void patches_init(ed_t *e) {
    e->pt.bank = BANK_FACTORY;
    e->pt.prog = prog_of(e);
    mark_clean(e);
}

void patches_free(ed_t *e) {
    free(e->pt.list);
    e->pt.list = NULL;
    e->pt.count = e->pt.cap = 0;
}

void patches_program_set(ed_t *e, int before) {   /* after effSetProgram (the host's or ours) */
    int now = prog_of(e);
    if (now == before) return;   /* the wrapper keeps the sound when the current program is picked again */
    e->pt.bank = BANK_FACTORY;
    e->pt.prog = now;
    e->pt.name[0] = 0;
    mark_clean(e);
    follow(e);
}

/* effGetChunk: the engine's state plus ";ui_patch=<edited>,<F|U|N>,<program>,<name>" when it fits the 8 KB chunk */
intptr_t patches_get_chunk(ed_t *e, void *ptr) {
    patches_t *p = &e->pt;
    if (!current_state(e, p->chunk, sizeof p->chunk)) return 0;
    char tag[96];
    int n = snprintf(tag, sizeof tag, ";ui_patch=%d,%c,%d,%s", patches_edited_now(e),
                     p->bank == BANK_FACTORY ? 'F' : p->bank == BANK_USER ? 'U' : 'N', p->prog, p->name);
    size_t len = strlen(p->chunk);
    if (len + n < sizeof p->chunk) memcpy(p->chunk + len, tag, n + 1);
    *(void **)ptr = p->chunk;
    return (intptr_t)strlen(p->chunk) + 1;
}

void patches_chunk_set(ed_t *e, const void *chunk, intptr_t len) {   /* after the host restored a project */
    patches_t *p = &e->pt;
    static char copy[PATCH_STATE_MAX];
    if (!chunk || len <= 0) return;
    snprintf(copy, sizeof copy, "%.*s", (int)(len < (intptr_t)sizeof copy ? len : (intptr_t)sizeof copy - 1),
             (const char *)chunk);
    const char *tag = strstr(copy, ";ui_patch=");
    int edited = 1, prog = 0, at = 0;
    char bank = 'N';
    mark_clean(e);
    p->bank = BANK_NONE;
    snprintf(p->name, sizeof p->name, "Project sound");
    if (tag && sscanf(tag, ";ui_patch=%d,%c,%d,%n", &edited, &bank, &prog, &at) == 3 && at > 0) {
        char name[64];
        snprintf(name, sizeof name, "%.*s", (int)strcspn(tag + at, ";"), tag + at);
        if (bank == 'F' && prog >= 0 && prog < e->fx->numPrograms) {
            p->bank = BANK_FACTORY;
            p->prog = prog;
            p->name[0] = 0;
        } else if ((bank == 'U' || bank == 'N') && patch_name_ok(name, NULL, 0)) {
            p->bank = bank == 'U' ? BANK_USER : BANK_NONE;
            snprintf(p->name, sizeof p->name, "%s", name);
        }
    } else {
        edited = 0;
    }
    p->forced_edit = p->edited = edited != 0;
    redraw(e);
}

/* ---- the window: dialogs ---- */
static HWND owner(ed_t *e) { return e->wnd ? GetAncestor(e->wnd, GA_ROOT) : NULL; }

static int confirm(ed_t *e, const char *text) {
    return MessageBoxA(owner(e), text, "ONE-ish", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

typedef struct { const char *title, *prompt; char text[64]; } ask_t;

static INT_PTR CALLBACK ask_proc(HWND d, UINT m, WPARAM wp, LPARAM lp) {
    ask_t *a = (ask_t *)GetWindowLongPtrA(d, DWLP_USER);
    if (m == WM_INITDIALOG) {
        a = (ask_t *)lp;
        SetWindowLongPtrA(d, DWLP_USER, lp);
        SetWindowTextA(d, a->title);
        SetDlgItemTextA(d, ID_PROMPT, a->prompt);
        SetDlgItemTextA(d, ID_EDIT, a->text);
        SendDlgItemMessageA(d, ID_EDIT, EM_LIMITTEXT, PATCH_NAME_MAX, 0);
        SendDlgItemMessageA(d, ID_EDIT, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(d, ID_EDIT));
        return FALSE;
    }
    if (m == WM_COMMAND && LOWORD(wp) == IDOK && a) {
        char t[128], why[96];
        GetDlgItemTextA(d, ID_EDIT, t, sizeof t);
        trim(t);
        if (!patch_name_ok(t, why, sizeof why)) { SetDlgItemTextA(d, ID_WHY, why); return TRUE; }
        snprintf(a->text, sizeof a->text, "%s", t);
        EndDialog(d, 1);
        return TRUE;
    }
    if (m == WM_COMMAND && LOWORD(wp) == IDCANCEL) { EndDialog(d, 0); return TRUE; }
    return FALSE;
}

static WORD *wstr(WORD *w, const char *s) {   /* a dialog template's text: UTF-16, here plain ASCII */
    do *w++ = (WORD)(unsigned char)*s; while (*s++);
    return w;
}
static WORD *item(WORD *w, DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const char *text) {
    w = (WORD *)(((ULONG_PTR)w + 3) & ~(ULONG_PTR)3);
    DLGITEMTEMPLATE *it = (DLGITEMTEMPLATE *)w;
    it->style = style | WS_CHILD | WS_VISIBLE;
    it->dwExtendedStyle = 0;
    it->x = x; it->y = y; it->cx = cx; it->cy = cy; it->id = id;
    w = (WORD *)(it + 1);
    *w++ = 0xFFFF;
    *w++ = cls;   /* 0x80 button, 0x81 edit, 0x82 static */
    w = wstr(w, text);
    *w++ = 0;     /* no creation data */
    return w;
}

/* a modal "name?" box: name in (a suggestion) and out; 0 if cancelled. The name is checked before it closes. */
static int ask_name(ed_t *e, const char *title, const char *prompt, char *name) {
    static DWORD buf[512];
    memset(buf, 0, sizeof buf);
    DLGTEMPLATE *dt = (DLGTEMPLATE *)buf;
    dt->style = DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dt->cdit = 5;
    dt->cx = 230;
    dt->cy = 76;
    WORD *w = (WORD *)(dt + 1);
    *w++ = 0;   /* no menu */
    *w++ = 0;   /* the standard dialog class */
    w = wstr(w, title);
    *w++ = 9;   /* DS_SETFONT: 9 pt */
    w = wstr(w, "Segoe UI");
    w = item(w, SS_LEFT, 8, 8, 214, 10, ID_PROMPT, 0x82, "");
    w = item(w, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 8, 21, 214, 13, ID_EDIT, 0x81, "");
    w = item(w, SS_LEFT, 8, 38, 214, 10, ID_WHY, 0x82, "");
    w = item(w, WS_TABSTOP | BS_DEFPUSHBUTTON, 118, 55, 50, 14, IDOK, 0x80, "OK");
    item(w, WS_TABSTOP | BS_PUSHBUTTON, 172, 55, 50, 14, IDCANCEL, 0x80, "Cancel");
    ask_t a = {title, prompt, ""};
    snprintf(a.text, sizeof a.text, "%s", name);
    HINSTANCE inst = e->wnd ? (HINSTANCE)GetWindowLongPtrA(e->wnd, GWLP_HINSTANCE) : GetModuleHandleA(NULL);
    if (DialogBoxIndirectParamA(inst, dt, owner(e), ask_proc, (LPARAM)&a) != 1) return 0;
    snprintf(name, PATCH_NAME_MAX + 1, "%s", a.text);
    return 1;
}

static void suggestion(ed_t *e, char *out) {   /* a starting name for SAVE AS */
    if (e->pt.bank == BANK_FACTORY) {
        char nm[64] = "";
        wrapped_call(e->fx, effGetProgramNameIndexed_, e->pt.prog, 0, nm, 0);
        snprintf(out, PATCH_NAME_MAX + 1, "%s", nm[0] ? nm : "My Sound");
    } else {
        snprintf(out, PATCH_NAME_MAX + 1, "%s", e->pt.name[0] && e->pt.bank == BANK_USER ? e->pt.name :
                 e->pt.name[0] && strcmp(e->pt.name, "Project sound") ? e->pt.name : "My Sound");
    }
}

static void ui_save_as(ed_t *e) {
    char name[PATCH_NAME_MAX + 1], q[200];
    suggestion(e, name);
    while (ask_name(e, "Save as", "A name for this sound (up to 32 characters):", name)) {
        if (patches_save_as(e, name, 0) != PE_EXISTS) return;
        snprintf(q, sizeof q, "A patch named '%s' already exists.\nReplace it with this sound?", name);
        if (confirm(e, q)) { patches_save_as(e, name, 1); return; }
    }
}

static void ui_rename(ed_t *e) {
    if (!need_user(e, "RENAME")) return;
    char name[PATCH_NAME_MAX + 1], prompt[96], q[200];
    snprintf(name, sizeof name, "%s", e->pt.name);
    snprintf(prompt, sizeof prompt, "A new name for '%s':", e->pt.name);
    while (ask_name(e, "Rename", prompt, name)) {
        if (patches_rename(e, name, 0) != PE_EXISTS) return;
        snprintf(q, sizeof q, "A patch named '%s' already exists.\nReplace it with '%s'?", name, e->pt.name);
        if (confirm(e, q)) { patches_rename(e, name, 1); return; }
    }
}

static void ui_import(ed_t *e) {
    char file[MAX_PATH] = "", name[PATCH_NAME_MAX + 1], q[200];
    OPENFILENAMEA o = {0};
    o.lStructSize = sizeof o;
    o.hwndOwner = owner(e);
    o.lpstrFilter = "ONE-ish patches (*.oneish)\0*.oneish\0All files (*.*)\0*.*\0";
    o.lpstrFile = file;
    o.nMaxFile = sizeof file;
    o.lpstrTitle = "Import a ONE-ish patch";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&o)) return;
    if (patches_import(e, file, 0) != PE_EXISTS) return;
    name_from_path(file, name);
    snprintf(q, sizeof q, "A patch named '%s' already exists.\nReplace it with the imported one?", name);
    if (confirm(e, q)) patches_import(e, file, 1);
}

static void ui_export(ed_t *e) {
    char file[MAX_PATH];
    suggestion(e, file);
    strcat(file, EXT);
    OPENFILENAMEA o = {0};
    o.lStructSize = sizeof o;
    o.hwndOwner = owner(e);
    o.lpstrFilter = "ONE-ish patches (*.oneish)\0*.oneish\0";
    o.lpstrFile = file;
    o.nMaxFile = sizeof file;
    o.lpstrDefExt = "oneish";
    o.lpstrTitle = "Export the sound as a ONE-ish patch";
    o.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (GetSaveFileNameA(&o)) patches_export(e, file);
}

static void patches_action(ed_t *e, int a) {
    char q[200];
    switch (a) {
    case PA_SAVE:
        if (e->pt.bank == BANK_USER) patches_save(e);
        else ui_save_as(e);
        break;
    case PA_SAVE_AS: ui_save_as(e); break;
    case PA_RENAME: ui_rename(e); break;
    case PA_DELETE:
        if (!need_user(e, "DELETE")) break;
        snprintf(q, sizeof q, "Delete the patch '%s'?\nIts file is removed; the sound stays loaded until you pick "
                              "another.", e->pt.name);
        if (confirm(e, q)) patches_delete(e);
        break;
    case PA_IMPORT: ui_import(e); break;
    case PA_EXPORT: ui_export(e); break;
    }
    redraw(e);
}

/* ---- the window: the PATCHES page on the centre screen ---- */
void patches_open(ed_t *e) {
    patches_rescan(e);
    e->screen = SCR_PATCHES;
    e->sub = 0;
    follow(e);
    redraw(e);
}

void patches_toggle(ed_t *e) {   /* the preset name: the list, or back to the matrix */
    if (e->screen == SCR_PATCHES) {
        e->screen = SCR_MATRIX;
        e->sub = 0;
        redraw(e);
    } else {
        patches_open(e);
    }
}

int patches_subs(ed_t *e) {
    int a = (e->fx->numPrograms + PER - 1) / PER, b = (e->pt.count + PER - 1) / PER;
    return a > b ? (a > 1 ? a : 1) : (b > 1 ? b : 1);
}

static RECT action_rect(int a) {   /* the button row on the column-heads line */
    int gap = sc(4), bw = (rw(&L.head) - gap * (PA_COUNT - 1)) / PA_COUNT;
    RECT r = {L.head.left + a * (bw + gap), L.head.top, L.head.left + a * (bw + gap) + bw, L.head.bottom - sc(1)};
    return r;
}

static RECT entry_rect(int col, int row) {   /* column 0 factory, 1 user; row 0 the heading */
    RECT nm, c;
    slot_rects(col * ROWS + row, &nm, &c);
    RECT r = {nm.left, nm.top + sc(1), c.right, nm.bottom - sc(1)};
    return r;
}

static int entry_pos(ed_t *e, int col, int row) {   /* the list position shown at col/row of this sub-page, or -1 */
    int k = e->sub * PER + row - 1, nf = e->fx->numPrograms;
    if (row < 1 || k < 0) return -1;
    if (col == 0) return k < nf ? k : -1;
    return k < e->pt.count ? nf + k : -1;
}

int patches_hit(ed_t *e, POINT pt) {
    for (int a = 0; a < PA_COUNT; a++) {
        RECT r = action_rect(a);
        if (PtInRect(&r, pt)) return Z_PATCH_ACT + a;
    }
    for (int col = 0; col < 2; col++)
        for (int row = 1; row < ROWS; row++) {
            RECT r = entry_rect(col, row);
            int pos = entry_pos(e, col, row);
            if (pos >= 0 && PtInRect(&r, pt)) return Z_PATCH_ITEM + pos;
        }
    return -1;
}

int patches_press(ed_t *e, int z) {   /* 1: z was the page's */
    if (z >= Z_PATCH_ACT && z < Z_PATCH_ACT + PA_COUNT) { patches_action(e, z - Z_PATCH_ACT); return 1; }
    if (z >= Z_PATCH_ITEM) { patches_rescan(e); patches_load_pos(e, z - Z_PATCH_ITEM); redraw(e); return 1; }
    return 0;
}

#define P_LCD RGB(150, 215, 232)
#define P_DIM RGB(90, 130, 145)
#define P_HOT RGB(255, 255, 255)
#define P_FRAME RGB(0x27, 0x40, 0x4E)
#define P_FRAME_HOT RGB(0x96, 0xD7, 0xE8)
#define P_FILL_HOT RGB(0x16, 0x2C, 0x3A)
#define P_SEL RGB(0x1F, 0x46, 0x5A)

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

void patches_draw(HDC dc, ed_t *e) {   /* under the title line, which editor_draw.c draws */
    static const char *const ACT[PA_COUNT] = {"SAVE", "SAVE AS", "RENAME", "DELETE", "IMPORT", "EXPORT"};
    patches_t *p = &e->pt;
    for (int a = 0; a < PA_COUNT; a++) {
        RECT r = action_rect(a);
        int hot = e->hover == Z_PATCH_ACT + a, live = (a != PA_RENAME && a != PA_DELETE) || p->bank == BANK_USER;
        int lit = a == PA_SAVE && p->edited;   /* SAVE glows while the sound has changes to keep */
        if (hot) fill(dc, r, P_FILL_HOT);
        frame(dc, r, hot || lit ? P_FRAME_HOT : P_FRAME);
        label(dc, r, ACT[a], font_tiny, !live ? P_DIM : hot || lit ? P_HOT : P_LCD, DT_CENTER);
    }
    int pos_now = patches_pos(e), nf = e->fx->numPrograms;
    for (int col = 0; col < 2; col++) {
        char h[48];
        RECT r = entry_rect(col, 0);
        snprintf(h, sizeof h, col ? "USER  (%d)" : "FACTORY  (%d)", col ? p->count : nf);
        label(dc, r, h, font_tiny, P_DIM, DT_LEFT);
        RECT line = {r.left, r.bottom - 1, r.right, r.bottom};
        fill(dc, line, P_FRAME);
        for (int row = 1; row < ROWS; row++) {
            int pos = entry_pos(e, col, row);
            if (pos < 0) continue;
            RECT er = entry_rect(col, row), tr = {er.left + sc(6), er.top, er.right - sc(4), er.bottom};
            int cur = pos == pos_now, hot = e->hover == Z_PATCH_ITEM + pos;
            char t[80], nm[64] = "";
            if (pos < nf) {
                wrapped_call(e->fx, effGetProgramNameIndexed_, pos, 0, nm, 0);
                snprintf(t, sizeof t, "%03d   %s%s", pos + 1, nm, cur && p->edited ? " *" : "");
            } else {
                snprintf(t, sizeof t, "%s%s", p->list[pos - nf], cur && p->edited ? " *" : "");
            }
            if (cur) fill(dc, er, P_SEL);
            if (hot) frame(dc, er, P_FRAME_HOT);
            label(dc, tr, t, font_cell, hot || cur ? P_HOT : P_LCD, DT_LEFT);
        }
    }
    if (p->count == 0 && e->sub == 0) {
        RECT r1 = entry_rect(1, 1), r2 = entry_rect(1, 2);
        r1.left += sc(6);
        r2.left += sc(6);
        label(dc, r1, "No user patches yet.", font_cell, P_DIM, DT_LEFT);
        label(dc, r2, "SAVE AS keeps the sound here.", font_cell, P_DIM, DT_LEFT);
    }
}

void patches_help(ed_t *e, char *out, int n) {   /* the page's own help, when the mouse is on nothing */
    char dir[MAX_PATH];
    if (!patch_dir(dir, sizeof dir, 0)) snprintf(dir, sizeof dir, "(no Documents or AppData folder found)");
    snprintf(out, n, "Click a patch to load it. SAVE keeps your changes in the current user patch, SAVE AS under a "
                     "new name. Your patches are files in %s", dir);
    (void)e;
}

const char *patches_zone_help(int z) {
    switch (z - Z_PATCH_ACT) {
    case PA_SAVE: return "SAVE: stores the sound over the current user patch. A factory preset or an unsaved sound "
                         "asks for a name (factory presets are never changed).";
    case PA_SAVE_AS: return "SAVE AS: stores the sound as a user patch under a new name (asks before replacing one).";
    case PA_RENAME: return "RENAME: gives the current user patch a new name.";
    case PA_DELETE: return "DELETE: removes the current user patch's file (asks first). The sound stays loaded.";
    case PA_IMPORT: return "IMPORT: copies a .oneish patch file (from a friend, a backup) into your user patches.";
    case PA_EXPORT: return "EXPORT: writes the sound to a .oneish file wherever you like, to share or back up.";
    }
    if (z >= Z_PATCH_ITEM) return "Click to load this patch. The sound you have now is replaced: SAVE it first to keep "
                                  "your changes.";
    return NULL;
}

/* ---- load_test: the operations without dialogs (effVendorSpecific TEST_PATCH; buf holds 1 KB) ---- */
intptr_t patches_test(ed_t *e, intptr_t op, char *buf, float opt) {
    int ow = opt > 0.5f;
    switch (op) {
    case PT_SAVE: return patches_save(e);
    case PT_SAVE_AS: return patches_save_as(e, buf, ow);
    case PT_RENAME: return patches_rename(e, buf, ow);
    case PT_DELETE: return patches_delete(e);
    case PT_IMPORT: return patches_import(e, buf, ow);
    case PT_EXPORT: return patches_export(e, buf);
    case PT_LOAD: patches_rescan(e); return patches_load_user(e, buf);
    case PT_LOAD_POS: patches_rescan(e); return patches_load_pos(e, (int)lroundf(opt));
    case PT_STEP: patches_step(e, (int)lroundf(opt)); return patches_pos(e);
    case PT_POS: patches_rescan(e); return patches_pos(e);
    case PT_TITLE: e->pt.edited = patches_edited_now(e); patches_title(e, buf, 1024); return 1;
    case PT_EDITED: return patches_edited_now(e);
    case PT_COUNT: patches_rescan(e); return e->pt.count;
    case PT_DIR: return patch_dir(buf, 1024, 0);
    case PT_MESSAGE: snprintf(buf, 1024, "%s", e->pt.msg); return 1;
    }
    return 0;
}
