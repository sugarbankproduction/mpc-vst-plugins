/* Microtuning for one Synth (beyond the Moog One manual, audit package 16): a Scala scale and keyboard map turned into a
 * pitch for each of the 128 keys. Pure arithmetic on fixed arrays: no allocation, no files.
 *
 * The files (.scl, .kbm) are read and checked by the window on its own thread (desktop/scala.c), which hands the
 * engine a compact text that is also what the sound's state keeps (src/synths.h, keys "tun_scl" and "tun_kbm", Synths
 * 2 and 3 with "s2_" / "s3_"):
 *     scale         "<name>|<n>,<c1>,...,<cn>"         the degrees in cents above the first (which is 0); the last, cn,
 *                                                     is the period (1200 for an octave)
 *     keyboard map  "<name>|<size>,<first>,<last>,<middle>,<ref>,<freq>,<octave>[,<m1>,...]"
 *                   as a .kbm: map size (0: every key the next degree), the first and last keys retuned (the others
 *                   are silent), the key that plays degree 0, the reference key and its frequency in Hz, the degree
 *                   one map repeat moves (0: the scale's size), then the map (-1 for a key left out, Scala's "x")
 * Without a keyboard map, key 60 plays degree 0 at its usual pitch (middle C) and every key plays the next degree, so
 * a 12-note equal scale gives exactly the usual pitches. Master tune and fine tune still move everything (engine.c:
 * the key's pitch here, then the master tune's A).
 *
 * tun_pitch() maps a key (fractional while gliding: between two keys' pitches) to its pitch in semitones as MIDI note
 * numbers count them (69 = A at the master tune). With no scale loaded the engine doesn't call it at all, so a sound
 * without one plays exactly as before. */
#ifndef ONEISH_TUNING_H
#define ONEISH_TUNING_H
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TUN_DEG_MAX 128      /* scale degrees; keyboard map entries */
#define TUN_SCL_MAX 2048     /* the scale's compact text, its NUL included */
#define TUN_KBM_MAX 1024     /* the keyboard map's */
#define TUN_NAME_MAX 48      /* a name in either (the file's name) */

typedef struct {
    int on;                  /* a scale is loaded: keys play key[] */
    float key[128];          /* each key's pitch, semitones (69 = A above middle C) */
    unsigned char play[128]; /* 0: a key the keyboard map leaves out (or outside its first..last): silent */
    char scl[TUN_SCL_MAX];   /* the compact texts as set ("" none) */
    char kbm[TUN_KBM_MAX];
} tuning_t;

static inline void tun_clear(tuning_t *t) {
    t->on = 0;
    t->scl[0] = t->kbm[0] = 0;
    for (int k = 0; k < 128; k++) { t->key[k] = (float)k; t->play[k] = 1; }
}

/* the numbers after "<name>|" in a compact text, comma-separated: how many were read (at most max), -1 if malformed */
static inline int tun_numbers(const char *text, double *out, int max) {
    const char *p = strchr(text, '|');
    int n = 0;
    if (!p) return -1;
    p++;
    while (*p) {
        char *end;
        double v = strtod(p, &end);
        if (end == p || !isfinite(v) || n >= max) return -1;
        out[n++] = v;
        p = end;
        if (*p == ',') p++;
        else if (*p) return -1;
    }
    return n;
}

static inline long tun_floor_div(long a, long b) { long q = a / b; return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q; }

/* the scale's cents at degree d (any integer: whole periods up or down, then the degree within one) */
static inline double tun_cents(const double *c, int n, long d) {
    long q = tun_floor_div(d, n);
    return (double)q * c[n] + c[d - q * n];
}

