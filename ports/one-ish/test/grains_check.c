/* (2026-10-30) GRAINS (src/fx/grains.h), Auto's CTRL 1 / CTRL 2 targets (src/fx/common.h FX_AUTO_CTRL) and the X/Y
 * pad's LINK (src/synths.h midi): included by offline_check.c (its helpers: fx_none, fx_run, patch, set, note ...).
 *   grains_checks()  the effect alone and in the engine: bounded and finite at its extremes, FREEZE holds, mix 0 is the
 *                    input exactly, PITCH shifts, the master slot type, a state round trip, the type changed mid-note
 *                    without a click; every type's Auto CTRL 1 / CTRL 2 audible and never MIX; old states keep
 *                    sounding as they did; the pad LINK
 *   grains_bench()   its CPU per 128-frame block (typical, and full density: every grain of the pool playing) */
void one_ish_set_input(void *inst, const float *in_lr, int frames);   /* src/synths.h */

static fx_params_t gr_params(void) {   /* GRAINS as the synth effect, its params' defaults (params.json) */
    fx_params_t p = fx_none();
    p.sfx_on = 1; p.sfx.type = SFX_GRAINS; p.sfx.mix = 1;
    p.sfx.position = 0.5f; p.sfx.size = 0.5f; p.sfx.density = 0.7f; p.sfx.texture = 0.5f; p.sfx.spread = 0.5f;
    p.sfx.spray = 0.15f; p.sfx.prand = 0.1f; p.sfx.reverse = 0; p.sfx.gquant = GQ_FREE; p.sfx.fb = 0;
    return p;
}

/* the effect over `in` in blocks, from a fresh state; freeze_at (samples, -1: never) switches FREEZE on there */
static void gr_run(fx_params_t p, const float *in, float *l, float *r, int n, int freeze_at) {
    memset(&fxs, 0, sizeof fxs);
    for (int at = 0; at < n; at += 128) {
        int m = n - at < 128 ? n - at : 128;
        p.sfx.freeze = freeze_at >= 0 && at >= freeze_at;
        fx_process(&fxs, &p, NULL, in + at, in + at, l + at, r + at, m);
    }
}

static double gr_rms(const float *x, int from, int to) {
    double e = 0;
    for (int i = from; i < to; i++) e += (double)x[i] * x[i];
    return sqrt(e / (to - from));
}

