/* Robustness checks (2026-10-30): the classes of bug a hardware synth's firmware notes list as fixed, each looked for
 * here. Included by offline_check.c after the generators' checks, so it uses that file's helpers (check, set, note,
 * patch, render_f, peak_hz, get, setkv, knob_ms, sounding, cc, midicfg, nrpn_send, seqcmd, log_notes ...).
 *   ends        every envelope ends: after the keys are let go each voice reaches idle and is freed, in every mode
 *               (normal, LOOP, LATCH with sustain 0, MULTI TRIG, delay and hold, curves, synced times in both EG TIMES
 *               settings, MIN, a 100-second release), played poly, mono legato and unison; LATCH holds by design and
 *               lets go when it is switched off or on All Notes Off
 *   on / off    switching a Synth ON or OFF never cuts the notes held in the other Synths
 *   flood       thousands of messages in one block on every channel (notes, CCs, bend, pressure, NRPN, garbage):
 *               no crash, no NaN, no note left hanging once every key is let go, and quick enough for real time
 *   routing     a note off releases what its note on started, whatever changed while the key was held: the global
 *               MIDI channel, Per Synth, a Synth's channel, its key range, transpose, octave; a chord, the arp
 *   panic       All Notes Off / All Sound Off (CC 123, 120, the window's PANIC: both on every channel) never start a
 *               note and stop every Synth's notes, the arp's, the generators' and the sequencer's OUTPUT targets'
 *   learn       a learned CC reaches every param (synced times, LFO sync, glide, ladder slope, SVF mode, osc sync ...),
 *               the host is told; NRPN synced envelope times land right in both EG TIMES settings
 *   loop/latch  LOOP with zero times, LATCH with sustain 0, a retrigger while looping, legato: finite, and they end
 *   init        a fresh instance (the Init sound) is one plain saw, nothing else mixed in, and starts clean */

static int rb_sounding(void *e) {   /* the voices not idle, every Synth (stolen ones fading out too) */
    return sounding(e, "") + sounding(e, "s2_") + sounding(e, "s3_");
}
static double rb_run(void *e, double sec) {   /* render sec seconds; the loudest sample (both channels) */
    int16_t buf[256];
    long frames = (long)(sec * SR);
    int peak = 0;
    for (long at = 0; at < frames; at += 128) {
        int n = frames - at < 128 ? (int)(frames - at) : 128;
        E->render(e, buf, n);
        for (int i = 0; i < 2 * n; i++) {
            int a = buf[i] < 0 ? -buf[i] : buf[i];
            if (a > peak) peak = a;
        }
    }
    return peak / 32768.0;
}
static void rb_msg(void *e, int st, int d1, int d2) {
    uint8_t m[3] = {(uint8_t)st, (uint8_t)d1, (uint8_t)d2};
    E->midi(e, m, 3);
}
static void rb_panic(void *e) {   /* as the window's PANIC (desktop/editor.c panic()): pedals up, CC 123 and 120 on all */
    for (int ch = 0; ch < 16; ch++) {
        rb_msg(e, 0xB0 | ch, 64, 0);
        rb_msg(e, 0xB0 | ch, 66, 0);
        rb_msg(e, 0xB0 | ch, 123, 0);
        rb_msg(e, 0xB0 | ch, 120, 0);
    }
}

