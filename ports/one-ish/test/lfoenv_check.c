/* ONE-ish LFO and envelope fidelity (audit package 4, 2026-10-29): LFO RANGE High (1 Hz - 1 kHz, run per sample on
 * pitch, the cutoffs and the amp level), the PULSE wave's variation over 0..100%, synced LFOs restarting at their start
 * phase when the host's transport starts, the envelopes' MIN below 1 ms (left alone by TIME SCALE) and the 13 synced
 * values (EG TIMES "MIN, 13 Syncs"); the High range's cost in the bench (lfoenv_bench). Included by offline_check.c
 * after modx_check.c (its helpers too). */

static void *le_patch(shape_t wave) {   /* one oscillator, open filter, no default vibrato slot */
    void *e = patch(wave, 1, 100, 100, 5);
    set(e, "mod1_amt", 0);
    set(e, "flt_cut", 100);
    return e;
}
static double le_knob_hz(double hz, double lowest) { return 100 * log10(hz / lowest) / 3; }   /* a rate knob */
/* a held key's spectrum: render a settling time, then N samples */
static float *le_hold(void *e, int key) {
    static float x[N];
    static float pre[8192];
    note(e, 1, key, 100);
    render_f(e, pre, 8192);
    render_f(e, x, N);
    note(e, 0, key, 0);
    render_f(e, pre, 4096);
    return x;
}
/* the fraction of the time the tracked pitch is above (dir 1) or below (-1) the key by more than 3 semitones */
static double le_side(void *e, int key, int ms, int dir) {
    static float x[SR * 2];
    static double t[MAXPTS], st[MAXPTS];
    int n = ms * SR / 1000;
    note(e, 1, key, 100);
    render_f(e, x, n);
    note(e, 0, key, 0);
    int k = track(x, n, t, st);
    double in = 0, all = 0;   /* by time (a higher pitch has more cycles, so more points) */
    for (int i = 1; i < k; i++) {
        double dt = t[i] - t[i - 1];
        all += dt;
        if (dir > 0 ? st[i] > key + 3 : st[i] < key - 3) in += dt;
    }
    return all > 0 ? in / all : -1;
}
/* an attack's length: the first sample within 50 ms where the level (the largest |x| over the last 11 samples, a cycle
 * of the C8 played) reaches 90% of the loudest there */
static int le_rise(void *e) {
    static float x[2205];
    note(e, 1, 108, 127);
    render_f(e, x, 2205);
    note(e, 0, 108, 0);
    float mx = 0;
    for (int i = 0; i < 2205; i++) mx = fmaxf(mx, fabsf(x[i]));
    for (int i = 0; i < 2205; i++) {
        float lv = 0;
        for (int j = i - 10 < 0 ? 0 : i - 10; j <= i; j++) lv = fmaxf(lv, fabsf(x[j]));
        if (lv > 0.9f * mx) return i;
    }
    return -1;
}
static void le_text(void *e, const char *key, char *out, int n) {
    char k[48];
    snprintf(k, sizeof k, "%s_display", key);
    out[0] = 0;
    E->get_param(e, k, out, n);
}