static void grains_effect(void) {
    enum { LEN = SR * 6 };
    static float in[LEN], l[LEN], r[LEN];
    char what[300];
    for (int i = 0; i < LEN; i++) in[i] = 0.5f * (2.0f * (float)fmod(i * 110.0 / SR, 1.0) - 1.0f);

    /* extremes: every knob at its ends (and FREEZE switching), finite and bounded, and never stuck in denormals */
    float worst = 0;
    int bad = 0, runs = 0, tiny = 0;
    for (int k = 0; k < 64; k++) {
        fx_params_t p = gr_params();
        p.sfx.size = k & 1; p.sfx.density = k >> 1 & 1; p.sfx.position = k >> 2 & 1; p.sfx.pitch_st = k & 8 ? 48 : -48;
        p.sfx.prand = k >> 4 & 1; p.sfx.reverse = (k >> 5 & 1) ? 1.0f : 0.5f; p.sfx.spray = (k >> 3) & 1;
        p.sfx.texture = (k % 3) * 0.5f; p.sfx.fb = k & 2 ? 0.95f : -0.95f; p.sfx.gquant = k % 4; p.sfx.spread = 1;
        gr_run(p, in, l, r, SR * 3, k & 4 ? SR : -1);
        for (int i = 0; i < SR * 3; i++) {
            if (!isfinite(l[i]) || !isfinite(r[i])) bad++;
            worst = fmaxf(worst, fmaxf(fabsf(l[i]), fabsf(r[i])));
            if (l[i] != 0.0f && fabsf(l[i]) < 1e-30f) tiny++;
        }
        runs++;
    }
    snprintf(what, sizeof what, "grains: %d settings at the extremes (size, density, position, pitch +-48, random pitch, "
             "reverse, spray, envelope, feedback +-95%%, quantise, freeze) stay finite (%d not) and bounded (largest %.2f), "
             "no denormals (%d)", runs, bad, worst, tiny);
    check(!bad && worst < 8.0f && !tiny, what);

    /* freeze holds: a second of saw, then silence; frozen from 1 s the grains play on, unfrozen they die away */
    static float burst[LEN];
    for (int i = 0; i < LEN; i++) burst[i] = i < SR ? in[i] : 0.0f;
    fx_params_t p = gr_params();
    p.sfx.position = 0.3f;   /* (0.14 s back: inside the second recorded) */
    gr_run(p, burst, l, r, LEN, SR);
    double f2 = gr_rms(l, 2 * SR, 2 * SR + SR / 2), f5 = gr_rms(l, 5 * SR, 5 * SR + SR / 2);
    gr_run(p, burst, l, r, LEN, -1);
    double u5 = gr_rms(l, 5 * SR, 5 * SR + SR / 2);
    snprintf(what, sizeof what, "grains freeze: after the input stops, frozen %.1f dB at 2 s and %.1f dB at 5 s (holds "
             "within 3 dB); not frozen %.1f dB at 5 s", 20 * log10(f2 + 1e-12), 20 * log10(f5 + 1e-12),
             20 * log10(u5 + 1e-12));
    check(f2 > 0.02 && fabs(20 * log10(f2 / f5)) < 3 && u5 < f5 * 0.01, what);

    /* mix 0, or off: the input exactly */
    int exact = 1;
    for (int k = 0; k < 2; k++) {
        p = gr_params();
        if (k) p.sfx_on = 0; else p.sfx.mix = 0;
        gr_run(p, in, l, r, SR, -1);
        for (int i = 0; i < SR; i++) exact &= l[i] == in[i] && r[i] == in[i];
    }
    check(exact, "grains at mix 0 (or off): the output is the input exactly");

    /* PITCH +12 (quantised to octaves, no random pitch): a 220 Hz sine comes back at 440 Hz */
    static float sine[LEN];
    for (int i = 0; i < LEN; i++) sine[i] = 0.5f * (float)sin(2 * M_PI * 220.0 * i / SR);
    p = gr_params(); p.sfx.pitch_st = 12; p.sfx.prand = 0; p.sfx.spray = 0; p.sfx.position = 0.2f;
    p.sfx.gquant = GQ_OCT;
    gr_run(p, sine, l, r, SR * 2, -1);
    double hz = peak_hz(l + SR / 2);
    snprintf(what, sizeof what, "grains pitch +12: a 220 Hz sine comes out at %.1f Hz (want 440)", hz);
    check(fabs(hz - 440) < 6, what);
    /* reverse 100% plays the same material backwards: as loud, a different waveform */
    p.sfx.pitch_st = 0;
    gr_run(p, in, l, r, SR * 2, -1);
    double fw = gr_rms(l, SR, 2 * SR);
    static float fwd[SR * 2];
    memcpy(fwd, l, sizeof fwd);
    p.sfx.reverse = 1;
    gr_run(p, in, l, r, SR * 2, -1);
    double rv = gr_rms(l, SR, 2 * SR), d = 0;
    for (int i = SR; i < 2 * SR; i++) d += (double)(l[i] - fwd[i]) * (l[i] - fwd[i]);
    d = sqrt(d / SR);
    snprintf(what, sizeof what, "grains reverse 100%%: %.1f dB against %.1f dB forwards, the waveform %.0f%% different",
             20 * log10(rv), 20 * log10(fw), 100 * d / fw);
    check(fabs(20 * log10(rv / fw)) < 3 && d > 0.3 * fw, what);

    /* the master slot: GRAINS on send 1 sounds, finite */
    p = fx_none(); p.send1 = 1; set_mfx(&p.mfx[0], MFX_GRAINS);
    p.mfx[0].position = 0.3f; p.mfx[0].size = 0.5f; p.mfx[0].density = 0.7f; p.mfx[0].texture = 0.5f;
    p.mfx[0].pitch_st = 12; p.mfx[0].spread = 0.5f; p.mfx[0].mix = 1;
    memset(&fxs, 0, sizeof fxs);
    for (int at = 0; at < SR * 2; at += 128) fx_process(&fxs, &p, NULL, in + at, in + at, l + at, r + at, 128);
    double ms = 0;
    bad = 0;
    for (int i = SR; i < 2 * SR; i++) { ms += (double)(l[i] - in[i]) * (l[i] - in[i]); bad += !isfinite(l[i]); }
    snprintf(what, sizeof what, "grains on master slot 1 (send 1): its return %.1f dB, finite (%d not)",
             10 * log10(ms / SR + 1e-30), bad);
    check(ms / SR > 1e-4 && !bad, what);
}

