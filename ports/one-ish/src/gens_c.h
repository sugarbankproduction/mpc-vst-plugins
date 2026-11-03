/* ONE-ish's CHORDS and BASS note generators and the harmony they share (2026-11-02), in the GENERATIVE section beside the
 * others. This file is the music theory only: no engine, no params. Which sample a chord falls on, the keys and the
 * sharing between the Synths are engine.c's (src/gens_c_run.h).
 *
 * A PROGRESSION is a queue of items, each a chord named relative to the key: a scale degree (its quality comes from
 * stacking thirds in the scale, so it is in the key whatever the scale) or a root in semitones from the key's root with a
 * named quality (secondary dominants, borrowed chords). The queue fills a section at a time: a pattern from a table (by
 * how jazzy, major or minor), or a short random walk over the scale's degrees with weights that follow functional
 * harmony. JAZZ (0 .. 1) sets:
 *   - the pattern table: common triad progressions; ii-V-I motion; secondary dominants; "super jazz" (cycles of thirds,
 *     tritone substitutions, backdoor cadences, chromatic descents);
 *   - how far up the stack each chord goes: triads, then 7ths, 9ths, 11ths, 13ths;
 *   - what decorates a section: secondary dominants and ii-Vs into the next chord (from 40-50%), altered dominants and
 *     tritone substitutions (from 65-70%), borrowed chords (from 75%), chromatic passing chords (from 80%). A decoration
 *     takes half of the chord before it, so a section keeps its length in bars.
 * Items are built into notes when they play, so a new KEY or SCALE is heard from the next chord.
 *
 * VOICE LEADING: of every inversion in every octave, the one that moves the fewest semitones from the chord before,
 * pulled gently towards the middle of its register so it never drifts away.
 *
 * Fixed-size arrays, a seeded random sequence (seq.h's xorshift), no allocation, no locks, no files: safe in the audio
 * thread, and the same seed always gives the same chords. The theory is common knowledge; the tables are our own. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "keyscale.h"
#include "seq.h"

#define HM_NOTES 6         /* notes a chord plays at most */
#define HM_SECTION 8       /* chords in a section before decoration */
#define HM_Q 40            /* items waiting (a section is 16 at most, decorated) */

enum { HQ_DIA, HQ_MAJ, HQ_MIN, HQ_DOM, HQ_HALF, HQ_DIM7, HQ_MMAJ, HQ_ALT };   /* HQ_DIA: v a degree; the rest: v semitones */
enum { HF_FAST = 1, HF_SHARP11 = 2, HF_ALT_B9 = 4, HF_ALT_S5 = 8 };

typedef struct { int8_t v; uint8_t q, ext, flags; } hm_item_t;   /* ext: 0 triad, 1 7th, 2 9th, 3 11th, 4 13th */
typedef struct { int8_t v; uint8_t q; } hm_tok_t;
typedef struct { uint8_t n; hm_tok_t t[HM_SECTION]; } hm_pat_t;

typedef struct {
    int8_t root;           /* its pitch class, 0 .. 11 */
    int8_t third, fifth, seventh, ninth, eleventh, thirteenth;   /* semitones above the root; -1: not in it */
    uint8_t alt, n;        /* an altered dominant; notes it plays */
    int8_t off[HM_NOTES];  /* what it plays, semitones above the root, the root first */
    uint16_t tones, tens;  /* its pitch classes; the tensions a melody may add over it (FOLLOW CHORDS) */
} hm_chord_t;

typedef struct {
    hm_item_t q[HM_Q];
    int n, pos;            /* the items waiting: q[pos .. n-1] */
    hm_tok_t next[HM_SECTION];
    int nnext;             /* the next section, chosen ahead (the last chord leads into its first) */
    uint32_t rng;          /* never 0 */
    int deg;               /* the walk's last degree */
    int sections;
    float jazz;            /* what the queue was made with */
    int scale;
} hm_seq_t;

#define HQ(v, q) {(int8_t)(v), (uint8_t)(q)}
#define D_(d) HQ(d, HQ_DIA)
#define MA(s) HQ(s, HQ_MAJ)
#define MI(s) HQ(s, HQ_MIN)
#define DO(s) HQ(s, HQ_DOM)
#define HD(s) HQ(s, HQ_HALF)
#define DM(s) HQ(s, HQ_DIM7)
#define MM(s) HQ(s, HQ_MMAJ)
#define AL(s) HQ(s, HQ_ALT)
/* patterns in a major key: common triads; ii-V-I motion; secondary dominants; super jazz. Degrees 0 .. 6 stack in the
 * scale; the rest are semitones above the key's root */
