/* What the screen shows for a param: its value in words and units. Part of engine.c (included there, one unit with it). */
static int hz_text(char *buf, int n, float hz) {
    if (hz < 1.0f) return snprintf(buf, n, "%.2f Hz", hz);
    if (hz < 100.0f) return snprintf(buf, n, "%.1f Hz", hz);
    return hz < 1000.0f ? snprintf(buf, n, "%.0f Hz", hz) : snprintf(buf, n, "%.2f kHz", hz * 0.001f);
}

static int time_text(char *buf, int n, float ms) {
    if (ms < 1.0f) return snprintf(buf, n, "%.2f ms", ms);
    if (ms < 10.0f) return snprintf(buf, n, "%.1f ms", ms);
    if (ms < 1000.0f) return snprintf(buf, n, "%.0f ms", ms);
    return snprintf(buf, n, "%.2f s", ms * 0.001f);
}

/* a master slot's control (field f of BANK[b], at v) as text, in the terms of the type it runs */
static int fx_bank_text(const one_t *s, int b, int f, float v, char *buf, int n) {
    const short *K = BANK[b];
    int mt = (int)clampf(s->val[K[BK_TYPE]], 0, MFX_NTYPES - 1), st = MFX_SFX[mt], sync = s->val[K[BK_SYNC]] > 0.5f;
    switch (f) {
    case BK_GAIN: {
        float g = fx_gain_x(v);
        return g > 0.0f ? snprintf(buf, n, "%+.1f dB", 20.0f * log10f(g)) : snprintf(buf, n, "-inf dB");
    }
    case BK_TIME: case BK_TIME_R:   /* the delay types' time, or the chorus or flanger time */
        if (st < 0 || st <= SFX_ECHO || st == SFX_TPHASER || st == SFX_SDELAY || st == SFX_PINGPONG) {
            if (sync) return snprintf(buf, n, "%s", FX_DIV[fx_div(v)].name);
            return time_text(buf, n, fx_delay_ms(v));
        }
        return time_text(buf, n, fx_mod_time_ms(st, v));
    case BK_RATE: case BK_RATE_R: case BK_VRATE:
        if (sync) return snprintf(buf, n, "%s", fx_div_rate_name(v));
        return hz_text(buf, n, f == BK_VRATE ? fx_vib_hz(v) : fx_rate_hz(v));
    case BK_DECAY: {   /* the decay time at this size */
        float sz = fx_rev_size(s->val[K[BK_SIZE]]);
        if (mt == MFX_PLATE || mt == MFX_ROOM) return snprintf(buf, n, "%.2f s", fx_rev_rt60(v) * sz);
        if (mt == MFX_HALL || mt == MFX_SHIMMER) return snprintf(buf, n, "%.2f s", fx_hall_rt60(v) * sz);
        return snprintf(buf, n, "%.0f%%", v);
    }
    case BK_SIZE: return snprintf(buf, n, "%.0f%%", fx_rev_size(v) * 100.0f);
    case BK_HIFREQ: return hz_text(buf, n, fx_hifreq_hz(v));
    case BK_XOVER: return v > 0.0f ? hz_text(buf, n, fx_xover_hz(v)) : snprintf(buf, n, "Off");
    case BK_SRATE: return hz_text(buf, n, fx_srate_hz(v));
    case BK_CUT1: case BK_CUT2: case BK_CUT3: return hz_text(buf, n, fx_cut_hz(v));
    default: return 0;
    }
}

