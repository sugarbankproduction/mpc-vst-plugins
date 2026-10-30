/* (2026-10-30) THE GENERATORS' KEYS and GEN TEMPO (src/seq.h GK_*, engine.c fire_step / clk_go, src/physgen_run.h,
 * src/gens_a_run.h, src/gens_b_run.h, src/synths.h state_absent):
 *   - FREE Euclid and Turing play without any key, in the Synth's KEY and SCALE, from LOW OCTAVE over OCTAVES;
 *   - a sound saved before gen_keys existed (the key missing) plays Euclid / Turing from the held keys, as it did;
 *     a new sound saves its FREE;
 *   - HELD: every generator plays while a key is held and nothing starts after the last key is let go (SYNC grids,
 *     the Markov melody's next line, the pond's drops included);
 *   - KEY LAUNCH: nothing before a key; then a phrase (Euclid: one pattern; Turing: one loop; Markov: 8 notes ending on
 *     the key; rain: a shower) or a start (LIFE, ORBITS, PENDULUMS) from that key;
 *   - KEY EMIT (rain): no rain without a key, more the harder it is played, none after it is let go;
 *   - GEN TEMPO: OWN 60 BPM gives half the events of GLOBAL 120 (Euclid, Markov, LIFE's synced RATE, the pendulums'
 *     CYCLE, FOLLOW TEMPO's balls); GLOBAL is exactly as before; OWN ignores the host's song position.
 * Included from offline_check.c after gens_a_check.c (its helpers: pg_run, pg_patch, pg_in_key, pg_same; set, setkv,
 * note, check, get). */

static int gk_count_after(const pg_ev_t *ev, int n, long long t) {   /* note-ons at or after sample t */
    int c = 0;
    for (int i = 0; i < n; i++) c += ev[i].t >= t;
    return c;
}

/* kv on a fresh patch; hold `key` (vel) from `on_s` for `hold_s` seconds (key < 0: no key), run `secs` in all; the
 * note-ons into pg_a; *rel: the sample the key was let go at */
static int gk_play(const char *kv, int key, int vel, double on_s, double hold_s, double secs, long long *rel) {
    void *a = pg_patch();
    setkv(a, kv);
    int n = 0;
    pg_run(a, on_s, "gen_log", pg_a, &n);
    if (key >= 0) note(a, 1, key, vel);
    pg_run(a, hold_s, "gen_log", pg_a, &n);
    if (rel) *rel = (long long)(((long)(on_s * SR) + 127) / 128 * 128 + ((long)(hold_s * SR) + 127) / 128 * 128);
    if (key >= 0) note(a, 0, key, 0);
    pg_run(a, secs - on_s - hold_s, "gen_log", pg_a, &n);
    E->destroy(a);
    return n;
}

