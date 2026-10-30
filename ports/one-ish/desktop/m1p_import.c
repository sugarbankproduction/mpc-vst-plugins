/* (2026-10-31) IMPORT .M1P and REPORT on the PATCHES page: Moog One preset files the owner has (downloads, packs,
 * their own exports; nothing of them ships with Moo Won) become user patches in the library in use. Each file is read
 * and converted (m1p.c), played once in a separate engine instance to keep its level safe, and saved as a .oneish
 * patch tagged GROUP "Imported" with the preset's own name, type, category and mood; DUPLICATE NAMES (SETTINGS >
 * LIBRARY) decides what a same-named patch does. The screen's message line gets a short summary, and "Import
 * report.txt" beside the patches the full list of what mapped and what didn't (REPORT opens it). Window thread only:
 * files and the separate engine are never touched from the audio thread. docs/PATCHES.md. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "library.h"
#include "parts.h"
#include "m1p.h"
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#define REPORT_FILE "Import report.txt"
enum { PICK_MAX = 65536, FILES_MAX = 2000 };

static char *g_pick;              /* load_test: the files the next IMPORT .M1P takes instead of asking ('|' between) */
static char g_opened[MAX_PATH];   /* load_test: what REPORT opened (with a pick set, nothing is really opened) */

static void say(ed_t *e, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pt_vsay(e, fmt, ap);
    va_end(ap);
}

int m1p_report_path(char *out, int n) {
    char d[MAX_PATH];
    return patch_dir(d, sizeof d, 0) && snprintf(out, n, "%s\\" REPORT_FILE, d) < n;
}

static char *read_all(const char *path, size_t *len) {   /* the file as it is (zero bytes kept), NUL after it */
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *b = malloc(M1P_TEXT_MAX + 2);
    size_t n = b ? fread(b, 1, M1P_TEXT_MAX + 1, f) : 0;
    fclose(f);
    if (!b) return NULL;
    b[n] = 0;
    *len = n;
    return b;
}

static int ends_with(const char *s, const char *x) {
    size_t a = strlen(s), b = strlen(x);
    return a >= b && !_stricmp(s + a - b, x);
}

/* a patch name from the preset's own name, else the file's: odd characters as '-', at most 32 */
static void name_for(const char *path, const m1p_t *m, char *out) {
    char t[MAX_PATH];
    if (m->name[0]) {
        snprintf(t, sizeof t, "%s", m->name);
    } else {
        const char *b = path;
        for (const char *c = path; *c; c++)
            if (*c == '\\' || *c == '/') b = c + 1;
        snprintf(t, sizeof t, "%s", b);
        if (ends_with(t, ".m1p")) t[strlen(t) - 4] = 0;
    }
    for (char *c = t; *c; c++)
        if ((unsigned char)*c < 32 || (unsigned char)*c > 126 || strchr("\\/:*?\"<>|;", *c)) *c = '-';
    t[PATCH_NAME_MAX] = 0;
    char *a = t;
    while (*a == ' ') a++;
    size_t l = strlen(a);
    while (l && (a[l - 1] == ' ' || a[l - 1] == '.')) a[--l] = 0;
    for (char *c = a; c[0]; c++)   /* "GNARZ 2   TEK": runs of spaces as one */
        while (c[0] == ' ' && c[1] == ' ') memmove(c, c + 1, strlen(c));
    snprintf(out, PATCH_NAME_MAX + 1, "%s", patch_name_ok(a, NULL, 0) ? a : "Imported preset");
}

typedef struct { int done, failed, skipped, mapped, unknown, guessed, trimmed, silent; } tally_t;

