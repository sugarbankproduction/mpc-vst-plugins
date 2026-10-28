/* ONE-ish's step sequencer: the sequence itself (after the Moog One manual's SEQUENCER chapter, p82-95), how it is
 * written down, and the factory sequences. engine.c (with src/stepseq_run.h) plays it; the desktop editor
 * (desktop/seq_page.c) draws and edits it through the same code. Fixed-size, allocation-free, no I/O.
 *
 * A sequence is up to 64 steps (shown as four pages of 16). Each step has up to SQ_NL NOTE LANES, each lane one note
 * (a lane is monophonic, so a chord takes several lanes): its note number, velocity (1-127), gate (1-100% of the step),
 * ratchet (1-4 re-articulations inside the step) and a tie (the note carries on into the next step without a new
 * attack). A step can be a REST (its notes are kept but don't play). Then up to SQ_ML MODULATION LANES, each with one
 * destination (the mod matrix's list), a controller (a matrix source, or none) with an amount, and a bipolar offset
 * (-100..+100%) per step, or no offset on that step.
 *
 * Written down (in the plugin's saved state as "seq=", in user patches and in .oneseq files) as base64url text of a
 * small binary form: only the notes there are, each storing only what differs from the one before, so an ordinary
 * sequence takes a few hundred characters (sq_encode). sq_decode checks everything and refuses damaged text. */
#pragma once
#include <stdint.h>
#include <string.h>
#include "modmatrix.h"   /* MDF_WORDS (the lanes' effect destinations) */

#define SQ_STEPS 64
#define SQ_PAGE 16
#define SQ_NL 8              /* note lanes */
#define SQ_ML 8              /* modulation lanes */
#define SQ_EMPTY 255         /* sq_note_t.note: no note in this lane on this step */
#define SQ_NO_OFF (-128)     /* sq_mlane_t.off: no offset on this step */
#define SQ_TIE 4             /* sq_note_t.flags: tied into the next step; bits 0-1: ratchet - 1 */
#define SQ_RATCH 3
#define SQ_TEXT_MAX 5200     /* the longest sq_encode text (a full sequence, every field different) and its NUL */
#define SQ_VERSION 1

typedef struct { uint8_t note, vel, gate, flags; } sq_note_t;
typedef struct {
    uint8_t dest, ctl, amt;  /* the mod matrix's destination (0: lane unused) and source (0: no controller); 0..100% */
    int8_t off[SQ_STEPS];    /* -100..100 (% of the destination's matrix range), or SQ_NO_OFF */
} sq_mlane_t;
typedef struct {
    sq_note_t n[SQ_STEPS][SQ_NL];
    uint64_t rest;           /* bit s: step s is a rest */
    sq_mlane_t m[SQ_ML];
} sq_seq_t;

static inline int sq_ratchet(const sq_note_t *n) { return (n->flags & SQ_RATCH) + 1; }
static inline int sq_has(const sq_note_t *n) { return n->note != SQ_EMPTY; }

