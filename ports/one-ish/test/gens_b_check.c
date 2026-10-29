/* RAIN AND RIPPLES and MARKOV MELODY (src/gens_b.h, src/gens_b_run.h; 2026-10-30): off is silent and changes nothing,
 * the same settings give the same notes, every note is in the Synth's key and scale, MAX NOTES/S holds, the defaults
 * are moderate, each Synth runs (and learns) its own, HELD KEYS, SYNC on the grid, echoes, the Markov melody learning
 * a played pattern (its output's steps move to the pattern's), its rhythm and velocity, a saved sound keeping the
 * settings but not what was learned, the views, and the modulation sources. Included from offline_check.c after
 * physgen_check.c (its helpers: pg_run, pg_in_key, pg_same, pg_patch; set, setkv, note, check). */
#include "../src/gens_b.h"

static pg_ev_t gb_a[PG_MAX], gb_b[PG_MAX];

/* the most notes in any one second */
static int gb_worst(const pg_ev_t *ev, int n) {
    int worst = 0;
    for (int i = 0; i < n; i++) {
        int c = 0;
        for (int j = i; j < n && ev[j].t - ev[i].t < (long long)SR; j++) c++;
        if (c > worst) worst = c;
    }
    return worst;
}
/* notes off a grid of `step` samples (more than a sample away) */
static int gb_off_grid(const pg_ev_t *ev, int n, double step) {
    int off = 0;
    for (int i = 0; i < n; i++) {
        double g = ev[i].t / step;
        if (fabs(g - floor(g + 0.5)) * step > 1.0) off++;
    }
    return off;
}
/* the share of steps between consecutive notes that are the pattern's C -> D -> E -> C (as pitch classes) */
static double gb_pattern_share(const pg_ev_t *ev, int n) {
    int hit = 0;
    for (int i = 1; i < n; i++) {
        int a = ev[i - 1].note % 12, b = ev[i].note % 12;
        hit += (a == 0 && b == 2) || (a == 2 && b == 4) || (a == 4 && b == 0);
    }
    return n > 1 ? (double)hit / (n - 1) : 0.0;
}
/* play C D E C D E ... `reps` times into Synth 1 (every `gap` samples, velocity `vel`) */
static void gb_teach(void *e, int reps, int gap, int vel) {
    static const int PAT[3] = {60, 62, 64};
    int16_t b[256];
    for (int r = 0; r < reps; r++)
        for (int k = 0; k < 3; k++) {
            note(e, 1, PAT[k], vel);
            for (int t = 0; t < gap; t += 128) E->render(e, b, 128);
            note(e, 0, PAT[k], 0);
        }
}

