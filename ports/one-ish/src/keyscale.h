/* The Synth's KEY and SCALE (key_root, key_scale; 2026-10-29): the notes the generators play in (the sequence
 * generator, the bouncing balls and particles). Each Synth has its own (s2_ / s3_ copies), so the Synths can play in
 * different keys or modes. Tables only: nothing here allocates, locks or touches a file. */
#pragma once
#include <stdint.h>

/* key_scale's options, in params.json's order */
enum { KS_MAJOR, KS_MINOR, KS_DORIAN, KS_PHRYGIAN, KS_LYDIAN, KS_MIXOLYDIAN, KS_LOCRIAN, KS_HARM_MINOR, KS_MEL_MINOR,
       KS_PENT_MAJOR, KS_PENT_MINOR, KS_BLUES, KS_WHOLE_TONE, KS_CHROMATIC, KS_COUNT };
static const uint8_t KS_STEPS[KS_COUNT][12] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 1, 3, 5, 6, 8, 10}, {0, 2, 3, 5, 7, 8, 11},
    {0, 2, 3, 5, 7, 9, 11}, {0, 2, 4, 7, 9}, {0, 3, 5, 7, 10}, {0, 3, 5, 6, 7, 10}, {0, 2, 4, 6, 8, 10},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
static const uint8_t KS_SIZE[KS_COUNT] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 6, 6, 12};

static inline int ks_clamp_scale(int scale) { return scale < 0 || scale >= KS_COUNT ? KS_MAJOR : scale; }

/* degree d of the scale (0 = the root; negative goes down) from MIDI note `base` (the root in some octave) */
static inline int ks_degree(int root, int scale, int base_octave, int d) {
    scale = ks_clamp_scale(scale);
    int n = KS_SIZE[scale], oct = d >= 0 ? d / n : -((-d + n - 1) / n), i = d - oct * n;
    return 12 * (base_octave + oct) + (root % 12) + KS_STEPS[scale][i];
}

/* the scale's note nearest MIDI note `note` (ties go down), clamped to 0..127 */
static inline int ks_quantize(int note, int root, int scale) {
    scale = ks_clamp_scale(scale);
    int best = note, dist = 99;
    for (int o = -1; o <= 1; o++)
        for (int i = 0; i < KS_SIZE[scale]; i++) {
            int c = 12 * (note / 12 + o) + (root % 12) + KS_STEPS[scale][i], dd = c > note ? c - note : note - c;
            if (dd < dist || (dd == dist && c < best)) { dist = dd; best = c; }
        }
    return best < 0 ? best + 12 * ((11 - best) / 12) : best > 127 ? best - 12 * ((best - 116) / 12) : best;
}