/* the patch file under `name`, as DUPLICATE NAMES says; 1 written (final: the name used), 0 failed, -1 left alone */
static int save_as(ed_t *e, const char *name, int dup, const char *meta, const char *state, char *final, char *why,
                   int wn) {
    char path[MAX_PATH], to[PATCH_NAME_MAX + 1];
    snprintf(to, sizeof to, "%s", name);
    if (!pt_patch_path(to, path, sizeof path, 1)) { snprintf(why, wn, "the patch folder can't be made"); return 0; }
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        if (dup == DUP_IGNORE) { snprintf(why, wn, "a patch with that name is there (DUPLICATE NAMES: IGNORE)"); return -1; }
        if (dup == DUP_RENAME) {
            if (!patches_free_name(name, to) || !pt_patch_path(to, path, sizeof path, 1)) {
                snprintf(why, wn, "no free name like it was found");
                return 0;
            }
        } else if (dup == DUP_ASK) {
            char q[200];
            snprintf(q, sizeof q, "A patch named '%s' already exists.\nReplace it with the imported preset?", name);
            if (!pt_confirm(e, q)) { snprintf(why, wn, "a patch with that name is there (kept)"); return -1; }
        }
    }
    char *text = pt_build(to, meta, state);
    int ok = text && pt_write(path, text);
    if (!ok) snprintf(why, wn, "%s", pt_why(text ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY));
    free(text);
    snprintf(final, PATCH_NAME_MAX + 1, "%s", to);
    return ok;
}

/* one file: converted, checked, saved; its part of the report appended to rep */
static void import_one(ed_t *e, const char *path, int dup, tally_t *t, char **rep, size_t *rl) {
    char line[MAX_PATH + 300];
    size_t len = 0;
    char *text = read_all(path, &len);
    m1p_t m;
    int st = text ? m1p_convert(text, len, &m) : M1P_EMPTY - 100;
    free(text);
#define ADD(...) do { snprintf(line, sizeof line, __VA_ARGS__); size_t a = strlen(line); char *q = realloc(*rep, *rl + a + 1); \
                      if (q) { *rep = q; memcpy(*rep + *rl, line, a + 1); *rl += a; } } while (0)
    ADD("\r\n== %s ==\r\n", path);
    if (!text) { t->failed++; ADD("Not imported: the file could not be opened.\r\n"); return; }
    if (st != M1P_OK) {
        t->failed++;
        ADD("Not imported: %s.\r\n", m1p_status_text(st));
        m1p_free(&m);
        return;
    }
    char *state = malloc(PATCH_STATE_MAX);
    m1p_level_t lv;
    if (!state || !m1p_make_state(m.assign, state, PATCH_STATE_MAX, &lv)) {
        t->failed++;
        ADD("Not imported: the sound could not be made (the engine's state didn't fit).\r\n");
        free(state);
        m1p_free(&m);
        return;
    }
    meta_t mt;
    memset(&mt, 0, sizeof mt);
    snprintf(mt.tag[TAG_TYPE], sizeof mt.tag[0], "%s", m.type[0] ? m.type : "Single");
    snprintf(mt.tag[TAG_CAT], sizeof mt.tag[0], "%s", m.category);
    snprintf(mt.tag[TAG_MOOD], sizeof mt.tag[0], "%s", m.mood);
    snprintf(mt.tag[TAG_GROUP], sizeof mt.tag[0], "Imported");
    char lvl[160];
    if (!lv.finite) snprintf(lvl, sizeof lvl, "Level check: the sound made invalid samples; play it carefully.");
    else if (lv.silent) snprintf(lvl, sizeof lvl, "Level check: a chord played silent (it may need settings that "
                                                  "didn't import, or a sequence).");
    else if (lv.trim_db > 0) snprintf(lvl, sizeof lvl, "Level check: too loud, so Volume was turned down %.1f dB (peak "
                                                       "now %.1f dBFS).", lv.trim_db, lv.peak_db);
    else snprintf(lvl, sizeof lvl, "Level check: a chord peaks at %.1f dBFS, fine.", lv.peak_db);
    snprintf(mt.notes, sizeof mt.notes, "%s %d of its settings mapped (%d of them our best reading), %d not. %s",
             m.notes, m.mapped, m.guessed, m.unknown, lvl);
    char meta[2 * NOTES_MAX + 256], name[PATCH_NAME_MAX + 1], final[PATCH_NAME_MAX + 1] = "", why[160] = "";
    lib_meta_lines(&mt, meta, sizeof meta);
    name_for(path, &m, name);
    int r = save_as(e, name, dup, meta, state, final, why, sizeof why);
    free(state);
    if (r > 0) {
        t->done++;
        t->mapped += m.mapped;
        t->guessed += m.guessed;
        t->unknown += m.unknown;
        t->trimmed += lv.trim_db > 0;
        t->silent += lv.silent;
        ADD("Imported as the patch '%s'. %s\r\n", final, lvl);
    } else {
        if (r < 0) t->skipped++; else t->failed++;
        ADD("Not imported as '%s': %s.\r\n", name, why);
    }
    /* the converter's own lines use \n; the report file is read in Notepad, so \r\n */
    for (const char *s = m.report; *s;) {
        const char *nl = strchr(s, '\n');
        int n = nl ? (int)(nl - s) : (int)strlen(s);
        ADD("%.*s\r\n", n > (int)sizeof line - 3 ? (int)sizeof line - 3 : n, s);
        s += n + (nl ? 1 : 0);
    }
