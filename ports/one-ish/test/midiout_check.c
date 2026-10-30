/* (2026-10-30) MIDI OUT (src/midi_out.h) and SUSTAIN IN SYNTH (sus_pedal). Included by offline_check.c after the
 * other checks (its helpers: check, set, setkv, note, get, pg_patch, E).
 *   off       not enabled (the MPC build), or MIDI OUT off: nothing is sent, whatever plays
 *   keys      a key goes out on the Synth's channel with its transpose, at the render's first frame; Keys / Generated /
 *             Both; Synth n on channel n by default; a key held by the pedal ends when the pedal comes up
 *   timing    the arpeggiator's, the sequencer's and every generator's notes go out at the very sample they play
 *             (the engine's own log of generated notes), on the Synth's channel
 *   pairs     every note-on is followed by its note-off and never by a second note-on of that note and channel:
 *             PANIC (all channels' All Notes Off), All Sound Off, the Synth off, a preset (state) loaded, MIDI OUT off,
 *             what it sends changed, its channel moved, generators stopped, the keys and the sequencer on one note
 *   sustain   the pedal holds only the Synths whose SUSTAIN PEDAL is on; turned off while down, it lets go
 *   bench     (offline_bench midiout) CPU with MIDI OUT off and on */
void one_ish_midi_out_enable(void *inst, int on);   /* src/synths.h */
int one_ish_midi_out_take(void *inst, void (*fn)(void *ctx, int at, const uint8_t *m), void *ctx);

#define MOC_MAX 65536
typedef struct { long long t; uint8_t m[3]; } moc_ev_t;
static struct {
    moc_ev_t ev[MOC_MAX];
    int n;
    long long base;      /* the frame the current render started at */
    int held[16][128];   /* note-ons not yet ended, per channel and note */
    int double_on;       /* a note-on while that note and channel was already on */
    int stray_off;       /* a note-off of a note that was not on */
} MO;

static void moc_fn(void *ctx, int at, const uint8_t *m) {
    (void)ctx;
    int st = m[0] & 0xf0, ch = m[0] & 15, n = m[1];
    if (MO.n < MOC_MAX) MO.ev[MO.n++] = (moc_ev_t){MO.base + at, {m[0], m[1], m[2]}};
    if (st == 0x90 && m[2]) {
        if (MO.held[ch][n]) MO.double_on++;
        MO.held[ch][n]++;
    } else if (st == 0x80 || st == 0x90) {
        if (!MO.held[ch][n]) MO.stray_off++;
        else MO.held[ch][n]--;
    }
}
static void moc_reset(void) { memset(&MO, 0, sizeof MO); }
static void moc_take(void *e) { one_ish_midi_out_take(e, moc_fn, NULL); }
static void moc_render(void *e, double secs) {
    int16_t buf[256];
    for (long at = 0; at < (long)(secs * SR); at += 128) {
        E->render(e, buf, 128);
        moc_take(e);
        MO.base += 128;
    }
}
static int moc_hanging(void) {
    int h = 0;
    for (int c = 0; c < 16; c++)
        for (int n = 0; n < 128; n++) h += MO.held[c][n];
    return h;
}
static int moc_count(int st, int ch) {   /* events of a status (0x90 note-on, 0x80 off) on channel ch (-1: any) */
    int c = 0;
    for (int i = 0; i < MO.n; i++)
        if ((MO.ev[i].m[0] & 0xf0) == st && (ch < 0 || (MO.ev[i].m[0] & 15) == ch)) c++;
    return c;
}
static int moc_paired(void) { return !MO.double_on && !MO.stray_off && !moc_hanging(); }
static void *moc_patch(void) {
    void *e = pg_patch();
    one_ish_midi_out_enable(e, 1);
    set(e, "mout_on", 1);
    return e;
}

static void mo_off_checks(void) {
    moc_reset();
    void *e = pg_patch();   /* not enabled: as the MPC build */
    setkv(e, "mout_on=1,arp_on=1,gen_keys=1");
    note(e, 1, 60, 100);
    moc_render(e, 1.0);
    note(e, 0, 60, 0);
    moc_render(e, 0.2);
    check(MO.n == 0, "MIDI OUT: nothing is sent while the host has not enabled it (the MPC build)");
    E->destroy(e);
    moc_reset();
    e = pg_patch();
    one_ish_midi_out_enable(e, 1);
    setkv(e, "arp_on=1,gen_keys=1");
    note(e, 1, 60, 100);
    moc_render(e, 1.0);
    check(MO.n == 0, "MIDI OUT: off (the default), nothing is sent");
    E->destroy(e);
}

