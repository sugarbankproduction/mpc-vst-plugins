/* MIDI input settings, MIDI learn, NRPN and microtuning (src/midi_in.h, src/midi_in_run.h, src/tuning.h): included by
 * offline_check.c after the multitimbral checks, so it uses that file's helpers (check, set, note, patch, render_f,
 * peak_hz, get, setkv, gen_events, gen_render_pos, gen_pick ...).
 *   settings  the "midicfg" command: defaults are exactly a fresh instance's samples; RECEIVE CLOCK off ignores the
 *             host's tempo and song position (the arp runs at the page's own tempo) and on again follows the host;
 *             the pedals' CC numbers (sustain on another CC holds notes, its old CC no longer does; EXP 1 / EXP 2) */

static void midicfg(void *e, const char *text) {
    char b[512];
    snprintf(b, sizeof b, "midicfg\n%s", text);
    E->set_param(e, "state", b);
}
static void cc(void *e, int ch, int n, int v) {
    uint8_t m[3] = {(uint8_t)(0xB0 | ch), (uint8_t)n, (uint8_t)v};
    E->midi(e, m, 3);
}
/* the samples between two arp notes (synced 1/16) while the host plays at host_bpm */
static double arp_gap(void *e, double host_bpm) {
    double ppq = 0;
    int n = gen_render_pos(e, 128, 0, &ppq, host_bpm);
    note(e, 1, 60, 100);
    note(e, 1, 64, 100);
    n = gen_render_pos(e, 44100, n, &ppq, host_bpm);
    long long t[8];
    int k = gen_pick(n, 1, NULL, t, NULL, 8);
    note(e, 0, 60, 0);
    note(e, 0, 64, 0);
    return k >= 3 ? (double)(t[2] - t[1]) : -1;
}

