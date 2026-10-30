/* (2026-10-30) The new defaults and what came with them. Included from offline_check.c after physgen_check.c (its
 * helpers: check, set, get, setkv, note, render_f, pg_run, pg_in_key; E_REAL is the engine as a host has it, E the one
 * that starts at the old defaults for the older checks).
 *   new instance  all three Synths on, each its own Init (Synth 2 an octave down, Synth 3 a triangle an octave up,
 *                 both quieter), the aftertouch / X/Y pad / ribbon rows (13-16), LINK SENDS and both master slots on;
 *                 the layer is not clipping and about as loud as one Synth was
 *   old states    a state, patch or SAVE SYNTH sound written before (no "dv=") reads what it leaves out as the old
 *                 defaults: Synth 2 off, rows 13-16 empty, Synth 2 Synth 1's sound; a new one round trips as saved;
 *                 LOAD INTO SYNTH 2 of a sound gets Synth 1's defaults, not Synth 2's Init
 *   effects       MASTER FX 1 and 2 each switch off on their own: off, the very samples of its send at 0 (after the
 *                 fade); the master ON still both
 *   hands         the pad, ribbon and aftertouch rows move the sound in a new instance and change nothing at rest;
 *                 channel pressure reaches every Synth on its channel, poly pressure the Synths holding that key
 *   randomiser    RANDOMISE always wires aftertouch, pad X, pad Y and ribbon (distinct, heard destinations); MUTATE
 *                 never adds, moves or removes them; the ANALOG page and SPREAD never change; MONO / POLY;
 *                 GENERATIVE only: one or two generators on, heard, finite, in key */

static void *fresh(void) { return E_REAL->create(NULL); }
static double peak_rms(void *e, int frames, double *rms) {   /* both channels; peak and RMS */
    int16_t buf[256];
    double pk = 0, s = 0;
    for (int at = 0; at < frames; at += 128) {
        E_REAL->render(e, buf, 128);
        for (int i = 0; i < 256; i++) {
            double v = fabs(buf[i] / 32768.0);
            pk = v > pk ? v : pk;
            s += v * v;
        }
    }
    *rms = sqrt(s / (frames * 2.0));
    return pk;
}
static void chord(void *e, int on) {
    static const int NT[3] = {48, 55, 64};
    for (int k = 0; k < 3; k++) {
        uint8_t m[3] = {(uint8_t)(on ? 0x90 : 0x80), (uint8_t)NT[k], 100};
        E_REAL->midi(e, m, 3);
    }
}
static void midi3r(void *e, int a, int b, int c) {
    uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
    E_REAL->midi(e, m, 3);
}
static uint64_t hash_out(void *e, int blocks) {
    int16_t buf[256];
    uint64_t h = 1469598103934665603ull;
    for (int b = 0; b < blocks; b++) {
        E_REAL->render(e, buf, 128);
        for (int i = 0; i < 256; i++) h = (h ^ (uint16_t)buf[i]) * 1099511628211ull;
    }
    return h;
}

