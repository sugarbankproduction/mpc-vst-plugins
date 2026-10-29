/* Multitimbral offline checks (src/synths.h): included by offline_check.c after the sequencer's, so it uses that file's
 * helpers (check, set, note, patch, render_f, peak_hz, get, setkv, silent_after, seqcmd ...).
 *   params      Synth 2 and 3 have a copy of every Synth 1 param but the shared ones; the engine agrees which are shared
 *   independent a Synth 2 or 3 param moves only that Synth; a shared one moves all three; automation of Synth 2 (on or
 *               off) leaves Synth 1's samples exactly as they were
 *   sounds      Synths 2 and 3 play their own sounds; off, they are silent and cost nothing; SOLO: only the soloed
 *               ones heard (the same samples as the others turned down), never saved
 *   channels    Omni, one channel for all, Per Synth
 *   ranges      a split (each half its own Synth) and a layer (both)
 *   pool        16 voices shared: never more sounding; a new note of one Synth steals another's; No Stealing waits
 *   perf        each Synth's own arpeggiator and sequencer; PLAY ALL starts the sequences together; LOAD INTO SYNTH
 *   state       a full 3-Synth performance round trip; the worst case against the chunk; an 8 KB chunk keeps Synth 1
 *   bench       (offline_bench) 1 Synth against 3 Synths, 16 voices */

static int key_shared(const char *k) {   /* presets/make_synths.py's GLOBAL */
    static const char *const G[] = {"mfx_on", "mfx_series", "mfx_width", "mfx_buss", "clock_swing", "master_tune",
                                    "fine_tune", "midi_ch", "learn_mode", "master_level", "seq_all"};
    if (!strncmp(k, "dly_", 4) || !strncmp(k, "rev_", 4)) return 1;
    for (unsigned i = 0; i < sizeof G / sizeof G[0]; i++)
        if (!strcmp(k, G[i])) return 1;
    return 0;
}
static int pindex(const char *k) {
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) return i;
    return -1;
}
/* a value of param i that is not its default (in its own units) */
static double off_default(int i) {
    const param_t *p = &PARAMS[i];
    double def = p->nopts ? round(p->def * (p->nopts - 1)) : p->min + p->def * (p->max - p->min);
    double v = p->nopts ? fmod(def + 1, p->nopts) : p->min + (p->max - p->min) * 0.123457;
    if (!p->nopts && fabs(v - def) < 1e-3) v = p->min + (p->max - p->min) * 0.876543;
    return v;
}
static int is_transient(const char *k) {   /* not kept in a state on purpose (or readouts) */
    const char *r = !strncmp(k, "s2_", 3) || !strncmp(k, "s3_", 3) ? k + 3 : k;
    return !strcmp(r, "seq_play") || !strcmp(r, "seq_rec") || !strcmp(r, "seq_step_mod") || !strcmp(r, "seq_pos")
           || !strcmp(r, "seq_all") || !strcmp(r, "syn_solo");
}
static int log_notes(void *e, const char *key) {   /* note-ons in a Synth's "gen_log" since the last read */
    static char b[8192];
    int n = 0;
    if (E->get_param(e, key, b, sizeof b) <= 0) return 0;
    for (char *p = b; *p;) {
        long long t;
        int nt, v, used;
        if (sscanf(p, "%lld:%d:%d;%n", &t, &nt, &v, &used) != 3) break;
        n += v > 0;
        p += used;
    }
    return n;
}
/* a two-Synth patch: Synth 1 a saw at 8', Synth 2 (on) a saw an octave up, both plain and loud enough */
static void *two_synths(void) {
    void *e = patch(SAW, 1, 100, 100, 20);
    setkv(e, "s2_syn_on=1,s2_o1_oct=3,s2_amp_a=0,s2_amp_s=100,s2_amp_r=10,s2_volume=70,s2_flt_cut=100,flt_cut=100");
    return e;
}
static double hz_of(void *e) {   /* the strongest frequency of the next N samples */
    static float x[N];
    render_f(e, x, N);
    return peak_hz(x);
}