/* ---- every envelope ends ---- */
typedef struct { const char *name, *kv; double wait; int modes; } rb_env_t;   /* modes: 1 poly, 2 mono, 4 unison */
static void rb_play(void *e, int mode, double hold) {   /* four keys, overlapping (legato in mono), then let go */
    static const int k[4] = {60, 64, 67, 72};
    for (int i = 0; i < 4; i++) {
        note(e, 1, k[i], 100);
        rb_run(e, hold / 8);
        if (mode == 2 && i) note(e, 0, k[i - 1], 0);   /* mono: each key let go after the next is down (legato) */
    }
    rb_run(e, hold / 2);
    for (int i = 0; i < 4; i++) note(e, 0, k[i], 0);
}
static void rb_env_ends(void) {
    char what[256], kv[512];
    static const rb_env_t C[] = {
        {"normal", "amp_a=25,amp_d=57,amp_s=60,amp_r=62", 2, 7},
        {"all times at 0", "amp_a=0,amp_d=0,amp_s=100,amp_r=0", 0.5, 7},
        {"sustain 0 (a pluck)", "amp_a=0,amp_d=50,amp_s=0,amp_r=60", 1, 7},
        {"LOOP", "amp_loop=1,amp_a=30,amp_d=35,amp_s=40,amp_r=40", 1, 7},
        {"LOOP, delay and hold", "amp_loop=1,amp_delay=40,amp_hold=40,amp_a=30,amp_d=35,amp_s=40,amp_r=40", 2, 7},
        {"LOOP, every time 0", "amp_loop=1,amp_delay=0,amp_hold=0,amp_a=0,amp_d=0,amp_s=50,amp_r=0", 0.5, 7},
        {"LOOP, MIN times", "eg_times=1,amp_loop=1,amp_a=0,amp_d=0,amp_s=50,amp_r=0", 0.5, 7},
        {"LATCH, sustain 0", "amp_latch=1,amp_a=10,amp_d=50,amp_s=0,amp_r=60", 1.5, 7},
        {"MULTI TRIG off", "amp_multi=0,amp_a=40,amp_d=50,amp_s=50,amp_r=60", 2, 7},
        {"RESET on", "amp_reset=1,amp_a=40,amp_d=50,amp_s=50,amp_r=60", 2, 7},
        {"curves +100", "amp_acurve=100,amp_dcurve=100,amp_rcurve=100,amp_r=75", 3, 7},
        {"curves -100", "amp_acurve=-100,amp_dcurve=-100,amp_rcurve=-100,amp_r=75", 3, 7},
        {"time scale 10%", "amp_scale=0,amp_r=100", 1.5, 7},
        {"synced (11 syncs), 4 bars", "amp_sync=1,amp_a=50,amp_d=50,amp_r=100", 9.5, 1},
        {"synced (13 syncs), 16 bars at 240", "eg_times=1,amp_sync=1,amp_a=50,amp_d=50,amp_r=100", 17.5, 1},
        {"MIN times", "eg_times=1,amp_a=0,amp_d=0,amp_s=80,amp_r=0", 0.2, 7},
        {"the other envelopes LOOP and LATCH", "fenv_loop=1,fenv_latch=1,menv_loop=1,menv_latch=1,lad_env=50", 1.5, 7},
        {"noise EG sustained, 10 s", "noise_level=50,noise_sus=1,noise_a=0,noise_r=100,amp_r=50", 1, 7},
        {"a 100 s release, exponential", "amp_r=100,amp_scale=100,amp_rcurve=100", 101, 1},
        {"a 100 s release, log curve", "amp_r=100,amp_scale=100,amp_rcurve=-100", 101, 1},
    };
    int all = 0, bad = 0;
    for (unsigned c = 0; c < sizeof C / sizeof C[0]; c++)
        for (int mode = 1; mode <= 4; mode <<= 1) {
            if (!(C[c].modes & mode)) continue;
            void *e = patch(SAW, 1, 100, 80, 100);
            E->set_param(e, "lfo_bpm", strstr(C[c].name, "240") ? "240" : "120");
            snprintf(kv, sizeof kv, "%s%s", C[c].kv, mode == 2 ? ",voices=0" : mode == 4 ? ",unison=1" : "");
            setkv(e, kv);
            rb_play(e, mode, 0.4);
            int during = rb_sounding(e), mid = -1;
            if (C[c].wait > 50) {   /* a long release: still going at 60%, so it is long (and then ends) */
                rb_run(e, C[c].wait * 0.6);
                mid = rb_sounding(e);
                rb_run(e, C[c].wait * 0.4);
            } else {
                rb_run(e, C[c].wait);
            }
            double tail = rb_run(e, 0.05);
            int left = rb_sounding(e), heals = (int)get(e, "heals");
            all++;
            if (left || tail != 0 || heals || !mid) {
                bad++;
                printf("     %s (%s): %d voices still sounding after %.1f s (peak %.5f, %d heals; %d at release)\n",
                       C[c].name, mode == 1 ? "poly" : mode == 2 ? "mono" : "unison", left, C[c].wait, tail, heals,
                       during);
            }
            E->destroy(e);
        }
    snprintf(what, sizeof what, "envelopes end: %d envelope settings x poly / mono legato / unison, every voice idle and "
             "silent after its release (%d stuck)", all, bad);
    check(!bad && all > 40, what);

    /* LATCH holds on after the keys come up (as it should), and lets go when switched off, or on All Notes Off */
    void *e = patch(SAW, 1, 100, 80, 100);
    setkv(e, "amp_latch=1,amp_r=50");
    rb_play(e, 1, 0.2);
    rb_run(e, 1);
    int held = rb_sounding(e);
    set(e, "amp_latch", 0);
    rb_run(e, 0.5);
    int off = rb_sounding(e);
    set(e, "amp_latch", 1);
    rb_play(e, 1, 0.2);
    rb_run(e, 1);
    int held2 = rb_sounding(e);
    rb_msg(e, 0xB0, 123, 0);
    rb_run(e, 0.5);
    snprintf(what, sizeof what, "LATCH: the keys up, %d voices hold on; LATCH off frees them (%d left); latched again "
             "(%d), All Notes Off frees them (%d left)", held, off, held2, rb_sounding(e));
    check(held == 4 && off == 0 && held2 == 4 && rb_sounding(e) == 0, what);
    /* HOLD (the voice section's) on, then off: the notes it held end */
    setkv(e, "amp_latch=0,hold=1");
    rb_play(e, 1, 0.2);
    rb_run(e, 0.5);
    held = rb_sounding(e);
    set(e, "hold", 0);
    rb_run(e, 0.5);
    snprintf(what, sizeof what, "HOLD on keeps %d notes after the keys; HOLD off ends them (%d left)", held,
             rb_sounding(e));
    check(held == 4 && rb_sounding(e) == 0, what);
    E->destroy(e);
}