static const hm_pat_t HM_MAJOR[4][8] = {
    {{4, {D_(0), D_(4), D_(5), D_(3)}}, {4, {D_(0), D_(3), D_(4), D_(3)}}, {4, {D_(5), D_(3), D_(0), D_(4)}},
     {4, {D_(0), D_(5), D_(3), D_(4)}}, {4, {D_(0), D_(3), D_(5), D_(4)}}, {4, {D_(3), D_(4), D_(0), D_(5)}},
     {4, {D_(0), D_(2), D_(3), D_(4)}}, {4, {D_(1), D_(4), D_(0), D_(5)}}},
    {{4, {D_(1), D_(4), D_(0), D_(5)}}, {4, {D_(0), D_(5), D_(1), D_(4)}}, {4, {D_(2), D_(5), D_(1), D_(4)}},
     {4, {D_(3), D_(2), D_(1), D_(0)}}, {4, {D_(0), D_(3), D_(2), D_(5)}}, {4, {D_(1), D_(4), D_(0), D_(3)}},
     {4, {D_(0), D_(1), D_(2), D_(3)}}, {6, {D_(3), D_(6), D_(2), D_(5), D_(1), D_(4)}}},
    {{4, {D_(0), DO(9), D_(1), D_(4)}}, {4, {D_(2), DO(9), D_(1), D_(4)}}, {4, {D_(0), DO(0), D_(3), MI(5)}},
     {4, {D_(1), D_(4), D_(0), DO(9)}}, {6, {D_(0), DO(4), D_(5), DO(2), D_(1), D_(4)}},
     {6, {D_(3), DM(6), D_(0), DO(9), D_(1), D_(4)}}, {4, {D_(5), DO(2), D_(1), D_(4)}},
     {8, {HD(11), DO(4), MI(9), DO(2), D_(1), D_(4), D_(0), D_(0)}}},
    {{8, {MA(0), DO(3), MA(8), DO(11), MA(4), DO(7), MA(0), MA(0)}}, {4, {D_(1), DO(1), D_(0), AL(9)}},
     {4, {MI(5), DO(10), MA(0), MA(8)}}, {5, {MI(4), DO(3), MI(2), DO(1), MA(0)}}, {4, {MA(0), DO(3), MA(8), DO(1)}},
     {8, {MA(0), HD(11), DO(4), MI(9), DO(2), MI(7), DO(0), MA(5)}},
     {8, {MA(0), DM(1), MI(2), DM(3), MI(4), DO(9), MI(2), AL(7)}}, {4, {MI(2), AL(7), MA(0), AL(9)}}}};
/* patterns in a minor key (a minor mode: the scale's own degrees); no raised notes at the first level */
static const hm_pat_t HM_MINOR[4][6] = {
    {{4, {D_(0), D_(5), D_(2), D_(6)}}, {4, {D_(0), D_(3), D_(4), D_(0)}}, {4, {D_(0), D_(6), D_(5), D_(6)}},
     {4, {D_(0), D_(3), D_(6), D_(2)}}, {4, {D_(5), D_(6), D_(0), D_(0)}}, {4, {D_(0), D_(2), D_(6), D_(3)}}},
    {{4, {D_(1), DO(7), D_(0), D_(5)}}, {4, {D_(0), D_(3), D_(1), DO(7)}}, {4, {D_(3), D_(6), D_(2), D_(5)}},
     {4, {D_(0), D_(5), D_(1), DO(7)}}, {6, {D_(3), D_(6), D_(2), D_(5), D_(1), DO(7)}}, {4, {D_(0), D_(6), D_(5), DO(7)}}},
    {{4, {D_(0), DO(0), D_(3), DO(7)}}, {4, {D_(1), DO(7), D_(0), DO(8)}}, {6, {D_(3), DO(10), D_(2), D_(5), D_(1), DO(7)}},
     {4, {MI(0), MM(0), MI(0), DO(9)}}, {6, {D_(0), DO(2), DO(7), D_(0), D_(5), DO(7)}}, {4, {D_(5), D_(1), DO(7), D_(0)}}},
    {{4, {HD(2), DO(1), MI(0), DO(8)}}, {6, {MI(0), DO(3), MA(8), DO(11), MA(4), AL(7)}}, {4, {HD(2), AL(7), MI(0), AL(2)}},
     {4, {MI(0), DO(1), MI(0), DO(8)}}, {4, {MA(8), DO(1), MI(0), MA(3)}}, {5, {MI(0), MI(11), MI(10), DO(9), DO(8)}}}};
#undef D_
#undef MA
#undef MI
#undef DO
#undef HD
#undef DM
#undef MM
#undef AL
#undef HQ

/* the walk: from a degree, how likely each next one (functional harmony: towards the dominant, then home) */
static const uint8_t HM_WALK[7][7] = {
    {0, 2, 1, 3, 3, 3, 0},   /* the tonic goes anywhere */
    {1, 0, 0, 1, 7, 0, 1},   /* the supertonic: to the dominant */
    {0, 1, 0, 2, 0, 5, 0},   /* the mediant: to the submediant */
    {3, 2, 0, 0, 4, 0, 1},   /* the subdominant: home, or on to the dominant */
    {7, 0, 0, 1, 0, 2, 0},   /* the dominant: home, now and then deceptive */
    {0, 4, 1, 3, 1, 0, 0},   /* the submediant: to the predominants */
    {5, 0, 1, 0, 0, 0, 0}};  /* the leading chord: home */