static void gens_b_checks(void) {
    char what[300];
    int16_t ba[256], bb[256];
    /* off: the very same samples as a fresh instance, with every setting moved (and keys learned), no notes */
    {
        void *a = pg_patch(), *b = pg_patch();
        setkv(b, "rain_rate=100,rain_sync=2,rain_spread=100,rain_speed=100,rain_life=100,rain_echo=100,rain_stones=6,"
                 "rain_keys=1,rain_max=30,mkv_order=0,mkv_creat=100,mkv_rhythm=2,mkv_rate=2,mkv_gate=100,mkv_max=30,"
                 "mkv_forget=1");
        int same = 1, nb = 0;
        for (int k = 0; k < 400; k++) {
            if (k % 40 == 0) { note(a, 1, 60 + k / 40, 100); note(b, 1, 60 + k / 40, 100); }
            if (k % 40 == 20) { note(a, 0, 60 + k / 40, 0); note(b, 0, 60 + k / 40, 0); }
            E->render(a, ba, 128); E->render(b, bb, 128);
            same &= !memcmp(ba, bb, sizeof ba);
        }
        pg_run(b, 0.1, "gen_log", pg_b, &nb);
        check(same && nb == 0, "RAIN / MARKOV off: the very same samples as before with every setting moved and keys "
              "played, no notes");
        E->destroy(a); E->destroy(b);
    }
    /* the same settings: the same notes; every note in the key */
    static const char *const MODES[] = {"rain_on=1", "rain_on=1,rain_echo=100,rain_stones=6,rain_rate=70",
                                        "mkv_on=1", "mkv_on=1,mkv_creat=100,mkv_rhythm=2,mkv_order=0"};
    static const char *const NAMES[] = {"rain", "rain with every echo and 6 stones", "the Markov melody",
                                        "the Markov melody at 100% creativity, sparse, first order"};
    for (int m = 0; m < 4; m++) {
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, "key_root=2,key_scale=10"); setkv(b, "key_root=2,key_scale=10");   /* D pentatonic minor */
        setkv(a, MODES[m]); setkv(b, MODES[m]);
        int na = 0, nb = 0;
        pg_run(a, 12.0, "gen_log", gb_a, &na);
        pg_run(b, 12.0, "gen_log", gb_b, &nb);
        snprintf(what, sizeof what, "%s: %d notes in 12 s, the same again from the same start, all in D pentatonic minor",
                 NAMES[m], na);
        check(na > 5 && pg_same(gb_a, na, gb_b, nb, 0) && pg_in_key(gb_a, na, 2, KS_PENT_MINOR), what);
        E->destroy(a); E->destroy(b);
    }
    /* musical by default: a few notes a second, not a flood */
    {
        static const char *const ON[] = {"rain_on=1", "mkv_on=1"};
        static const char *const NM[] = {"rain", "Markov"};
        for (int m = 0; m < 2; m++) {
            void *a = pg_patch();
            setkv(a, ON[m]);
            int na = 0;
            pg_run(a, 30.0, "gen_log", gb_a, &na);
            snprintf(what, sizeof what, "%s at its defaults: %.2f notes a second (want 0.5 .. 5)", NM[m], na / 30.0);
            check(na >= 15 && na <= 150, what);
            E->destroy(a);
        }
    }
    /* echoes: none at ECHO 0 (a note a drop), more with ECHO and stones */
    {
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, "rain_on=1,rain_rate=60,rain_echo=0,rain_stones=0,rain_max=30");
        setkv(b, "rain_on=1,rain_rate=60,rain_echo=100,rain_stones=6,rain_max=30,rain_life=80");
        int na = 0, nb = 0;
        pg_run(a, 20.0, "gen_log", gb_a, &na);
        pg_run(b, 20.0, "gen_log", gb_b, &nb);
        float va = 0, vb = 0;
        for (int i = 0; i < na; i++) va += gb_a[i].vel;
        for (int i = 0; i < nb; i++) vb += gb_b[i].vel;
        double rate = 0.2 * pow(50.0, 0.6);   /* the drops a second */
        snprintf(what, sizeof what, "ECHO 0: %d notes in 20 s (%.0f drops expected); ECHO 100 with 6 stones: %d notes, "
                 "softer on average (%.0f against %.0f)", na, rate * 20, nb, nb ? vb / nb : 0, na ? va / na : 0);
        check(na > rate * 20 * 0.6 && na < rate * 20 * 1.4 && nb > na * 3 / 2 && vb / nb < va / na, what);
        E->destroy(a); E->destroy(b);
    }
    /* MAX NOTES/S */
    {
        static const char *const KV[] = {"rain_on=1,rain_rate=100,rain_echo=100,rain_stones=6,rain_max=5",
                                         "mkv_on=1,mkv_rate=3,mkv_rhythm=1,mkv_max=5"};
        static const char *const NM[] = {"rain at 10 drops a second with every echo", "Markov on 1/32 steps"};
        for (int m = 0; m < 2; m++) {
            void *a = pg_patch();
            setkv(a, KV[m]);
            int na = 0;
            pg_run(a, 10.0, "gen_log", gb_a, &na);
            int worst = gb_worst(gb_a, na);
            snprintf(what, sizeof what, "MAX NOTES/S 5, %s: %d notes in 10 s, at most %d in any second (want <= 7)", NM[m],
                     na, worst);
            check(na >= 40 && na <= 52 && worst <= 7, what);
            E->destroy(a);
        }
    }
    /* SYNC: rain on 1/16 notes; the Markov melody always on its RATE grid (1/8 at 120 BPM: 11025 samples) */
    {
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, "rain_on=1,rain_sync=3,rain_rate=70,rain_max=16");
        setkv(b, "mkv_on=1");
        int na = 0, nb = 0;
        pg_run(a, 8.0, "gen_log", gb_a, &na);
        pg_run(b, 8.0, "gen_log", gb_b, &nb);
        int oa = gb_off_grid(gb_a, na, 5512.5), ob = gb_off_grid(gb_b, nb, 11025.0);
        snprintf(what, sizeof what, "SYNC: rain on 1/16 %d notes, %d off the grid; Markov on 1/8 %d notes, %d off", na, oa,
                 nb, ob);
        check(na > 5 && oa == 0 && nb > 5 && ob == 0, what);
        E->destroy(a); E->destroy(b);
    }
    /* each Synth its own: Synth 2's rain plays and Synth 1's doesn't; both on: different drops */
    {
        void *a = pg_patch();
        setkv(a, "s2_syn_on=1,s2_rain_on=1");
        int n1 = 0, n2 = 0;
        pg_run(a, 6.0, "gen_log", gb_a, &n1);
        void *b = pg_patch();
        setkv(b, "s2_syn_on=1,s2_rain_on=1");
        pg_run(b, 6.0, "s2_gen_log", gb_b, &n2);
        static char t[4096];
        E->get_param(a, "s3_rain_view", t, sizeof t);
        snprintf(what, sizeof what, "Synth 2's rain plays (%d notes), Synth 1's and 3's don't (%d notes, view \"%.12s\")",
                 n2, n1, t);
        check(n2 > 3 && n1 == 0 && !strncmp(t, "0 0 0;", 6), what);
        E->destroy(a); E->destroy(b);
        a = pg_patch(); b = pg_patch();
        setkv(a, "s2_syn_on=1,s2_rain_on=1,rain_on=1"); setkv(b, "s2_syn_on=1,s2_rain_on=1,rain_on=1");
        n1 = n2 = 0;
        pg_run(a, 6.0, "gen_log", gb_a, &n1);
        pg_run(b, 6.0, "s2_gen_log", gb_b, &n2);
        check(n1 > 3 && n2 > 3 && !pg_same(gb_a, n1, gb_b, n2, 0), "Synths 1 and 2 with the same rain: each its own drops");
        E->destroy(a); E->destroy(b);
    }
    /* HELD KEYS: nothing without a key; from the lowest key held, in the key; nothing after it lets go */
    {
        static const char *const KV[] = {"rain_on=1,rain_keys=1,rain_rate=70,rain_max=12",
                                         "mkv_on=1,mkv_keys=1,mkv_rhythm=1"};
        static const char *const NM[] = {"rain", "Markov"};
        for (int m = 0; m < 2; m++) {
            void *a = pg_patch();
            setkv(a, "key_root=2,key_scale=1");   /* D minor */
            setkv(a, KV[m]);
            int na = 0;
            pg_run(a, 2.0, "gen_log", gb_a, &na);
            int before = na;
            note(a, 1, 50, 100); note(a, 1, 57, 100);
            na = 0;
            pg_run(a, 6.0, "gen_log", gb_a, &na);
            int low = 127;
            for (int i = 0; i < na; i++) if (gb_a[i].note < low) low = gb_a[i].note;
            note(a, 0, 50, 0); note(a, 0, 57, 0);
            int nb = 0;
            pg_run(a, 0.01, "gen_log", gb_b, &nb);
            nb = 0;
            pg_run(a, 3.0, "gen_log", gb_b, &nb);
            snprintf(what, sizeof what, "%s HELD KEYS: %d notes with no key, %d holding D3 + A3 (lowest %d, want >= 50, in "
                     "D minor), %d after letting go", NM[m], before, na, low, nb);
            check(before == 0 && na > 5 && low >= 50 && pg_in_key(gb_a, na, 2, KS_MINOR) && nb == 0, what);
            E->destroy(a);
        }
    }
    /* the Markov melody learns: C D E played over and over, then ON at CREATIVITY 0: it plays the pattern's steps;
     * fresh (the built-in table) it does not. At 35% most of its steps are still the pattern's */
    {
        double share[3];
        int vel_ok = 1, n[3];
        for (int k = 0; k < 3; k++) {
            void *a = pg_patch();
            setkv(a, "key_root=0,key_scale=0,mkv_creat=0");   /* C major */
            if (k) gb_teach(a, 30, 11025, 60);   /* eighth notes at 120 BPM, velocity 60 */
            if (k == 2) set(a, "mkv_creat", 35);
            set(a, "mkv_on", 1);
            int na = 0;
            pg_run(a, 10.0, "gen_log", gb_a, &na);
            share[k] = gb_pattern_share(gb_a, na);
            n[k] = na;
            if (k == 1) for (int i = 0; i < na; i++) vel_ok &= abs(gb_a[i].vel - 60) <= 2;
            E->destroy(a);
        }
        snprintf(what, sizeof what, "MARKOV learns C-D-E: the pattern's steps are %.0f%% of its own (%d notes) at 0%% "
                 "creativity, %.0f%% at 35%% (%d), %.0f%% from the built-in table (%d); it plays at the velocity heard (60)",
                 100 * share[1], n[1], 100 * share[2], n[2], 100 * share[0], n[0]);
        check(share[1] > 0.95 && share[2] > 0.6 && share[0] < 0.4 && n[1] > 20 && vel_ok, what);
    }
    /* its rhythm: quarter notes heard, rate 1/8: it plays every second step (22050 samples), STRAIGHT every step */
    {
        int gaps[2] = {0, 0}, cnt[2] = {0, 0};
        for (int k = 0; k < 2; k++) {
            void *a = pg_patch();
            setkv(a, "key_root=0,key_scale=0,mkv_creat=0");
            gb_teach(a, 20, 22050, 100);
            if (k) set(a, "mkv_rhythm", 1);
            set(a, "mkv_on", 1);
            int na = 0;
            pg_run(a, 10.0, "gen_log", gb_a, &na);
            for (int i = 1; i < na; i++) {
                long long d = gb_a[i].t - gb_a[i - 1].t;
                gaps[k] += d == (k ? 11025 : 22050);
                cnt[k]++;
            }
            E->destroy(a);
        }
        snprintf(what, sizeof what, "MARKOV rhythm: quarter notes learned, %d of %d gaps a quarter; STRAIGHT %d of %d an "
                 "eighth", gaps[0], cnt[0], gaps[1], cnt[1]);
        check(cnt[0] > 10 && gaps[0] == cnt[0] && cnt[1] >= 30 && gaps[1] == cnt[1], what);
    }
    /* each Synth learns its own; FORGET goes back to the built-in table; the view */
    {
        void *a = pg_patch();
        setkv(a, "midi_ch=17,s2_syn_on=1,key_root=0,key_scale=0");   /* (Per Synth: channel 1 is Synth 1's) */
        gb_teach(a, 5, 11025, 90);
        setkv(a, "mkv_on=1,s2_mkv_on=1");
        int na = 0;
        pg_run(a, 1.0, "gen_log", gb_a, &na);
        static char v1[2048], v2[2048], v3[2048];
        E->get_param(a, "markov_view", v1, sizeof v1);
        E->get_param(a, "s2_markov_view", v2, sizeof v2);
        set(a, "mkv_forget", 1);
        pg_run(a, 0.1, "gen_log", gb_a, &na);
        E->get_param(a, "markov_view", v3, sizeof v3);
        int n1 = -1, l1 = -1, sp = -1, o = -1, c = -1, l2 = -1, l3 = -1, x, used = 0, bars = 0, inside = 1;
        sscanf(v1, "%d %d %d %d %d;%n", &n1, &l1, &sp, &o, &c, &used);
        sscanf(v2, "%*d %d", &l2);
        sscanf(v3, "%*d %d", &l3);
        const char *p = v1 + used;
        for (int i = 0; i < n1; i++) {
            int h, ag, u;
            if (sscanf(p, "%d,%d;%n", &h, &ag, &u) != 2) { inside = 0; break; }
            inside &= h >= 0 && h <= sp && ag >= 0;
            p += u;
        }
        for (int u; sscanf(p, "%d,%n", &x, &u) == 1; p += u) { bars++; inside &= x >= 0 && x <= 1000; }
        snprintf(what, sizeof what, "MARKOV: Synth 1 learned %d notes, Synth 2 %d; after FORGET %d; its view: %d notes in "
                 "a %d-degree range, order %d, %d bars (\"%.30s...\")", l1, l2, l3, n1, sp, o, bars, v1);
        check(l1 == 14 && l2 == 0 && l3 == 0 && n1 > 0 && sp == 14 && o == 2 && bars == MK_IV && inside, what);
        E->destroy(a);
    }
    /* a saved sound keeps the settings, not what was learned: from it, the melody plays the built-in table */
    {
        void *a = pg_patch(), *b = pg_patch(), *c = pg_patch();
        setkv(a, "key_root=0,key_scale=0,mkv_creat=20");
        gb_teach(a, 20, 11025, 70);
        static char st1[65536], st2[65536];
        E->get_param(a, "state", st1, sizeof st1);
        E->set_param(b, "state", st1);
        setkv(c, "key_root=0,key_scale=0,mkv_creat=20");
        E->get_param(c, "state", st2, sizeof st2);   /* a fresh one with the same settings: the very same state */
        set(b, "mkv_on", 1); set(c, "mkv_on", 1);
        int n1 = 0, n2 = 0;
        pg_run(b, 6.0, "gen_log", gb_a, &n1);
        pg_run(c, 6.0, "gen_log", gb_b, &n2);
        check(!strcmp(st1, st2) && !strstr(st1, "markov_view") && !strstr(st1, "rain_view") && get(b, "mkv_creat") == 20 &&
              n1 > 5 && pg_same(gb_a, n1, gb_b, n2, 0), "a saved sound keeps the settings, not what the melody learned (its "
              "state is a fresh one's with the same settings; loaded, it plays as a fresh one)");
        E->destroy(a); E->destroy(b); E->destroy(c);
    }
    /* the pond's view: drops, rings and stones inside it */
    {
        void *a = pg_patch();
        setkv(a, "rain_on=1,rain_rate=80,rain_stones=4,rain_life=80");
        int na = 0;
        pg_run(a, 3.0, "gen_log", gb_a, &na);
        static char v[4096];
        E->get_param(a, "rain_view", v, sizeof v);
        int nd = -1, nr = -1, ns = -1, used = 0, ok = 1, cnt = 0;
        sscanf(v, "%d %d %d;%n", &nd, &nr, &ns, &used);
        const char *p = v + used;
        for (int i = 0; i < nd + nr + ns; i++, cnt++) {
            int x, y, z, e = 0, l = 0, u;
            int got = i >= nd && i < nd + nr ? sscanf(p, "%d,%d,%d,%d,%d;%n", &x, &y, &z, &e, &l, &u) - 2
                                              : sscanf(p, "%d,%d,%d;%n", &x, &y, &z, &u);
            if (got != 3) { ok = 0; break; }
            ok &= x >= 0 && x <= 1000 && y >= 0 && y <= 1000 && z >= 0 && z <= 2000 && e >= 0 && e <= 1000;
            p += u;
        }
        snprintf(what, sizeof what, "the pond's view: %d drops, %d rings, %d stones, every one inside (\"%.40s...\")", nd, nr,
                 ns, v);
        check(nd >= 0 && nr > 2 && ns == 4 && cnt == nd + nr + ns && ok, what);
        E->destroy(a);
    }
    /* the modulation sources: finite and 0 .. 1 in the pond itself; through the matrix they move the sound */
    {
        rn_world_t w;
        rn_cfg_t c = {10.0f, 0.5f, 0.6f, 6.0f, 1.0f, 6};
        rn_hit_t h[RN_HITS];
        rn_restart(&w, 7);
        int ok = 1, hits = 0;
        for (int k = 0; k < 20000; k++) {
            hits += rn_step(&w, &c, (float)GB_STEP / SR, h);
            ok &= isfinite(w.energy) && w.energy >= 0.0f && w.energy <= 1.0f;
        }
        static float x0[SR], x1[SR];
        static const struct { int src; const char *on; } S[] = {{MS_RAIN_DROP, "rain_on=1,rain_rate=80"},
                                                               {MS_RIPPLE, "rain_on=1,rain_rate=80"},
                                                               {MS_MK_STEP, "mkv_on=1"}, {MS_MK_HEIGHT, "mkv_on=1"}};
        int moved = 0, finite = 1;
        for (int k = 0; k < 4; k++) {
            void *a = patch(SAW, 5, 300, 80, 200), *b = patch(SAW, 5, 300, 80, 200);
            setkv(a, S[k].on); setkv(b, S[k].on);
            set(a, "mod1_src", 0); set(b, "mod1_src", S[k].src);
            set(a, "mod1_dst", MD_PITCH); set(b, "mod1_dst", MD_PITCH);
            set(a, "mod1_amt", 50); set(b, "mod1_amt", 50);
            set(a, "mod1_via", 0); set(b, "mod1_via", 0);   /* (no controller: the source alone) */
            note(a, 1, 48, 100); note(b, 1, 48, 100);
            render_f(a, x0, SR); render_f(b, x1, SR);
            int diff = 0;
            for (int i = 0; i < SR; i++) { diff |= x0[i] != x1[i]; finite &= isfinite(x1[i]); }
            moved += diff;
            E->destroy(a); E->destroy(b);
        }
        snprintf(what, sizeof what, "the sources: the pond's energy finite and 0..1 over %d hits; RAIN DROP, RIPPLE ENERGY, "
                 "MARKOV STEP and HEIGHT each move the pitch through the matrix (%d of 4), the output finite", hits, moved);
        check(ok && hits > 100 && moved == 4 && finite, what);
    }
}