static void defaults_instance(void) {
    char what[400], st[65536];
    void *e = fresh();
    int ok = get(e, "syn_on") == 1 && get(e, "s2_syn_on") == 1 && get(e, "s3_syn_on") == 1 && get(e, "s2_o1_oct") == 1
             && get(e, "s3_o1_oct") == 3 && get(e, "s3_o1_wave") == 0 && get(e, "s2_volume") == 45
             && get(e, "s3_volume") == 35 && get(e, "o1_oct") == 2 && get(e, "volume") == 70;
    int rows = get(e, "mod13_src") == 8 && get(e, "mod14_src") == 23 && get(e, "mod15_src") == 24
               && get(e, "mod16_src") == 22 && get(e, "s3_mod14_src") == 23 && get(e, "mod14_amt") == -90;
    int sh = get(e, "link_send") == 1 && get(e, "dly_on") == 1 && get(e, "rev_on") == 1;
    snprintf(what, sizeof what, "a new instance: Synths 1-3 on (S2 16', S3 Tri 4', levels 70/45/35): %s; aftertouch, pad X, "
             "pad Y, ribbon rows 13-16 in every Synth: %s; LINK SENDS, FX 1 ON, FX 2 ON: %s", ok ? "yes" : "NO",
             rows ? "yes" : "NO", sh ? "yes" : "NO");
    check(ok && rows && sh, what);
    /* the Init layer: heard, not clipping; about one old Synth's loudness */
    double r3, r1, p3, p1;
    chord(e, 1);
    p3 = peak_rms(e, SR, &r3);
    chord(e, 0);
    void *one = E->create(NULL);   /* (the old Init: Synth 1 alone) */
    chord(one, 1);
    p1 = peak_rms(one, SR, &r1);
    snprintf(what, sizeof what, "the Init layer (a 3-note chord, 1 s): peak %.3f, RMS %.4f; Synth 1 alone (the old Init) "
             "peak %.3f, RMS %.4f: x %.2f the loudness (%.1f dB), no clipping", p3, r3, p1, r1, r3 / r1,
             20 * log10(r3 / r1));
    check(p3 < 0.98 && r3 > r1 * 0.9 && r3 < r1 * 2.0, what);
    E_REAL->destroy(one);
    /* the voices: 3 Synths x 3 notes of a 16-voice pool */
    E_REAL->get_param(e, "voices_total", st, sizeof st);
    E_REAL->destroy(e);

    /* old states: no "dv=" */
    e = fresh();
    E_REAL->set_param(e, "state", "v=1;flt_cut=50");
    ok = get(e, "s2_syn_on") == 0 && get(e, "s3_syn_on") == 0 && get(e, "mod14_src") == 0 && get(e, "mod13_src") == 0
         && get(e, "s2_mod16_src") == 0 && get(e, "s2_o1_oct") == 2 && get(e, "s2_volume") == 70
         && get(e, "s3_o1_wave") == 1 && get(e, "flt_cut") == 50;
    E_REAL->set_param(e, "state", "v=2;s2_syn_on=1;~volume=55");
    int ok2 = get(e, "s2_syn_on") == 1 && get(e, "s2_volume") == 55 && get(e, "s2_o1_oct") == 2 && get(e, "s3_syn_on") == 0;
    snprintf(what, sizeof what, "an old-format state (no dv=) with no s2_syn_on loads with Synth 2 and 3 off, Synth 1's old "
             "sound in them and no pad rows: %s; an old v=2 state with Synth 2 on keeps its 8' and its own level: %s",
             ok ? "yes" : "NO", ok2 ? "yes" : "NO");
    check(ok && ok2, what);
    E_REAL->destroy(e);
    /* new states round trip: a new instance's state says dv=1, leaves the new defaults out, and restores them */
    e = fresh();
    E_REAL->get_param(e, "state", st, sizeof st);
    void *b = E->create(NULL);
    E_REAL->set_param(b, "state", st);
    ok = !strncmp(st, "v=1;dv=1", 8) && !strstr(st, "s2_syn_on") && get(b, "s2_syn_on") == 1 && get(b, "s3_syn_on") == 1
         && get(b, "mod15_src") == 24 && get(b, "s2_o1_oct") == 1;
    set(e, "s2_syn_on", 0);
    E_REAL->get_param(e, "state", st, sizeof st);
    E_REAL->set_param(b, "state", st);
    ok2 = strstr(st, "s2_syn_on=0") && get(b, "s2_syn_on") == 0 && get(b, "s3_syn_on") == 1;
    snprintf(what, sizeof what, "a new instance's state: \"%.24s...\" restores all three on and the pad rows: %s; Synth 2 "
             "switched off is saved as s2_syn_on=0 and comes back off: %s", st, ok ? "yes" : "NO", ok2 ? "yes" : "NO");
    check(ok && ok2, what);
    /* SAVE SYNTH 2 / LOAD INTO SYNTH 3: Synth 2's own Init travels with it; a sound loads against Synth 1's defaults */
    char one_s[16384], cmd[17000];
    E_REAL->get_param(e, "synth_state2", one_s, sizeof one_s);
    snprintf(cmd, sizeof cmd, "synthload3\n%s", one_s);
    E_REAL->set_param(e, "state", cmd);
    ok = get(e, "s3_o1_oct") == 1 && get(e, "s3_volume") == 45 && get(e, "s3_o1_wave") == 1 && get(e, "s3_mod14_src") == 23;
    E_REAL->set_param(e, "state", "synthload2\nv=1;dv=1;flt_cut=40");
    ok2 = get(e, "s2_o1_oct") == 2 && get(e, "s2_volume") == 70 && get(e, "s2_flt_cut") == 40 && get(e, "s2_mod14_src") == 23;
    E_REAL->set_param(e, "state", "synthload2\nv=1;flt_cut=40");   /* (an old patch: no pad rows) */
    int ok3 = get(e, "s2_mod14_src") == 0 && get(e, "s2_mod13_src") == 0 && get(e, "s2_flt_cut") == 40;
    snprintf(what, sizeof what, "SAVE SYNTH 2 then INTO SYNTH 3 brings Synth 2's 16' and level: %s; a new sound INTO SYNTH 2 "
             "gets Synth 1's defaults (8', 70), the pad rows: %s; an old sound (no dv=): no pad rows: %s",
             ok ? "yes" : "NO", ok2 ? "yes" : "NO", ok3 ? "yes" : "NO");
    check(ok && ok2 && ok3, what);
    E_REAL->destroy(e);
    E_REAL->destroy(b);
}

