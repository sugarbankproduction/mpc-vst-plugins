/* (2026-10-30) MIDI OUT (audit 14b): each Synth sends the notes it plays to the host's MIDI out, on its own channel.
 * The types; the functions are src/midi_out_run.h (part of engine.c).
 *
 * Per Synth (saved with the sound, like its MIDI IN channel): `mout_on` (Off / On, default Off), `mout_send` (Keys:
 * the notes played from the keys, chord memory's included; Generated: the arpeggiator's, the step sequencer's and every
 * generator's; Both, the default), `mout_ch` (1-16; Synth n on channel n). The notes go out as the Synth plays them:
 * with its TRANSPOSE and OCTAVE, at the sample they start and end (render() splits its work at each step).
 *
 * Pairs: every note-on sent is remembered (per source note, with the note and channel it went out on), and its note-off
 * always follows: when its key or step ends, on All Notes Off / All Sound Off (and so PANIC), a Synth switched off, the
 * generators stopped (a change of way), MIDI OUT switched off, what it sends changed or its channel moved (those end
 * now, on the old channel), whatever the routing or the transpose became meanwhile. A key let go while the sustain pedal
 * (or HOLD) holds the Synth ends when the pedal does. Two sources on one note and channel (the keys and the sequencer, or
 * two Synths sharing a channel) are counted per instance: the second retriggers (off, then on), and only the last end
 * sends the note-off, so a receiver never hears two note-ons for one note-off.
 *
 * No allocation, lock or file: a fixed buffer in the instance, filled by the audio thread (and by set_param under the
 * wrapper's lock), emptied after each render by the desktop's hostrate.c (one_ish_midi_out_take). Note-ons are refused
 * once MO_ON_ROOM events wait, so the note-offs always find room (at most 3 x 256 notes held, plus one retrigger per
 * note-on). The MPC build never enables it (nothing takes the events there): the engine then sends nothing at all. */
#pragma once
#include <stdint.h>

#define MO_EVMAX 2048     /* events per render, at most */
#define MO_ON_ROOM 512    /* note-ons are taken only while fewer events than this wait */

typedef struct {
    int enabled;          /* the desktop takes the events (one_ish_midi_out_enable); 0: nothing is sent */
    int n;                /* events waiting */
    int64_t now0;         /* the Synths' sample counter at the current render's start (or the next one's) */
    unsigned dropped;     /* note-ons refused (the buffer full) */
    uint8_t cnt[16][128]; /* sources holding each note, per channel (see above) */
    struct { int32_t at; uint8_t m[3]; } ev[MO_EVMAX];   /* at: frames from the render's start */
} mo_buf_t;

typedef struct {
    uint8_t note[128];    /* the note it went out on + 1 (0: none), per source note */
    uint8_t ch[128];      /* its channel */
    uint8_t pend[128];    /* its key came up while the pedal or HOLD holds the Synth: it ends with them */
    uint8_t keys[128];    /* it came from the keys (else generated) */
} mo_tab_t;

typedef struct {
    mo_tab_t key;         /* the keys played straight to the voices */
    mo_tab_t gen;         /* the notes out_on() plays: generated, and chord memory's from the keys */
    int keyctx;           /* out_on() is playing a key's chord (chord memory) */
} mo_syn_t;