static inline void sq_clear_mlane(sq_mlane_t *m) {
    m->dest = m->ctl = m->amt = 0;
    memset(m->off, SQ_NO_OFF, sizeof m->off);
}
static inline void sq_clear(sq_seq_t *q) {
    memset(q->n, SQ_EMPTY, sizeof q->n);   /* (every byte: only .note is read while a lane is empty) */
    q->rest = 0;
    for (int l = 0; l < SQ_ML; l++) sq_clear_mlane(&q->m[l]);
}
static inline void sq_clear_step(sq_seq_t *q, int s) {
    memset(q->n[s], SQ_EMPTY, sizeof q->n[s]);
    q->rest &= ~(1ull << s);
    for (int l = 0; l < SQ_ML; l++) q->m[l].off[s] = SQ_NO_OFF;
}
static inline void sq_set_note(sq_seq_t *q, int s, int l, int note, int vel, int gate, int ratchet, int tie) {
    sq_note_t *n = &q->n[s][l];
    n->note = (uint8_t)note;
    n->vel = (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel);
    n->gate = (uint8_t)(gate < 1 ? 1 : gate > 100 ? 100 : gate);
    n->flags = (uint8_t)(((ratchet < 1 ? 1 : ratchet > 4 ? 4 : ratchet) - 1) | (tie ? SQ_TIE : 0));
}
/* the first free lane of step s, or -1 */
static inline int sq_free_lane(const sq_seq_t *q, int s) {
    for (int l = 0; l < SQ_NL; l++)
        if (!sq_has(&q->n[s][l])) return l;
    return -1;
}
static inline int sq_step_empty(const sq_seq_t *q, int s) {
    for (int l = 0; l < SQ_NL; l++)
        if (sq_has(&q->n[s][l])) return 0;
    return 1;
}
/* the number of steps up to the last one holding anything (a note, a rest or an offset): 0 for an empty sequence */
static inline int sq_used_steps(const sq_seq_t *q) {
    for (int s = SQ_STEPS - 1; s >= 0; s--) {
        if (q->rest >> s & 1) return s + 1;
        for (int l = 0; l < SQ_NL; l++)
            if (sq_has(&q->n[s][l])) return s + 1;
        for (int l = 0; l < SQ_ML; l++)
            if (q->m[l].dest && q->m[l].off[s] != SQ_NO_OFF) return s + 1;
    }
    return 0;
}
/* the lowest note of the first step that has one (TRANSPOSE's Start Key), or -1 */
static inline int sq_first_note(const sq_seq_t *q, int len) {
    for (int s = 0; s < len && s < SQ_STEPS; s++) {
        int lo = -1;
        for (int l = 0; l < SQ_NL; l++)
            if (sq_has(&q->n[s][l]) && (lo < 0 || q->n[s][l].note < lo)) lo = q->n[s][l].note;
        if (lo >= 0) return lo;
    }
    return -1;
}
/* DUPLICATE X-Y: steps x..y (0-based, inclusive) copied after the first `len` steps, as far as step 64 goes; returns
 * the new length */
static inline int sq_duplicate(sq_seq_t *q, int x, int y, int len) {
    if (x < 0) x = 0;
    if (y >= SQ_STEPS) y = SQ_STEPS - 1;
    if (len < 1) len = 1;
    if (len > SQ_STEPS) len = SQ_STEPS;
    int to = len;
    for (int s = x; s <= y && to < SQ_STEPS; s++, to++) {
        memcpy(q->n[to], q->n[s], sizeof q->n[s]);
        q->rest = (q->rest & ~(1ull << to)) | ((q->rest >> s & 1) << to);
        for (int l = 0; l < SQ_ML; l++) q->m[l].off[to] = q->m[l].off[s];
    }
    return to;
}

/* ---- the text form ---- */
static const char SQ_B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
static inline int sq_b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '-' ? 62 : c == '_' ? 63 : -1;
}

/* the binary form: version; a rest bitmap (8 bytes, little end first); the note count (2 bytes) and the notes in
 * step then lane order, each a header byte (lane in bits 0-2; bit 3: a step byte follows, else the last note's step;
 * bit 4 velocity, bit 5 gate, bit 6 ratchet/tie byte follow, else the last note's) and its note number; then the
 * modulation lanes in use: their count, and each its lane, destination, controller, amount, an 8-byte bitmap of the
 * steps with an offset, and those offsets */