static void defaults_effects(void) {
    char what[300];
    /* a slot switched off: after its fade, exactly the samples of its send at 0 (the master effects run on the rest) */
    static const char *const SLOT[2] = {"dly_on", "rev_on"}, *const SEND[2] = {"mfx_send1", "mfx_send2"};
    int ok[2];
    for (int k = 0; k < 2; k++) {
        void *a = E->create(NULL), *b = E->create(NULL);
        setkv(a, "amp_a=0,amp_s=100,mfx_send1=40,mfx_send2=40");
        setkv(b, "amp_a=0,amp_s=100,mfx_send1=40,mfx_send2=40");
        set(a, SLOT[k], 0);
        set(b, SEND[k], 0);
        int16_t ba[256], bb[256];
        for (int i = 0; i < 4; i++) { E->render(a, ba, 128); E->render(b, bb, 128); }   /* (the fade) */
        note(a, 1, 60, 100);
        note(b, 1, 60, 100);
        int same = 1;
        double diff_on = 0;
        for (int i = 0; i < 700; i++) {
            E->render(a, ba, 128);
            E->render(b, bb, 128);
            same &= !memcmp(ba, bb, sizeof ba);
        }
        set(a, SLOT[k], 1);   /* back on: the effect is heard again */
        for (int i = 0; i < 400; i++) {
            E->render(a, ba, 128);
            E->render(b, bb, 128);
            for (int j = 0; j < 256; j++) diff_on += fabs((double)ba[j] - bb[j]);
        }
        ok[k] = same && diff_on > 1000;
        E->destroy(a);
        E->destroy(b);
    }
    snprintf(what, sizeof what, "MASTER FX 1 ON off: the samples of SEND 1 at 0: %s; MASTER FX 2 ON off: of SEND 2 at 0: %s; "
             "each heard again when back on", ok[0] ? "yes" : "NO", ok[1] ? "yes" : "NO");
    check(ok[0] && ok[1], what);
}