/* the CPU: the pond alone (the full pools), the table's choice, and the engine with each playing */
static void gens_b_bench(void) {
    enum { STEPS = 400000 };
    rn_world_t w;
    rn_cfg_t c = {10.0f, 0.5f, 0.15f, 8.0f, 1.0f, 6};
    rn_hit_t h[RN_HITS];
    long hits = 0;
    int live = 0;
    struct timespec t0, t1;
    rn_restart(&w, 1);
    for (int k = 0; k < 20000; k++) rn_step(&w, &c, (float)GB_STEP / SR, h);   /* the pools filled */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int k = 0; k < STEPS; k++) hits += rn_step(&w, &c, (float)GB_STEP / SR, h);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    for (int i = 0; i < RN_RIPS; i++) live += w.r[i].live;
    double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / STEPS * (128.0 / GB_STEP);
    printf("bench: rain, 10 drops/s, %d rings, 6 stones (the pond alone): %6.3f us per 128-frame block (%ld hits)\n", live,
           us, hits);
    mk_table_t t;
    mk_builtin(&t, 7);
    uint32_t rng = 1;
    float bars[MK_IV];
    long sum = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int k = 0; k < STEPS; k++) sum += mk_pick(t.t1[k % 7], MK_IV, 0.35f, 0x1FF0u, &rng, bars);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / STEPS;
    printf("bench: Markov, one note's choice: %6.3f us (%ld)\n", us, sum % 7);
    enum { BLOCKS = 20000 };
    int16_t buf[256];
    double eng[4];
    static const char *const KV[] = {"", "rain_rate=100,rain_echo=100,rain_stones=6,rain_life=100,rain_spread=100,"
                                         "rain_speed=100,rain_max=30,mkv_rate=3,mkv_creat=100,mkv_max=30",
                                     "rain_on=1,rain_rate=100,rain_echo=100,rain_stones=6,rain_life=100,rain_max=8,rain_len=20",
                                     "mkv_on=1,mkv_rate=3,mkv_rhythm=1,mkv_max=8,mkv_gate=30"};
    for (int k = 0; k < 4; k++) {   /* a held 4-note chord; every setting moved with both off; rain; Markov */
        void *e = patch(SAW, 5, 300, 70, 250);
        set(e, "voices", 7);
        for (int n = 0; n < 4; n++) note(e, 1, 48 + 3 * n, 100);
        setkv(e, KV[k]);
        for (int b = 0; b < 2000; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        eng[k] = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
        E->destroy(e);
    }
    printf("bench: engine, 4 held voices %6.2f us; both off, every setting moved %6.2f us; with the rain (32 rings, 6 "
           "stones, up to 8 notes/s into the voices) %6.2f us; with the Markov melody (1/32, up to 8 notes/s) %6.2f us per "
           "128-frame block\n", eng[0], eng[1], eng[2], eng[3]);
}
