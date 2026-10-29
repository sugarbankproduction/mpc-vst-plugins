/* The instance's MIDI input settings at work (types: src/midi_in.h). Included by src/synths.h after the instance
 * (inst_t) and before midi(). The window sends them as "midicfg\n<key>=<value>;..." (any subset):
 *     rx_clock=0|1   RECEIVE MIDI CLOCK: off, the Synths ignore the host's tempo and song position and run at tempo=
 *     tempo=<bpm>    10-280
 *     sustain=<cc>, exp1=<cc>, exp2=<cc>   the pedals' CC numbers (0-119): the sustain pedal, the matrix sources
 *                    Exp 1 and Exp 2. A pedal moved to another number no longer answers to its old one.
 * Nothing here allocates or touches a file; midi() and render() only read the settings. */

static void synth_bpm(one_t *s, float bpm);

static void mi_init(midi_in_t *m) {
    m->rx_clock = 1;
    m->tempo = 120.0f;
    m->host_bpm = 0.0f;
    for (int p = 0; p < PEDALS; p++) m->pedal[p] = PEDAL_ROLE[p];
}

/* the tempo the Synths follow now: the host's (when received and known), else the SETTINGS page's own */
static void mi_clock(inst_t *in) {
    float bpm = in->mi.rx_clock ? in->mi.host_bpm : in->mi.tempo;
    if (bpm > 0.0f)
        for (int c = 0; c < NSYN; c++) synth_bpm(in->syn[c], bpm);
}

/* the host's tempo (set_param "lfo_bpm"): kept, and followed while the clock is received */
static void mi_host_bpm(inst_t *in, float bpm) {
    in->mi.host_bpm = bpm;
    if (in->mi.rx_clock)
        for (int c = 0; c < NSYN; c++) synth_bpm(in->syn[c], bpm);
}

/* one "key=value" of the command; 0: not one of these */
static int mi_set(inst_t *in, const char *key, const char *val) {
    midi_in_t *m = &in->mi;
    char *end;
    double v = strtod(val, &end);
    if (end == val || !isfinite(v)) return 0;
    static const char *const PK[PEDALS] = {"sustain", "exp1", "exp2"};
    if (!strcmp(key, "rx_clock")) { m->rx_clock = v >= 0.5; return 1; }
    if (!strcmp(key, "tempo")) { m->tempo = (float)(v < 10 ? 10 : v > 280 ? 280 : v); return 1; }
    for (int p = 0; p < PEDALS; p++)
        if (!strcmp(key, PK[p])) {
            if (v >= 0 && v <= 119) m->pedal[p] = (uint8_t)v;
            return 1;
        }
    return 0;
}

/* "midicfg\n..." from the window (set_param "state"); the text is worked on in the instance's own buffer */
static void mi_command(inst_t *in, const char *text) {
    char *buf = in->state_buf, *save = NULL;
    snprintf(buf, sizeof in->state_buf, "%s", text);
    for (char *kv = strtok_r(buf, ";", &save); kv; kv = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(kv, '=');
        if (!eq) continue;
        *eq = 0;
        mi_set(in, kv, eq + 1);
    }
    mi_clock(in);
}

/* the settings as text (get_param "midicfg", for the tests) */
static int mi_text(const inst_t *in, char *buf, int n) {
    const midi_in_t *m = &in->mi;
    return snprintf(buf, (size_t)n, "rx_clock=%d;tempo=%g;sustain=%d;exp1=%d;exp2=%d", m->rx_clock, m->tempo,
                    m->pedal[PEDAL_SUS], m->pedal[PEDAL_EXP1], m->pedal[PEDAL_EXP2]);
}

/* an incoming controller's number as the Synths read it: a pedal's CC becomes its function's (64, 11, 4); a default
 * pedal number no pedal uses now is dropped (-1); any other passes as it is */
static int mi_pedal(const midi_in_t *m, int cc) {
    for (int p = 0; p < PEDALS; p++)
        if (cc == m->pedal[p]) return PEDAL_ROLE[p];
    for (int p = 0; p < PEDALS; p++)
        if (cc == PEDAL_ROLE[p]) return -1;
    return cc;
}