static void defaults_hands(void) {
    char what[400];
    /* in a new instance (Synth 1 alone, so one sound): at rest the rows do nothing; moved, they do */
    void *a = fresh(), *b = E->create(NULL);
    set(a, "s2_syn_on", 0); set(a, "s3_syn_on", 0);
    note(a, 1, 60, 100);
    note(b, 1, 60, 100);
    int rest = hash_out(a, 200) == hash_out(b, 200);
    uint64_t base = hash_out(b, 100);
    midi3r(a, 0xb0, 2, 127);   /* pad X right: the ladder closes */
    double rx, ra;
    peak_rms(a, 8192, &rx);
    (void)base;
    midi3r(a, 0xb0, 2, 0);
    void *c = fresh();
    set(c, "s2_syn_on", 0); set(c, "s3_syn_on", 0);
    note(c, 1, 60, 100);
    peak_rms(c, 8192, &ra);
    int padx = rx < ra * 0.8;
    snprintf(what, sizeof what, "a new instance: at rest the pad / ribbon / aftertouch rows change nothing: %s; pad X right "
             "darkens it (RMS %.4f against %.4f)", rest ? "yes" : "NO", rx, ra);
    check(rest && padx, what);
    E_REAL->destroy(a); E_REAL->destroy(b); E_REAL->destroy(c);
    /* pressure: channel pressure reaches every Synth on its channel; poly pressure the Synths holding the key */
    a = fresh(); b = fresh(); c = fresh();
    void *d = fresh();
    for (int k = 0; k < 4; k++) {
        void *x = k == 0 ? a : k == 1 ? b : k == 2 ? c : d;
        setkv(x, "s2_key_lo=72,s3_syn_on=0,syn_on=1,key_hi=71,mod13_amt=60,s2_mod13_amt=60");   /* a split */
        note(x, 1, 60, 100);
        note(x, 1, 76, 100);
    }
    midi3r(b, 0xd0, 127, 0);           /* channel pressure: both halves */
    midi3r(c, 0xa0, 60, 127);          /* poly pressure on C4: Synth 1 only */
    midi3r(d, 0xa0, 50, 127);          /* on a key nobody holds: nothing */
    double ha = 0, hb = 0, hc = 0, hd = 0;
    peak_rms(a, 32768, &ha); peak_rms(b, 32768, &hb); peak_rms(c, 32768, &hc); peak_rms(d, 32768, &hd);
    uint64_t xa = hash_out(a, 50), xb = hash_out(b, 50), xc = hash_out(c, 50), xd = hash_out(d, 50);
    int ok = xa != xb && xa != xc && xc != xb && xa == xd;
    snprintf(what, sizeof what, "aftertouch: channel pressure moves both Synths of a split, poly pressure only the Synth "
             "holding that key, poly pressure on a key nobody holds nothing: %s", ok ? "yes" : "NO");
    check(ok, what);
    E_REAL->destroy(a); E_REAL->destroy(b); E_REAL->destroy(c); E_REAL->destroy(d);
}