static const char *const HM_ROOTS[12] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

/* the 7-note scale (or the whole-tone one) a scale's chords are stacked in: pentatonic and blues use their parent scale */
static inline int hm_parent(int sc) {
    sc = ks_clamp_scale(sc);
    return sc == KS_PENT_MAJOR || sc == KS_CHROMATIC ? KS_MAJOR : sc == KS_PENT_MINOR || sc == KS_BLUES ? KS_MINOR : sc;
}
static inline int hm_is_minor(int parent) { return KS_STEPS[parent][2] == 3; }
static inline int hm_semi(int parent, int d) {   /* degree d in semitones above the root (past the octave too) */
    int n = KS_SIZE[parent];
    return KS_STEPS[parent][d % n] + 12 * (d / n);
}
static inline float hm_ramp(float x, float a, float b) { return x <= a ? 0.0f : x >= b ? 1.0f : (x - a) / (b - a); }

/* an item's root, in semitones above the key's root */
static inline int hm_item_semi(const hm_item_t *it, int parent) {
    return it->q == HQ_DIA ? hm_semi(parent, ((it->v % KS_SIZE[parent]) + KS_SIZE[parent]) % KS_SIZE[parent]) : it->v;
}

/* ---- an item as a chord ---- */
static inline void hm_add(hm_chord_t *c, int off) {
    if (off >= 0 && c->n < HM_NOTES) c->off[c->n++] = (int8_t)off;
}
static inline void hm_build(hm_chord_t *c, const hm_item_t *it, int tonic, int scale, float jazz) {
    int parent = hm_parent(scale), ext = it->ext, deg = 0, n9 = -1, n11 = -1, n13 = -1;
    memset(c, 0, sizeof *c);
    c->seventh = c->ninth = c->eleventh = c->thirteenth = -1;
    c->root = (int8_t)((tonic + hm_item_semi(it, parent)) % 12);
    if (it->q == HQ_DIA) {
        int sz = KS_SIZE[parent];
        deg = ((it->v % sz) + sz) % sz;
        int base = hm_semi(parent, deg);
        if (sz == 7) {
            c->third = (int8_t)(hm_semi(parent, deg + 2) - base);
            c->fifth = (int8_t)(hm_semi(parent, deg + 4) - base);
            c->seventh = (int8_t)(hm_semi(parent, deg + 6) - base);
            n9 = hm_semi(parent, deg + 8) - base;
            n11 = hm_semi(parent, deg + 10) - base;
            n13 = hm_semi(parent, deg + 12) - base;
        } else {   /* whole tone: augmented, a 7th, a 9th and a raised 11th all in the scale */
            c->third = 4; c->fifth = 8; c->seventh = 10; n9 = 14; n11 = 18;
        }
    } else {
        static const int8_t Q[8][6] = {   /* 3rd, 5th, 7th, 9th, 11th, 13th */
            {0, 0, 0, 0, 0, 0}, {4, 7, 11, 14, 18, 21}, {3, 7, 10, 14, 17, 21}, {4, 7, 10, 14, 17, 21},
            {3, 6, 10, -1, 17, -1}, {3, 6, 9, -1, -1, -1}, {3, 7, 11, 14, 17, -1}, {4, 7, 10, 13, -1, -1}};
        const int8_t *q = Q[it->q];
        c->third = q[0]; c->fifth = q[1]; c->seventh = q[2]; n9 = q[3]; n11 = q[4]; n13 = q[5];
        if (it->q == HQ_ALT) {   /* a flat or sharp 9th, a flat or sharp 5th */
            c->alt = 1;
            n9 = it->flags & HF_ALT_B9 ? 13 : 15;
            c->fifth = it->flags & HF_ALT_S5 ? 8 : 6;
            if (ext < 2) ext = 2;
        }
    }
    int dom = c->third == 4 && c->seventh == 10, minor = c->third == 3;
    int plain9 = n9 == 14 || dom;
    int plain11 = n11 == 18 || (minor && n11 == 17);
    if ((it->flags & HF_SHARP11) && c->third == 4) { n11 = 18; plain11 = 1; }   /* (a raised 11th now and then) */
    int plain13 = n13 == 21 && !(c->fifth == 6 && minor);
    if (ext < 1) c->seventh = -1;
    if (ext >= 2 && plain9 && n9 >= 0) c->ninth = (int8_t)n9;
    if (ext >= 3 && plain11 && n11 >= 0) c->eleventh = (int8_t)n11;
    if (ext >= 4 && plain13 && !c->alt) c->thirteenth = (int8_t)n13;
    if ((it->flags & HF_SHARP11) && c->third == 4) c->eleventh = 18;
    /* what it plays: the root and third always; a perfect 5th goes first when there are too many */
    int want = 3 + (c->seventh >= 0) + (c->ninth >= 0) + (c->eleventh >= 0) + (c->thirteenth >= 0);
    hm_add(c, 0);
    hm_add(c, c->third);
    if (c->fifth != 7 || want < HM_NOTES) hm_add(c, c->fifth);
    hm_add(c, c->seventh);
    hm_add(c, c->ninth);
    hm_add(c, c->eleventh);
    hm_add(c, c->thirteenth);
    for (int i = 0; i < c->n; i++) c->tones |= (uint16_t)(1u << ((c->root + c->off[i]) % 12));
    c->tones |= (uint16_t)(1u << ((c->root + c->fifth) % 12));
    /* the tensions a melody may add (from JAZZ 25%; below it the chord's own notes only) */
    if (jazz >= 0.25f) {
        unsigned t = 0;
        if (n9 >= 0 && plain9) t |= 1u << ((c->root + n9) % 12);
        if (n11 >= 0 && plain11) t |= 1u << ((c->root + n11) % 12);
        if (n13 >= 0 && plain13) t |= 1u << ((c->root + n13) % 12);
        if (c->alt) t |= 1u << ((c->root + 1) % 12) | 1u << ((c->root + 3) % 12) | 1u << ((c->root + 6) % 12) | 1u << ((c->root + 8) % 12);
        c->tens = (uint16_t)t;
    }
}