/* "<key>_display": the text MPC shows for a knob whose 0..100 position is a log scale or a tempo division */
static int display_text(const one_t *s, int k, char *buf, int n) {
    if (k >= P_PH_BALLS && k <= P_PH_DYN) return ph_display(s, k, buf, n);
    if (k >= P_RAIN_ON && k <= P_MKV_MAX) return gb_display(s, k, buf, n);   /* (2026-10-30) */
    if (k >= P_LIFE_ON && k <= P_PEND_DYN) return ga_display(s, k, buf, n);
    if (k == P_FLT_CUT || k == P_SVF_CUT) {
        float hz = cut_hz(s->val[k]);
        return hz < 1000.0f ? snprintf(buf, n, "%.0f Hz", hz) : snprintf(buf, n, "%.2f kHz", hz * 0.001f);
    }
    if (k >= P_ENV_A && k < P_ENV_A + NENV * ENV_STRIDE && (k - P_ENV_A) % ENV_STRIDE != 2) {   /* A, D, R: not S */
        if (s->val[P_FENV_SYNC + EX_STRIDE * ((k - P_ENV_A) / ENV_STRIDE)] > 0.5f)
            return snprintf(buf, n, "%s", env_sync_name(s, s->val[k]));
        if (s->val[k] <= 0.0f && eg13(s)) return snprintf(buf, n, "MIN");
        return time_text(buf, n, env_ms(s->val[k]));
    }
    if (k >= P_FENV_DELAY && k < P_FENV_DELAY + NENV * EX_STRIDE) {   /* delay, hold, time scale */
        int x = (k - P_FENV_DELAY) % EX_STRIDE, e = (k - P_FENV_DELAY) / EX_STRIDE;
        if (x == X_SCALE) return snprintf(buf, n, "%.0f%%", env_scale(s->val[k]) * 100.0f);
        if (x != X_DELAY && x != X_HOLD) return 0;
        if (s->val[k] <= 0.0f) return snprintf(buf, n, "0 ms");
        if (s->val[P_FENV_SYNC + EX_STRIDE * e] > 0.5f) return snprintf(buf, n, "%s", env_sync_name(s, s->val[k]));
        return time_text(buf, n, env_ms(s->val[k]));
    }
    if (k == P_KEY_CENTER || k == P_KEY_LO || k == P_KEY_HI) {   /* a note name, middle C (60) as C4, and the MIDI note number */
        static const char *const NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int note = k == P_KEY_CENTER ? (int)s->center : (int)roundf(s->val[k]);
        return snprintf(buf, n, "%s%d (%d)", NAMES[note % 12], note / 12 - 1, note);
    }
    if (k == P_NOISE_A || k == P_NOISE_R) return time_text(buf, n, env_ms(s->val[k]));
    if (k == P_GLIDE_TIME) return s->val[k] > 0.0f ? time_text(buf, n, glide_ms(s->val[k])) : snprintf(buf, n, "0 ms");
    if (k == P_MODFX_RATE || k == P_MODFX_RATE_R || k == P_MODFX_VRATE) {
        if (s->val[P_MODFX_SYNC] > 0.5f) return snprintf(buf, n, "%s", fx_div_rate_name(s->val[k]));
        return hz_text(buf, n, k == P_MODFX_VRATE ? fx_vib_hz(s->val[k]) : fx_rate_hz(s->val[k]));
    }
    if (k == P_MODFX_TIME || k == P_MODFX_TIME_R) return time_text(buf, n, fx_mod_time_ms((int)s->val[P_MODFX_TYPE], s->val[k]));
    if (k == P_MODFX_DTIME || k == P_MODFX_DTIME_R) {
        if (s->val[P_MODFX_SYNC] > 0.5f) return snprintf(buf, n, "%s", FX_DIV[fx_div(s->val[k])].name);
        return time_text(buf, n, fx_delay_ms(s->val[k]));
    }
    if (k == P_MODFX_XOVER) return s->val[k] > 0.0f ? hz_text(buf, n, fx_xover_hz(s->val[k])) : snprintf(buf, n, "Off");
    if (s->fx_bank[k] >= 0) return fx_bank_text(s, s->fx_bank[k], s->fx_bankf[k], s->val[k], buf, n);
    if (k == P_MODFX_GAIN) {
        float g = fx_gain_x(s->val[k]);
        return g > 0.0f ? snprintf(buf, n, "%+.1f dB", 20.0f * log10f(g)) : snprintf(buf, n, "-inf dB");
    }
    if (k == P_MODFX_SRATE) return hz_text(buf, n, fx_srate_hz(s->val[k]));
    if (k >= P_MODFX_CUT1 && k <= P_MODFX_CUT3) return hz_text(buf, n, fx_cut_hz(s->val[k]));
    if (k == P_MODFX_CTRL1 || k == P_MODFX_CTRL2)   /* the parameter it moves, and by how much */
        return snprintf(buf, n, "%s %+.0f%%", FXD_NAME[fx_ctrl_dst(s, k - P_MODFX_CTRL1)], s->val[k]);
    if (k == P_MODFX_DRIVE) {   /* in each Airwindows type's own terms */
        float d = s->val[k] * 0.01f;
        switch ((int)s->val[P_MODFX_TYPE]) {
        case SFX_AWTAPE: return snprintf(buf, n, "%+.1f dB", (d - 0.5f) * 24.0f);   /* input gain */
        case SFX_DENSITY: return snprintf(buf, n, "%.2f", d * 5.0f - 1.0f);        /* density, -1 .. 4 */
        default: return snprintf(buf, n, "%.0f%%", s->val[k]);
        }
    }
    for (int l = 0; l < NLFO; l++) {
        if (k == lfo_base(l)) {   /* the rate */
            if (s->val[k + 2] > 0.5f)
                return snprintf(buf, n, "%s%s", LFO_DIV[lfo_div(s->val[k])].name,
                                LFO_TIMING_TAG[(int)clampf(lfo_x(s, l)[L_TIMING], 0, 2)]);
            float hz = lfo_hz(s, l);
            return snprintf(buf, n, hz < 0.1f ? "%.3f Hz" : hz < 1.0f ? "%.2f Hz" : hz < 100.0f ? "%.1f Hz" : "%.0f Hz", hz);
        }
        int x = k - (P_LFO1_RANGE + LX_STRIDE * l);
        if (x == L_DELAY || x == L_FADE_IN || x == L_FADE_OUT)
            return s->val[k] > 0.0f ? time_text(buf, n, env_ms(s->val[k])) : snprintf(buf, n, "Off");
    }
    return 0;
}