static void gen_tempo_src_checks(void);
static void gen_follow_keys_checks(void);
static void gen_keys_checks(void) {
    char what[400];
    /* FREE (the new default): Euclid and Turing with no key at all, in D pentatonic minor from LOW OCTAVE */
    {
        void *a = E->create(NULL);
        check(get(a, "gen_keys") == 0 && rnd_section_of(pindex("gen_keys")) == RS_NEVER &&
              rnd_section_of(pindex("gen_tempo_src")) == RS_NEVER, "Euclid / Turing KEYS: FREE for a new sound; "
              "RANDOMISE / MUTATE never touch KEYS or GEN TEMPO");
        E->destroy(a);
        int n = gk_play("key_root=2,key_scale=10,gen_euclid_on=1", -1, 0, 0, 0, 4.0, NULL), lo = 999, hi = 0;
        for (int i = 0; i < n; i++) { lo = pg_a[i].note < lo ? pg_a[i].note : lo; hi = pg_a[i].note > hi ? pg_a[i].note : hi; }
        snprintf(what, sizeof what, "Euclid FREE, no key: E(5,16) at 1/16, 120 BPM plays %d hits in 4 s (want 10), the "
                 "root of D at LOW OCTAVE C3 (notes %d .. %d, want 50)", n, lo, hi);
        check(n >= 9 && n <= 11 && lo == 50 && hi == 50 && pg_in_key(pg_a, n, 2, KS_PENT_MINOR), what);
        n = gk_play("key_root=2,key_scale=10,gen_euclid_on=1,gen_eu_notes=1,gen_range=1", -1, 0, 0, 0, 8.0, NULL);
        int distinct = 0, seen[128] = {0};
        for (int i = 0; i < n; i++) if (!seen[pg_a[i].note]++) distinct++;
        snprintf(what, sizeof what, "Euclid FREE, EUCLID NOTES Scale Up over 2 octaves: %d hits on %d different notes, "
                 "all in D pentatonic minor", n, distinct);
        check(n >= 18 && distinct == 10 && pg_in_key(pg_a, n, 2, KS_PENT_MINOR), what);
        n = gk_play("key_root=2,key_scale=10,gen_tm_on=1,gen_tm_change=50,gen_range=1,gen_octave=1", -1, 0, 0, 0, 4.0, NULL);
        lo = 999; hi = 0;
        for (int i = 0; i < n; i++) { lo = pg_a[i].note < lo ? pg_a[i].note : lo; hi = pg_a[i].note > hi ? pg_a[i].note : hi; }
        snprintf(what, sizeof what, "Turing FREE, no key: %d notes in 4 s (a step each 1/16), in D pentatonic minor between "
                 "D2 and D4 (%d .. %d)", n, lo, hi);
        check(n >= 30 && lo >= 38 && hi <= 62 && hi > lo && pg_in_key(pg_a, n, 2, KS_PENT_MINOR), what);
        /* a key played meanwhile sounds as usual (it is not taken) */
        n = gk_play("gen_euclid_on=1,gen_hits=0", 72, 100, 0.2, 0.5, 1.0, NULL);
        void *b = pg_patch();
        setkv(b, "gen_euclid_on=1,gen_hits=0");
        note(b, 1, 72, 100);
        float buf[512];
        float pk = 0;
        for (int k = 0; k < 40; k++) { render_f(b, buf, 128); for (int i = 0; i < 128; i++) pk = fmaxf(pk, fabsf(buf[i])); }
        E->destroy(b);
        snprintf(what, sizeof what, "Euclid FREE: a key played meanwhile sounds as usual (peak %.3f), the rhythm takes none", pk);
        check(pk > 0.05f && n == 0, what);
    }
    /* old sounds: the key missing means HELD KEYS; a new sound writes its FREE */
    {
        void *a = E->create(NULL);
        E->set_param(a, "state", "v=1;gen_euclid_on=1");
        float gk = get(a, "gen_keys");
        int n = 0;
        pg_run(a, 2.0, "gen_log", pg_a, &n);
        note(a, 1, 60, 100);
        int before = n;
        pg_run(a, 2.0, "gen_log", pg_a, &n);
        static char st[1 << 16];
        E->set_param(a, "state", "v=1");
        int len = E->get_param(a, "state", st, sizeof st);
        int has = len > 0 && strstr(st, ";gen_keys=1") != NULL;
        E->destroy(a);
        void *b = E->create(NULL);
        len = E->get_param(b, "state", st, sizeof st);
        int fresh = len > 0 && strstr(st, ";gen_keys=0") != NULL;
        E->set_param(b, "state", st);
        float back = get(b, "gen_keys");
        E->destroy(b);
        snprintf(what, sizeof what, "a sound saved before KEYS existed: Euclid from the held keys (KEYS %g, %d notes "
                 "without a key, %d with); it saves nothing for it (%d); a new sound saves FREE (%d) and reads it back (%g)",
                 gk, before, n - before, has, fresh, back);
        check(gk == 1 && before == 0 && n - before > 3 && !has && fresh && back == 0, what);
    }
    /* HELD: notes while the key is held, none starting after it is let go */
    static const char *const HELD[] = {
        "gen_keys=1,gen_euclid_on=1", "gen_keys=1,gen_tm_on=1",
        "phys_keys=1,phys_balls_on=1,phys_sync=1,phys_max=30", "phys_keys=1,phys_parts_on=1,phys_sync=1,phys_max=30",
        "rain_keys=1,rain_on=1,rain_sync=1,rain_rate=80", "mkv_keys=1,mkv_on=1",
        "life_keys=1,life_on=1", "orb_keys=1,orb_on=1,orb_quant=1", "pend_keys=1,pend_on=1,pend_cycle=0"};
    for (unsigned g = 0; g < sizeof HELD / sizeof HELD[0]; g++) {
        long long rel;
        int n = gk_play(HELD[g], 55, 100, 0.5, 3.0, 6.0, &rel), during = n - gk_count_after(pg_a, n, rel);
        int after = gk_count_after(pg_a, n, rel), first = n ? (int)pg_a[0].t : -1;
        snprintf(what, sizeof what, "HELD KEYS %s: %d notes while the key is held (from %.2f s), %d after it is let go",
                 HELD[g], during, first / (double)SR, after);
        check(during > 0 && after == 0 && first >= (int)(0.5 * SR) - 128, what);
    }
    /* KEY LAUNCH */
    {
        long long rel;
        int n = gk_play("gen_keys=2,gen_euclid_on=1,gen_steps=8,gen_hits=3,key_scale=1", 50, 90, 1.0, 0.05, 5.0, &rel);
        int same = 1;
        for (int i = 0; i < n; i++) same &= pg_a[i].note == 50 && pg_a[i].vel == 90;
        snprintf(what, sizeof what, "KEY LAUNCH Euclid E(3,8): nothing before the key, then one pattern (%d hits, want 3) "
                 "from the key (D3, its velocity), played on after a short press", n);
        check(n == 3 && same && pg_a[0].t >= (long long)(1.0 * SR) - 128, what);
        n = gk_play("gen_keys=2,gen_tm_on=1,gen_tm_len=8,key_scale=1", 50, 90, 1.0, 0.05, 5.0, &rel);
        snprintf(what, sizeof what, "KEY LAUNCH Turing (LENGTH 8): one loop from the key (%d notes, want 8), in D minor (FOLLOW KEYS: the key D played)",
                 n);
        check(n == 8 && pg_in_key(pg_a, n, 2, KS_MINOR) && pg_a[0].note >= 48 && pg_a[0].t >= (long long)(1.0 * SR) - 128,
              what);
        n = gk_play("mkv_keys=2,mkv_on=1,mkv_max=30,key_scale=1", 51, 100, 1.0, 0.05, 6.0, &rel);
        snprintf(what, sizeof what, "KEY LAUNCH Markov: nothing before the key, then a phrase of %d notes (want 8) ending "
                 "on the key (%d, want 51)", n, n ? pg_a[n - 1].note : -1);
        check(n == 8 && pg_a[n - 1].note == 51 && pg_a[0].t >= (long long)(1.0 * SR) - 128, what);
        n = gk_play("rain_keys=2,rain_on=1,rain_echo=0,rain_max=30", 60, 127, 1.0, 0.05, 5.0, &rel);
        int late = gk_count_after(pg_a, n, (long long)(2.5 * SR));
        snprintf(what, sizeof what, "KEY LAUNCH rain: no drops before the key, a shower of %d notes within a second (12 "
                 "drops at velocity 127, some over MAX NOTES/S), %d later", n, late);
        check(n >= 6 && n <= 12 && late == 0 && pg_a[0].t >= (long long)(1.0 * SR) - 128, what);
        static const char *const START[] = {"life_keys=2,life_on=1", "orb_keys=2,orb_on=1",
                                            "pend_keys=2,pend_on=1,pend_cycle=0,pend_damp=100"};
        for (int g = 0; g < 3; g++) {
            n = gk_play(START[g], 48, 100, 2.0, 0.05, 12.0, &rel);
            int before = n - gk_count_after(pg_a, n, (long long)(2.0 * SR) - 128), end = gk_count_after(pg_a, n, 9 * SR);
            snprintf(what, sizeof what, "KEY LAUNCH %s: %d notes before the key, %d after it (from the key on)%s", START[g],
                     before, n - before, g == 2 ? (end ? "; damped, still playing at 9 s" : "; damped, swung out by 9 s") : "");
            check(before == 0 && n - before > 2 && (g != 2 || end == 0), what);
        }
    }
    /* KEY EMIT: rain only while the key is held, more when played harder */
    {
        long long rel;
        int soft = gk_play("rain_keys=3,rain_on=1,rain_rate=70,rain_echo=0,rain_max=30", 60, 32, 1.0, 4.0, 6.0, &rel);
        int none = gk_count_after(pg_a, soft, rel + (long long)(0.35 * SR)), pre = soft - gk_count_after(pg_a, soft, SR - 128);
        int hard = gk_play("rain_keys=3,rain_on=1,rain_rate=70,rain_echo=0,rain_max=30", 60, 127, 1.0, 4.0, 6.0, &rel);
        int none2 = gk_count_after(pg_a, hard, rel + (long long)(0.35 * SR));
        snprintf(what, sizeof what, "KEY EMIT rain: %d drops before the key, %d in 4 s at velocity 32, %d at 127; after "
                 "the key is let go (and the last drops have landed) %d, %d", pre, soft, hard, none, none2);
        check(pre == 0 && soft > 0 && hard > 2 * soft && none == 0 && none2 == 0, what);
    }
    /* random sounds with ARPEGGIATOR and GENERATIVE (Euclid / Turing FREE playing by themselves), then every param back
     * to its default one by one (as a preset loads): silence after the release, nothing left sounding */
    {
        static float def[NPARAMS], out[NPARAMS];
        rnd_defaults(def);
        int loud = 0, worst_s = -1;
        double worst = 0;
        for (int s = 0; s < 12; s++) {
            rnd_opts_t o = {1.0f, 1u << RS_ARP | 1u << RS_GEN, 0};
            uint32_t seed = 777 + 131 * s;
            rnd_generate_x(def, out, 0, &seed, &o);
            void *a = pg_patch();
            rnd_apply_norm(a, out);
            float buf[512];
            for (int k = 0; k < 300; k++) render_f(a, buf, 128);
            rnd_apply_norm(a, def);
            for (int k = 0; k < 2000; k++) render_f(a, buf, 128);   /* (5.8 s: the longest release) */
            double sum = 0;
            for (int k = 0; k < 40; k++) { render_f(a, buf, 128); for (int i = 0; i < 128; i++) sum += buf[i] * buf[i]; }
            double rms = sqrt(sum / (40 * 128));
            if (rms > 0.001) loud++;
            if (rms > worst) { worst = rms; worst_s = s; }
            E->destroy(a);
        }
        snprintf(what, sizeof what, "12 random sounds with GENERATIVE playing FREE, then every param back to its default: "
                 "%d still sounding (the worst rms %.4f, sound %d)", loud, worst, worst_s);
        check(loud == 0, what);
    }
    gen_follow_keys_checks();
    gen_tempo_src_checks();
}

