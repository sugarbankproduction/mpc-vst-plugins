/* (2026-11-02) CHORDS, BASS, FOLLOW CHORDS (src/gens_c.h, src/gens_c_run.h):
 *   - JAZZ 0: triads in the key, for every 7-note scale; rising JAZZ: more notes in each chord and more notes outside the
 *     scale, measured over many seeds; the progressions printed as chord names, to read;
 *   - voice leading moves fewer semitones than root-position chords;
 *   - the same seed plays the same chords, another seed others;
 *   - BASS under the chords and on their roots, on another Synth too; without CHORDS in its KEY and SCALE;
 *   - FOLLOW CHORDS: every generator's notes in the chord or its tensions of the moment, whichever Synth leads, also on the
 *     very sample of a change; off, or no CHORDS: the notes as they were;
 *   - two CHORDS: the higher Synth plays the leader's chords;
 *   - WILD raises JAZZ through RANDOMISE and MUTATE, with GENERATIVE lit; not lit: untouched;
 *   - nothing allocates; no stuck notes on stop; KEYS (Held, Launch); own KEY and SCALE; off is exactly off.
 * chords_wavs: demo WAVs to listen to, with the notes analysed and the chords printed.
 * Included from offline_check.c after own_keys_check.c (helpers: pg_*, set, setkv, note, check, get, pindex). */
#include <malloc.h>
#include "../src/gens_c.h"
static const char *const KS_NAMES_T[KS_COUNT] = {"major", "minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian", "harmonic minor", "melodic minor", "pent major", "pent minor", "blues", "whole tone", "chromatic"};

#define CC_MAX 256
typedef struct { long long t; int root; unsigned tones, tens; char name[16]; } cc_chord_t;
extern size_t __sanitizer_get_current_allocated_bytes(void) __attribute__((weak));
static size_t cc_heap(void) {
    if (__sanitizer_get_current_allocated_bytes) return __sanitizer_get_current_allocated_bytes();
    struct mallinfo2 m = mallinfo2();
    return m.uordblks + m.hblkhd;
}

/* run `secs`, the note-ons of Synth `pre` ("" or "s2_") into ev, the chords of Synth `cpre` into c */
static void cch_run(void *e, double secs, const char *pre, pg_ev_t *ev, int *n, const char *cpre, cc_chord_t *c, int *nc) {
    int16_t buf[256];
    static char b[8192];
    char kn[32], kc[32];
    snprintf(kn, sizeof kn, "%sgen_log", pre);
    snprintf(kc, sizeof kc, "%schd_log", cpre);
    for (long at = 0; at < (long)(secs * SR); at += 128) {
        E->render(e, buf, 128);
        if (ev && E->get_param(e, kn, b, sizeof b) > 0)
            for (char *p = b; *p;) {
                long long t;
                int nt, v, used;
                if (sscanf(p, "%lld:%d:%d;%n", &t, &nt, &v, &used) != 3) break;
                if (v > 0 && *n < PG_MAX) ev[(*n)++] = (pg_ev_t){t, nt, v};
                p += used;
            }
        if (c && E->get_param(e, kc, b, sizeof b) > 0)
            for (char *p = b; *p;) {
                long long t;
                int r, to, te, used;
                char nm[16];
                if (sscanf(p, "%lld:%d:%d:%d:%15[^;];%n", &t, &r, &to, &te, nm, &used) != 5) break;
                if (*nc < CC_MAX) {
                    c[*nc] = (cc_chord_t){t, r, (unsigned)to, (unsigned)te, ""};
                    snprintf(c[*nc].name, sizeof c[*nc].name, "%s", nm);
                    (*nc)++;
                }
                p += used;
            }
    }
}
static const cc_chord_t *cc_at(const cc_chord_t *c, int nc, long long t) {   /* the chord sounding at sample t */
    const cc_chord_t *f = NULL;
    for (int i = 0; i < nc && c[i].t <= t; i++) f = &c[i];
    return f;
}
static void cc_names(const cc_chord_t *c, int nc, int max, char *out, int n) {
    int used = 0;
    out[0] = 0;
    for (int i = 0; i < nc && i < max && used < n - 18; i++) used += snprintf(out + used, n - used, "%s%s", i ? " " : "", c[i].name);
}
static pg_ev_t cc_ev[PG_MAX], cc_ev2[PG_MAX];
static cc_chord_t cc_c[CC_MAX], cc_c2[CC_MAX];

/* per chord: notes played, and the share of the notes outside the scale, at this JAZZ over many seeds */
static void cc_measure(int jazz, int root, int scale, double *notes, double *out, double *names7) {
    long nnotes = 0, nchords = 0, outside = 0, seventh = 0;
    for (int seed = 1; seed <= 6; seed++) {
        void *a = pg_patch();
        char kv[160];
        snprintf(kv, sizeof kv, "chd_on=1,chd_jazz=%d,chd_seed=%d,key_root=%d,key_scale=%d,chd_rate=2", jazz, seed, root, scale);
        setkv(a, kv);
        int n = 0, nc = 0;
        cch_run(a, 40.0, "", cc_ev, &n, "", cc_c, &nc);
        E->destroy(a);
        unsigned sm = hm_scale_mask(root, scale);
        for (int i = 0; i < n; i++) outside += !(sm >> (cc_ev[i].note % 12) & 1u);
        nnotes += n;
        nchords += nc;
        for (int i = 0; i < nc; i++) seventh += __builtin_popcount(cc_c[i].tones) >= 4;
    }
    *notes = nchords ? (double)nnotes / nchords : 0;
    *out = nnotes ? (double)outside / nnotes : 0;
    *names7 = nchords ? (double)seventh / nchords : 0;
}

