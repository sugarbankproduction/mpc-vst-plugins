/* ONE-ish's performance section, the parts that don't touch the voices: the arpeggiator's note order (after the Moog
 * One manual, p78-81), the chord memory's shapes (p63), and the GENERATIVE section's own ideas (beyond the Moog One):
 * Euclidean rhythms (Bjorklund's algorithm), a looping random "Turing machine" sequence and its scales. engine.c owns
 * the clock, the keys and the notes; everything here is fixed-size, allocation-free and safe in the audio thread. */
#pragma once
#include <stdint.h>
#include <string.h>

#define SEQ_KEYS 16        /* keys the arpeggiator holds at once (as many as the engine's held-key list) */
#define SEQ_OCTS 6         /* octave offsets in one pass: 4 octaves Up/Down make 0, 1, 2, 3, 2, 1 */
#define SEQ_MAX (2 * SEQ_KEYS * SEQ_OCTS)   /* a whole pattern, pendulum tail included */
#define CHORD_MAX 6        /* notes in a chord: the root and up to five more */
#define EU_MAX 32          /* Euclidean steps */
#define TM_MAX 16          /* Turing machine length */

enum { ARP_UP, ARP_DOWN, ARP_ORDER, ARP_RANDOM };                 /* "arp_pattern" */
enum { ARP_OCT_UP, ARP_OCT_DOWN, ARP_OCT_UPDOWN, ARP_OCT_DOWNUP }; /* "arp_dir": where the extra octaves go */

/* the keys the arpeggiator plays from, oldest first (ORDER plays them so); `down`: the key itself is still held (not
 * only latched) */
typedef struct {
    int16_t note[SEQ_KEYS];
    uint8_t vel[SEQ_KEYS], down[SEQ_KEYS];
    int n;
} seq_keys_t;

/* xorshift32, its own sequence (the generative choices never disturb the voices' noise) */
static inline uint32_t seq_rand(uint32_t *x) {
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}
static inline float seq_rand01(uint32_t *x) { return (float)(seq_rand(x) >> 8) * (1.0f / 16777216.0f); }   /* 0 .. <1 */

static inline int seq_find(const seq_keys_t *k, int note) {
    for (int i = 0; i < k->n; i++)
        if (k->note[i] == note) return i;
    return -1;
}
static inline void seq_remove(seq_keys_t *k, int i) {
    k->n--;
    memmove(k->note + i, k->note + i + 1, sizeof k->note[0] * (k->n - i));
    memmove(k->vel + i, k->vel + i + 1, sizeof k->vel[0] * (k->n - i));
    memmove(k->down + i, k->down + i + 1, sizeof k->down[0] * (k->n - i));
}

/* ---- the arpeggiator's pattern ---------------------------------------------------------------------------------- */

static inline void seq_swap(int16_t *nt, uint8_t *vl, int a, int b) {
    int16_t t = nt[a];
    uint8_t u = vl[a];
    nt[a] = nt[b]; vl[a] = vl[b];
    nt[b] = t; vl[b] = u;
}

/* a new random order of the pass's L entries (Fisher-Yates); with avoid >= 0 the first is not that note, so a
 * non-repeating random pattern never plays a note twice in a row across passes either */
static inline void arp_shuffle(int16_t *nt, uint8_t *vl, int L, uint32_t *rng, int avoid) {
    for (int i = L - 1; i > 0; i--) seq_swap(nt, vl, i, (int)(seq_rand(rng) % (uint32_t)(i + 1)));
    if (avoid >= 0 && L > 1 && nt[0] == avoid) seq_swap(nt, vl, 0, 1 + (int)(seq_rand(rng) % (uint32_t)(L - 1)));
}

/* PENDULUM: the pass played back reversed after itself. ONCE plays the two end notes once (1 2 3 2 | 1 2 3 2), TWICE
 * twice (1 2 3 3 2 1 | 1 2 3 3 2 1). Writes the tail after the L entries; returns the whole length. */
static inline int arp_tail(int16_t *nt, uint8_t *vl, int L, int pendulum, int twice) {
    int n = L;
    if (!pendulum || L < 2) return L;
    for (int i = twice ? L - 1 : L - 2; i >= (twice ? 0 : 1); i--) { nt[n] = nt[i]; vl[n] = vl[i]; n++; }
    return n;
}