/* its name: Dm7, G13, Cmaj9, Bm7b5, G7alt, Db7#11, Bdim7 ... */
static inline int hm_name(const hm_chord_t *c, char *b, int n) {
    int top = c->thirteenth >= 0 ? 13 : c->eleventh >= 0 && (c->third == 3 || c->seventh < 0) ? 11 : c->ninth >= 0 ? 9 : 7;
    const char *r = HM_ROOTS[((c->root % 12) + 12) % 12];
    char s11[8] = "";
    if (c->eleventh == 18) snprintf(s11, sizeof s11, "#11");
    if (c->third == 3 && c->fifth == 6) {
        if (c->seventh == 9) return snprintf(b, n, "%sdim7", r);
        if (c->seventh == 10) return snprintf(b, n, "%sm7b5", r);
        return snprintf(b, n, "%sdim", r);
    }
    if (c->third == 4 && c->fifth == 8 && c->seventh < 0) return snprintf(b, n, "%saug", r);
    if (c->seventh < 0) return snprintf(b, n, "%s%s", r, c->third == 3 ? "m" : "");
    if (c->alt) return snprintf(b, n, "%s7alt", r);
    if (c->third == 3) return c->seventh == 11 ? snprintf(b, n, "%smMaj7", r) : snprintf(b, n, "%sm%d", r, top);
    if (c->seventh == 11) return snprintf(b, n, "%smaj%d%s", r, top == 11 ? 9 : top, s11);
    if (c->seventh == 10) {
        if (c->ninth == 13 || c->ninth == 15)
            return snprintf(b, n, "%s7%s%s%s", r, c->fifth == 8 ? "#5" : "", c->ninth == 13 ? "b9" : "#9", s11);
        return snprintf(b, n, "%s%s%s%s", r, top == 11 ? "9" : top == 13 ? "13" : top == 9 ? "9" : "7", c->fifth == 8 ? "#5" : "", s11);
    }
    return snprintf(b, n, "%s7", r);
}