/* ---- a Synth switched on or off leaves the others' notes alone ---- */
static void rb_syn_on(void) {
    char what[256];
    void *e = patch(SAW, 1, 100, 100, 100);
    setkv(e, "s3_syn_on=1,s3_amp_a=0,s3_amp_s=100,s3_volume=70,s3_o1_oct=3");
    note(e, 1, 60, 100);
    note(e, 1, 64, 100);
    rb_run(e, 0.2);
    int s1 = sounding(e, ""), s3 = sounding(e, "s3_");
    double before = rb_run(e, 0.1);
    int cut = 0;
    for (int k = 0; k < 6; k++) {   /* Synth 2 on, off, on ... with the keys held in 1 and 3 */
        set(e, "s2_syn_on", k & 1 ? 0 : 1);
        double p = rb_run(e, 0.05);
        cut += sounding(e, "") != s1 || sounding(e, "s3_") != s3 || p < before * 0.5;
    }
    set(e, "s3_syn_on", 0);   /* Synth 3 off: its notes go, Synth 1's stay */
    rb_run(e, 0.3);
    int s1b = sounding(e, ""), s3b = sounding(e, "s3_");
    set(e, "syn_on", 0);   /* and Synth 1 off: Synth 3, back on, keeps its new note */
    set(e, "s3_syn_on", 1);
    note(e, 1, 67, 100);
    rb_run(e, 0.1);
    set(e, "syn_on", 1);
    set(e, "s2_syn_on", 0);
    rb_run(e, 0.1);
    int s3c = sounding(e, "s3_");
    note(e, 0, 60, 0);
    note(e, 0, 64, 0);
    note(e, 0, 67, 0);
    rb_run(e, 1);
    snprintf(what, sizeof what, "ON / OFF: Synth 2 switched six times never cuts Synth 1's %d and Synth 3's %d held "
             "notes (%d cut); Synth 3 off ends only its own (%d / %d); Synth 1 off and on keeps Synth 3's (%d); all "
             "end on release (%d)", s1, s3, cut, s1b, s3b, s3c, rb_sounding(e));
    check(s1 == 2 && s3 == 2 && !cut && s1b == 2 && s3b == 0 && s3c == 1 && rb_sounding(e) == 0, what);
    E->destroy(e);
}

/* ---- floods of MIDI ---- */
static uint32_t rb_rnd(uint32_t *x) { *x ^= *x << 13; *x ^= *x >> 17; *x ^= *x << 5; return *x; }
static void rb_flood(void) {
    char what[256];
    uint32_t r = 12345;
    void *e = patch(SAW, 1, 100, 80, 50);
    setkv(e, "s2_syn_on=1,s3_syn_on=1,midi_ch=17,syn_ch=1,s2_syn_ch=2,s3_syn_ch=0");   /* Per Synth: 1, 2, Omni */
    static uint8_t down[16][128];   /* the keys this test holds down, per channel */
    memset(down, 0, sizeof down);
    long events = 0;
    clock_t t0 = clock();
    for (int blk = 0; blk < 40; blk++) {   /* 40 blocks of 2500 messages: notes, CCs, bend, pressure, program */
        for (int i = 0; i < 2500; i++) {
            uint32_t x = rb_rnd(&r);
            int ch = x & 15, d1 = (x >> 4) & 127, d2 = (x >> 11) & 127, kind = (x >> 18) % 10;
            uint8_t m[3];
            int len = 3;
            switch (kind) {
            case 0: case 1: case 2: m[0] = 0x90 | ch; down[ch][d1] = d2 > 0; break;   /* on (velocity 0: an off) */
            case 3: case 4: m[0] = 0x80 | ch; down[ch][d1] = 0; break;
            case 5:   /* a CC, never the NRPN / RPN ones (below, on their own) nor a learned or sustain one */
                if (d1 == 6 || d1 == 38 || (d1 >= 96 && d1 <= 101)) d1 = 7;
                m[0] = 0xB0 | ch;
                break;
            case 6: m[0] = 0xE0 | ch; break;
            case 7: m[0] = 0xD0 | ch; len = 2; break;
            case 8: m[0] = 0xA0 | ch; break;   /* poly pressure (not used: ignored) */
            default: m[0] = 0xC0 | ch; len = 2; break;
            }
            m[1] = (uint8_t)d1;
            m[2] = (uint8_t)d2;
            E->midi(e, m, len);
            events++;
        }
        for (int i = 0; i < 300; i++) {   /* garbage: system bytes, data bytes as status, short and empty messages */
            uint32_t x = rb_rnd(&r);
            uint8_t m[3] = {(uint8_t)(x & 255), (uint8_t)(x >> 8), (uint8_t)(x >> 16)};
            if ((m[0] & 0xF0) == 0x90 || (m[0] & 0xF0) == 0x80 || (m[0] & 0xF0) == 0xB0) m[0] = 0xF0 | (m[0] & 15);
            E->midi(e, m, (int)(x >> 24) % 4);
            events++;
        }
        rb_run(e, 128.0 / SR);
    }
    double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    int during = rb_sounding(e);
    for (int ch = 0; ch < 16; ch++) {   /* every key the test still holds, let go; the pedals up */
        for (int n = 0; n < 128; n++)
            if (down[ch][n]) { rb_msg(e, 0x80 | ch, n, 0); down[ch][n] = 0; }
        rb_msg(e, 0xB0 | ch, 64, 0);
        rb_msg(e, 0xB0 | ch, 66, 0);
    }
    rb_run(e, 4);
    int left = rb_sounding(e), heals = (int)get(e, "heals");
    snprintf(what, sizeof what, "MIDI flood: %ld messages in 40 blocks on every channel (%d voices sounding), %.0f ms "
             "here (ASan); every key let go: %d left sounding, %d heals", events, during, ms, left, heals);
    check(left == 0 && heals == 0 && during > 0, what);
    /* NRPN and data-entry floods with everything else: random params moved by the manual's numbers; then PANIC */
    long nrpns = 0;
    for (int blk = 0; blk < 20; blk++) {
        for (int i = 0; i < 1000; i++) {
            uint32_t x = rb_rnd(&r);
            int ch = x & 15, v = (x >> 4) & 127;
            static const uint8_t NR[] = {99, 98, 6, 38, 101, 100, 96, 97};
            if ((x >> 11) & 1) rb_msg(e, 0xB0 | ch, NR[(x >> 12) & 7], v);
            else rb_msg(e, ((x >> 12) & 1 ? 0x90 : 0x80) | ch, (x >> 13) & 127, v);
            nrpns++;
        }
        rb_run(e, 128.0 / SR);
    }
    rb_panic(e);
    rb_run(e, 0.05);
    left = rb_sounding(e);
    double after = rb_run(e, 0.05);
    snprintf(what, sizeof what, "NRPN flood: %ld messages (params moved at random by NRPN), then PANIC: %d sounding, "
             "peak %.5f", nrpns, left, after);
    check(left == 0 && after == 0, what);
    E->destroy(e);

    /* how much one message costs, worst case: 16 voices held in three Synths, notes against a full pool */
    e = patch(SAW, 1, 100, 80, 50);
    setkv(e, "s2_syn_on=1,s3_syn_on=1,voices=15,s2_voices=15,s3_voices=15");
    t0 = clock();
    for (int i = 0; i < 20000; i++) note(e, i & 1 ? 0 : 1, 30 + (i >> 1) % 64, 100);
    ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    snprintf(what, sizeof what, "MIDI cost: 20000 note messages into three Synths against a full pool, %.1f us each "
             "(ASan)", ms * 1000.0 / 20000);
    check(ms * 1000.0 / 20000 < 200, what);
    E->destroy(e);
}