/* the whole pattern from the keys: the notes in PATTERN order (Up: lowest first; Down: highest first; Order: as
 * played; Random: shuffled), through OCTAVE octaves in DIRECTION (Up/Down climbs and comes back: 0 +1 +2 +1), either
 * every note in one octave before the next (OCTAVE ORDER off: C1 E1 G1 C2 E2 G2) or each note through the octaves
 * (on: C1 C2 E1 E2 G1 G2), then the PENDULUM tail. *base: the length of one pass without the tail. */
static inline int arp_build(const seq_keys_t *k, int pattern, int oct, int dir, int oct_order, int pendulum, int twice,
                            int16_t *nt, uint8_t *vl, int *base, uint32_t *rng, int avoid) {
    int16_t bn[SEQ_KEYS];
    uint8_t bv[SEQ_KEYS];
    int n = k->n, no = 0, L = 0, ol[SEQ_OCTS];
    *base = 0;
    if (n <= 0) return 0;
    memcpy(bn, k->note, sizeof bn[0] * n);
    memcpy(bv, k->vel, sizeof bv[0] * n);
    if (pattern == ARP_UP || pattern == ARP_DOWN)   /* insertion sort: stable, at most 16 keys */
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && (pattern == ARP_UP ? bn[j] < bn[j - 1] : bn[j] > bn[j - 1]); j--) seq_swap(bn, bv, j, j - 1);
    if (oct < 1) oct = 1;
    if (oct > 4) oct = 4;
    int sign = dir == ARP_OCT_DOWN || dir == ARP_OCT_DOWNUP ? -12 : 12;
    for (int i = 0; i < oct; i++) ol[no++] = sign * i;
    if (dir == ARP_OCT_UPDOWN || dir == ARP_OCT_DOWNUP)
        for (int i = oct - 2; i >= 1; i--) ol[no++] = sign * i;
    for (int a = 0; a < (oct_order ? n : no); a++)
        for (int b = 0; b < (oct_order ? no : n); b++) {
            int key = oct_order ? a : b, o = oct_order ? b : a;
            nt[L] = (int16_t)(bn[key] + ol[o]);
            vl[L++] = bv[key];
        }
    if (pattern == ARP_RANDOM) arp_shuffle(nt, vl, L, rng, avoid);
    *base = L;
    return arp_tail(nt, vl, L, pendulum, twice);
}

/* ---- chord memory --------------------------------------------------------------------------------------------- */

/* "chord_shape": index 0 is Custom (the learned chord, in chord_iv1..5); the others are built in, root first */
#define CHORD_SHAPES 15
static const int8_t CHORD_SHAPE[CHORD_SHAPES][CHORD_MAX] = {
    {0},                       /* Custom */
    {0, 4, 7}, {0, 3, 7}, {0, 2, 7}, {0, 5, 7}, {0, 3, 6}, {0, 4, 8},   /* Major Minor Sus2 Sus4 Dim Aug */
    {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10},                        /* Maj7 Min7 Dom7 */
    {0, 4, 7, 11, 14}, {0, 3, 7, 10, 14}, {0, 4, 7, 14},                /* Maj9 Min9 Add9 */
    {0, 7, 12}, {0, 12},                                                /* Power, Octaves */
};
static const uint8_t CHORD_SIZE[CHORD_SHAPES] = {1, 3, 3, 3, 3, 3, 3, 4, 4, 4, 5, 5, 4, 3, 2};

/* ---- Euclidean rhythm ----------------------------------------------------------------------------------------- */

/* Bjorklund's algorithm (as in Toussaint, "The Euclidean algorithm generates traditional musical rhythms"): `hits`
 * onsets spread as evenly as possible over `steps`, rotated so the pattern starts on an onset (E(3,8) = x..x..x.,
 * E(5,8) = x.xx.xx.). out: 1 = hit. No recursion: the levels are expanded with an explicit stack. */