static inline int sq_pack(const sq_seq_t *q, uint8_t *b) {
    int n = 0, cnt = 0;
    b[n++] = SQ_VERSION;
    for (int k = 0; k < 8; k++) b[n++] = (uint8_t)(q->rest >> (8 * k));
    int at = n;
    n += 2;
    int ps = 0, pv = 100, pg = 50, pf = 0;
    for (int s = 0; s < SQ_STEPS; s++)
        for (int l = 0; l < SQ_NL; l++) {
            const sq_note_t *x = &q->n[s][l];
            if (!sq_has(x)) continue;
            uint8_t h = (uint8_t)l;
            int hs = s != ps, hv = x->vel != pv, hg = x->gate != pg, hf = x->flags != pf;
            h |= (uint8_t)(hs << 3 | hv << 4 | hg << 5 | hf << 6);
            b[n++] = h;
            if (hs) b[n++] = (uint8_t)s;
            b[n++] = x->note;
            if (hv) b[n++] = x->vel;
            if (hg) b[n++] = x->gate;
            if (hf) b[n++] = x->flags;
            ps = s; pv = x->vel; pg = x->gate; pf = x->flags;
            cnt++;
        }
    b[at] = (uint8_t)cnt;
    b[at + 1] = (uint8_t)(cnt >> 8);
    int mc = n, nm = 0;
    b[n++] = 0;
    for (int l = 0; l < SQ_ML; l++) {
        const sq_mlane_t *m = &q->m[l];
        if (!m->dest) continue;
        b[n++] = (uint8_t)l; b[n++] = m->dest; b[n++] = m->ctl; b[n++] = m->amt;
        uint8_t *bits = b + n;
        memset(bits, 0, 8);
        n += 8;
        for (int s = 0; s < SQ_STEPS; s++)
            if (m->off[s] != SQ_NO_OFF) { bits[s >> 3] |= (uint8_t)(1 << (s & 7)); b[n++] = (uint8_t)m->off[s]; }
        nm++;
    }
    b[mc] = (uint8_t)nm;
    return n;
}

/* the sequence as text into out (SQ_TEXT_MAX is always enough); returns its length */
static inline int sq_encode(const sq_seq_t *q, char *out, int cap) {
    uint8_t b[4096];
    int n = sq_pack(q, b), o = 0;
    for (int i = 0; i < n && o + 5 < cap; i += 3) {
        uint32_t v = (uint32_t)b[i] << 16 | (i + 1 < n ? (uint32_t)b[i + 1] << 8 : 0) | (i + 2 < n ? b[i + 2] : 0);
        out[o++] = SQ_B64[v >> 18 & 63];
        out[o++] = SQ_B64[v >> 12 & 63];
        if (i + 1 < n) out[o++] = SQ_B64[v >> 6 & 63];
        if (i + 2 < n) out[o++] = SQ_B64[v & 63];
    }
    out[o] = 0;
    return o;
}

/* text (up to its end, a ';', a newline or a space) back into a sequence: 1, or 0 (q untouched) when it is damaged.
 * maxdest / maxsrc: the destination and source counts, so an unknown one is refused rather than misread. */
static inline int sq_decode(const char *t, sq_seq_t *q, int maxdest, int maxsrc) {
    uint8_t bb[4096];
    int n = 0, acc = 0, bits = 0;
    for (; *t && *t != ';' && *t != '\n' && *t != '\r' && *t != ' '; t++) {
        int v = sq_b64_val(*t);
        if (v < 0 || n >= (int)sizeof bb) return 0;
        acc = acc << 6 | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; bb[n++] = (uint8_t)(acc >> bits); acc &= (1 << bits) - 1; }
    }
    sq_seq_t tmp;
    sq_clear(&tmp);
    int i = 0;
#define SQ_NEED(k) do { if (i + (k) > n) return 0; } while (0)
    SQ_NEED(11);
    if (bb[i++] != SQ_VERSION) return 0;
    for (int k = 0; k < 8; k++) tmp.rest |= (uint64_t)bb[i++] << (8 * k);
    int cnt = bb[i] | bb[i + 1] << 8, ps = 0, pv = 100, pg = 50, pf = 0, last = -1;
    i += 2;
    if (cnt > SQ_STEPS * SQ_NL) return 0;
    for (int c = 0; c < cnt; c++) {
        SQ_NEED(2);
        int h = bb[i++], l = h & 7, s = ps;
        if (h >> 7) return 0;
        if (h & 8) { s = bb[i++]; SQ_NEED(1); }
        int note = bb[i++];
        if (h & 16) { SQ_NEED(1); pv = bb[i++]; }
        if (h & 32) { SQ_NEED(1); pg = bb[i++]; }
        if (h & 64) { SQ_NEED(1); pf = bb[i++]; }
        if (s >= SQ_STEPS || note > 127 || pv < 1 || pv > 127 || pg < 1 || pg > 100 || pf & ~(SQ_TIE | SQ_RATCH)) return 0;
        if (s * SQ_NL + l <= last) return 0;   /* in order, each lane once */
        last = s * SQ_NL + l;
        tmp.n[s][l] = (sq_note_t){(uint8_t)note, (uint8_t)pv, (uint8_t)pg, (uint8_t)pf};
        ps = s;
    }
    SQ_NEED(1);
    int nm = bb[i++];
    if (nm > SQ_ML) return 0;
    for (int k = 0; k < nm; k++) {
        SQ_NEED(12);
        int l = bb[i++], dest = bb[i++], ctl = bb[i++], amt = bb[i++];
        if (l >= SQ_ML || dest < 1 || dest >= maxdest || ctl >= maxsrc || amt > 100 || tmp.m[l].dest) return 0;
        sq_mlane_t *m = &tmp.m[l];
        m->dest = (uint8_t)dest; m->ctl = (uint8_t)ctl; m->amt = (uint8_t)amt;
        const uint8_t *set = bb + i;
        i += 8;
        for (int s = 0; s < SQ_STEPS; s++)
            if (set[s >> 3] >> (s & 7) & 1) {
                SQ_NEED(1);
                int v = (int8_t)bb[i++];
                if (v < -100 || v > 100) return 0;
                m->off[s] = (int8_t)v;
            }
    }