/* GEN TEMPO: OWN 60 against GLOBAL 120 (the host's) */
static int gk_count(const char *kv, const char *bpm, double secs) {
    void *a = pg_patch();
    setkv(a, kv);
    E->set_param(a, "lfo_bpm", bpm);
    int n = 0;
    pg_run(a, secs, "gen_log", pg_a, &n);
    E->destroy(a);
    return n;
}
static void gen_tempo_src_checks(void) {
    char what[400];
    static const char *const ON[] = {
        "gen_euclid_on=1", "mkv_on=1,mkv_rhythm=1,mkv_max=30", "life_on=1,life_mode=1,life_notes=1,life_max=30",
        "pend_on=1,pend_cycle=0,pend_max=30",
        "phys_balls_on=1,phys_balls=4,phys_gravity=0,phys_bounce=100,phys_speed=75,phys_max=30"};
    static const char *const NAME[] = {"Euclid", "Markov", "LIFE (1/8)", "pendulums", "balls (FOLLOW TEMPO)"};
    for (int g = 0; g < 5; g++) {
        char own[160], glob[160];
        snprintf(own, sizeof own, "%s,gen_tempo_src=1,gen_bpm=60", ON[g]);
        snprintf(glob, sizeof glob, "%s,gen_tempo_src=0,gen_bpm=60", ON[g]);
        int n120 = gk_count(ON[g], "120", 16.0), n60 = gk_count(own, "120", 16.0), host60 = gk_count(ON[g], "60", 16.0);
        void *a = pg_patch(), *b = pg_patch();
        setkv(a, ON[g]); setkv(b, glob);
        int na = 0, nb = 0;
        pg_run(a, 8.0, "gen_log", pg_a, &na);
        pg_run(b, 8.0, "gen_log", pg_b, &nb);
        int same = pg_same(pg_a, na, pg_b, nb, 0);
        E->destroy(a); E->destroy(b);
        double r = (double)n60 / n120, rh = (double)host60 / n120;
        snprintf(what, sizeof what, "GEN TEMPO %s: OWN 60 BPM (the host at 120) %d events in 16 s against GLOBAL 120 %d "
                 "(x %.2f, want 0.5; the host itself at 60: x %.2f); GLOBAL with a BPM set: the very same %d notes",
                 NAME[g], n60, n120, r, rh, na);
        check(n120 > 8 && r > 0.4 && r < 0.62 && rh > 0.4 && rh < 0.62 && same && na > 3, what);
    }
    /* OWN ignores the host's song position: 120 OWN with the host playing at 60 = 120 GLOBAL free */
    {
        int n120 = gk_count("gen_euclid_on=1", "120", 8.0);
        void *a = pg_patch();
        setkv(a, "gen_euclid_on=1,gen_tempo_src=1,gen_bpm=120");
        int16_t buf[256];
        static char lg[8192];
        int n = 0;
        double ppq = 0.0;
        for (long at = 0; at < (long)(8.0 * SR); at += 128) {
            E->song_pos(a, ppq, 60.0, 1, 128);
            ppq += 128 * 60.0 / 60.0 / SR;
            E->render(a, buf, 128);
            if (E->get_param(a, "gen_log", lg, sizeof lg) <= 0) continue;
            for (char *p = lg; *p;) {
                long long t;
                int nt, vl, used;
                if (sscanf(p, "%lld:%d:%d;%n", &t, &nt, &vl, &used) != 3) break;
                n += vl > 0;
                p += used;
            }
        }
        E->destroy(a);
        snprintf(what, sizeof what, "GEN TEMPO OWN 120 with the host playing at 60 by its song position: %d Euclid hits "
                 "in 8 s, as GLOBAL at 120 (%d)", n, n120);
        check(abs(n - n120) <= 1 && n120 > 10, what);
    }
    /* a change of OWN BPM mid-run: the ball moves on smoothly (FOLLOW TEMPO's glide), all finite */
    {
        void *a = pg_patch();
        setkv(a, "phys_balls_on=1,phys_balls=1,phys_gravity=0,phys_bounce=100,phys_speed=50,gen_tempo_src=1,gen_bpm=120");
        int16_t buf[256];
        char v[2048];
        int worst = 0, ok = 1, px = -1, py = -1;
        for (int blk = 0; blk < (int)(4.0 * SR / 128); blk++) {
            if (blk == (int)(2.0 * SR / 128)) set(a, "gen_bpm", 300);
            E->render(a, buf, 128);
            int nb, np, wl, x = -1, y = -1, f;
            if (E->get_param(a, "phys_view", v, sizeof v) <= 0 || sscanf(v, "%d %d %d;%d,%d,%d;", &nb, &np, &wl, &x, &y, &f) != 6)
                continue;
            ok &= x >= 0 && x <= 1000 && y >= 0 && y <= 1000;
            if (px >= 0 && abs(x - px) + abs(y - py) > worst) worst = abs(x - px) + abs(y - py);
            px = x; py = y;
        }
        E->destroy(a);
        snprintf(what, sizeof what, "GEN TEMPO OWN 120 -> 300 mid-run: the ball moves on smoothly (the most in one block "
                 "%d of 1000), all finite", worst);
        check(ok && worst > 0 && worst < 40, what);
    }
}