static inline void euclid(int steps, int hits, uint8_t *out) {
    if (steps < 1) steps = 1;
    if (steps > EU_MAX) steps = EU_MAX;
    if (hits <= 0 || hits >= steps) { memset(out, hits > 0, (size_t)steps); return; }
    int counts[EU_MAX + 2], rem[EU_MAX + 2], level = 0, divisor = steps - hits;
    rem[0] = hits;
    do {
        counts[level] = divisor / rem[level];
        rem[level + 1] = divisor % rem[level];
        divisor = rem[level];
        level++;
    } while (rem[level] > 1);
    counts[level] = divisor;
    /* build(l): counts[l] x build(l - 1), then build(l - 2) if rem[l]; build(-1) = 0, build(-2) = 1. A stack of
     * pending levels does it in order: each entry is a level still to expand. */
    int stack[EU_MAX * 2 + 8], sp = 0, n = 0;
    uint8_t tmp[EU_MAX];
    stack[sp++] = level;
    while (sp > 0 && n < steps) {
        int l = stack[--sp];
        if (l == -1) { tmp[n++] = 0; continue; }
        if (l == -2) { tmp[n++] = 1; continue; }
        if (rem[l] && sp < (int)(sizeof stack / sizeof stack[0])) stack[sp++] = l - 2;   /* pushed first: expanded last */
        for (int i = 0; i < counts[l] && sp < (int)(sizeof stack / sizeof stack[0]); i++) stack[sp++] = l - 1;
    }
    while (n < steps) tmp[n++] = 0;   /* (never: the expansion is exactly `steps` long) */
    int first = 0;
    while (first < steps && !tmp[first]) first++;
    for (int i = 0; i < steps; i++) out[i] = tmp[(first + i) % steps];
}

/* ---- the Turing machine and its scales ------------------------------------------------------------------------ */

/* "gen_scale": index 0 plays the held keys themselves; the rest are scales from the lowest held key */
#define SCALES 9
static const uint8_t SCALE[SCALES][12] = {
    {0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},   /* Chromatic */
    {0, 2, 4, 5, 7, 9, 11},                   /* Major */
    {0, 2, 3, 5, 7, 8, 10},                   /* Minor */
    {0, 2, 3, 5, 7, 9, 10},                   /* Dorian */
    {0, 2, 4, 5, 7, 9, 10},                   /* Mixolydian */
    {0, 2, 4, 7, 9},                          /* Pentatonic major */
    {0, 3, 5, 7, 10},                         /* Pentatonic minor */
    {0, 3, 5, 6, 7, 10},                      /* Blues */
};
static const uint8_t SCALE_SIZE[SCALES] = {0, 12, 7, 7, 7, 7, 5, 5, 6};

/* one step of the looping random sequence: the value at the loop's current place (0..255), replaced by a new random
 * one with probability `change` (0: a locked loop that repeats exactly; 1: a new value every step), then on to the
 * next place */
static inline int tm_step(uint8_t *reg, int len, int *pos, float change, uint32_t *rng) {
    if (len < 2) len = 2;
    if (len > TM_MAX) len = TM_MAX;
    int i = *pos % len;
    if (change > 0.0f && seq_rand01(rng) < change) reg[i] = (uint8_t)(seq_rand(rng) >> 24);
    *pos = (i + 1) % len;
    return reg[i];
}

/* a Turing machine value (0..255) as a note over `range` octaves: with scale 0 one of the held keys (lowest first,
 * then the same an octave up ...), else a degree of the scale from the lowest held key */
static inline int tm_note(int v, int scale, int range, const seq_keys_t *k) {
    int16_t sorted[SEQ_KEYS];
    int n = k->n;
    if (n <= 0) return -1;
    if (range < 1) range = 1;
    memcpy(sorted, k->note, sizeof sorted[0] * n);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && sorted[j] < sorted[j - 1]; j--) { int16_t t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t; }
    if (scale <= 0 || scale >= SCALES) {
        int idx = v * n * range / 256;
        return sorted[idx % n] + 12 * (idx / n);
    }
    int m = SCALE_SIZE[scale], idx = v * m * range / 256;
    return sorted[0] + 12 * (idx / m) + SCALE[scale][idx % m];
}
