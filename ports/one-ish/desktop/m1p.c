/* (2026-10-31) Moog One preset files (.m1p) into Moo Won sounds: the reader and the converter (m1p.h). The file is
 * plain text, one setting a line, "key value": the key a path like /p/part/t/1/p/o1/oct, a space, then a number,
 * true / false, a "quoted" word or a [list] (a value may end in " #raw", which is dropped). Each setting the tables in
 * m1p_map.h know becomes one of ours in the engine's own units; the matrix rows are rebuilt in our matrix; everything
 * else is listed for the report, never guessed. Window thread only (and the offline checks). */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "params.h"
#include "engine.h"
#include "m1p.h"
#include "m1p_map.h"

void one_ish_render_float(void *inst, float *out_lr, int frames);   /* src/synths.h */

enum { MM_ROWS = 16, MM_MAX = 64, NSYN_ = 3 };

/* ---- small helpers ---- */
static void grow(char **buf, size_t *len, size_t *cap, const char *fmt, ...) {   /* append, printf style */
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) return;
    if (*len + need + 1 > *cap) {
        size_t c = *cap ? *cap : 1024;
        while (*len + need + 1 > c) c *= 2;
        char *q = realloc(*buf, c);
        if (!q) return;   /* out of memory: the line is left out */
        *buf = q;
        *cap = c;
    }
    va_start(ap, fmt);
    vsnprintf(*buf + *len, *cap - *len, fmt, ap);
    va_end(ap);
    *len += need;
}

static int same_ci(const char *a, const char *b) {   /* the same, upper and lower case alike */
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static int param_index(const char *key) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, key)) return i;
    return -1;
}

static int option_of(int i, const char *label) {
    for (int k = 0; k < PARAMS[i].nopts; k++)
        if (same_ci(PARAMS[i].opts[k], label)) return k;
    return -1;
}

/* does key match the table's pattern? ('#' the Synth 1-3, '@' a number 1-4) */
static int match(const char *pat, const char *key, int *syn, int *num) {
    *syn = *num = 0;
    for (; *pat; pat++, key++) {
        if (*pat == '#' || *pat == '@') {
            if (*key < '1' || *key > (*pat == '#' ? '3' : '4')) return 0;
            *(*pat == '#' ? syn : num) = *key - '0';
        } else if (*pat != *key) {
            return 0;
        }
    }
    return *key == 0;
}

static void fill_num(const char *pat, int num, char *out, int n) {   /* '@' replaced by num */
    int k = 0;
    for (const char *p = pat; *p && k < n - 1; p++) out[k++] = *p == '@' ? (char)('0' + num) : *p;
    out[k] = 0;
}

/* our key for Synth syn (1-3; 0 a key outside the Synths: Synth 1's): the prefix added. "" when Moo Won shares the
 * control between the Synths and syn is 2 or 3 */
static void our_key(const char *base, int syn, char *out, int n) {
    if (syn <= 1) { snprintf(out, n, "%s", base); return; }
    snprintf(out, n, "s%d_%s", syn, base);
    if (param_index(out) < 0) out[0] = 0;
}

static void set_raw(m1p_t *m, const char *key, const char *val) {
    grow(&m->assign, &m->alen, &m->acap, "%s%s=%s", m->alen ? ";" : "", key, val);
}

static int parse_bool(const char *v, int *b) {
    static const char *const Y[] = {"true", "1", "on", "yes"}, *const N[] = {"false", "0", "off", "no"};
    for (int k = 0; k < 4; k++) {
        if (same_ci(v, Y[k])) { *b = 1; return 1; }
        if (same_ci(v, N[k])) { *b = 0; return 1; }
    }
    return 0;
}
static int parse_num(const char *v, double *d) {
    char *end;
    *d = strtod(v, &end);
    return end != v && !*end && isfinite(*d);
}

/* a number onto our param i (clamped), assigned; its text for the report into show */
static void put_num(m1p_t *m, const char *key, int i, double d, char *show, int sn) {
    const param_t *p = &PARAMS[i];
    char v[32];
    d = d < p->min ? p->min : d > p->max ? p->max : d;
    snprintf(v, sizeof v, "%.4g", d);
    set_raw(m, key, v);
    snprintf(show, sn, "%.4g%s%s", d, p->unit[0] ? " " : "", p->unit);
}
static void put_opt(m1p_t *m, const char *key, int i, int opt, char *show, int sn) {
    char v[16];
    snprintf(v, sizeof v, "%d", opt);
    set_raw(m, key, v);
    snprintf(show, sn, "%s", PARAMS[i].opts[opt]);
}