/* ---- the progression ---- */
static inline void hm_seq_reset(hm_seq_t *q, uint32_t seed) {
    memset(q, 0, sizeof *q);
    q->rng = seed ? seed : 1u;
    q->jazz = -1.0f;
    q->scale = -1;
}
static inline int hm_pick_walk(hm_seq_t *q, int from) {
    int sum = 0, r;
    for (int k = 0; k < 7; k++) sum += HM_WALK[from][k];
    r = (int)(seq_rand(&q->rng) % (uint32_t)sum);
    for (int k = 0; k < 7; k++) {
        r -= HM_WALK[from][k];
        if (r < 0) return k;
    }
    return 0;
}
/* the next section's chords, in q->next */
static inline void hm_choose(hm_seq_t *q, float j, int parent) {
    int size = KS_SIZE[parent], minor = hm_is_minor(parent), band = j < 0.15f ? 0 : j < 0.4f ? 1 : j < 0.7f ? 2 : 3;
    hm_tok_t was = q->nnext ? q->next[q->nnext - 1] : (hm_tok_t){-1, HQ_DIA};   /* the section before ended here */
    if (q->sections > 0 && j >= 0.15f && seq_rand01(&q->rng) < 0.35f) {   /* a walk over the degrees, ending on the dominant */
        int d = q->deg;
        for (int k = 0; k < 4; k++) {
            d = k == 3 ? 4 : hm_pick_walk(q, d);
            q->next[k] = (hm_tok_t){(int8_t)(d % size), HQ_DIA};
        }
        q->deg = d;
        q->nnext = 4;
        q->sections++;
        return;
    }
    if (band > 1 && seq_rand01(&q->rng) < 0.25f) band--;   /* (now and then something plainer between) */
    const hm_pat_t *p = NULL;
    for (int t = 0; t < 4; t++) {   /* (a new section does not start on the chord the last one ended on) */
        p = minor ? &HM_MINOR[band][seq_rand(&q->rng) % 6] : &HM_MAJOR[band][seq_rand(&q->rng) % 8];
        if (p->t[0].v != was.v || p->t[0].q != was.q) break;
    }
    memcpy(q->next, p->t, sizeof p->t);
    q->nnext = p->n;
    for (int k = 0; k < q->nnext; k++)
        if (q->next[k].q == HQ_DIA) q->next[k].v = (int8_t)(q->next[k].v % size);
    q->deg = q->next[q->nnext - 1].q == HQ_DIA ? q->next[q->nnext - 1].v : 0;
    if (q->deg >= 7) q->deg = 0;
    q->sections++;
}
/* how far up the stack: triads, then 7ths at 20%, 9ths at 35%, 11ths at 50%, 13ths from 65% (mixed at the top) */
static inline int hm_ext(hm_seq_t *q, float j) {
    float r = seq_rand01(&q->rng);
    if (j >= 0.65f) return r < 0.6f ? 4 : r < 0.8f ? 3 : 2;
    float f = j < 0.2f ? j / 0.2f : 1.0f + (j - 0.2f) / 0.15f;
    if (j >= 0.3f) f += seq_rand01(&q->rng) - 0.5f;   /* (a mix, not every chord the same height) */
    int e = (int)f + (r < f - (int)f);
    return e > 4 ? 4 : e;
}
static inline void hm_push(hm_seq_t *q, int v, int kind, int ext, int flags) {
    if (q->n >= HM_Q) return;
    q->q[q->n++] = (hm_item_t){(int8_t)v, (uint8_t)kind, (uint8_t)ext, (uint8_t)flags};
}
/* a dominant: as it is, altered or by its tritone substitute */
static inline void hm_push_dom(hm_seq_t *q, float j, int v, int ext, int fast) {
    float r = seq_rand01(&q->rng), pa = 0.6f * hm_ramp(j, 0.65f, 1.0f), pt = 0.45f * hm_ramp(j, 0.7f, 1.0f);
    int f = fast ? HF_FAST : 0;
    if (r < pa) {
        hm_push(q, ((v % 12) + 12) % 12, HQ_ALT, ext, f | (seq_rand01(&q->rng) < 0.5f ? HF_ALT_B9 : 0) |
                                                     (seq_rand01(&q->rng) < 0.5f ? HF_ALT_S5 : 0));
    } else if (r < pa + pt) {
        hm_push(q, ((v + 6) % 12 + 12) % 12, HQ_DOM, ext < 3 ? 3 : ext, f | HF_SHARP11);
    } else {
        hm_push(q, ((v % 12) + 12) % 12, HQ_DOM, ext, f);
    }
}
/* a section's chords into the queue, decorated by JAZZ; `next`: the chord after the last one */
static inline void hm_decorate(hm_seq_t *q, const hm_tok_t *t, int n, float j, int parent) {
    int size = KS_SIZE[parent], minor = hm_is_minor(parent);
    for (int i = 0; i < n; i++) {
        hm_tok_t tk = t[i], nx = i + 1 < n ? t[i + 1] : q->next[0];
        int ext = hm_ext(q, j), s11 = seq_rand01(&q->rng) < 0.2f * hm_ramp(j, 0.5f, 1.0f) ? HF_SHARP11 : 0;
        hm_item_t a = {tk.v, tk.q, 0, 0}, b = {nx.v, nx.q, 0, 0};
        int rs = hm_item_semi(&a, parent), target = hm_item_semi(&b, parent);
        /* borrowed chords (modal interchange), from 75% */
        if (tk.q == HQ_DIA && j >= 0.75f && seq_rand01(&q->rng) < 0.4f * hm_ramp(j, 0.75f, 1.0f) && size == 7) {
            if (!minor && tk.v == 3) { tk.q = HQ_MIN; tk.v = 5; }
            else if (!minor && tk.v == 5) { tk.q = HQ_MAJ; tk.v = 8; }
            else if (!minor && tk.v == 6) { tk.q = HQ_DOM; tk.v = 10; }
            else if (minor && tk.v == 3) { tk.q = HQ_DOM; tk.v = 5; }
            else if (minor && tk.v == 0) { tk.q = HQ_MAJ; tk.v = 0; }
        }
        a.v = tk.v; a.q = tk.q;
        rs = hm_item_semi(&a, parent);
        /* what leads into the next chord takes half of this one */
        float r = seq_rand01(&q->rng);
        float p_sec = 0.30f * hm_ramp(j, 0.4f, 0.9f), p_iiv = 0.25f * hm_ramp(j, 0.5f, 0.9f);
        float p_tri = 0.30f * hm_ramp(j, 0.8f, 1.0f), p_dim = 0.25f * hm_ramp(j, 0.8f, 1.0f);
        int same = ((target - rs) % 12 + 12) % 12 == 0;
        int up_tone = ((target - rs) % 12 + 12) % 12 == 2;
        int dom_v = target + 7;   /* the next chord's dominant */
        int isdom = tk.q == HQ_DOM || tk.q == HQ_ALT;
        int decor = same || i + 1 >= n ? 0 : r < p_sec ? 1 : r < p_sec + p_iiv ? 2 : r < p_sec + p_iiv + p_tri ? 3 :
                    r < p_sec + p_iiv + p_tri + p_dim && up_tone ? 4 : 0;
        if (decor == 2) {   /* a ii-V into the next chord in place of this one */
            hm_push(q, ((target + 2) % 12 + 12) % 12, HQ_MIN, ext > 3 ? 3 : ext, HF_FAST);
            hm_push_dom(q, j, dom_v, ext, 1);
            continue;
        }
        int fast = decor ? HF_FAST : 0;
        if (isdom || (tk.q == HQ_DIA && size == 7 && hm_semi(parent, tk.v + 2) - rs == 4 && hm_semi(parent, tk.v + 6) - rs == 10)) {
            hm_push_dom(q, j, rs, ext, decor != 0);
        } else {
            hm_push(q, tk.v, tk.q, ext, fast | s11);
        }
        if (decor == 1) hm_push_dom(q, j, dom_v, ext, 1);   /* its secondary dominant */
        else if (decor == 3) hm_push(q, ((target + 1) % 12 + 12) % 12, HQ_DOM, ext < 3 ? 3 : ext, HF_FAST | HF_SHARP11);   /* from a semitone above */
        else if (decor == 4) hm_push(q, ((rs + 1) % 12 + 12) % 12, HQ_DIM7, 1, HF_FAST);   /* a chromatic passing chord */
    }
}
/* keep at least two items waiting (the one to play, and the one after, for the bass to walk to) */
static inline void hm_seq_fill(hm_seq_t *q, float j, int scale) {
    int parent = hm_parent(scale);
    if (fabsf(j - q->jazz) > 0.04f || q->scale != parent) {   /* JAZZ, or the kind of scale, moved: fresh from here */
        q->n = q->pos = 0;
        q->nnext = 0;
        q->jazz = j;
        q->scale = parent;
    }
    if (!q->nnext) hm_choose(q, j, parent);
    while (q->n - q->pos < 2) {
        if (q->pos) {
            memmove(q->q, q->q + q->pos, sizeof(hm_item_t) * (size_t)(q->n - q->pos));
            q->n -= q->pos;
            q->pos = 0;
        }
        hm_tok_t now[HM_SECTION];
        int nn = q->nnext;
        memcpy(now, q->next, sizeof now);
        hm_choose(q, j, parent);
        hm_decorate(q, now, nn, j, parent);
    }
}
/* the next item, and the root (a pitch class relative to the key) of the one after it */
static inline hm_item_t hm_seq_next(hm_seq_t *q, float j, int scale, int *next_semi) {
    hm_seq_fill(q, j, scale);
    hm_item_t it = q->q[q->pos++];
    hm_seq_fill(q, j, scale);
    *next_semi = hm_item_semi(&q->q[q->pos], hm_parent(scale));
    return it;
}

