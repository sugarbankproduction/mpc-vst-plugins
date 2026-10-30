/* (2026-10-30) Multi-out offline checks (src/synths.h one_ish_render_multi; the desktop's eight outputs,
 * desktop/hostrate.h): included by offline_check.c after the other checks, so it uses that file's helpers (check, set,
 * note, E, E_REAL ...).
 *   default    every OUTPUT at Main: MAIN is exactly one_ish_render_float()'s (three Synths, the master effects on) and
 *              every pair is silent
 *   own pair   a Synth on Own Pair is on its pair and not on MAIN; MAIN + the pair = the default's MAIN
 *   both       Main + Own: MAIN exactly the default's, and the Synth on its pair too
 *   sends      a Synth on its own pair still feeds the shared master effects: their returns come out on MAIN only
 *   stereo     render() / one_ish_render_float() have one output: every Synth on it whatever its OUTPUT
 *   state      OUTPUT is saved with the sound and kept by LOAD INTO SYNTH (a Synth's setup, like its channel)
 *   bench      (offline_bench) one_ish_render_multi against one_ish_render_float */

void one_ish_render_multi(void *inst, float *out_lr, float *const *syn_lr, int frames);   /* src/synths.h */

enum { MO_N = 172 * 128 };   /* about 0.5 s, whole blocks */
static float mo_main[2 * MO_N], mo_ref[2 * MO_N], mo_pair[3][2 * MO_N];

/* three Synths on, a chord held: Synth 1 at 8', Synth 2 at 4', Synth 3 at 16'; quiet enough for the limiter to stay
 * linear; the master effects on or off */
static void *mo_patch(int fx_on) {
    void *e = E_REAL->create(NULL);
    set(e, "mfx_on", fx_on);
    set(e, "volume", 18);
    set(e, "s2_syn_on", 1);
    set(e, "s2_o1_oct", 3);
    set(e, "s2_volume", 18);
    set(e, "s3_syn_on", 1);
    set(e, "s3_o1_oct", 1);
    set(e, "s3_volume", 18);
    if (fx_on) { set(e, "mfx_send1", 40); set(e, "s2_mfx_send1", 40); set(e, "s3_mfx_send2", 40); }
    for (int k = 0; k < 3; k++) note(e, 1, 48 + 4 * k, 100);
    return e;
}
/* MO_N frames: MAIN into main, the pairs into mo_pair (multi), or one_ish_render_float() into main (pairs NULL) */
static void mo_render(void *e, float *main, int multi) {
    for (int at = 0; at < MO_N; at += 128) {
        float *p[3] = {mo_pair[0] + 2 * at, mo_pair[1] + 2 * at, mo_pair[2] + 2 * at};
        if (multi) one_ish_render_multi(e, main + 2 * at, p, 128);
        else one_ish_render_float(e, main + 2 * at, 128);
    }
}
static double mo_peak(const float *x, int n) {
    double p = 0;
    for (int i = 0; i < n; i++) p = fmax(p, fabs(x[i]));
    return p;
}