#undef ADD
    m1p_free(&m);
}

/* every .m1p in a folder and the folders inside it (A to Z as Windows lists them), into files */
static void walk(const char *dir, char (*files)[MAX_PATH], int *n, int depth) {
    char pat[MAX_PATH + 4];
    if (depth > 8 || snprintf(pat, sizeof pat, "%s\\*", dir) >= (int)sizeof pat) return;
    WIN32_FIND_DATAA f;
    HANDLE h = FindFirstFileA(pat, &f);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char p[MAX_PATH];
        if (f.cFileName[0] == '.' || snprintf(p, sizeof p, "%s\\%s", dir, f.cFileName) >= (int)sizeof p) continue;
        if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) walk(p, files, n, depth + 1);
        else if (ends_with(f.cFileName, ".m1p") && *n < FILES_MAX) snprintf(files[(*n)++], MAX_PATH, "%s", p);
    } while (FindNextFileA(h, &f));
    FindClose(h);
}

/* the files (or folders: every .m1p in them) imported; the report written; the summary said. The number imported. */
int m1p_import_list(ed_t *e, const char *const *paths, int np, int dup) {
    char (*files)[MAX_PATH] = calloc(FILES_MAX, MAX_PATH);
    if (!files) return 0;
    int n = 0;
    for (int k = 0; k < np; k++) {
        DWORD a = GetFileAttributesA(paths[k]);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) walk(paths[k], files, &n, 0);
        else if (n < FILES_MAX) snprintf(files[n++], MAX_PATH, "%s", paths[k]);
    }
    tally_t t = {0};
    char *rep = NULL, stamp[64];
    size_t rl = 0;
    time_t now = time(NULL);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", localtime(&now));
    char head[512];
    snprintf(head, sizeof head, "Moo Won: import of Moog One presets, %s\r\n"
             "Each file's settings that Moo Won knows were converted; the rest are listed under it. \"A guess: check by "
             "ear\" marks a setting whose meaning or scale was worked out from preset files rather than documented.\r\n",
             stamp);
    rep = strdup(head);
    rl = rep ? strlen(rep) : 0;
    if (e->lib) e->lib->busy++;   /* (the library's timer leaves the folder alone meanwhile) */
    for (int k = 0; k < n; k++) import_one(e, files[k], dup, &t, &rep, &rl);
    if (e->lib) e->lib->busy--;
    free(files);
    char rpath[MAX_PATH];
    int wrote = rep && m1p_report_path(rpath, sizeof rpath) && pt_write(rpath, rep);
    free(rep);
    patches_rescan(e);
    if (!n) say(e, "No Moog One preset files (.m1p) were found there.");
    else
        say(e, "Imported %d of %d Moog One presets as user patches (GROUP Imported)%s%s. %d settings mapped, %d not "
               "(yet)%s. %s", t.done, n, t.skipped ? ", some names kept" : "", t.trimmed ? ", loud ones turned down" : "",
            t.mapped, t.unknown, t.silent ? ", some silent in the level check" : "",
            wrote ? "REPORT opens the full list." : "The report could not be written.");
    return t.done;
}