#undef SQ_NEED
    if (i != n) return 0;   /* nothing left over */
    *q = tmp;
    return 1;
}

/* ---- note names: middle C (60) is C4 ---- */
static inline void sq_note_name(int note, char *out, int n) {
    static const char *const NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (note < 0 || note > 127) { if (n > 0) out[0] = 0; return; }
    int oct = note / 12 - 1, k = 0;
    const char *nm = NAMES[note % 12];
    while (*nm && k < n - 1) out[k++] = *nm++;
    if (oct < 0 && k < n - 1) out[k++] = '-';
    if (k < n - 1) out[k++] = (char)('0' + (oct < 0 ? -oct : oct));
    out[k < n ? k : n - 1] = 0;
}

/* ---- the factory sequences ("seq_pattern"; 0, User, loads nothing). md_lad_cut / md_svf_cut: the matrix's ladder
 * and SVF cutoff destinations, ms_wheel its mod wheel source (engine.c passes them). Returns the sequence length. ---- */
enum { SQF_USER, SQF_BASS, SQF_CHORDS, SQF_RATCHET, SQF_COUNT };
static inline int sq_factory(sq_seq_t *q, int id, int md_lad_cut, int md_svf_cut, int ms_wheel) {
    sq_clear(q);
    switch (id) {
    case SQF_BASS: {   /* 16 steps of a C minor bass line: a tie, a rest, two ratchets, a cutoff lane that opens */
        static const int8_t line[16] = {36, 36, 48, 36, 39, 36, 43, -1, 36, 36, 46, 36, 39, 41, 43, 46};
        for (int s = 0; s < 16; s++) {
            if (line[s] < 0) { q->rest |= 1ull << s; sq_set_note(q, s, 0, 43, 100, 50, 1, 0); continue; }
            sq_set_note(q, s, 0, line[s], s % 4 == 0 ? 118 : 92, s == 5 ? 100 : 45, s == 9 || s == 15 ? 2 : 1, s == 5);
        }
        sq_mlane_t *m = &q->m[0];
        m->dest = (uint8_t)md_lad_cut;
        m->ctl = (uint8_t)ms_wheel;   /* the mod wheel brings the sweep in */
        m->amt = 0;
        for (int s = 0; s < 16; s++) m->off[s] = (int8_t)(s % 4 == 0 ? 60 : s % 2 ? -25 : 30);
        return 16;
    }
    case SQF_CHORDS: {   /* 32 steps of stabs: Cm9 / Abmaj7 / Bb6 / Gm7, two to four lanes, a last chord tied over */
        static const int8_t ch[4][4] = {{48, 51, 55, 62}, {44, 48, 51, 55}, {46, 50, 53, 55}, {43, 50, 53, 58}};
        static const uint8_t hits[8] = {0, 3, 6, 8, 11, 12, 14, 15};   /* where in each 8 steps a stab falls */
        for (int bar = 0; bar < 4; bar++)
            for (int h = 0; h < 8; h++) {
                int s = bar * 8 + hits[h];
                for (int l = 0; l < 4; l++)
                    if (l < 3 || h % 2 == 0) sq_set_note(q, s, l, ch[bar][l], h % 3 == 0 ? 112 : 84, h == 7 ? 100 : 30, 1,
                                                         bar == 3 && h == 7);
            }
        sq_mlane_t *m = &q->m[0];
        m->dest = (uint8_t)md_svf_cut;
        for (int s = 0; s < 32; s++) m->off[s] = (int8_t)(-20 + (s % 8) * 6);
        return 32;
    }
    case SQF_RATCHET: {   /* 16 steps up and down a C minor pentatonic, ratchets of 1-4 and a rest */
        static const int8_t up[16] = {60, 63, 65, 67, 70, 72, 75, 72, 70, 67, -1, 63, 60, 63, 67, 70};
        for (int s = 0; s < 16; s++) {
            if (up[s] < 0) { q->rest |= 1ull << s; continue; }
            sq_set_note(q, s, 0, up[s], 70 + (s * 37) % 57, 60, 1 + (s * 7) % 4 % 3 + (s == 7 ? 1 : 0), 0);
        }
        return 16;
    }
    default: return 16;
    }
}