static void lfo_env_fidelity(void) {
    char what[320];
    /* ---- LFO RANGE High: 1 Hz - 1 kHz, per sample. LFO 1 at 200 Hz on pitch: FM sidebands at +-200 Hz around a
     * 1047 Hz triangle and none of the control rate's images (2756 Hz - 200 Hz away: 1510 Hz) ---- */
    {
        double side[3], image[3], carrier[3];
        char rate[32] = "";
        for (int k = 0; k < 3; k++) {   /* 0: High on pitch, 1: High on the amp level (matrix), 2: no LFO */
            void *e = le_patch(TRI);
            set(e, "lfo1_range", 3);
            set(e, "lfo1_wave", 1);   /* sine */
            set(e, "lfo1_rate", le_knob_hz(200, 1));
            if (k == 0) set(e, "pitch_lfo", 2.75);   /* about +-20 Hz: an FM index of 0.1 */
            if (k == 1) mx_slot(e, 2, MS_LFO1, MD_AMP, 50);
            if (k == 0) le_text(e, "lfo1_rate", rate, sizeof rate);
            const float *x = le_hold(e, 84);
            carrier[k] = band_db(x, 1046.5);
            side[k] = 0.5 * (band_db(x, 846.5) + band_db(x, 1246.5)) - carrier[k];
            image[k] = band_db(x, 1510.0) - carrier[k];
            E->destroy(e);
        }
        snprintf(what, sizeof what, "LFO RANGE High (%s): on pitch, sidebands at +-200 Hz %.1f dB (FM index 0.1: about "
                 "-26), the control rate's image %.1f dB; without the LFO %.1f / %.1f dB", rate, side[0], image[0], side[2],
                 image[2]);
        check(!strcmp(rate, "200 Hz") && side[0] > -30 && side[0] < -22 && image[0] < -70 && side[2] < -60, what);
        snprintf(what, sizeof what, "LFO RANGE High: on Amp Level by the matrix (50%%): sidebands %.1f dB (AM at 50%%: "
                 "-12), the image %.1f dB", side[1], image[1]);
        check(side[1] > -14 && side[1] < -10 && image[1] < -70, what);
    }
    {
        /* LFO 2 High at 300 Hz on the ladder cutoff (hard-wired): sidebands appear, the output stays finite and bounded */
        double side[2], peak = 0;
        int finite = 1;
        for (int k = 0; k < 2; k++) {
            void *e = le_patch(TRI);
            set(e, "flt_cut", 55); set(e, "flt_res", 40);
            set(e, "lfo2_range", 3); set(e, "lfo2_wave", 1); set(e, "lfo2_rate", le_knob_hz(300, 1));
            if (k) set(e, "lad_lfo", 30);
            const float *x = le_hold(e, 84);
            for (int i = 0; i < N; i++) { finite &= isfinite(x[i]); peak = fmax(peak, fabs(x[i])); }
            side[k] = band_db(x, 1346.5) - band_db(x, 1046.5);
            E->destroy(e);
        }
        snprintf(what, sizeof what, "LFO RANGE High on the ladder cutoff (LFO 2, 300 Hz): sidebands %.1f dB (%.1f without), "
                 "finite %d, peak %.2f", side[1], side[0], finite, peak);
        check(side[1] > side[0] + 20 && finite && peak < 1.0, what);
    }
    {
        /* the Low range is untouched: the same samples as before the High range existed (lfo_rate in Low, synced) */
        void *a = le_patch(SAW), *b = le_patch(SAW);
        set(a, "pitch_lfo", 20); set(b, "pitch_lfo", 20);
        set(b, "lfo1_range", 3); set(b, "lfo1_range", 1);   /* to High and back */
        static float xa[8192], xb[8192];
        note(a, 1, 57, 100); note(b, 1, 57, 100);
        render_f(a, xa, 8192); render_f(b, xb, 8192);
        check(!memcmp(xa, xb, sizeof xa), "LFO RANGE: back from High to Low plays the same samples as never in High");
        E->destroy(a); E->destroy(b);
    }
    /* ---- PULSE: the width over the manual's whole range, 0% always off .. 100% always on ---- */
    {
        double up[3];
        static const double VAR[3] = {0, 25, 100};
        for (int k = 0; k < 3; k++) {
            void *e = le_patch(SAW);
            set(e, "lfo1_wave", LFO_PULSE);
            set(e, "lfo1_rate", le_knob_hz(2, 0.01));
            set(e, "lfo1_var", VAR[k]);
            set(e, "pitch_lfo", 50);   /* +-6 st */
            up[k] = le_side(e, 57, 2000, 1);
            E->destroy(e);
        }
        snprintf(what, sizeof what, "LFO PULSE: the time high at variation 0%% %.0f%% (always off), 25%% %.0f%%, 100%% "
                 "%.0f%% (always on)", up[0] * 100, up[1] * 100, up[2] * 100);
        check(up[0] < 0.01 && fabs(up[1] - 0.25) < 0.04 && up[2] > 0.99, what);
    }
    /* ---- synced LFOs restart at their start phase when the host's transport starts ---- */
    {
        double high[2];
        for (int k = 0; k < 2; k++) {   /* 0: synced to 1/4 at 120 BPM (a 0.5 s cycle); 1: free at 2 Hz (the same) */
            void *e = le_patch(SAW);
            E->set_param(e, "lfo_bpm", "120");
            set(e, "lfo1_wave", 2);   /* square: high for the first half of its cycle */
            if (!k) { set(e, "lfo1_sync", 1); set(e, "lfo1_rate", 50); }
            else set(e, "lfo1_rate", le_knob_hz(2, 0.01));
            set(e, "pitch_lfo", 50);
            static float x[SR], y[4410];
            static double t[MAXPTS], st[MAXPTS];
            note(e, 1, 57, 100);
            render_f(e, x, 13230);   /* 0.3 s: 60% into the cycle, the low half */
            double ppq = 0;
            int16_t buf[256];
            for (int at = 0; at < 4410; at += 128) {   /* the transport starts: the song position each block */
                int m = 4410 - at < 128 ? 4410 - at : 128;
                E->song_pos(e, ppq, 120, 1, m);
                ppq += m * 120 / 60.0 / SR;
                E->render(e, buf, m);
                for (int i = 0; i < m; i++) y[at + i] = buf[2 * i] / 32768.0f;
            }
            int n = track(y, 4410, t, st), in = 0;
            for (int i = 0; i < n; i++) in += st[i] > 60;
            high[k] = n ? (double)in / n : -1;
            E->destroy(e);
        }
        snprintf(what, sizeof what, "synced LFO and the host's transport start: the first 100 ms after it high %.0f%% of "
                 "the time (restarted at its start phase), a free LFO %.0f%% (carries on in its low half)", high[0] * 100,
                 high[1] * 100);
        check(high[0] > 0.9 && high[1] < 0.1, what);
    }
    /* ---- the envelopes' MIN, and TIME SCALE leaving it alone ---- */
    {
        int r[4];
        char t0[32], t1[32];
        for (int k = 0; k < 4; k++) {   /* classic / MIN, time scale 100% / 1000% */
            void *e = le_patch(SAW);
            set(e, "amp_a", 0);
            set(e, "amp_d", 80);
            route(e, "o1", 0); set(e, "flt_mix", 100);   /* the open ladder alone */
            if (k & 1) set(e, "eg_times", 1);
            if (k & 2) set(e, "amp_scale", 100);
            if (k == 0) le_text(e, "amp_a", t0, sizeof t0);
            if (k == 1) le_text(e, "amp_a", t1, sizeof t1);
            r[k] = le_rise(e);
            E->destroy(e);
        }
        snprintf(what, sizeof what, "EG MIN: attack knob at the left: \"%s\" rises in %d samples (1 ms = 44), \"%s\" in %d; "
                 "at TIME SCALE 1000%% %d and %d (MIN unscaled)", t0, r[0], t1, r[1], r[2], r[3]);
        check(!strcmp(t1, "MIN") && r[0] > 35 && r[0] < 60 && r[1] < 30 && r[1] < r[0] - 12 && r[2] > 300 && r[3] == r[1], what);
    }
    /* ---- the 13 synced values: 1/64 to 64 beats ---- */
    {
        char a[32], b[32], c[32], d[32];
        double t_fall = 0;
        void *e = le_patch(SAW);
        E->set_param(e, "lfo_bpm", "120");
        set(e, "amp_sync", 1);
        set(e, "amp_d", 50);
        le_text(e, "amp_d", d, sizeof d);   /* the classic list's middle */
        set(e, "eg_times", 1);
        le_text(e, "amp_d", b, sizeof b);
        set(e, "amp_d", 100);
        le_text(e, "amp_d", a, sizeof a);
        set(e, "amp_d", 0);
        le_text(e, "amp_d", c, sizeof c);
        set(e, "amp_d", 50); set(e, "amp_s", 0); set(e, "amp_a", 0);
        static float x[SR];
        note(e, 1, 57, 100);
        render_f(e, x, SR);
        int top = 0;
        for (int j = 0; j + 220 < SR / 4; j += 220)
            if (rms_range(x, j, j + 220) > rms_range(x, top, top + 220)) top = j;
        double pk = rms_range(x, top, top + 220);
        int i = top;
        while (i + 220 < SR && rms_range(x, i, i + 220) > 0.01 * pk) i += 220;
        t_fall = (i - top) / 44.1;
        E->destroy(e);
        snprintf(what, sizeof what, "EG 13 synced values: the decay knob right \"%s\" (64 beats), middle \"%s\", left \"%s\" "
                 "(1/64 beat); the classic list's middle \"%s\"; the middle at 120 BPM decays to -40 dB in %.0f ms (a beat: 500)",
                 a, b, c, d, t_fall);
        check(!strcmp(a, "16 bars") && !strcmp(b, "1/4") && !strcmp(c, "1/256") && !strcmp(d, "1/4T") && t_fall > 400
              && t_fall < 520, what);
    }
}