/* the table from the two texts. 1: done (t->on), 0: a text made no sense (t unchanged) */
static inline int tun_build(tuning_t *t, const char *scl, const char *kbm) {
    double c[TUN_DEG_MAX + 1], m[7 + TUN_DEG_MAX];
    double nums[TUN_DEG_MAX + 1];
    int got = tun_numbers(scl, nums, TUN_DEG_MAX + 1);
    if (got < 2 || nums[0] != floor(nums[0]) || nums[0] < 1 || nums[0] > TUN_DEG_MAX || got != (int)nums[0] + 1)
        return 0;
    int n = (int)nums[0];
    c[0] = 0.0;
    for (int k = 1; k <= n; k++) c[k] = nums[k];
    if (!(c[n] > 0.0)) return 0;   /* a period that doesn't go up */
    /* the keyboard map: given, or the default (key 60 degree 0 at its usual pitch, every key the next degree) */
    long size = 0, first = 0, last = 127, middle = 60, ref = 60, oct = n;
    double ref_st = 60.0;
    int nmap = 0;
    if (kbm && kbm[0]) {
        int k = tun_numbers(kbm, m, 7 + TUN_DEG_MAX);
        if (k < 7) return 0;
        for (int j = 0; j < k; j++)
            if (j != 5 && m[j] != floor(m[j])) return 0;   /* whole numbers, but the frequency */
        size = (long)m[0], first = (long)m[1], last = (long)m[2], middle = (long)m[3], ref = (long)m[4];
        oct = (long)m[6];
        nmap = k - 7;
        if (size < 0 || size > TUN_DEG_MAX || nmap > size || first < 0 || last > 127 || first > last || middle < 0
            || middle > 127 || ref < 0 || ref > 127 || !(m[5] > 0.0) || oct < 0 || oct > 100000)
            return 0;
        if (oct == 0) oct = n;
        ref_st = 69.0 + 12.0 * log2(m[5] / 440.0);
        for (int j = 0; j < nmap; j++)
            if (m[7 + j] < -1 || m[7 + j] > 100000) return 0;
    }
    /* each key's degree (or none) */
    long deg[128];
    unsigned char ok[128];
    for (int key = 0; key < 128; key++) {
        long d = key - middle;
        ok[key] = key >= first && key <= last;
        if (size == 0) { deg[key] = d; continue; }
        long q = tun_floor_div(d, size), r = d - q * size;
        long e = r < nmap ? (long)m[7 + r] : -1;   /* (entries past the listed ones: left out, as in Scala) */
        if (e < 0) { ok[key] = 0; deg[key] = d; continue; }
        deg[key] = q * oct + e;
    }
    long dref = deg[ref];
    if (size != 0 && (ref - middle) != 0) {   /* the reference key's degree, even when the map leaves it out */
        long q = tun_floor_div(ref - middle, size), r = (ref - middle) - q * size;
        long e = r < nmap ? (long)m[7 + r] : -1;
        dref = e >= 0 ? q * oct + e : ref - middle;
    }
    double cref = tun_cents(c, n, dref);
    for (int key = 0; key < 128; key++) t->key[key] = (float)(ref_st + (tun_cents(c, n, deg[key]) - cref) / 100.0);
    /* a key left out still gets a pitch between its neighbours', so a glide across it stays smooth */
    for (int key = 0; key < 128; key++) {
        if (ok[key]) continue;
        int lo = key - 1, hi = key + 1;
        while (lo >= 0 && !ok[lo]) lo--;
        while (hi < 128 && !ok[hi]) hi++;
        if (lo >= 0 && hi < 128) t->key[key] = t->key[lo] + (t->key[hi] - t->key[lo]) * (float)(key - lo) / (float)(hi - lo);
        else if (lo >= 0) t->key[key] = t->key[lo] + (float)(key - lo);
        else if (hi < 128) t->key[key] = t->key[hi] - (float)(hi - key);
        else t->key[key] = (float)key;
    }
    memcpy(t->play, ok, sizeof t->play);
    t->on = 1;
    return 1;
}

/* a new scale (kbm: keep the keyboard map) or keyboard map ("" back to the default one). 1: taken */
static inline int tun_set(tuning_t *t, const char *scl, const char *kbm) {   /* (scl and kbm may be t's own texts) */
    if (strlen(scl) >= TUN_SCL_MAX || strlen(kbm) >= TUN_KBM_MAX) return 0;
    char s[TUN_SCL_MAX], k[TUN_KBM_MAX];
    snprintf(s, sizeof s, "%s", scl);
    snprintf(k, sizeof k, "%s", kbm);
    if (!s[0]) {   /* no scale: equal temperament as without one (a keyboard map alone is kept for later) */
        tun_clear(t);
        memcpy(t->kbm, k, sizeof k);
        return 1;
    }
    tuning_t nt;
    if (!tun_build(&nt, s, k)) return 0;
    memcpy(t->key, nt.key, sizeof t->key);
    memcpy(t->play, nt.play, sizeof t->play);
    memcpy(t->scl, s, sizeof s);
    memcpy(t->kbm, k, sizeof k);
    t->on = 1;
    return 1;
}

/* a key's pitch (x: a key number, fractional while gliding), semitones */
static inline float tun_pitch(const tuning_t *t, float x) {
    int i = (int)floorf(x);
    if (i < 0) i = 0;
    if (i > 126) i = 126;
    return t->key[i] + (t->key[i + 1] - t->key[i]) * (x - (float)i);
}

/* the name part of a compact text, into out */
static inline void tun_name(const char *text, char *out, int n) {
    const char *b = strchr(text, '|');
    int len = b ? (int)(b - text) : 0;
    snprintf(out, (size_t)n, "%.*s", len, text);
}
#endif
