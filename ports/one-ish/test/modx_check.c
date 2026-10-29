/* ONE-ish modulation extras (audit package 3, 2026-10-29): the step-mod list's new matrix destinations (oscillator
 * OCTAVE, the noise EG's ATTACK / SUSTAIN / RELEASE, each envelope's MULTI TRIG / SYNC / LOOP / LATCH, each LFO's WAVE),
 * every slot's ENABLE, PAD POLARITY, X/Y HOLD and LOAD SAVED POS, STEP MOD on the new destinations, and sequences
 * whose lanes reach a destination past 255 (the two-byte form). Included by offline_check.c after the sequencer's and
 * the multitimbral checks, so it uses their helpers (check, set, note, patch, render_f, peak_hz, route, midi3, get,
 * seqcmd, seq_text, rms_range ...). */

#define PAD_X_CC_T 2       /* the X/Y pad's CCs (engine.c PAD_X_CC, PAD_Y_CC, PAD_TOUCH_CC) */
#define PAD_Y_CC_T 3
#define PAD_TOUCH_CC_T 18
#define MX_AMP_ENV 1       /* the amp envelope: the second of filter, amp, mod (engine.c ENV_AMP) */

static void mx_slot(void *e, int n, int src, int dst, double amt) {   /* slot n: source, destination, amount, no via */
    char k[24];
    snprintf(k, sizeof k, "mod%d_src", n); set(e, k, src);
    snprintf(k, sizeof k, "mod%d_dst", n); set(e, k, dst);
    snprintf(k, sizeof k, "mod%d_amt", n); set(e, k, amt);
    snprintf(k, sizeof k, "mod%d_via", n); set(e, k, 0);
}
static void *mx_patch(void) {   /* one saw, no vibrato slot, no filter in the way */
    void *e = patch(SAW, 1, 100, 100, 5);
    set(e, "mod1_amt", 0);
    set(e, "flt_cut", 100);
    return e;
}
static double mx_pitch(void *e, int key) {   /* the held key's frequency, after a settling time */
    static float x[N + 8192];
    note(e, 1, key, 100);
    render_f(e, x, 8192);
    render_f(e, x, N);
    note(e, 0, key, 0);
    render_f(e, x + N, 8192);
    return peak_hz(x);
}
/* the RMS of `ms` of output, from now */
static double mx_rms(void *e, int ms) {
    static float x[SR * 4];
    int n = ms * SR / 1000;
    render_f(e, x, n);
    return rms_range(x, 0, n);
}

