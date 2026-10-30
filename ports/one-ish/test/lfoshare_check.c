/* (2026-10-30) SHARE (lfoN_share): LFO n of Synths 2 and 3 plays Synth 1's LFO n, in step. Included by offline_check.c
 * after the multitimbral checks, so it uses that file's helpers (check, set, setkv, note, get, create_one ...).
 *   off       three Synths with different LFO settings: their LFO outputs differ (free-running and in the voices)
 *   on        SHARE on all four: every LFO output of Synths 2 and 3, free-running and in every voice (note reset,
 *             start phase, delay, fade in, smoothing, S&H, synced, the High range), equals Synth 1's, sample for
 *             sample at every control step, notes started at odd moments included; with Synth 1 switched off too
 *   restore   SHARE off: Synth 2's and 3's own settings are still there and heard again
 *   state     a state with SHARE on round trips (SHARE and the Synths' own settings), and plays in step again
 *   bench     (offline_bench) CPU with SHARE off and on */

static int ls_read(void *e, int c, char *b, int n) {
    char k[16];
    snprintf(k, sizeof k, "lfo_out%d", c + 1);
    return E->get_param(e, k, b, n);
}
/* three Synths on, each its own LFO settings (all four LFOs, every kind of setting on their pages) */
static void *ls_patch(void) {
    void *e = patch(SAW, 1, 100, 100, 50);
    setkv(e, "s2_syn_on=1,s3_syn_on=1");
    static const char *const AMP[] = {"amp_a", "amp_d", "amp_s", "amp_r", "volume"};   /* the same voices in each */
    for (unsigned i = 0; i < sizeof AMP / sizeof AMP[0]; i++) {
        char k[24];
        double v = get(e, AMP[i]);
        snprintf(k, sizeof k, "s2_%s", AMP[i]); set(e, k, v);
        snprintf(k, sizeof k, "s3_%s", AMP[i]); set(e, k, v);
    }
    /* Synth 1: LFO 1 a free sine with variation, smoothing, delay and fade in; LFO 2 note reset at 25%, a saw;
     * LFO 3 S&H, unipolar, the High range (per sample); LFO 4 synced, dotted, repeats 3, note reset */
    setkv(e, "lfo1_rate=70,lfo1_wave=1,lfo1_var=20,lfo1_smooth=10,lfo1_delay=30,lfo1_fade_in=40,"
             "lfo2_rate=60,lfo2_wave=3,lfo2_reset=1,lfo2_phase=25,"
             "lfo3_rate=50,lfo3_wave=5,lfo3_polarity=1,lfo3_range=3,"
             "lfo4_sync=1,lfo4_rate=60,lfo4_timing=1,lfo4_repeats=3,lfo4_reset=1");
    /* Synth 2 and 3: other settings each */
    setkv(e, "s2_lfo1_rate=40,s2_lfo1_wave=3,s2_lfo2_rate=80,s2_lfo2_wave=2,s2_lfo3_rate=20,s2_lfo3_wave=0,"
             "s2_lfo4_rate=30,s2_lfo4_wave=1,s2_lfo1_fade_out=50");
    setkv(e, "s3_lfo1_rate=55,s3_lfo1_wave=4,s3_lfo2_rate=35,s3_lfo2_reset=1,s3_lfo3_range=3,s3_lfo3_rate=10,"
             "s3_lfo4_rate=85,s3_lfo4_wave=6");
    return e;
}
static void ls_share(void *e, int on) {
    for (int n = 1; n <= 4; n++) {
        char k[16];
        snprintf(k, sizeof k, "lfo%d_share", n);
        set(e, k, on);
    }
}
/* render `pieces` pieces of 37 frames (so control steps and notes fall at odd places); count the pieces where every
 * Synth's LFO outputs (free and per voice) were the same as Synth 1's, and the pieces where Synth 1's changed */