/* ---- the engine's playback state (engine.c holds one in its instance; src/stepseq_run.h works it) ---- */
#define SQ_CHORD 6           /* notes one lane's note can become through chord memory (seq.h CHORD_MAX) */
#define SQ_EVQ 256           /* ratchet re-articulations and note ends waiting for their sample */
enum { SQ_PLAY, SQ_TRIG };   /* what started it: START/STOP (seq_play), or NOTE TRIG keys */
enum { SQ_EV_OFF, SQ_EV_ON, SQ_EV_ON_TIED };
typedef struct {
    sq_seq_t q;
    /* the clock: beat position pos0 + clk * bps; step k starts at k * step (+ swing on odd k); the pattern step of k is
     * (k - k0) mod length */
    double pos0, bps, step, swoff;
    int64_t clk, next_k, k0, cur_k;
    int follow, sp_new;      /* following the host's song position; a new one arrived this block */
    int running, mode, start_pending, start_mode, start_resume;
    int cur;                 /* the pattern step playing now (-1: none since the start) */
    int resume;              /* where NOTE TRIG carries on from when RESTART ON TRIG is off */
    int cursor, cursor_lane; /* the step (and lane) the editor has selected: step recording goes there */
    int def_gate;            /* the gate new notes get (the last one set in the editor) */
    int transpose;           /* semitones, from the keyboard while TRANSPOSE is on */
    /* each lane's sounding note: its root (-1: none), tied into the next step, the notes chord memory made of it */
    int lroot[SQ_NL], ltied[SQ_NL], lnout[SQ_NL];
    uint8_t lout[SQ_NL][SQ_CHORD];
    struct { int64_t at; uint8_t lane, root, vel, kind; } ev[SQ_EVQ];
    int nev;
    float moff[SQ_ML];       /* each modulation lane's offset now (-1..1); mod_live: any is not 0 */
    int mod_live, env_lanes, lfo_lanes;
    uint64_t fx_mask[MDF_WORDS]; /* the effect destinations the lanes reach (bits of MDF_*) */
    uint8_t trig[128];       /* keys down as NOTE TRIG / TRANSPOSE keys (not sounding), and how many */
    int ntrig;
    int rec_held, rec_step;  /* step recording: keys of the chord being entered, and its step */
    uint8_t rk_on[128];      /* recording: each key held, its step, lane and start (real time, for its gate) */
    int8_t rk_step[128], rk_lane[128];
    double rk_t[128];
    int64_t repl_k[SQ_STEPS];   /* REPLACE: the clock step at which each step was last cleared for new notes */
    uint8_t fresh[SQ_STEPS];    /* lanes just recorded ahead of the playhead: not played again this time round */
    int was_on, was_play, was_trig, was_tr, was_rec, was_sm, was_hold;   /* the switches as last seen */
    unsigned rev;            /* counts edits (the window re-reads the sequence when it moves) */
    int lost;                /* the last saved state had no room for the sequence */
} sq_run_t;
