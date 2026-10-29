/* ONE-ish's sequence generator (2026-10-29): writes a step sequence (src/stepseq.h's sq_seq_t) in a Synth's KEY and
 * SCALE (src/keyscale.h). Pure: a seed and the options in, a pattern out; no globals, no allocation, no I/O, so the
 * window (desktop/seq_page.c's GEN view, and later the randomiser) runs it and sends the result to the engine as a
 * "seqcmd" load; the engine just plays the steps.
 *
 * How it makes music: one bar (16 steps, or the length when shorter) is the MOTIF. Its rhythm comes from weights per
 * step (STRAIGHT: the beats first; SYNCO: the off-beats of a 3-3-2 feel; EUCLID: hits spread as evenly as they go) and
 * each of its steps draws one random number that decides its note. Every later bar reuses those numbers, so the bar
 * repeats (each bar plays DENSITY % of its steps); VARIATION is the chance a step draws a new one (and that a hit
 * moves), so the line wanders more or less.
 * Bars follow a chord progression from the scale (a single chord when VARIATION is low, except for chords and arps);
 * bass lines sit on the chord's root (fifths, octaves and passing notes off the beat), arps run through its chord
 * tones, melodies step through the scale landing on chord tones on the beat, chords are voiced inside the octave so
 * they move by the nearest inversion. The last note of a phrase goes home to the key's root. ACCENT spreads the
 * velocities (beats louder), FILLS adds ties and ratchets (more of them in the last beat), MODS fills modulation lanes
 * (both filters' cutoff with one shape, then resonance, then a fourth lane per style). LENGTH is the sequencer's. */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include "stepseq.h"
#include "keyscale.h"

enum { SG_BASS, SG_ARP, SG_MELODY, SG_CHORDS, SG_RANDOM, SG_STYLES };
enum { SG_STRAIGHT, SG_SYNCO, SG_EUCLID, SG_RHYTHMS };
typedef struct {
    int style;       /* SG_BASS .. SG_RANDOM */
    int rhythm;      /* SG_STRAIGHT, SG_SYNCO, SG_EUCLID */
    int density;     /* % of the steps that play (1-100) */
    int range;       /* octaves the notes spread over (1-4) */
    int variation;   /* 0: every bar the same; 100: it wanders a lot */
    int accent;      /* 0: one velocity; 100: strong beats loud, off-beats soft */
    int fills;       /* ties and ratchets, 0-100 */
    int mods;        /* modulation lanes, 0 (none) - 100 (four lanes, deep) */
    int len;         /* steps (1-64): the sequencer's LENGTH */
} seqgen_opts_t;

static inline void seqgen_defaults(seqgen_opts_t *o) {
    o->style = SG_BASS; o->rhythm = SG_STRAIGHT; o->density = 60; o->range = 2; o->variation = 30; o->accent = 50;
    o->fills = 25; o->mods = 30; o->len = 16;
}

static const uint8_t SG_OCT[SG_STYLES] = {3, 5, 5, 4, 4};   /* each style's lowest octave (3: C2 = 36) */
/* the lowest note a style plays in (the key's root there): every note lies within `range` octaves above it */
static inline int seqgen_base(int style, int root) {
    return 12 * SG_OCT[style < 0 || style >= SG_STYLES ? 0 : style] + ((root % 12) + 12) % 12;
}

/* ---- helpers ---- */
static inline uint32_t sg_rnd(uint32_t *x) { uint32_t v = *x; v ^= v << 13; v ^= v >> 17; v ^= v << 5; return *x = v; }
static inline int sg_below(uint32_t *x, int n) { return n > 0 ? (int)(sg_rnd(x) % (uint32_t)n) : 0; }
static inline int sg_pct(uint32_t *x, int p) { return sg_below(x, 100) < p; }
static inline int sg_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int sg_semi(int scale, int d) { return ks_degree(0, scale, 0, d); }   /* degree d's semitones above the root */

/* degree p of a seven-note scale (0 = I .. 6 = VII) as the degree of `scale` nearest it in pitch */
static inline int sg_map(int scale, int p) {
    static const uint8_t MAJ[7] = {0, 2, 4, 5, 7, 9, 11};
    if (KS_SIZE[scale] == 7) return p;
    int best = 0, bd = 99;
    for (int d = 0; d < KS_SIZE[scale]; d++) {
        int dd = abs(sg_semi(scale, d) - MAJ[p % 7]);
        if (dd < bd) { bd = dd; best = d; }
    }
    return best;
}
/* the chord on degree c: four degrees (root, third, fifth, seventh), the scale's notes nearest 0, 3-4, 7 and 10-11
 * semitones above c (any scale: diatonic triads in the modes, something close in the others) */