static void mo_key_checks(void) {
    char what[160];
    moc_reset();
    void *e = moc_patch();
    moc_render(e, 0.01);
    set(e, "transpose", 2);
    long long at_key = MO.base;   /* (the next render's first frame) */
    note(e, 1, 60, 99);
    moc_render(e, 0.01);
    int ok = MO.n == 1 && MO.ev[0].m[0] == 0x90 && MO.ev[0].m[1] == 62 && MO.ev[0].m[2] == 99 && MO.ev[0].t == at_key;
    set(e, "transpose", 5);   /* (changed while the key is down: its note-off still says 62) */
    note(e, 0, 60, 0);
    moc_render(e, 0.01);
    ok &= MO.n == 2 && MO.ev[1].m[0] == 0x80 && MO.ev[1].m[1] == 62;
    check(ok && moc_paired(), "MIDI OUT keys: a key goes out on channel 1 with its transpose, at the block's first frame; "
                              "its note-off on the same note though the transpose moved");
    E->destroy(e);
    /* what it sends: Keys / Generated / Both, with the arpeggiator on and chord memory off */
    static const char *const SEND[3] = {"Keys", "Generated", "Both"};
    for (int w = 0; w < 3; w++) {
        moc_reset();
        e = moc_patch();
        set(e, "mout_send", w);
        note(e, 1, 50, 100);   /* plain: a key */
        moc_render(e, 0.1);
        note(e, 0, 50, 0);
        int keys = moc_count(0x90, -1);
        setkv(e, "arp_on=1,gen_keys=1");
        note(e, 1, 60, 100);
        note(e, 1, 64, 100);
        moc_render(e, 1.0);
        int gen = moc_count(0x90, -1) - keys;
        set(e, "mout_on", 0);
        moc_render(e, 0.01);
        snprintf(what, sizeof what, "MIDI OUT sends %s: %d key notes, %d arpeggiated, all paired", SEND[w], keys, gen);
        check((keys > 0) == (w != 1) && (gen > 0) == (w != 0) && moc_paired(), what);   /* (0 Keys, 1 Generated) */
        E->destroy(e);
    }
    /* each Synth on its own channel: Synth n on n */
    moc_reset();
    e = moc_patch();
    setkv(e, "s2_syn_on=1,s3_syn_on=1,s2_mout_on=1,s3_mout_on=1");
    note(e, 1, 60, 100);
    moc_render(e, 0.05);
    note(e, 0, 60, 0);
    moc_render(e, 0.05);
    snprintf(what, sizeof what, "MIDI OUT: three Synths layered send on channels 1, 2, 3 (%d %d %d note-ons)",
             moc_count(0x90, 0), moc_count(0x90, 1), moc_count(0x90, 2));
    check(moc_count(0x90, 0) == 1 && moc_count(0x90, 1) == 1 && moc_count(0x90, 2) == 1 && moc_paired(), what);
    E->destroy(e);
    /* chord memory: a key's chord goes out as keys */
    moc_reset();
    e = moc_patch();
    setkv(e, "mout_send=0,chord_on=1");
    note(e, 1, 60, 100);
    moc_render(e, 0.05);
    int chord = moc_count(0x90, 0);
    note(e, 0, 60, 0);
    moc_render(e, 0.05);
    snprintf(what, sizeof what, "MIDI OUT keys: chord memory's chord goes out as the key's notes (%d notes)", chord);
    check(chord >= 3 && moc_paired(), what);
    E->destroy(e);
    /* the pedal: a key let go under it ends when the pedal comes up */
    moc_reset();
    e = moc_patch();
    uint8_t ped[3] = {0xb0, 64, 127};
    E->midi(e, ped, 3);
    note(e, 1, 60, 100);
    moc_render(e, 0.05);
    note(e, 0, 60, 0);
    moc_render(e, 0.2);
    int held = moc_count(0x80, -1) == 0;
    ped[2] = 0;
    E->midi(e, ped, 3);
    moc_render(e, 0.05);
    check(held && moc_count(0x80, -1) == 1 && moc_paired(),
          "MIDI OUT keys: a key let go under the sustain pedal ends when the pedal comes up");
    E->destroy(e);
}