/* ---- voicing ---- */
enum { HV_CLOSE, HV_OPEN, HV_DROP2, HV_SPREAD };   /* "chd_voicing" */
enum { HI_ROOT, HI_SMOOTH, HI_CLIMB, HI_FALL };    /* "chd_inv" */

static inline void hm_sort(uint8_t *a, int n) {
    for (int i = 1; i < n; i++) {
        uint8_t v = a[i];
        int k = i - 1;
        for (; k >= 0 && a[k] > v; k--) a[k + 1] = a[k];
        a[k + 1] = v;
    }
}
/* the semitones between two chords, note for note (each note to the nearest of the other when their sizes differ) */
static inline int hm_move(const uint8_t *a, int na, const uint8_t *b, int nb) {
    int sum = 0;
    if (na == nb) {
        for (int i = 0; i < na; i++) sum += a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
        return sum;
    }
    const uint8_t *x = na > nb ? a : b, *y = na > nb ? b : a;
    int nx = na > nb ? na : nb, ny = na > nb ? nb : na;
    for (int i = 0; i < nx; i++) {
        int best = 127;
        for (int k = 0; k < ny; k++) {
            int d = x[i] > y[k] ? x[i] - y[k] : y[k] - x[i];
            if (d < best) best = d;
        }
        sum += best;
    }
    return sum;
}
/* inversion `inv` of the chord, its lowest note in octave `oct` (MIDI 12 x oct up), shaped by the voicing, sorted */
static inline int hm_shape(const hm_chord_t *c, int voicing, int inv, int oct, uint8_t *out) {
    int pc[HM_NOTES], n = c->n, m[HM_NOTES];
    for (int i = 0; i < n; i++) pc[i] = c->off[i] % 12;
    for (int i = 1; i < n; i++) {   /* up from the root, within the octave */
        int v = pc[i], k = i - 1;
        for (; k >= 0 && pc[k] > v; k--) pc[k + 1] = pc[k];
        pc[k + 1] = v;
    }
    int base = 12 * oct + (c->root + pc[inv % n]) % 12, last = base;
    m[0] = base;
    for (int i = 1; i < n; i++) {
        int want = (c->root + pc[(inv + i) % n]) % 12, x = last + ((want - last) % 12 + 12) % 12;
        if (x == last) x += 12;
        m[i] = x;
        last = x;
    }
    if (voicing == HV_OPEN) for (int i = 1; i < n; i += 2) m[i] += 12;
    else if (voicing == HV_DROP2 && n >= 3) m[n - 2] -= 12;
    else if (voicing == HV_SPREAD && n >= 2) { m[0] -= 12; m[n - 1] += 12; }
    for (int i = 0; i < n; i++) {
        if (m[i] < 0 || m[i] > 127) return 0;
        out[i] = (uint8_t)m[i];
    }
    hm_sort(out, n);
    return n;
}
/* the chord's notes: the inversion (and octave) that moves least from prev, near `centre`; returns how many */
static inline int hm_voice(const hm_chord_t *c, int voicing, int mode, int centre, const uint8_t *prev, int np, uint8_t *out) {
    int bestn = 0, ninv = mode == HI_ROOT ? 1 : c->n;
    float best = 1e9f, pull = np && mode != HI_ROOT ? (mode == HI_SMOOTH ? 0.35f : 1.2f) : 2.0f;
    uint8_t cand[HM_NOTES];
    for (int inv = 0; inv < ninv; inv++)
        for (int oct = centre / 12 - 2; oct <= centre / 12 + 1; oct++) {
            int n = hm_shape(c, voicing, inv, oct, cand);
            if (!n) continue;
            int sum = 0;
            for (int i = 0; i < n; i++) sum += cand[i];
            float mean = (float)sum / n, far = mean > centre ? mean - centre : centre - mean;
            float score = pull * far + (np && mode != HI_ROOT ? (float)hm_move(prev, np, cand, n) : 0.0f);
            if (score < best) {
                best = score;
                bestn = n;
                memcpy(out, cand, (size_t)n);
            }
        }
    return bestn;
}