static inline void sg_chord(int scale, int c, int deg[4]) {
    static const uint8_t T2[4] = {0, 7, 14, 21};   /* the targets in half-semitones */
    int base = sg_semi(scale, c), n = KS_SIZE[scale];
    deg[0] = c;
    for (int k = 1; k < 4; k++) {
        int best = deg[k - 1] + 1, bd = 999;
        for (int t = deg[k - 1] + 1; t <= c + n; t++) {
            int dd = abs(2 * (sg_semi(scale, t) - base) - T2[k]);
            if (dd < bd) { bd = dd; best = t; }
        }
        deg[k] = best;
    }
}
/* a degree folded into 0..top by octaves (n degrees each) */
static inline int sg_fold(int d, int n, int top) {
    while (d > top) d -= n;
    while (d < 0) d += n;
    return d;
}
/* the chord tone (any octave, within 0..top) nearest degree d (ties: the lower) */
static inline int sg_snap(int d, const int ct[4], int tones, int n, int top) {
    int best = d, bd = 999;
    for (int o = -1; o <= top / n + 1; o++)
        for (int j = 0; j < tones; j++) {
            int c = ct[j] % n + o * n, dd = abs(c - d);
            if (c < 0 || c > top) continue;
            if (dd < bd || (dd == bd && c < best)) { bd = dd; best = c; }
        }
    return best;
}
/* MIDI note of degree d above the style's base octave, into [lo, lo + 12) by octaves (window 0: no fold) */
static inline int sg_note(int root, int scale, int oct, int d) { return ks_degree(root, scale, oct, d); }
static inline int sg_in(int note, int lo) {
    while (note < lo) note += 12;
    while (note >= lo + 12) note -= 12;
    return note;
}

/* a step's rhythm weight (higher plays first) */
static inline int sg_weight(int rhythm, int i, int bar) {
    static const uint8_t SYN[16] = {9, 1, 3, 8, 2, 3, 8, 1, 3, 2, 7, 2, 6, 2, 5, 3};
    if (rhythm == SG_SYNCO) return SYN[i * 16 / (bar < 1 ? 1 : bar) % 16];
    return i % 8 == 0 ? 10 : i % 4 == 0 ? 8 : i % 2 == 0 ? 5 : 2;
}

static const int8_t SG_PROG[][4] = {
    {0, 0, 0, 0},   /* one chord (low VARIATION) */
    {0, 5, 3, 4},   /* I vi IV V (minor: i VI iv v) */
    {0, 3, 4, 0},   /* I IV V I */
    {0, 5, 3, 6},   /* i VI iv VII */
    {0, 6, 5, 6},   /* i VII VI VII */
    {0, 3, 0, 4},   /* i iv i v */
    {0, 4, 5, 3},   /* I V vi IV */
    {0, 2, 3, 4},   /* i iii iv v */
};
#define SG_NPROG ((int)(sizeof SG_PROG / sizeof SG_PROG[0]))

/* ---- the generator: a new sequence in *out (everything else cleared); *seed moves on (so a caller can chain), the
 * same *seed in always gives the same pattern. Returns the number of steps that play. ---- */