/* seconds onto a time knob: 1 ms .. 10 s on a log scale (src/engine.c env_ms, glide_ms); 0 stays 0 */
static double sec_knob(double s) { return s * 1000 <= 1 ? 0 : 25 * log10(s * 1000); }
/* a MIDI note number as a cutoff onto our cutoff knob: 20 Hz .. 20 kHz on a log scale (engine.c cut_hz) */
static double cut_knob(double note) { return 100.0 / 3 * log10(440 * pow(2, (note - 69) / 12) / 20); }

/* one row's value turned into ours and assigned. 1: done (show says what); 0: a value that makes no sense; -1: the
 * row names a key Moo Won doesn't have; -2: a shared control of Synth 2 or 3 */
static int convert(m1p_t *m, const m1p_row_t *r, int syn, int num, const char *v, char *show, int sn) {
    char base[64], key[64];
    double d;
    int b, opt = -1;
    show[0] = 0;
    if (r->conv == M_COVER) return 1;
    fill_num(r->ours, num, base, sizeof base);
    our_key(base, syn, key, sizeof key);
    if (!key[0]) return -2;
    int i = param_index(key);
    if (i < 0) return -1;
    const param_t *p = &PARAMS[i];
    switch (r->conv) {
    case M_UNIT: case M_NUM: case M_MUL: case M_SEC: case M_CUT:
        if (!parse_num(v, &d) || p->nopts) return 0;
        if (r->conv == M_UNIT) d = p->min + (d < 0 ? 0 : d > 1 ? 1 : d) * (p->max - p->min);
        else if (r->conv == M_MUL) d *= atof(r->opts);
        else if (r->conv == M_SEC) d = sec_knob(d);
        else if (r->conv == M_CUT) d = cut_knob(d);
        put_num(m, key, i, d, show, sn);
        if (r->conv == M_SEC) snprintf(show + strlen(show), sn - strlen(show), " (%.3g s)", atof(v));
        if (r->conv == M_CUT) snprintf(show + strlen(show), sn - strlen(show), " (%.0f Hz)", 440 * pow(2, (atof(v) - 69) / 12));
        return 1;
    case M_LFOHZ: {   /* our RANGE that holds the rate, then the knob in it: base x 1000^(knob/100) */
        static const double BASE[] = {0.001, 0.01, 0.1, 1.0};
        char rk[64], rkey[64];
        if (!parse_num(v, &d) || d <= 0) return 0;
        int rg = d < 0.01 ? 0 : d <= 10 ? 1 : d <= 100 ? 2 : 3;
        snprintf(rk, sizeof rk, "%s", key);
        char *u = strstr(rk, "_rate");
        if (!u) return -1;
        strcpy(u, "_range");
        our_key(rk + (syn > 1 ? 3 : 0), syn, rkey, sizeof rkey);
        int ri = param_index(rkey);
        if (ri < 0 || rg >= PARAMS[ri].nopts) return -1;
        char s2[24];
        put_opt(m, rkey, ri, rg, s2, sizeof s2);
        put_num(m, key, i, 100.0 / 3 * log10(d / BASE[rg]), show, sn);
        snprintf(show + strlen(show), sn - strlen(show), " (%.3g Hz, range %s)", d, s2);
        return 1;
    }
    case M_OCT:
        if (!parse_num(v, &d) || p->nopts < 1) return 0;
        opt = (int)lround(d) + p->nopts / 2;
        opt = opt < 0 ? 0 : opt >= p->nopts ? p->nopts - 1 : opt;
        break;
    case M_TAP:   /* 4 taps (poles) = 24 dB, the first of our list; 1 = 6 dB, the last */
        if (!parse_num(v, &d) || p->nopts < 1) return 0;
        opt = 4 - (int)lround(d);
        opt = opt < 0 ? 0 : opt >= p->nopts ? p->nopts - 1 : opt;
        break;
    case M_BOOL:
        if (!parse_bool(v, &b) || p->nopts != 2) return 0;
        opt = b;
        break;
    case M_LABEL: {   /* the option whose label is that number (Mono counts as 1), the nearest when none is */
        if (!parse_num(v, &d) || !p->nopts) return 0;
        if (d <= 0) return 1;   /* "as many as there are": left as it is */
        double best = 1e9;
        for (int k = 0; k < p->nopts; k++) {
            double l = same_ci(p->opts[k], "Mono") ? 1 : atof(p->opts[k]);
            if (fabs(l - d) < best) { best = fabs(l - d); opt = k; }
        }
        break;
    }
    case M_FLAG: {   /* true: ours to the label before '|', and each "key=Label" after it */
        if (!parse_bool(v, &b)) return 0;
        if (!b) return 1;
        char lab[48];
        const char *bar = strchr(r->opts, '|');
        snprintf(lab, sizeof lab, "%.*s", bar ? (int)(bar - r->opts) : (int)strlen(r->opts), r->opts);
        if ((opt = option_of(i, lab)) < 0) return -1;
        put_opt(m, key, i, opt, show, sn);
        if (bar) {
            char ek[64], ekey[64];
            const char *eq = strchr(bar + 1, '=');
            if (!eq) return -1;
            snprintf(ek, sizeof ek, "%.*s", (int)(eq - bar - 1), bar + 1);
            fill_num(ek, num, base, sizeof base);
            our_key(base, syn, ekey, sizeof ekey);
            int ei = param_index(ekey), eo = ei >= 0 ? option_of(ei, eq + 1) : -1;
            if (eo < 0) return -1;
            char s2[32];
            put_opt(m, ekey, ei, eo, s2, sizeof s2);
            snprintf(show + strlen(show), sn - strlen(show), ", %s = %s", PARAMS[ei].name, s2);
        }
        return 1;
    }
    case M_ENUM:
        for (const char *s = r->opts; s && *s;) {   /* "code=Label|code=Label" */
            const char *eq = strchr(s, '='), *bar = strchr(s, '|');
            if (!eq) break;
            size_t cl = (size_t)(eq - s), ll = bar ? (size_t)(bar - eq - 1) : strlen(eq + 1);
            if (strlen(v) == cl && !strncmp(v, s, cl)) {
                char lab[48];
                snprintf(lab, sizeof lab, "%.*s", (int)ll, eq + 1);
                if (lab[0] == '!') {   /* "!key": that key Off instead */
                    char k2[64];
                    our_key(lab + 1, syn, k2, sizeof k2);
                    int i2 = k2[0] ? param_index(k2) : -1;
                    if (i2 < 0) return -1;
                    put_opt(m, k2, i2, 0, show, sn);
                    snprintf(show + strlen(show), sn - strlen(show), " (%s)", PARAMS[i2].name);
                    return 1;
                }
                if ((opt = option_of(i, lab)) < 0) return -1;
                break;
            }
            s = bar ? bar + 1 : "";
        }
        if (opt < 0) return 0;
        break;
    default: return 0;
    }
    put_opt(m, key, i, opt, show, sn);
    return 1;
}