static void multitimbral(void) {
    char what[256];

    /* ---- params ---- */
    int per = 0, shared = 0, bad = 0, copies = 0;
    for (int i = 0; i < NPARAMS; i++) {
        const char *k = PARAMS[i].key;
        if (!strncmp(k, "s2_", 3) || !strncmp(k, "s3_", 3)) { copies++; continue; }
        char k2[64], k3[64];
        snprintf(k2, sizeof k2, "s2_%s", k);
        snprintf(k3, sizeof k3, "s3_%s", k);
        int has2 = pindex(k2) >= 0, has3 = pindex(k3) >= 0;
        if (key_shared(k)) { shared++; bad += has2 || has3; }
        else { per++; bad += !has2 || !has3; }
    }
    snprintf(what, sizeof what, "params: %d per Synth (each with a Synth 2 and 3 copy), %d shared (no copies), %d in all "
             "(%d copies); %d wrong", per, shared, NPARAMS, copies, bad);
    check(!bad && copies == 2 * per && NPARAMS == 3 * per + shared, what);

    /* ---- independence: each per-Synth param of Synth 2 moves Synth 2 only; a shared one moves all three ---- */
    {
        void *e = E->create(NULL);
        int wrong = 0, n = 0;
        char k1[64], k3[64], first[80] = "";
        for (int i = 0; i < NPARAMS; i++) {
            const char *k = PARAMS[i].key;
            if (strncmp(k, "s2_", 3) || PARAMS[i].string_display || !strcmp(k, "s2_seq_pattern")) continue;
            snprintf(k1, sizeof k1, "%s", k + 3);
            snprintf(k3, sizeof k3, "s3_%s", k + 3);
            double a1 = get(e, k1), a3 = get(e, k3), a2 = get(e, k), v = off_default(i);
            set(e, k, v);
            n++;
            int w = (fabs(get(e, k) - v) > 1e-3 && !is_transient(k)) + (get(e, k1) != a1 || get(e, k3) != a3);
            if (w && !wrong) snprintf(first, sizeof first, " %s", k);
            wrong += w;
            /* a switch that changes how later params are set (STEP MOD records knob moves instead of setting them)
             * goes back: each param after it is tested as it is by itself */
            if (is_transient(k)) set(e, k, a2);
        }
        snprintf(what, sizeof what, "independent: %d Synth 2 params each set without moving Synth 1 or 3 (%d wrong%s)", n,
                 wrong, first);
        check(n > 400 && !wrong, what);
        E->destroy(e);
        e = E->create(NULL);   /* (fresh: the loop above switched STEP MOD on, which takes knob moves) */
        set(e, "dly_time", 12.5);
        snprintf(what, sizeof what, "a shared param (master delay time) is the same for all three: %.1f %.1f %.1f",
                 get(e, "dly_time"), get(e, "s2_dly_time"), get(e, "s3_dly_time"));
        check(get(e, "s2_dly_time") == 12.5 && get(e, "s3_dly_time") == 12.5, what);
        E->destroy(e);
    }

    /* ---- automation of Synth 2 leaves Synth 1's samples as they were ---- */
    {
        static float xa[SR], xb[SR];
        for (int on = 0; on < 2; on++) {
            void *a = patch(SAW, 5, 200, 70, 100), *b = patch(SAW, 5, 200, 70, 100);
            set(a, "s2_syn_on", on); set(b, "s2_syn_on", on);
            set(a, "s2_volume", 0); set(b, "s2_volume", 0);   /* Synth 2 plays, silently */
            note(a, 1, 48, 100); note(b, 1, 48, 100);
            int same = 1;
            for (int blk = 0; blk < 8; blk++) {
                set(b, "s2_flt_cut", 10 * blk);   /* automation, block by block */
                set(b, "s2_o1_level", 100 - 10 * blk);
                set(b, "s2_mod1_amt", 30 * blk);
                render_f(a, xa, SR / 16);
                render_f(b, xb, SR / 16);
                for (int i = 0; i < SR / 16; i++) same &= xa[i] == xb[i];
            }
            snprintf(what, sizeof what, "automating Synth 2 (%s) changes nothing of Synth 1: the same samples", on ? "on, "
                     "silent" : "off");
            check(same, what);
            E->destroy(a); E->destroy(b);
        }
        void *a = two_synths();
        set(a, "volume", 0);   /* Synth 1 silent: only Synth 2 heard */
        note(a, 1, 57, 100);
        double f0 = hz_of(a);
        set(a, "s2_o1_oct", 2);   /* Synth 2 down an octave */
        double f1 = hz_of(a);
        snprintf(what, sizeof what, "automating Synth 2 moves Synth 2: %.0f Hz then %.0f Hz (want 440, 220)", f0, f1);
        check(fabs(f0 - 440) < 4 && fabs(f1 - 220) < 3, what);
        E->destroy(a);
    }

    /* ---- Synths 2 and 3 play their own sounds ---- */
    {
        void *e = two_synths();
        setkv(e, "s3_syn_on=1,s3_o1_oct=4,s3_amp_a=0,s3_amp_s=100,s3_volume=70,s3_flt_cut=100,volume=0,s2_volume=0");
        note(e, 1, 45, 100);   /* A2: Synth 1 110 Hz (silent), Synth 2 220 Hz (silent), Synth 3 two octaves up: 440 */
        double f3 = hz_of(e);
        set(e, "s3_volume", 0); set(e, "s2_volume", 70);
        double f2 = hz_of(e);
        set(e, "s2_volume", 0); set(e, "volume", 70);
        double f1 = hz_of(e);
        snprintf(what, sizeof what, "three Synths, three sounds on one key: Synth 1 %.0f Hz, Synth 2 %.0f Hz, Synth 3 %.0f Hz "
                 "(want 110, 220, 440)", f1, f2, f3);
        check(fabs(f1 - 110) < 2 && fabs(f2 - 220) < 3 && fabs(f3 - 440) < 4, what);
        E->destroy(e);
        e = two_synths();
        set(e, "s2_syn_on", 0); set(e, "volume", 0);
        note(e, 1, 57, 100);
        double r = rms_of(e, SR / 4);
        snprintf(what, sizeof what, "Synth 2 off: nothing of it is heard (rms %.6f)", r);
        check(r < 1e-5, what);
        E->destroy(e);

        /* SOLO: Synth 2 soloed is Synth 2 alone (past the 10 ms glide: the same samples as Synth 1 turned down) */
        void *a = two_synths(), *b = two_synths(), *c = two_synths();   /* c: no solo, rendered alongside */
        set(b, "volume", 0);
        set(a, "s2_syn_solo", 1);
        note(a, 1, 57, 100);
        note(b, 1, 57, 100);
        note(c, 1, 57, 100);
        static float xa[N], xb[N];
        render_f(a, xa, N);
        render_f(b, xb, N);
        double worst = 0, ra = 0;
        for (int i = SR / 50; i < N; i++) { worst = fmax(worst, fabs(xa[i] - xb[i])); ra += xa[i] * xa[i]; }
        ra = sqrt(ra / (N - SR / 50));
        snprintf(what, sizeof what, "SOLO Synth 2: only Synth 2 heard (largest difference to Synth 1 turned down %.2g, "
                 "rms %.3f, %.0f Hz)", worst, ra, peak_hz(xa));
        check(worst < 1e-6 && ra > 0.01, what);
        set(a, "syn_solo", 1);   /* both soloed: both heard, as with no solo */
        set(a, "s2_syn_solo", 0);
        set(a, "syn_solo", 0);
        render_f(c, xb, N);   /* (c catches up with a) */
        render_f(a, xa, N);
        render_f(c, xb, N);
        render_f(a, xa, N);
        render_f(c, xb, N);
        worst = 0;
        for (int i = 0; i < N; i++) worst = fmax(worst, fabs(xa[i] - xb[i]));
        snprintf(what, sizeof what, "SOLO off again: both heard as before (largest difference %.2g)", worst);
        check(worst < 1e-6, what);
        static char st[16384];
        set(a, "s2_syn_solo", 1);
        int n = E->get_param(a, "state", st, sizeof st);
        snprintf(what, sizeof what, "SOLO is not saved with the sound (state %d bytes)", n);
        check(n > 0 && !strstr(st, "syn_solo"), what);
        E->destroy(a);
        E->destroy(b);
        E->destroy(c);
    }

    /* ---- MIDI channels ---- */
    {
        double f[3];
        static const int mode[3] = {0, 17, 17};   /* Omni; Per Synth on channel 1; Per Synth on channel 2 */
        for (int c = 0; c < 3; c++) {
            void *e = two_synths();
            set(e, "midi_ch", mode[c]);
            if (c == 0) set(e, "s2_volume", 0);
            uint8_t m[3] = {(uint8_t)(0x90 | (c == 2)), 57, 100};
            E->midi(e, m, 3);
            f[c] = hz_of(e);
            E->destroy(e);
        }
        void *e = two_synths();
        set(e, "midi_ch", 5);   /* one channel for all: channel 5 */
        uint8_t m1[3] = {0x90, 57, 100}, m5[3] = {0x94, 57, 100};
        E->midi(e, m1, 3);
        double r1 = rms_of(e, SR / 8);
        E->midi(e, m5, 3);
        double r5 = rms_of(e, SR / 8);
        E->destroy(e);
        snprintf(what, sizeof what, "MIDI: Omni plays every Synth (Synth 1 heard: %.0f Hz); Per Synth, channel 1 plays "
                 "Synth 1 only (%.0f Hz), channel 2 Synth 2 only (%.0f Hz); channel 5 for all: channel 1 silent (%.5f), "
                 "channel 5 plays (%.3f)", f[0], f[1], f[2], r1, r5);
        check(fabs(f[0] - 220) < 3 && fabs(f[1] - 220) < 3 && fabs(f[2] - 440) < 4 && r1 < 1e-5 && r5 > 0.01, what);
    }

    /* ---- key ranges: a split and a layer ---- */
    {
        void *e = two_synths();
        setkv(e, "key_hi=59,s2_key_lo=60");   /* Synth 1 below middle C, Synth 2 from it */
        note(e, 1, 48, 100);
        double lo = hz_of(e);
        note(e, 0, 48, 0);
        silent_after(e, 300);
        note(e, 1, 69, 100);
        double hi = hz_of(e);
        char a[32], b[32];
        E->get_param(e, "voices_sounding", a, sizeof a);
        E->get_param(e, "s2_voices_sounding", b, sizeof b);
        snprintf(what, sizeof what, "split at middle C: C3 plays Synth 1 (%.0f Hz, want 131), A4 Synth 2 (%.0f Hz, want "
                 "880); voices sounding: Synth 1 %s, Synth 2 %s", lo, hi, a, b);
        check(fabs(lo - 130.8) < 2 && fabs(hi - 880) < 6 && atoi(a) == 0 && atoi(b) == 1, what);
        E->destroy(e);
        e = two_synths();   /* a layer: both full range */
        note(e, 1, 57, 100);
        render_f(e, (float[512]){0}, 512);
        E->get_param(e, "voices_sounding", a, sizeof a);
        E->get_param(e, "s2_voices_sounding", b, sizeof b);
        snprintf(what, sizeof what, "layer: one key sounds a voice in each Synth (%s, %s)", a, b);
        check(atoi(a) == 1 && atoi(b) == 1, what);
        E->destroy(e);
    }

    /* ---- the voice pool ---- */
    {
        void *e = two_synths();
        setkv(e, "voices=15,s2_voices=15,key_hi=59,s2_key_lo=60,amp_r=0,s2_amp_r=0");   /* 16 each, a split */
        char t[16], a[16], b[16];
        for (int k = 0; k < 12; k++) note(e, 1, 36 + k, 100);   /* 12 on Synth 1 */
        render_f(e, (float[512]){0}, 512);
        for (int k = 0; k < 8; k++) note(e, 1, 60 + k, 100);    /* 8 on Synth 2: 4 must come from Synth 1 */
        render_f(e, (float[4096]){0}, 4096);
        E->get_param(e, "voices_total", t, sizeof t);
        E->get_param(e, "voices_sounding", a, sizeof a);
        E->get_param(e, "s2_voices_sounding", b, sizeof b);
        snprintf(what, sizeof what, "pool: 12 keys on Synth 1, then 8 on Synth 2: %s voices sounding (want 16): Synth 1 %s "
                 "(the 4 oldest stolen), Synth 2 %s", t, a, b);
        check(atoi(t) == 16 && atoi(a) == 8 && atoi(b) == 8, what);
        E->destroy(e);
        e = two_synths();
        setkv(e, "voices=15,s2_voices=15,key_hi=59,s2_key_lo=60,s2_steal_mode=0");   /* Synth 2: No Stealing */
        for (int k = 0; k < 16; k++) note(e, 1, 36 + k % 24, 100);
        render_f(e, (float[512]){0}, 512);
        note(e, 1, 72, 100);
        render_f(e, (float[512]){0}, 512);
        E->get_param(e, "s2_voices_sounding", b, sizeof b);
        E->get_param(e, "voices_total", t, sizeof t);
        snprintf(what, sizeof what, "pool full of held notes, Synth 2 on No Stealing: its note is dropped (Synth 2 %s voices, "
                 "%s in all)", b, t);
        check(atoi(b) == 0 && atoi(t) == 16, what);
        E->destroy(e);
    }

    /* ---- each Synth's own arpeggiator and sequencer; PLAY ALL ---- */
    {
        void *e = two_synths();
        setkv(e, "s2_arp_on=1,s2_arp_div=4");   /* Synth 2 arpeggiates, Synth 1 plays the keys */
        log_notes(e, "gen_log"); log_notes(e, "s2_gen_log");
        note(e, 1, 60, 100); note(e, 1, 64, 100);
        render_f(e, (float[SR]){0}, SR);
        int g1 = log_notes(e, "gen_log"), g2 = log_notes(e, "s2_gen_log");
        snprintf(what, sizeof what, "Synth 2's arpeggiator plays its own notes (%d in 1 s), Synth 1 none (%d)", g2, g1);
        check(g2 >= 6 && g1 == 0, what);
        E->destroy(e);

        e = two_synths();
        setkv(e, "seq_on=1,s2_seq_on=1,seq_pattern=1,s2_seq_pattern=2");   /* each its own factory sequence */
        log_notes(e, "gen_log"); log_notes(e, "s2_gen_log");
        set(e, "seq_all", 1);   /* PLAY ALL */
        int p1 = (int)get(e, "seq_play"), p2 = (int)get(e, "s2_seq_play");
        render_f(e, (float[SR]){0}, SR);
        g1 = log_notes(e, "gen_log"); g2 = log_notes(e, "s2_gen_log");
        set(e, "seq_all", 0);
        render_f(e, (float[4096]){0}, 4096);
        log_notes(e, "gen_log"); log_notes(e, "s2_gen_log");
        render_f(e, (float[SR / 2]){0}, SR / 2);
        int after = log_notes(e, "gen_log") + log_notes(e, "s2_gen_log");
        snprintf(what, sizeof what, "PLAY ALL starts both Synths' sequences (PLAY %d %d; %d and %d notes in 1 s) and stops "
                 "them (%d notes after)", p1, p2, g1, g2, after);
        check(p1 == 1 && p2 == 1 && g1 > 2 && g2 > 2 && after == 0, what);
        E->destroy(e);
    }

    /* ---- LOAD INTO SYNTH, SAVE SYNTH ---- */
    {
        void *e = E->create(NULL);
        static char st[VST2_CHUNK_MAX];
        setkv(e, "s2_syn_on=1,s2_key_lo=60,flt_cut=33,o2_level=44,dly_time=20");
        E->get_param(e, "synth_state1", st, sizeof st);   /* Synth 1 as a single sound */
        static char cmd[VST2_CHUNK_MAX + 32];
        snprintf(cmd, sizeof cmd, "synthload3\n%s", st);
        E->set_param(e, "state", cmd);   /* into Synth 3 */
        snprintf(what, sizeof what, "SAVE SYNTH 1 then LOAD INTO SYNTH 3: Synth 3's cutoff %.0f, osc 2 %.0f (want 33, 44), "
                 "its setup kept (on %.0f), Synth 2 untouched (key low %.0f)", get(e, "s3_flt_cut"), get(e, "s3_o2_level"),
                 get(e, "s3_syn_on"), get(e, "s2_key_lo"));
        check(get(e, "s3_flt_cut") == 33 && get(e, "s3_o2_level") == 44 && get(e, "s3_syn_on") == 0
              && get(e, "s2_key_lo") == 60 && !strstr(st, "dly_time"), what);
        E->destroy(e);
    }

    /* ---- state: a whole performance round trip; the worst case ---- */
    {
        static char st[VST2_CHUNK_MAX], st2[VST2_CHUNK_MAX];
        void *a = E->create(NULL), *b = E->create(NULL);
        uint32_t seed = 4242;
        static float cur[NPARAMS], out[NPARAMS];
        for (int i = 0; i < NPARAMS; i++) cur[i] = PARAMS[i].def;
        for (int s = 0; s < 3; s++) {   /* a random sound in each Synth (the randomiser's Synth 1 keys, prefixed) */
            rnd_generate(cur, out, 0, &seed);
            for (int i = 0; i < NPARAMS; i++) {
                const char *k = PARAMS[i].key;
                if (PARAMS[i].string_display || !strncmp(k, "s2_", 3) || !strncmp(k, "s3_", 3)) continue;
                if (s && key_shared(k)) continue;
                char key[64];
                if (s) snprintf(key, sizeof key, "s%d_%s", s + 1, k);
                else snprintf(key, sizeof key, "%s", k);
                set(a, key, PARAMS[i].nopts ? round(out[i] * (PARAMS[i].nopts - 1)) : PARAMS[i].min + out[i] * (PARAMS[i].max - PARAMS[i].min));
            }
        }
        setkv(a, "s2_syn_on=1,s3_syn_on=1,s2_key_lo=40,s3_key_hi=90,midi_ch=17");
        seqcmd(a, "note 0 0 60 100 50 1 0\nnote 4 0 67 90 50 2 0");
        E->set_param(a, "state", "seqcmd3\nnote 2 1 48 80 70 1 0");
        int n = E->get_param(a, "state", st, sizeof st);
        E->set_param(b, "state", st);
        int n2 = E->get_param(b, "state", st2, sizeof st2);
        int same = n > 0 && n == n2 && !strcmp(st, st2);
        for (int i = 0; i < NPARAMS && same; i++)
            if (!PARAMS[i].string_display && !is_transient(PARAMS[i].key)) same &= get(a, PARAMS[i].key) == get(b, PARAMS[i].key);
        static float xa[SR / 2], xb[SR / 2];
        for (int k = 0; k < 4; k++) { note(a, 1, 40 + 9 * k, 100); note(b, 1, 40 + 9 * k, 100); }
        render_f(a, xa, SR / 2);
        render_f(b, xb, SR / 2);
        for (int i = 0; i < SR / 2; i++) same &= xa[i] == xb[i];
        snprintf(what, sizeof what, "a 3-Synth performance (three random sounds, setups, two sequences) round trips: every "
                 "value, the steps and the samples (%d characters)", n);
        check(same && strstr(st, ";s3_seq=") && strstr(st, ";seq="), what);

        /* the worst case: every param of all three Synths off its default, and three full sequences */
        for (int i = 0; i < NPARAMS; i++)
            if (!PARAMS[i].string_display && !is_transient(PARAMS[i].key)) set(b, PARAMS[i].key, off_default(i));
        static char cmds[64 * 9 * 40 + 8 * 70 * 16 + 16];
        uint32_t rng = 777;
        int used = 0;
        for (int s = 0; s < SQ_STEPS; s++)
            for (int l = 0; l < SQ_NL; l++)
                used += snprintf(cmds + used, sizeof cmds - used, "note %d %d %u %u %u %u %u\n", s, l, sq_rnd(&rng) % 128,
                                 1 + sq_rnd(&rng) % 127, 1 + sq_rnd(&rng) % 100, 1 + sq_rnd(&rng) % 4, sq_rnd(&rng) % 2);
        for (int l = 0; l < SQ_ML; l++) {
            used += snprintf(cmds + used, sizeof cmds - used, "lane %d %d %d %d\n", l, 1 + l * 9, l, l * 10);
            for (int s = 0; s < SQ_STEPS; s++)
                used += snprintf(cmds + used, sizeof cmds - used, "off %d %d %d\n", l, s, (int)(sq_rnd(&rng) % 201) - 100);
        }
        static char big[sizeof cmds + 64];
        for (int s = 0; s < 3; s++) {
            snprintf(big, sizeof big, "seqcmd%d\n%s", s + 1, cmds);
            E->set_param(b, "state", big);
        }
        n = E->get_param(b, "state", st, sizeof st);
        int all = strstr(st, ";seq=") && strstr(st, ";s2_seq=") && strstr(st, ";s3_seq=") && strstr(st, "s3_");
        printf("     multitimbral state sizes: a 3-Synth performance %d characters; the worst case (every param of all "
               "three off its default, three full sequences) %d of %d\n", n2, n, (int)sizeof st);
        snprintf(what, sizeof what, "the worst 3-Synth state fits this build's %d-byte chunk with room to spare: %d bytes, "
                 "everything kept", (int)sizeof st, n);
        check(n > 0 && n < (int)sizeof st * 85 / 100 && all, what);
        /* the MPC's 8 KB chunk: Synth 1 and the shared values kept, the rest left out from the end, never a cut state */
        static char small[8192];
        int ns = E->get_param(b, "state", small, sizeof small);
        void *c = E->create(NULL);
        E->set_param(c, "state", small);
        snprintf(what, sizeof what, "in an 8 KB chunk (MPC) the worst state keeps Synth 1 and the shared values (%d bytes; "
                 "Synth 1's cutoff %.2f = %.2f, the delay time %.2f = %.2f)", ns, get(c, "flt_cut"), get(b, "flt_cut"),
                 get(c, "dly_time"), get(b, "dly_time"));
        check_mpc(ns > 0 && ns < 8192 && get(c, "flt_cut") == get(b, "flt_cut") && get(c, "dly_time") == get(b, "dly_time"), what);
        E->destroy(a); E->destroy(b); E->destroy(c);
    }
}

