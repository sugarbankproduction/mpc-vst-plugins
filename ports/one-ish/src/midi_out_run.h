/* (2026-10-30) MIDI OUT: the functions (src/midi_out.h has the overview). Included by engine.c after hold_on(); part of
 * engine.c. Every function here is a few comparisons when MIDI OUT is off. */
enum { MO_KEYS, MO_GEN, MO_BOTH };   /* "mout_send" options */

static int mo_live(const one_t *s) { return s->mob && s->mob->enabled && s->val[P_MOUT_ON] > 0.5f; }
static int mo_wants(const one_t *s, int keys) {
    if (!mo_live(s)) return 0;
    int w = (int)s->val[P_MOUT_SEND];
    return w == MO_BOTH || w == (keys ? MO_KEYS : MO_GEN);
}
static int mo_chan(const one_t *s) { return (int)clampf(s->val[P_MOUT_CH], 0.0f, 15.0f); }

/* one event, at this Synth's current sample; a note-on only while there is room for every note-off after it */
static int mo_push(one_t *s, int st, int d1, int d2) {
    mo_buf_t *b = s->mob;
    if (b->n >= ((st & 0xf0) == 0x90 ? MO_ON_ROOM : MO_EVMAX)) { b->dropped++; return 0; }
    int64_t at = s->now - b->now0;
    b->ev[b->n].at = (int32_t)(at < 0 ? 0 : at > 1 << 20 ? 1 << 20 : at);
    b->ev[b->n].m[0] = (uint8_t)st;
    b->ev[b->n].m[1] = (uint8_t)(d1 & 127);
    b->ev[b->n].m[2] = (uint8_t)(d2 & 127);
    b->n++;
    return 1;
}

/* the note source note n of table t sent ends (now) */
static void mo_end(one_t *s, mo_tab_t *t, int n) {
    if (!t->note[n]) return;
    int note = t->note[n] - 1, ch = t->ch[n];
    uint8_t *c = &s->mob->cnt[ch][note];
    t->note[n] = 0;
    t->pend[n] = 0;
    if (*c && --*c) return;   /* another source still holds it: its own end sends the note-off */
    mo_push(s, 0x80 | ch, note, 0);
}
/* source note n starts (a key, or out_on's note): what it had sent ends first; then it goes out if this Synth sends it */
static void mo_start(one_t *s, mo_tab_t *t, int n, int vel, int keys) {
    mo_end(s, t, n);
    if (!mo_wants(s, keys)) return;
    int note = n + (int)s->shift, ch = mo_chan(s);
    if (note < 0 || note > 127) return;
    uint8_t *c = &s->mob->cnt[ch][note];
    if (*c) mo_push(s, 0x80 | ch, note, 0);   /* held by another source: retriggered (off, then on) */
    if (!mo_push(s, 0x90 | ch, note, vel < 1 ? 1 : vel)) return;
    if (*c < 255) ++*c;
    t->note[n] = (uint8_t)(note + 1);
    t->ch[n] = (uint8_t)ch;
    t->keys[n] = (uint8_t)keys;
    t->pend[n] = 0;
}
/* a key's or a chord key's note let go: held on by the sustain pedal or HOLD as the voices are, else it ends now */
static void mo_up(one_t *s, mo_tab_t *t, int n, int held) {
    if (!t->note[n]) return;
    if (held && (s->sustain || hold_on(s))) t->pend[n] = 1;
    else mo_end(s, t, n);
}
static void mo_flush_tab(one_t *s, mo_tab_t *t) {
    for (int n = 0; n < 128; n++) mo_end(s, t, n);
}

/* the keys */
static void mo_key_on(one_t *s, int note, int vel) { mo_start(s, &s->mo.key, note, vel, 1); }
static void mo_key_off(one_t *s, int note) { mo_up(s, &s->mo.key, note, 1); }
/* out_on() / out_off(): generated notes (and chord memory's, keyctx); `bypass`: a step's end, which nothing holds */
static void mo_gen_on(one_t *s, int note, int vel) { mo_start(s, &s->mo.gen, note, vel, s->mo.keyctx); }
static void mo_gen_off(one_t *s, int note, int bypass) { mo_up(s, &s->mo.gen, note, !bypass); }
/* the sequencer's tie moved a voice from one note to another: the new note first, then the old one ends (legato) */
static void mo_gen_move(one_t *s, int from, int to, int vel) {
    int keys = s->mo.gen.note[from] ? s->mo.gen.keys[from] : 0;
    mo_tab_t *t = &s->mo.gen;
    uint8_t had = t->note[from], ch = t->ch[from];
    t->note[from] = 0;   /* (kept aside, so the new note's start does not end it first) */
    mo_start(s, t, to, vel, keys);
    if (had) { t->note[from] = had; t->ch[from] = ch; mo_end(s, t, from); }
}
/* the pedal or HOLD let go (release_held): the notes they kept end */
static void mo_release(one_t *s, int keep_latched) {
    if (s->sustain || (keep_latched && hold_on(s))) return;
    for (int n = 0; n < 128; n++) {
        if (s->mo.key.pend[n]) mo_end(s, &s->mo.key, n);
        if (s->mo.gen.pend[n]) mo_end(s, &s->mo.gen, n);
    }
}
/* everything this Synth sent ends (all_off: All Notes Off, All Sound Off, a change of way, the Synth off) */
static void mo_flush(one_t *s) {
    mo_flush_tab(s, &s->mo.key);
    mo_flush_tab(s, &s->mo.gen);
}
/* MIDI OUT's settings changed (or a whole state came): what it no longer sends, or sent on another channel, ends now */
static void mo_settings(one_t *s) {
    int ch = mo_chan(s);
    mo_tab_t *tabs[2] = {&s->mo.key, &s->mo.gen};
    for (int k = 0; k < 2; k++)
        for (int n = 0; n < 128; n++)
            if (tabs[k]->note[n] && (!mo_wants(s, tabs[k]->keys[n]) || tabs[k]->ch[n] != ch)) mo_end(s, tabs[k], n);
}