/* ---- a note off releases what its note on started, whatever changed while the key was down ---- */
static void rb_routing(void) {
    char what[512];
    static const struct { const char *name, *setup, *change; int ch; } C[] = {
        {"global channel 1 -> 2", "midi_ch=1", "midi_ch=2", 0},
        {"global channel Omni -> 5", "midi_ch=0", "midi_ch=5", 0},
        {"Omni -> Per Synth (Synth 1 on 3)", "midi_ch=0,syn_ch=3", "midi_ch=17", 0},
        {"Per Synth, Synth 1's channel 1 -> 2", "midi_ch=17,syn_ch=1", "syn_ch=2", 0},
        {"Per Synth, Synth 1's channel Omni -> 9", "midi_ch=17,syn_ch=0", "syn_ch=9", 3},
        {"key range moved off the key", "", "key_lo=70", 0},
        {"key range high under the key", "", "key_hi=40", 0},
        {"transpose +7", "", "transpose=7", 0},
        {"octave +2", "", "octave_shift=4", 0},
        {"chord memory: the shape changed", "chord_on=1,chord_shape=1", "chord_shape=3,transpose=-5", 0},
        {"chord memory switched on", "", "chord_on=1", 0},
        {"arp: channel changed", "arp_on=1,midi_ch=1", "midi_ch=2", 0},
        {"mono: channel changed", "voices=0,midi_ch=1", "midi_ch=3", 0},
        {"unison: Per Synth channel changed", "unison=1,midi_ch=17,syn_ch=1", "syn_ch=4", 0},
    };
    int bad = 0;
    for (unsigned c = 0; c < sizeof C / sizeof C[0]; c++) {
        void *e = patch(SAW, 1, 100, 100, 60);
        E->set_param(e, "lfo_bpm", "120");
        setkv(e, C[c].setup);
        note(e, 1, 60, 100);   /* (as note(), but on channel ch) */
        rb_msg(e, 0x90 | C[c].ch, 62, 100);
        rb_run(e, 0.3);
        int on = rb_sounding(e);
        setkv(e, C[c].change);
        rb_run(e, 0.1);
        rb_msg(e, 0x80 | C[c].ch, 62, 0);
        note(e, 0, 60, 0);
        rb_run(e, 2);
        int left = rb_sounding(e);
        if (!on || left) {
            bad++;
            printf("     %s: %d sounding before, %d still sounding after the note offs\n", C[c].name, on, left);
        }
        E->destroy(e);
    }
    snprintf(what, sizeof what, "routing changed while keys are down: %d cases (channels, Per Synth, key range, "
             "transpose, octave, chord, arp, mono, unison), the note offs end every note (%d hung)",
             (int)(sizeof C / sizeof C[0]), bad);
    check(!bad, what);

    /* two Synths on two channels holding the same key: a channel change of one must not let the other's key go */
    void *e = patch(SAW, 1, 100, 100, 60);
    setkv(e, "s2_syn_on=1,s2_amp_a=0,s2_amp_s=100,s2_amp_r=60,midi_ch=17,syn_ch=1,s2_syn_ch=2");
    rb_msg(e, 0x90, 60, 100);       /* Synth 1's key 60 (channel 1) */
    rb_msg(e, 0x91, 60, 100);       /* Synth 2's key 60 (channel 2) */
    rb_run(e, 0.2);
    int a1 = sounding(e, ""), a2 = sounding(e, "s2_");
    set(e, "syn_ch", 2);            /* Synth 1 now listens on channel 2 as well */
    rb_msg(e, 0x81, 60, 0);         /* channel 2's key up: Synth 2's note ends, Synth 1's (from channel 1) stays */
    rb_run(e, 1);
    int b1 = sounding(e, ""), b2 = sounding(e, "s2_");
    rb_msg(e, 0x80, 60, 0);         /* channel 1's key up: Synth 1's ends too */
    rb_run(e, 1);
    snprintf(what, sizeof what, "routing: the same key on two channels, one Synth moved onto the other's channel: each "
             "key up ends only the note it started (held %d / %d; after channel 2's off %d / %d; after channel 1's %d)",
             a1, a2, b1, b2, rb_sounding(e));
    check(a1 == 1 && a2 == 1 && b1 == 1 && b2 == 0 && rb_sounding(e) == 0, what);
    /* All Notes Off on a channel a Synth has left still reaches the notes it started there */
    rb_msg(e, 0x92, 50, 100);       /* channel 3: no Synth (Synth 1 on 2, Synth 2 on 2) */
    set(e, "syn_ch", 3);
    rb_msg(e, 0x92, 52, 100);       /* Synth 1's, on channel 3 */
    set(e, "syn_ch", 1);
    rb_msg(e, 0xB2, 123, 0);
    rb_run(e, 1);
    snprintf(what, sizeof what, "routing: All Notes Off on channel 3 ends the note Synth 1 started there before its "
             "channel moved (%d left)", rb_sounding(e));
    check(rb_sounding(e) == 0, what);
    E->destroy(e);
}