/* every generated note-on at the sample the engine played it (its own log), on the channel */
static void mo_timing_checks(void) {
    static const struct { const char *name, *kv; int key; } G[] = {
        {"arpeggiator", "arp_on=1,gen_keys=1,arp_div=4", 60},
        {"step sequencer", "seq_on=1,seq_pattern=2,seq_play=1", -1},
        {"Euclid", "gen_euclid_on=1", -1},
        {"Turing", "gen_tm_on=1", -1},
        {"balls", "phys_balls_on=1,phys_balls=4,phys_gravity=0,phys_bounce=100,phys_speed=75,phys_max=30", -1},
        {"particles", "phys_keys=1,phys_parts_on=1,phys_max=30", 60},
        {"LIFE", "life_on=1,life_mode=1,life_notes=1,life_max=30", -1},
        {"orbits", "orb_on=1,orb_quant=1", -1},
        {"pendulums", "pend_on=1,pend_cycle=0,pend_max=30", -1},
        {"rain", "rain_on=1,rain_rate=80,rain_max=30", -1},
        {"Markov", "mkv_on=1,mkv_rhythm=1,mkv_max=30", -1},
    };
    static pg_ev_t log[PG_MAX];
    static char b[8192];
    for (unsigned g = 0; g < sizeof G / sizeof G[0]; g++) {
        moc_reset();
        void *e = moc_patch();
        setkv(e, "mout_send=1,mout_ch=9");   /* generated only, on channel 10 */
        setkv(e, G[g].kv);
        if (G[g].key >= 0) note(e, 1, G[g].key, 100);
        int nl = 0;
        int16_t buf[256];
        for (long at = 0; at < 6 * SR; at += 128) {   /* 6 s; the engine's log read after each block */
            E->render(e, buf, 128);
            moc_take(e);
            MO.base += 128;
            if (E->get_param(e, "gen_log", b, sizeof b) <= 0) continue;
            for (char *p = b; *p;) {
                long long t;
                int nt, v, used;
                if (sscanf(p, "%lld:%d:%d;%n", &t, &nt, &v, &used) != 3) break;
                if (v > 0 && nl < PG_MAX) log[nl++] = (pg_ev_t){t, nt, v};
                p += used;
            }
        }
        int ons = 0, match = 0;
        for (int i = 0; i < MO.n; i++) {
            if ((MO.ev[i].m[0] & 0xf0) != 0x90) continue;
            int j = ons++;
            if (j < nl && MO.ev[i].t == log[j].t && MO.ev[i].m[1] == log[j].note && MO.ev[i].m[2] == log[j].vel
                && (MO.ev[i].m[0] & 15) == 9)
                match++;
        }
        if (G[g].key >= 0) note(e, 0, G[g].key, 0);
        set(e, "mout_on", 0);   /* switched off mid-note: what is on ends */
        moc_render(e, 0.01);
        char what[200];
        snprintf(what, sizeof what, "MIDI OUT %s: %d note-ons, %d of them at the engine's own sample, note and velocity "
                 "(%d logged), on channel 10; all paired after MIDI OUT off", G[g].name, ons, match, nl);
        check(ons > 0 && match == ons && ons == nl && moc_paired(), what);
        E->destroy(e);
    }
}

