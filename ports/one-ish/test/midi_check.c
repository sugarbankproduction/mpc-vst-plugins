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