static void chords_checks(void) {
    char what[900], names[600];
    /* off is exactly off: every new setting moved, not switched on */
    {
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, "chd_jazz=80,chd_rate=4,chd_voicing=2,chd_strum=50,chd_seed=9,bass_jazz=90,bass_pattern=5,bass_rate=3,gen_follow_chords=1,bass_follow=0");
        note(a, 1, 60, 100);
        note(b, 1, 60, 100);
        int same = 1;
        int16_t ba[256], bb[256];
        for (int k = 0; k < 400; k++) {
            E->render(a, ba, 128);
            E->render(b, bb, 128);
            same &= !memcmp(ba, bb, sizeof ba);
        }
        E->destroy(a); E->destroy(b);
        void *e = E->create(NULL);
        int defaults = get(e, "chd_on") == 0 && get(e, "bass_on") == 0 && get(e, "gen_follow_chords") == 0 && get(e, "bass_follow") == 1 &&
                       get(e, "s2_gen_follow_chords") == 0 && get(e, "s3_bass_follow") == 1;
        E->destroy(e);
        check(same && defaults, "CHORDS, BASS off with every setting moved: the very same samples; new sound: off, FOLLOW CHORDS off, "
              "BASS's FOLLOW on");
    }
    /* JAZZ 0: triads in the key, every 7-note scale */
    {
        static const int SC[] = {KS_MAJOR, KS_MINOR, KS_DORIAN, KS_PHRYGIAN, KS_LYDIAN, KS_MIXOLYDIAN, KS_LOCRIAN, KS_HARM_MINOR, KS_MEL_MINOR};
        for (unsigned k = 0; k < sizeof SC / sizeof SC[0]; k++) {
            void *a = pg_patch();
            char kv[120];
            snprintf(kv, sizeof kv, "chd_on=1,chd_jazz=0,key_root=2,key_scale=%d,chd_rate=3", SC[k]);
            setkv(a, kv);
            int n = 0, nc = 0;
            cch_run(a, 30.0, "", pg_a, &n, "", cc_c, &nc);
            E->destroy(a);
            int triads = nc > 10;
            for (int i = 0; i < nc; i++) triads &= __builtin_popcount(cc_c[i].tones) == 3;
            int diff = 0;
            for (int i = 1; i < nc; i++) diff += cc_c[i].root != cc_c[i - 1].root;
            snprintf(what, sizeof what, "CHORDS JAZZ 0, D %s: %d chords all triads (%d), %d notes all in the key (%d), the root moves (%d changes)",
                     KS_NAMES_T[SC[k]], nc, triads, n, pg_in_key(pg_a, n, 2, SC[k]), diff);
            check(triads && pg_in_key(pg_a, n, 2, SC[k]) && diff >= nc / 3, what);
        }
    }
    /* the progressions, to read */
    {
        static const struct { const char *name; int root, scale; } K[] = {{"C major", 0, KS_MAJOR}, {"A minor", 9, KS_MINOR}};
        for (int kk = 0; kk < 2; kk++)
            for (int j = 0; j <= 100; j += 25) {
                void *a = pg_patch();
                char kv[120];
                snprintf(kv, sizeof kv, "chd_on=1,chd_jazz=%d,key_root=%d,key_scale=%d,chd_seed=3", j, K[kk].root, K[kk].scale);
                setkv(a, kv);
                int n = 0, nc = 0;
                cch_run(a, 40.0, "", NULL, &n, "", cc_c, &nc);
                E->destroy(a);
                cc_names(cc_c, nc, 20, names, sizeof names);
                printf("chords: %s, JAZZ %3d: %s\n", K[kk].name, j, names);
            }
    }
    /* rising JAZZ: more chord tones, more notes outside the scale */
    {
        double nt[5], ou[5], s7[5];
        for (int k = 0; k < 5; k++) cc_measure(k * 25, 0, KS_MAJOR, &nt[k], &ou[k], &s7[k]);
        snprintf(what, sizeof what, "CHORDS rising JAZZ 0/25/50/75/100: notes a chord %.2f %.2f %.2f %.2f %.2f; chords of 4 notes or more "
                 "%.0f%% %.0f%% %.0f%% %.0f%% %.0f%%; notes outside the scale %.1f%% %.1f%% %.1f%% %.1f%% %.1f%%", nt[0], nt[1], nt[2], nt[3], nt[4],
                 100 * s7[0], 100 * s7[1], 100 * s7[2], 100 * s7[3], 100 * s7[4], 100 * ou[0], 100 * ou[1], 100 * ou[2], 100 * ou[3], 100 * ou[4]);
        check(nt[0] < 3.01 && nt[1] > nt[0] + 0.5 && nt[2] > nt[1] && nt[4] > nt[1] && s7[0] == 0.0 && s7[2] > 0.9 && ou[0] == 0.0 &&
              ou[2] > ou[1] && ou[4] > ou[2] && ou[4] > 0.2, what);
        double a[2], b[2], c[2];
        cc_measure(0, 9, KS_MINOR, &a[0], &b[0], &c[0]);
        cc_measure(100, 9, KS_MINOR, &a[1], &b[1], &c[1]);
        snprintf(what, sizeof what, "CHORDS in A minor: JAZZ 0: %.2f notes a chord, %.1f%% outside; JAZZ 100: %.2f, %.1f%%", a[0], 100 * b[0], a[1], 100 * b[1]);
        check(a[0] < 3.01 && b[0] == 0.0 && a[1] > 4.0 && b[1] > 0.2, what);
    }
    /* the theory's names */
    {
        hm_chord_t c;
        char nm[16];
        hm_build(&c, &(hm_item_t){1, HQ_DIA, 1, 0}, 0, KS_MAJOR, 0.3f); hm_name(&c, nm, sizeof nm);
        int ok = !strcmp(nm, "Dm7");
        hm_build(&c, &(hm_item_t){4, HQ_DIA, 1, 0}, 0, KS_MAJOR, 0.3f); hm_name(&c, nm, sizeof nm); ok &= !strcmp(nm, "G7");
        hm_build(&c, &(hm_item_t){0, HQ_DIA, 2, 0}, 0, KS_MAJOR, 0.3f); hm_name(&c, nm, sizeof nm); ok &= !strcmp(nm, "Cmaj9");
        hm_build(&c, &(hm_item_t){6, HQ_DIA, 1, 0}, 0, KS_MAJOR, 0.3f); hm_name(&c, nm, sizeof nm); ok &= !strcmp(nm, "Bm7b5");
        hm_build(&c, &(hm_item_t){6, HQ_DIA, 0, 0}, 0, KS_MAJOR, 0.0f); hm_name(&c, nm, sizeof nm); ok &= !strcmp(nm, "Bdim");
        hm_build(&c, &(hm_item_t){7, HQ_ALT, 2, HF_ALT_B9 | HF_ALT_S5}, 0, KS_MAJOR, 1.0f); hm_name(&c, nm, sizeof nm); ok &= !strcmp(nm, "G7alt") && c.tones >> 8 & 1u;
        hm_build(&c, &(hm_item_t){1, HQ_DOM, 3, HF_SHARP11}, 0, KS_MAJOR, 1.0f); hm_name(&c, nm, sizeof nm); ok &= !strcmp(nm, "Db9#11");
        hm_build(&c, &(hm_item_t){7, HQ_DOM, 4, 0}, 0, KS_MAJOR, 0.7f); hm_name(&c, nm, sizeof nm); ok &= !strcmp(nm, "G13#11") || !strcmp(nm, "G13");
        ok &= hm_snap(61, 1u << 0 | 1u << 4 | 1u << 7) == 60 && hm_snap(62, 1u << 0 | 1u << 4 | 1u << 7) == 60 && hm_snap(62, 1u << 0) == 60;
        check(ok, "CHORDS: built and named right (Dm7, G7, Cmaj9, Bm7b5, Bdim, G7alt, Db9#11, G13); a note snaps to the nearest chord tone");
    }
    /* voice leading: fewer semitones than root position, the same chords (voicing draws no random numbers) */
    for (int v = 0; v < 4; v++) {
        hm_seq_t qa;
        hm_chord_t c;
        long mv[2] = {0, 0};
        for (int m = 0; m < 2; m++) {
            uint8_t prev[HM_NOTES], cur[HM_NOTES];
            int np = 0, nx;
            hm_seq_reset(&qa, 77);
            for (int i = 0; i < 400; i++) {
                hm_item_t it = hm_seq_next(&qa, 0.45f, KS_MAJOR, &nx);
                hm_build(&c, &it, 0, KS_MAJOR, 0.45f);
                int n = hm_voice(&c, v, m ? HI_SMOOTH : HI_ROOT, 58, prev, np, cur);
                if (np) mv[m] += hm_move(prev, np, cur, n);
                memcpy(prev, cur, (size_t)n);
                np = n;
            }
        }
        snprintf(what, sizeof what, "CHORDS voice leading (voicing %d): SMOOTH moves %.1f semitones a change, root position %.1f", v, mv[1] / 399.0, mv[0] / 399.0);
        check(mv[1] < mv[0] * 3 / 4, what);
    }
    /* the same seed, the same chords and notes; another seed others */
    {
        int n[3], nc[3];
        pg_ev_t *E2[3] = {cc_ev, cc_ev2, pg_b};
        for (int k = 0; k < 3; k++) {
            void *a = pg_patch();
            setkv(a, "chd_on=1,chd_jazz=60,bass_on=1,chd_rate=3");
            set(a, "chd_seed", k == 2 ? 2 : 1);
            n[k] = nc[k] = 0;
            cch_run(a, 16.0, "", E2[k], &n[k], "", cc_c, &nc[k]);
            E->destroy(a);
        }
        int same = pg_same(cc_ev, n[0], cc_ev2, n[1], 0), other = !pg_same(cc_ev, n[0], pg_b, n[2], 0);
        snprintf(what, sizeof what, "CHORDS deterministic: the same seed plays the same %d notes (%d), another seed others (%d)", n[0], same, other);
        check(same && other && n[0] > 20, what);
    }
    /* BASS under the chords, on their roots: chords on Synth 1, bass on Synth 2; and each pattern */
    {
        static const char *const PN[] = {"Auto", "Root", "Root-Fifth", "Octave", "Arpeggio", "Walking"};
        for (int pat = 0; pat <= HB_WALK; pat++) {
            void *a = pg_patch();
            char kv[160];
            snprintf(kv, sizeof kv, "chd_on=1,chd_jazz=50,s2_syn_on=1,s2_bass_on=1,s2_bass_jazz=60,s2_bass_pattern=%d,key_root=0,key_scale=0", pat);
            setkv(a, kv);
            int n = 0, nc = 0, nb = 0, nc2 = 0;
            /* both Synths' logs in one pass */
            int16_t buf[256];
            static char b[8192];
            for (long at = 0; at < (long)(40.0 * SR); at += 128) {
                E->render(a, buf, 128);
                if (E->get_param(a, "gen_log", b, sizeof b) > 0)
                    for (char *p = b; *p;) {
                        long long t; int nt, v, used;
                        if (sscanf(p, "%lld:%d:%d;%n", &t, &nt, &v, &used) != 3) break;
                        if (v > 0 && n < PG_MAX) cc_ev[n++] = (pg_ev_t){t, nt, v};
                        p += used;
                    }
                if (E->get_param(a, "s2_gen_log", b, sizeof b) > 0)
                    for (char *p = b; *p;) {
                        long long t; int nt, v, used;
                        if (sscanf(p, "%lld:%d:%d;%n", &t, &nt, &v, &used) != 3) break;
                        if (v > 0 && nb < PG_MAX) cc_ev2[nb++] = (pg_ev_t){t, nt, v};
                        p += used;
                    }
                if (E->get_param(a, "chd_log", b, sizeof b) > 0)
                    for (char *p = b; *p;) {
                        long long t; int r, to, te, used; char nm[16];
                        if (sscanf(p, "%lld:%d:%d:%d:%15[^;];%n", &t, &r, &to, &te, nm, &used) != 5) break;
                        if (nc < CC_MAX) { cc_c[nc] = (cc_chord_t){t, r, (unsigned)to, (unsigned)te, ""}; nc++; }
                        p += used;
                    }
            }
            E->destroy(a);
            (void)nc2;
            double chord_low = 0, bass_mean = 0;
            int low = 999, on_root = 0, onset = 0, overlap = 0, hi = 0;
            for (int i = 0; i < n; i++) low = cc_ev[i].note < low ? cc_ev[i].note : low;
            long long last = -1;
            for (int i = 0; i < nb; i++) {
                bass_mean += cc_ev2[i].note;
                hi = cc_ev2[i].note > hi ? cc_ev2[i].note : hi;
                overlap += cc_ev2[i].t == last;
                last = cc_ev2[i].t;
            }
            bass_mean /= nb ? nb : 1;
            /* at each change the bass's note (on the same sample, or within a step's grain) has the chord's root */
            for (int c = 0; c < nc; c++)
                for (int i = 0; i < nb; i++)
                    if (cc_ev2[i].t >= cc_c[c].t - 64 && cc_ev2[i].t <= cc_c[c].t + 64) {
                        onset++;
                        on_root += cc_ev2[i].note % 12 == cc_c[c].root;
                        break;
                    }
            for (int i = 0; i < n; i++) chord_low += cc_ev[i].note;
            chord_low /= n ? n : 1;
            snprintf(what, sizeof what, "BASS %s on Synth 2 under CHORDS on Synth 1: %d notes, one at a time (%d together), mean %.1f (highest %d) "
                     "under the chords' mean %.1f (lowest %d); at %d of %d chord changes on the chord's root, of %d chords", PN[pat], nb, overlap, bass_mean,
                     hi, chord_low, low, on_root, onset, nc);
            check(nb > 30 && !overlap && bass_mean < chord_low - 8 && onset >= nc - 2 && on_root >= onset - 1, what);
        }
    }
    /* BASS alone: its KEY and SCALE; a walking line stays in it at low JAZZ; its JAZZ moves Auto from root to walking */
    {
        void *a = pg_patch();
        setkv(a, "bass_on=1,bass_jazz=20,key_root=2,key_scale=1");
        int n = 0;
        pg_run(a, 20.0, "gen_log", pg_a, &n);
        E->destroy(a);
        int distinct[2] = {0, 0}, seen[128] = {0}, lo = 999, hi = 0;
        for (int i = 0; i < n; i++) { distinct[0] += !seen[pg_a[i].note]++; lo = pg_a[i].note < lo ? pg_a[i].note : lo; hi = pg_a[i].note > hi ? pg_a[i].note : hi; }
        int in = pg_in_key(pg_a, n, 2, KS_MINOR);
        a = pg_patch();
        setkv(a, "bass_on=1,bass_jazz=100,key_root=2,key_scale=1");
        int nw = 0;
        pg_run(a, 20.0, "gen_log", pg_b, &nw);
        E->destroy(a);
        memset(seen, 0, sizeof seen);
        for (int i = 0; i < nw; i++) distinct[1] += !seen[pg_b[i].note]++;
        snprintf(what, sizeof what, "BASS with no CHORDS: JAZZ 20: %d notes, all in D minor (%d), between %d and %d in its low octave, %d different; "
                 "JAZZ 100 walks: %d different", n, in, lo, hi, distinct[0], distinct[1]);
        check(n >= 35 && in && lo >= 36 && hi <= 55 && distinct[1] > distinct[0] && distinct[0] >= 2, what);
    }
    /* FOLLOW CHORDS: every generator, chords in another Synth; on the very sample of a change too; off: as they were */
    {
        static const struct { const char *name, *on; } G[] = {
            {"Euclid", "gen_euclid_on=1,gen_eu_notes=1,gen_range=1"},
            {"Turing", "gen_tm_on=1,gen_tm_change=50,gen_range=1"},
            {"Balls", "phys_balls_on=1,phys_max=30,phys_range=2"},
            {"Particles", "phys_parts_on=1,phys_max=30,phys_range=2"},
            {"Rain", "rain_on=1,rain_rate=80,rain_max=30,rain_range=2"},
            {"Markov", "mkv_on=1,mkv_creat=100,mkv_max=30"},
            {"Life", "life_on=1,life_range=2"},
            {"Orbits", "orb_on=1,orb_range=2"},
            {"Pendulums", "pend_on=1,pend_cycle=0,pend_count=12"}};
        for (unsigned g = 0; g < sizeof G / sizeof G[0]; g++)
            for (int lead = 1; lead <= 2; lead++) {   /* the chords on Synth 2 or 3, the generator on Synth 1 */
                void *a = pg_patch();
                char kv[120];
                setkv(a, G[g].on);
                snprintf(kv, sizeof kv, "s%d_syn_on=1,s%d_chd_on=1,s%d_chd_jazz=50,s%d_chd_rate=3,gen_follow_chords=1", lead + 1, lead + 1, lead + 1, lead + 1);
                setkv(a, kv);
                char cp[8];
                snprintf(cp, sizeof cp, "s%d_", lead + 1);
                int n = 0, nc = 0;
                cch_run(a, 14.0, "", cc_ev, &n, cp, cc_c, &nc);
                E->destroy(a);
                int bad = 0, counted = 0, same_sample = 0;
                for (int i = 0; i < n; i++) {
                    const cc_chord_t *c = cc_at(cc_c, nc, cc_ev[i].t);
                    if (!c) continue;
                    counted++;
                    bad += !((c->tones | c->tens) >> (cc_ev[i].note % 12) & 1u);
                    same_sample += c->t == cc_ev[i].t;
                }
                snprintf(what, sizeof what, "FOLLOW CHORDS %s on Synth 1, CHORDS on Synth %d: %d notes, %d out of the chord or its tensions; %d chords, %d notes on a change's very sample",
                         G[g].name, lead + 1, counted, bad, nc, same_sample);
                check(counted > 5 && !bad && nc > 3, what);
            }
        /* no CHORDS: FOLLOW does nothing; off with CHORDS on: notes as they were */
        void *a = pg_patch(), *b = pg_patch(), *c = pg_patch();
        setkv(a, "rain_on=1,rain_rate=80,rain_max=30,gen_follow_chords=1");
        setkv(b, "rain_on=1,rain_rate=80,rain_max=30");
        setkv(c, "rain_on=1,rain_rate=80,rain_max=30,s2_syn_on=1,s2_chd_on=1");
        int na = 0, nb = 0, nc = 0;
        pg_run(a, 8.0, "gen_log", pg_a, &na);
        pg_run(b, 8.0, "gen_log", pg_b, &nb);
        pg_run(c, 8.0, "gen_log", cc_ev, &nc);
        E->destroy(a); E->destroy(b); E->destroy(c);
        snprintf(what, sizeof what, "FOLLOW CHORDS with no CHORDS on: the same %d notes (%d); off with CHORDS on: the same notes (%d)", nb,
                 pg_same(pg_a, na, pg_b, nb, 0), pg_same(cc_ev, nc, pg_b, nb, 0));
        check(nb > 5 && pg_same(pg_a, na, pg_b, nb, 0) && pg_same(cc_ev, nc, pg_b, nb, 0), what);
        /* a chord stopped: FOLLOW stops */
        a = pg_patch();
        setkv(a, "rain_on=1,rain_rate=80,rain_max=30,gen_follow_chords=1,s2_syn_on=1,s2_chd_on=1,s2_chd_jazz=0");
        na = 0;
        pg_run(a, 4.0, "gen_log", pg_a, &na);
        set(a, "s2_chd_on", 0);
        pg_run(a, 0.1, "gen_log", pg_a, &na);
        na = 0;
        pg_run(a, 8.0, "gen_log", pg_a, &na);
        E->destroy(a);
        unsigned pcs = 0;
        for (int i = 0; i < na; i++) pcs |= 1u << (pg_a[i].note % 12);
        snprintf(what, sizeof what, "FOLLOW CHORDS: CHORDS switched off: the rain plays the whole scale again (%d pitch classes)", __builtin_popcount(pcs));
        check(__builtin_popcount(pcs) >= 5, what);
    }
    /* two CHORDS: the lower Synth leads, the higher plays its chords in its own voicing */
    {
        void *a = pg_patch();
        setkv(a, "chd_on=1,chd_jazz=50,s2_syn_on=1,s2_chd_on=1,s2_chd_jazz=0,s2_chd_voicing=3");
        int16_t buf[256];
        static char b[8192];
        int nc = 0, nc2 = 0;
        for (long at = 0; at < (long)(30.0 * SR); at += 128) {
            E->render(a, buf, 128);
            for (int w = 0; w < 2; w++) {
                if (E->get_param(a, w ? "s2_chd_log" : "chd_log", b, sizeof b) <= 0) continue;
                for (char *p = b; *p;) {
                    long long t; int r, to, te, used; char nm[16];
                    if (sscanf(p, "%lld:%d:%d:%d:%15[^;];%n", &t, &r, &to, &te, nm, &used) != 5) break;
                    if (w == 0 && nc < CC_MAX) cc_c[nc++] = (cc_chord_t){t, r, (unsigned)to, (unsigned)te, ""};
                    if (w == 1 && nc2 < CC_MAX) cc_c2[nc2++] = (cc_chord_t){t, r, (unsigned)to, (unsigned)te, ""};
                    p += used;
                }
            }
        }
        E->destroy(a);
        int same = nc > 8 && nc2 > 8;
        for (int i = 0; i < nc2; i++) {
            const cc_chord_t *c = cc_at(cc_c, nc, cc_c2[i].t);
            same &= c && c->root == cc_c2[i].root && c->tones == cc_c2[i].tones;
        }
        snprintf(what, sizeof what, "two CHORDS: Synth 2 plays Synth 1's chords (%d chords, %d in Synth 2, all the same: %d)", nc, nc2, same);
        check(same, what);
    }
    /* each generator's own KEY and SCALE; FOLLOW KEYS' root; Held and Launch */
    {
        void *a = pg_patch();
        setkv(a, "key_root=2,key_scale=1,chd_on=1,chd_jazz=0,chd_own_key=7,chd_own_scale=1");
        int n = 0;
        pg_run(a, 12.0, "gen_log", pg_a, &n);
        E->destroy(a);
        int in_own = pg_in_key(pg_a, n, 6, KS_MAJOR), in_main = pg_in_key(pg_a, n, 2, KS_MINOR);
        a = pg_patch();
        setkv(a, "key_root=6,key_scale=0,chd_on=1,chd_jazz=0");
        int nm = 0;
        pg_run(a, 12.0, "gen_log", pg_b, &nm);
        E->destroy(a);
        snprintf(what, sizeof what, "CHORDS' own KEY and SCALE: main D minor, own F# major: %d notes all in F# major (%d), not all in D minor (%d), the very notes of the main ones set to it (%d)",
                 n, in_own, in_main, pg_same(pg_a, n, pg_b, nm, 0));
        check(n > 10 && in_own && !in_main && pg_same(pg_a, n, pg_b, nm, 0), what);
        /* Held: nothing without a key, only while it is held, in the key played */
        a = pg_patch();
        setkv(a, "chd_on=1,chd_jazz=0,chd_keys=1,chd_rate=4,key_root=0,key_scale=0,gen_follow_keys=1");
        int n0 = 0;
        pg_run(a, 2.0, "gen_log", pg_a, &n0);
        note(a, 1, 64, 100);
        int n1 = 0;
        pg_run(a, 4.0, "gen_log", pg_a, &n1);
        int ek = pg_in_key(pg_a, n1, 4, KS_MAJOR);
        note(a, 0, 64, 0);
        int n2 = 0;
        pg_run(a, 0.2, "gen_log", pg_b, &n2);
        n2 = 0;
        pg_run(a, 3.0, "gen_log", pg_b, &n2);
        E->destroy(a);
        snprintf(what, sizeof what, "CHORDS Held Keys: %d notes before a key, %d while E is held, in E major (%d), %d after it is let go", n0 - 0, n1, ek, n2);
        check(n1 > 6 && ek && !n2, what);
        /* Launch: four chords a key */
        a = pg_patch();
        setkv(a, "chd_on=1,chd_jazz=0,chd_keys=2,chd_rate=4");
        int nc = 0, nn = 0;
        cch_run(a, 1.0, "", pg_a, &nn, "", cc_c, &nc);
        int before = nc;
        note(a, 1, 62, 100);
        note(a, 0, 62, 0);
        cch_run(a, 8.0, "", pg_a, &nn, "", cc_c, &nc);
        E->destroy(a);
        snprintf(what, sizeof what, "CHORDS Key Launch: %d chords before a key, %d after one", before, nc);
        check(before == 0 && nc == 4, what);
    }
    /* WILD sets JAZZ through RANDOMISE and MUTATE, GENERATIVE lit; not lit: untouched; the own pair and FOLLOW never */
    {
        double mean[3][2];
        int never = 1;
        static const float W[3] = {0.0f, 0.5f, 1.0f};
        static float cur[NPARAMS], out[NPARAMS];
        for (int i = 0; i < NPARAMS; i++) cur[i] = PARAMS[i].def;
        int ij = pindex("chd_jazz"), ib = pindex("bass_jazz"), ion = pindex("chd_on"), iun = pindex("bass_on");
        float on_n = 1.0f;
        for (int k = 0; k < 3; k++) {
            uint32_t seed = 11;
            double sr = 0, sm = 0;
            rnd_opts_t o = {W[k], 1u << RS_GEN, 0, RV_BOTH, 0.0f};
            for (int r = 0; r < 40; r++) {
                rnd_generate_x(cur, out, 0, &seed, &o);
                sr += out[ij] * 100.0;
                cur[ion] = on_n;
                rnd_mutate_x(cur, out, 0.4f, 0, &seed, &o);
                sm += out[ij] * 100.0;
                cur[ion] = PARAMS[ion].def;
            }
            mean[k][0] = sr / 40;
            mean[k][1] = sm / 40;
        }
        uint32_t seed = 5;
        rnd_opts_t none = {1.0f, 0, 0, RV_BOTH, 0.0f};
        rnd_generate_x(cur, out, 0, &seed, &none);
        int untouched = out[ij] == cur[ij] && out[ib] == cur[ib] && out[ion] == cur[ion] && out[iun] == cur[iun];
        /* the GENERATIVE page's own buttons: WILD only sets JAZZ there */
        rnd_opts_t hub = {0.0f, 1u << RS_GEN, 0, RV_BOTH, 1.0f};
        rnd_generate_x(cur, out, (1u << RS_COUNT) - 1u, &seed, &hub);
        int hub_jazz = out[ij] * 100.0f >= 89.0f;
        never &= rnd_section_of(pindex("chd_own_key")) == RS_NEVER && rnd_section_of(pindex("bass_own_scale")) == RS_NEVER &&
                 rnd_section_of(pindex("bass_follow")) == RS_NEVER && rnd_section_of(pindex("s2_chd_jazz")) == RS_NEVER;
        snprintf(what, sizeof what, "WILD sets JAZZ, GENERATIVE lit: WILD 0 / 50 / 100%%: RANDOMISE %.0f %.0f %.0f, MUTATE %.0f %.0f %.0f; not lit: untouched (%d); "
                 "the page's own RANDOM with WILD 100%% (%d); own KEY, SCALE, BASS's FOLLOW and the other Synths never (%d)", mean[0][0], mean[1][0], mean[2][0],
                 mean[0][1], mean[1][1], mean[2][1], untouched, hub_jazz, never);
        check(mean[0][0] < 10 && mean[1][0] > 35 && mean[1][0] < 65 && mean[2][0] > 90 && mean[0][1] < 10 && mean[2][1] > 90 && untouched && hub_jazz && never, what);
    }
    /* nothing allocates in the render path: chords, bass and followers on, keys, stop */
    {
        void *a = pg_patch();
        setkv(a, "chd_on=1,chd_jazz=100,chd_rate=5,chd_strum=20,bass_on=1,bass_jazz=100,bass_rate=3,s2_syn_on=1,s2_chd_on=1,s3_syn_on=1,s3_rain_on=1,s3_gen_follow_chords=1");
        int16_t buf[256];
        size_t h0 = cc_heap();
        for (long at = 0; at < (long)(10.0 * SR); at += 128) E->render(a, buf, 128);
        size_t h1 = cc_heap();
        E->destroy(a);
        snprintf(what, sizeof what, "CHORDS, BASS, FOLLOW: 10 s of the busiest settings allocate nothing (the heap %zu -> %zu)", h0, h1);
        check(h0 == h1, what);
    }
    /* no stuck notes on stop: switched off, ON off, GEN OFF's way; the notes end, the voices go quiet */
    {
        static const char *const STOP[] = {"chd_on", "syn_on"};
        for (int k = 0; k < 2; k++) {
            void *a = pg_patch();
            setkv(a, "chd_on=1,chd_jazz=100,chd_rate=0,chd_gate=100,bass_on=1,bass_gate=100,bass_rate=0");
            int16_t buf[256];
            char b[32];
            for (long at = 0; at < (long)(3.0 * SR); at += 128) E->render(a, buf, 128);
            E->get_param(a, "voices_sounding", b, sizeof b);
            int during = atoi(b), n = 0;
            pg_run(a, 0.01, "gen_log", pg_a, &n);   /* (what played so far: read and gone) */
            set(a, STOP[k], 0);
            if (k == 0) set(a, "bass_on", 0);
            for (long at = 0; at < (long)(12.0 * SR); at += 128) E->render(a, buf, 128);
            E->get_param(a, "voices_sounding", b, sizeof b);
            int after = atoi(b);
            n = 0;
            pg_run(a, 0.1, "gen_log", pg_a, &n);
            E->destroy(a);
            snprintf(what, sizeof what, "no stuck notes: %s off, gate 100%%, chords of 16 beats: %d voices during, %d after, no new notes (%d)", STOP[k], during, after, n);
            check(during >= 3 && after == 0 && n == 0, what);
        }
        /* a state round trip: everything set, the same again */
        void *a = pg_patch();
        setkv(a, "chd_on=1,chd_jazz=73,chd_own_key=3,bass_on=1,bass_pattern=5,bass_follow=0,gen_follow_chords=1");
        static char st[1 << 16];
        int len = E->get_param(a, "state", st, sizeof st);
        void *c = pg_patch();
        E->set_param(c, "state", st);
        int ok = len > 0 && get(c, "chd_jazz") == 73 && get(c, "chd_own_key") == 3 && get(c, "bass_pattern") == 5 && get(c, "bass_follow") == 0 &&
                 get(c, "gen_follow_chords") == 1;
        E->destroy(a);
        E->destroy(c);
        a = pg_patch();
        E->set_param(a, "state", "v=1;rain_on=1");
        int oldok = get(a, "chd_on") == 0 && get(a, "bass_on") == 0 && get(a, "gen_follow_chords") == 0;
        E->destroy(a);
        check(ok && oldok, "CHORDS, BASS: saved and read back; a sound saved before them: all off, FOLLOW CHORDS off");
    }
    /* every scale, every voicing: nothing out of range, no crash */
    {
        int fine = 1;
        for (int sc = 0; sc < KS_COUNT; sc++)
            for (int v = 0; v < 4; v++) {
                void *a = pg_patch();
                char kv[160];
                snprintf(kv, sizeof kv, "chd_on=1,chd_jazz=%d,key_scale=%d,chd_voicing=%d,chd_inv=%d,chd_rate=5,bass_on=1,bass_jazz=100,chd_octave=%d", (sc * 7 + v * 31) % 101, sc, v, v, v * 2 % 6);
                setkv(a, kv);
                int n = 0;
                pg_run(a, 3.0, "gen_log", pg_a, &n);
                E->destroy(a);
                fine &= n > 10;
                for (int i = 0; i < n; i++) fine &= pg_a[i].note >= 0 && pg_a[i].note <= 127;
            }
        check(fine, "CHORDS, BASS: every scale with every voicing and inversion: notes in range");
    }
    /* (2026-11-03) FEED: the chords into the arpeggiator as keys, here or in another Synth; one way; it leaves with them */
    {
        char what2[400];
        int counts[6], inchord = 0, total = 0, roots = 0;
        static const char *const KV[6] = {
            "chd_on=1,chd_jazz=0,chd_rate=2,chd_feed=2,arp_on=1,gen_keys=1",                 /* Feed Only into its arp */
            "chd_on=1,chd_jazz=0,chd_rate=2,chd_feed=0,arp_on=1,gen_keys=1",                 /* off: only the chords */
            "chd_on=1,chd_jazz=0,chd_rate=2,chd_feed=2",                                     /* Feed Only, nothing to take it */
            "chd_on=1,chd_jazz=0,chd_rate=2,chd_feed=2,chd_keys=1,arp_on=1,gen_keys=1",      /* Held Keys: fed notes never start it */
            "bass_on=1,bass_rate=1,bass_feed=2,arp_on=1,gen_keys=1",                         /* the bass, likewise */
            "bass_on=1,bass_rate=1,bass_feed=0,arp_on=1,gen_keys=1"};
        for (int k = 0; k < 6; k++) {
            void *a = pg_patch();
            setkv(a, KV[k]);
            int n = 0, nc = 0;
            cch_run(a, 9.0, "", cc_ev, &n, "", cc_c, &nc);
            counts[k] = n;
            if (k == 0) {
                unsigned seen = 0;
                for (int i = 0; i < nc; i++) seen |= 1u << cc_c[i].root;
                for (; seen; seen &= seen - 1) roots++;
                for (int i = 0; i < n; i++) {
                    const cc_chord_t *c = cc_at(cc_c, nc, cc_ev[i].t);
                    total++;
                    inchord += c && (c->tones >> (cc_ev[i].note % 12) & 1u);
                }
                set(a, "chd_on", 0);
                int16_t buf[256];
                for (long at = 0; at < (long)(1.0 * SR); at += 128) E->render(a, buf, 128);
                n = 0;
                pg_run(a, 0.1, "gen_log", pg_a, &n);
                for (long at = 0; at < (long)(3.0 * SR); at += 128) E->render(a, buf, 128);
                n = 0;
                pg_run(a, 3.0, "gen_log", pg_a, &n);
                snprintf(what2, sizeof what2, "FEED: into the arp, %d notes, %d in the chord of the moment, %d chord roots; CHORDS off: %d notes after",
                         total, inchord, roots, n);
                check(total > 30 && inchord == total && roots >= 2 && n == 0, what2);
            }
            E->destroy(a);
        }
        snprintf(what2, sizeof what2, "FEED: Feed Only with no arp or generator is silent (%d); off, the arp gets nothing, only the chords play (%d, a few); "
                 "Held Keys chords are never started by what they feed (%d); BASS feeds too (%d, off %d)", counts[2], counts[1], counts[3], counts[4], counts[5]);
        check(counts[2] == 0 && counts[1] > 0 && counts[1] < counts[0] && counts[3] == 0 && counts[4] > 0, what2);
        void *a = pg_patch();
        setkv(a, "chd_on=1,chd_jazz=0,chd_rate=2,chd_feed=2,chd_feed_to=1,s2_syn_on=1,s2_arp_on=1,s2_gen_keys=1");
        int n1 = 0, n2 = 0;
        cch_run(a, 6.0, "s2_", cc_ev2, &n1, "", NULL, NULL);
        E->destroy(a);
        a = pg_patch();
        setkv(a, "chd_on=1,chd_jazz=0,chd_rate=2,chd_feed=2,chd_feed_to=0,s2_syn_on=1,s2_arp_on=1,s2_gen_keys=1");
        cch_run(a, 6.0, "s2_", cc_ev2, &n2, "", NULL, NULL);
        E->destroy(a);
        snprintf(what2, sizeof what2, "FEED TO: All Synths plays Synth 2's arp (%d notes), This Synth does not (%d)", n1, n2);
        check(n1 > 10 && n2 == 0, what2);
    }
    /* (2026-11-03) TIME SIGNATURE: CHORDS' bars and BASS on the bar line */
    {
        char what2[300];
        static const struct { const char *v; double beats; } SIG[3] = {{"0", 4.0}, {"2", 3.0}, {"8", 3.5}};
        double got[3];
        for (int k = 0; k < 3; k++) {
            void *a = pg_patch();
            char kv[120];
            snprintf(kv, sizeof kv, "chd_on=1,chd_jazz=0,chd_rate=2,time_sig=%s", SIG[k].v);
            setkv(a, kv);
            int nc = 0;
            cch_run(a, 20.0, "", NULL, NULL, "", cc_c, &nc);
            got[k] = nc > 3 ? (double)(cc_c[nc - 1].t - cc_c[1].t) / (nc - 2) : 0.0;
            E->destroy(a);
        }
        snprintf(what2, sizeof what2, "TIME SIGNATURE: a bar's chord lasts %.0f samples in 4/4, %.0f in 3/4 (ratio %.3f), %.0f in 7/8 (ratio %.3f)",
                 got[0], got[1], got[1] / got[0], got[2], got[2] / got[0]);
        check(fabs(got[1] / got[0] - 0.75) < 0.01 && fabs(got[2] / got[0] - 0.875) < 0.01, what2);
        void *a = pg_patch();
        setkv(a, "bass_on=1,bass_rate=1,time_sig=8");
        int n = 0;
        cch_run(a, 12.0, "", cc_ev, &n, "", NULL, NULL);
        E->destroy(a);
        int shortgap = 0, longgap = 0;
        double q = 0.0;
        for (int i = 1; i < n; i++) { double d = (double)(cc_ev[i].t - cc_ev[i - 1].t); if (d > q) q = d; }
        for (int i = 1; i < n; i++) {
            double d = (double)(cc_ev[i].t - cc_ev[i - 1].t);
            if (fabs(d / q - 0.5) < 0.02) shortgap++;
            if (fabs(d / q - 1.0) < 0.02) longgap++;
        }
        snprintf(what2, sizeof what2, "TIME SIGNATURE: BASS in 7/8, 1/4s: %d notes, %d quarters, %d short ones on the bar's end (one in four)", n, longgap, shortgap);
        check(n > 10 && shortgap > 0 && longgap / 3 >= shortgap - 1 && longgap / 3 <= shortgap + 1, what2);
    }
}