static void ls_run(void *e, int pieces, int keys, int *same, int *moved, int *voices) {
    static char a[4096], b[4096], prev[4096];
    int16_t buf[2 * 37];
    *same = *moved = *voices = 0;
    prev[0] = 0;
    for (int p = 0; p < pieces; p++) {
        if (keys && p % 50 == 7) note(e, 1, 48 + (p / 50) % 24, 100);   /* a new note now and then */
        if (keys && p % 50 == 33) note(e, 0, 48 + (p / 50) % 24, 0);
        E->render(e, buf, 37);
        ls_read(e, 0, a, sizeof a);
        int all = 1;
        for (int c = 1; c < 3; c++) {
            ls_read(e, c, b, sizeof b);
            all &= !strcmp(a, b);
        }
        *same += all;
        *moved += strcmp(a, prev) != 0;
        *voices += strchr(a, '|') != NULL;
        snprintf(prev, sizeof prev, "%s", a);
    }
}

static void lfo_share_checks(void) {
    char what[160];
    int same, moved, voices;
    void *e = ls_patch();
    check(get(e, "lfo1_share") == 0 && get(e, "lfo4_share") == 0, "SHARE: off by default");
    check(pindex("s2_lfo1_share") < 0, "SHARE: one shared copy (no S2 / S3 params)");

    /* off: the Synths' own LFOs */
    ls_run(e, 2000, 1, &same, &moved, &voices);
    snprintf(what, sizeof what, "SHARE off: Synths 2 and 3 run their own LFOs (the same as Synth 1's in %d of 2000)",
             same);
    check(same < 20 && moved > 1000, what);

    /* on: Synth 1's, in step. The voices sounding now took their own LFO copies at their note-on; once they end,
     * every voice is started under SHARE. */
    ls_share(e, 1);
    for (int k = 0; k < 128; k++) note(e, 0, k, 0);
    ls_run(e, 300, 0, &same, &moved, &voices);   /* the releases end */
    ls_run(e, 4000, 1, &same, &moved, &voices);
    snprintf(what, sizeof what, "SHARE on: every LFO output of Synths 2 and 3 equals Synth 1's (%d of 4000 pieces, %d "
             "with voices, Synth 1's moved in %d)", same, voices, moved);
    check(same == 4000 && voices > 1500 && moved > 2000, what);
    check(get(e, "s2_lfo1_rate") == 40 && get(e, "s3_lfo4_rate") == 85 && get(e, "lfo1_share") == 1,
          "SHARE on: the Synths' own settings stay stored");

    /* the state: SHARE and every Synth's own settings come back, and it plays in step */
    static char st[VST2_CHUNK_MAX + 1000];
    int len = E->get_param(e, "state", st, sizeof st);
    void *b = create_one(NULL);
    E->set_param(b, "state", st);
    int kept = len > 0 && get(b, "lfo2_share") == 1 && get(b, "lfo3_share") == 1;
    static const char *const K[] = {"lfo1_rate", "s2_lfo1_rate", "s2_lfo1_wave", "s3_lfo3_range", "s3_lfo4_wave",
                                    "lfo4_timing", "s2_lfo1_fade_out", "s2_syn_on"};
    for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) kept &= get(b, K[i]) == get(e, K[i]);
    check(kept, "SHARE: a state with SHARE on round trips (SHARE and each Synth's own LFO settings)");
    ls_run(b, 1500, 1, &same, &moved, &voices);
    snprintf(what, sizeof what, "SHARE: the loaded state plays in step (%d of 1500)", same);
    check(same == 1500 && voices > 500, what);
    E->destroy(b);

    /* Synth 1 off: Synths 2 and 3 still share its LFOs, which keep moving */
    set(e, "syn_on", 0);
    for (int k = 0; k < 128; k++) note(e, 0, k, 0);
    ls_run(e, 1200, 0, &same, &moved, &voices);   /* Synth 1's last notes end */
    {
        static char x[4096], y[4096], prev[4096];
        int16_t buf[2 * 37];
        int eq = 0, mv = 0, vc = 0;
        prev[0] = 0;
        for (int p = 0; p < 2000; p++) {
            if (p % 50 == 7) note(e, 1, 60 + (p / 50) % 12, 100);
            if (p % 50 == 33) note(e, 0, 60 + (p / 50) % 12, 0);
            E->render(e, buf, 37);
            ls_read(e, 1, x, sizeof x);
            ls_read(e, 2, y, sizeof y);
            eq += !strcmp(x, y);
            mv += strcmp(x, prev) != 0;
            vc += strchr(x, '|') != NULL;
            snprintf(prev, sizeof prev, "%s", x);
        }
        snprintf(what, sizeof what, "SHARE with Synth 1 off: Synths 2 and 3 in step (%d of 2000, %d with voices), the "
                 "LFOs moving (%d)", eq, vc, mv);
        check(eq == 2000 && vc > 500 && mv > 1000, what);
    }
    set(e, "syn_on", 1);

    /* off again: each Synth's own settings are back and heard */
    ls_share(e, 0);
    for (int k = 0; k < 128; k++) note(e, 0, k, 0);
    ls_run(e, 300, 0, &same, &moved, &voices);
    ls_run(e, 2000, 1, &same, &moved, &voices);
    snprintf(what, sizeof what, "SHARE off again: each Synth's own LFOs (the same as Synth 1's in %d of 2000)", same);
    check(same < 20 && get(e, "s2_lfo2_wave") == 2 && get(e, "s3_lfo3_range") == 3, what);
    E->destroy(e);

    /* SHARE switched on mid-play, after Synth 2 ran its own LFO 1 for a while: its free-running LFOs are Synth 1's at the next control step */
    e = ls_patch();
    setkv(e, "s2_lfo1_rate=70,s2_lfo1_wave=1,s2_lfo1_var=20,s2_lfo1_smooth=10,s2_lfo1_delay=30,s2_lfo1_fade_in=40");
    ls_run(e, 1000, 1, &same, &moved, &voices);
    ls_share(e, 1);
    ls_run(e, 300, 0, &same, &moved, &voices);
    char a[4096], b2[4096];
    ls_read(e, 0, a, sizeof a);
    ls_read(e, 1, b2, sizeof b2);
    check(!strncmp(a, b2, strcspn(a, "|")), "SHARE on mid-play: Synth 2's free-running LFOs are Synth 1's at once");
    E->destroy(e);
}