/* FOLLOW CHORDS: a note to the nearest of the pitch classes in mask (ties go down), kept in 0 .. 127 */
static inline int hm_snap(int note, unsigned mask) {
    if (!(mask & 0xFFFu)) return note;
    for (int d = 0; d <= 11; d++) {
        if (note - d >= 0 && (mask >> ((note - d) % 12) & 1u)) return note - d;
        if (note + d <= 127 && (mask >> ((note + d) % 12) & 1u)) return note + d;
    }
    return note;
}

/* ---- the bass line ---- */
enum { HB_AUTO, HB_ROOT, HB_FIFTH, HB_OCTAVE, HB_ARP, HB_WALK };   /* "bass_pattern" */
typedef struct { int last, pos, pat; uint32_t rng; } hm_bass_t;

/* the note in [lo, lo + 19] nearest `from` with pitch class pc */
static inline int hm_near(int pc, int from, int lo) {
    int best = lo + ((pc - lo) % 12 + 12) % 12, bd = 99;
    for (int x = best; x <= lo + 19; x += 12) {
        int d = x > from ? x - from : from - x;
        if (d < bd) { bd = d; best = x; }
    }
    return best;
}
/* a new chord: which way this chord's notes go (AUTO: by the adventure, from the root alone up to walking) */
static inline void hm_bass_chord(hm_bass_t *b, int pattern, float adv) {
    b->pos = 0;
    if (pattern != HB_AUTO) { b->pat = pattern; return; }
    float x = adv + (seq_rand01(&b->rng) - 0.5f) * 0.25f;
    b->pat = x < 0.15f ? HB_ROOT : x < 0.35f ? HB_FIFTH : x < 0.5f ? HB_OCTAVE : x < 0.72f ? HB_ARP : HB_WALK;
}
/* the next note: c the chord now, next_root the next one's pitch class (-1: not known), steps_left the steps until it
 * (<= 0: not known), lo the line's lowest note, scale a pitch-class mask of the key */