/* CPU: the busy patch at 16 voices in Synth 1 alone, then shared 6 / 5 / 5 by three Synths of the same sound (each with
 * its own synth effect), and Synth 1 alone with Synths 2 and 3 off (against the single-Synth numbers above) */
static void multi_bench(void) {
    enum { BLOCKS = 20000 };
    int16_t buf[256];
    for (int c = 0; c < 2; c++) {
        void *e = patch(SAW, 5, 300, 70, 250);
        const char *pre[3] = {"", "s2_", "s3_"};
        int ns = c ? 3 : 1;
        for (int s = 0; s < ns; s++) {
            char kv[512];
            snprintf(kv, sizeof kv, "%ssyn_on=1,%svoices=15,%so2_level=80,%so3_level=60,%sflt_route=1,%slad_env=40,"
                     "%sflt_res=50,%smod2_src=6,%smod2_dst=9,%smod2_amt=30,%smodfx_on=1,%samp_a=5,%samp_s=70",
                     pre[s], pre[s], pre[s], pre[s], pre[s], pre[s], pre[s], pre[s], pre[s], pre[s], pre[s], pre[s], pre[s]);
            setkv(e, kv);
        }
        if (c) setkv(e, "midi_ch=17,syn_ch=1,s2_syn_ch=2,s3_syn_ch=3");
        for (int k = 0; k < 16; k++) {
            uint8_t m[3] = {(uint8_t)(0x90 | (c ? (k < 6 ? 0 : k < 11 ? 1 : 2) : 0)), (uint8_t)(48 + 3 * k), 100};
            E->midi(e, m, 3);
        }
        char t[16];
        E->render(e, buf, 128);
        E->get_param(e, "voices_total", t, sizeof t);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int b = 0; b < BLOCKS; b++) E->render(e, buf, 128);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / BLOCKS;
        printf("bench: 16 voices (%s voices sounding) in %s, chorus each: %6.2f us per 128-frame block (%.2f per voice)\n", t,
               c ? "3 Synths (6/5/5)" : "Synth 1 alone", us, us / 16);
        E->destroy(e);
    }
}