static void midi_settings(void) {
    char what[256], b[256];

    /* defaults: the same as a fresh instance, sample for sample */
    void *a = patch(SAW, 5, 100, 70, 50), *d = patch(SAW, 5, 100, 70, 50);
    midicfg(d, "rx_clock=1;tempo=120;sustain=64;exp1=11;exp2=4");
    E->get_param(d, "midicfg", b, sizeof b);
    check(!strcmp(b, "rx_clock=1;tempo=120;sustain=64;exp1=11;exp2=4"), "settings: the defaults read back");
    static float xa[8192], xd[8192];
    note(a, 1, 60, 100);
    note(d, 1, 60, 100);
    cc(a, 0, 64, 127); cc(d, 0, 64, 127);
    cc(a, 0, 11, 90); cc(d, 0, 11, 90);
    render_f(a, xa, 8192);
    render_f(d, xd, 8192);
    check(!memcmp(xa, xd, sizeof xa), "settings: the default MIDI settings play exactly a fresh instance's samples");
    E->destroy(a);
    E->destroy(d);

    /* RECEIVE MIDI CLOCK */
    void *e = patch(SAW, 1, 100, 80, 20);
    setkv(e, "arp_on=1,arp_sync=1,arp_div=4");
    E->set_param(e, "lfo_bpm", "60");
    double host = arp_gap(e, 60);
    midicfg(e, "rx_clock=0;tempo=120");
    double own = get(e, "bpm");
    E->set_param(e, "lfo_bpm", "90");   /* the host moves on: ignored */
    double gap_off = arp_gap(e, 90);
    double own2 = get(e, "bpm");
    midicfg(e, "rx_clock=1");
    double back = get(e, "bpm");
    snprintf(what, sizeof what, "RECEIVE CLOCK: on, 1/16 at the host's 60 BPM is %.0f samples apart; off, the own 120 "
             "BPM (%.0f, the host's 90 ignored: %.0f samples); on again the host's 90 (%.0f)", host, own, gap_off, back);
    check(fabs(host - 11025) <= 1 && own == 120 && own2 == 120 && fabs(gap_off - 5512.5) <= 1 && back == 90, what);
    midicfg(e, "tempo=500");
    E->get_param(e, "midicfg", b, sizeof b);
    check(strstr(b, "tempo=280") != NULL, "RECEIVE CLOCK: the own tempo stays within 10-280 BPM");
    E->destroy(e);

    /* the pedals' numbers */
    e = patch(SAW, 1, 100, 80, 20);
    midicfg(e, "sustain=69");
    cc(e, 0, 69, 127);
    note(e, 1, 60, 100);
    rms_of(e, 2000);
    note(e, 0, 60, 0);
    rms_of(e, 8000);
    double held = rms_of(e, 2000);
    cc(e, 0, 69, 0);
    rms_of(e, 8000);
    double gone = rms_of(e, 2000);
    cc(e, 0, 64, 127);   /* the old number: nothing */
    note(e, 1, 60, 100);
    rms_of(e, 2000);
    note(e, 0, 60, 0);
    rms_of(e, 8000);
    double old = rms_of(e, 2000);
    snprintf(what, sizeof what, "pedals: sustain on CC 69 holds the note (rms %.4f) and lets go (%.5f); CC 64 doesn't "
             "any more (%.5f)", held, gone, old);
    check(held > 0.01 && gone < 1e-4 && old < 1e-4, what);
    E->destroy(e);

    /* EXP 1 / EXP 2 moved: the matrix's Exp sources follow the new numbers (Exp 1 -> amp level) */
    e = patch(SAW, 1, 100, 100, 20);
    int exp1 = -1;
    for (int k = 0; k < PARAMS[pindex("mod1_src")].nopts; k++)
        if (!strncmp(PARAMS[pindex("mod1_src")].opts[k], "Exp 1", 5)) exp1 = k;   /* ("Exp 1 (CC 11)": its default) */
    snprintf(b, sizeof b, "mod1_src=%d,mod1_dst=0,mod1_via=0,mod1_amt=0", exp1);
    setkv(e, b);
    int amp = -1;
    for (int k = 0; k < PARAMS[pindex("mod1_dst")].nopts; k++)
        if (!strcmp(PARAMS[pindex("mod1_dst")].opts[k], "Amp Level")) amp = k;
    set(e, "mod1_dst", amp);
    set(e, "mod1_amt", -100);
    midicfg(e, "exp1=20;exp2=21");
    note(e, 1, 60, 100);
    rms_of(e, 4000);
    double open = rms_of(e, 4000);
    cc(e, 0, 11, 127);   /* the old EXP 1 number: nothing */
    rms_of(e, 4000);
    double still = rms_of(e, 4000);
    cc(e, 0, 20, 127);   /* the new one: Exp 1 full, the level down to 0 */
    rms_of(e, 4000);
    double shut = rms_of(e, 4000);
    snprintf(what, sizeof what, "pedals: EXP 1 on CC 20 moves the matrix's Exp 1 (rms %.4f -> %.5f), CC 11 no longer "
             "does (%.4f)", open, shut, still);
    check(exp1 >= 0 && amp >= 0 && open > 0.01 && fabs(still - open) < open * 0.05 && shut < open * 0.05, what);
    E->destroy(e);
}
/* NRPN after the manual's table, and the learned CC map */
static void nrpn_send(void *e, int ch, int num, int val, int lsb) {
    cc(e, ch, 99, num >> 7);
    cc(e, ch, 98, num & 127);
    cc(e, ch, 6, val >> 7);
    if (lsb) cc(e, ch, 38, val & 127);
}
static void midi_learn_nrpn(void) {
    char what[256], b[64];
    void *e = E->create(NULL);
    int found = 0, missing = -1;
    E->get_param(e, "nrpn_table", b, sizeof b);
    sscanf(b, "%d %d", &found, &missing);
    snprintf(what, sizeof what, "NRPN: every number of the manual's table finds its param in each Synth (%d keys, %d "
             "missing)", found, missing);
    check(found > 300 && missing == 0, what);
    /* continuous, 14 bits: Synth 1 ladder cutoff 1150, Synth 2's 4990, Synth 3's 8830 */
    nrpn_send(e, 0, 1150, 8192, 1);
    nrpn_send(e, 0, 1150 + 3840, 16383, 1);
    nrpn_send(e, 0, 1150 + 7680, 0, 1);
    double c1 = get(e, "flt_cut"), c2 = get(e, "s2_flt_cut"), c3 = get(e, "s3_flt_cut");
    snprintf(what, sizeof what, "NRPN: ladder cutoff 1150 / 4990 / 8830 set Synth 1, 2, 3 (%.3f, %.3f, %.3f)", c1, c2, c3);
    check(fabs(c1 - 100.0 * 8192 / 16383) < 1e-3 && c2 == 100 && c3 == 0, what);
    nrpn_send(e, 0, 1150, 0x55 << 7, 0);   /* the MSB alone: coarse */
    check(fabs(get(e, "flt_cut") - 100.0 * (0x55 << 7) / 16383) < 1e-3, "NRPN: the data MSB alone sets the value");
    /* options in the manual's bands */
    static const struct { int num; const char *key; int val; double want; } O[] = {
        {451, "lfo1_wave", 4096, 2}, {451, "lfo1_wave", 12288, 5}, {152, "o1_oct", 3276, 0}, {152, "o1_oct", 3277, 1},
        {152, "o1_oct", 16383, 4}, {958, "svf_mode", 8192, 1}, {958, "svf_mode", 4096, 2}, {1102, "flt_route", 0, 1},
        {1159, "flt_slope", 0, 3}, {1159, "flt_slope", 16383, 0}, {65, "fm_route", 10922, 2}, {217, "o2_sync", 8192, 1},
        {662, "fenv_latch", 8192, 1}, {1209, "unison", 8192, 1}, {800, "noise_color_pair", 5461, 1}};
    int bad = 0;
    for (unsigned k = 0; k < sizeof O / sizeof O[0]; k++) {
        nrpn_send(e, 0, O[k].num, O[k].val, 1);
        if (get(e, O[k].key) != O[k].want) { bad++; printf("     NRPN %d = %d: %s is %g, want %g\n", O[k].num, O[k].val, O[k].key, get(e, O[k].key), O[k].want); }
    }
    snprintf(what, sizeof what, "NRPN: %d option values land in the manual's bands (%d wrong)",
             (int)(sizeof O / sizeof O[0]), bad);
    check(!bad, what);
    /* synced and free rates: each only while the LFO / envelope is that way */
    set(e, "lfo2_sync", 0);
    nrpn_send(e, 0, 514, 16383, 1);   /* RATE SYNC'D while free: ignored */
    double free_rate = get(e, "lfo2_rate");
    nrpn_send(e, 0, 513, 0, 1);
    double free0 = get(e, "lfo2_rate");
    set(e, "lfo2_sync", 1);
    nrpn_send(e, 0, 514, 8192, 1);   /* "1" beat: LFO_DIV[6] */
    double sync1 = get(e, "lfo2_rate");
    snprintf(what, sizeof what, "NRPN: LFO 2 RATE SYNC'D only while synced (free %.1f -> %.1f; synced 1 beat: knob %.1f)",
             free_rate, free0, sync1);
    check(free0 == 0 && fabs(sync1 - 50) < 1e-3 && free_rate != 0, what);
    set(e, "amp_sync", 1);
    nrpn_send(e, 0, 722, 10922, 1);   /* attack synced 4 beats: SYNC_DIV "1 bar" (index 2) -> knob 80 */
    snprintf(what, sizeof what, "NRPN: an envelope's synced time picks the nearest division (4 beats: knob %.1f)",
             get(e, "amp_a"));
    check(fabs(get(e, "amp_a") - 80) < 1e-3, what);
    /* mono, master volume, an RPN deselects */
    nrpn_send(e, 0, 1203, 16383, 1);
    double mono = get(e, "voices");
    nrpn_send(e, 0, 1203, 0, 1);
    double poly = get(e, "voices");
    nrpn_send(e, 0, 1, 0, 1);
    double ml = get(e, "master_level");
    cc(e, 0, 101, 0);
    cc(e, 0, 100, 0);
    cc(e, 0, 6, 127);   /* an RPN's data: not the NRPN's */
    snprintf(what, sizeof what, "NRPN: MONO on / off (voices %g, %g), MASTER VOLUME 0 (%g), an RPN deselects (%g)",
             mono, poly, ml, get(e, "master_level"));
    const param_t *pv = &PARAMS[pindex("voices")];
    check(mono == 0 && poly == round(pv->def * (pv->nopts - 1)) && ml == 0 && get(e, "master_level") == 0, what);
    /* the host hears about it */
    int told = 0;
    char key[64];
    while (mpc_engine_changed(e, key, sizeof key) > 0) told += !strcmp(key, "s2_flt_cut") || !strcmp(key, "master_level");
    check(told == 2, "NRPN: the host is told of the params it moved (mpc_engine_changed)");
    E->destroy(e);

    /* the learned CC map */
    e = E->create(NULL);
    midicfg(e, "map=74:flt_cut:7,7:s2_svf_cut:14,20:lfo1_wave:7,64:volume:7,1:pan:7,39:pan:7,nonsense:x:7");
    E->get_param(e, "midimap", b, sizeof b);
    snprintf(what, sizeof what, "learn: the map is taken; reserved CCs, a 14-bit pair's LSB and nonsense refused (\"%s\")", b);
    check(!strcmp(b, "7:s2_svf_cut:14,20:lfo1_wave:7,74:flt_cut:7"), what);
    cc(e, 0, 74, 127);
    double l1 = get(e, "flt_cut");
    cc(e, 0, 74, 0);
    double l0 = get(e, "flt_cut");
    cc(e, 3, 7, 64);
    cc(e, 3, 39, 0);
    double m14 = get(e, "s2_svf_cut");
    cc(e, 3, 39, 127);
    double m14b = get(e, "s2_svf_cut");
    cc(e, 0, 20, 127);
    snprintf(what, sizeof what, "learn: CC 74 moves Synth 1's cutoff (%.1f, %.1f); CC 7 + 39 14-bit Synth 2's SVF "
             "(%.4f, %.4f); CC 20 an option (%g)", l1, l0, m14, m14b, get(e, "lfo1_wave"));
    check(l1 == 100 && l0 == 0 && fabs(m14 - 100.0 * 8192 / 16383) < 1e-3 && fabs(m14b - 100.0 * (8192 + 127) / 16383) < 1e-3
              && get(e, "lfo1_wave") == PARAMS[pindex("lfo1_wave")].nopts - 1,
          what);
    E->destroy(e);
}