static void mod_extras(void) {
    char what[256];
    /* ---- oscillator OCTAVE: whole octaves within 32' .. 2' (+-100% = the whole list) ---- */
    {
        void *e = mx_patch();
        double f0 = mx_pitch(e, 57);
        mx_slot(e, 2, MS_CONST, MD_O1_OCT, 25);   /* one step: 8' to 4' */
        double up = mx_pitch(e, 57);
        set(e, "mod2_amt", -50);                  /* two steps down: 32' */
        double dn = mx_pitch(e, 57);
        set(e, "mod2_amt", 100);                  /* past the end: 2' */
        double top = mx_pitch(e, 57);
        set(e, "mod2_amt", 10);                   /* less than half a step: no change */
        double small = mx_pitch(e, 57);
        snprintf(what, sizeof what, "matrix Osc 1 Octave: A3 %.1f Hz; +25%% %.1f (an octave up), -50%% %.1f (two down), "
                 "+100%% %.1f (2', the top), +10%% %.1f (no step)", f0, up, dn, top, small);
        check(fabs(f0 - 220) < 1 && fabs(up / f0 - 2) < 0.01 && fabs(dn / f0 - 0.25) < 0.01 && fabs(top / f0 - 4) < 0.02
              && fabs(small / f0 - 1) < 0.005, what);
        /* ENABLE: the slot off keeps its settings and does nothing; on again it acts */
        set(e, "mod2_amt", 25);
        set(e, "mod2_on", 0);
        double off = mx_pitch(e, 57);
        int kept = get(e, "mod2_amt") == 25 && get(e, "mod2_dst") == MD_O1_OCT && get(e, "mod2_src") == MS_CONST;
        set(e, "mod2_on", 1);
        double back = mx_pitch(e, 57);
        snprintf(what, sizeof what, "matrix slot ENABLE: off %.1f Hz (as without it; settings kept %d), on %.1f Hz", off,
                 kept, back);
        check(fabs(off / f0 - 1) < 0.005 && kept && fabs(back / f0 - 2) < 0.01, what);
        /* the state keeps the switch (and an old state without it has every slot on) */
        set(e, "mod2_on", 0);
        static char st[1 << 16];
        E->get_param(e, "state", st, sizeof st);
        void *g = E->create(NULL);
        E->set_param(g, "state", st);
        int saved = get(g, "mod2_on") == 0 && get(g, "mod3_on") == 1 && strstr(st, "mod2_on=0") != NULL;
        E->set_param(g, "state", "v=1;mod2_src=19;mod2_dst=1;mod2_amt=10");
        int old = get(g, "mod2_on") == 1;
        snprintf(what, sizeof what, "matrix slot ENABLE in the state: saved off and restored (%d); a state without it: on "
                 "(%d)", saved, old);
        check(saved && old, what);
        E->destroy(g);
        E->destroy(e);
    }
    /* ---- LFO WAVE: LFO 1's triangle stepped to a square by +34% (two steps: Triangle, Sine, Square) ---- */
    {
        double mid[2];
        for (int k = 0; k < 2; k++) {
            void *e = mx_patch();
            set(e, "lfo1_rate", 77);
            set(e, "pitch_lfo", 50);   /* +-6 semitones */
            if (k) mx_slot(e, 2, MS_CONST, MD_LFO1_WAVE, 34);
            static float x[SR * 2];
            static double t[MAXPTS], stv[MAXPTS];
            note(e, 1, 57, 100);
            render_f(e, x, SR * 2);
            int n = track(x, SR * 2, t, stv), in = 0;
            for (int i = 0; i < n; i++) in += fabs(stv[i] - 57) < 3.0;
            mid[k] = n ? (double)in / n : 1;
            E->destroy(e);
        }
        snprintf(what, sizeof what, "matrix LFO 1 Wave +34%%: the triangle becomes a square (the pitch within 3 st of the "
                 "note: %.0f%% of the time, %.0f%% with the triangle)", mid[1] * 100, mid[0] * 100);
        check(mid[0] > 0.35 && mid[1] < 0.08, what);
    }
    /* ---- the noise EG's ATTACK, SUSTAIN and RELEASE ---- */
    {
        double early[2], held[2], tail[2];
        for (int k = 0; k < 2; k++) {
            void *e = mx_patch();
            set(e, "o1_level", 0);
            set(e, "noise_level", 100);
            route(e, "noise", 0);
            set(e, "noise_env", 1);
            set(e, "noise_a", 0);     /* 1 ms */
            set(e, "noise_sus", 1);
            set(e, "noise_r", 25);    /* 10 ms */
            set(e, "amp_r", 75);      /* (the voice itself rings on for a second) */
            if (k) {
                mx_slot(e, 2, MS_CONST, MD_NOISE_A, 50);   /* +50%: two decades, 100 ms */
                mx_slot(e, 3, MS_CONST, MD_NOISE_R, 50);   /* 1 s */
            }
            note(e, 1, 57, 100);
            early[k] = mx_rms(e, 20);
            held[k] = mx_rms(e, 400);
            note(e, 0, 57, 0);
            mx_rms(e, 100);
            tail[k] = mx_rms(e, 100);
            E->destroy(e);
        }
        snprintf(what, sizeof what, "matrix Noise EG Attack / Release +50%%: the first 20 ms %.3f (%.3f without), held "
                 "%.3f (%.3f), 100-200 ms after the key %.4f (%.4f)", early[1], early[0], held[1], held[0], tail[1], tail[0]);
        check(early[1] < 0.5 * early[0] && fabs(held[1] / held[0] - 1) < 0.15 && tail[1] > 10 * tail[0] + 1e-4, what);
        void *e = mx_patch();
        set(e, "o1_level", 0);
        set(e, "noise_level", 100);
        route(e, "noise", 0);
        set(e, "noise_env", 1);
        set(e, "noise_sus", 1);
        set(e, "noise_r", 50);    /* 100 ms */
        mx_slot(e, 2, MS_CONST, MD_NOISE_SUS, -100);   /* SUSTAIN off: the release time runs as a decay */
        note(e, 1, 57, 100);
        mx_rms(e, 600);
        double gone = mx_rms(e, 100);
        set(e, "mod2_amt", 0);
        note(e, 1, 59, 100);
        mx_rms(e, 600);
        double stays = mx_rms(e, 100);
        snprintf(what, sizeof what, "matrix Noise EG Sustain -100%%: held 600 ms later %.4f (gone), without %.3f", gone, stays);
        check(gone < 0.02 * stays && stays > 0.05, what);
        E->destroy(e);
    }
    /* ---- the EG switches: LOOP, LATCH, SYNC, MULTI TRIG ---- */
    {
        void *e = mx_patch();   /* a pluck: sustain 0 */
        set(e, "amp_s", 0); set(e, "amp_d", knob_ms(50)); set(e, "amp_a", 0);
        note(e, 1, 57, 100);
        mx_rms(e, 800);
        double plain = mx_rms(e, 200);
        note(e, 0, 57, 0);
        mx_rms(e, 200);
        mx_slot(e, 2, MS_CONST, MD_FENV_MULTI + MDS_COUNT * MX_AMP_ENV + MDS_LOOP, 100);
        note(e, 1, 57, 100);
        mx_rms(e, 800);
        double looped = mx_rms(e, 200);
        note(e, 0, 57, 0);
        E->destroy(e);
        snprintf(what, sizeof what, "matrix Amp Env Loop +100%%: a pluck held 0.8 s %.4f (silent), looping %.3f", plain, looped);
        check(plain < 1e-3 && looped > 0.05, what);
    }
    {
        void *e = mx_patch();
        set(e, "amp_s", 70); set(e, "amp_r", knob_ms(20));
        mx_slot(e, 2, MS_CONST, MD_FENV_MULTI + MDS_COUNT * MX_AMP_ENV + MDS_LATCH, 100);
        note(e, 1, 57, 100);
        mx_rms(e, 200);
        note(e, 0, 57, 0);
        mx_rms(e, 300);
        double latched = mx_rms(e, 100);
        set(e, "mod2_amt", 0);   /* LATCH off by modulation: it lets go now */
        mx_rms(e, 200);
        double let_go = mx_rms(e, 100);
        E->destroy(e);
        snprintf(what, sizeof what, "matrix Amp Env Latch +100%%: 300 ms after key up %.3f (held), the modulation gone %.5f "
                 "(released)", latched, let_go);
        check(latched > 0.05 && let_go < 1e-4, what);
    }
    {
        double t_fall[2];
        for (int k = 0; k < 2; k++) {
            void *e = mx_patch();   /* decay knob 50 (100 ms free; synced, 1/4 triplet: 333 ms at 120 BPM) */
            set(e, "amp_s", 0); set(e, "amp_a", 0); set(e, "amp_d", 50);
            E->set_param(e, "lfo_bpm", "120");
            if (k) mx_slot(e, 2, MS_CONST, MD_FENV_MULTI + MDS_COUNT * MX_AMP_ENV + MDS_SYNC, 100);
            static float x[SR];
            note(e, 1, 57, 100);
            render_f(e, x, SR);
            int top = 0;   /* (synced, the attack is a division too: from the loudest moment on) */
            for (int j = 0; j + 220 < SR / 4; j += 220)
                if (rms_range(x, j, j + 220) > rms_range(x, top, top + 220)) top = j;
            double pk = rms_range(x, top, top + 220);
            int i = top;
            while (i + 220 < SR && rms_range(x, i, i + 220) > 0.01 * pk) i += 220;
            t_fall[k] = (i - top) / 44.1;
            E->destroy(e);
        }
        snprintf(what, sizeof what, "matrix Amp Env Sync +100%%: from the peak to -40 dB in %.0f ms synced (a 1/4T decay at "
                 "120 BPM: 333 ms), %.0f ms free (100 ms)", t_fall[1], t_fall[0]);
        check(t_fall[1] > 260 && t_fall[1] < 345 && t_fall[0] < 140, what);
    }
    {
        double rise[2];
        for (int k = 0; k < 2; k++) {
            void *e = mx_patch();   /* mono, a slow attack to 20% sustain; a legato second key */
            set(e, "voices", 0);
            set(e, "amp_a", knob_ms(150)); set(e, "amp_d", knob_ms(20)); set(e, "amp_s", 20);
            if (k) mx_slot(e, 2, MS_CONST, MD_FENV_MULTI + MDS_COUNT * MX_AMP_ENV + MDS_MULTI, -100);
            note(e, 1, 57, 100);
            mx_rms(e, 500);
            note(e, 1, 60, 100);
            mx_rms(e, 60);
            rise[k] = mx_rms(e, 40);
            E->destroy(e);
        }
        snprintf(what, sizeof what, "matrix Amp Env Multi Trig -100%%: a legato key 60-100 ms on %.3f (carries on at the "
                 "sustain), %.3f with MULTI TRIG (a new attack)", rise[1], rise[0]);
        check(rise[0] > 2.0 * rise[1], what);
    }
    /* ---- the X/Y pad: PAD POLARITY, X/Y HOLD ---- */
    {
        void *e = mx_patch();
        double f0 = mx_pitch(e, 57);
        mx_slot(e, 2, MS_PAD_X, MD_PITCH, 50);   /* +12 st at the right edge */
        midi3(e, 0xB0, PAD_X_CC_T, 64);          /* the middle */
        double uni = mx_pitch(e, 57);
        set(e, "pad_polarity", 1);
        double bip = mx_pitch(e, 57);
        midi3(e, 0xB0, PAD_X_CC_T, 0);
        double left = mx_pitch(e, 57);
        snprintf(what, sizeof what, "PAD POLARITY: Pad X in the middle to pitch: unipolar %+.2f st, bipolar %+.2f st; at the "
                 "left edge bipolar %+.2f st", 12 * log2(uni / f0), 12 * log2(bip / f0), 12 * log2(left / f0));
        check(fabs(12 * log2(uni / f0) - 6.05) < 0.15 && fabs(12 * log2(bip / f0)) < 0.15 && fabs(12 * log2(left / f0) + 12) < 0.15,
              what);
        set(e, "pad_polarity", 0);
        midi3(e, 0xB0, PAD_TOUCH_CC_T, 127);
        midi3(e, 0xB0, PAD_X_CC_T, 127);
        midi3(e, 0xB0, PAD_TOUCH_CC_T, 0);   /* let go: HOLD on (the default) keeps it */
        double held = mx_pitch(e, 57);
        set(e, "pad_hold", 0);
        midi3(e, 0xB0, PAD_TOUCH_CC_T, 127);
        midi3(e, 0xB0, PAD_X_CC_T, 127);
        midi3(e, 0xB0, PAD_TOUCH_CC_T, 0);   /* HOLD off: back to its rest */
        double rest = mx_pitch(e, 57);
        snprintf(what, sizeof what, "X/Y HOLD: let go at the right edge, on %+.2f st (kept), off %+.2f st (back to 0)",
                 12 * log2(held / f0), 12 * log2(rest / f0));
        check(fabs(12 * log2(held / f0) - 12) < 0.15 && fabs(12 * log2(rest / f0)) < 0.15, what);
        E->destroy(e);
    }
    /* ---- LOAD SAVED POS: the wheel and pad saved with the sound and put back when it loads ---- */
    {
        static char st[1 << 16], st_off[1 << 16];
        void *e = mx_patch();
        mx_slot(e, 2, MS_WHEEL, MD_PITCH, 50);
        mx_slot(e, 3, MS_PAD_Y, MD_O1_PITCH, 25);
        midi3(e, 0xB0, 1, 127);                  /* the wheel up: +12 st */
        midi3(e, 0xB0, PAD_Y_CC_T, 127);         /* the pad's Y at the top: osc 1 +6 st */
        E->get_param(e, "state", st_off, sizeof st_off);   /* LOAD SAVED POS off: nothing saved */
        set(e, "load_pos", 3);
        E->get_param(e, "state", st, sizeof st);
        E->destroy(e);
        void *g = mx_patch(), *h = mx_patch();
        double f0 = mx_pitch(g, 57);
        E->set_param(g, "state", st);
        double loaded = mx_pitch(g, 57);
        E->set_param(h, "state", st_off);
        double plain = mx_pitch(h, 57);
        snprintf(what, sizeof what, "LOAD SAVED POS Both: the wheel and pad saved (%d) and restored: %+.2f st on a fresh "
                 "instance (+18 expected); without it %+.2f st and nothing saved (%d)", strstr(st, "wheel_pos=") != NULL,
                 12 * log2(loaded / f0), 12 * log2(plain / f0), strstr(st_off, "wheel_pos") == NULL);
        check(strstr(st, "wheel_pos=100") && strstr(st, "pad_y_pos=100") && fabs(12 * log2(loaded / f0) - 18) < 0.2
              && fabs(12 * log2(plain / f0)) < 0.15 && !strstr(st_off, "wheel_pos"), what);
        E->destroy(g);
        E->destroy(h);
    }
    /* ---- STEP MOD and sequencer lanes on the new destinations ---- */
    {
        void *e = mx_patch();
        set(e, "seq_on", 1);
        set(e, "seq_step_mod", 1);
        seqcmd(e, "sel 0 0");
        set(e, "o1_oct", 3);   /* 8' -> 4': recorded as step 1's offset, the knob stays */
        set(e, "amp_loop", 1); /* a switch: +100% */
        static char txt[8192];
        seq_text(e, txt, sizeof txt);
        sq_seq_t q;
        int ok = sq_decode(txt, &q, MD_COUNT, MS_COUNT);
        int found_oct = 0, found_loop = 0;
        for (int l = 0; l < SQ_ML; l++) {
            found_oct |= q.m[l].dest == MD_O1_OCT && q.m[l].off[0] == 25;
            found_loop |= q.m[l].dest == MD_FENV_MULTI + MDS_COUNT * MX_AMP_ENV + MDS_LOOP && q.m[l].off[0] == 100;
        }
        snprintf(what, sizeof what, "STEP MOD: the footage knob 8' -> 4' records +25%% on Osc 1 Octave (%d), AMP LOOP on "
                 "+100%% on Amp Env Loop (%d); the knobs stay (%g, %g)", found_oct, found_loop, get(e, "o1_oct"),
                 get(e, "amp_loop"));
        check(ok && found_oct && found_loop && get(e, "o1_oct") == 2 && get(e, "amp_loop") == 0, what);
        /* the stopped preview of the selected step: the octave lane plays (one octave up) */
        set(e, "seq_step_mod", 1);
        double f = mx_pitch(e, 57);
        snprintf(what, sizeof what, "sequencer lane on Osc 1 Octave (+25%%): A3 plays %.1f Hz (an octave up)", f);
        check(fabs(f / 440 - 1) < 0.01, what);
        E->destroy(e);
    }
    {
        /* a lane past destination 255 (LFO 4 Wave): the two-byte form, and back; lanes under 256 stay version 1 */
        sq_seq_t q, back;
        uint8_t b[4096];
        sq_clear(&q);
        sq_set_note(&q, 0, 0, 60, 100, 50, 1, 0);
        q.m[0].dest = MD_LAD_CUT;
        q.m[0].off[3] = 40;
        int v1 = sq_pack(&q, b) > 0 && b[0] == SQ_VERSION;
        q.m[1].dest = MD_LFO1_WAVE + 3;
        q.m[1].ctl = MS_WHEEL;
        q.m[1].amt = 50;
        q.m[1].off[2] = -60;
        int v2 = sq_pack(&q, b) > 0 && b[0] == SQ_VERSION_WIDE;
        static char t[SQ_TEXT_MAX];
        sq_encode(&q, t, sizeof t);
        int dec = sq_decode(t, &back, MD_COUNT, MS_COUNT);
        int same = dec && !memcmp(&back, &q, sizeof q);
        int refused = !sq_decode(t, &back, MD_LFO1_WAVE, MS_COUNT);   /* an engine with fewer destinations refuses it */
        snprintf(what, sizeof what, "sequence lanes: destinations under 256 keep version 1 (%d); LFO 4 Wave (%d) writes the "
                 "two-byte form (%d) and reads back the same (%d); a shorter destination list refuses it (%d)", v1,
                 MD_LFO1_WAVE + 3, v2, same, refused);
        check(MD_COUNT > 256 && v1 && v2 && same && refused, what);
    }
    printf("     matrix destinations: %d (the step-mod list's 22 from %d)\n", MD_COUNT, MD_O1_OCT);
}