/* ---- All Notes Off, All Sound Off, PANIC ---- */
static void rb_panic_checks(void) {
    char what[512];
    /* nothing playing: never a note, whatever the way the keys go (Euclid on Held Keys: a FREE one plays by itself) */
    static const char *const WAYS[] = {"", "voices=0", "chord_on=1", "arp_on=1", "arp_on=1,arp_latch=1", "hold=1",
                                       "voices=0,hold=1", "gen_euclid_on=1,gen_keys=1", "seq_on=1", "chord_learn=1"};
    int started = 0;
    for (unsigned w = 0; w < sizeof WAYS / sizeof WAYS[0]; w++) {
        void *e = patch(SAW, 1, 100, 100, 60);
        E->set_param(e, "lfo_bpm", "120");
        setkv(e, WAYS[w]);
        rb_run(e, 0.1);
        rb_panic(e);
        rb_msg(e, 0xB0, 123, 0);
        double p = rb_run(e, 0.5);
        if (p != 0 || rb_sounding(e)) { started++; printf("     PANIC with nothing playing (%s): peak %.4f\n", WAYS[w], p); }
        E->destroy(e);
    }
    snprintf(what, sizeof what, "PANIC with nothing playing never starts a note (%d ways of playing, %d did)",
             (int)(sizeof WAYS / sizeof WAYS[0]), started);
    check(!started, what);

    /* mono, LOW priority, chord memory: All Notes Off lets the chord's notes go without moving the voice onto another
     * (its release stays on the key it played) */
    void *e = patch(SAW, 1, 100, 100, 1000);
    setkv(e, "voices=0,note_priority=0,chord_on=1,chord_shape=1,flt_cut=100,o1_oct=2");
    note(e, 1, 57, 100);   /* A3, 220 Hz: low priority plays the chord's root */
    static float x[N];
    render_f(e, x, N);
    double held = peak_hz(x);
    rb_msg(e, 0xB0, 123, 0);
    render_f(e, x, N);
    double tail = peak_hz(x);
    note(e, 0, 57, 0);
    snprintf(what, sizeof what, "All Notes Off in mono (low priority, a chord): the release stays on the note played "
             "(%.1f Hz held, %.1f Hz in the release)", held, tail);
    check(fabs(held - 220) < 2 && fabs(tail - 220) < 2, what);
    E->destroy(e);

    /* everything at once: three Synths, Synth 1's arp, Synth 2's sequencer playing on all three (OUTPUT 1+2+3), Synth
     * 3 held, a pedal down; PANIC stops every note in every Synth and starts none */
    e = patch(SAW, 1, 100, 100, 300);
    E->set_param(e, "lfo_bpm", "20");   /* (slow: no step falls in the checks' few ms) */
    setkv(e, "arp_on=1,s2_syn_on=1,s3_syn_on=1,s2_amp_a=0,s2_amp_s=100,s2_amp_r=60,s3_amp_a=0,s3_amp_s=100,"
             "s3_amp_r=60,s2_seq_on=1,s2_seq_length=1,s2_seq_output=7,midi_ch=17,syn_ch=1,s2_syn_ch=2,s3_syn_ch=3");
    {
        char b[64];
        snprintf(b, sizeof b, "seqcmd2\nnote 0 0 48 100 100 1 1");
        E->set_param(e, "state", b);
    }
    rb_msg(e, 0x90, 60, 100);
    rb_msg(e, 0x90, 64, 100);
    rb_msg(e, 0x92, 72, 100);
    rb_msg(e, 0xB2, 64, 127);
    set(e, "s2_seq_play", 1);
    rb_run(e, 0.05);
    int b1 = sounding(e, ""), b2 = sounding(e, "s2_"), b3 = sounding(e, "s3_");
    log_notes(e, "gen_log");
    log_notes(e, "s2_gen_log");
    log_notes(e, "s3_gen_log");
    rb_panic(e);
    rb_run(e, 0.02);
    int a = rb_sounding(e), ons = log_notes(e, "gen_log") + log_notes(e, "s2_gen_log") + log_notes(e, "s3_gen_log");
    snprintf(what, sizeof what, "PANIC: three Synths (the arp, a sequence on all three, a held key and pedal: %d / %d "
             "/ %d sounding), 20 ms later %d sounding, %d notes started", b1, b2, b3, a, ons);
    check(b1 > 0 && b2 > 0 && b3 > 1 && a == 0 && ons == 0, what);
    /* the keys and pedal let go and the sequence stopped after the panic: nothing left hanging */
    rb_msg(e, 0x80, 60, 0);
    rb_msg(e, 0x80, 64, 0);
    rb_msg(e, 0x82, 72, 0);
    set(e, "s2_seq_play", 0);
    rb_run(e, 2);
    snprintf(what, sizeof what, "PANIC: then the keys up and the sequence stopped, nothing hangs (%d sounding)",
             rb_sounding(e));
    check(rb_sounding(e) == 0, what);
    E->destroy(e);

    /* All Notes Off on Synth 2's channel only, while Synth 1's sequence plays on Synth 2 (OUTPUT 1+2), two lanes
     * sharing its note: once the sequence stops nothing hangs */
    e = patch(SAW, 1, 100, 100, 60);
    E->set_param(e, "lfo_bpm", "240");
    setkv(e, "s2_syn_on=1,s2_amp_a=0,s2_amp_s=100,s2_amp_r=60,seq_on=1,seq_length=2,seq_output=4,midi_ch=17,"
             "syn_ch=1,s2_syn_ch=2");
    seqcmd(e, "note 0 0 48 100 100 1 1\nnote 1 0 48 100 50 1 0\nnote 0 1 48 100 50 1 0");
    set(e, "seq_play", 1);
    int hang = 0;
    for (int k = 0; k < 12; k++) {
        rb_run(e, 0.037 * (k % 5 + 1));
        rb_msg(e, 0xB1, k & 1 ? 120 : 123, 0);
    }
    set(e, "seq_play", 0);
    rb_run(e, 1.5);
    hang = rb_sounding(e);
    snprintf(what, sizeof what, "All Notes Off on Synth 2's channel while Synth 1's sequence plays on it: the "
             "sequence stopped, nothing hangs (%d sounding)", hang);
    check(hang == 0, what);
    E->destroy(e);
}