/* every note-on gets its note-off, in each way a note can be cut short */
static void mo_pair_checks(void) {
    static const char *const PLAY = "arp_on=1,gen_keys=1,arp_div=5,s2_syn_on=1,s2_mout_on=1,s2_seq_on=1,s2_seq_pattern=2,"
                                    "s2_seq_play=1,s3_syn_on=1,s3_mout_on=1,s3_gen_euclid_on=1,s3_phys_balls_on=1,"
                                    "s3_phys_max=30";
    static const char *const CASE[] = {"PANIC (All Notes Off on every channel)", "All Sound Off", "each Synth off",
                                       "a preset loaded (a state)", "MIDI OUT off", "MIDI OUT sends Keys only",
                                       "MIDI OUT channel moved", "the generators stopped (arp, sequencer, Euclid off)",
                                       "INTO SYNTH (a sound loaded into each Synth)"};
    char what[200];
    for (unsigned c = 0; c < sizeof CASE / sizeof CASE[0]; c++) {
        moc_reset();
        void *e = moc_patch();
        setkv(e, PLAY);
        uint8_t ped[3] = {0xb0, 64, 127};
        E->midi(e, ped, 3);   /* the pedal down, keys held: every way of holding a note at once */
        note(e, 1, 60, 100);
        note(e, 1, 67, 100);
        moc_render(e, 1.5);
        note(e, 0, 67, 0);
        moc_render(e, 0.3);
        int before = moc_count(0x90, -1);
        for (int ch = 0; c == 0 && ch < 16; ch++) {
            uint8_t m[3] = {(uint8_t)(0xb0 | ch), 123, 0};
            E->midi(e, m, 3);
        }
        if (c == 1)
            for (int ch = 0; ch < 3; ch++) {
                uint8_t m[3] = {(uint8_t)(0xb0 | ch), 120, 0};
                E->midi(e, m, 3);
            }
        if (c == 2) setkv(e, "syn_on=0,s2_syn_on=0,s3_syn_on=0");
        if (c == 3) E->set_param(e, "state", "v=1;dv=1;o1_wave=0;mout_on=1");
        if (c == 4) setkv(e, "mout_on=0,s2_mout_on=0,s3_mout_on=0");
        if (c == 5) setkv(e, "mout_send=0,s2_mout_send=0,s3_mout_send=0");
        if (c == 6) setkv(e, "mout_ch=5,s2_mout_ch=6,s3_mout_ch=7");
        if (c == 7) setkv(e, "arp_on=0,s2_seq_on=0,s3_gen_euclid_on=0,s3_phys_balls_on=0");
        if (c == 8)
            for (int s = 1; s <= 3; s++) {
                char k[256];
                snprintf(k, sizeof k, "synthload%d\nv=1;dv=1;o1_wave=0", s);
                E->set_param(e, "state", k);
            }
        moc_render(e, 0.01);
        int quiet = c < 5 || c == 7 || c == 8;   /* nothing new may start (the held key 60 is let go below) */
        note(e, 0, 60, 0);
        ped[2] = 0;
        E->midi(e, ped, 3);
        if (quiet) setkv(e, "arp_on=0,s2_seq_on=0,s3_gen_euclid_on=0,s3_phys_balls_on=0");
        moc_render(e, 0.5);
        int left = quiet ? moc_hanging() : 0;   /* ended by the event itself (and the keys and pedal let go) */
        setkv(e, "mout_on=0,s2_mout_on=0,s3_mout_on=0");   /* the rest ends, to count */
        moc_render(e, 0.01);
        snprintf(what, sizeof what, "MIDI OUT pairs, %s: %d note-ons before, every one ended once (%d twice on, %d "
                 "stray offs, %d hanging, %d left before MIDI OUT off)", CASE[c], before, MO.double_on, MO.stray_off,
                 moc_hanging(), left);
        check(before > 10 && moc_paired() && left == 0, what);
        E->destroy(e);
    }
    /* the keys and the sequencer on the same note: counted, so no second note-on without an off between */
    moc_reset();
    void *e = moc_patch();
    setkv(e, "seq_on=1,seq_pattern=2,seq_play=1");
    for (int k = 0; k < 20; k++) {
        note(e, 1, 48 + k % 24, 100);
        moc_render(e, 0.13);
        note(e, 0, 48 + k % 24, 0);
    }
    set(e, "mout_on", 0);
    moc_render(e, 0.01);
    snprintf(what, sizeof what, "MIDI OUT: keys over the sequencer, %d note-ons, never two on one note (%d), all ended",
             moc_count(0x90, -1), MO.double_on);
    check(moc_paired(), what);
    E->destroy(e);
}