static inline int seqgen_make(const seqgen_opts_t *o, int root, int scale, uint32_t *seed, sq_seq_t *out) {
    scale = ks_clamp_scale(scale);
    root = ((root % 12) + 12) % 12;
    int style = sg_clampi(o->style, 0, SG_STYLES - 1), rhythm = sg_clampi(o->rhythm, 0, SG_RHYTHMS - 1);
    int len = sg_clampi(o->len, 1, SQ_STEPS), dens = sg_clampi(o->density, 1, 100), range = sg_clampi(o->range, 1, 4);
    int var = sg_clampi(o->variation, 0, 100), acc = sg_clampi(o->accent, 0, 100), fills = sg_clampi(o->fills, 0, 100);
    int mods = sg_clampi(o->mods, 0, 100);
    uint32_t x = (*seed ? *seed : 1) * 2654435761u ^ 0x6D2B79F5u;
    if (!x) x = 1;
    for (int i = 0; i < 4; i++) sg_rnd(&x);
    sq_clear(out);
    int n = KS_SIZE[scale], top = n * range, oct = SG_OCT[style], base = seqgen_base(style, root);
    int bar = len >= 16 ? 16 : len >= 8 ? 8 : len;
    int span = (style == SG_CHORDS || style == SG_ARP) && len <= 16 && bar >= 8 ? bar / 2 : bar;   /* a chord's steps */
    int prog = var < 15 && style != SG_CHORDS && style != SG_ARP ? 0 : 1 + sg_below(&x, SG_NPROG - 1);
    int sev = sg_pct(&x, 45), pat = sg_below(&x, 4), shape = sg_below(&x, 4);
    int rot = rhythm == SG_EUCLID && style != SG_BASS ? sg_below(&x, bar) : 0;

    /* the rhythm: each bar takes its share of the hits; the motif's weights, nudged by VARIATION in later bars */
    uint8_t hit[SQ_STEPS] = {0};
    int w0[16];
    for (int i = 0; i < bar; i++)
        w0[i] = style == SG_RANDOM ? sg_below(&x, 40) : 4 * sg_weight(rhythm, i, bar) + sg_below(&x, 4);
    if (style == SG_BASS && rhythm != SG_EUCLID) w0[0] = 99;   /* a bass line starts on the one */
    for (int b = 0; b * bar < len; b++) {
        int st = b * bar, bs = len - st < bar ? len - st : bar;
        int hb = (dens * bs + 50) / 100;   /* (each bar its own share: so the bars can repeat) */
        if (b == 0 && hb < 1) hb = 1;
        if (rhythm == SG_EUCLID) {
            for (int i = 0; i < bs; i++) hit[st + i] = (uint8_t)((i + rot) * hb % bs < hb);
            continue;
        }
        int w[16];
        for (int i = 0; i < bs; i++) w[i] = w0[i] + (b > 0 ? sg_below(&x, var * 12 / 100 + 1) : 0);
        for (int k = 0; k < hb; k++) {   /* the hb highest (ties: the earlier) */
            int best = -1;
            for (int i = 0; i < bs; i++)
                if (!hit[st + i] && (best < 0 || w[i] > w[best])) best = i;
            hit[st + best] = 1;
        }
    }
    int last = -1, count = 0;
    for (int s = 0; s < len; s++) if (hit[s]) { last = s; count++; }

    /* the motif: one random number per step of the bar (a melody's second half answers its first) */
    uint32_t mr[16], rs[SQ_STEPS];
    for (int i = 0; i < bar; i++) mr[i] = sg_rnd(&x);
    if (style == SG_MELODY && bar >= 8)
        for (int i = bar / 2; i < bar * 3 / 4; i++) mr[i] = mr[i - bar / 2];
    for (int s = 0; s < len; s++) {
        int v = sg_pct(&x, var / 2), fresh = (int)sg_rnd(&x);
        rs[s] = s >= bar && v ? (uint32_t)fresh : mr[s % bar];
    }

    /* the notes */
    int ct[4], c0[4], cur = 0, leap = 0, ai = 0, aspan = -1, apat = pat;
    sg_chord(scale, sg_map(scale, SG_PROG[prog][0]), c0);
    int start = sg_fold(c0[sg_below(&x, 3)] + n * ((range - 1) / 2), n, top);
    for (int s = 0; s < len; s++) {
        int pos = s % bar, sp = s / span;
        uint32_t r = rs[s];
        if (pos == 0) { cur = start; leap = 0; }
        if (sp != aspan) {   /* a new chord: the arp starts again (and may turn round in later chords) */
            aspan = sp;
            ai = 0;
            apat = sp > 0 && sg_pct(&x, var / 3) ? (pat + 1 + sg_below(&x, 3)) % 4 : pat;
        }
        if (!hit[s]) continue;
        sg_chord(scale, sg_map(scale, SG_PROG[prog][sp % 4]), ct);
        int c = ct[0], strong = pos % 4 == 0, k = (int)(r % 100), gate = 50, end = s == last && len >= 4;
        int vel = 92 + (pos % 4 == 0 ? acc * 35 / 100 : pos % 2 == 0 ? -acc / 10 : -acc * 3 / 10)
                + ((int)(r >> 12 & 31) % 21 - 10) * acc / 100;
        int deg = 0;
        switch (style) {
        case SG_BASS:
            if (strong) deg = k < 80 ? c : k < 92 && range > 1 ? c + n : ct[2];
            else deg = k < 35 ? c : k < 55 ? c + n : k < 75 ? ct[2] : k < 85 ? ct[1] : k < 93 ? c - 1 : c + 1;
            if (range > 2 && !strong && (r >> 8) % 100 < 20) deg += n * (1 + (int)((r >> 16) % (uint32_t)(range - 2)));
            deg = sg_fold(deg, n, top);
            if (end) deg = 0;
            gate = 45 + (int)(r >> 20 & 31) % 25;
            break;
        case SG_ARP: {
            int tl[24], nt = 0;
            for (int o2 = 0; o2 <= range && nt < 20; o2++)
                for (int j = 0; j < 3 + sev; j++) {
                    int d = ct[j] + o2 * n;
                    if (d <= top && nt < 24) tl[nt++] = d;
                }
            for (int a = 1; a < nt; a++)   /* (sorted: a high chord's tones can pass the octave) */
                for (int b2 = a; b2 > 0 && tl[b2] < tl[b2 - 1]; b2--) { int t = tl[b2]; tl[b2] = tl[b2 - 1]; tl[b2 - 1] = t; }
            int i = ai++, per = nt > 1 ? 2 * nt - 2 : 1, j;
            static const uint8_t ALB[4] = {0, 2, 1, 2};
            if (apat == 0) j = i % nt;
            else if (apat == 1) j = nt - 1 - i % nt;
            else if (apat == 2) { j = i % per; if (j >= nt) j = per - j; }
            else j = (i / 4 * 3 + ALB[i % 4]) % nt;
            deg = nt ? tl[j] : c;
            if (end) deg = 0;
            gate = 38 + (int)(r >> 20 & 15);
            break;
        }
        case SG_MELODY: {
            int step = k < 15 ? 0 : k < 40 ? 1 : k < 65 ? -1 : k < 75 ? 2 : k < 85 ? -2 : k < 92 ? 3 + (int)(r >> 8 & 1)
                     : -3 - (int)(r >> 8 & 1);
            if (leap) step = leap > 0 ? -1 : 1;   /* after a leap, a step back the other way */
            leap = abs(step) >= 3 ? step : 0;
            cur += step;
            if (cur > top) cur = 2 * top - cur;
            if (cur < 0) cur = -cur;
            cur = sg_clampi(cur, 0, top);
            int phrase = pos == bar - 1 || (s + 1 < len && (s + 1) % bar == 0) || (bar >= 8 && pos >= bar - 4 && s + 1 < len
                         && !hit[s + 1] && (s + 2 >= len || (s + 2) % bar < 2));
            if (strong || phrase) cur = sg_snap(cur, ct, 3, n, top);
            gate = phrase ? 85 : 55 + (int)(r >> 20 & 31) % 35;
            if (end) { cur = sg_clampi((cur + n / 2) / n * n, 0, top); gate = 95; }
            deg = cur;
            break;
        }
        case SG_RANDOM:
            deg = (int)(r % (uint32_t)(top + 1));
            gate = 10 + (int)((r >> 8) % 90);
            vel = 127 - (int)((r >> 12) % (uint32_t)(1 + acc * 87 / 100));
            break;
        }
        if (style == SG_CHORDS) {   /* the chord inside an octave (the nearest inversion), a root under it and doubling
                                     * over it as the range allows */
            int lo = base + (range > 1 ? 12 : 0), lane = 0, tones = 3 + sev;
            int thin = !strong && k < var / 3;   /* a lighter stab: no fifth */
            if (range > 1) sq_set_note(out, s, lane++, sg_in(sg_note(root, scale, oct, c), base), vel, 0, 1, 0);
            for (int j = 0; j < tones; j++) {
                if (thin && j == 2) continue;
                sq_set_note(out, s, lane++, sg_in(sg_note(root, scale, oct, ct[j]), lo), vel, 0, 1, 0);
            }
            if (range > 2) sq_set_note(out, s, lane++, sg_in(sg_note(root, scale, oct, c), base + 24), vel, 0, 1, 0);
            if (range > 3) sq_set_note(out, s, lane++, sg_in(sg_note(root, scale, oct, ct[2]), base + 36), vel, 0, 1, 0);
            gate = (strong ? 38 : 28) + (int)(r >> 20 & 15);
            if (end) gate = 90;
            for (int l = 0; l < lane; l++) out->n[s][l].gate = (uint8_t)gate;
        } else {
            int note = sg_note(root, scale, oct, sg_clampi(deg, 0, top));
            sq_set_note(out, s, 0, note, vel, gate, 1, 0);
        }
    }

    /* FILLS: ratchets (off the beat, more in the last beat), then ties into the next hit */
    for (int s = 0; s < len && fills; s++) {
        if (!hit[s]) continue;
        int pos = s % bar, p = fills * (style == SG_CHORDS ? 8 : style == SG_MELODY ? 10 : 22) / 100;
        if (len >= 8 && s >= len - (bar < 8 ? 2 : bar / 4)) p *= 2;
        if ((pos % 4 == 0 && style != SG_RANDOM) || (int)(rs[s] >> 24) % 100 >= p) continue;
        int rat = 2 + (fills > 60 && (rs[s] >> 3) % 100 < 30) + (fills > 85 && (rs[s] >> 5) % 100 < 20);
        for (int l = 0; l < SQ_NL; l++)
            if (sq_has(&out->n[s][l])) out->n[s][l].flags = (uint8_t)((out->n[s][l].flags & SQ_TIE) | (rat - 1));
    }
    for (int s = 0; s + 1 < len && fills; s++) {
        if (!hit[s] || !hit[s + 1] || s + 1 == last || (s + 1) % bar == 0 || sq_ratchet(&out->n[s][0]) > 1) continue;
        if ((int)(rs[s] >> 4) % 100 >= fills / 4) continue;
        for (int l = 0; l < SQ_NL; l++) {   /* the next step holds the same notes, tied over (no new attack) */
            sq_note_t *a = &out->n[s][l], *b = &out->n[s + 1][l];
            if (!sq_has(a)) { memset(b, SQ_EMPTY, sizeof *b); continue; }
            int g = sq_has(b) ? b->gate : a->gate;
            *b = *a;
            b->gate = (uint8_t)g;
            b->flags &= (uint8_t)~SQ_TIE;
            a->flags |= SQ_TIE;
            a->gate = 100;
        }
        s++;   /* (not a tie into a tie) */
    }

    /* MODS: both cutoffs with one shape; resonance on the accents; a fourth lane per style */
    if (mods > 0) {
        int A = 15 + mods * 60 / 100, hi = 0;
        sq_mlane_t *m = out->m;
        m[0].dest = MD_LAD_CUT;
        m[1].dest = MD_SVF_CUT;
        if (mods >= 50) m[2].dest = MD_LAD_RES;
        if (mods >= 80) m[3].dest = style == SG_BASS ? MD_LAD_DRIVE : style == SG_RANDOM ? MD_FM_AMT : MD_PAN;
        for (int s = 0; s < len; s++) {
            int pos = s % span, v, vel = hit[s] ? out->n[s][0].vel : 0;
            if (shape == 0) v = -A + 2 * A * pos / (span > 1 ? span - 1 : 1);   /* opening over each chord */
            else if (shape == 1) v = hit[s] ? sg_clampi((vel - 92) * A / 35, -A, A) : -A / 3;   /* with the accents */
            else if (shape == 2) v = len > 1 ? -A + 2 * A * (len - 1 - abs(2 * s - (len - 1))) / (len - 1) : 0;   /* up and down */
            else v = (int)(mr[s % bar] >> 9) % (2 * A + 1) - A;   /* a stepped shape, repeating with the bar */
            m[0].off[s] = m[1].off[s] = (int8_t)sg_clampi(v, -100, 100);
            if (!hit[s]) continue;
            if (m[2].dest) m[2].off[s] = (int8_t)(vel > 100 ? A / 2 : 0);
            if (m[3].dest == MD_PAN) m[3].off[s] = (int8_t)(hi++ % 2 ? -A / 2 : A / 2);
            else if (m[3].dest == MD_LAD_DRIVE) m[3].off[s] = (int8_t)(vel > 100 ? A / 2 : 0);
            else if (m[3].dest) m[3].off[s] = (int8_t)((rs[s] >> 7) % (uint32_t)(A + 1));
        }
    }
    *seed = x;
    return count;
}