/* ---- MIDI learn reaches every param; NRPN synced envelope times ---- */
static int rb_transient(const char *k) {   /* params the engine sets back itself, or that act and are not values */
    const char *r = !strncmp(k, "s2_", 3) || !strncmp(k, "s3_", 3) ? k + 3 : k;
    return !strcmp(r, "seq_play") || !strcmp(r, "seq_rec") || !strcmp(r, "seq_step_mod") || !strcmp(r, "seq_all")
           || !strcmp(r, "life_reseed") || !strcmp(r, "chord_learn") || !strcmp(r, "rnd_go");
}
static void rb_learn(void) {
    char what[512], cfg[256], first[256] = "";
    int n = 0, bad = 0, told_bad = 0;
    void *e = E->create(NULL);
    for (int i = 0; i < NPARAMS; i++) {
        const param_t *p = &PARAMS[i];
        if (p->string_display || p->nopts == 1 || rb_transient(p->key)) continue;
        if (p->nopts == 0 && p->max <= p->min) continue;
        snprintf(cfg, sizeof cfg, "map=20:%s:7", p->key);
        midicfg(e, cfg);
        double lo = p->nopts ? 0 : p->min, hi = p->nopts ? p->nopts - 1 : p->max;
        char key[64];
        while (mpc_engine_changed(e, key, sizeof key) > 0) {}
        cc(e, 0, 20, 127);
        double vh = get(e, p->key);
        int told = 0;
        while (mpc_engine_changed(e, key, sizeof key) > 0) told |= !strcmp(key, p->key);
        cc(e, 0, 20, 0);
        double vl = get(e, p->key);
        int ok = fabs(vh - hi) < 1e-3 * (hi - lo + 1) && fabs(vl - lo) < 1e-3 * (hi - lo + 1);
        n++;
        if (!ok) {
            bad++;
            if (strlen(first) < 200)
                snprintf(first + strlen(first), sizeof first - strlen(first), " %s(%g,%g)", p->key, vh, vl);
        }
        told_bad += !told;
        if (n % 200 == 0) { E->destroy(e); e = E->create(NULL); }   /* (fresh now and then: a switch moved many) */
    }
    E->destroy(e);
    snprintf(what, sizeof what, "MIDI learn: a CC moves each of %d params (every Synth's) across its whole range, the "
             "host told each time (%d wrong%s; %d not told)", n, bad, first, told_bad);
    check(n > 1500 && !bad && !told_bad, what);

    /* the ones the request names, by a learned CC in the middle of the range */
    static const struct { const char *key; int v; double want; } L[] = {
        {"amp_r", 127, 100}, {"fenv_sync", 127, 1}, {"amp_sync", 127, 1}, {"lfo1_sync", 127, 1}, {"lfo3_sync", 127, 1},
        {"glide", 127, 1}, {"glide_time", 127, 100}, {"flt_slope", 85, 2}, {"svf_mode", 43, 1}, {"svf_mode", 127, 3},
        {"o2_sync", 127, 1}, {"o3_sync", 127, 1}, {"eg_times", 127, 1}, {"s3_flt_slope", 127, 3}};
    e = E->create(NULL);
    bad = 0;
    for (unsigned k = 0; k < sizeof L / sizeof L[0]; k++) {
        snprintf(cfg, sizeof cfg, "map=21:%s:7", L[k].key);
        midicfg(e, cfg);
        cc(e, 0, 21, L[k].v);
        if (fabs(get(e, L[k].key) - L[k].want) > 1e-3) {
            bad++;
            printf("     learned CC 21 = %d: %s is %g, want %g\n", L[k].v, L[k].key, get(e, L[k].key), L[k].want);
        }
    }
    snprintf(what, sizeof what, "MIDI learn: synced times, LFO sync, glide on and rate, ladder slope, SVF mode, osc "
             "sync, EG TIMES each land where the CC puts them (%d wrong)", bad);
    check(!bad, what);
    /* a synced amp release time set by its NRPN (725): 1/64 .. 64 beats; in "MIN, 13 Syncs" the exact one of the 13 */
    midicfg(e, "map=");
    setkv(e, "amp_sync=1,eg_times=1");
    E->set_param(e, "lfo_bpm", "120");
    bad = 0;
    for (int b = 0; b <= 12; b++) {
        nrpn_send(e, 0, 725, (b * 16384 + 630) / 13, 1);   /* the middle of the b-th of 13 bands */
        char d[64] = "";
        E->get_param(e, "amp_r_display", d, sizeof d);
        static const char *const NAME[13] = {"1/256", "1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar",
                                             "2 bars", "4 bars", "8 bars", "16 bars"};
        if (!strstr(d, NAME[b])) { bad++; printf("     NRPN 725 = %d/12: amp release shows \"%s\", want %s\n", b, d, NAME[b]); }
    }
    snprintf(what, sizeof what, "NRPN: a synced envelope time in MIN, 13 Syncs lands on each of the 13 values, 1/64 "
             "to 64 beats (%d wrong)", bad);
    check(!bad, what);
    /* the data MSB alone (a 7-bit controller) reaches the end of the 13: an envelope's 64 beats, an LFO's 1/64 */
    nrpn_send(e, 0, 725, 127 << 7, 0);
    char d1[64] = "", d2[64] = "";
    E->get_param(e, "amp_r_display", d1, sizeof d1);
    set(e, "lfo2_sync", 1);
    nrpn_send(e, 0, 514, 127 << 7, 0);
    E->get_param(e, "lfo2_rate_display", d2, sizeof d2);
    snprintf(what, sizeof what, "NRPN: the data MSB alone at 127 reaches the last synced value (amp release 64 beats: "
             "\"%s\"; LFO 2 RATE SYNC'D 1/64 beat: \"%s\")", d1, d2);
    check(strstr(d1, "16 bars") && strstr(d2, "1/256"), what);
    E->destroy(e);
}