static int dc_opt(const float *n, const char *key) {   /* an option's index (or a value) in a normalized sound */
    int i = rnd_key(key);
    return i < NPARAMS ? (int)lroundf(rnd_real(i, n[i])) : -1;
}
static void defaults_randomiser(void) {
    char what[600];
    static float def[NPARAMS], out[NPARAMS], mut[NPARAMS];
    for (int i = 0; i < NPARAMS; i++) def[i] = PARAMS[i].def;
    int d1 = rnd_key("mod1_dst"), s1 = rnd_key("mod1_src");
    static const char *const SRC[4] = {"Aftertouch", "Pad X (CC 2)", "Pad Y (CC 3)", "Ribbon"};
    int wired = 0, distinct = 0, heard = 0, kept = 0, analog = 0, n = 0, mono = 0, poly = 0, locked = 0;   /* (n: a count here) */
    for (int s = 0; s < 150; s++) {
        uint32_t seed = 400 + s;
        rnd_opts_t o = {(s % 3) * 0.5f, 0, 0, s % 2 ? RV_MONO : RV_POLY};
        float cur[NPARAMS];
        memcpy(cur, def, sizeof cur);
        cur[rnd_key("drift")] = 0.3f;   /* (someone's ANALOG settings: kept) */
        cur[rnd_key("an_cut")] = 0.25f;
        rnd_generate_x(cur, out, 0, &seed, &o);
        n++;
        int w = 1, dis = 1, hd = 1;
        for (int h = 0; h < 4; h++) {
            int k = 13 + h;
            char ks[16], kd[16], ka[16];
            snprintf(ks, sizeof ks, "mod%d_src", k); snprintf(kd, sizeof kd, "mod%d_dst", k);
            snprintf(ka, sizeof ka, "mod%d_amt", k);
            w &= dc_opt(out, ks) == rnd_opt(s1, SRC[h]) && dc_opt(out, kd) > 0 && fabsf(rnd_real(rnd_key(ka), out[rnd_key(ka)])) >= 1.0f;
            for (int h2 = 0; h2 < h; h2++) {
                char k2[16];
                snprintf(k2, sizeof k2, "mod%d_dst", 13 + h2);
                dis &= dc_opt(out, kd) != dc_opt(out, k2);
            }
            const char *dn = PARAMS[d1].opts[dc_opt(out, kd) > 0 ? dc_opt(out, kd) : 0];
            if (!strncmp(dn, "Synth FX", 8)) hd &= dc_opt(out, "modfx_on") == 1;
            if (!strncmp(dn, "Send", 4)) hd &= dc_opt(out, "mfx_on") == 1;
        }
        wired += w; distinct += dis; heard += hd;
        analog += fabsf(out[rnd_key("drift")] - 0.3f) < 1e-6f && fabsf(out[rnd_key("an_cut")] - 0.25f) < 1e-6f
                  && fabsf(out[rnd_key("spread")] - def[rnd_key("spread")]) < 1e-6f;
        if (s % 2) mono += dc_opt(out, "voices") == 0;
        else poly += dc_opt(out, "voices") > 0;
        /* MUTATE (any WILD): the four rows as they were */
        uint32_t ms = 900 + s;
        rnd_mutate_x(out, mut, 0.8f, 0, &ms, &o);
        int same = 1;
        for (int k = 13; k <= 16; k++) {
            static const char *const F[] = {"src", "dst", "amt", "via", "xf", "on"};
            for (int f = 0; f < 6; f++) {
                char kk[24];
                snprintf(kk, sizeof kk, "mod%d_%s", k, F[f]);
                same &= fabsf(mut[rnd_key(kk)] - out[rnd_key(kk)]) < 1e-6f;
            }
        }
        kept += same && fabsf(mut[rnd_key("drift")] - 0.3f) < 1e-6f && dc_opt(mut, "voices") == dc_opt(out, "voices");
        /* the MODULATION lock: the matrix as it was */
        uint32_t ls = 50 + s;
        rnd_generate_x(def, mut, 1u << RS_MOD, &ls, &o);
        locked += fabsf(mut[rnd_key("mod14_src")] - def[rnd_key("mod14_src")]) < 1e-6f
                  && fabsf(mut[rnd_key("mod2_src")] - def[rnd_key("mod2_src")]) < 1e-6f;
    }
    snprintf(what, sizeof what, "RANDOMISE (150 sounds, WILD 0/50/100%%): aftertouch, pad X, pad Y and ribbon wired in rows "
             "13-16 %d, distinct destinations %d, heard %d; ANALOG and SPREAD kept %d; MONO all mono %d of %d, POLY all poly "
             "%d of %d; MUTATE 80%% kept the four rows, ANALOG and the voices %d; MODULATION locked: matrix kept %d",
             wired, distinct, heard, analog, mono, n / 2, poly, n - n / 2, kept, locked);
    check(wired == n && distinct == n && heard == n && analog == n && mono == n / 2 && poly == n - n / 2 && kept == n &&
          locked == n, what);
}

/* the GENERATIVE section alone (the GENERATIVE page's RANDOMISE): one or two generators on, settings in range, heard,
 * finite and in the Synth's key and scale (Turing aside: it plays in its own SCALE) */