/* FOLLOW KEYS (on for a new sound): a generator playing from the keys plays in the key played. The Synth in C minor:
 * D held -> every note in D minor; C E G held -> only Cs, Es and Gs; off -> C minor (as before) */
static int gk_pcs_ok(const pg_ev_t *ev, int n, unsigned pcs) {
    for (int i = 0; i < n; i++) if (!(pcs >> (ev[i].note % 12) & 1)) return 0;
    return 1;
}
static void gen_follow_keys_checks(void) {
    char what[400];
    static const char *const G[] = {
        "phys_keys=1,phys_balls_on=1,phys_max=30,phys_range=2", "rain_keys=1,rain_on=1,rain_rate=80,rain_max=30,rain_range=2",
        "mkv_keys=1,mkv_on=1,mkv_creat=100,mkv_max=30", "life_keys=1,life_on=1,life_range=2",
        "orb_keys=1,orb_on=1,orb_range=2", "pend_keys=1,pend_on=1,pend_cycle=0,pend_count=12",
        "rain_keys=3,rain_on=1,rain_rate=80,rain_max=30,rain_range=2"};
    unsigned dmin = 0, cmin = 0;
    for (int k = 0; k < KS_SIZE[KS_MINOR]; k++) {
        dmin |= 1u << ((2 + KS_STEPS[KS_MINOR][k]) % 12);
        cmin |= 1u << KS_STEPS[KS_MINOR][k];
    }
    const unsigned ceg = 1u << 0 | 1u << 4 | 1u << 7;
    for (unsigned g = 0; g < sizeof G / sizeof G[0]; g++) {
        int got[3], ok[3];
        for (int m = 0; m < 3; m++) {   /* 0: D held, on; 1: C E G held, on; 2: D held, off */
            void *a = pg_patch();
            setkv(a, "key_root=0,key_scale=1");
            setkv(a, G[g]);
            if (m == 2) set(a, "gen_follow_keys", 0);
            if (m == 1) { note(a, 1, 60, 100); note(a, 1, 64, 100); note(a, 1, 67, 100); }
            else note(a, 1, 50, 100);
            int n = 0;
            pg_run(a, 8.0, "gen_log", pg_a, &n);
            E->destroy(a);
            got[m] = n;
            ok[m] = n > 2 && gk_pcs_ok(pg_a, n, m == 0 ? dmin : m == 1 ? ceg : cmin);
        }
        snprintf(what, sizeof what, "FOLLOW KEYS %s (the Synth in C minor): D held, %d notes all in D minor (%d); C E G "
                 "held, %d all Cs, Es, Gs (%d); off, %d all in C minor (%d)", G[g], got[0], ok[0], got[1], ok[1], got[2],
                 ok[2]);
        check(ok[0] && ok[1] && ok[2], what);
    }
    {
        void *a = E->create(NULL);
        E->set_param(a, "state", "v=1;phys_balls_on=1");
        float was = get(a, "gen_follow_keys");
        E->destroy(a);
        a = E->create(NULL);
        float now = get(a, "gen_follow_keys");
        E->destroy(a);
        snprintf(what, sizeof what, "FOLLOW KEYS: on for a new sound (%g), off for a sound saved before it existed (%g)",
                 now, was);
        check(now == 1 && was == 0, what);
    }
}