/* ---- the file ---- */
int m1p_looks_like(const char *text) {
    const char *t = !strncmp(text, "\xEF\xBB\xBF", 3) ? text + 3 : text;
    for (;;) {   /* past blank lines and '#' comments (our synthetic test files have one) */
        while (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n') t++;
        if (*t != '#') break;
        while (*t && *t != '\n') t++;
    }
    if (*t != '/') return 0;
    while (*t && *t != ' ' && *t != '\t' && *t != '\n' && *t != '\r') t++;
    return *t == ' ' || *t == '\t';
}

const char *m1p_status_text(int s) {
    switch (s) {
    case M1P_OK: return "converted";
    case M1P_EMPTY: return "the file is empty";
    case M1P_NOT_M1P: return "it isn't a Moog One preset (no \"/key value\" lines in it)";
    case M1P_BINARY: return "it isn't a text file (a Moog One preset is plain text)";
    case M1P_TOO_BIG: return "the file is too big to be a preset";
    case M1P_NOTHING_MAPPED: return "none of its settings is one Moo Won knows yet";
    }
    return "it could not be read";
}

void m1p_free(m1p_t *m) {
    free(m->assign);
    free(m->report);
    memset(m, 0, sizeof *m);
}

/* the part of the instrument a key is in, for the report's summary: "/p/part/t/1/p/seq/..." -> "Synth 1: seq" */
static void module_of(const char *key, char *out, int n) {
    int syn = 0;
    const char *rest = key;
    if (!strncmp(key, "/p/part/t/", 10) && key[10] >= '1' && key[10] <= '3' && key[11] == '/') {
        syn = key[10] - '0';
        rest = key + 12;
        if (!strncmp(rest, "p/", 2) || !strncmp(rest, "a/", 2)) rest += 2;
    } else if (!strncmp(key, "/p/part/", 8)) {
        rest = key + 8;
    } else if (!strncmp(key, "/p/", 3)) {
        rest = key + 3;
    }
    const char *end = strchr(rest, '/');
    int len = end ? (int)(end - rest) : (int)strlen(rest);
    if (syn) snprintf(out, n, "Synth %d: %.*s", syn, len, rest);
    else snprintf(out, n, "%.*s", len, rest);
}

static int text_ok(const char *text, size_t len) {
    size_t odd = 0;
    if (len > M1P_TEXT_MAX) return M1P_TOO_BIG;
    for (size_t k = 0; k < len; k++) {
        unsigned char c = (unsigned char)text[k];
        if (!c) return M1P_BINARY;
        if (c < 32 && c != '\t' && c != '\r' && c != '\n') odd++;
    }
    if (len && odd * 100 > len) return M1P_BINARY;
    for (size_t k = 0; k < len; k++)
        if (!isspace((unsigned char)text[k])) return M1P_OK;
    return M1P_EMPTY;
}

/* a label from the file in plain characters ("BRASS" as "Brass" when title is set), at most n - 1 of them */
static void plain(const char *v, char *out, int n, int title) {
    int k = 0, start = 1;
    for (; *v && k < n - 1; v++) {
        unsigned char c = (unsigned char)*v;
        if (c < 32 || c > 126 || strchr("\\;=|", c)) c = ' ';
        out[k++] = (char)(title ? (start ? toupper(c) : tolower(c)) : c);
        start = c == ' ' || c == '/' || c == '-';
    }
    out[k] = 0;
    while (k && out[k - 1] == ' ') out[--k] = 0;
}

/* ---- the matrix rows of a file ---- */
typedef struct { int used, mute; char src[24], ctl[24], tr[24], dst[96]; double src_a, ctl_a, tr_a; } mrow_t;

static const char *pair_of(const m1p_pair_t *t, int n, const char *k) {
    for (int i = 0; i < n; i++)
        if (!strcmp(t[i].key, k)) return t[i].label;
    return NULL;
}
static int none(const char *s) { return !s[0] || !strcmp(s, "-"); }

/* a destination key of the file -> our matrix destination's label (into out); 0: none */
static int dst_label(const char *dst, char *out, int n) {
    int syn, num;
    for (int k = 0; k < M1P_N(M1P_DST_ONLY); k++)
        if (match(M1P_DST_ONLY[k].key, dst, &syn, &num)) { snprintf(out, n, "%s", M1P_DST_ONLY[k].label); return 1; }
    for (int r = 0; r < M1P_ROWS; r++) {
        if (!M1P_MAP[r].ours || !match(M1P_MAP[r].m1, dst, &syn, &num)) continue;
        for (int k = 0; k < M1P_N(M1P_DST); k++) {
            char pk[48];
            fill_num(M1P_DST[k].key, num, pk, sizeof pk);
            char ours[48];
            fill_num(M1P_MAP[r].ours, num, ours, sizeof ours);
            if (!strcmp(pk, ours)) { fill_num(M1P_DST[k].label, num, out, n); return 1; }
        }
        return 0;
    }
    return 0;
}

static void opt_set(m1p_t *m, const char *key, const char *label) {   /* an option by its label (it must exist) */
    int i = param_index(key), o = i >= 0 ? option_of(i, label) : -1;
    char v[16];
    snprintf(v, sizeof v, "%d", o < 0 ? 0 : o);
    if (i >= 0) set_raw(m, key, v);
}
static void num_set(m1p_t *m, const char *key, double d) {
    int i = param_index(key);
    if (i < 0) return;
    char v[32];
    d = d < PARAMS[i].min ? PARAMS[i].min : d > PARAMS[i].max ? PARAMS[i].max : d;
    snprintf(v, sizeof v, "%.4g", d);
    set_raw(m, key, v);
}

/* Synth syn's rows into our matrix: all 16 emptied first (the file's matrix replaces ours), then each row the file
 * uses, in order */
static void matrix_rows(m1p_t *m, int syn, const mrow_t *rows, int n, char **rep, size_t *rl, size_t *rc,
                        char **unk, size_t *ul, size_t *uc) {
    char pre[4] = "";
    if (syn > 1) snprintf(pre, sizeof pre, "s%d_", syn);
    for (int r = 1; r <= MM_ROWS; r++) {
        char k[32];
        snprintf(k, sizeof k, "%smod%d_src", pre, r); opt_set(m, k, "Off");
        snprintf(k, sizeof k, "%smod%d_dst", pre, r); opt_set(m, k, "Off");
        snprintf(k, sizeof k, "%smod%d_amt", pre, r); num_set(m, k, 0);
        snprintf(k, sizeof k, "%smod%d_via", pre, r); opt_set(m, k, "None");
        snprintf(k, sizeof k, "%smod%d_via_amt", pre, r); num_set(m, k, 100);
        snprintf(k, sizeof k, "%smod%d_xf", pre, r); opt_set(m, k, "None");
        snprintf(k, sizeof k, "%smod%d_xf_amt", pre, r); num_set(m, k, 0);
        snprintf(k, sizeof k, "%smod%d_on", pre, r); opt_set(m, k, "On");
    }
    int ours = 0;
    for (int e = 0; e < n; e++) {
        const mrow_t *w = &rows[e];
        if (!w->used || (none(w->src) && none(w->ctl)) || !w->dst[0]) continue;   /* an empty row */
        const char *src = none(w->src) ? w->ctl : w->src, *via = none(w->src) || none(w->ctl) ? NULL : w->ctl;
        const char *s = pair_of(M1P_SRC, M1P_N(M1P_SRC), src), *v = via ? pair_of(M1P_SRC, M1P_N(M1P_SRC), via) : NULL;
        const char *x = none(w->tr) ? NULL : pair_of(M1P_XFORM, M1P_N(M1P_XFORM), w->tr);
        char dl[48];
        const char *why = !s ? "its source has no equivalent" : via && !v ? "its controller has no equivalent"
                        : !dst_label(w->dst, dl, sizeof dl) ? "its destination has no equivalent"
                        : ours >= MM_ROWS ? "our 16 rows are full" : NULL;
        if (why) {
            m->unknown++;
            grow(unk, ul, uc, "  matrix row %d (Synth %d): %s > %s%s%s: %s\n", e + 1, syn, src, w->dst,
                 via ? " via " : "", via ? via : "", why);
            continue;
        }
        double amt = none(w->src) ? w->ctl_a * w->src_a : w->src_a;
        ours++;
        char k[32];
        snprintf(k, sizeof k, "%smod%d_src", pre, ours); opt_set(m, k, s);
        snprintf(k, sizeof k, "%smod%d_dst", pre, ours); opt_set(m, k, dl);
        snprintf(k, sizeof k, "%smod%d_amt", pre, ours); num_set(m, k, amt * 100);
        if (v) {
            snprintf(k, sizeof k, "%smod%d_via", pre, ours); opt_set(m, k, v);
            snprintf(k, sizeof k, "%smod%d_via_amt", pre, ours); num_set(m, k, w->ctl_a * 100);
        }
        if (x) {
            snprintf(k, sizeof k, "%smod%d_xf", pre, ours); opt_set(m, k, x);
            snprintf(k, sizeof k, "%smod%d_xf_amt", pre, ours); num_set(m, k, w->tr_a * 100);
        }
        if (w->mute) { snprintf(k, sizeof k, "%smod%d_on", pre, ours); opt_set(m, k, "Off"); }
        m->matrix++;
        m->mapped++;
        m->guessed++;
        grow(rep, rl, rc, "  matrix row %d (Synth %d): %s > %s%s%s  ->  row %d: %s > %s %+.0f%%%s%s%s%s   (a guess: "
             "check by ear)\n", e + 1, syn, src, w->dst, via ? " via " : "", via ? via : "", ours, s, dl, amt * 100,
             v ? " via " : "", v ? v : "", x ? ", " : "", x ? x : "", w->mute ? ", off" : "");
    }
}

/* a matrix line ("/p/part/t/S/p/modmatrix/entry/N/field value") into rows; 1: it was one */
static int matrix_line(const char *key, const char *val, mrow_t (*mm)[MM_MAX]) {
    if (strncmp(key, "/p/part/t/", 10) || key[10] < '1' || key[10] > '3' || strncmp(key + 11, "/p/modmatrix/", 13))
        return 0;
    int syn = key[10] - '1';
    const char *rest = key + 24;
    if (strncmp(rest, "entry/", 6)) return 1;   /* its name, comment, expressions: nothing of ours */
    char *end;
    long e = strtol(rest + 6, &end, 10);
    if (end == rest + 6 || *end != '/' || e < 0 || e >= MM_MAX) return 1;
    mrow_t *w = &mm[syn][e];
    const char *f = end + 1;
    w->used = 1;
    if (!strcmp(f, "src")) snprintf(w->src, sizeof w->src, "%s", val);
    else if (!strcmp(f, "ctl")) snprintf(w->ctl, sizeof w->ctl, "%s", val);
    else if (!strcmp(f, "tr")) snprintf(w->tr, sizeof w->tr, "%s", val);
    else if (!strcmp(f, "dst")) snprintf(w->dst, sizeof w->dst, "%s", val);
    else if (!strcmp(f, "src_a")) w->src_a = atof(val);
    else if (!strcmp(f, "ctl_a")) w->ctl_a = atof(val);
    else if (!strcmp(f, "tr_a")) w->tr_a = atof(val);
    else if (!strcmp(f, "mute")) w->mute = same_ci(val, "true");
    return 1;
}

/* the preset's labels: "/meta/name" and the rest; 1: the key was one */
static int meta_line(m1p_t *m, const char *key, const char *val, char *note_extra, int nn) {
    if (strncmp(key, "/meta/", 6)) return 0;
    const char *f = key + 6;
    if (!strcmp(f, "name")) plain(val, m->name, sizeof m->name, 0);
    else if (!strcmp(f, "category")) plain(val, m->category, sizeof m->category, 1);
    else if (!strcmp(f, "type")) plain(val, m->type, sizeof m->type, 1);
    else if (!strcmp(f, "mood")) plain(val, m->mood, sizeof m->mood, 1);
    else if (!strcmp(f, "comment") && val[0]) {
        char c[300];
        plain(val, c, sizeof c, 0);
        snprintf(note_extra + strlen(note_extra), nn - strlen(note_extra), " Its comment: %s.", c);
    } else if (!strcmp(f, "group") && val[0]) {
        char c[64];
        plain(val, c, sizeof c, 0);
        snprintf(note_extra + strlen(note_extra), nn - strlen(note_extra), " Its group: %s.", c);
    } else if (!strncmp(f, "synth", 5) && f[5] >= '1' && f[5] <= '3' && !strcmp(f + 6, "name") && val[0]) {
        char c[16], k[16];
        plain(val, c, 13, 0);
        snprintf(k, sizeof k, f[5] == '1' ? "syn_name" : "s%c_syn_name", f[5]);
        if (c[0]) set_raw(m, k, c);
    }
    return 1;
}

int m1p_convert(const char *text, size_t len, m1p_t *m) {
    memset(m, 0, sizeof *m);
    m->status = text_ok(text, len);
    if (m->status != M1P_OK) return m->status;
    char *t = malloc(len + 1);
    mrow_t (*mm)[MM_MAX] = calloc(NSYN_, sizeof *mm);
    if (!t || !mm) { free(t); free(mm); return m->status = M1P_TOO_BIG; }
    memcpy(t, text, len);
    t[len] = 0;
    /* Synths 2 and 3 start off: a file without their on/off (SYNTH ON) leaves them so */
    set_raw(m, "s2_syn_on", "0");
    set_raw(m, "s3_syn_on", "0");
    char *unk = NULL, *mapped = NULL, extra[400] = "";
    size_t ulen = 0, ucap = 0, mlen = 0, mcap = 0;
    char mods[64][40];
    int modn[64], nmods = 0, line_no = 0;
    char *save = NULL;
    for (char *line = strtok_r(t + (!strncmp(t, "\xEF\xBB\xBF", 3) ? 3 : 0), "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        line_no++;
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\r' || line[l - 1] == ' ' || line[l - 1] == '\t')) line[--l] = 0;
        while (*line == ' ' || *line == '\t') line++;
        if (!*line || *line == '#') continue;
        char *sp = line;
        while (*sp && *sp != ' ' && *sp != '\t') sp++;
        if (*line != '/' || !*sp) {
            m->unreadable++;
            grow(&unk, &ulen, &ucap, "  line %d is not \"key value\": %.60s\n", line_no, line);
            continue;
        }
        *sp++ = 0;
        while (*sp == ' ' || *sp == '\t') sp++;
        /* the key: "\_" (seen in some files) is "_"; the value: a trailing " #raw" dropped, quotes taken off */
        char *key = line, *w = line;
        for (char *r = line; *r; r++) {
            if (r[0] == '\\' && r[1] == '_') continue;
            *w++ = *r;
        }
        *w = 0;
        char *val = sp, *hash = strstr(val, " #");
        if (hash) *hash = 0;
        l = strlen(val);
        while (l && val[l - 1] == ' ') val[--l] = 0;
        if (l >= 2 && val[0] == '"' && val[l - 1] == '"') { val[l - 1] = 0; val++; }
        m->settings++;
        if (!strncmp(key, "/p/part/t/", 10) && key[10] >= '1' && key[10] <= '3') m->synths |= 1 << (key[10] - '1');
        if (meta_line(m, key, val, extra, sizeof extra) || matrix_line(key, val, mm)) { m->covered++; continue; }
        if (same_ci(val, "null")) { m->empty++; continue; }
        if (strstr(key, "/types/")) { m->stored++; continue; }   /* an effect type's settings kept for when it is picked */
        int done = 0;
        for (int r = 0; r < M1P_ROWS && !done; r++) {
            int syn, num;
            if (!match(M1P_MAP[r].m1, key, &syn, &num)) continue;
            done = 1;
            char show[96];
            int c = convert(m, &M1P_MAP[r], syn, num, val, show, sizeof show);
            if (M1P_MAP[r].conv == M_COVER || (c == 1 && !show[0])) {
                m->covered++;
            } else if (c == -2) {
                m->shared++;
                grow(&unk, &ulen, &ucap, "  %s = %s: Moo Won shares this control between the Synths (Synth 1's is "
                     "used)\n", key, val);
            } else if (c == -1) {   /* a table row naming a key or option we don't have: a mistake in m1p_map.h */
                m->unknown++;
                grow(&unk, &ulen, &ucap, "  %s = %s: (the table's '%s' doesn't fit Moo Won)\n", key, val, M1P_MAP[r].ours);
            } else if (c == 0) {
                m->unreadable++;
                grow(&unk, &ulen, &ucap, "  %s = %s: a value the table doesn't know\n", key, val);
            } else {
                char base[64];
                fill_num(M1P_MAP[r].ours, num, base, sizeof base);
                int i = param_index(base);
                m->mapped++;
                if (M1P_MAP[r].sure == 'G') m->guessed++;
                grow(&mapped, &mlen, &mcap, "  %s = %s  ->  %s%s%s = %s%s\n", key, val, syn > 1 ? "Synth " : "",
                     syn > 1 ? (syn == 2 ? "2 " : "3 ") : "", i >= 0 ? PARAMS[i].name : base, show,
                     M1P_MAP[r].sure == 'G' ? "   (a guess: check by ear)" : "");
            }
        }
        if (done) continue;
        m->unknown++;
        grow(&unk, &ulen, &ucap, "  %s = %s\n", key, val);
        char mod[40];
        module_of(key, mod, sizeof mod);
        int k = 0;
        while (k < nmods && strcmp(mods[k], mod)) k++;
        if (k == nmods && nmods < 64) { snprintf(mods[nmods], sizeof mods[0], "%s", mod); modn[nmods++] = 0; }
        if (k < nmods) modn[k]++;
    }
    for (int s = 0; s < NSYN_; s++) {
        int any = 0;
        for (int e = 0; e < MM_MAX; e++) any |= mm[s][e].used;
        if (any) matrix_rows(m, s + 1, mm[s], MM_MAX, &mapped, &mlen, &mcap, &unk, &ulen, &ucap);
    }
    free(t);
    free(mm);
    snprintf(m->notes, sizeof m->notes, "Imported from a Moog One preset file.%s", extra);
    if (!m->settings) m->status = M1P_NOT_M1P;
    else if (!m->mapped) m->status = M1P_NOTHING_MAPPED;
    grow(&m->report, &m->rlen, &m->rcap, "%d settings read: %d mapped (%d of them our best reading: check by ear; %d "
         "matrix rows), %d with no equivalent or not known yet", m->settings, m->mapped, m->guessed, m->matrix, m->unknown);
    if (m->covered) grow(&m->report, &m->rlen, &m->rcap, ", %d covered by others (knob positions, labels, matrix lines)",
                         m->covered);
    if (m->stored) grow(&m->report, &m->rlen, &m->rcap, ", %d ignored (the stored settings of effect types not in "
                        "use)", m->stored);
    if (m->shared) grow(&m->report, &m->rlen, &m->rcap, ", %d skipped (shared in Moo Won)", m->shared);
    if (m->empty) grow(&m->report, &m->rlen, &m->rcap, ", %d empty", m->empty);
    if (m->unreadable) grow(&m->report, &m->rlen, &m->rcap, ", %d lines or values that couldn't be read", m->unreadable);
    grow(&m->report, &m->rlen, &m->rcap, ".\n");
    if (mlen) grow(&m->report, &m->rlen, &m->rcap, "Mapped:\n%s", mapped);
    if (nmods) {
        grow(&m->report, &m->rlen, &m->rcap, "Not mapped, by part of the instrument (how many settings):\n");
        for (int k = 0; k < nmods; k++) grow(&m->report, &m->rlen, &m->rcap, "  %s (%d)\n", mods[k], modn[k]);
    }
    if (ulen) grow(&m->report, &m->rlen, &m->rcap, "Not mapped, every setting:\n%s", unk);
    free(unk);
    free(mapped);
    if (!m->assign) m->assign = calloc(1, 1);
    if (!m->report) m->report = calloc(1, 1);
    return m->status;
}

/* ---- the sound: a separate engine instance, the level check, the state ---- */
enum { LV_RATE = 44100, LV_HOLD = LV_RATE * 6 / 5, LV_TAIL = LV_RATE * 3 / 5, LV_BLOCK = 128 };

static double play_peak(const mpc_engine_t *E, void *eng, int *finite) {   /* a chord held, then let go */
    static const uint8_t notes[] = {48, 52, 55, 60};
    float lr[2 * LV_BLOCK];
    double peak = 0;
    uint8_t msg[3];
    for (unsigned k = 0; k < sizeof notes; k++) {
        msg[0] = 0x90; msg[1] = notes[k]; msg[2] = 100;
        E->midi(eng, msg, 3);
    }
    for (int at = 0; at < LV_HOLD + LV_TAIL; at += LV_BLOCK) {
        if (at >= LV_HOLD && at < LV_HOLD + LV_BLOCK)
            for (unsigned k = 0; k < sizeof notes; k++) {
                msg[0] = 0x80; msg[1] = notes[k]; msg[2] = 0;
                E->midi(eng, msg, 3);
            }
        one_ish_render_float(eng, lr, LV_BLOCK);
        for (int s = 0; s < 2 * LV_BLOCK; s++) {
            if (!isfinite(lr[s])) { *finite = 0; continue; }
            if (fabs(lr[s]) > peak) peak = fabs(lr[s]);
        }
    }
    return peak;
}

static void *fresh(const mpc_engine_t *E, const char *assign, double vol_scale) {
    void *eng = E->create(NULL);
    if (!eng) return NULL;
    char *a = strdup(assign ? assign : ""), *save = NULL;
    for (char *kv = a ? strtok_r(a, ";", &save) : NULL; kv; kv = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(kv, '=');
        if (!eq) continue;
        *eq = 0;
        const char *nm = strstr(kv, "syn_name");
        if (nm && (nm == kv || (kv[0] == 's' && nm == kv + 3))) {   /* a Synth's name: the engine's window command */
            char cmd[40];
            snprintf(cmd, sizeof cmd, "synname%c\n%s", nm == kv ? '1' : kv[1], eq + 1);
            E->set_param(eng, "state", cmd);
        } else {
            E->set_param(eng, kv, eq + 1);
        }
    }
    free(a);
    static const char *const VOL[] = {"volume", "s2_volume", "s3_volume"};
    for (int s = 0; s < 3 && vol_scale != 1.0; s++) {   /* every Synth's Volume by the same step */
        char b[32];
        double v = 0;
        if (E->get_param(eng, VOL[s], b, sizeof b) > 0) v = atof(b);
        snprintf(b, sizeof b, "%.2f", v * vol_scale < 1 ? 1 : v * vol_scale);
        E->set_param(eng, VOL[s], b);
    }
    return eng;
}

int m1p_make_state(const char *assign, char *state, int n, m1p_level_t *lv) {
    const mpc_engine_t *E = mpc_engine();
    memset(lv, 0, sizeof *lv);
    lv->finite = 1;
    if (!E) return 0;
    void *eng = fresh(E, assign, 1.0);
    if (!eng) return 0;
    double peak = play_peak(E, eng, &lv->finite), scale = 1.0;
    /* over -1 dBFS: Volume down towards a -4 dBFS peak and played again (the level isn't quite in proportion to
     * Volume where something saturates or detunes at random, so up to four times) */
    for (int k = 0; k < 4 && peak > 0.891; k++) {
        scale *= 0.63 / peak;
        E->destroy(eng);
        eng = fresh(E, assign, scale);
        if (!eng) return 0;
        lv->trim_db = -20 * log10(scale);
        peak = play_peak(E, eng, &lv->finite);
    }
    lv->peak_db = 20 * log10(peak + 1e-9);
    lv->silent = peak < 3.2e-4;   /* under -70 dBFS */
    int ok = E->get_param(eng, "state", state, n) > 0;
    E->destroy(eng);
    return ok;
}