static void defaults_gens(void) {
    char what[500];
    static float def[NPARAMS], out[NPARAMS];
    for (int i = 0; i < NPARAMS; i++) def[i] = PARAMS[i].def;
    const unsigned all_sound = (1u << RS_COUNT) - 1u;
    int n = 0, onok = 0, heard = 0, finite = 1, inkey = 0, keyed = 0, sound_same = 0, notes = 0;
    for (int s = 0; s < 24; s++) {
        uint32_t seed = 1200 + 31 * s;
        rnd_opts_t o = {0.0f, 1u << RS_GEN, 0, RV_BOTH};
        rnd_generate_x(def, out, all_sound, &seed, &o);
        n++;
        int on = 0, same = 1;
        for (int g = 0; g < RND_NGEN; g++) on += dc_opt(out, RND_GEN_ON[g]) == 1;
        onok += on >= 1 && on <= 2;
        for (int i = 0; i < NPARAMS; i++)   /* nothing outside GENERATIVE moved */
            if (rnd_section_of(i) != RS_GEN && fabsf(out[i] - def[i]) > 1e-6f) same = 0;
        sound_same += same;
        void *e = E->create(NULL);
        set(e, "amp_r", 10);
        E->set_param(e, "lfo_bpm", "120");
        for (int i = 0; i < NPARAMS; i++)
            if (rnd_section_of(i) == RS_GEN && fabsf(out[i] - def[i]) > 1e-6f) set(e, PARAMS[i].key, rnd_real(i, out[i]));
        int root = dc_opt(out, "key_root"), scale = dc_opt(out, "key_scale");
        note(e, 1, 48 + root, 100);   /* (held-key generators play from it) */
        int ne = 0;
        pg_run(e, 6.0, "gen_log", pg_a, &ne);
        double r;
        peak_rms(e, 4096, &r);
        finite &= isfinite(r);
        heard += ne > 0;
        notes += ne;
        int tm_own = dc_opt(out, "gen_tm_on") == 1;   /* (Turing: its own SCALE, not KEY / SCALE) */
        if (!tm_own && ne) { keyed++; inkey += pg_in_key(pg_a, ne, root, scale); }
        E->destroy(e);
    }
    snprintf(what, sizeof what, "the generators' RANDOMISE (24, the sound locked): one or two on %d of %d, the sound itself "
             "unchanged %d, heard %d (%d notes in 6 s), finite %s, every note in key %d of %d", onok, n, sound_same, heard,
             notes, finite ? "yes" : "NO", inkey, keyed);
    check(onok == n && sound_same == n && heard >= n - 2 && finite && inkey == keyed, what);
}

/* (offline_bench) what the new defaults cost per 128-frame block, 4 held voices of Init: Synth 1 alone with rows 13-16
 * empty (the old Init), Synth 1 alone with the hands rows (the new Init's Synth 1), the whole new Init (3 Synths) */
static double dc_block_us(void *e, int blocks) {
    int16_t buf[256];
    struct timespec t0, t1;
    for (int b = 0; b < 200; b++) E_REAL->render(e, buf, 128);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < blocks; b++) E_REAL->render(e, buf, 128);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / blocks;
}
static void defaults_bench(void) {
    enum { BLOCKS = 20000 };
    static const int K[4] = {48, 55, 60, 64};
    double us[3][3];
    for (int run = 0; run < 3; run++)
        for (int v = 0; v < 3; v++) {
            void *e = v == 0 ? E->create(NULL) : fresh();
            if (v == 1) { set(e, "s2_syn_on", 0); set(e, "s3_syn_on", 0); }
            for (int k = 0; k < 4; k++) midi3r(e, 0x90, K[k], 100);
            us[run][v] = dc_block_us(e, BLOCKS);
            E_REAL->destroy(e);
        }
    double m[3];
    for (int v = 0; v < 3; v++) {   /* the median of three runs */
        double a = us[0][v], b = us[1][v], c = us[2][v];
        m[v] = a > b ? (b > c ? b : a > c ? c : a) : (a > c ? a : b > c ? c : b);
    }
    printf("bench: Init, 4 held keys, per 128-frame block: Synth 1 alone, rows 13-16 empty (the old Init) %.2f us; with "
           "the aftertouch / pad / ribbon rows %.2f us; the new Init (3 Synths) %.2f us\n", m[0], m[1], m[2]);
}

static void defaults_checks(void) {
    defaults_instance();
    defaults_effects();
    defaults_hands();
    defaults_randomiser();
    defaults_gens();
}