/* ---- the demo WAVs ---- */
static void cc_sound(void *e) {
    /* Synth 1: a warm, soft electric-piano-like pad for the chords; Synth 2: a round bass; Synth 3: a soft bell */
    E->set_param(e, "lfo_bpm", "108");
    set(e, "o1_wave", 1); set(e, "o2_level", 70); set(e, "o2_freq", 0.05); set(e, "o3_level", 0);
    set(e, "flt_cut", knob_for_hz(1400)); set(e, "flt_res", 12); set(e, "lad_env", 22);
    set(e, "fenv_a", knob_ms(5)); set(e, "fenv_d", knob_ms(900)); set(e, "fenv_s", 25);
    set(e, "amp_a", knob_ms(12)); set(e, "amp_d", knob_ms(1800)); set(e, "amp_s", 55); set(e, "amp_r", knob_ms(350));
    set(e, "voices", 7); set(e, "volume", 42); set(e, "mfx_send2", 25); set(e, "chd_voicing", 2);
    set(e, "s2_syn_on", 1); set(e, "s2_o1_oct", 2); set(e, "s2_o1_wave", 1); set(e, "s2_o2_level", 40); set(e, "s2_o2_oct", 1);
    set(e, "s2_flt_cut", knob_for_hz(520)); set(e, "s2_flt_res", 18); set(e, "s2_lad_env", 28); set(e, "s2_fenv_d", knob_ms(220)); set(e, "s2_fenv_s", 10);
    set(e, "s2_amp_a", knob_ms(3)); set(e, "s2_amp_d", knob_ms(500)); set(e, "s2_amp_s", 60); set(e, "s2_amp_r", knob_ms(80)); set(e, "s2_volume", 62);
    set(e, "s3_syn_on", 1); set(e, "s3_o1_wave", 0); set(e, "s3_o1_oct", 3); set(e, "s3_flt_cut", knob_for_hz(5000));
    set(e, "s3_amp_a", knob_ms(2)); set(e, "s3_amp_d", knob_ms(600)); set(e, "s3_amp_s", 0); set(e, "s3_amp_r", knob_ms(400));
    set(e, "s3_volume", 30); set(e, "s3_mfx_send2", 40);
}
static void cc_wav(const char *demo_path, const char *file, const char *kv, const char *cpre, double secs, int check_follow) {
    enum { MAXF = 44100 * 40 };
    static int16_t pcm[2 * MAXF];
    char dir[256], path[320], names[600], what[700];
    snprintf(dir, sizeof dir, "%s", demo_path);
    char *slash = strrchr(dir, '/');
    if (slash) slash[1] = 0; else dir[0] = 0;
    snprintf(path, sizeof path, "%s%s", dir, file);
    void *e = E->create(NULL);
    cc_sound(e);
    setkv(e, kv);
    static char b[8192];
    char kc[32];
    snprintf(kc, sizeof kc, "%schd_log", cpre);
    int nc = 0, frames = (int)(secs * SR) / 128 * 128, nr = 0;
    if (frames > MAXF) frames = MAXF;
    for (int at = 0; at < frames; at += 128) {
        E->render(e, pcm + 2 * at, 128);
        if (E->get_param(e, kc, b, sizeof b) > 0)
            for (char *p = b; *p;) {
                long long t; int r, to, te, used; char nm[16];
                if (sscanf(p, "%lld:%d:%d:%d:%15[^;];%n", &t, &r, &to, &te, nm, &used) != 5) break;
                if (nc < CC_MAX) { cc_c[nc] = (cc_chord_t){t, r, (unsigned)to, (unsigned)te, ""}; snprintf(cc_c[nc].name, 16, "%s", nm); nc++; }
                p += used;
            }
        if (check_follow && E->get_param(e, "s3_gen_log", b, sizeof b) > 0)
            for (char *p = b; *p;) {
                long long t; int nt, v, used;
                if (sscanf(p, "%lld:%d:%d;%n", &t, &nt, &v, &used) != 3) break;
                if (v > 0 && nr < PG_MAX) cc_ev[nr++] = (pg_ev_t){t, nt, v};
                p += used;
            }
    }
    E->destroy(e);
    write_wav(path, pcm, (uint32_t)frames);
    int peak = 0;
    for (int i = 0; i < 2 * frames; i++) peak = abs(pcm[i]) > peak ? abs(pcm[i]) : peak;
    cc_names(cc_c, nc, 24, names, sizeof names);
    printf("wrote %s (%.0f s, peak %.2f): %s\n", path, frames / (double)SR, peak / 32768.0, names);
    int bad = 0;
    if (check_follow)
        for (int i = 0; i < nr; i++) {
            const cc_chord_t *c = cc_at(cc_c, nc, cc_ev[i].t);
            if (c) bad += !((c->tones | c->tens) >> (cc_ev[i].note % 12) & 1u);
        }
    snprintf(what, sizeof what, "demo %s: %d chords, it sounds (peak %.2f), not clipped hard%s", file, nc, peak / 32768.0, check_follow ? ", the rain in the chords" : "");
    check(nc > 4 && peak > 2000 && !bad, what);
}
static void chords_wavs(const char *demo_path) {
    cc_wav(demo_path, "chords_jazz_0.wav", "chd_on=1,chd_jazz=0,chd_seed=3,key_root=0,key_scale=0", "", 36.0, 0);
    cc_wav(demo_path, "chords_jazz_50.wav", "chd_on=1,chd_jazz=50,chd_seed=3,key_root=0,key_scale=0", "", 36.0, 0);
    cc_wav(demo_path, "chords_jazz_100.wav", "chd_on=1,chd_jazz=100,chd_seed=3,key_root=0,key_scale=0", "", 36.0, 0);
    cc_wav(demo_path, "chords_bass.wav", "chd_on=1,chd_jazz=60,chd_seed=5,s2_bass_on=1,s2_bass_jazz=85,s3_syn_on=0,key_root=0,key_scale=1", "", 36.0, 0);
    cc_wav(demo_path, "chords_bass_rain_follow.wav", "chd_on=1,chd_jazz=75,chd_seed=7,s2_bass_on=1,s2_bass_jazz=90,s3_rain_on=1,s3_rain_rate=45,s3_rain_max=8,s3_rain_octave=3,s3_gen_follow_chords=1,key_root=0,key_scale=0", "", 36.0, 1);
}