/* the High range's cost: 8 voices of the bench's busy patch with LFO 1 on pitch in Low, then in High (per sample), then
 * LFOs 1 and 2 both High (pitch and both cutoffs) */
static void lfoenv_bench(void) {
    enum { BLOCKS = 30000 };
    int16_t buf[256];
    double us[3];
    for (int c = 0; c < 3; c++) {
        void *e = patch(SAW, 5, 300, 70, 250);
        set(e, "voices", 7); set(e, "o2_level", 80); set(e, "o3_level", 60);
        route(e, "o1", 2); set(e, "flt_route", 1); set(e, "lad_env", 40); set(e, "flt_res", 50);
        set(e, "mod2_src", 6); set(e, "mod2_dst", 9); set(e, "mod2_amt", 30);
        set(e, "pitch_lfo", 3); set(e, "lfo1_wave", 1);
        if (c >= 1) { set(e, "lfo1_range", 3); set(e, "lfo1_rate", le_knob_hz(220, 1)); }
        if (c == 2) { set(e, "lfo2_range", 3); set(e, "lfo2_rate", le_knob_hz(330, 1)); set(e, "lad_lfo", 20); set(e, "svf_lfo", 20); }
        for (int k = 0; k < 8; k++) note(e, 1, 48 + 3 * k, 100);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        us[c] = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
        E->destroy(e);
    }
    printf("bench: 8 voices, LFO 1 on pitch in Low: %6.2f us per 128-frame block (%.2f per voice)\n", us[0], us[0] / 8);
    printf("bench: 8 voices, LFO 1 in High (per sample, on pitch): %6.2f us (%+.2f: %.2f per voice for the High LFO)\n", us[1],
           us[1] - us[0], (us[1] - us[0]) / 8);
    printf("bench: 8 voices, LFOs 1 and 2 in High (pitch, both cutoffs): %6.2f us (%+.2f: %.2f per voice)\n", us[2],
           us[2] - us[0], (us[2] - us[0]) / 8);
}
