/* Scala files for microtuning (package 16): a scale (.scl) and a keyboard map (.kbm) read into the compact texts the
 * engine takes and the sound's state keeps (src/tuning.h describes them). Plain C, no Windows calls: the window's
 * TUNING tab uses it on its own thread (desktop/settings.c), and the offline tests include it (test/midi_check.c).
 *
 * The formats (the Scala program's, as published):
 *   .scl  lines starting with '!' are comments. The first other line is a description (may be empty), the next the
 *         number of notes, then one pitch per line: with a '.' it is cents ("701.955"), else a ratio ("3/2", or a
 *         whole number: "2" is 2/1). Anything after the number on a line is ignored. The first degree (0 cents) is
 *         not listed; the last pitch is the period (usually 2/1, the octave).
 *   .kbm  comment lines likewise; then the map size, the first and last keys to retune, the middle key (degree 0),
 *         the reference key and its frequency, the degree that is one map repeat (the formal octave), then one
 *         entry per map position: a degree number, or 'x' for a key left out. Fewer entries than the size: the rest
 *         are left out. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/tuning.h"

/* the next line that isn't a comment (NULL at the end); *p moves past it. Line ends: \n, \r\n or \r */
static const char *sc_line(const char **p, char *buf, int n) {
    while (**p) {
        const char *s = *p, *e = s;
        while (*e && *e != '\n' && *e != '\r') e++;
        *p = *e == '\r' && e[1] == '\n' ? e + 2 : *e ? e + 1 : e;
        if (*s == '!') continue;
        int len = (int)(e - s) < n - 1 ? (int)(e - s) : n - 1;
        memcpy(buf, s, (size_t)len);
        buf[len] = 0;
        return buf;
    }
    return NULL;
}

/* a name usable inside the compact text: the file's name without its folder and extension, plain characters only */
static void sc_name(const char *path, char *out, int n) {
    const char *b = path;
    for (const char *c = path; *c; c++)
        if (*c == '\\' || *c == '/') b = c + 1;
    int k = 0;
    for (const char *c = b; *c && k < n - 1 && k < TUN_NAME_MAX - 1; c++) {
        if (*c == '.' && (!strcmp(c, ".scl") || !strcmp(c, ".kbm") || !strcmp(c, ".SCL") || !strcmp(c, ".KBM"))) break;
        unsigned char ch = (unsigned char)*c;
        out[k++] = ch >= 32 && ch < 127 && !strchr("|;=,~", ch) ? (char)ch : '-';
    }
    out[k] = 0;
    if (!k) snprintf(out, (size_t)n, "scale");
}

static void sc_err(char *err, int en, const char *msg) { if (err && en > 0) snprintf(err, (size_t)en, "%s", msg); }

/* a .scl file's text -> "<name>|<n>,<c1>,...,<cn>". 1: done; 0: err says why */
int scl_compact(const char *text, const char *path, char *out, int n, char *err, int en) {
    const char *p = text;
    char line[512];
    if (!strncmp(p, "\xEF\xBB\xBF", 3)) p += 3;
    if (!sc_line(&p, line, sizeof line)) { sc_err(err, en, "the file is empty"); return 0; }   /* the description */
    if (!sc_line(&p, line, sizeof line)) { sc_err(err, en, "the number of notes is missing"); return 0; }
    char *end;
    long count = strtol(line, &end, 10);
    if (end == line || count < 1 || count > TUN_DEG_MAX) {
        sc_err(err, en, "the number of notes is not 1 to 128");
        return 0;
    }
    char name[TUN_NAME_MAX];
    sc_name(path, name, sizeof name);
    int used = snprintf(out, (size_t)n, "%s|%ld", name, count);
    for (long k = 0; k < count; k++) {
        if (!sc_line(&p, line, sizeof line)) { sc_err(err, en, "fewer pitches than the number of notes says"); return 0; }
        const char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        size_t tok = strcspn(s, " \t");
        double cents;
        if (memchr(s, '.', tok)) {
            cents = strtod(s, &end);
            if (end == s) { sc_err(err, en, "a pitch line makes no sense"); return 0; }
        } else {
            long a = strtol(s, &end, 10), b = 1;
            if (end == s || a <= 0) { sc_err(err, en, "a ratio makes no sense (it must be above 0)"); return 0; }
            if (*end == '/') {
                const char *d = end + 1;
                b = strtol(d, &end, 10);
                if (end == d || b <= 0) { sc_err(err, en, "a ratio makes no sense (it must be above 0)"); return 0; }
            }
            cents = 1200.0 * log2((double)a / (double)b);
        }
        if (!isfinite(cents)) { sc_err(err, en, "a pitch makes no sense"); return 0; }
        used += snprintf(used < n ? out + used : NULL, used < n ? (size_t)(n - used) : 0, ",%.10g", cents);
    }
    if (used >= n || used >= TUN_SCL_MAX) { sc_err(err, en, "the scale is too long"); return 0; }
    tuning_t t;
    if (!tun_build(&t, out, "")) { sc_err(err, en, "the scale's last pitch (its period) must be above 0 cents"); return 0; }
    return 1;
}

/* a .kbm file's text -> "<name>|<size>,<first>,<last>,<middle>,<ref>,<freq>,<octave>,<m1>,...". 1: done */
int kbm_compact(const char *text, const char *path, char *out, int n, char *err, int en) {
    const char *p = text;
    char line[512], name[TUN_NAME_MAX];
    double head[7];
    static const char *const WHAT[7] = {"the map size", "the first key", "the last key", "the middle key",
                                        "the reference key", "the reference frequency", "the octave degree"};
    if (!strncmp(p, "\xEF\xBB\xBF", 3)) p += 3;
    for (int k = 0; k < 7; k++) {
        char *end;
        if (!sc_line(&p, line, sizeof line)) {
            char m[96];
            snprintf(m, sizeof m, "%s is missing", WHAT[k]);
            sc_err(err, en, m);
            return 0;
        }
        head[k] = strtod(line, &end);
        if (end == line || !isfinite(head[k])) {
            char m[96];
            snprintf(m, sizeof m, "%s makes no sense", WHAT[k]);
            sc_err(err, en, m);
            return 0;
        }
    }
    if (head[0] < 0 || head[0] > TUN_DEG_MAX) { sc_err(err, en, "the map size is not 0 to 128"); return 0; }
    sc_name(path, name, sizeof name);
    int used = snprintf(out, (size_t)n, "%s|%d,%d,%d,%d,%d,%.10g,%d", name, (int)head[0], (int)head[1], (int)head[2],
                        (int)head[3], (int)head[4], head[5], (int)head[6]);
    for (int k = 0; k < (int)head[0]; k++) {
        if (!sc_line(&p, line, sizeof line)) break;   /* (fewer entries: the rest left out) */
        const char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        long v;
        if (*s == 'x' || *s == 'X') v = -1;
        else {
            char *end;
            v = strtol(s, &end, 10);
            if (end == s || v < 0) { sc_err(err, en, "a map entry is not a degree or x"); return 0; }
        }
        used += snprintf(used < n ? out + used : NULL, used < n ? (size_t)(n - used) : 0, ",%ld", v);
    }
    if (used >= n || used >= TUN_KBM_MAX) { sc_err(err, en, "the keyboard map is too long"); return 0; }
    tuning_t t;   /* checked against a plain 12-note scale: its numbers must make sense */
    if (!tun_build(&t, "x|12,100,200,300,400,500,600,700,800,900,1000,1100,1200", out)) {
        sc_err(err, en, "the keyboard map's keys or frequency are out of range");
        return 0;
    }
    return 1;
}