static void multiout_checks(void) {
    char what[300];
    for (int fx_on = 0; fx_on < 2; fx_on++) {   /* the default routing: MAIN exactly as before, the pairs silent */
        void *a = mo_patch(fx_on), *b = mo_patch(fx_on);
        mo_render(a, mo_ref, 0);
        memset(mo_pair, 0x7f, sizeof mo_pair);   /* (anything: the render must clear them) */
        mo_render(b, mo_main, 1);
        int same = !memcmp(mo_ref, mo_main, sizeof mo_ref);
        double pp = fmax(mo_peak(mo_pair[0], 2 * MO_N), fmax(mo_peak(mo_pair[1], 2 * MO_N), mo_peak(mo_pair[2], 2 * MO_N)));
        snprintf(what, sizeof what, "multi-out, every OUTPUT at Main (master effects %s): MAIN %s one_ish_render_float()'s "
                 "(peak %.3f), the three pairs silent (peak %.2g)", fx_on ? "on" : "off", same ? "exactly" : "NOT",
                 mo_peak(mo_ref, 2 * MO_N), pp);
        check(same && pp == 0.0 && mo_peak(mo_ref, 2 * MO_N) > 0.05, what);
        E->destroy(a);
        E->destroy(b);
    }
    {   /* Synth 2 on its own pair (master effects off): MAIN + the pair = the default MAIN */
        void *a = mo_patch(0), *b = mo_patch(0);
        set(b, "s2_syn_out", 1);
        mo_render(a, mo_ref, 0);
        mo_render(b, mo_main, 1);
        double err = 0;
        for (int i = 0; i < 2 * MO_N; i++) err = fmax(err, fabs(mo_main[i] + mo_pair[1][i] - mo_ref[i]));
        double p1 = mo_peak(mo_pair[0], 2 * MO_N), p2 = mo_peak(mo_pair[1], 2 * MO_N), p3 = mo_peak(mo_pair[2], 2 * MO_N);
        snprintf(what, sizeof what, "multi-out, Synth 2 on Own Pair: on its pair (peak %.3f), Synths 1 and 3's pairs silent "
                 "(%.2g, %.2g), MAIN + the pair = the default MAIN within %.2g", p2, p1, p3, err);
        check(p2 > 0.02 && p1 == 0.0 && p3 == 0.0 && err < 1e-6, what);
        E->destroy(a);
        E->destroy(b);
    }
    {   /* Synth 1 alone, on its own pair: MAIN silent; its pair exactly what MAIN was */
        void *a = E_ONE.create(NULL), *b = E_ONE.create(NULL);
        set(a, "volume", 40);
        set(b, "volume", 40);
        set(b, "syn_out", 1);
        for (int k = 0; k < 3; k++) { note(a, 1, 48 + 4 * k, 100); note(b, 1, 48 + 4 * k, 100); }
        set(a, "mfx_on", 0);
        set(b, "mfx_on", 0);
        mo_render(a, mo_ref, 0);
        mo_render(b, mo_main, 1);
        int same = !memcmp(mo_ref, mo_pair[0], sizeof mo_ref);
        snprintf(what, sizeof what, "multi-out, Synth 1 alone on Own Pair: MAIN silent (peak %.2g), its pair %s what MAIN "
                 "was (peak %.3f)", mo_peak(mo_main, 2 * MO_N), same ? "exactly" : "NOT", mo_peak(mo_pair[0], 2 * MO_N));
        check(same && mo_peak(mo_main, 2 * MO_N) == 0.0 && mo_peak(mo_ref, 2 * MO_N) > 0.02, what);
        E->destroy(a);
        E->destroy(b);
    }
    {   /* Main + Own (master effects on): MAIN exactly the default's, Synth 3 on its pair too */
        void *a = mo_patch(1), *b = mo_patch(1);
        set(b, "s3_syn_out", 2);
        mo_render(a, mo_ref, 0);
        mo_render(b, mo_main, 1);
        int same = !memcmp(mo_ref, mo_main, sizeof mo_ref);
        double p3 = mo_peak(mo_pair[2], 2 * MO_N), p12 = fmax(mo_peak(mo_pair[0], 2 * MO_N), mo_peak(mo_pair[1], 2 * MO_N));
        snprintf(what, sizeof what, "multi-out, Synth 3 on Main + Own: MAIN %s the default's, Synth 3 on its pair too "
                 "(peak %.3f; the others' pairs %.2g)", same ? "exactly" : "NOT", p3, p12);
        check(same && p3 > 0.02 && p12 == 0.0, what);
        E->destroy(a);
        E->destroy(b);
    }
    {   /* the sends: Synth 1 alone on its own pair, SEND 1 up: dry on the pair, the delay's echo on MAIN only */
        void *e = E_ONE.create(NULL);
        set(e, "syn_out", 1);
        set(e, "mfx_send1", 80);
        set(e, "amp_r", 0);
        note(e, 1, 60, 100);
        mo_render(e, mo_main, 1);   /* (held: the dry sound and the echo begin) */
        note(e, 0, 60, 0);
        double dry_main = mo_peak(mo_main, 2 * 1000), held_pair = mo_peak(mo_pair[0], 2 * MO_N);
        mo_render(e, mo_main, 1);   /* (released: the echo rings on MAIN, the pair goes quiet) */
        double echo_main = mo_peak(mo_main, 2 * MO_N), pair_after = mo_peak(mo_pair[0] + 2 * 4410, 2 * (MO_N - 4410));
        snprintf(what, sizeof what, "multi-out, Synth 1 on Own Pair with SEND 1 up: dry on its pair (peak %.3f, %.2g once "
                 "let go), MAIN has no dry sound (%.2g in its first 1000 frames) but the shared delay's echo (%.3f)",
                 held_pair, pair_after, dry_main, echo_main);
        check(held_pair > 0.02 && pair_after < 1e-4 && dry_main == 0.0 && echo_main > 0.005, what);
        E->destroy(e);
    }
    {   /* stereo callers: one_ish_render_float() puts every Synth on its one output, whatever OUTPUT says */
        void *a = mo_patch(1), *b = mo_patch(1);
        set(b, "syn_out", 1);
        set(b, "s2_syn_out", 2);
        set(b, "s3_syn_out", 1);
        mo_render(a, mo_ref, 0);
        mo_render(b, mo_main, 0);
        int same = !memcmp(mo_ref, mo_main, sizeof mo_ref);
        int16_t x[256], y[256];
        E->render(a, x, 128);
        E->render(b, y, 128);
        same &= !memcmp(x, y, sizeof x);
        snprintf(what, sizeof what, "multi-out: render() and one_ish_render_float() (the MPC, any stereo caller) %s every "
                 "Synth on their one output whatever its OUTPUT", same ? "keep" : "do NOT keep");
        check(same, what);
        E->destroy(a);
        E->destroy(b);
    }
    {   /* the state: OUTPUT saved per Synth (a default Main not written), restored; LOAD INTO SYNTH keeps it */
        static char st[65536], cmd[65600];
        void *a = mo_patch(0), *b = E_REAL->create(NULL);
        set(a, "s2_syn_out", 1);
        set(a, "s3_syn_out", 2);
        E->get_param(a, "state", st, sizeof st);
        int has_main = strstr(st, ";syn_out=") != NULL;
        E->set_param(b, "state", st);
        double o1 = get(b, "syn_out"), o2 = get(b, "s2_syn_out"), o3 = get(b, "s3_syn_out");
        snprintf(cmd, sizeof cmd, "synthload2\nv=1;dv=1;o1_wave=1");   /* a sound into Synth 2: its OUTPUT stays */
        E->set_param(b, "state", cmd);
        double o2b = get(b, "s2_syn_out");
        snprintf(what, sizeof what, "multi-out: OUTPUT saved and restored per Synth (%g %g %g; Synth 1's Main %s), "
                 "LOAD INTO SYNTH 2 keeps Synth 2's (%g)", o1, o2, o3, has_main ? "written" : "left out", o2b);
        check(o1 == 0 && o2 == 1 && o3 == 2 && !has_main && o2b == 1, what);
        E->destroy(a);
        E->destroy(b);
    }
}

static void multiout_bench(void) {   /* the render with three pairs against the stereo render: three Synths, a chord each */
    static float m[256], p[3][256];
    float *pp[3] = {p[0], p[1], p[2]};
    double us[3];
    for (int k = 0; k < 3; k++) {   /* stereo, multi with all on MAIN, multi with each on its own pair */
        void *e = mo_patch(1);
        if (k == 2) { set(e, "syn_out", 1); set(e, "s2_syn_out", 1); set(e, "s3_syn_out", 1); }
        enum { BL = 4000 };
        for (int b = 0; b < 200; b++) k ? one_ish_render_multi(e, m, pp, 128) : one_ish_render_float(e, m, 128);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BL; b++) k ? one_ish_render_multi(e, m, pp, 128) : one_ish_render_float(e, m, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        us[k] = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) * 1e-3) / BL;
        E->destroy(e);
    }
    printf("bench: 3 Synths x 3 notes, master effects on: stereo render %6.2f, multi-out all on MAIN %6.2f, each on its "
           "own pair %6.2f us per 128-frame block\n", us[0], us[1], us[2]);
}