/* CPU: three Synths, 4 keys each, every LFO moving (offline_bench). "own settings": Synths 2 and 3 their own LFO
 * settings, SHARE off, then on (on, they run Synth 1's: its High-range LFO 3 per sample in every voice). "the same
 * settings": every Synth already has Synth 1's LFO settings, so off and on run the same LFOs and only SHARE's own work
 * differs (the copy per control step, reading Synth 1's settings). */
static void lfo_share_bench(void) {
    static const char *const X[] = {"rate", "wave", "sync", "range", "var", "polarity", "phase", "reset", "delay",
                                    "fade_in", "fade_out", "repeats", "smooth", "timing"};
    for (int same = 0; same < 2; same++)
        for (int on = 0; on < 2; on++) {
            void *e = ls_patch();
            for (int n = 1; n <= 4 && same; n++)
                for (unsigned x = 0; x < sizeof X / sizeof X[0]; x++) {
                    char k[32], k2[40];
                    snprintf(k, sizeof k, "lfo%d_%s", n, X[x]);
                    double v = get(e, k);
                    snprintf(k2, sizeof k2, "s2_%s", k); set(e, k2, v);
                    snprintf(k2, sizeof k2, "s3_%s", k); set(e, k2, v);
                }
            ls_share(e, on);
            for (int k = 0; k < 4; k++) note(e, 1, 60 + 3 * k, 100);
            int16_t buf[256];
            for (int i = 0; i < 200; i++) E->render(e, buf, 128);
            double best = 1e9;
            for (int r = 0; r < 5; r++) {
                struct timespec t0, t1;
                clock_gettime(CLOCK_MONOTONIC, &t0);
                for (int i = 0; i < 3000; i++) E->render(e, buf, 128);
                clock_gettime(CLOCK_MONOTONIC, &t1);
                double us = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / 3000;
                if (us < best) best = us;
            }
            printf("bench SHARE %s (%s): 3 Synths x 4 keys, 4 LFOs each: %.2f us per 128-frame block\n",
                   on ? "on " : "off", same ? "the same settings" : "own settings", best);
            E->destroy(e);
        }
}