/* ---- LOOP and LATCH edge cases ---- */
static void rb_loop_latch(void) {
    char what[512];
    void *e = patch(SAW, 1, 100, 100, 60);
    setkv(e, "amp_loop=1,amp_delay=0,amp_hold=0,amp_a=0,amp_d=0,amp_r=0,amp_s=0,eg_times=1,fenv_loop=1,fenv_a=0,"
             "fenv_d=0,fenv_r=0,lad_env=100,flt_cut=30,flt_res=90");
    note(e, 1, 60, 127);
    double p1 = rb_run(e, 2);
    note(e, 0, 60, 0);
    rb_run(e, 0.2);
    int h1 = (int)get(e, "heals"), l1 = rb_sounding(e);
    snprintf(what, sizeof what, "LOOP with every time at MIN and sustain 0 (amp and filter EGs, resonance 90): finite "
             "(peak %.3f, %d heals), and it ends on release (%d left)", p1, h1, l1);
    check(p1 > 0.001 && p1 < 1.0 && h1 == 0 && l1 == 0, what);
    E->destroy(e);

    e = patch(SAW, 1, 100, 100, 60);
    setkv(e, "amp_latch=1,amp_a=10,amp_d=40,amp_s=0,amp_r=50");
    note(e, 1, 60, 100);
    rb_run(e, 1);
    int held = rb_sounding(e);   /* LATCH, sustain 0: the decay ends it, key down or not */
    note(e, 0, 60, 0);
    rb_run(e, 0.1);
    snprintf(what, sizeof what, "LATCH with sustain 0: the decay frees the voice even with the key down (%d sounding), "
             "%d after", held, rb_sounding(e));
    check(held == 0 && rb_sounding(e) == 0, what);
    E->destroy(e);

    /* a retrigger storm while looping: the same key 200 times, poly then mono legato */
    int bad = 0;
    for (int mono = 0; mono < 2; mono++) {
        e = patch(SAW, 1, 100, 100, 60);
        setkv(e, mono ? "amp_loop=1,amp_a=20,amp_d=30,amp_s=30,amp_r=30,voices=0,amp_multi=0,glide=1"
                      : "amp_loop=1,amp_a=20,amp_d=30,amp_s=30,amp_r=30,fenv_loop=1,fenv_multi=0");
        note(e, 1, 48, 100);
        for (int k = 0; k < 200; k++) {
            note(e, 1, 60 + (k % 3) * (mono ? 2 : 0), 30 + k % 90);
            rb_run(e, 0.003 * (k % 7));
            if (k % 3 == 2) note(e, 0, 60 + (k % 3) * (mono ? 2 : 0), 0);
        }
        double p = rb_run(e, 0.2);
        note(e, 0, 48, 0);
        for (int k = 0; k < 3; k++) note(e, 0, 60 + k * 2, 0);
        rb_run(e, 1);
        int left = rb_sounding(e), h = (int)get(e, "heals");
        if (left || h || !(p > 0.001 && p < 1.0)) { bad++; printf("     retrigger storm (%s): peak %.3f, %d heals, %d left\n", mono ? "mono" : "poly", p, h, left); }
        E->destroy(e);
    }
    snprintf(what, sizeof what, "LOOP: a key retriggered 200 times while looping (poly, and mono legato with glide and "
             "MULTI TRIG off): finite, and it ends (%d wrong)", bad);
    check(!bad, what);

    /* LOOP switched on while sustaining, off while looping; LATCH on while held, then off */
    e = patch(SAW, 1, 100, 100, 60);
    setkv(e, "amp_a=10,amp_d=30,amp_s=50,amp_r=40");
    note(e, 1, 60, 100);
    rb_run(e, 0.5);
    set(e, "amp_loop", 1);
    double lp = rb_run(e, 0.5);
    int unlooped = 0;
    for (int k = 0; k < 8; k++) {   /* LOOP off at eight points of the loop: the key is still down, so it sustains */
        set(e, "amp_loop", 1);
        rb_run(e, 0.011 * (k + 1));
        set(e, "amp_loop", 0);
        rb_run(e, 0.5);
        unlooped += rb_sounding(e) == 1;
    }
    double sus = rb_run(e, 0.1);
    set(e, "amp_latch", 1);
    note(e, 0, 60, 0);
    rb_run(e, 0.5);
    int latched = rb_sounding(e);
    set(e, "amp_latch", 0);
    rb_run(e, 0.5);
    snprintf(what, sizeof what, "LOOP on while sustaining (peak %.3f), then off at 8 points of the loop with the key "
             "down: it goes on to its sustain each time (%d of 8, peak %.3f); LATCH on, the key up: still sounding (%d), "
             "LATCH off ends it (%d left, %d heals)", lp, unlooped, sus, latched, rb_sounding(e), (int)get(e, "heals"));
    check(lp > 0.01 && unlooped == 8 && sus > 0.05 && latched == 1 && rb_sounding(e) == 0 && get(e, "heals") == 0,
          what);
    E->destroy(e);
}