/* the engine with a held chord through the synth effect: n frames of the left channel; feed: an audio input (for the
 * vocoders' modulator) */
static void gr_play(void *e, float *out, int frames, int feed) {
    static float lr[256];
    uint32_t rng = 777;
    int16_t buf[256];
    for (int at = 0; at < frames; at += 128) {
        if (feed) {
            for (int i = 0; i < 128; i++) lr[2 * i] = lr[2 * i + 1] = 0.3f * fx_white(&rng) + 0.3f * (float)sin(at + i);
            one_ish_set_input(e, lr, 128);
        }
        E->render(e, buf, 128);
        for (int i = 0; i < 128 && at + i < frames; i++) out[at + i] = buf[2 * i] / 32768.0f;
    }
}

static void *gr_synth(int type, int c, double ctrl) {   /* a held saw chord through synth effect `type` */
    void *e = patch(SAW, 0, 1, 100, 10);
    char k[16];
    set(e, "modfx_on", 1); set(e, "modfx_type", type); set(e, "modfx_mix", 100); set(e, "volume", 70);
    snprintf(k, sizeof k, "modfx_ctrl%d", c + 1);
    set(e, k, ctrl);
    note(e, 1, 48, 100); note(e, 1, 55, 100);
    return e;
}

static void grains_engine(void) {
    enum { LEN = SR * 3 };
    static float a[LEN], b[LEN];
    char what[400];
    static char st[262144];

    /* every type's Auto CTRL 1 and CTRL 2: never MIX, and the knob's two ends sound clearly different */
    int mixes = 0, quiet = 0;
    double least = 1e9;
    char least_at[64] = "";
    for (int t = 0; t < SFX_NTYPES; t++)
        for (int c = 0; c < 2; c++) {
            mixes += FX_AUTO_CTRL[t][c] == FXD_MIX;
            int feed = SFX_IS_VOCODER(t);
            /* the knob's centre against each end: the larger difference (a ping-pong's feedback at -100% and +100%
             * sounds the same on one side, both differ from 0) */
            double rel = 0;
            void *mid = gr_synth(t, c, 0);
            gr_play(mid, a, LEN, feed);
            E->destroy(mid);
            for (int end = -100; end <= 100; end += 200) {
                void *x = gr_synth(t, c, end);
                gr_play(x, b, LEN, feed);
                E->destroy(x);
                double dd = 0, ref = 0;
                for (int i = SR; i < LEN; i++) { dd += (double)(a[i] - b[i]) * (a[i] - b[i]); ref += (double)a[i] * a[i] + (double)b[i] * b[i]; }
                rel = fmax(rel, sqrt(dd / (0.5 * ref + 1e-20)));
            }
            if (rel < least) { least = rel; snprintf(least_at, sizeof least_at, "%s CTRL %d (%s)", SFX_NAMES[t], c + 1, FXD_NAME[FX_AUTO_CTRL[t][c]]); }
            if (rel < 0.05) { quiet++; printf("     (quiet: %s CTRL %d %s: %.3f)\n", SFX_NAMES[t], c + 1, FXD_NAME[FX_AUTO_CTRL[t][c]], rel); }
        }
    snprintf(what, sizeof what, "Auto CTRL 1 / CTRL 2 on all %d synth effect types: never MIX (%d are), each knob's -100%% "
             "and +100%% sound different (%d by under 5%%; the least %.0f%%: %s)", SFX_NTYPES, mixes, quiet,
             100 * least, least_at);
    check(!mixes && !quiet, what);

    /* old states: a CTRL on Auto that did something gets Auto's old target written in; new states say Auto */
    void *e = E->create(NULL);
    char v[64];
    E->set_param(e, "state", "v=1;modfx_on=1;modfx_type=0;modfx_ctrl1=40");   /* (before 2026-10-30: Delay, CTRL 1 Mix) */
    E->get_param(e, "modfx_c1_dst", v, sizeof v);
    int old1 = atoi(v);
    E->get_param(e, "modfx_c2_dst", v, sizeof v);
    int old2 = atoi(v);   /* (CTRL 2 at 0 and unmodulated: left on Auto) */
    snprintf(st, sizeof st, "v=1;modfx_on=1;modfx_type=%d;mod1_src=2;mod1_dst=%d;mod1_amt=50", SFX_PHASER6,
             MD_FX_FIRST + MDF_CTRL1);
    E->set_param(e, "state", st);   /* a matrix slot moving CTRL 1 of an old phaser: its old target, Mix */
    E->get_param(e, "modfx_c1_dst", v, sizeof v);
    int mat2 = atoi(v);
    E->set_param(e, "state", "v=1;ctl=2;modfx_on=1;modfx_type=0;modfx_ctrl1=40");   /* (a new state) */
    E->get_param(e, "modfx_c1_dst", v, sizeof v);
    int new1 = atoi(v);
    int n = E->get_param(e, "state", st, sizeof st);
    snprintf(what, sizeof what, "old states: Delay's CTRL 1 at 40%% on Auto becomes %s (%d), CTRL 2 untouched stays Auto "
             "(%d), an old phaser's CTRL 1 under the matrix becomes %s (%d); a new state (\"ctl=2\") keeps Auto (%d), "
             "and states say so (%.12s)", FXD_NAME[old1], old1, old2, FXD_NAME[mat2], mat2, new1, n > 0 ? st : "");
    check(old1 == FXD_MIX && old2 == FXD_AUTO && mat2 == FXD_MIX && new1 == FXD_AUTO && !strncmp(st, "v=1;ctl=2;", 10), what);
    E->destroy(e);

    /* a state round trip with GRAINS everywhere: every value back */
    static const struct { const char *k; double v; } GV[] = {
        {"modfx_on", 1}, {"modfx_type", SFX_GRAINS}, {"modfx_spray", 37}, {"modfx_prand", 21}, {"modfx_reverse", 64},
        {"modfx_gquant", GQ_OCT5}, {"modfx_freeze", 1}, {"modfx_pos", 12}, {"modfx_size", 81}, {"modfx_density", 33},
        {"modfx_c1_dst", FXD_SPRAY}, {"modfx_ctrl1", 25}, {"dly_type", MFX_GRAINS}, {"dly_gpos", 70}, {"dly_gdens", 15},
        {"dly_genv", 90}, {"dly_gspray", 44}, {"dly_gprand", 55}, {"dly_greverse", 12}, {"dly_gquant", GQ_SEMI},
        {"rev_type", MFX_GRAINS}, {"rev_gquant", GQ_OCT}, {"rev_greverse", 99}, {"link_pad", 1}, {"s2_modfx_spray", 3},
        {"s3_modfx_gquant", GQ_OCT}};
    enum { NGV = sizeof GV / sizeof GV[0] };
    void *x = E->create(NULL), *y = E->create(NULL);
    for (int k = 0; k < NGV; k++) set(x, GV[k].k, GV[k].v);
    n = E->get_param(x, "state", st, sizeof st);
    E->set_param(y, "state", st);
    int same = 0;
    for (int k = 0; k < NGV; k++) {
        E->get_param(y, GV[k].k, v, sizeof v);
        same += fabs(atof(v) - GV[k].v) < 1e-3;
    }
    snprintf(what, sizeof what, "grains state round trip: %d of %d values back (%d bytes)", same, NGV, n);
    check(same == NGV, what);
    E->destroy(x);
    E->destroy(y);

    /* the type changed mid-note (chorus -> grains -> delay -> grains): no click (no sample-to-sample jump beyond what
     * the steady sound has) */
    e = gr_synth(SFX_CHORUS, 0, 0);
    set(e, "modfx_mix", 50);
    static float o[SR * 8];
    int at = 0;
    gr_play(e, o, SR, 0); at += SR;
    float steady = 0;
    for (int i = SR / 2; i < SR; i++) steady = fmaxf(steady, fabsf(o[i] - o[i - 1]));
    static const int TYPES[] = {SFX_GRAINS, SFX_DELAY, SFX_GRAINS, SFX_CLOUDS, SFX_GRAINS};
    float jump = 0;
    for (int k = 0; k < 5; k++) {
        set(e, "modfx_type", TYPES[k]);
        gr_play(e, o + at, SR, 0);
        for (int i = at; i < at + SR / 4; i++) jump = fmaxf(jump, fabsf(o[i] - o[i - 1]));
        at += SR;
    }
    snprintf(what, sizeof what, "type changed mid-note (chorus > grains > delay > grains > granular > grains): the "
             "largest step %.4f against %.4f in the steady sound (no click)", jump, steady);
    check(jump < 1.5f * steady + 0.002f, what);
    E->destroy(e);

    /* the X/Y pad's LINK: with Per Synth channels, the pad on channel 1 reaches only Synth 1; with LINK, all three */
    e = E->create(NULL);
    set(e, "midi_ch", 17); set(e, "s2_syn_on", 1); set(e, "s3_syn_on", 1);
    char p1[64], p2[64], p3[64], b2[64], b3[64];
    int ok[2];
    for (int link = 0; link < 2; link++) {
        set(e, "link_pad", link);
        E->get_param(e, "s2_pad_now", b2, sizeof b2);   /* (before) */
        E->get_param(e, "s3_pad_now", b3, sizeof b3);
        uint8_t m[3][3] = {{0xB0, 2, (uint8_t)(link ? 100 : 64)}, {0xB0, 3, 30}, {0xB0, 18, 127}};
        for (int k = 0; k < 3; k++) E->midi(e, m[k], 3);
        int16_t buf[256];
        E->render(e, buf, 128);
        E->get_param(e, "pad_now", p1, sizeof p1);
        E->get_param(e, "s2_pad_now", p2, sizeof p2);
        E->get_param(e, "s3_pad_now", p3, sizeof p3);
        float x1 = (float)atof(p1), x2 = (float)atof(p2), x3 = (float)atof(p3);
        float want = (link ? 100 : 64) / 127.0f;
        ok[link] = link ? fabsf(x1 - want) < 1e-3f && fabsf(x2 - want) < 1e-3f && fabsf(x3 - want) < 1e-3f
                          && !strcmp(p2 + strlen(p2) - 1, "1") && !strcmp(p3 + strlen(p3) - 1, "1")
                        : fabsf(x1 - want) < 1e-3f && !strcmp(p2, b2) && !strcmp(p3, b3);
        snprintf(what, sizeof what, "X/Y pad LINK %s: CC 2/3/18 on channel 1 (Per Synth channels) reach Synth 1 \"%s\", "
                 "Synth 2 \"%s\", Synth 3 \"%s\"", link ? "on" : "off", p1, p2, p3);
        check(ok[link], what);
    }
    E->destroy(e);
}

static void grains_checks(void) {
    grains_effect();
    grains_engine();
}

static void grains_bench(void) {
    double m, w;
    fx_params_t p = gr_params();
    oss_time(&p, &m, &w);
    printf("bench: Grains (defaults: 183 ms grains, 74 a second) %6.2f us per 128-frame block (99%%: %.1f)\n", m, w);
    p.sfx.density = 1; p.sfx.size = 1; p.sfx.prand = 1; p.sfx.reverse = 0.5f; p.sfx.pitch_st = 7; p.sfx.spray = 1;
    oss_time(&p, &m, &w);
    printf("bench: Grains (full density: all %d grains playing) %6.2f us per 128-frame block (99%%: %.1f)\n", GR_VOICES,
           m, w);
}