/* the folder the dialog opens in: Documents\Moog One presets when it is there */
static void start_dir(char *out, int n) {
    char d[MAX_PATH];
    out[0] = 0;
    if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, d) != S_OK) return;
    snprintf(out, n, "%s\\Moog One presets", d);
    DWORD a = GetFileAttributesA(out);
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) out[0] = 0;
}

/* IMPORT .M1P: a file dialog for one or many presets (Ctrl+A picks all in a folder), then the import */
void m1p_ui_import(ed_t *e) {
    enum { MAXP = 512 };
    const char *paths[MAXP];
    int np = 0;
    char *buf = NULL;
    if (g_pick) {   /* load_test: the files it set, no dialog */
        buf = strdup(g_pick);
        for (char *s = buf, *save = NULL, *p = buf ? strtok_r(s, "|", &save) : NULL; p && np < MAXP; p = strtok_r(NULL, "|", &save))
            paths[np++] = p;
    } else {
        char dir[MAX_PATH];
        buf = calloc(1, PICK_MAX);
        if (!buf) return;
        start_dir(dir, sizeof dir);
        OPENFILENAMEA o = {0};
        o.lStructSize = sizeof o;
        o.hwndOwner = pt_owner(e);
        o.lpstrFilter = "Moog One presets (*.m1p)\0*.m1p\0All files (*.*)\0*.*\0";
        o.lpstrFile = buf;
        o.nMaxFile = PICK_MAX;
        o.lpstrInitialDir = dir[0] ? dir : NULL;
        o.lpstrTitle = "Import Moog One presets (pick one or many: Ctrl+A picks them all)";
        o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT |
                  OFN_EXPLORER;
        if (e->lib) e->lib->busy++;
        int ok = GetOpenFileNameA(&o);
        if (e->lib) e->lib->busy--;
        if (!ok) { free(buf); return; }
        /* one file: its full path; several: the folder, then each name, a zero after each, two at the end */
        static char full[MAXP][MAX_PATH];
        const char *first = buf, *next = buf + strlen(buf) + 1;
        if (!*next) paths[np++] = first;
        for (; *next && np < MAXP; next += strlen(next) + 1) {
            snprintf(full[np], MAX_PATH, "%s\\%s", first, next);
            paths[np] = full[np];
            np++;
        }
    }
    m1p_import_list(e, paths, np, settings_import_dup());
    free(buf);
}

/* REPORT: the last import's report in the text editor Windows uses */
void m1p_ui_report(ed_t *e) {
    char p[MAX_PATH];
    if (!m1p_report_path(p, sizeof p) || GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES) {
        say(e, "There is no import report yet: IMPORT .M1P makes one.");
        return;
    }
    snprintf(g_opened, sizeof g_opened, "%s", p);
    if (g_pick) return;   /* load_test: nothing really opened */
    if ((INT_PTR)ShellExecuteA(e->wnd, "open", p, NULL, NULL, SW_SHOWNORMAL) <= 32) say(e, "Couldn't open %s", p);
}

int m1p_have_report(void) {
    char p[MAX_PATH];
    return m1p_report_path(p, sizeof p) && GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

/* load_test (patches.h PT_M1P_*): buf a 1 KB text buffer */
intptr_t m1p_test(ed_t *e, intptr_t op, char *buf, float opt) {
    switch (op) {
    case PT_M1P_PICK:
        free(g_pick);
        g_pick = buf[0] ? strdup(buf) : NULL;
        g_opened[0] = 0;
        return 1;
    case PT_M1P_IMPORT: {   /* buf: a file or folder; opt: DUPLICATE NAMES' choice (DUP_) */
        const char *p = buf;
        return m1p_import_list(e, &p, 1, (int)opt);
    }
    case PT_M1P_REPORT: return m1p_report_path(buf, 1024) && GetFileAttributesA(buf) != INVALID_FILE_ATTRIBUTES;
    case PT_M1P_OPENED: snprintf(buf, 1024, "%s", g_opened); return g_opened[0] != 0;
    }
    return 0;
}