/* ---- the Init sound ---- */
static void rb_init(void) {
    char what[512];
    static const char *const OFF[] = {"o2_level", "o3_level", "noise_level", "ring_level", "mac_level", "ext_level",
                                      "o1_mix", "fm_amt", "pitch_env", "pitch_lfo", "lad_env", "lad_lfo", "svf_env",
                                      "svf_lfo", "flt_res", "flt_drive", "an_ladder", "pitch_var", "mfx_send1", "mfx_send2",
                                      "arp_on", "chord_on", "seq_on", "hold", "amp_loop", "amp_latch", "glide",
                                      "unison", "transpose"};
    void *e = E->create(NULL);
    int bad = 0;
    char list[256] = "";
    for (unsigned k = 0; k < sizeof OFF / sizeof OFF[0]; k++) {
        double v = get(e, OFF[k]);
        if (isnan(v)) continue;   /* (a key this build doesn't have) */
        if (v != 0) { bad++; snprintf(list + strlen(list), sizeof list - strlen(list), " %s=%g", OFF[k], v); }
    }
    double lvl = get(e, "o1_level"), cut = get(e, "flt_cut");
    static float x[N];
    double before = rb_run(e, 0.1);
    note(e, 1, 69, 100);
    render_f(e, x, N);
    double hz = peak_hz(x), first = fabs(x[0]);
    note(e, 0, 69, 0);
    rb_run(e, 3);
    snprintf(what, sizeof what, "Init: one plain saw (osc 1 at %g, the filter open at %g), nothing else mixed in or "
             "moving it (%d not at 0:%s); silent before the first note (%.4f), it starts at 0 (%.4f), A4 at %.1f Hz, "
             "and ends (%d sounding)", lvl, cut, bad, list, before, first, hz, rb_sounding(e));
    check(!bad && lvl == 100 && cut == 100 && before == 0 && first < 0.01 && fabs(hz - 440) < 1 && rb_sounding(e) == 0,
          what);
    E->destroy(e);
}

static void robust_checks(void) {
    rb_init();
    rb_syn_on();
    rb_routing();
    rb_panic_checks();
    rb_loop_latch();
    rb_learn();
    rb_flood();
    rb_env_ends();
}