static int moc_sounding(void *e, const char *key) { return (int)get(e, key); }
static void mo_sustain_checks(void) {
    char what[200];
    void *e = pg_patch();
    setkv(e, "amp_r=0,s2_syn_on=1,s2_amp_r=0,s2_sus_pedal=0,s3_syn_on=0");
    uint8_t ped[3] = {0xb0, 64, 127};
    E->midi(e, ped, 3);
    note(e, 1, 60, 100);
    int16_t buf[256];
    for (int i = 0; i < 40; i++) E->render(e, buf, 128);
    note(e, 0, 60, 0);
    for (int i = 0; i < 200; i++) E->render(e, buf, 128);
    int s1 = moc_sounding(e, "voices_sounding"), s2 = moc_sounding(e, "s2_voices_sounding");
    snprintf(what, sizeof what, "SUSTAIN IN SYNTH: the pedal holds Synth 1 (%d voices) and not Synth 2 (%d), its "
             "SUSTAIN PEDAL off", s1, s2);
    check(s1 > 0 && s2 == 0, what);
    set(e, "sus_pedal", 0);   /* off while the pedal is down: Synth 1 lets go too */
    for (int i = 0; i < 200; i++) E->render(e, buf, 128);
    s1 = moc_sounding(e, "voices_sounding");
    snprintf(what, sizeof what, "SUSTAIN IN SYNTH: switched off while the pedal is down, Synth 1 lets go (%d voices)", s1);
    check(s1 == 0, what);
    E->destroy(e);
    /* the default: every Synth (as before) */
    e = pg_patch();
    setkv(e, "amp_r=0,s2_syn_on=1,s2_amp_r=0,s3_syn_on=1,s3_amp_r=0");
    ped[2] = 127;
    E->midi(e, ped, 3);
    note(e, 1, 60, 100);
    for (int i = 0; i < 40; i++) E->render(e, buf, 128);
    note(e, 0, 60, 0);
    for (int i = 0; i < 200; i++) E->render(e, buf, 128);
    int all = moc_sounding(e, "voices_sounding") > 0 && moc_sounding(e, "s2_voices_sounding") > 0 &&
              moc_sounding(e, "s3_voices_sounding") > 0;
    check(all, "SUSTAIN IN SYNTH: by default the pedal holds all three Synths, as before");
    E->destroy(e);
    /* a state keeps it; INTO SYNTH leaves a Synth's own (its setup) */
    e = pg_patch();
    set(e, "s2_sus_pedal", 0);
    static char st[262144];
    E->get_param(e, "state", st, sizeof st);
    void *f = pg_patch();
    E->set_param(f, "state", st);
    int kept = get(f, "s2_sus_pedal") == 0 && get(f, "sus_pedal") == 1;
    E->set_param(f, "state", "synthload2\nv=1;dv=1;o1_wave=0;sus_pedal=1;mout_on=1");
    kept &= get(f, "s2_sus_pedal") == 0 && get(f, "s2_mout_on") == 0;
    check(kept, "SUSTAIN PEDAL and MIDI OUT: saved with the sound; INTO SYNTH keeps the Synth's own (they are its setup)");
    E->destroy(e);
    E->destroy(f);
}

static void midi_out_checks(void) {
    mo_off_checks();
    mo_key_checks();
    mo_timing_checks();
    mo_pair_checks();
    mo_sustain_checks();
}

/* CPU: three Synths playing (arp, sequencer, Euclid), MIDI OUT off and on */
static void midi_out_bench(void) {
    for (int on = 0; on < 2; on++) {
        void *e = pg_patch();
        one_ish_midi_out_enable(e, 1);
        setkv(e, "arp_on=1,gen_keys=1,arp_div=5,s2_syn_on=1,s2_seq_on=1,s2_seq_pattern=2,s2_seq_play=1,"
                 "s3_syn_on=1,s3_gen_euclid_on=1");
        if (on) setkv(e, "mout_on=1,s2_mout_on=1,s3_mout_on=1");
        for (int k = 0; k < 4; k++) note(e, 1, 60 + 3 * k, 100);
        int16_t buf[256];
        for (int i = 0; i < 200; i++) { E->render(e, buf, 128); moc_take(e); }
        double best = 1e9;
        for (int r = 0; r < 5; r++) {
            moc_reset();
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int i = 0; i < 3000; i++) { E->render(e, buf, 128); one_ish_midi_out_take(e, moc_fn, NULL); }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double us = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / 3000;
            if (us < best) best = us;
        }
        printf("bench MIDI OUT %s: 3 Synths (arp, sequencer, Euclid), 4 keys: %.2f us per 128-frame block (%d events "
               "in the last 3000 blocks)\n", on ? "on " : "off", best, MO.n);
        E->destroy(e);
    }
}