static void chords_bench(void) {
    struct timespec t0, t1;
    hm_seq_t q;
    hm_chord_t c;
    uint8_t prev[HM_NOTES], cur[HM_NOTES];
    int np = 0, nx;
    volatile long sink = 0;
    enum { CC_N = 100000 };
    hm_seq_reset(&q, 9);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < CC_N; i++) {
        hm_item_t it = hm_seq_next(&q, 1.0f, KS_MAJOR, &nx);
        hm_build(&c, &it, 0, KS_MAJOR, 1.0f);
        np = hm_voice(&c, HV_DROP2, HI_SMOOTH, 58, prev, np, cur);
        memcpy(prev, cur, (size_t)np);
        sink += np;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("bench: chords, one chord worked out at JAZZ 100 (progression, notes, voicing): %6.3f us\n",
           ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / CC_N);
    static const char *const KV[4] = {"", "chd_on=1,chd_jazz=100,chd_rate=3,bass_on=1,bass_rate=2,s2_syn_on=0,s3_syn_on=0",
                                      "chd_on=1,chd_jazz=100,chd_rate=5,bass_on=1,bass_rate=3,rain_on=1,gen_follow_chords=1",
                                      "chd_on=1,chd_jazz=100,chd_rate=3,chd_feed=1,arp_on=1,gen_keys=1,s2_syn_on=0,s3_syn_on=0"};
    static const char *const NM[4] = {"off", "chords and bass on (their voices too)", "chords on 1/8, bass, rain following",
                                      "chords feeding the arp (2026-11-03)"};
    for (int k = 0; k < 4; k++) {
        void *a = pg_patch();
        if (*KV[k]) setkv(a, KV[k]);
        int16_t buf[256];
        for (int i = 0; i < 2000; i++) E->render(a, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < 20000; i++) E->render(a, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        printf("bench: %-40s %7.2f us per 128-frame block\n", NM[k], ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / 20000);
        E->destroy(a);
    }
    (void)sink;
}