static inline int hm_bass_next(hm_bass_t *b, const hm_chord_t *c, int next_root, int steps_left, int lo, float adv, unsigned scale) {
    int root = lo + ((c->root - lo) % 12 + 12) % 12, pos = b->pos++, note = root;
    int fifth = c->fifth > 0 ? c->fifth : 7, third = c->third > 0 ? c->third : 4, sev = c->seventh > 0 ? c->seventh : 12;
    switch (b->pat) {
    case HB_ROOT: break;
    case HB_FIFTH:
        if (pos & 1) note = root + fifth > lo + 16 ? root + fifth - 12 : root + fifth;
        break;
    case HB_OCTAVE:
        if (pos & 1) note = root + 12;
        break;
    case HB_ARP: {
        int a[4] = {0, third, fifth, sev};
        note = root + a[pos % 4];
        if (note > lo + 19) note -= 12;
        break;
    }
    default: {   /* WALKING */
        if (pos == 0) break;
        int last = b->last;
        if (next_root >= 0 && steps_left == 1) {   /* the approach: from a semitone or a scale step away, or its dominant */
            int t = hm_near(next_root, last, lo), r = (int)(seq_rand(&b->rng) % 100u);
            if (adv >= 0.85f && r < 30) note = t + 7 > lo + 19 ? t - 5 : t + 7;
            else if (adv >= 0.5f) note = r % 2 ? t + 1 : t - 1;
            else {
                note = t - 1;
                for (int d = 1; d <= 2; d++) {
                    if ((scale >> ((t - d + 120) % 12) & 1u) && t - d != last) { note = t - d; break; }
                    if ((scale >> ((t + d) % 12) & 1u) && t + d != last) { note = t + d; break; }
                }
            }
            if (note == last) note = last > t ? t + 1 : t - 1;
            break;
        }
        unsigned mask = c->tones | c->tens;
        if ((c->tones & scale) == c->tones) mask |= scale;
        int t = next_root >= 0 ? hm_near(next_root, last, lo) : root;
        int dir = t > last ? 1 : t < last ? -1 : seq_rand(&b->rng) & 1u ? 1 : -1;
        if (steps_left > 2 && seq_rand01(&b->rng) < 0.3f) dir = -dir;   /* (a line, not a straight run) */
        if (last + 4 * dir > lo + 19) dir = -1;
        if (last + 4 * dir < lo) dir = 1;
        note = last + dir;
        for (int d = 1; d <= 4; d++) {
            int x = last + dir * d;
            if (mask >> ((x + 120) % 12) & 1u) { note = x; break; }
        }
        break;
    }
    }
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    b->last = note;
    return note;
}
/* a scale as a pitch-class mask */
static inline unsigned hm_scale_mask(int root, int scale) {
    unsigned m = 0;
    scale = ks_clamp_scale(scale);
    for (int i = 0; i < KS_SIZE[scale]; i++) m |= 1u << ((root + KS_STEPS[scale][i]) % 12);
    return m;
}

/* ---- the harmony the Synths share (src/gens_c_run.h): one per instance, audio thread only ---- */
typedef struct {
    int leader;            /* the lowest Synth with CHORDS on (-1: none) */
    int valid;             /* a chord is sounding: FOLLOW CHORDS and BASS follow it */
    uint32_t gen;          /* counts the chords */
    hm_chord_t cur;
    int next_root;         /* the next chord's pitch class */
    int64_t t_change;      /* the sample it comes at */
} hm_share_t;

/* ---- the engine's side (src/gens_c_run.h): one per Synth, in its one_t; runtime state only, never saved ---- */
#define GC_STEP 64         /* the clocks look for their next line every 64 samples; a note falls on its own sample */
#define GC_Q (HM_NOTES + 2)
#define GC_NAMES 5         /* the chord names the window shows: the one now, the four before */
#define GC_NAME_LEN 12
#define GC_BASS_HIST 16    /* the bass notes the window shows */
#define GC_LOG 64
enum { GC_CHD = 1, GC_BASS = 2 };

typedef struct { int64_t at, off; uint8_t note, vel; } gc_note_t;
typedef struct {
    int on, chd_mode, bass_mode, takes;
    double b0, bps0;         /* the beat clock, as the other generators' */
    int64_t t0, sp_at;
    int follow;
    /* CHORDS */
    int64_t ct_next;
    hm_seq_t seq;
    hm_chord_t cur;
    uint8_t prev[HM_NOTES];
    int nprev, cfirst, cseed, cgo, ckey, cleft, cwas;
    double cnext, canchor, cblast;
    int64_t clen;            /* the chord now, in samples */
    float centre;            /* CLIMB, FALL: where it is heading */
    gc_note_t cq[GC_Q];
    int ncq;
    /* BASS */
    int64_t bt_next;
    /* (2026-11-03) FEED, [0] CHORDS [1] BASS: the notes for the keys, when; still held by the Synths */
    int64_t fd_at[2], fd_until[2];
    uint8_t fd_note[2][HM_NOTES];
    int fd_n[2], fd_vel[2], fd_wait[2], fd_held[2];
    hm_bass_t bl;
    hm_seq_t bw;             /* no CHORDS to follow: its own plain chords */
    hm_chord_t bwc;
    int bwnext;
    double bwend;
    int bfirst, bseed, bgo, bkey, bleft, bwas;
    uint32_t bgen;
    double bnext, banchor, bblast;
    gc_note_t bq[2];
    int nbq;
    /* the window's views, written by the audio thread without a lock: each counter odd while it is written */
    uint32_t cvseq, bvseq;
    char vname[GC_NAMES][GC_NAME_LEN];
    uint8_t vnotes[HM_NOTES], vnn, vbh[GC_BASS_HIST], vbn;
    /* what played, for the tests ("chd_log": read in the audio thread's stead, as gen_log) */
    struct { int64_t t; uint16_t tones, tens; int8_t root; char name[GC_NAME_LEN]; } log[GC_LOG];
    int log_head, log_n;
} gc_run_t;
