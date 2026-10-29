/* ONE-ish engine. Per voice: three band-limited dual-wave oscillators (src/osc.h: a triangle/saw with a wave angle
 * blended with a pulse of any width; octave, frequency +-7 semitones, beat frequency +-7 Hz; 2 and 3 can hard-sync
 * to 1), linear FM (1 to 2, 1 to 3 or 3 to 1), a ring modulator (1 x 2 or 2 x 3, from the triangle/saw waves) and
 * noise (two of white, red and purple, blended by a colour mix, optionally shaped by its own attack/release envelope
 * with a sustain button) and the MACRO channel (Mutable Instruments Plaits' 24 models, MIT licence: src/macro.h, its own
 * pitch knobs, harmonics / timbre / morph, a trigger on each note-on), each sent to the SVF, the ladder, both or neither
 * by its own two buttons; a state-variable filter
 * (LP/BP/HP/notch; a pair of elements with spacing, in series or parallel: src/svf.h) and a ladder filter (cutoff,
 * resonance, drive, LP/HP, 24-6 dB) in parallel or in series (SVF into ladder) with a mix between them, their cutoff
 * knobs optionally linked; three DAHDSR envelopes (filter, amp, mod: src/env.h, with stage curves, loop, latch, tempo
 * sync, time scale, velocity amount, multi trig and reset). Four LFOs (src/lfo.h: seven waves with a variation morph,
 * three rate ranges or tempo sync with dotted and triplet timing, bipolar or unipolar, smoothing), each free-running
 * or, with note-on reset or a repeat count, restarted per voice at its start phase; per voice a delay and fade in /
 * fade out after each note-on.
 * Hard-wired modulation as on the Moog One panel: filter envelope, LFO 2, keyboard tracking (-200..+200%) and
 * audio-rate FM from a chosen oscillator to each cutoff, LFO 1 and the mod envelope to pitch, LFO 3 to every
 * oscillator's wave angle and pulse width. A 16-slot modulation matrix (src/modmatrix.h: source x amount up to
 * +-300%, a controller with its own amount, a transform) over pitch, levels, wave shapes, both filters, amp, the
 * hard-wired amounts, envelope times (per voice) and LFO rates, variation and times, pan, noise colour, drift and
 * spread, and every continuous effect control (a second pass per control step, from the global sources: see
 * fx_mod_step); mod wheel, aftertouch, pitch bend (separate up and down amounts, plus per-oscillator offsets), sustain
 * and sostenuto pedals, HOLD, two expression CCs (11 and 4), a ribbon (CC 16 position, CC 17 touch: spring or hold,
 * centre or left origin, with a quick assignment of its own), an X/Y pad (CC 2 X, CC 3 Y, CC 18 touch). Up to 16
 * voices: a new note takes an idle voice, else one by the stealing mode (default: the quietest releasing one, else
 * the oldest held one); the same key replays its own voice. Mono (1 voice) plays
 * legato with low, high or last-note priority. Velocity curves, transpose, octave shift, key-track centre, master
 * tune; per oscillator pitch tracking, note-on phase reset and a choice of hard sync source. Glide (portamento): each
 * voice slides from its own
 * last pitch at a constant rate (LCR), in a constant time (LCT) or exponentially (EXP); optionally only on legato
 * notes, paused while the key is up (gated), or in semitone steps (glissando). Unison stacks 2-16 voices on each note
 * (a stack is allocated and stolen as one; when the keys held need more voices than whole stacks leave, the voice count
 * is shared out among them, as on the instrument: 8 voices, count 8, one key 8, two keys 4 each), detuned unevenly
 * around the centre; pitch variance gives each new note a
 * small random pitch offset. Analog character (all off at 0): drift, each oscillator's pitch (and each voice's
 * cutoffs) wandering slowly at random; each voice slot a "voice card" with its own fixed tolerances in cutoff,
 * resonance, envelope times, level and oscillator tuning, scaled by a knob each; and a non-linear ladder (src/ladder.h).
 * Each voice is placed in stereo (equal power) at the pan, spread around it note by note (or across a unison stack).
 * After the voices, in stereo (src/fx.h): a synth effect of a chosen type (delays, chorus, flanger, phaser, bit
 * crusher, resonator...; and open-source ones: Mutable Instruments Rings and Clouds, Airwindows tape, Density and
 * console) with CTRL 1/2, taking the panned voices, then the master effects: two slots on two sends (or one, stereo),
 * each any type of the master list: the delays, choruses, flangers, phasers, bit crusher and resonator, or a plate,
 * room, hall, space (Airwindows Galactic), shimmer or blackhole-style reverb (by default a stereo delay and a plate).
 * Before the voices, a performance section (all off by default: then keys reach the voices exactly as before): an
 * arpeggiator after the Moog One's (Up, Down, Order, Random; 1-4 octaves up/down; octave order; pendulum; synced
 * divisions 1/1-1/128 straight, dotted or triplet, or a free rate; gate, swing, latch, note reset), chord memory (a
 * learned chord or built-in shapes, applied after the arp), and generative steps beyond the Moog One: a Euclidean
 * rhythm, step probability, velocity randomness and a looping random "Turing machine" melody over the held keys or a
 * scale. Its step clock follows the host's song position (HAS_SONG_POS) and places notes on their exact sample.
 *
 * Audio runs per sample; modulation (envelopes 1 and 3, LFOs, the matrix, pitch, cutoffs) every CR samples, with levels and the
 * filter mix ramped across each step so no knob or mod move steps audibly.
 * Implements mpc_engine() (wrapper/engine.h): 44.1 kHz, interleaved int16 stereo, up to 128 frames per render().
 * Everything is allocated in create(); render() and midi() never allocate, lock or touch files.
 * MULTITIMBRAL: one_t below is ONE SYNTH (a timbre: everything above, its own arp and sequencer); an instance holds up
 * to three of them sharing a 16-voice pool, the master effects and a few settings (src/synths.h, included at the end:
 * the engine API, the voice pool, MIDI routing, the state, the mix). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "params.h"
#include "engine.h"
#include "osc.h"
#include "env.h"
#include "ladder.h"
#include "svf.h"
#include "lfo.h"
#include "modmatrix.h"
#include "fx.h"
#include "seq.h"
#include "keyscale.h"
#include "physgen.h"
#include "stepseq.h"
#include "macro.h"
#include "tuning.h"

#define SR 44100.0f
#define CR 16              /* control-rate step in samples: modulation is worked out every 0.36 ms */
#define MAX_HELD 16
#define POOL_VOICES 16      /* the voices every Synth of an instance shares (src/synths.h) */
#define MAX_VOICES 16       /* the instrument's 16-voice model (the "voices" list: Mono, 2 .. 16) */
#define NOSC 3
#define CARD_SET 8         /* voice cards are made in sets of eight (create()) */
#define NSRC 7             /* the mixer's channels: oscillators 1-3, noise, ring mod, MACRO (Plaits: src/macro.h), EXT */
#define NLFO 4
#define HEADROOM 0.5f      /* one full-scale saw at full velocity and volume peaks at -6 dBFS */
#ifndef VST2_CHUNK_MAX
#define VST2_CHUNK_MAX 8192   /* the wrapper's chunk size (wrapper/vst2_wrap.c): 8 KB on the MPC, more on the desktop */
#endif
#define STATE_MAX VST2_CHUNK_MAX
#define NOISE_FLOOR 1e-5f  /* -100 dB of noise into the filters, as in a circuit: starts self-oscillation, avoids denormals */
#define CUT_MIN_HZ 20.0f   /* cutoff knobs: 20 Hz .. 20 kHz on a log scale (0..100) */
#define CUT_RANGE 1000.0f
#define ENV_RANGE 10000.0f /* envelope time knobs: 1 ms .. 10 s on a log scale (0..100) */
#define LFO_RANGE 1000.0f  /* LFO rate knobs: three decades on a log scale (0..100), from the range's lowest rate */
#define LFO_HW_ANGLE 0.5f  /* LFO 3 to wave angle at 100%: +-50% of the angle's range (a sweep across all of it) */
#define LFO_HW_PW 0.45f    /* LFO 3 to pulse width at 100%: +-45% (5..95% from a square) */
#define BEAT_HZ 7.0f       /* the matrix's beat frequency destinations at +-100%: +-7 Hz, the knob's range */
#define LFO_SMOOTH_HZ 1000.0f   /* LFO smoothing: the filter's cutoff falls from 1 kHz at 1% to 0.1 Hz at 100% */
#define ENV_CUT_OCT 5.0f   /* filter envelope amount at +-100%: +-5 octaves of cutoff */
#define LFO_CUT_OCT 3.0f   /* LFO 2 amount at +-100%: +-3 octaves */
#define LFO_PITCH_ST 12.0f /* LFO 1 pitch amount at +-100%: +-12 semitones */
#define ENV_PITCH_ST 24.0f /* mod envelope pitch amount at +-100%: +-24 semitones */
#define GLIDE_MS 10.0f     /* cutoff knob moves are smoothed over about this long */
#define MOD_PITCH_ST 24.0f /* the matrix's pitch destinations at +-100%: +-24 semitones */
#define MOD_CUT_OCT 5.0f   /* its cutoff destinations at +-100%: +-5 octaves */
#define FM_DEPTH 2.0f      /* linear FM at 100%: the carrier's frequency swings by up to 2x its own, times the modulator */
#define FILTER_FM_OCT 4.0f /* filter FM at 100%: the cutoff swings +-4 octaves with a full-scale oscillator */
#define UNI_DETUNE_ST 0.5f /* unison detune at 100%: the outermost stacked voices sit +-50 cents from the note */
#define GLIDE_EXP_K 3.0f   /* EXP glide: the remaining distance shrinks by e every third of the glide time (95% there) */
/* noise colours from the one white source: red is white through a one-pole low-pass at 40 Hz (-6 dB/oct above it),
 * purple its first difference (+6 dB/oct); each scaled to white's RMS level */
#define RED_C 0.0056827f   /* 1 - e^(-2 pi 40 / 44100) */
#define RED_GAIN 18.734f   /* sqrt((2 - c) / c): the low-pass's RMS gain on white noise, undone */
#define PURPLE_GAIN 0.70710678f   /* a first difference doubles white noise's power */
#define DRIFT_ST 0.04f     /* analog drift at 100%: each oscillator wanders within +-4 cents */
#define DRIFT_CUT_OCT 0.04f   /* and each voice's cutoffs within +-0.04 octave (about +-3%) */
#define DRIFT_TAU_S 0.5f   /* the wander follows a new random target with this time constant ... */
#define DRIFT_MIN_S 0.3f   /* ... picked every 0.3 .. 1.5 s, at random */
#define DRIFT_VAR_S 1.2f
/* ANALOG CHARACTER at 100%: how far apart the voice cards' parts sit (each card's spread is fixed, see create()) */
#define AN_CUT_OCT (1.0f / 3)   /* each voice's cutoffs: +-1/3 octave */
#define AN_ENV_FRAC 0.25f       /* its envelopes' stage times: +-25% */
#define AN_LVL_DB 1.5f          /* its level: +-1.5 dB */
#define AN_TUNE_ST 0.08f        /* each of its oscillators: +-8 cents */
#define AN_RES 0.05f            /* its resonances: +-5% of the knob's range */
#define RIBBON_CC 16       /* the ribbon's position, 0..127 (64: the middle) */
#define RIBBON_TOUCH_CC 17 /* 127 (>= 64) as a finger lands on it, 0 as it lifts */
#define RIBBON_RETURN_MS 50.0f   /* SPRING: back to the origin over about this long after the finger lifts */
#define PAD_X_CC 2         /* the X/Y pad (the desktop window's, or any controller): X, 0..127 ... */
#define PAD_Y_CC 3         /* ... Y, 0..127 ... */
#define PAD_TOUCH_CC 18    /* ... and 127 (>= 64) while touched, 0 when let go: the sources Pad X, Pad Y, Pad Gate */
#define FX_MOD_MS 10.0f    /* the effect destinations' modulation is smoothed over about this long */

/* engine-side slots, matched to params.json by key (so the VST order there can grow freely). Per oscillator the
 * slots run WAVE .. LAD from P_O1_WAVE + OSC_STRIDE * n (osc 1's SYNC slot is unused);
 * the envelopes run A, D, S, R from P_ENV_A + ENV_STRIDE * n in ENV_FILTER, ENV_AMP, ENV_MOD order; the LFOs run
 * RATE, WAVE, SYNC from lfo_base(n) (LFOs 1-2 and 3-4 in two blocks) and their extras (L_* below) from
 * P_LFO1_RANGE + LX_STRIDE * n; the source routes and levels in source order. The matrix's slots run (source,
 * destination, amount, via) from P_MOD_BASE and (via amount, transform, transform amount) from P_MODX_BASE. */
enum { L_RANGE, L_VAR, L_POLARITY, L_PHASE, L_RESET, L_DELAY, L_FADE_IN, L_FADE_OUT, L_REPEATS, L_SMOOTH, L_TIMING,
       LX_STRIDE };
enum {
    P_O1_WAVE, P_O1_OCT, P_O1_FREQ, P_O1_BEAT, P_O1_ANGLE, P_O1_PW, P_O1_MIX, P_O1_LEVEL, P_O1_SYNC, P_O1_SVF, P_O1_LAD,
    P_O2_WAVE, P_O2_OCT, P_O2_FREQ, P_O2_BEAT, P_O2_ANGLE, P_O2_PW, P_O2_MIX, P_O2_LEVEL, P_O2_SYNC, P_O2_SVF, P_O2_LAD,
    P_O3_WAVE, P_O3_OCT, P_O3_FREQ, P_O3_BEAT, P_O3_ANGLE, P_O3_PW, P_O3_MIX, P_O3_LEVEL, P_O3_SYNC, P_O3_SVF, P_O3_LAD,
    P_NOISE_LEVEL, P_NOISE_SVF, P_NOISE_LAD, P_RING_LEVEL, P_RING_SVF, P_RING_LAD, P_RING_SRC, P_FM_AMT, P_FM_ROUTE,
    P_FLT_CUT, P_FLT_RES, P_FLT_DRIVE, P_FLT_MODE, P_FLT_SLOPE, P_SVF_CUT, P_SVF_RES, P_SVF_MODE, P_FLT_ROUTE, P_FLT_MIX,
    P_ENV_A, P_ENV_D, P_ENV_S, P_ENV_R,                 /* filter envelope */
    P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R,                 /* amp envelope */
    P_MENV_A, P_MENV_D, P_MENV_S, P_MENV_R,             /* mod envelope */
    P_LAD_ENV, P_LAD_LFO, P_LAD_KBD, P_SVF_ENV, P_SVF_LFO, P_SVF_KBD, P_PITCH_LFO, P_PITCH_ENV,
    P_LFO1_RATE, P_LFO1_WAVE, P_LFO1_SYNC, P_LFO2_RATE, P_LFO2_WAVE, P_LFO2_SYNC,
    P_BEND_RANGE, P_VOICES, P_MOD_BASE,                           /* then MOD_SLOTS x (source, destination, amount, via) */
    P_VOLUME = P_MOD_BASE + MOD_SLOTS * MOD_SLOT_PARAMS,
    P_MODFX_TYPE, P_MODFX_RATE, P_MODFX_DEPTH, P_MODFX_FB, P_MODFX_MIX,
    P_DLY_TIME, P_DLY_SYNC, P_DLY_FB, P_DLY_TONE, P_DLY_PP, P_MFX_SEND1,
    P_REV_DECAY, P_REV_DAMP, P_REV_PRE, P_MFX_SEND2,
    P_GLIDE, P_GLIDE_TIME, P_GLIDE_TYPE, P_GLIDE_LEGATO, P_GLIDE_GATED, P_GLISSANDO,
    P_UNISON, P_UNI_COUNT, P_UNI_DETUNE, P_PITCH_VAR,
    P_SVF_SPACE, P_SVF_PAIR, P_FLT_LINK, P_LAD_FM, P_SVF_FM, P_LAD_FM_SRC, P_SVF_FM_SRC,   /* the filters' MORE page */
    /* each envelope's extras, in ENV_FILTER, ENV_AMP, ENV_MOD order, EX_STRIDE apart (X_* below) */
    P_FENV_DELAY, P_FENV_HOLD, P_FENV_ACURVE, P_FENV_DCURVE, P_FENV_RCURVE, P_FENV_LOOP, P_FENV_LATCH, P_FENV_SYNC,
    P_FENV_MULTI, P_FENV_RESET, P_FENV_SCALE, P_FENV_VEL,
    P_AMP_DELAY, P_AMP_HOLD, P_AMP_ACURVE, P_AMP_DCURVE, P_AMP_RCURVE, P_AMP_LOOP, P_AMP_LATCH, P_AMP_SYNC,
    P_AMP_MULTI, P_AMP_RESET, P_AMP_SCALE, P_AMP_VEL,
    P_MENV_DELAY, P_MENV_HOLD, P_MENV_ACURVE, P_MENV_DCURVE, P_MENV_RCURVE, P_MENV_LOOP, P_MENV_LATCH, P_MENV_SYNC,
    P_MENV_MULTI, P_MENV_RESET, P_MENV_SCALE, P_MENV_VEL,
    /* voice and keyboard options (the polyphony and left-hand MORE pages, tuning) */
    P_NOTE_PRIORITY, P_STEAL_MODE, P_HOLD, P_BEND_UP, P_BEND_DOWN, P_VEL_CURVE, P_TRANSPOSE, P_OCT_SHIFT,
    P_KEY_CENTER, P_MASTER_TUNE, P_FINE_TUNE,
    /* each oscillator's MORE page, XO_STRIDE apart (XO_* below); then the sync sources of oscillators 2 and 3 */
    P_O1_TRACK, P_O1_RESET, P_O1_BEND_UP_OFF, P_O1_BEND_DN_OFF,
    P_O2_TRACK, P_O2_RESET, P_O2_BEND_UP_OFF, P_O2_BEND_DN_OFF,
    P_O3_TRACK, P_O3_RESET, P_O3_BEND_UP_OFF, P_O3_BEND_DN_OFF,
    P_O2_SYNC_SRC, P_O3_SYNC_SRC,
    P_LFO3_RATE, P_LFO3_WAVE, P_LFO3_SYNC, P_LFO4_RATE, P_LFO4_WAVE, P_LFO4_SYNC,
    P_ANGLE_LFO3, P_PW_LFO3,                                      /* LFO 3's hard-wired amounts to the wave shapes */
    P_LFO1_RANGE, P_LFOX_END = P_LFO1_RANGE + NLFO * LX_STRIDE - 1,   /* NLFO x the L_* extras */
    P_MODX_BASE, P_MODX_END = P_MODX_BASE + MOD_SLOTS * MOD_SLOT_EXTRA - 1,
    /* the synth effect's ON and its parameters beyond type, rate, depth, feedback and mix (src/fx.h) */
    P_MODFX_ON, P_MODFX_TIME, P_MODFX_TIME_R, P_MODFX_RATE_R, P_MODFX_FB_R, P_MODFX_DTIME, P_MODFX_DFB, P_MODFX_TONE,
    P_MODFX_SPREAD, P_MODFX_GAIN, P_MODFX_SYNC, P_MODFX_SWEEP, P_MODFX_NOISE, P_MODFX_VRATE, P_MODFX_VDEPTH,
    P_MODFX_FLUTTER, P_MODFX_WOW, P_MODFX_BIAS, P_MODFX_ROOM, P_MODFX_DECAY, P_MODFX_BITS, P_MODFX_SRATE, P_MODFX_RESO,
    P_MODFX_CUT1, P_MODFX_CUT2, P_MODFX_CUT3, P_MODFX_LVL1, P_MODFX_LVL2, P_MODFX_LVL3, P_MODFX_FTYPE,
    P_MODFX_CTRL1, P_MODFX_CTRL2, P_MODFX_C1_DST, P_MODFX_C2_DST,
    /* the master effects: ON, delay into reverb, width; the delay's right time and ping-pong offset; the reverb */
    P_MFX_ON, P_MFX_SERIES, P_MFX_WIDTH, P_DLY_TIME_R, P_DLY_OFFSET,
    P_REV_TYPE, P_REV_SIZE, P_REV_DIFF, P_REV_TONE, P_REV_LOWDAMP, P_REV_MOD, P_REV_EARLY, P_REV_FREEZE,
    P_NOISE_COLOR_PAIR, P_NOISE_COLOR_MIX, P_NOISE_ENV, P_NOISE_A, P_NOISE_R, P_NOISE_SUS,   /* noise colours and EG */
    P_PAN, P_SPREAD, P_DRIFT,
    P_AN_CUT, P_AN_ENV, P_AN_LVL, P_AN_TUNE, P_AN_RES, P_AN_LADDER,   /* ANALOG CHARACTER (with drift) */
    P_RIBBON_DST, P_RIBBON_AMT, P_RIBBON_MODE, P_RIBBON_ORIGIN,   /* the ribbon's quick assignment and behaviour */
    /* the performance section (src/seq.h and "THE PERFORMANCE SECTION" below): arpeggiator, chord memory, generative */
    P_ARP_ON, P_ARP_PATTERN, P_ARP_OCT, P_ARP_DIR, P_ARP_OCT_ORDER, P_ARP_PENDULUM, P_ARP_PEND_MODE, P_ARP_RND_MODE,
    P_ARP_SYNC, P_ARP_RATE, P_ARP_DIV, P_ARP_TIMING, P_ARP_GATE, P_ARP_SWING, P_ARP_LATCH, P_ARP_RESET,
    P_CHORD_LEARN, P_CHORD_ON, P_CHORD_SHAPE, P_CHORD_IV1, P_CHORD_IV5 = P_CHORD_IV1 + CHORD_MAX - 2,
    P_GEN_EUCLID, P_GEN_STEPS, P_GEN_HITS, P_GEN_ROT, P_GEN_PROB, P_GEN_VEL_RAND, P_GEN_TM, P_GEN_TM_LEN,
    P_GEN_TM_CHANGE, P_GEN_SCALE, P_GEN_RANGE,
    /* the MACRO channel (Plaits): model, its three knobs, footage and frequency, main/aux blend, level, filter buttons */
    P_MAC_MODEL, P_MAC_HARM, P_MAC_TIMBRE, P_MAC_MORPH, P_MAC_OCT, P_MAC_FREQ, P_MAC_AUX, P_MAC_LEVEL, P_MAC_SVF,
    P_MAC_LAD,
    /* the open-source synth effect types (src/fx/mi_effects.h, src/fx/airwindows.h): Rings and Clouds, then the
     * Airwindows ones */
    P_MODFX_PITCH, P_MODFX_FOLLOW, P_MODFX_STRUCT, P_MODFX_POS, P_MODFX_MODEL, P_MODFX_SIZE, P_MODFX_DENSITY,
    P_MODFX_TEXTURE, P_MODFX_FREEZE, P_MODFX_GMODE, P_MODFX_QUALITY, P_MODFX_DRIVE, P_MODFX_BUMP, P_MODFX_LOWCUT,
    /* the master effect slots (src/fx.h): the synth effect's two-tap delays and crossover, the buss mode, then
     * each slot's controls beyond what it held before (slot 1: dly_*, slot 2: rev_*; BANK below) */
    P_MODFX_DTIME_R, P_MODFX_DEPTH_R, P_MODFX_OFFSET, P_MODFX_FBL, P_MODFX_FBR, P_MODFX_XOVER, P_MFX_BUSS, P_DLY_TYPE,
    P_DLY_MIX, P_DLY_GAIN, P_DLY_FB_R, P_DLY_FBL, P_DLY_FBR, P_DLY_RATE, P_DLY_RATE_R, P_DLY_DEPTH,
    P_DLY_DEPTH_R, P_DLY_SPREAD, P_DLY_NOISE, P_DLY_SWEEP, P_DLY_DFB, P_DLY_VRATE, P_DLY_VDEPTH, P_DLY_FLUTTER,
    P_DLY_WOW, P_DLY_BIAS, P_DLY_ROOM, P_DLY_DECAY, P_DLY_SIZE, P_DLY_PRE, P_DLY_DAMP, P_DLY_LOWDAMP,
    P_DLY_DIFF, P_DLY_MOD, P_DLY_EARLY, P_DLY_FREEZE, P_DLY_DISTANCE, P_DLY_HIFREQ, P_DLY_HLEVEL, P_DLY_MLEVEL,
    P_DLY_LLEVEL, P_DLY_LDECAY, P_DLY_HDECAY, P_DLY_PITCH, P_DLY_PITCH_B, P_DLY_PDECAY, P_DLY_GRAVITY, P_DLY_XOVER,
    P_DLY_BITS, P_DLY_SRATE, P_DLY_RESO, P_DLY_CUT1, P_DLY_CUT2, P_DLY_CUT3, P_DLY_LVL1, P_DLY_LVL2,
    P_DLY_LVL3, P_DLY_FTYPE, P_REV_MIX, P_REV_GAIN, P_REV_SYNC, P_REV_TIME, P_REV_TIME_R, P_REV_FB,
    P_REV_FB_R, P_REV_FBL, P_REV_FBR, P_REV_RATE, P_REV_RATE_R, P_REV_DEPTH, P_REV_DEPTH_R, P_REV_OFFSET,
    P_REV_SPREAD, P_REV_NOISE, P_REV_SWEEP, P_REV_DFB, P_REV_VRATE, P_REV_VDEPTH, P_REV_FLUTTER, P_REV_WOW,
    P_REV_BIAS, P_REV_ROOM, P_REV_DISTANCE, P_REV_HIFREQ, P_REV_HLEVEL, P_REV_MLEVEL, P_REV_LLEVEL, P_REV_LDECAY,
    P_REV_HDECAY, P_REV_PITCH, P_REV_PITCH_B, P_REV_PDECAY, P_REV_GRAVITY, P_REV_XOVER, P_REV_BITS, P_REV_SRATE,
    P_REV_RESO, P_REV_CUT1, P_REV_CUT2, P_REV_CUT3, P_REV_LVL1, P_REV_LVL2, P_REV_LVL3, P_REV_FTYPE,
    /* the step sequencer (src/stepseq.h, src/stepseq_run.h): its switches and settings; the steps are engine state */
    P_SEQ_ON, P_SEQ_PLAY, P_SEQ_REC, P_SEQ_STEP_MOD, P_SEQ_SYNC, P_SEQ_RATE, P_SEQ_DIV, P_SEQ_TIMING, P_SEQ_LENGTH,
    P_SEQ_FOLLOW_SWING, P_CLOCK_SWING, P_SEQ_NOTE_TRIG, P_SEQ_NOTE_RESET, P_SEQ_RESTART_HOLD, P_SEQ_RESTART_TRIG,
    P_SEQ_TRANSPOSE, P_SEQ_TR_MODE, P_SEQ_ONE_SHOT, P_SEQ_REC_MODE, P_SEQ_PATTERN,
    /* settings: the MIDI channel the instrument listens on (Omni: every channel), and CHORD LEARN heard or silent */
    P_MIDI_CH, P_LEARN_MODE,
    /* multitimbral (src/synths.h): each Synth's setup (ON, MIDI channel, key range); master level and PLAY ALL (shared) */
    P_SYN_ON, P_SYN_CH, P_KEY_LO, P_KEY_HI, P_MASTER_LEVEL, P_SEQ_ALL,
    /* the mixer's EXT channel: the audio input (the desktop plugin's; see one_ish_set_input) into every voice's filters */
    P_EXT_LEVEL, P_EXT_ROUTE,
    /* the vocoders' SIBILANCE and SENSITIVITY: the synth effect's, then each master slot's (src/fx/vocoder.h) */
    P_MODFX_SIBIL, P_MODFX_SENS, P_DLY_SIBIL, P_DLY_SENS, P_REV_SIBIL, P_REV_SENS,
    /* (2026-10-29) each matrix slot's ENABLE; the X/Y pad's polarity and hold, LOAD SAVED POS and the saved wheel and
     * pad positions */
    P_MOD1_ON, P_MOD16_ON = P_MOD1_ON + MOD_SLOTS - 1, P_PAD_POLARITY, P_PAD_HOLD, P_LOAD_POS, P_WHEEL_POS, P_PAD_X_POS,
    P_PAD_Y_POS, P_EG_TIMES,   /* (2026-10-29) the envelopes' MIN and 13 synced values */
    P_ARP_FOLLOW_SWING, P_SEQ_HOST_SS,   /* (2026-10-29) the arp's FOLLOW SWING; the sequencer on the host's start, stop */
    P_SYN_SOLO,          /* (2026-10-29) the Synth soloed (src/synths.h; never saved) */
    P_KEY_ROOT, P_KEY_SCALE,   /* (2026-10-29) the key the generators play in (src/keyscale.h) */
    /* (2026-10-29) the bouncing balls and particles (src/physgen.h, src/physgen_run.h) */
    P_PH_BALLS, P_PH_NBALLS, P_PH_SPEED, P_PH_GRAVITY, P_PH_SIZE, P_PH_BOUNCE, P_PH_PARTS, P_PH_RATE, P_PH_SPREAD,
    P_PH_LIFE, P_PH_ATTRACT, P_PH_TRIGGER, P_PH_KEYS, P_PH_RESTART, P_PH_SYNC, P_PH_LEN, P_PH_OCT, P_PH_RANGE, P_PH_MAX,
    P_PH_DYN,
    /* (2026-10-30) the window's LINKs (desktop/hub.c): shared, stored only; the engine does nothing with them */
    P_LINK_KEY, P_LINK_SCALE, P_LINK_ARP, P_LINK_SEQ, P_LINK_LFO1, P_LINK_LFO2, P_LINK_LFO3, P_LINK_LFO4, P_LINK_PHYS,
    P_LINK_SEND, P_LINK_CUT,
    P_COUNT
};
enum { XO_TRACK, XO_RESET, XO_BEND_UP, XO_BEND_DN, XO_STRIDE = P_O2_TRACK - P_O1_TRACK };
enum { PRIO_LOW, PRIO_HIGH, PRIO_LAST };                        /* "note_priority" options */
enum { STEAL_NONE, STEAL_OLDEST, STEAL_QUIETEST, STEAL_NEWEST }; /* "steal_mode" options */
enum { VEL_SOFT, VEL_MEDIUM, VEL_HARD, VEL_COMPRESS, VEL_EXPAND }; /* "vel_curve" options */
#define OCT_SHIFT_CENTRE 2   /* option index of 0 in the "octave_shift" list */
enum { NC_RED, NC_WHITE, NC_PURPLE };           /* noise colours */
/* the two colours of each "noise_color_pair" option: COLOR MIX at 0% is the first, at 100% the second */
static const int NOISE_PAIR[3][2] = {{NC_RED, NC_WHITE}, {NC_RED, NC_PURPLE}, {NC_WHITE, NC_PURPLE}};
#define NDRIFT (NOSC + 1)                       /* drift: one wander per oscillator, one for the voice's cutoffs */
/* a voice card's parts that differ from card to card: each is a fixed number in -1..1 per voice */
enum { AC_LAD_CUT, AC_SVF_CUT, AC_LAD_RES, AC_SVF_RES, AC_ENV, AC_LVL = AC_ENV + 3, AC_TUNE, AC_COUNT = AC_TUNE + NOSC };
enum { GLIDE_LCR, GLIDE_LCT, GLIDE_EXP };       /* "glide_type" options */
enum { OSC_STRIDE = P_O2_WAVE - P_O1_WAVE, O_WAVE = 0, O_OCT, O_FREQ, O_BEAT, O_ANGLE, O_PW, O_MIX };
/* each mixer channel's level and filter buttons, in channel order (osc 1-3, noise, ring mod, MACRO, EXT), and its
 * level's matrix destination (MACRO's and EXT's came later, after the effects). EXT has a ROUTE list instead of the two
 * buttons (-1: see set_targets). */
static const int LEVEL_SLOT[NSRC] = {P_O1_LEVEL, P_O2_LEVEL, P_O3_LEVEL, P_NOISE_LEVEL, P_RING_LEVEL, P_MAC_LEVEL,
                                     P_EXT_LEVEL};
static const int SVF_SLOT[NSRC] = {P_O1_SVF, P_O2_SVF, P_O3_SVF, P_NOISE_SVF, P_RING_SVF, P_MAC_SVF, -1};
static const int LAD_SLOT[NSRC] = {P_O1_LAD, P_O2_LAD, P_O3_LAD, P_NOISE_LAD, P_RING_LAD, P_MAC_LAD, -1};
static const int LEVEL_MD[NSRC] = {MD_O1_LEVEL, MD_O2_LEVEL, MD_O3_LEVEL, MD_NOISE_LEVEL, MD_RING_LEVEL, MD_MAC_LEVEL,
                                   MD_EXT_LEVEL};
enum { NOISE = 3, RING = 4, MACRO = 5, EXT = 6 };   /* the noise, ring modulator, MACRO and EXT channels */
enum { EXT_NONE, EXT_SVF, EXT_LADDER, EXT_BOTH };    /* "ext_route" options */
enum { RING_1X2, RING_2X3 };                    /* "ring_src" options */
enum { FM_1TO2, FM_1TO3, FM_3TO1 };             /* "fm_route" options */
enum { ENV_FILTER, ENV_AMP, ENV_MOD, NENV, ENV_STRIDE = P_AMP_A - P_ENV_A };
enum { LFO_STRIDE = P_LFO2_RATE - P_LFO1_RATE };
enum { X_DELAY, X_HOLD, X_ACURVE, X_DCURVE, X_RCURVE, X_LOOP, X_LATCH, X_SYNC, X_MULTI, X_RESET, X_SCALE, X_VEL,
       EX_STRIDE = P_AMP_DELAY - P_FENV_DELAY };
static const char *const KEYS[P_COUNT] = {
#define OSC_KEYS(n) "o" #n "_wave", "o" #n "_oct", "o" #n "_freq", "o" #n "_beat", "o" #n "_angle", "o" #n "_pw", \
                    "o" #n "_mix", "o" #n "_level"
    OSC_KEYS(1), "", "o1_svf", "o1_lad",
    OSC_KEYS(2), "o2_sync", "o2_svf", "o2_lad",
    OSC_KEYS(3), "o3_sync", "o3_svf", "o3_lad",
#undef OSC_KEYS
    "noise_level", "noise_svf", "noise_lad", "ring_level", "ring_svf", "ring_lad", "ring_src", "fm_amt", "fm_route",
    "flt_cut", "flt_res", "flt_drive", "flt_mode", "flt_slope", "svf_cut", "svf_res", "svf_mode", "flt_route", "flt_mix",
    "fenv_a", "fenv_d", "fenv_s", "fenv_r",
    "amp_a", "amp_d", "amp_s", "amp_r",
    "menv_a", "menv_d", "menv_s", "menv_r",
    "lad_env", "lad_lfo", "lad_kbd", "svf_env", "svf_lfo", "svf_kbd", "pitch_lfo", "pitch_env",
    "lfo1_rate", "lfo1_wave", "lfo1_sync", "lfo2_rate", "lfo2_wave", "lfo2_sync",
    "" /* the retired "bend_range": store() maps it onto bend_up / bend_down */, "voices",
#define SLOT_KEYS(n) [P_MOD_BASE + 4 * ((n) - 1)] = "mod" #n "_src", [P_MOD_BASE + 4 * ((n) - 1) + 1] = "mod" #n "_dst", \
                     [P_MOD_BASE + 4 * ((n) - 1) + 2] = "mod" #n "_amt", [P_MOD_BASE + 4 * ((n) - 1) + 3] = "mod" #n "_via"
    SLOT_KEYS(1), SLOT_KEYS(2), SLOT_KEYS(3), SLOT_KEYS(4), SLOT_KEYS(5), SLOT_KEYS(6), SLOT_KEYS(7), SLOT_KEYS(8),
    SLOT_KEYS(9), SLOT_KEYS(10), SLOT_KEYS(11), SLOT_KEYS(12), SLOT_KEYS(13), SLOT_KEYS(14), SLOT_KEYS(15),
    SLOT_KEYS(16),
#undef SLOT_KEYS
    [P_VOLUME] = "volume",
    "modfx_type", "modfx_rate", "modfx_depth", "modfx_fb", "modfx_mix",
    "dly_time", "dly_sync", "dly_fb", "dly_tone", "" /* the retired "dly_pp": store() maps it onto dly_type */,
    "mfx_send1",
    "rev_decay", "rev_damp", "rev_pre", "mfx_send2",
    "glide", "glide_time", "glide_type", "glide_legato", "glide_gated", "glissando",
    "unison", "uni_count", "uni_detune", "pitch_var",
    "svf_space", "svf_pair", "flt_link", "lad_fm", "svf_fm", "lad_fm_src", "svf_fm_src",
#define ENV_EXTRA_KEYS(e) e "_delay", e "_hold", e "_acurve", e "_dcurve", e "_rcurve", e "_loop", e "_latch", e "_sync", \
                          e "_multi", e "_reset", e "_scale", e "_vel"
    ENV_EXTRA_KEYS("fenv"), ENV_EXTRA_KEYS("amp"), ENV_EXTRA_KEYS("menv"),
#undef ENV_EXTRA_KEYS
    [P_NOTE_PRIORITY] = "note_priority", [P_STEAL_MODE] = "steal_mode", [P_HOLD] = "hold", [P_BEND_UP] = "bend_up",
    [P_BEND_DOWN] = "bend_down", [P_VEL_CURVE] = "vel_curve", [P_TRANSPOSE] = "transpose",
    [P_OCT_SHIFT] = "octave_shift", [P_KEY_CENTER] = "key_center", [P_MASTER_TUNE] = "master_tune",
    [P_FINE_TUNE] = "fine_tune",
#define OSC_MORE_KEYS(n) [P_O##n##_TRACK] = "o" #n "_track", [P_O##n##_RESET] = "o" #n "_reset", \
                         [P_O##n##_BEND_UP_OFF] = "o" #n "_bend_up_off", [P_O##n##_BEND_DN_OFF] = "o" #n "_bend_dn_off"
    OSC_MORE_KEYS(1), OSC_MORE_KEYS(2), OSC_MORE_KEYS(3),
#undef OSC_MORE_KEYS
    [P_O2_SYNC_SRC] = "o2_sync_src", [P_O3_SYNC_SRC] = "o3_sync_src",
    "lfo3_rate", "lfo3_wave", "lfo3_sync", "lfo4_rate", "lfo4_wave", "lfo4_sync",
    "angle_lfo3", "pw_lfo3",
#define LFO_EXTRA_KEYS(n) "lfo" #n "_range", "lfo" #n "_var", "lfo" #n "_polarity", "lfo" #n "_phase", "lfo" #n "_reset", \
                          "lfo" #n "_delay", "lfo" #n "_fade_in", "lfo" #n "_fade_out", "lfo" #n "_repeats", \
                          "lfo" #n "_smooth", "lfo" #n "_timing"
    LFO_EXTRA_KEYS(1), LFO_EXTRA_KEYS(2), LFO_EXTRA_KEYS(3), LFO_EXTRA_KEYS(4),
#undef LFO_EXTRA_KEYS
#define SLOTX_KEYS(n) "mod" #n "_via_amt", "mod" #n "_xf", "mod" #n "_xf_amt"
    SLOTX_KEYS(1), SLOTX_KEYS(2), SLOTX_KEYS(3), SLOTX_KEYS(4), SLOTX_KEYS(5), SLOTX_KEYS(6), SLOTX_KEYS(7),
    SLOTX_KEYS(8), SLOTX_KEYS(9), SLOTX_KEYS(10), SLOTX_KEYS(11), SLOTX_KEYS(12), SLOTX_KEYS(13), SLOTX_KEYS(14),
    SLOTX_KEYS(15), SLOTX_KEYS(16),
#undef SLOTX_KEYS
    "modfx_on", "modfx_time", "modfx_time_r", "modfx_rate_r", "modfx_fb_r", "modfx_dtime", "modfx_dfb", "modfx_tone",
    "modfx_spread", "modfx_gain", "modfx_sync", "modfx_sweep", "modfx_noise", "modfx_vrate", "modfx_vdepth",
    "modfx_flutter", "modfx_wow", "modfx_bias", "modfx_room", "modfx_decay", "modfx_bits", "modfx_srate", "modfx_reso",
    "modfx_cut1", "modfx_cut2", "modfx_cut3", "modfx_lvl1", "modfx_lvl2", "modfx_lvl3", "modfx_ftype",
    "modfx_ctrl1", "modfx_ctrl2", "modfx_c1_dst", "modfx_c2_dst",
    "mfx_on", "mfx_series", "mfx_width", "dly_time_r", "dly_offset",
    "rev_type", "rev_size", "rev_diff", "rev_tone", "rev_lowdamp", "rev_mod", "rev_early", "rev_freeze",
    "noise_color_pair", "noise_color_mix", "noise_env", "noise_a", "noise_r", "noise_sus",
    "pan", "spread", "drift",
    "an_cut", "an_env", "an_lvl", "an_tune", "an_res", "an_ladder",
    "ribbon_dst", "ribbon_amt", "ribbon_mode", "ribbon_origin",
    [P_ARP_ON] = "arp_on", "arp_pattern", "arp_oct", "arp_dir", "arp_oct_order", "arp_pendulum", "arp_pend_mode",
    "arp_rnd_mode", "arp_sync", "arp_rate", "arp_div", "arp_timing", "arp_gate", "arp_swing", "arp_latch", "arp_reset",
    [P_CHORD_LEARN] = "chord_learn", "chord_on", "chord_shape", "chord_iv1", "chord_iv2", "chord_iv3", "chord_iv4",
    "chord_iv5",
    [P_GEN_EUCLID] = "gen_euclid_on", "gen_steps", "gen_hits", "gen_rot", "gen_prob", "gen_vel_rand", "gen_tm_on",
    "gen_tm_len", "gen_tm_change", "gen_scale", "gen_range",
    "mac_model", "mac_harm", "mac_timbre", "mac_morph", "mac_oct", "mac_freq", "mac_aux", "mac_level", "mac_svf",
    "mac_lad",
    "modfx_pitch", "modfx_follow", "modfx_struct", "modfx_pos", "modfx_model", "modfx_size", "modfx_density",
    "modfx_texture", "modfx_freeze", "modfx_gmode", "modfx_quality", "modfx_drive", "modfx_bump", "modfx_lowcut",
    [P_MODFX_DTIME_R] = "modfx_dtime_r",
    "modfx_depth_r", "modfx_offset", "modfx_fbl", "modfx_fbr", "modfx_xover", "mfx_buss", "dly_type", "dly_mix",
    "dly_gain", "dly_fb_r", "dly_fbl", "dly_fbr", "dly_rate", "dly_rate_r", "dly_depth", "dly_depth_r",
    "dly_spread", "dly_noise", "dly_sweep", "dly_dfb", "dly_vrate", "dly_vdepth", "dly_flutter", "dly_wow",
    "dly_bias", "dly_room", "dly_decay", "dly_size", "dly_pre", "dly_damp", "dly_lowdamp", "dly_diff", "dly_mod",
    "dly_early", "dly_freeze", "dly_distance", "dly_hifreq", "dly_hlevel", "dly_mlevel", "dly_llevel", "dly_ldecay",
    "dly_hdecay", "dly_pitch", "dly_pitch_b", "dly_pdecay", "dly_gravity", "dly_xover", "dly_bits", "dly_srate",
    "dly_reso", "dly_cut1", "dly_cut2", "dly_cut3", "dly_lvl1", "dly_lvl2", "dly_lvl3", "dly_ftype", "rev_mix",
    "rev_gain", "rev_sync", "rev_time", "rev_time_r", "rev_fb", "rev_fb_r", "rev_fbl", "rev_fbr", "rev_rate",
    "rev_rate_r", "rev_depth", "rev_depth_r", "rev_offset", "rev_spread", "rev_noise", "rev_sweep", "rev_dfb",
    "rev_vrate", "rev_vdepth", "rev_flutter", "rev_wow", "rev_bias", "rev_room", "rev_distance", "rev_hifreq",
    "rev_hlevel", "rev_mlevel", "rev_llevel", "rev_ldecay", "rev_hdecay", "rev_pitch", "rev_pitch_b", "rev_pdecay",
    "rev_gravity", "rev_xover", "rev_bits", "rev_srate", "rev_reso", "rev_cut1", "rev_cut2", "rev_cut3", "rev_lvl1",
    "rev_lvl2", "rev_lvl3", "rev_ftype",
    "seq_on", "seq_play", "seq_rec", "seq_step_mod", "seq_sync", "seq_rate", "seq_div", "seq_timing", "seq_length",
    "seq_follow_swing", "clock_swing", "seq_note_trig", "seq_note_reset", "seq_restart_hold", "seq_restart_trig",
    "seq_transpose", "seq_tr_mode", "seq_one_shot", "seq_rec_mode", "seq_pattern",
    "midi_ch", "learn_mode",
    "syn_on", "syn_ch", "key_lo", "key_hi", "master_level", "seq_all",
    "ext_level", "ext_route",
    "modfx_sibil", "modfx_sens", "dly_sibil", "dly_sens", "rev_sibil", "rev_sens",
    "mod1_on", "mod2_on", "mod3_on", "mod4_on", "mod5_on", "mod6_on", "mod7_on", "mod8_on", "mod9_on", "mod10_on",
    "mod11_on", "mod12_on", "mod13_on", "mod14_on", "mod15_on", "mod16_on", "pad_polarity", "pad_hold", "load_pos",
    "wheel_pos", "pad_x_pos", "pad_y_pos", "eg_times", "arp_follow_swing", "seq_host_ss",
    "syn_solo", "key_root", "key_scale",
    "phys_balls_on", "phys_balls", "phys_speed", "phys_gravity", "phys_size", "phys_bounce", "phys_parts_on", "phys_rate",
    "phys_spread", "phys_life", "phys_attract", "phys_trigger", "phys_keys", "phys_restart", "phys_sync", "phys_len",
    "phys_octave", "phys_range", "phys_max", "phys_dyn",
    "link_key", "link_scale", "link_arp", "link_seq", "link_lfo1", "link_lfo2", "link_lfo3", "link_lfo4", "link_phys",
    "link_send", "link_cut",
};
enum { RIB_SPRING, RIB_HOLD };                  /* "ribbon_mode" options */
enum { RIB_CENTRE, RIB_LEFT };                  /* "ribbon_origin" options */
/* the effect destinations (modmatrix.h's MDF_* order): each one's slot */
static const short FXMD_SLOT[] = {
    P_MODFX_MIX, P_MODFX_GAIN, P_MODFX_CTRL1, P_MODFX_CTRL2, P_MODFX_RATE, P_MODFX_RATE_R, P_MODFX_DEPTH, P_MODFX_FB,
    P_MODFX_FB_R, P_MODFX_TIME, P_MODFX_TIME_R, P_MODFX_DTIME, P_MODFX_DFB, P_MODFX_TONE, P_MODFX_SPREAD, P_MODFX_SWEEP,
    P_MODFX_NOISE, P_MODFX_VRATE, P_MODFX_VDEPTH, P_MODFX_FLUTTER, P_MODFX_WOW, P_MODFX_BIAS, P_MODFX_ROOM,
    P_MODFX_DECAY, P_MODFX_BITS, P_MODFX_SRATE, P_MODFX_RESO, P_MODFX_CUT1, P_MODFX_CUT2, P_MODFX_CUT3, P_MODFX_LVL1,
    P_MODFX_LVL2, P_MODFX_LVL3, P_MFX_SEND1, P_MFX_SEND2, P_MFX_SERIES, P_MFX_WIDTH, P_DLY_TIME, P_DLY_TIME_R, P_DLY_FB,
    P_DLY_TONE, P_DLY_OFFSET, P_REV_DECAY, P_REV_SIZE, P_REV_PRE, P_REV_DAMP, P_REV_LOWDAMP, P_REV_DIFF, P_REV_TONE,
    P_REV_MOD, P_REV_EARLY,
    P_MODFX_PITCH, P_MODFX_STRUCT, P_MODFX_POS, P_MODFX_SIZE, P_MODFX_DENSITY, P_MODFX_TEXTURE, P_MODFX_DRIVE,
    P_MODFX_BUMP, P_MODFX_LOWCUT,
    P_MODFX_DTIME_R, P_MODFX_DEPTH_R, P_MODFX_OFFSET, P_MODFX_FBL, P_MODFX_FBR, P_MODFX_XOVER, P_DLY_MIX,
    P_DLY_GAIN, P_DLY_FB_R, P_DLY_FBL, P_DLY_FBR, P_DLY_RATE, P_DLY_RATE_R, P_DLY_DEPTH,
    P_DLY_DEPTH_R, P_DLY_SPREAD, P_DLY_NOISE, P_DLY_SWEEP, P_DLY_DFB, P_DLY_VRATE, P_DLY_VDEPTH,
    P_DLY_FLUTTER, P_DLY_WOW, P_DLY_BIAS, P_DLY_ROOM, P_DLY_DECAY, P_DLY_SIZE, P_DLY_PRE,
    P_DLY_DAMP, P_DLY_LOWDAMP, P_DLY_DIFF, P_DLY_MOD, P_DLY_EARLY, P_DLY_DISTANCE, P_DLY_HIFREQ,
    P_DLY_HLEVEL, P_DLY_MLEVEL, P_DLY_LLEVEL, P_DLY_LDECAY, P_DLY_HDECAY, P_DLY_PITCH, P_DLY_PITCH_B,
    P_DLY_PDECAY, P_DLY_GRAVITY, P_DLY_XOVER, P_DLY_BITS, P_DLY_SRATE, P_DLY_RESO, P_DLY_CUT1,
    P_DLY_CUT2, P_DLY_CUT3, P_DLY_LVL1, P_DLY_LVL2, P_DLY_LVL3, P_REV_MIX, P_REV_GAIN,
    P_REV_TIME, P_REV_TIME_R, P_REV_FB, P_REV_FB_R, P_REV_FBL, P_REV_FBR, P_REV_RATE,
    P_REV_RATE_R, P_REV_DEPTH, P_REV_DEPTH_R, P_REV_OFFSET, P_REV_SPREAD, P_REV_NOISE, P_REV_SWEEP,
    P_REV_DFB, P_REV_VRATE, P_REV_VDEPTH, P_REV_FLUTTER, P_REV_WOW, P_REV_BIAS, P_REV_ROOM,
    P_REV_DISTANCE, P_REV_HIFREQ, P_REV_HLEVEL, P_REV_MLEVEL, P_REV_LLEVEL, P_REV_LDECAY, P_REV_HDECAY,
    P_REV_PITCH, P_REV_PITCH_B, P_REV_PDECAY, P_REV_GRAVITY, P_REV_XOVER, P_REV_BITS, P_REV_SRATE,
    P_REV_RESO, P_REV_CUT1, P_REV_CUT2, P_REV_CUT3, P_REV_LVL1, P_REV_LVL2, P_REV_LVL3,
};
_Static_assert(sizeof FXMD_SLOT / sizeof FXMD_SLOT[0] == MDF_COUNT, "an effect destination per MDF_* entry");
/* each master effect slot's controls (fx.h; slot 1's were the delay's dly_* params, slot 2's the reverb's rev_*), in
 * the field order fx_bank_field() reads them */
enum {
    BK_TYPE, BK_MIX, BK_GAIN, BK_SYNC, BK_TIME, BK_TIME_R, BK_FB, BK_FB_R, BK_FBL, BK_FBR, BK_RATE, BK_RATE_R,
    BK_DEPTH, BK_DEPTH_R, BK_TONE, BK_OFFSET, BK_SPREAD, BK_NOISE, BK_SWEEP, BK_DFB, BK_VRATE, BK_VDEPTH, BK_FLUTTER,
    BK_WOW, BK_BIAS, BK_ROOM, BK_DECAY, BK_SIZE, BK_PRE, BK_DAMP, BK_LOWDAMP, BK_DIFF, BK_MOD, BK_EARLY, BK_FREEZE,
    BK_DISTANCE, BK_HIFREQ, BK_HLEVEL, BK_MLEVEL, BK_LLEVEL, BK_LDECAY, BK_HDECAY, BK_PITCH, BK_PITCH_B, BK_PDECAY,
    BK_GRAVITY, BK_XOVER, BK_BITS, BK_SRATE, BK_RESO, BK_CUT1, BK_CUT2, BK_CUT3, BK_LVL1, BK_LVL2, BK_LVL3, BK_FTYPE,
    BK_SIBIL, BK_SENS,   /* the vocoders' (src/fx/vocoder.h) */
    BK_COUNT };
static const short BANK[2][BK_COUNT] = {
    {P_DLY_TYPE, P_DLY_MIX, P_DLY_GAIN, P_DLY_SYNC, P_DLY_TIME, P_DLY_TIME_R, P_DLY_FB, P_DLY_FB_R, P_DLY_FBL,
     P_DLY_FBR, P_DLY_RATE, P_DLY_RATE_R, P_DLY_DEPTH, P_DLY_DEPTH_R, P_DLY_TONE, P_DLY_OFFSET, P_DLY_SPREAD,
     P_DLY_NOISE, P_DLY_SWEEP, P_DLY_DFB, P_DLY_VRATE, P_DLY_VDEPTH, P_DLY_FLUTTER, P_DLY_WOW, P_DLY_BIAS, P_DLY_ROOM,
     P_DLY_DECAY, P_DLY_SIZE, P_DLY_PRE, P_DLY_DAMP, P_DLY_LOWDAMP, P_DLY_DIFF, P_DLY_MOD, P_DLY_EARLY, P_DLY_FREEZE,
     P_DLY_DISTANCE, P_DLY_HIFREQ, P_DLY_HLEVEL, P_DLY_MLEVEL, P_DLY_LLEVEL, P_DLY_LDECAY, P_DLY_HDECAY, P_DLY_PITCH,
     P_DLY_PITCH_B, P_DLY_PDECAY, P_DLY_GRAVITY, P_DLY_XOVER, P_DLY_BITS, P_DLY_SRATE, P_DLY_RESO, P_DLY_CUT1,
     P_DLY_CUT2, P_DLY_CUT3, P_DLY_LVL1, P_DLY_LVL2, P_DLY_LVL3, P_DLY_FTYPE, P_DLY_SIBIL, P_DLY_SENS},
    {P_REV_TYPE, P_REV_MIX, P_REV_GAIN, P_REV_SYNC, P_REV_TIME, P_REV_TIME_R, P_REV_FB, P_REV_FB_R, P_REV_FBL,
     P_REV_FBR, P_REV_RATE, P_REV_RATE_R, P_REV_DEPTH, P_REV_DEPTH_R, P_REV_TONE, P_REV_OFFSET, P_REV_SPREAD,
     P_REV_NOISE, P_REV_SWEEP, P_REV_DFB, P_REV_VRATE, P_REV_VDEPTH, P_REV_FLUTTER, P_REV_WOW, P_REV_BIAS, P_REV_ROOM,
     P_REV_DECAY, P_REV_SIZE, P_REV_PRE, P_REV_DAMP, P_REV_LOWDAMP, P_REV_DIFF, P_REV_MOD, P_REV_EARLY, P_REV_FREEZE,
     P_REV_DISTANCE, P_REV_HIFREQ, P_REV_HLEVEL, P_REV_MLEVEL, P_REV_LLEVEL, P_REV_LDECAY, P_REV_HDECAY, P_REV_PITCH,
     P_REV_PITCH_B, P_REV_PDECAY, P_REV_GRAVITY, P_REV_XOVER, P_REV_BITS, P_REV_SRATE, P_REV_RESO, P_REV_CUT1,
     P_REV_CUT2, P_REV_CUT3, P_REV_LVL1, P_REV_LVL2, P_REV_LVL3, P_REV_FTYPE, P_REV_SIBIL, P_REV_SENS},
};

#define OCT_CENTRE 2   /* option index of 8' (no octave shift) in the "oct" lists */
#define NOCT 5         /* their options: 32' .. 2' */
enum { FLT_PARALLEL, FLT_SERIES };

/* tempo-synced LFO rates (the rate knob picks one): beats per LFO cycle */
static const struct { const char *name; float beats; } SYNC_DIV[] = {
    {"4 bars", 16}, {"2 bars", 8}, {"1 bar", 4}, {"1/2", 2}, {"1/4", 1}, {"1/4T", 2.0f / 3}, {"1/8", 0.5f},
    {"1/8T", 1.0f / 3}, {"1/16", 0.25f}, {"1/16T", 1.0f / 6}, {"1/32", 0.125f},
};
#define NDIV ((int)(sizeof SYNC_DIV / sizeof SYNC_DIV[0]))

/* a synced LFO's divisions (the Moog One's 64 .. 1/64 quarter-note beats, named as note lengths), times its timing:
 * straight, dotted (1.5x as long) or triplet (2/3) */
static const struct { const char *name; float beats; } LFO_DIV[] = {
    {"16 bars", 64}, {"8 bars", 32}, {"4 bars", 16}, {"2 bars", 8}, {"1 bar", 4}, {"1/2", 2}, {"1/4", 1},
    {"1/8", 0.5f}, {"1/16", 0.25f}, {"1/32", 0.125f}, {"1/64", 1.0f / 16}, {"1/128", 1.0f / 32}, {"1/256", 1.0f / 64},
};
#define NLDIV ((int)(sizeof LFO_DIV / sizeof LFO_DIV[0]))
static const float LFO_TIMING[] = {1.0f, 1.5f, 2.0f / 3};   /* "lfoN_timing" options */
static const char *const LFO_TIMING_TAG[] = {"", " D", " T"};
/* "lfoN_range": Slow, Low, Medium, High (each one's lowest rate; High, 1 Hz .. 1 kHz, runs per sample: 2026-10-29) */
static const float LFO_RANGE_HZ[] = {0.001f, 0.01f, 0.1f, 1.0f};
enum { LR_HIGH = 3 };
enum { POL_BIPOLAR, POL_UNIPOLAR };                         /* "lfoN_polarity" options */

typedef struct {
    osc_t osc[NOSC];
    osc_shape_t shape[NOSC];     /* each oscillator's wave shape at the last control step (ramp start) */
    osc_shape_t tgt_shape[NOSC]; /* and at this one (ramp end), modulation included */
    float inc0[NOSC];        /* each oscillator's frequency / sample rate before FM */
    float fm, tgt_fm;        /* the FM depth (ramp start, end) */
    float fm_prev;           /* oscillator 3's last sample, for FM 3 > 1 */
    ladder_t lad;
    svf2_t svf;              /* the SVF: a pair of elements acting as one filter */
    env_t env[NENV];
    uint32_t noise;          /* xorshift state */
    int note;                /* the sounding note (glide's target), -1 before the first */
    float pitch;             /* the glided pitch in semitones (MIDI note numbers), equal to note once a glide ends */
    float glide_span;        /* how far this glide goes, in semitones (LCT covers it in the glide time) */
    int gliding;             /* the pitch is still on its way to note */
    float uni_pos;           /* place in the unison stack, -1..1 (0: the centre or no unison), times the detune */
    float uni_gain;          /* 1 / sqrt(stack size): a detuned stack about as loud as one voice */
    float pvar;              /* this note's pitch variance offset, semitones */
    float note_hz, velocity;
    float gain;              /* smoothed towards velocity x volume, so neither jumps the level mid-note */
    float cut, svf_cut;      /* the cutoff knobs (0..100) as this voice's filters follow them, smoothed */
    float rand;              /* this note's random value (-1..1), a matrix source */
    float to_lad[NSRC], to_svf[NSRC], mix_lad;   /* last control step's levels and filter mix (ramp start) */
    float tgt_lad[NSRC], tgt_svf[NSRC], tgt_mix; /* this control step's (ramp end), modulation included */
    float amp_mod;           /* the matrix's amp level factor (0..2) */
    float rand_u;            /* this note's unipolar random value (0..1), a matrix source */
    float beat_mod[NOSC];    /* the matrix's beat frequency per oscillator, Hz */
    float lad_fm, svf_fm;    /* the filter FM amounts (0..1), modulation included */
    lfo_t lfo[NLFO];         /* the LFOs as this voice runs them: its own when restarted per note or modulated */
    float lfo_out[NLFO];     /* this control step's LFO outputs, delay and fades applied */
    float lfo_time[NLFO];    /* samples since the note-on, for the LFO delay and fades */
    float lfo_dm[NLFO][MDL_COUNT];   /* the matrix's LFO destinations from the last control step */
    float src[MS_COUNT];     /* the matrix sources at the last control step (the effect pass reads the newest voice's) */
    env_cfg_t ecfg[NENV];    /* each envelope's settings with this voice's matrix modulation (when it has any) */
    float eq[NENV][5];       /* the stage times ecfg was worked out for, as env_q() values */
    unsigned egen[NENV];     /* and the shared settings' generation */
    unsigned lfo_own;        /* bit n: the voice runs LFO n itself (restarted per note or modulated) */
    int gate;                /* key down, or released while the sustain pedal is down */
    int sustained;           /* released while the pedal (or HOLD, or sostenuto) is down: let go when it comes up */
    unsigned age;            /* when its note started, for stealing the oldest */
    int sost;                /* sounding when the sostenuto pedal went down: its key's release waits for the pedal */
    int reset_pend;          /* NOTE-ON RESET: a bit per oscillator to restart at phase 0 on the next sample */
    env_t nenv;              /* the noise EG (attack, release, sustain button) */
    float noise_env;         /* its level at the last control step, 0..1 (a matrix source) */
    float red, wprev;        /* red noise's low-pass state; the last white sample, for purple */
    float nz[2], tgt_nz[2];  /* the two noise colours' gains (colour mix x noise EG): ramp start, end */
    float nz_m, nz_e;        /* the colour mix and noise EG level they were worked out for */
    float pan_pos;           /* where the voice sits, -1 (left) .. 1 (right): pan, spread place and modulation, smoothed */
    float pl, pr, tgt_pl, tgt_pr;   /* its left and right gains (ramp start, end): equal power, 1 and 1 at the centre */
    float drift[NDRIFT], drift_tgt[NDRIFT];   /* analog drift: each wander, -1..1, and the target it is heading for */
    int drift_left[NDRIFT];  /* control steps until a new target */
    uint32_t drift_rng;      /* drift's own random numbers (the noise's sequence stays as it was) */
    /* ANALOG CHARACTER: this voice card's parts (AC_*, -1..1, fixed), and what they come to at the knobs (apply_analog):
     * all exactly 0 (gain 1) with the knobs at 0, so the sound is then the same to the sample */
    float card[AC_COUNT];
    float an_cut[2], an_res[2];   /* ladder, SVF: octaves of cutoff, resonance (0..1 units) */
    float an_env_dq[NENV];   /* each envelope's stage times, as log10 of the factor (env_q's units) */
    float an_gain;           /* level factor */
    float an_tune[NOSC];     /* semitones per oscillator */
    int env_mod[NENV];       /* the envelope's settings had matrix modulation at the last control step */
    macro_t *mac;            /* the MACRO channel's Plaits state (allocated in create()), run only while it is heard */
    float mac_note;          /* its pitch (a MIDI note number, modulation included) */
    float mac_harm, mac_timbre, mac_morph;   /* its three knobs with the matrix's modulation, 0..1 */
    int mac_trig;            /* a note-on it hasn't seen yet (kept until the channel is heard) */
    int dying;               /* stolen for another Synth (the pool, src/synths.h): fading out, not counted as sounding */
    /* (2026-10-29) the step-mod list's destinations */
    env_cfg_t ncfg_v;        /* the noise EG's settings with this voice's modulation (when it has any) */
    float nq[3];             /* the attack knob, release knob and sustain they were worked out for ... */
    unsigned ngen;           /* ... and the shared settings' generation */
    float multi_mat[NENV];   /* the matrix's MULTI TRIG modulation at the last control step (gate_on adds the sequencer's) */
    float lfo_wdm[NLFO];     /* the matrix's LFO wave modulation from the last control step */
    /* (2026-10-29) LFOs in the High range run per sample in render_voice (hi_lfo_block) */
    unsigned hi_run;         /* bit n: LFO n is High and runs this control step (not waiting out its delay, not done) */
    unsigned hi_reach;       /* bit n: and it moves pitch, a cutoff or the amp level (its per-sample offsets) */
    float hi_inc[NLFO];      /* its step per sample */
    int hi_wave[NLFO];       /* its wave and variation this step (modulation included) */
    float hi_var[NLFO];
    float hi_g[NLFO];        /* its delay / fade gain this step */
    float hi_y0[NLFO];       /* its value at the control step (lfo_out), which the control-rate settings already hold */
    float hi_k[NLFO][7];     /* how far it moves (per unit): HK_* below */
    float hi_amp0;           /* the amp level's modulation without the High LFOs' share (1: none) ... */
    float hi_amp1;           /* ... and with it, at the control step */
} voice_t;
enum { HK_PITCH, HK_O1, HK_O2, HK_O3, HK_LAD, HK_SVF, HK_AMP };   /* semitones (all, each osc), octaves (ladder, SVF), x */

typedef struct {
    float val[P_COUNT];      /* each value in its own units: option index, semitones, cents, % or a 0..100 knob */
    float def[P_COUNT];      /* the defaults, so a saved state lists only what differs from them */
    float ratio[NOSC];       /* each oscillator's pitch against the played note (octave and frequency) */
    voice_t voice[MAX_VOICES];
    int voices;              /* the voices in use last applied (a change lets go of voices past it) */
    float glide_frac;        /* one control step as a fraction of the glide time (>= 1: no glide) */
    float glide_exp;         /* EXP glide: the fraction of the remaining distance covered per control step */
    fx_syn_t fx;             /* the synth effect's buffers (about 1 MB; the master effects are the instance's) */
    fx_send_t snd;           /* its two sends into the master effects, as last ramped */
    int sustain;             /* the pedal (CC 64) is down */
    lfo_t lfo[NLFO];         /* the free-running LFOs */
    float lfo_val[NLFO];     /* this control step's free-running LFO outputs (polarity and smoothing applied) */
    float lfo_hz[NLFO];      /* and their rates (apply_lfos) */
    float lfo_coef[NLFO];    /* each LFO's smoothing coefficient per control step (1: none) */
    float lfo_fade[NLFO][3]; /* each LFO's delay, fade in and fade out, samples (0: off) */
    float lfo_coef1[NLFO];   /* each LFO's smoothing coefficient per sample (the High range's path; 1: none) */
    uint32_t bipolar;        /* bit s: matrix source s is bipolar (for the limit transforms) */
    uint8_t mod_list[MOD_SLOTS];   /* the matrix slots in use, and how many */
    int mod_n;
    int mod_env, mod_lfo;    /* a slot in use goes to an envelope's / an LFO's settings */
    int mod_nenv;            /* ... or the noise EG's (2026-10-29) */
    uint8_t fx_list[MOD_SLOTS];   /* the slots in use that reach the effects (mod_list has the others), and how many */
    int fx_n;
    int fx_mod;              /* a slot or the ribbon reaches the effects: the effect pass runs */
    int fx_live;             /* the smoothed effect modulation is not all 0 yet */
    float fxm[MDF_COUNT];    /* the effect destinations' modulation, smoothed (+-1 = +-the control's range) */
    float fx_lo[MDF_COUNT], fx_hi[MDF_COUNT];   /* each effect destination's range (its param's) */
    short fx_of[P_COUNT];    /* each slot's effect destination (MDF_*), or -1 */
    signed char fx_bank[P_COUNT], fx_bankf[P_COUNT];   /* a master slot's control: its slot (or -1) and BK_* field */
    uint64_t fx_was[MDF_WORDS];   /* bit k: effect destination k was modulated (not 0) at the last control step */
    uint64_t fx_dst[MDF_WORDS];   /* bit k: a slot in use or the ribbon reaches effect destination k */
    fx_params_t fxp;         /* the effect settings as fx.h reads them, kept between renders */
    int fx_stale;            /* a setting changed since fxp was worked out */
    float fx_key;            /* the Rings effect's key: the newest sounding voice's, kept while nothing sounds */
    unsigned fx_age;         /* that voice's note start (0: none yet), so a new note strums it */
    float rib, rib_to;       /* the ribbon's position (0..127, smoothed) and where it is heading */
    int rib_touch, rib_back; /* a finger is on it; SPRING is taking it back to its origin */
    float rib_coef, fx_coef; /* per control step: the spring's return; the effect modulation's smoothing */
    unsigned env_gen;        /* counts changes to the shared envelope settings */
    float exp1, exp2;        /* expression controllers (CC 11, CC 4), 0..1 */
    float pad_x, pad_y, pad_gate;   /* the X/Y pad (CC 2, CC 3, CC 18): 0..1, 0..1, 0 or 1 */
    float bpm;               /* the host's tempo, for synced LFOs, delay and envelopes */
    env_cfg_t ecfg[NENV];    /* each envelope's settings (times, curves, loop, latch...), shared by the voices */
    env_cfg_t ncfg;          /* the noise EG's */
    float drift_coef;        /* drift's smoothing per control step */
    float wheel, aftertouch, bend;   /* 0..1, 0..1, -1..1 (times the bend range) */
    float gain_coef, glide;  /* per sample / per control step */
    int phase;               /* samples into the current control step */
    float link_off;          /* LINK CUTOFF: the SVF cutoff knob minus the ladder's when link went on */
    int held[MAX_HELD];      /* keys down, oldest first (mono: releasing the newest falls back to the one before) */
    int nheld;
    int sostenuto;           /* the sostenuto pedal (CC 66) is down */
    int hold;                /* HOLD as last applied (turning it off lets go of the notes it latched) */
    float tune_hz;           /* A above middle C: master tune with fine tune */
    float shift, center;     /* transpose + octave shift, and the key track centre, in whole semitones */
    float track[NOSC];       /* each oscillator's pitch tracking - 1 (0 at 100%) */
    float bend_off[NOSC][2]; /* each oscillator's bend up / down offset, whole semitones */
    float mac_st;            /* the MACRO channel's octave and frequency knobs, semitones */
    /* THE PERFORMANCE SECTION (arpeggiator, chord memory, generative steps): see its functions below */
    int route;               /* where keys go (ROUTE_*), as last applied */
    int seq_on;              /* the step clock plays (arp, Euclid or Turing machine on) */
    int gen_bypass;          /* a generated note's release: HOLD and the pedals don't hold its voice */
    uint8_t ocnt[128];       /* generated notes sounding, per note (overlapping chords share a voice) */
    uint8_t cmem[128][CHORD_MAX], cmem_n[128];   /* the chord notes each key started (chord memory without the arp) */
    seq_keys_t keys;         /* the keys the step clock plays from, oldest first */
    int phys;                /* of them, keys still physically down */
    int running;             /* a phrase is playing (keys held or latched) */
    int start_pending;       /* a phrase began: line its first step up at the next render */
    int16_t aseq[SEQ_MAX];   /* the arpeggiator's whole pattern, its velocities, length, one pass's length, place */
    uint8_t avel[SEQ_MAX];
    int alen, abase, apos, adirty, alast;
    uint8_t eu[EU_MAX];      /* the Euclidean pattern, rotation applied (1 = hit) */
    int eu_n;
    uint8_t tm_reg[TM_MAX];  /* the Turing machine's loop */
    int tm_pos;
    uint32_t grng;           /* the generative choices' random numbers */
    struct { int64_t at; int note; } pend[128];  /* generated notes' releases, by sample */
    int npend;
    int64_t now;             /* samples rendered since create() */
    /* the step clock: beat position pos0 + clk * bps; steps fall on k * step (+ swing on odd k) */
    double pos0, bps, step, swoff;
    int64_t clk, next_k, k0;
    int follow;              /* the clock follows the host's song position (sync on, transport playing) */
    double sp_ppq, sp_bpm;   /* the host's last song_pos() */
    int sp_playing, sp_new;
    int learning, learn_fresh, learn_n;   /* CHORD LEARN: on, waiting for its first key, the keys so far */
    int learn[CHORD_MAX];
    unsigned changed;        /* params the engine set itself (bit i: CHANGED_KEY[i]), for mpc_engine_changed() */
    struct { int64_t t; uint8_t note, vel; } glog[128];   /* the last generated notes (get_param "gen_log", for tests) */
    int glog_head, glog_n;
    /* THE STEP SEQUENCER (src/stepseq_run.h): the sequence and its playback; STEP MOD's knob-to-destination map */
    sq_run_t sq;
    short sq_md[P_COUNT];    /* the matrix destination a knob's move records into (-1: none) ... */
    float sq_sc[P_COUNT];    /* ... and how far one unit of the knob moves it (1 = the destination's whole +-100%) */
    char sq_text[SQ_TEXT_MAX];   /* get_state's encoding of the sequence */
    int heals;               /* how often heal() ran (src/synths.h) */
    /* THE SYNTHS (src/synths.h): this Synth in its instance */
    struct inst *in;         /* the instance, and this Synth's place in it (0: Synth 1) */
    int idx;
    const float *ext;        /* this piece's audio input (mono, the EXT channel's signal), NULL while it is silent */
    const short *pidx;       /* each slot's params.json entry for this Synth (its own copies; the shared ones Synth 1's) */
    int on;                  /* ON, as last applied */
    uint8_t kin[128];        /* the keys it took (each key's release goes where the key went) */
    int64_t quiet;           /* samples since a voice of it last sounded */
    int moved;               /* LOAD INTO SYNTH changed its values: the host is told (mpc_engine_changed) */
    tuning_t tun;            /* MICROTUNING (src/tuning.h): a Scala scale and keyboard map, saved with the sound */
    ph_run_t ph;             /* THE BOUNCING BALLS AND PARTICLES (src/physgen_run.h; runtime only, never saved) */
} one_t;
static void sq_init(one_t *s);   /* src/stepseq_run.h */
static int pool_room(one_t *s, const int *idx, int n);   /* src/synths.h */
static unsigned next_age(one_t *s);
static void synth_switch(one_t *s);
static void ph_apply(one_t *s);   /* src/physgen_run.h */

static int slot(const char *key) {
    for (int i = 0; i < P_COUNT; i++)
        if (KEYS[i][0] && !strcmp(KEYS[i], key)) return i;
    return -1;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline float white(uint32_t *x) {   /* xorshift32: -1..1 */
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return (int32_t)*x * (1.0f / 2147483648.0f);
}

/* the retired keys an older project may hold (1: it was one) */
static int store_retired(one_t *s, const char *key, const char *text) {
    float v = (float)atof(text);
    if (!strcmp(key, "dly_pp") && isfinite(v)) {   /* retired: the ping-pong switch is now master effect 1's type */
        s->val[P_DLY_TYPE] = v >= 0.5f ? MFX_PINGPONG : MFX_SDELAY;
        return 1;
    }
    if (!strcmp(key, "bend_range") && isfinite(v)) {   /* retired: an older project's one range, both ways */
        s->val[P_BEND_UP] = clampf(v, 0.0f, 24.0f);
        s->val[P_BEND_DOWN] = -s->val[P_BEND_UP];
        return 1;
    }
    return 0;
}

/* store a value in slot k's range (this Synth's params.json entry); NaN, infinities and unknown slots leave the old
 * value (a damaged project) */
static void store_k(one_t *s, int k, const char *text) {
    float v = (float)atof(text);
    if (k < 0 || s->pidx[k] < 0 || !isfinite(v)) return;
    const param_t *p = &PARAMS[s->pidx[k]];
    s->val[k] = p->nopts ? clampf(roundf(v), 0, p->nopts - 1) : clampf(v, p->min, p->max);
}

/* knob (0..100) to units */
static float cut_hz(float knob) { return CUT_MIN_HZ * powf(CUT_RANGE, knob * 0.01f); }
static float env_ms(float knob) { return powf(ENV_RANGE, knob * 0.01f); }
static float glide_ms(float knob) { return knob > 0.0f ? env_ms(knob) : 0.0f; }   /* 0, then 1 ms .. 10 s */
static int sync_div(float knob) { return (int)clampf(roundf(knob * 0.01f * (NDIV - 1)), 0, NDIV - 1); }


/* envelope times. A synced time knob picks a tempo division, longest on the right; free times are scaled by the
 * envelope's time scale (10% .. 1000%, a log knob with 100% in the middle). Delay and hold at 0 are off. */
static float env_scale(float knob) { return powf(10.0f, (knob - 50.0f) * 0.02f); }
static int env_div(float knob) { return NDIV - 1 - sync_div(knob); }
/* (2026-10-29) EG TIMES "MIN, 13 Syncs" ("eg_times" 1; 0, the default, is as before): an attack, decay or release knob
 * at its far left is MIN, shorter than 1 ms and left alone by TIME SCALE (and the voice cards); synced, the time knobs
 * pick from the manual's 13 values, 1/64 to 64 beats (LFO_DIV's, longest on the right) */
#define ENV_MIN_MS 0.2f
static int eg13(const one_t *s) { return s->val[P_EG_TIMES] > 0.5f; }
static int env_div13(float knob) { return NLDIV - 1 - (int)clampf(roundf(knob * 0.01f * (NLDIV - 1)), 0, NLDIV - 1); }
static float env_sync_beats(const one_t *s, float knob) {
    return eg13(s) ? LFO_DIV[env_div13(knob)].beats : SYNC_DIV[env_div(knob)].beats;
}
static const char *env_sync_name(const one_t *s, float knob) {
    return eg13(s) ? LFO_DIV[env_div13(knob)].name : SYNC_DIV[env_div(knob)].name;
}
static float env_time_scaled(const one_t *s, int n, float knob, int zero_off, float scale_knob) {
    const float *x = &s->val[P_FENV_DELAY + EX_STRIDE * n];
    if (zero_off && knob <= 0.0f) return 0.0f;
    if (x[X_SYNC] > 0.5f) return 60000.0f / s->bpm * env_sync_beats(s, knob);
    if (!zero_off && knob <= 0.0f && eg13(s)) return ENV_MIN_MS;
    return env_ms(knob) * env_scale(scale_knob);
}
static float env_time(const one_t *s, int n, float knob, int zero_off) {
    return env_time_scaled(s, n, knob, zero_off, s->val[P_FENV_SCALE + EX_STRIDE * n]);
}

/* the envelope's output at this velocity (0..1): VEL MOD AMT +100% scales it by the velocity, -100% by its inverse,
 * 0 leaves it at full */
static float env_vel(const one_t *s, int n, float velocity) {
    float a = s->val[P_FENV_VEL + EX_STRIDE * n] * 0.01f;
    return a >= 0.0f ? 1.0f - a + a * velocity : 1.0f + a * velocity;
}

/* the envelopes' settings, which every voice's envelopes read */
static void apply_envs(one_t *s) {
    for (int n = 0; n < NENV; n++) {
        const float *e = &s->val[P_ENV_A + ENV_STRIDE * n], *x = &s->val[P_FENV_DELAY + EX_STRIDE * n];
        env_cfg_t *c = &s->ecfg[n];
        const float ms[5] = {env_time(s, n, x[X_DELAY], 1), env_time(s, n, e[0], 0), env_time(s, n, x[X_HOLD], 1),
                             env_time(s, n, e[1], 0), env_time(s, n, e[3], 0)};
        env_config(c, ms, e[2] * 0.01f, x[X_ACURVE] * 0.01f, x[X_DCURVE] * 0.01f, x[X_RCURVE] * 0.01f, SR);
        c->loop = x[X_LOOP] > 0.5f;
        c->latch = x[X_LATCH] > 0.5f;
        c->multi = x[X_MULTI] > 0.5f;
        c->reset = x[X_RESET] > 0.5f;
    }
    s->env_gen++;   /* voices with modulated envelope times start again from these */
    /* the noise EG: attack and release (1 ms .. 10 s); with SUSTAIN off the release time runs as a decay to silence as
     * soon as the attack is done (the key's release then just continues it) */
    float na = env_ms(s->val[P_NOISE_A]), nr = env_ms(s->val[P_NOISE_R]);
    int sus = s->val[P_NOISE_SUS] > 0.5f;
    env_config(&s->ncfg, (const float[5]){0.0f, na, 0.0f, nr, nr}, sus ? 1.0f : 0.0f, 0.0f, 0.5f, 0.5f, SR);
}

/* the LFO slots: rate, wave, sync of LFO n, and its extras */
static int lfo_base(int n) { return n < 2 ? P_LFO1_RATE + LFO_STRIDE * n : P_LFO3_RATE + LFO_STRIDE * (n - 2); }
static const float *lfo_x(const one_t *s, int n) { return &s->val[P_LFO1_RANGE + LX_STRIDE * n]; }
static int lfo_div(float knob) { return (int)clampf(roundf(knob * 0.01f * (NLDIV - 1)), 0, NLDIV - 1); }

/* LFO n's rate at a rate knob position (0..100): three decades up from its range's lowest rate, or synced, a
 * division of the host tempo with its timing */
static float lfo_rate_hz(const one_t *s, int n, float knob) {
    const float *x = lfo_x(s, n);
    knob = clampf(knob, 0.0f, 100.0f);
    if (s->val[lfo_base(n) + 2] > 0.5f)
        return s->bpm / 60.0f / (LFO_DIV[lfo_div(knob)].beats * LFO_TIMING[(int)clampf(x[L_TIMING], 0, 2)]);
    return LFO_RANGE_HZ[(int)clampf(x[L_RANGE], 0, LR_HIGH)] * powf(LFO_RANGE, knob * 0.01f);
}
static float lfo_hz(const one_t *s, int n) { return lfo_rate_hz(s, n, s->val[lfo_base(n)]); }

/* an LFO delay or fade knob (0: off, then 1 ms .. 10 s) in samples */
static float lfo_samples(float knob) { return knob > 0.0f ? env_ms(fminf(knob, 100.0f)) * 0.001f * SR : 0.0f; }

/* ---- (2026-10-29) the step-mod list's destinations, the matrix slots' ENABLE, the pad's polarity, hold and saved
 * positions ---- */
/* which kind of per-voice setting a matrix destination reaches: an envelope's (times, sustain, switches), an LFO's
 * (rate ... fades, wave), the noise EG's */
static int md_env(int d) { return (d >= MD_FENV_DELAY && d <= MD_ENV_END) || (d >= MD_FENV_MULTI && d <= MD_ENVSW_END); }
static int md_lfo(int d) { return (d >= MD_LFO1_RATE && d <= MD_LFO_END) || (d >= MD_LFO1_WAVE && d <= MD_LFO_WAVE_END); }
static int md_nenv(int d) { return d >= MD_NOISE_A && d <= MD_NOISE_R; }
/* a switch or option list moved by modulation m (+-1 = the whole list): the setting plus m times the list's span,
 * rounded to a whole step and kept in the list */
static int md_step(float setting, float m, int nopts) {
    return (int)clampf(roundf(setting + m * (float)(nopts - 1)), 0.0f, (float)(nopts - 1));
}
/* PAD POLARITY: Bipolar makes Pad X and Y -1..1 (the centre 0) instead of 0..1 */
static int pad_bipolar(const one_t *s) { return s->val[P_PAD_POLARITY] > 0.5f; }
static float pad_src(const one_t *s, float v) { return pad_bipolar(s) ? 2.0f * v - 1.0f : v; }
/* X/Y HOLD off: lifting the finger sends the pad back to its rest (0, or the centre when bipolar) */
static void pad_release(one_t *s) {
    if (s->val[P_PAD_HOLD] > 0.5f) return;
    s->pad_x = s->pad_y = pad_bipolar(s) ? 0.5f : 0.0f;
}
/* LOAD SAVED POS ("load_pos": Off, Mod Wheel, Pad, Both; bit 0 the wheel, bit 1 the pad): a state saved with it holds
 * the mod wheel's and the pad's positions (wheel_pos, pad_x_pos, pad_y_pos, 0..100%); loading it puts them back */
static void positions_capture(one_t *s) {   /* (get_state: before the values are written) */
    int lp = (int)s->val[P_LOAD_POS];
    if (lp & 1) s->val[P_WHEEL_POS] = s->wheel * 100.0f;
    if (lp & 2) { s->val[P_PAD_X_POS] = s->pad_x * 100.0f; s->val[P_PAD_Y_POS] = s->pad_y * 100.0f; }
}
static void positions_load(one_t *s) {
    int lp = (int)s->val[P_LOAD_POS];
    if (lp & 1) s->wheel = s->val[P_WHEEL_POS] * 0.01f;
    if (lp & 2) { s->pad_x = s->val[P_PAD_X_POS] * 0.01f; s->pad_y = s->val[P_PAD_Y_POS] * 0.01f; }
}

/* the LFOs' settings that don't change per voice */
static void apply_lfos(one_t *s) {
    s->bipolar = 1u << MS_KEY | 1u << MS_RAND | 1u << MS_BEND
                 | (uint32_t)((int)s->val[P_RIBBON_ORIGIN] == RIB_CENTRE) << MS_RIBBON
                 | (uint32_t)pad_bipolar(s) * (1u << MS_PAD_X | 1u << MS_PAD_Y);
    static const int src[NLFO] = {MS_LFO1, MS_LFO2, MS_LFO3, MS_LFO4};
    for (int n = 0; n < NLFO; n++) {
        const float *x = lfo_x(s, n);
        float sm = x[L_SMOOTH] * 0.01f;
        s->lfo_coef[n] = sm > 0.0f ? 1.0f - expf(-6.2831853f * LFO_SMOOTH_HZ * powf(10.0f, -4.0f * sm) * CR / SR) : 1.0f;
        s->lfo_fade[n][0] = lfo_samples(x[L_DELAY]);
        s->lfo_fade[n][1] = lfo_samples(x[L_FADE_IN]);
        s->lfo_fade[n][2] = lfo_samples(x[L_FADE_OUT]);
        s->lfo_hz[n] = lfo_hz(s, n);
        if ((int)x[L_POLARITY] == POL_BIPOLAR) s->bipolar |= 1u << src[n];
        /* the High range's per-sample path: its smoothing per sample (2026-10-29) */
        s->lfo_coef1[n] = sm > 0.0f ? 1.0f - expf(-6.2831853f * LFO_SMOOTH_HZ * powf(10.0f, -4.0f * sm) / SR) : 1.0f;
    }
}
/* LFO n runs per sample: RANGE High, not tempo-synced (2026-10-29) */
static int lfo_high(const one_t *s, int n) {
    return s->val[lfo_base(n) + 2] < 0.5f && (int)lfo_x(s, n)[L_RANGE] == LR_HIGH;
}

/* the matrix slots in use, split into those the voices evaluate and those that reach the effects (the effect pass),
 * and whether any (or the ribbon's quick assignment) reaches envelope or LFO settings (else those per-voice paths are
 * skipped) or the effects (else the effect pass is skipped) */
static void apply_matrix(one_t *s) {
    uint8_t all[MOD_SLOTS];
    int n = mod_active(&s->val[P_MOD_BASE], &s->val[P_MODX_BASE], &s->val[P_MOD1_ON], all), rd = (int)s->val[P_RIBBON_DST];
    s->fx_stale = 1;   /* a setting changed: the next render works the effect settings out again */
    s->mod_n = s->fx_n = 0;
    s->mod_env = md_env(rd);
    s->mod_lfo = md_lfo(rd);
    s->mod_nenv = md_nenv(rd) || s->sq.nenv_lanes;
    memset(s->fx_dst, 0, sizeof s->fx_dst);
    if (rd >= MD_FX_FIRST && rd <= MD_FX_END) s->fx_dst[(rd - MD_FX_FIRST) >> 6] |= 1ull << ((rd - MD_FX_FIRST) & 63);
    for (int k = 0; k < n; k++) {
        int d = (int)s->val[P_MOD_BASE + MOD_SLOT_PARAMS * all[k] + 1];
        if (d >= MD_FX_FIRST && d <= MD_FX_END) {   /* (the MACRO destinations, past the effects, are the voices') */
            s->fx_list[s->fx_n++] = all[k];
            s->fx_dst[(d - MD_FX_FIRST) >> 6] |= 1ull << ((d - MD_FX_FIRST) & 63);
            continue;
        }
        s->mod_list[s->mod_n++] = all[k];
        s->mod_env |= md_env(d);
        s->mod_lfo |= md_lfo(d);
        s->mod_nenv |= md_nenv(d);
    }
    s->mod_env |= s->sq.env_lanes;   /* the step sequencer's modulation lanes reach them like slots */
    s->mod_lfo |= s->sq.lfo_lanes;
    s->fx_mod = 0;
    for (int w = 0; w < MDF_WORDS; w++) {
        s->fx_dst[w] |= s->sq.fx_mask[w];
        s->fx_mod |= s->fx_dst[w] != 0;
    }
}

/* ANALOG CHARACTER: each voice card's parts times the knobs, in the units the voice reads them in */
static void apply_analog(one_t *s) {
    const float *p = s->val;
    float cut = p[P_AN_CUT] * 0.01f * AN_CUT_OCT, res = p[P_AN_RES] * 0.01f * AN_RES;
    float env = p[P_AN_ENV] * 0.01f * AN_ENV_FRAC, lvl = p[P_AN_LVL] * 0.01f * AN_LVL_DB;
    float tune = p[P_AN_TUNE] * 0.01f * AN_TUNE_ST;
    for (int i = 0; i < MAX_VOICES; i++) {
        voice_t *v = &s->voice[i];
        for (int f = 0; f < 2; f++) {
            v->an_cut[f] = v->card[AC_LAD_CUT + f] * cut;
            v->an_res[f] = v->card[AC_LAD_RES + f] * res;
        }
        for (int n = 0; n < NENV; n++) v->an_env_dq[n] = log10f(1.0f + v->card[AC_ENV + n] * env);   /* 0 at 0 */
        v->an_gain = lvl > 0.0f ? exp2f(v->card[AC_LVL] * lvl / 6.0206f) : 1.0f;
        for (int n = 0; n < NOSC; n++) v->an_tune[n] = v->card[AC_TUNE + n] * tune;
    }
}

/* the performance section's settings: whether the step clock plays and the Euclidean pattern (rotated later by
 * ROTATION steps: E(3,8) rotated by 1 is .x..x..x). The arpeggiator's pattern is built again (at its next step) only
 * when an arp setting changes (set_param) or the keys do, so another knob doesn't reshuffle a Random one mid-pass. */
static void apply_perf(one_t *s) {
    const float *p = s->val;
    s->seq_on = p[P_ARP_ON] > 0.5f || p[P_GEN_EUCLID] > 0.5f || p[P_GEN_TM] > 0.5f;
    int n = (int)clampf(roundf(p[P_GEN_STEPS]), 1, EU_MAX), hits = (int)clampf(roundf(p[P_GEN_HITS]), 0, (float)n);
    int rot = (int)clampf(roundf(p[P_GEN_ROT]), 0, EU_MAX - 1) % n;
    uint8_t e[EU_MAX];
    euclid(n, hits, e);
    for (int i = 0; i < n; i++) s->eu[i] = e[(i - rot + n) % n];
    s->eu_n = n;
}

/* recompute what the audio path reads from the parameter values */
static void apply(one_t *s) {
    apply_perf(s);
    ph_apply(s);
    for (int n = 0; n < NOSC; n++) {
        const float *o = &s->val[P_O1_WAVE + OSC_STRIDE * n];
        s->ratio[n] = powf(2.0f, (o[O_OCT] - OCT_CENTRE) + o[O_FREQ] / 12.0f);
    }
    apply_envs(s);
    apply_lfos(s);
    apply_matrix(s);
    apply_analog(s);
    float t = glide_ms(s->val[P_GLIDE_TIME]) * 0.001f;
    s->glide_frac = t > 0.0f ? (float)CR / (SR * t) : 2.0f;
    s->glide_exp = 1.0f - expf(-GLIDE_EXP_K * fminf(s->glide_frac, 1.0f));
    s->tune_hz = s->val[P_MASTER_TUNE] * exp2f(s->val[P_FINE_TUNE] / 1200.0f);
    s->shift = roundf(s->val[P_TRANSPOSE]) + 12.0f * (s->val[P_OCT_SHIFT] - OCT_SHIFT_CENTRE);
    s->center = roundf(s->val[P_KEY_CENTER]);
    for (int n = 0; n < NOSC; n++) {
        const float *x = &s->val[P_O1_TRACK + XO_STRIDE * n];
        s->track[n] = (x[XO_TRACK] - 100.0f) * 0.01f;   /* exactly 0 at 100% */
        s->bend_off[n][0] = roundf(x[XO_BEND_UP]);
        s->bend_off[n][1] = roundf(x[XO_BEND_DN]);
    }
    s->mac_st = 12.0f * (s->val[P_MAC_OCT] - OCT_CENTRE) + s->val[P_MAC_FREQ];
}

/* the "voices" options: Mono, then 2 .. 16 (index i: i + 1 voices) */
static int voice_count(const one_t *s) { return (int)clampf(s->val[P_VOICES], 0, MAX_VOICES - 1) + 1; }
static int is_mono(const one_t *s) { return voice_count(s) == 1; }

/* voices stacked on each note: 1 without unison; the unison count, at most the voice count except in mono (mono +
 * unison stacks the unison count on its one note) */
static int stack_size(const one_t *s) {
    if (s->val[P_UNISON] < 0.5f) return 1;
    int n = (int)clampf(s->val[P_UNI_COUNT], 0, MAX_VOICES - 2) + 2, pool = voice_count(s);
    return is_mono(s) || n < pool ? n : pool;
}

/* the voices the allocator uses: whole stacks only (6 voices with stacks of 4 use 4) */
static int voices_used(const one_t *s) {
    int stack = stack_size(s);
    return is_mono(s) ? stack : voice_count(s) / stack * stack;
}

static float gain_target(const one_t *s, const voice_t *v) {
    return env_vel(s, ENV_AMP, v->velocity) * s->val[P_VOLUME] * 0.01f * HEADROOM * v->amp_mod * v->uni_gain * v->an_gain;
}

/* the pitch the voice plays (semitones): the glided pitch, in whole semitones with glissando, shifted by semitone
 * transpose and octave shift (they follow live, and keyboard tracking and the Keyboard source follow them too) */
static float voice_key(const one_t *s, const voice_t *v) {
    return (s->val[P_GLISSANDO] > 0.5f ? roundf(v->pitch) : v->pitch) + s->shift;
}

/* MICROTUNING: a key's pitch in semitones through the Synth's scale (src/tuning.h); without one, the key itself (the
 * same arithmetic as before, so a sound without a scale plays sample for sample as it did) */
static float tuned(const one_t *s, float key) { return s->tun.on ? tun_pitch(&s->tun, key) : key; }
/* the voice's key against the key track centre, both through the scale: pitch tracking scales this distance */
static float key_from_centre(const one_t *s, const voice_t *v) {
    return s->tun.on ? tuned(s, voice_key(s, v)) - tuned(s, s->center) : voice_key(s, v) - s->center;
}

/* the voice's base frequency: its key (through the scale, if any), pitch variance and place in the unison stack
 * (detune follows the knob live), tuned to the master tune */
static void update_note_hz(const one_t *s, voice_t *v) {
    float st = tuned(s, voice_key(s, v)) + v->pvar + v->uni_pos * s->val[P_UNI_DETUNE] * 0.01f * UNI_DETUNE_ST;
    v->note_hz = s->tune_hz * exp2f((st - 69.0f) / 12.0f);
}

/* one control step of glide towards the voice's note: constant rate (LCR, the glide time per octave), constant time
 * (LCT, every glide takes the glide time) or exponential (EXP); gated glide holds still while the key is up */
static void glide_step(const one_t *s, voice_t *v) {
    float d = (float)v->note - v->pitch;
    if (!v->gliding || d == 0.0f || s->val[P_GLIDE] < 0.5f || s->glide_frac >= 1.0f) {
        v->pitch = (float)v->note;
        v->gliding = 0;
        return;
    }
    if (s->val[P_GLIDE_GATED] > 0.5f && !v->gate) return;
    int type = (int)s->val[P_GLIDE_TYPE];
    if (type == GLIDE_EXP) {
        v->pitch += d * s->glide_exp;
        if (fabsf((float)v->note - v->pitch) < 0.005f) { v->pitch = (float)v->note; v->gliding = 0; }   /* half a cent */
        return;
    }
    float step = (type == GLIDE_LCR ? 12.0f : v->glide_span) * s->glide_frac;
    if (fabsf(d) <= step) { v->pitch = (float)v->note; v->gliding = 0; }
    else v->pitch += copysignf(step, d);
}

/* each mixer channel's level into each filter (its two buttons), the filter mix, the oscillators' wave shapes, the
 * FM depth and the filter FM amounts, with the matrix's modulation dm (MD_COUNT values) and LFO 3's value lfo3 added */
static void set_targets(const one_t *s, voice_t *v, const float *dm, float lfo3) {
    const float *p = s->val;
    float angle_lfo = lfo3 * clampf(p[P_ANGLE_LFO3] * 0.01f + dm[MD_ANGLE_LFO3], -1.0f, 1.0f) * LFO_HW_ANGLE;
    float pw_lfo = lfo3 * clampf(p[P_PW_LFO3] * 0.01f + dm[MD_PW_LFO3], -1.0f, 1.0f) * LFO_HW_PW;
    for (int k = 0; k < NSRC; k++) {
        float l = clampf(p[LEVEL_SLOT[k]] * 0.01f + dm[LEVEL_MD[k]], 0.0f, 1.0f);
        if (k == EXT) {   /* ROUTE: None, SVF, Ladder, Both */
            int r = (int)p[P_EXT_ROUTE];
            v->tgt_lad[k] = r == EXT_LADDER || r == EXT_BOTH ? l : 0.0f;
            v->tgt_svf[k] = r == EXT_SVF || r == EXT_BOTH ? l : 0.0f;
            continue;
        }
        v->tgt_lad[k] = p[LAD_SLOT[k]] > 0.5f ? l : 0.0f;
        v->tgt_svf[k] = p[SVF_SLOT[k]] > 0.5f ? l : 0.0f;
    }
    for (int n = 0; n < NOSC; n++) {
        const float *o = &p[P_O1_WAVE + OSC_STRIDE * n];
        v->tgt_shape[n] = osc_shape((int)o[O_WAVE], clampf(o[O_ANGLE] * 0.01f + dm[MD_O1_ANGLE + n] + angle_lfo, 0.0f, 1.0f),
                                    o[O_PW] * 0.01f + dm[MD_O1_PW + n] * 0.9f + pw_lfo, o[O_MIX] * 0.01f + dm[MD_O1_MIX + n],
                                    v->inc0[n]);
    }
    v->tgt_fm = clampf(p[P_FM_AMT] * 0.01f + dm[MD_FM_AMT], 0.0f, 1.0f) * FM_DEPTH;
    v->lad_fm = clampf(p[P_LAD_FM] * 0.01f + dm[MD_LAD_FM], 0.0f, 1.0f);
    v->svf_fm = clampf(p[P_SVF_FM] * 0.01f + dm[MD_SVF_FM], 0.0f, 1.0f);
    v->tgt_mix = clampf(p[P_FLT_MIX] * 0.01f + dm[MD_FLT_MIX], 0.0f, 1.0f);
    v->amp_mod = clampf(1.0f + dm[MD_AMP], 0.0f, 2.0f);
    v->mac_harm = clampf(p[P_MAC_HARM] * 0.01f + dm[MD_MAC_HARM], 0.0f, 1.0f);
    v->mac_timbre = clampf(p[P_MAC_TIMBRE] * 0.01f + dm[MD_MAC_TIMBRE], 0.0f, 1.0f);
    v->mac_morph = clampf(p[P_MAC_MORPH] * 0.01f + dm[MD_MAC_MORPH], 0.0f, 1.0f);
}

/* the MACRO channel is (or is about to be) heard: a level into either filter, at the last control step or this one */
static int mac_heard(const voice_t *v) {
    return v->tgt_lad[MACRO] != 0.0f || v->tgt_svf[MACRO] != 0.0f || v->to_lad[MACRO] != 0.0f || v->to_svf[MACRO] != 0.0f;
}

/* the MACRO channel's pitch as a MIDI note number (Plaits' unit): the voice's frequency (its note and glide, pitch
 * variance, unison detune, master tune) moved by the channel's octave and frequency knobs, by oscillator 1's pitch
 * tracking, and by `semis` (pitch modulation, bend, drift and the voice card's tuning: oscillator 1's) */
static float mac_pitch(const one_t *s, const voice_t *v, float semis) {
    return 69.0f + 12.0f * log2f(v->note_hz * (1.0f / 440.0f)) + s->mac_st + key_from_centre(s, v) * s->track[0]
           + semis;
}

/* the pitch wheel in semitones: PITCH BEND UP / DOWN for all oscillators (osc -1), or oscillator osc's BEND UP / DOWN
 * OFFSET, added to that. Each end of the wheel has its own amount; either may go up or down. */
static float bend_semis(const one_t *s, int osc) {
    const float *p = s->val;
    if (s->bend >= 0.0f) return s->bend * (osc < 0 ? p[P_BEND_UP] : s->bend_off[osc][0]);
    return -s->bend * (osc < 0 ? p[P_BEND_DOWN] : s->bend_off[osc][1]);
}

/* each oscillator's frequency: the note times its octave and frequency settings and the pitch modulation (semitones,
 * shared and per oscillator), plus its beat frequency in Hz (the knob and the matrix). PITCH TRACKING scales the
 * key's distance from the key track centre (100%: as played, 0%: the centre note whatever the key) */
static void set_pitch(const one_t *s, voice_t *v, float semis, const float *osc_semis) {
    float from_centre = key_from_centre(s, v);
    for (int n = 0; n < NOSC; n++) {
        float track = from_centre * s->track[n];
        float hz = v->note_hz * s->ratio[n] * exp2f((semis + osc_semis[n] + track) / 12.0f) + s->val[P_O1_BEAT + OSC_STRIDE * n]
                   + v->beat_mod[n];
        osc_set_freq(&v->osc[n], hz, SR);
        v->inc0[n] = v->osc[n].inc;
    }
}

/* the two noise colours' gains: COLOR MIX (plus `mix_mod`, 0..1 units) crossfades them at equal power, so an even mix
 * of two independent noises is as loud as either; with NOISE ENV on the noise EG shapes both */
static void noise_targets(const one_t *s, voice_t *v, float mix_mod) {
    float m = clampf(s->val[P_NOISE_COLOR_MIX] * 0.01f + mix_mod, 0.0f, 1.0f);
    float e = s->val[P_NOISE_ENV] > 0.5f ? v->noise_env : 1.0f;
    if (m == v->nz_m && e == v->nz_e) return;   /* unchanged (the usual case): no square roots */
    v->nz_m = m;
    v->nz_e = e;
    v->tgt_nz[0] = sqrtf(1.0f - m) * e;
    v->tgt_nz[1] = sqrtf(m) * e;
}

/* SPREAD: where each note sits around the pan, -1..1. A unison stack fans out by its place in the stack; otherwise
 * each new note takes the next place in a pattern that alternates sides and varies the width. One voice (mono, no
 * unison) has nothing to spread and stays on the pan. */
static float spread_place(const one_t *s, const voice_t *v) {
    static const float ORDER[8] = {-1.0f, 1.0f, -0.5f, 0.5f, -0.75f, 0.75f, -0.25f, 0.25f};
    if (s->val[P_UNISON] > 0.5f) return v->uni_pos;
    return is_mono(s) ? 0.0f : ORDER[v->age % 8];
}

/* the voice's left and right gains: PAN plus its spread place (plus `pan_mod`, -1..1 units), equal power with both at
 * 1 in the centre (so a centred voice sounds exactly as a mono one did); hard left is sqrt 2 and 0. The place glides
 * over about 10 ms (a stolen voice moving sides doesn't click); `snap` puts it there at once (a note from silence). */
static void pan_targets(const one_t *s, voice_t *v, float pan_mod, int snap) {
    float to = clampf((s->val[P_PAN] + s->val[P_SPREAD] * spread_place(s, v)) * 0.01f + pan_mod, -1.0f, 1.0f);
    if (!snap && to == v->pan_pos) return;   /* in place (the usual case): no square roots */
    v->pan_pos = snap || fabsf(to - v->pan_pos) < 1e-5f ? to : v->pan_pos + (to - v->pan_pos) * s->glide;
    v->tgt_pl = sqrtf(1.0f - v->pan_pos);
    v->tgt_pr = sqrtf(1.0f + v->pan_pos);
    if (snap) { v->pl = v->tgt_pl; v->pr = v->tgt_pr; }
}

/* ANALOG DRIFT, once per control step while it is up: each wander eases (a one-pole, DRIFT_TAU_S) towards a random
 * target in -1..1 and picks a new one every 0.3-1.5 s, so it stays within -1..1. out: the wanders times the amount
 * (0..1: the knob with the matrix's drift modulation). */
static void drift_step(const one_t *s, voice_t *v, float *out, float amt) {
    for (int k = 0; k < NDRIFT; k++) {
        if (--v->drift_left[k] <= 0) {
            v->drift_tgt[k] = white(&v->drift_rng);
            float u = 0.5f + 0.5f * white(&v->drift_rng);
            v->drift_left[k] = (int)((DRIFT_MIN_S + DRIFT_VAR_S * u) * SR / CR);
        }
        v->drift[k] += (v->drift_tgt[k] - v->drift[k]) * s->drift_coef;
        out[k] = v->drift[k] * amt;
    }
}

/* THE RIBBON. Its position arrives as CC 16 (0..127, 64 the middle) and is smoothed at control rate (about 10 ms), so
 * the CC's steps are not heard; CC 17 says whether a finger is on it (>= 64) or has lifted. Its value, the Ribbon
 * source: with the origin at the centre -1..1, exactly 0 at 64; with the origin at the left 0..1 from the left end.
 * SPRING takes it back to the origin over about 50 ms once the finger lifts (and on CC 121, reset controllers);
 * HOLD keeps the last position (a controller mapped to CC 16 without CC 17 acts the same in either mode). */
static float ribbon_origin(const one_t *s) { return (int)s->val[P_RIBBON_ORIGIN] == RIB_LEFT ? 0.0f : 64.0f; }
static float ribbon_value(const one_t *s) {
    if ((int)s->val[P_RIBBON_ORIGIN] == RIB_LEFT) return s->rib * (1.0f / 127.0f);
    return (s->rib - 64.0f) * (s->rib < 64.0f ? 1.0f / 64.0f : 1.0f / 63.0f);
}
static void ribbon_rest(one_t *s) {
    s->rib_to = ribbon_origin(s);
    s->rib_back = 1;
}
static void ribbon_step(one_t *s) {   /* once per control step */
    float d = s->rib_to - s->rib;
    s->rib = fabsf(d) < 1e-3f ? s->rib_to : s->rib + d * (s->rib_back ? s->rib_coef : s->glide);
}
static void ribbon_cc(one_t *s, int cc, int v) {
    if (cc == RIBBON_CC) {
        s->rib_to = (float)v;
        s->rib_back = 0;
    } else {
        s->rib_touch = v >= 64;
        if (!s->rib_touch && (int)s->val[P_RIBBON_MODE] == RIB_SPRING) ribbon_rest(s);
    }
}

static void load_defaults(one_t *s) {
    for (int k = 0; k < P_COUNT; k++) {
        if (s->pidx[k] < 0) continue;
        const param_t *p = &PARAMS[s->pidx[k]];
        s->val[k] = p->nopts ? roundf(p->def * (p->nopts - 1)) : p->min + p->def * (p->max - p->min);
    }
}

/* one Synth of instance `in` (src/synths.h), at place idx, its params.json entries pidx */
static one_t *synth_create(struct inst *in, int idx, const short *pidx) {
    one_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->in = in;
    s->idx = idx;
    s->pidx = pidx;
    fx_touch(&s->fx, sizeof s->fx);   /* the effect buffers' pages, mapped now rather than in the audio thread */
    load_defaults(s);
    memcpy(s->def, s->val, sizeof s->def);
    for (int i = 0; i < MAX_VOICES; i++) {
        voice_t *v = &s->voice[i];
        for (int n = 0; n < NOSC; n++) osc_reset(&v->osc[n]);
        v->noise = 0x9e3779b9u + 0x6d2b79f5u * (uint32_t)i;   /* each voice its own noise */
        v->note = -1;
        v->amp_mod = 1.0f;
        v->uni_gain = 1.0f;
        for (int n = 0; n < NENV; n++) v->env[n].cfg = &s->ecfg[n];
        for (int n = 0; n < NLFO; n++) lfo_init(&v->lfo[n], 0x2545f491u * (n + 1) + 0x632be5abu * (uint32_t)(i + 1));
        v->nenv.cfg = &s->ncfg;
        v->drift_rng = 0x85ebca6bu + 0x27d4eb2fu * (uint32_t)i;   /* never 0: xorshift would stick */
        v->pl = v->pr = v->tgt_pl = v->tgt_pr = 1.0f;
        v->nz_m = -1.0f;   /* nothing worked out yet */
        v->cut = s->val[P_FLT_CUT];
        v->svf_cut = s->val[P_SVF_CUT];
        v->mac_note = 60.0f;
        if (!(v->mac = macro_create())) {   /* out of memory: no instance */
            for (int j = 0; j < i; j++) macro_destroy(s->voice[j].mac);
            free(s);
            return NULL;
        }
    }
    /* the voice cards, as in an analog polysynth where no two voices match: for each part, the first eight cards take
     * the eight evenly spaced values -1 .. 1 in an order shuffled per part (so at 100% the whole range is in use, and a
     * card's parts don't follow each other); cards 9-16 take the eight values -0.875 .. 0.875 (between those), shuffled
     * with a second seed, so the first eight are exactly the cards of the
     * 8-voice version (old sounds keep their voices). A fixed seed: every instance, and every session, has the same cards. */
    uint32_t rng = 0x3c6ef372u, rng2 = 0x9b05688cu;
    for (int c = 0; c < AC_COUNT; c++) {
        for (int half = 0; half < 2; half++) {
            int order[CARD_SET];
            uint32_t *r = half ? &rng2 : &rng;
            for (int i = 0; i < CARD_SET; i++) order[i] = i;
            for (int i = CARD_SET - 1; i > 0; i--) {   /* Fisher-Yates */
                white(r);
                int j = (int)(*r % (uint32_t)(i + 1)), t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
            for (int i = 0; i < CARD_SET; i++)
                s->voice[half * CARD_SET + i].card[c] =
                    half ? (2.0f * order[i] + 1.0f) / CARD_SET - 1.0f : 2.0f * order[i] / (CARD_SET - 1) - 1.0f;
        }
    }
    for (int n = 0; n < NLFO; n++) lfo_init(&s->lfo[n], 0x2545f491u * (n + 1));
    s->bpm = 120.0f;
    s->voices = voices_used(s);
    s->link_off = s->val[P_SVF_CUT] - s->val[P_FLT_CUT];
    s->gain_coef = env_coef(5.0f, SR);
    s->glide = 1.0f - expf(-(float)CR / (GLIDE_MS * 0.001f * SR));
    s->drift_coef = 1.0f - expf(-(float)CR / (DRIFT_TAU_S * SR));
    s->fx_coef = 1.0f - expf(-(float)CR / (FX_MOD_MS * 0.001f * SR));
    s->rib_coef = 1.0f - expf(-3.0f * CR / (RIBBON_RETURN_MS * 0.001f * SR));   /* 95% back in that time */
    for (int k = 0; k < P_COUNT; k++) s->fx_of[k] = s->fx_bank[k] = -1;
    for (int b = 0; b < 2; b++)
        for (int f = 0; f < BK_COUNT; f++) { s->fx_bank[BANK[b][f]] = (signed char)b; s->fx_bankf[BANK[b][f]] = (signed char)f; }
    for (int k = 0; k < MDF_COUNT; k++) {
        s->fx_of[FXMD_SLOT[k]] = (short)k;
        for (int i = 0; i < NPARAMS; i++)
            if (!strcmp(PARAMS[i].key, KEYS[FXMD_SLOT[k]])) { s->fx_lo[k] = PARAMS[i].min; s->fx_hi[k] = PARAMS[i].max; }
    }
    sq_init(s);   /* the step sequencer: an empty sequence; STEP MOD's map (after the effect ranges) */
    tun_clear(&s->tun);   /* no scale: the usual tuning */
    s->grng = 0x1b873593u;   /* fixed: the same generative choices in every instance, from the same start */
    for (int i = 0; i < TM_MAX; i++) s->tm_reg[i] = (uint8_t)(seq_rand(&s->grng) >> 24);
    s->bps = 120.0 / 60.0 / SR;
    s->step = 0.25;
    s->sp_ppq = -1.0;
    s->alast = -1;
    s->adirty = 1;
    apply(s);
    s->rib = s->rib_to = ribbon_origin(s);
    s->on = s->val[P_SYN_ON] > 0.5f;
    return s;
}

static void synth_destroy(one_t *s) {
    for (int i = 0; i < MAX_VOICES; i++) macro_destroy(s->voice[i].mac);
    free(s);
}

/* legato: another key is held as this note starts. The voice glides from its own last pitch when glide is on, it has
 * played before, and (with legato glide) the note is legato; else it jumps straight to the note. */
static void start_note(one_t *s, voice_t *v, int note, int legato) {
    static const float no_mod[MD_COUNT];
    int glide = s->val[P_GLIDE] > 0.5f && v->note >= 0 && (legato || s->val[P_GLIDE_LEGATO] < 0.5f);
    v->dying = 0;   /* (a voice the pool was taking away is this Synth's again) */
    if (glide) v->glide_span = fabsf((float)note - v->pitch);
    else v->pitch = (float)note;
    v->gliding = glide && v->glide_span > 0.0f;
    v->note = note;
    update_note_hz(s, v);
    set_pitch(s, v, 0.0f, v->an_tune);   /* pitch at once (the voice card's tuning); the next control step adds the modulation */
    v->mac_note = mac_pitch(s, v, v->an_tune[0]);   /* so a drum model's hit (the trigger) lands on the new note */
    if (v->env[ENV_AMP].stage == ENV_IDLE) {   /* from silence every note starts the same way */
        for (int n = 0; n < NOSC; n++) osc_reset(&v->osc[n]);
        ladder_reset(&v->lad);
        svf2_reset(&v->svf);
        set_targets(s, v, no_mod, 0.0f);   /* unmodulated, until the next control step adds the matrix */
        v->mix_lad = v->tgt_mix;
        memcpy(v->shape, v->tgt_shape, sizeof v->shape);
        v->fm = v->tgt_fm;
        v->fm_prev = 0.0f;
        for (int n = 0; n < NENV; n++) { v->env[n].level = 0.0f; v->env[n].stage = ENV_IDLE; }
        v->nenv.level = v->noise_env = 0.0f;   /* the noise EG and its colours, and the voice's place in the stereo field */
        v->nenv.stage = ENV_IDLE;
        noise_targets(s, v, 0.0f);
        memcpy(v->nz, v->tgt_nz, sizeof v->nz);
        pan_targets(s, v, 0.0f, 1);
        v->gain = gain_target(s, v);
        v->cut = s->val[P_FLT_CUT];
        v->svf_cut = s->val[P_SVF_CUT];
        memset(v->to_lad, 0, sizeof v->to_lad);   /* the levels ramp up from 0 in the first control step */
        memset(v->to_svf, 0, sizeof v->to_svf);
        /* the filters' unmodulated settings, for the samples before the next control step works out the rest */
        ladder_set(&v->lad, cut_hz(v->cut) * exp2f(v->an_cut[0]),
                   clampf(s->val[P_FLT_RES] * 0.01f + v->an_res[0], 0.0f, 1.0f), s->val[P_FLT_DRIVE] * 0.01f,
                   (int)s->val[P_FLT_MODE], (int)s->val[P_FLT_SLOPE], SR);
        v->lad.nl = s->val[P_AN_LADDER] * 0.01f;
        svf2_set(&v->svf, cut_hz(v->svf_cut) * exp2f(v->an_cut[1]), s->val[P_SVF_SPACE] * 0.01f,
                 clampf(s->val[P_SVF_RES] * 0.01f + v->an_res[1], 0.0f, 1.0f), (int)s->val[P_SVF_MODE],
                 (int)s->val[P_SVF_PAIR], SR);
    }
}

static void forget(one_t *s, int note) {
    for (int i = 0; i < s->nheld; i++)
        if (s->held[i] == note) {
            memmove(s->held + i, s->held + i + 1, sizeof s->held[0] * (s->nheld - i - 1));
            s->nheld--;
            return;
        }
}

static float sq_dest_now(const one_t *s, int d, const float *src);   /* src/stepseq_run.h */
/* envelope n's MULTI TRIG for a note starting now: the setting, or with modulation reaching it, the matrix's (from the
 * last control step) and the sequencer's lanes as they are on this step */
static int env_multi(const one_t *s, const voice_t *v, int n) {
    if (!s->mod_env) return v->env[n].cfg->multi;
    int d = MD_FENV_MULTI + MDS_COUNT * n + MDS_MULTI;
    return md_step(s->val[P_FENV_MULTI + EX_STRIDE * n], v->multi_mat[n] + sq_dest_now(s, d, v->src), 2);
}
static void gate_on(const one_t *s, voice_t *v) {
    /* a legato note (this voice's key still down) restarts an envelope only with its MULTI TRIG on */
    int legato = v->gate && !v->sustained && v->env[ENV_AMP].stage != ENV_IDLE;
    for (int n = 0; n < NENV; n++)
        if (!legato || env_multi(s, v, n)) env_gate_on(&v->env[n]);
    if (!legato || env_multi(s, v, ENV_AMP)) {   /* the noise EG and the MACRO channel's trigger go with the amp's */
        env_gate_on(&v->nenv);
        v->mac_trig = 1;
    }
    v->gate = 1;
    v->sustained = 0;
    v->sost = 0;   /* a note played now: the sostenuto pedal, if down, doesn't hold it */
}

static void gate_off(voice_t *v) {
    for (int n = 0; n < NENV; n++) env_gate_off(&v->env[n]);
    env_gate_off(&v->nenv);
    v->gate = 0;
    v->sustained = 0;
    v->sost = 0;
}

static int hold_on(const one_t *s) { return s->val[P_HOLD] > 0.5f; }

/* a released key: held on by the sustain pedal, HOLD or (a note sounding when it went down) the sostenuto pedal, or
 * let go */
static void key_up(one_t *s, voice_t *v) {
    if (s->gen_bypass) { gate_off(v); return; }   /* a generated note's end: the arp's latch holds its keys instead */
    if (s->sustain || hold_on(s) || (s->sostenuto && v->sost)) v->sustained = 1;
    else gate_off(v);
}

/* let go of the notes held only after their keys came up, except those the sustain pedal, HOLD or the sostenuto pedal
 * (`keep_latched` 0: not HOLD; a non-legato note under HOLD clears what HOLD latched) still holds */
static void release_held(one_t *s, int keep_latched) {
    for (int i = 0; i < MAX_VOICES; i++) {
        voice_t *v = &s->voice[i];
        if (v->sustained && !s->sustain && !(keep_latched && hold_on(s)) && !(s->sostenuto && v->sost)) gate_off(v);
    }
}

/* a stack of voices (voices first .. first + stack - 1) taken as one: idle when all are, held when any is */
typedef struct { int idle, gated, sustained; float level, loud; unsigned age; } stack_info_t;
static stack_info_t stack_state(const one_t *s, int first, int stack) {
    stack_info_t st = {1, 0, 0, 0.0f, 0.0f, 0};
    for (int i = first; i < first + stack; i++) {
        const voice_t *v = &s->voice[i];
        st.idle &= v->env[ENV_AMP].stage == ENV_IDLE;
        st.gated |= v->gate;
        st.sustained |= v->sustained;
        st.level = fmaxf(st.level, v->env[ENV_AMP].level);
        st.loud = fmaxf(st.loud, v->env[ENV_AMP].level * v->gain);   /* what it adds to the sound */
        if (v->age > st.age) st.age = v->age;
    }
    return st;
}

/* the first voice of the stack for a new note, among the voices in use (whole stacks; one voice each without
 * unison), or -1 to drop the note: the stack already playing this key, else an idle one, else by VOICE STEALING MODE
 * (a stolen stack restarts from its current level, no click):
 *   Oldest    the quietest releasing one, else the oldest one held only by a pedal or HOLD, else the oldest held one
 *   Quietest  the one quietest in the sound now (level times velocity and volume), held or not
 *   Newest    the quietest releasing one, else the newest held one (a line over a held chord keeps taking one voice)
 *   No Stealing  the quietest releasing one, else none: the note waits for a free voice (it is dropped) */
static int pick_stack(one_t *s, int note) {
    int stack = stack_size(s), used = voices_used(s), best = -1, mode = (int)s->val[P_STEAL_MODE];
    stack_info_t b = {0};
    for (int i = 0; i < used; i += stack)
        if (s->voice[i].note == note && !stack_state(s, i, stack).idle) return i;
    for (int i = 0; i < used; i += stack)
        if (stack_state(s, i, stack).idle) return i;
    if (mode == STEAL_QUIETEST) {
        for (int i = 0; i < used; i += stack) {
            stack_info_t st = stack_state(s, i, stack);
            if (best < 0 || st.loud < b.loud) { best = i; b = st; }
        }
        return best;
    }
    for (int i = 0; i < used; i += stack) {
        stack_info_t st = stack_state(s, i, stack);
        if (!st.gated && (best < 0 || st.level < b.level)) { best = i; b = st; }
    }
    if (best >= 0 || mode == STEAL_NONE) return best;
    if (mode == STEAL_NEWEST) {
        for (int i = 0; i < used; i += stack) {
            stack_info_t st = stack_state(s, i, stack);
            if (best < 0 || st.age > b.age) { best = i; b = st; }
        }
        return best;
    }
    for (int i = 0; i < used; i += stack) {   /* next: the oldest stack held only by the sustain pedal */
        stack_info_t st = stack_state(s, i, stack);
        if (st.sustained && (best < 0 || st.age < b.age)) { best = i; b = st; }
    }
    if (best >= 0) return best;
    for (int i = 0; i < used; i += stack) {
        stack_info_t st = stack_state(s, i, stack);
        if (best < 0 || st.age < b.age) { best = i; b = st; }
    }
    return best;
}

/* voice k of a stack of n: its place in the detune spread, -1..1, symmetric about the centre but uneven (the inner
 * voices sit closer in: u * sqrt|u|), and its share of the level */
static void stack_voice(voice_t *v, int k, int n) {
    float u = n > 1 ? 2.0f * k / (n - 1) - 1.0f : 0.0f;
    v->uni_pos = u * sqrtf(fabsf(u));
    v->uni_gain = 1.0f / sqrtf((float)n);
}

/* VELOCITY CURVE, on velocity 0..1: Medium as played; Soft louder for less effort, Hard the reverse; Compress pulls
 * the levels towards the middle (a steadier level), Expand pushes them apart (soft softer, loud louder). Every curve
 * keeps the two ends. */
static float vel_curve(const one_t *s, float v) {
    float u = 2.0f * v - 1.0f;
    switch ((int)s->val[P_VEL_CURVE]) {
    case VEL_SOFT: return sqrtf(v);
    case VEL_HARD: return v * v;
    case VEL_COMPRESS: return 0.5f + 0.5f * u * fabsf(u);
    case VEL_EXPAND: return 0.5f + 0.5f * copysignf(sqrtf(fabsf(u)), u);
    default: return v;
    }
}

/* the held key mono plays, by MONOPHONIC NOTE PRIORITY: the lowest, the highest or the newest (nheld > 0) */
static int mono_key(const one_t *s) {
    int prio = (int)s->val[P_NOTE_PRIORITY], key = s->held[s->nheld - 1];
    if (prio == PRIO_LAST) return key;
    for (int i = 0; i < s->nheld; i++)
        if (prio == PRIO_LOW ? s->held[i] < key : s->held[i] > key) key = s->held[i];
    return key;
}

/* a note-on for the voice's LFOs: each starts from the free-running one (so a free LFO carries on where it is); with
 * note-on reset or a repeat count it restarts at its start phase with a random value of its own; the delay and fades
 * start again */
static void lfo_note_on(one_t *s, voice_t *v) {
    v->hi_run = v->hi_reach = 0;   /* the High range's per-sample path waits for the next control step's values */
    for (int n = 0; n < NLFO; n++) {
        const float *x = lfo_x(s, n);
        lfo_t *l = &v->lfo[n];
        *l = s->lfo[n];
        v->lfo_own &= ~(1u << n);
        if (x[L_RESET] > 0.5f || x[L_REPEATS] >= 1.0f) {
            l->rng = (s->lfo[n].rng ^ v->noise) | 1u;   /* never 0, and each voice its own sequence */
            v->lfo_own |= 1u << n;   /* restarted here: the voice runs it from now */
            l->prev = l->next;
            l->next = lfo_rand(l);
            lfo_reset(l, x[L_PHASE] * 0.01f);
        }
        v->lfo_time[n] = 0.0f;
    }
}

/* UNISON in poly, when the keys held need more voices than whole stacks leave: the voice count shared out among the
 * held keys and the new one (each at most the unison count), as the manual has it (8 voices, count 8: one key 8, two
 * keys 4 each, three keys 2 each and the rest as they fall). The new note takes idle voices first, then released ones
 * (quietest first), then the outermost voices of the stacks bigger than its share (biggest stack first); a stack that
 * gives voices up is spread again over what it keeps. Fills idx with the new note's voices and returns how many (0:
 * none to take), or -1 when whole stacks fit (the usual allocation, pick_stack, then applies). */
static int uni_take(one_t *s, int note, int *idx) {
    int unit = stack_size(s), pool = voice_count(s), nst = 0, per, n = 0;
    unsigned ages[MAX_VOICES];
    for (int i = 0; i < pool; i++) {   /* the stacks of held keys (another key than this one) */
        const voice_t *v = &s->voice[i];
        if (!v->gate || v->env[ENV_AMP].stage == ENV_IDLE || v->note == note) continue;
        int seen = 0;
        for (int k = 0; k < nst; k++) seen |= ages[k] == v->age;
        if (!seen) ages[nst++] = v->age;
    }
    per = pool / (nst + 1);
    if (per >= unit) return -1;
    if (per < 1) per = 1;
    uint32_t taken = 0;
    for (int i = 0; i < pool && n < per; i++)   /* this key's own stack, replayed */
        if (s->voice[i].note == note && s->voice[i].gate && s->voice[i].env[ENV_AMP].stage != ENV_IDLE) {
            idx[n++] = i;
            taken |= 1u << i;
        }
    for (int i = 0; i < pool && n < per; i++)
        if (s->voice[i].env[ENV_AMP].stage == ENV_IDLE && !(taken >> i & 1)) { idx[n++] = i; taken |= 1u << i; }
    while (n < per) {   /* released voices, quietest first */
        int best = -1;
        for (int i = 0; i < pool; i++) {
            const voice_t *v = &s->voice[i];
            if ((taken >> i & 1) || v->gate) continue;
            if (best < 0 || v->env[ENV_AMP].level < s->voice[best].env[ENV_AMP].level) best = i;
        }
        if (best < 0) break;
        idx[n++] = best;
        taken |= 1u << best;
    }
    uint32_t moved = 0;   /* stacks that gave voices up (bit: their first voice) */
    while (n < per) {   /* the biggest stack over its share gives up its outermost voice */
        int big = -1, bigc = per;
        unsigned bage = 0;
        for (int k = 0; k < nst; k++) {
            int c = 0;
            for (int i = 0; i < pool; i++) c += !(taken >> i & 1) && s->voice[i].gate && s->voice[i].age == ages[k];
            if (c > bigc || (c == bigc && big >= 0 && ages[k] < bage)) { big = k; bigc = c; bage = ages[k]; }
        }
        if (big < 0) break;
        int out = -1;
        for (int i = 0; i < pool; i++) {
            const voice_t *v = &s->voice[i];
            if ((taken >> i & 1) || !v->gate || v->age != ages[big]) continue;
            if (out < 0 || fabsf(v->uni_pos) > fabsf(s->voice[out].uni_pos)) out = i;
        }
        idx[n++] = out;
        taken |= 1u << out;
        moved |= 1u << big;
    }
    for (int k = 0; k < nst; k++) {   /* the stacks that gave voices up: spread again over the voices they keep */
        if (!(moved >> k & 1)) continue;
        int keep[MAX_VOICES], c = 0;
        for (int i = 0; i < pool; i++)
            if (!(taken >> i & 1) && s->voice[i].gate && s->voice[i].age == ages[k]) keep[c++] = i;
        for (int j = 0; j < c; j++) stack_voice(&s->voice[keep[j]], j, c);
    }
    return n;
}

static void note_on(one_t *s, int note, int vel) {
    if (s->nheld == 0) release_held(s, 0);   /* a non-legato note under HOLD (no pedal down) replaces what it latched */
    forget(s, note);              /* a repeated key moves to the top instead of being held twice */
    if (s->nheld == MAX_HELD) forget(s, s->held[0]);   /* drop the oldest key */
    s->held[s->nheld++] = note;
    int legato = s->nheld > 1, first = 0, stack = stack_size(s), idx[MAX_VOICES];
    if (is_mono(s)) {   /* the priority key: a new key that loses to a held one changes nothing */
        int key = mono_key(s);
        if (key != note && s->voice[0].gate && s->voice[0].note == key) return;
        note = key;
    } else if (stack > 1 && (first = uni_take(s, note, idx)) >= 0) {
        if (!(stack = first)) return;   /* nothing to take */
        first = -1;
    } else if ((first = pick_stack(s, note)) < 0) {
        return;   /* No Stealing and every voice held */
    }
    if (first >= 0)
        for (int k = 0; k < stack; k++) idx[k] = first + k;
    if (!pool_room(s, idx, stack)) return;   /* the instance's 16 voices: room made in any Synth, or none */
    unsigned age = next_age(s);
    float var = s->val[P_PITCH_VAR] * 0.01f;   /* cents to semitones */
    for (int k = 0; k < stack; k++) {
        voice_t *v = &s->voice[idx[k]];
        if (v->env[ENV_AMP].stage != ENV_IDLE)   /* NOTE-ON RESET (a voice from silence starts at phase 0 anyway) */
            for (int n = 0; n < NOSC; n++)
                if (s->val[P_O1_RESET + XO_STRIDE * n] > 0.5f) v->reset_pend |= 1 << n;
        v->velocity = vel_curve(s, vel / 127.0f);
        v->rand = white(&v->noise);
        v->pvar = var > 0.0f ? white(&v->noise) * var : 0.0f;   /* each voice its own, within +-the variance */
        v->rand_u = 0.5f * (white(&v->noise) + 1.0f);
        v->age = age;
        stack_voice(v, k, stack);
        start_note(s, v, note, legato);
        gate_on(s, v);
        lfo_note_on(s, v);
    }
}

static void note_off(one_t *s, int note) {
    forget(s, note);
    if (is_mono(s)) {   /* mono: on to the priority key still held, without a retrigger; else let go */
        if (s->nheld == 0) {
            for (int i = 0; i < MAX_VOICES; i++)   /* the stack, and any left from before a switch to mono */
                if (s->voice[i].gate && !s->voice[i].sustained) key_up(s, &s->voice[i]);
        } else if (mono_key(s) != s->voice[0].note) {
            int stack = stack_size(s), key = mono_key(s);
            for (int k = 0; k < stack; k++) {
                voice_t *v = &s->voice[k];
                int start = !v->gate;   /* this voice wasn't playing (a switch from poly or to unison): start it */
                if (start) stack_voice(v, k, stack);
                start_note(s, v, key, 1);
                if (start) { gate_on(s, v); lfo_note_on(s, v); }
            }
        }
        return;
    }
    for (int i = 0; i < MAX_VOICES; i++)
        if (s->voice[i].note == note && s->voice[i].gate && !s->voice[i].sustained) key_up(s, &s->voice[i]);
}

static void sustain_pedal(one_t *s, int down) {
    s->sustain = down;
    if (!down) release_held(s, 1);   /* HOLD and the sostenuto pedal keep theirs */
}

/* the sostenuto pedal (CC 66): the notes sounding as it goes down (keys held or sustained) hold on after their keys
 * come up, until it comes up; notes played while it is down don't */
static void sostenuto_pedal(one_t *s, int down) {
    if (down == s->sostenuto) return;
    s->sostenuto = down;
    if (down) {
        for (int i = 0; i < MAX_VOICES; i++) s->voice[i].sost = s->voice[i].gate;
        return;
    }
    release_held(s, 1);
    for (int i = 0; i < MAX_VOICES; i++) s->voice[i].sost = 0;
}

static void all_off(one_t *s, int hard) {
    s->nheld = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        voice_t *v = &s->voice[i];
        if (hard) {   /* All Sound Off: silent within a few ms, without a click */
            for (int n = 0; n < NENV; n++) env_kill(&v->env[n]);
            env_kill(&v->nenv);
        } else {   /* latched envelopes too */
            for (int n = 0; n < NENV; n++) env_release(&v->env[n]);
            env_release(&v->nenv);
        }
        v->gate = 0;
        v->sustained = 0;
        v->sost = 0;
    }
}

/* ==== THE PERFORMANCE SECTION =====================================================================================
 * Keys go one of four ways (ROUTE_*):
 *   plain  straight to the voices, as before this section existed (everything here off: the very same samples);
 *   chord  CHORD memory on: each key plays its chord, the key as the root (a built-in shape, or the learned one);
 *   seq    the STEP CLOCK runs (ARP on, or a GENERATIVE Euclidean rhythm or Turing machine): the keys feed it and it
 *          plays its own notes on the voices, each through chord memory too (the Moog One's order, p63: keys, then
 *          the arpeggiator, then chord). Each step's note: the Turing machine's when it is on, else the arpeggiator's,
 *          else every key at once (a Euclidean rhythm on the chord held). A Euclidean rest or a failed PROBABILITY
 *          draw plays nothing, and the arp's pattern moves on only when a note plays;
 *   learn  CHORD LEARN on: the keys play as they are and set the chord (first key the root); turning learn off stores it
 *          in chord_iv1..5 and picks the Custom shape.
 * A change of way lets go of every sound (a held key must not hang).
 * Timing: the step clock is a beat position worked out per sample (pos0 + clk x bps), so a step falls on its exact
 * sample inside a block: render() splits its work at each step and each note's end. Synced (arp_sync), it follows the
 * host: with the transport playing it takes the song position (song_pos(), HAS_SONG_POS) at each block, so steps sit
 * on the song's grid (a phrase waits for the next step; one starting up to a quarter step late plays that step at
 * once); stopped, it runs on at the host tempo. Unsynced it runs at arp_rate. With NOTE RESET on and the transport
 * stopped, the first key restarts the clock so its step plays at once; off, it waits for the next step. Latch (arp_latch
 * or HOLD) keeps the keys after they come up (legato keys add, a new set replaces); the sustain pedal keeps them while
 * down. Generated notes ignore HOLD and the pedals (their keys are latched instead). */
enum { ROUTE_PLAIN, ROUTE_CHORD, ROUTE_SEQ, ROUTE_LEARN };
static const float ARP_DIV_BEATS[8] = {4, 2, 1, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f};   /* "arp_div": 1/1 .. 1/128 */
static const float ARP_TIMING[3] = {1.0f, 1.5f, 2.0f / 3};   /* "arp_timing": straight, dotted, triplet */
enum { RND_NONREPEAT, RND_REPEAT };                          /* "arp_rnd_mode" */
#define LATE_FRAC 0.25     /* a phrase starting within this fraction of a step after it plays that step at once */
#define JUMP_BEATS 1.0     /* the song position moving more than this from where the clock is: a locate or loop */
#define PEND_MAX ((int)(sizeof ((one_t *)0)->pend / sizeof ((one_t *)0)->pend[0]))
#define GLOG_MAX ((int)(sizeof ((one_t *)0)->glog / sizeof ((one_t *)0)->glog[0]))
/* the params the engine sets itself (CHORD LEARN; the sequencer's PLAY at a one shot's end or NOTE TRIG's exclusion,
 * its NOTE TRIG, its LENGTH after DUPLICATE or a factory sequence), as mpc_engine_changed() reports them */
static const int CHANGED_SLOT[CHORD_MAX + 3] = {P_CHORD_SHAPE, P_CHORD_IV1, P_CHORD_IV1 + 1, P_CHORD_IV1 + 2,
                                                P_CHORD_IV1 + 3, P_CHORD_IV1 + 4, P_SEQ_PLAY, P_SEQ_NOTE_TRIG,
                                                P_SEQ_LENGTH};

static int route_of(const one_t *s) {
    if (s->val[P_CHORD_LEARN] > 0.5f) return ROUTE_LEARN;
    if (s->seq_on || s->ph.held) return ROUTE_SEQ;   /* (the balls' HELD KEYS take the keys the same way) */
    return s->val[P_CHORD_ON] > 0.5f ? ROUTE_CHORD : ROUTE_PLAIN;
}

static void glog_add(one_t *s, int note, int vel) {
    int i;
    if (s->glog_n < GLOG_MAX) i = (s->glog_head + s->glog_n++) % GLOG_MAX;
    else { i = s->glog_head; s->glog_head = (s->glog_head + 1) % GLOG_MAX; }   /* full: the oldest goes */
    s->glog[i].t = s->now;
    s->glog[i].note = (uint8_t)note;
    s->glog[i].vel = (uint8_t)vel;
}

/* a generated note on and off: counted per note, so two chords sharing a note (or a tied step) hold its voice until
 * the last lets go; `bypass`: a step's end, which HOLD and the pedals don't hold */
static void out_on(one_t *s, int note, int vel) {
    if (note < 0 || note > 127) return;
    if (s->ocnt[note] < 255) s->ocnt[note]++;
    glog_add(s, note, vel);
    note_on(s, note, vel);
}
static void out_off(one_t *s, int note, int bypass) {
    if (note < 0 || note > 127 || !s->ocnt[note] || --s->ocnt[note]) return;
    glog_add(s, note, 0);
    s->gen_bypass = bypass;
    note_off(s, note);
    s->gen_bypass = 0;
}

/* the notes a root plays: itself, and with CHORD on the shape's notes above (or around) it, root first, no repeats */
static int chord_notes(const one_t *s, int root, int *out) {
    int n = 0, shape = (int)s->val[P_CHORD_SHAPE];
    out[n++] = root;
    if (s->val[P_CHORD_ON] < 0.5f) return n;
    for (int i = 1; i < CHORD_MAX; i++) {
        int iv = shape == 0 ? (int)roundf(s->val[P_CHORD_IV1 + i - 1]) : i < CHORD_SIZE[shape] ? CHORD_SHAPE[shape][i] : 0;
        if (!iv) continue;
        int dup = 0;
        for (int j = 0; j < n; j++) dup |= out[j] == root + iv;
        if (!dup) out[n++] = root + iv;
    }
    return n;
}

static void sq_forget(one_t *s);   /* src/stepseq_run.h */

/* stop everything this section started and forget its keys (a change of way, All Notes Off); the step sequencer's
 * notes are generated notes too */
static void gen_stop(one_t *s) {
    s->gen_bypass = 1;
    for (int n = 0; n < 128; n++)
        if (s->ocnt[n]) { s->ocnt[n] = 0; note_off(s, n); }
    s->gen_bypass = 0;
    s->npend = 0;
    s->ph.nq = 0;   /* the balls' and particles' notes still waiting */
    sq_forget(s);
    memset(s->cmem_n, 0, sizeof s->cmem_n);
    s->keys.n = s->phys = s->running = s->start_pending = 0;
}

/* chord memory on direct keys: the chord each key started is kept, so it ends as it began whatever the shape now */
static void chord_key_off(one_t *s, int key) {
    for (int i = 0; i < s->cmem_n[key]; i++) out_off(s, s->cmem[key][i], 0);
    s->cmem_n[key] = 0;
}
static void chord_key_on(one_t *s, int key, int vel) {
    int out[CHORD_MAX], n = chord_notes(s, key, out);
    chord_key_off(s, key);   /* a repeated key (no note-off between) */
    for (int i = 0; i < n; i++)
        if (out[i] >= 0 && out[i] <= 127) { s->cmem[key][s->cmem_n[key]++] = (uint8_t)out[i]; out_on(s, out[i], vel); }
}

/* CHORD LEARN: the first key clears the last chord and is the root; up to five more keys add to it */
static void learn_key(one_t *s, int note) {
    if (s->learn_fresh) { s->learn_n = 0; s->learn_fresh = 0; }
    for (int i = 0; i < s->learn_n; i++)
        if (s->learn[i] == note) return;
    if (s->learn_n < CHORD_MAX) s->learn[s->learn_n++] = note;
}
static void learn_commit(one_t *s) {   /* learn off: the chord, as intervals from its root, becomes the Custom shape */
    if (s->learn_fresh || s->learn_n == 0) return;   /* nothing played: the old chord stays */
    for (int i = 1; i < CHORD_MAX; i++)
        s->val[P_CHORD_IV1 + i - 1] = i < s->learn_n ? clampf((float)(s->learn[i] - s->learn[0]), -24.0f, 24.0f) : 0.0f;
    s->val[P_CHORD_SHAPE] = 0.0f;
    s->changed |= (1u << CHORD_MAX) - 1;   /* the host's controls follow (mpc_engine_changed) */
}

/* ---- the step clock ---- */
static double step_t(const one_t *s, int64_t k) { return (double)k * s->step + ((k & 1) ? s->swoff : 0.0); }
static double clock_pos(const one_t *s) { return s->pos0 + (double)s->clk * s->bps; }
static void clock_anchor(one_t *s, double pos) { s->pos0 = pos; s->clk = 0; }
static int64_t first_step_from(const one_t *s, double p) {   /* the first step at or after beat position p */
    int64_t k = (int64_t)floor(p / s->step) - 1;
    while (step_t(s, k) < p - 1e-9) k++;
    return k;
}
static int64_t until(const one_t *s, double t) {   /* samples from now to beat position t (0 or less: due now) */
    return (int64_t)ceil((t - s->pos0) / s->bps - 1e-6) - s->clk;
}

/* once per render while the clock plays: tempo, step length and swing, the host's song position, a new phrase's start */
static void clock_block(one_t *s) {
    const float *p = s->val;
    int sync = p[P_ARP_SYNC] > 0.5f;
    double bpm = sync ? (s->sp_bpm > 0.0 ? s->sp_bpm : (double)s->bpm) : (double)clampf(p[P_ARP_RATE], 10.0f, 280.0f);
    double bps = bpm / 60.0 / SR, now_pos = clock_pos(s);
    double step = ARP_DIV_BEATS[(int)clampf(p[P_ARP_DIV], 0, 7)] * ARP_TIMING[(int)clampf(p[P_ARP_TIMING], 0, 2)];
    /* SWING: the on-step's share of each pair of steps (50%: none), so odd steps come later (or earlier) */
    /* FOLLOW SWING (synced, 2026-10-29): the master swing instead of the arp's own */
    float sw = sync && p[P_ARP_FOLLOW_SWING] > 0.5f ? p[P_CLOCK_SWING] : p[P_ARP_SWING];
    double swoff = (2.0 * clampf(sw * 0.01f, 0.1f, 0.9f) - 1.0) * step;
    int restep = step != s->step || swoff != s->swoff, jump = 0;
    s->step = step;
    s->swoff = swoff;
    int follow = sync && s->sp_playing && s->sp_ppq >= 0.0;
    if (follow && s->sp_new) {   /* the song position at this block: on the song's grid, steps counted from its start */
        jump = !s->follow || fabs(now_pos - s->sp_ppq) > JUMP_BEATS;
        clock_anchor(s, s->sp_ppq);
        if (jump) { s->next_k = first_step_from(s, s->sp_ppq - 0.002); s->k0 = 0; }   /* (0.002: the host's jitter) */
    } else if (bps != s->bps) {
        clock_anchor(s, now_pos);   /* a new tempo from here on */
    }
    s->bps = bps;
    s->follow = follow;
    s->sp_new = 0;
    if (restep && !jump) {
        s->next_k = first_step_from(s, clock_pos(s));
        if (!follow) s->k0 = s->next_k;
    }
    if (!s->start_pending) return;
    s->start_pending = 0;
    if (!follow && p[P_ARP_RESET] > 0.5f) {   /* NOTE RESET: the phrase's first step now */
        clock_anchor(s, 0.0);
        s->next_k = s->k0 = 0;
        return;
    }
    double at = clock_pos(s);   /* else the next step, or the last one if it was only just now */
    int64_t k = first_step_from(s, at);
    if (at - step_t(s, k - 1) < LATE_FRAC * (step_t(s, k) - step_t(s, k - 1))) k--;
    s->next_k = k;
    s->k0 = follow ? 0 : k;
}

static void pend_add(one_t *s, int note, int64_t at) {
    if (s->npend == PEND_MAX) {   /* full (never in practice): the first one ends now */
        out_off(s, s->pend[0].note, 1);
        s->pend[0] = s->pend[--s->npend];
    }
    s->pend[s->npend].note = note;
    s->pend[s->npend++].at = at;
}

/* the ends due: before `now` (inclusive 0) or at it too (inclusive 1: after this sample's steps, so a 100% gate ties) */
static void pend_release(one_t *s, int inclusive) {
    for (int i = 0; i < s->npend;) {
        if (s->pend[i].at < s->now || (inclusive && s->pend[i].at == s->now)) {
            int note = s->pend[i].note;
            s->pend[i] = s->pend[--s->npend];
            out_off(s, note, 1);
        } else {
            i++;
        }
    }
}

/* one note of a step, through chord memory, with VELOCITY RANDOM; it ends at sample `off` */
static void play_step(one_t *s, int note, int vel, int64_t off) {
    float vr = s->val[P_GEN_VEL_RAND] * 0.01f;
    if (vr > 0.0f) vel = (int)clampf(roundf((float)vel + (seq_rand01(&s->grng) * 2.0f - 1.0f) * vr * 64.0f), 1.0f, 127.0f);
    int out[CHORD_MAX], n = chord_notes(s, note, out);
    for (int i = 0; i < n; i++)
        if (out[i] >= 0 && out[i] <= 127) { out_on(s, out[i], vel); pend_add(s, out[i], off); }
}

static void arp_rebuild(one_t *s) {
    const float *p = s->val;
    s->alen = arp_build(&s->keys, (int)p[P_ARP_PATTERN], (int)p[P_ARP_OCT] + 1, (int)p[P_ARP_DIR], p[P_ARP_OCT_ORDER] > 0.5f,
                        p[P_ARP_PENDULUM] > 0.5f, p[P_ARP_PEND_MODE] > 0.5f, s->aseq, s->avel, &s->abase, &s->grng, s->alast);
    if (s->alen) s->apos %= s->alen;
    s->adirty = 0;
}

/* the arpeggiator's next note, -1 for none */
static int arp_next(one_t *s, int *vel) {
    const float *p = s->val;
    if (s->adirty) arp_rebuild(s);
    if (!s->alen) return -1;
    int random = (int)p[P_ARP_PATTERN] == ARP_RANDOM, i;
    if (random && (int)p[P_ARP_RND_MODE] == RND_REPEAT) {
        i = (int)(seq_rand(&s->grng) % (uint32_t)s->abase);
    } else {
        if (s->apos >= s->alen) s->apos = 0;
        i = s->apos++;
    }
    int note = s->aseq[i];
    *vel = s->avel[i];
    s->alast = note;
    if (s->apos >= s->alen) {   /* the pattern starts again: a random one in a new order */
        s->apos = 0;
        if (random && (int)p[P_ARP_RND_MODE] == RND_NONREPEAT) {
            arp_shuffle(s->aseq, s->avel, s->abase, &s->grng, note);
            s->alen = arp_tail(s->aseq, s->avel, s->abase, p[P_ARP_PENDULUM] > 0.5f, p[P_ARP_PEND_MODE] > 0.5f);
        }
    }
    return note;
}

/* step k: the Euclidean rhythm and PROBABILITY decide whether it plays; then its note(s), each held for GATE of the
 * step (its own length: swing makes pairs uneven) */
static void fire_step(one_t *s, int64_t k) {
    const float *p = s->val;
    int64_t e = k - s->k0;
    if (p[P_GEN_EUCLID] > 0.5f && !s->eu[(e % s->eu_n + s->eu_n) % s->eu_n]) return;
    float prob = p[P_GEN_PROB] * 0.01f;
    if (prob < 1.0f && seq_rand01(&s->grng) >= prob) return;
    double t0 = step_t(s, k), t1 = step_t(s, k + 1);
    int64_t off = s->now + until(s, t0 + clampf(p[P_ARP_GATE], 1.0f, 100.0f) * 0.01 * (t1 - t0));
    if (off <= s->now) off = s->now + 1;
    if (p[P_GEN_TM] > 0.5f) {
        int v = tm_step(s->tm_reg, (int)roundf(p[P_GEN_TM_LEN]), &s->tm_pos, p[P_GEN_TM_CHANGE] * 0.01f, &s->grng);
        play_step(s, tm_note(v, (int)p[P_GEN_SCALE], (int)p[P_GEN_RANGE] + 1, &s->keys), s->keys.vel[s->keys.n - 1], off);
    } else if (p[P_ARP_ON] > 0.5f) {
        int vel, note = arp_next(s, &vel);
        if (note >= 0) play_step(s, note, vel, off);
    } else {
        for (int i = 0; i < s->keys.n; i++) play_step(s, s->keys.note[i], s->keys.vel[i], off);
    }
}

/* everything due at this sample: earlier ends, the steps, then the ends at this very sample */
static void seq_due(one_t *s) {
    pend_release(s, 0);
    for (int guard = 0; s->seq_on && s->running && s->keys.n && until(s, step_t(s, s->next_k)) <= 0; guard++) {
        if (guard == 16) { s->next_k = first_step_from(s, clock_pos(s)); break; }   /* a huge jump: no burst of notes */
        fire_step(s, s->next_k);
        s->next_k++;
    }
    pend_release(s, 1);
}

/* samples until the next thing the clock does (at least 1) */
static int64_t seq_next(const one_t *s) {
    int64_t d = INT64_MAX;
    if (s->seq_on && s->running && s->keys.n) d = until(s, step_t(s, s->next_k));
    for (int i = 0; i < s->npend; i++)
        if (s->pend[i].at - s->now < d) d = s->pend[i].at - s->now;
    return d < 1 ? 1 : d;
}

/* the keys while the clock plays */
static int seq_latched(const one_t *s) { return s->val[P_ARP_LATCH] > 0.5f || hold_on(s); }
static void seq_key_on(one_t *s, int note, int vel) {
    seq_keys_t *k = &s->keys;
    int start = !s->running || k->n == 0;
    if (s->phys == 0 && k->n && seq_latched(s) && !s->sustain) { k->n = 0; start = 1; }   /* latched: a new set replaces */
    int i = seq_find(k, note);
    if (i >= 0) { s->phys -= k->down[i]; seq_remove(k, i); }   /* played again: to the end (ORDER) */
    if (k->n == SEQ_KEYS) { s->phys -= k->down[0]; seq_remove(k, 0); }
    k->note[k->n] = (int16_t)note;
    k->vel[k->n] = (uint8_t)vel;
    k->down[k->n++] = 1;
    s->phys++;
    s->adirty = 1;
    if (start) {   /* a new phrase: the pattern and the Turing machine from their start */
        s->running = s->start_pending = 1;
        s->apos = s->tm_pos = 0;
        s->alast = -1;
    }
}
static void seq_key_off(one_t *s, int note) {
    seq_keys_t *k = &s->keys;
    int i = seq_find(k, note);
    if (i < 0 || !k->down[i]) return;
    k->down[i] = 0;
    s->phys--;
    if (!seq_latched(s) && !s->sustain) seq_remove(k, i);
    s->adirty = 1;
    if (k->n == 0) s->running = 0;
}
static void seq_unlatch(one_t *s) {   /* latch or the pedal let go: the keys no longer down leave */
    if (seq_latched(s) || s->sustain) return;
    for (int i = 0; i < s->keys.n;) {
        if (s->keys.down[i]) i++;
        else seq_remove(&s->keys, i);
    }
    s->adirty = 1;
    if (s->keys.n == 0) s->running = 0;
}

#include "stepseq_run.h"   /* THE STEP SEQUENCER: part of this file */
#include "physgen_run.h"   /* THE BOUNCING BALLS AND PARTICLES: part of this file */

static void key_on(one_t *s, int note, int vel) {
    ph_key_on(s, note);   /* KEY RESTARTS: the balls and particles from their start */
    if (sq_key_on(s, note, vel)) return;   /* NOTE TRIG / TRANSPOSE keys move the sequencer instead of sounding */
    switch (s->route) {
    case ROUTE_SEQ: seq_key_on(s, note, vel); return;
    case ROUTE_CHORD: chord_key_on(s, note, vel); return;
    case ROUTE_LEARN:   /* heard as played, or with SILENT LEARN (a setting) not at all */
        learn_key(s, note);
        if (s->val[P_LEARN_MODE] < 0.5f) note_on(s, note, vel);
        return;
    default: note_on(s, note, vel); return;
    }
}
static void key_off(one_t *s, int note) {
    if (sq_key_off(s, note)) return;
    switch (s->route) {
    case ROUTE_SEQ: seq_key_off(s, note); return;
    case ROUTE_CHORD: chord_key_off(s, note); return;
    default: note_off(s, note); return;
    }
}

/* after a set_param (k: its slot, -1 for a whole state): CHORD LEARN turned off stores the chord; a change of way lets
 * go of everything; latch off lets go of the keys no longer down */
static void perf_after_set(one_t *s, int k) {
    int learn = s->val[P_CHORD_LEARN] > 0.5f;
    if (k == P_CHORD_LEARN && learn != s->learning) {
        if (learn) s->learn_fresh = 1;
        else learn_commit(s);
    }
    s->learning = learn;
    int r = route_of(s);
    if (r != s->route) {
        gen_stop(s);
        all_off(s, 0);
        s->route = r;
    }
    if (r == ROUTE_SEQ && (k == P_HOLD || k == P_ARP_LATCH || k < 0)) seq_unlatch(s);
}

/* (2026-10-29) the host's transport starts: every tempo-synced LFO goes back to its START PHASE, the free-running one
 * and each voice's own (its repeat count carries on), as the manual's Master Clock start does */
static void lfo_transport_start(one_t *s) {
    for (int n = 0; n < NLFO; n++) {
        if (s->val[lfo_base(n) + 2] < 0.5f) continue;
        float ph = lfo_x(s, n)[L_PHASE] * 0.01f;
        lfo_reset(&s->lfo[n], ph);
        for (int i = 0; i < MAX_VOICES; i++)
            if (s->voice[i].lfo_own >> n & 1) s->voice[i].lfo[n].phase = ph - floorf(ph);
    }
}

/* the host's song position at each block (HAS_SONG_POS): the step clock follows it while synced and playing */
static void synth_after(one_t *s, int k);
static void synth_song_pos(one_t *s, double ppq, double bpm, int playing, int frames) {
    (void)frames;
    int started = playing && !s->sp_playing, stopped = !playing && s->sp_playing;
    if (started) lfo_transport_start(s);
    s->sp_ppq = ppq;
    s->sp_bpm = bpm;
    s->sp_playing = playing;
    s->sp_new = 1;
    s->sq.sp_new = 1;
    s->ph.sp_at = s->now;   /* (the balls' SYNC: the sample this song position is at) */
    /* RECEIVE START/STOP (seq_host_ss, 2026-10-29): the host's start plays the sequence (ON, the Synth on, NOTE TRIG
     * off), its stop stops it; PLAY follows and the host is told */
    if (s->val[P_SEQ_HOST_SS] < 0.5f || (!started && !stopped)) return;
    int play = s->val[P_SEQ_PLAY] > 0.5f;
    if (started && !play && s->on && s->val[P_SEQ_ON] > 0.5f && s->val[P_SEQ_NOTE_TRIG] < 0.5f) play = 1;
    else if (stopped && play) play = 0;
    else return;
    s->val[P_SEQ_PLAY] = (float)play;
    s->changed |= 1u << SQ_CHG_PLAY;
    synth_after(s, P_SEQ_PLAY);
}

/* a MIDI message for this Synth (src/synths.h chose which Synths, by channel and key range) */
static void synth_midi(one_t *s, const uint8_t *m, int len) {
    if (len < 2) return;
    int st = m[0] & 0xf0, d1 = m[1] & 0x7f, d2 = len > 2 ? m[2] & 0x7f : 0;
    if (st == 0xd0) { s->aftertouch = d1 / 127.0f; return; }   /* channel pressure: two bytes */
    if (len < 3) return;
    if (st == 0x90 && d2 && s->tun.on && !s->tun.play[(int)clampf(d1 + s->shift, 0, 127)]) return;   /* a key the
                                                                     * keyboard map leaves out: silent */
    if (st == 0x90 && d2) key_on(s, d1, d2);
    else if (st == 0x80 || st == 0x90) key_off(s, d1);
    else if (st == 0xe0) s->bend = (((d2 << 7) | d1) - 8192) / 8192.0f;
    else if (st == 0xb0 && d1 == 1) s->wheel = d2 / 127.0f;
    else if (st == 0xb0 && d1 == 64) {
        sustain_pedal(s, d2 >= 64);
        if (d2 < 64 && s->route == ROUTE_SEQ) seq_unlatch(s);   /* the keys it kept in the arp leave */
    }
    else if (st == 0xb0 && d1 == 66) sostenuto_pedal(s, d2 >= 64);
    else if (st == 0xb0 && d1 == 11) s->exp1 = d2 / 127.0f;   /* expression: matrix sources Exp 1, 2 */
    else if (st == 0xb0 && d1 == 4) s->exp2 = d2 / 127.0f;
    else if (st == 0xb0 && d1 == PAD_X_CC) s->pad_x = d2 / 127.0f;
    else if (st == 0xb0 && d1 == PAD_Y_CC) s->pad_y = d2 / 127.0f;
    else if (st == 0xb0 && d1 == PAD_TOUCH_CC) {
        s->pad_gate = d2 >= 64 ? 1.0f : 0.0f;
        if (d2 < 64) pad_release(s);   /* X/Y HOLD off: back to its rest */
    }
    else if (st == 0xb0 && (d1 == RIBBON_CC || d1 == RIBBON_TOUCH_CC)) ribbon_cc(s, d1, d2);
    else if (st == 0xb0 && d1 == 121) {   /* reset all controllers */
        s->wheel = s->aftertouch = s->bend = s->exp1 = s->exp2 = s->pad_x = s->pad_y = s->pad_gate = 0.0f;
        sustain_pedal(s, 0);
        sostenuto_pedal(s, 0);
        s->rib_touch = 0;
        ribbon_rest(s);
    }
    else if (st == 0xb0 && (d1 == 120 || d1 == 123)) {
        if (s->route != ROUTE_PLAIN) gen_stop(s);   /* the arp's keys and its notes too (and the sequencer's) */
        else sq_silence(s);   /* the sequencer's notes (it plays on at its next step) */
        all_off(s, d1 == 120);
    }
}

/* LINK CUTOFF, after param k was set (-1: a whole state): with link on, a cutoff knob's move takes the other knob
 * along at the offset they had when link went on. A knob pushed to its end stays there and the offset comes back
 * once it leaves the end. A state restores both knobs as saved (store() never links). */
static void link_cutoffs(one_t *s, int k) {
    if (k == P_FLT_LINK || k < 0 || s->val[P_FLT_LINK] < 0.5f) s->link_off = s->val[P_SVF_CUT] - s->val[P_FLT_CUT];
    else if (k == P_FLT_CUT) s->val[P_SVF_CUT] = clampf(s->val[P_FLT_CUT] + s->link_off, 0.0f, 100.0f);
    else if (k == P_SVF_CUT) s->val[P_FLT_CUT] = clampf(s->val[P_SVF_CUT] - s->link_off, 0.0f, 100.0f);
}

/* the host's tempo (HAS_LFO_BPM "lfo_bpm", not a param): synced envelopes, LFOs and effects follow */
static void synth_bpm(one_t *s, float bpm) {
    if (bpm == s->bpm) return;
    s->bpm = bpm;
    apply_envs(s);
    apply_lfos(s);
    s->fx_stale = 1;
}

/* everything a change of slot k (-1: a whole state) moves */
static void synth_after(one_t *s, int k) {
    if (k == P_SEQ_PATTERN) sq_load_factory(s, (int)s->val[k]);   /* (a state restores its own steps instead) */
    link_cutoffs(s, k);
    apply(s);
    if (k < 0 || (k >= P_ARP_ON && k <= P_ARP_PEND_MODE)) s->adirty = 1;   /* what the arp's pattern is built from */
    if (k < 0 || (k >= P_WHEEL_POS && k <= P_PAD_Y_POS)) positions_load(s);   /* LOAD SAVED POS: a state, or a preset */
    /* a new origin or SPRING with no finger on the ribbon: it goes back to its (new) origin */
    if ((k < 0 || k == P_RIBBON_ORIGIN || k == P_RIBBON_MODE) && !s->rib_touch
        && (int)s->val[P_RIBBON_MODE] == RIB_SPRING)
        ribbon_rest(s);
    int count = voices_used(s);
    if (count < s->voices)   /* fewer voices: let go of the ones past the new count (they finish their release) */
        for (int i = count; i < MAX_VOICES; i++) {
            gate_off(&s->voice[i]);
            for (int n = 0; n < NENV; n++) env_release(&s->voice[i].env[n]);   /* latched ones too */
        }
    s->voices = count;
    if (s->hold && !hold_on(s)) release_held(s, 1);   /* HOLD off: the notes it latched let go (pedals keep theirs) */
    s->hold = hold_on(s);
    for (int i = 0; i < MAX_VOICES; i++)   /* LATCH off: envelopes it held open after key up now release */
        for (int n = 0; n < NENV; n++)
            if (!(s->mod_env ? s->voice[i].env[n].cfg->latch : s->ecfg[n].latch) && s->voice[i].env[n].held
                && !s->voice[i].gate)   /* (with modulation reaching an envelope: the voice's own LATCH) */
                env_release(&s->voice[i].env[n]);
    perf_after_set(s, k);
    sq_sync_params(s);
    synth_switch(s);   /* ON off: everything it plays lets go */
}

/* a param of this Synth (slot k) set from the host or the window */
static void synth_set(one_t *s, int k, const char *val) {
    if (sq_step_mod(s, k, val)) return;   /* STEP MOD: the knob's move goes into the sequence, the knob stays */
    float was = s->val[k];
    store_k(s, k, val);
    if (s->val[k] == was && k != P_SEQ_PATTERN) return;   /* unchanged (a preset sets every param): nothing moves */
    synth_after(s, k);
}

static int hz_text(char *buf, int n, float hz) {
    if (hz < 1.0f) return snprintf(buf, n, "%.2f Hz", hz);
    if (hz < 100.0f) return snprintf(buf, n, "%.1f Hz", hz);
    return hz < 1000.0f ? snprintf(buf, n, "%.0f Hz", hz) : snprintf(buf, n, "%.2f kHz", hz * 0.001f);
}

/* the synth effect's CTRL 1 (c = 0) or CTRL 2 target: its own pick, or the type's (Auto) */
static int fx_ctrl_dst(const one_t *s, int c) {
    int d = (int)s->val[P_MODFX_C1_DST + c];
    return d == FXD_AUTO ? FX_AUTO_CTRL[(int)s->val[P_MODFX_TYPE]][c] : d;
}

static int time_text(char *buf, int n, float ms) {
    if (ms < 1.0f) return snprintf(buf, n, "%.2f ms", ms);
    if (ms < 10.0f) return snprintf(buf, n, "%.1f ms", ms);
    if (ms < 1000.0f) return snprintf(buf, n, "%.0f ms", ms);
    return snprintf(buf, n, "%.2f s", ms * 0.001f);
}

/* a master slot's control (field f of BANK[b], at v) as text, in the terms of the type it runs */
static int fx_bank_text(const one_t *s, int b, int f, float v, char *buf, int n) {
    const short *K = BANK[b];
    int mt = (int)clampf(s->val[K[BK_TYPE]], 0, MFX_NTYPES - 1), st = MFX_SFX[mt], sync = s->val[K[BK_SYNC]] > 0.5f;
    switch (f) {
    case BK_GAIN: {
        float g = fx_gain_x(v);
        return g > 0.0f ? snprintf(buf, n, "%+.1f dB", 20.0f * log10f(g)) : snprintf(buf, n, "-inf dB");
    }
    case BK_TIME: case BK_TIME_R:   /* the delay types' time, or the chorus or flanger time */
        if (st < 0 || st <= SFX_ECHO || st == SFX_TPHASER || st == SFX_SDELAY || st == SFX_PINGPONG) {
            if (sync) return snprintf(buf, n, "%s", FX_DIV[fx_div(v)].name);
            return time_text(buf, n, fx_delay_ms(v));
        }
        return time_text(buf, n, fx_mod_time_ms(st, v));
    case BK_RATE: case BK_RATE_R: case BK_VRATE:
        if (sync) return snprintf(buf, n, "%s", fx_div_rate_name(v));
        return hz_text(buf, n, f == BK_VRATE ? fx_vib_hz(v) : fx_rate_hz(v));
    case BK_DECAY: {   /* the decay time at this size */
        float sz = fx_rev_size(s->val[K[BK_SIZE]]);
        if (mt == MFX_PLATE || mt == MFX_ROOM) return snprintf(buf, n, "%.2f s", fx_rev_rt60(v) * sz);
        if (mt == MFX_HALL || mt == MFX_SHIMMER) return snprintf(buf, n, "%.2f s", fx_hall_rt60(v) * sz);
        return snprintf(buf, n, "%.0f%%", v);
    }
    case BK_SIZE: return snprintf(buf, n, "%.0f%%", fx_rev_size(v) * 100.0f);
    case BK_HIFREQ: return hz_text(buf, n, fx_hifreq_hz(v));
    case BK_XOVER: return v > 0.0f ? hz_text(buf, n, fx_xover_hz(v)) : snprintf(buf, n, "Off");
    case BK_SRATE: return hz_text(buf, n, fx_srate_hz(v));
    case BK_CUT1: case BK_CUT2: case BK_CUT3: return hz_text(buf, n, fx_cut_hz(v));
    default: return 0;
    }
}

/* "<key>_display": the text MPC shows for a knob whose 0..100 position is a log scale or a tempo division */
static int display_text(const one_t *s, int k, char *buf, int n) {
    if (k >= P_PH_BALLS && k <= P_PH_DYN) return ph_display(s, k, buf, n);
    if (k == P_FLT_CUT || k == P_SVF_CUT) {
        float hz = cut_hz(s->val[k]);
        return hz < 1000.0f ? snprintf(buf, n, "%.0f Hz", hz) : snprintf(buf, n, "%.2f kHz", hz * 0.001f);
    }
    if (k >= P_ENV_A && k < P_ENV_A + NENV * ENV_STRIDE && (k - P_ENV_A) % ENV_STRIDE != 2) {   /* A, D, R: not S */
        if (s->val[P_FENV_SYNC + EX_STRIDE * ((k - P_ENV_A) / ENV_STRIDE)] > 0.5f)
            return snprintf(buf, n, "%s", env_sync_name(s, s->val[k]));
        if (s->val[k] <= 0.0f && eg13(s)) return snprintf(buf, n, "MIN");
        return time_text(buf, n, env_ms(s->val[k]));
    }
    if (k >= P_FENV_DELAY && k < P_FENV_DELAY + NENV * EX_STRIDE) {   /* delay, hold, time scale */
        int x = (k - P_FENV_DELAY) % EX_STRIDE, e = (k - P_FENV_DELAY) / EX_STRIDE;
        if (x == X_SCALE) return snprintf(buf, n, "%.0f%%", env_scale(s->val[k]) * 100.0f);
        if (x != X_DELAY && x != X_HOLD) return 0;
        if (s->val[k] <= 0.0f) return snprintf(buf, n, "0 ms");
        if (s->val[P_FENV_SYNC + EX_STRIDE * e] > 0.5f) return snprintf(buf, n, "%s", env_sync_name(s, s->val[k]));
        return time_text(buf, n, env_ms(s->val[k]));
    }
    if (k == P_KEY_CENTER || k == P_KEY_LO || k == P_KEY_HI) {   /* a note name, middle C (60) as C4, and the MIDI note number */
        static const char *const NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int note = k == P_KEY_CENTER ? (int)s->center : (int)roundf(s->val[k]);
        return snprintf(buf, n, "%s%d (%d)", NAMES[note % 12], note / 12 - 1, note);
    }
    if (k == P_NOISE_A || k == P_NOISE_R) return time_text(buf, n, env_ms(s->val[k]));
    if (k == P_GLIDE_TIME) return s->val[k] > 0.0f ? time_text(buf, n, glide_ms(s->val[k])) : snprintf(buf, n, "0 ms");
    if (k == P_MODFX_RATE || k == P_MODFX_RATE_R || k == P_MODFX_VRATE) {
        if (s->val[P_MODFX_SYNC] > 0.5f) return snprintf(buf, n, "%s", fx_div_rate_name(s->val[k]));
        return hz_text(buf, n, k == P_MODFX_VRATE ? fx_vib_hz(s->val[k]) : fx_rate_hz(s->val[k]));
    }
    if (k == P_MODFX_TIME || k == P_MODFX_TIME_R) return time_text(buf, n, fx_mod_time_ms((int)s->val[P_MODFX_TYPE], s->val[k]));
    if (k == P_MODFX_DTIME || k == P_MODFX_DTIME_R) {
        if (s->val[P_MODFX_SYNC] > 0.5f) return snprintf(buf, n, "%s", FX_DIV[fx_div(s->val[k])].name);
        return time_text(buf, n, fx_delay_ms(s->val[k]));
    }
    if (k == P_MODFX_XOVER) return s->val[k] > 0.0f ? hz_text(buf, n, fx_xover_hz(s->val[k])) : snprintf(buf, n, "Off");
    if (s->fx_bank[k] >= 0) return fx_bank_text(s, s->fx_bank[k], s->fx_bankf[k], s->val[k], buf, n);
    if (k == P_MODFX_GAIN) {
        float g = fx_gain_x(s->val[k]);
        return g > 0.0f ? snprintf(buf, n, "%+.1f dB", 20.0f * log10f(g)) : snprintf(buf, n, "-inf dB");
    }
    if (k == P_MODFX_SRATE) return hz_text(buf, n, fx_srate_hz(s->val[k]));
    if (k >= P_MODFX_CUT1 && k <= P_MODFX_CUT3) return hz_text(buf, n, fx_cut_hz(s->val[k]));
    if (k == P_MODFX_CTRL1 || k == P_MODFX_CTRL2)   /* the parameter it moves, and by how much */
        return snprintf(buf, n, "%s %+.0f%%", FXD_NAME[fx_ctrl_dst(s, k - P_MODFX_CTRL1)], s->val[k]);
    if (k == P_MODFX_DRIVE) {   /* in each Airwindows type's own terms */
        float d = s->val[k] * 0.01f;
        switch ((int)s->val[P_MODFX_TYPE]) {
        case SFX_AWTAPE: return snprintf(buf, n, "%+.1f dB", (d - 0.5f) * 24.0f);   /* input gain */
        case SFX_DENSITY: return snprintf(buf, n, "%.2f", d * 5.0f - 1.0f);        /* density, -1 .. 4 */
        default: return snprintf(buf, n, "%.0f%%", s->val[k]);
        }
    }
    for (int l = 0; l < NLFO; l++) {
        if (k == lfo_base(l)) {   /* the rate */
            if (s->val[k + 2] > 0.5f)
                return snprintf(buf, n, "%s%s", LFO_DIV[lfo_div(s->val[k])].name,
                                LFO_TIMING_TAG[(int)clampf(lfo_x(s, l)[L_TIMING], 0, 2)]);
            float hz = lfo_hz(s, l);
            return snprintf(buf, n, hz < 0.1f ? "%.3f Hz" : hz < 1.0f ? "%.2f Hz" : hz < 100.0f ? "%.1f Hz" : "%.0f Hz", hz);
        }
        int x = k - (P_LFO1_RANGE + LX_STRIDE * l);
        if (x == L_DELAY || x == L_FADE_IN || x == L_FADE_OUT)
            return s->val[k] > 0.0f ? time_text(buf, n, env_ms(s->val[k])) : snprintf(buf, n, "Off");
    }
    return 0;
}

/* a readout of this Synth (src/synths.h get_param: its key without the "s2_" prefix) */
static int synth_get(one_t *s, const char *key, char *buf, int n) {
    if (!strcmp(key, "seq_pos")) return sq_readout(s, buf, n);
    if (!strcmp(key, "bpm")) return snprintf(buf, n, "%g", s->bpm);   /* (a test readout: "s2_bpm", 2026-10-30) */
    if (!strcmp(key, "phys_view")) return ph_view_text(s, buf, n);   /* the balls and particles (the window's view) */   /* the sequencer's readout (a "display": "string" param) */
    if (!strcmp(key, "gen_log")) {   /* readouts for the tests, not params: the generated notes since the last read ... */
        int used = 0;
        buf[0] = 0;
        while (s->glog_n && used < n - 32) {
            used += snprintf(buf + used, n - used, "%lld:%d:%d;", (long long)s->glog[s->glog_head].t,
                             s->glog[s->glog_head].note, s->glog[s->glog_head].vel);
            s->glog_head = (s->glog_head + 1) % GLOG_MAX;
            s->glog_n--;
        }
        return used;
    }
    if (!strcmp(key, "voices_sounding")) {   /* ... the voices not idle (a test readout) ... */
        int c = 0;
        for (int i = 0; i < MAX_VOICES; i++) c += s->voice[i].env[ENV_AMP].stage != ENV_IDLE;
        return snprintf(buf, n, "%d", c);
    }
    if (!strcmp(key, "gen_euclid")) {   /* ... and the Euclidean pattern as x and . */
        int i = 0;
        for (; i < s->eu_n && i < n - 1; i++) buf[i] = s->eu[i] ? 'x' : '.';
        buf[i] = 0;
        return i;
    }
    size_t len = strlen(key);
    if (len > 8 && !strcmp(key + len - 8, "_display")) {
        char base[32];
        if (len - 8 >= sizeof base) return 0;
        memcpy(base, key, len - 8);
        base[len - 8] = 0;
        int k = slot(base);
        return k < 0 ? 0 : display_text(s, k, buf, n);
    }
    int k = slot(key);
    return k < 0 ? 0 : snprintf(buf, n, "%g", s->val[k]);
}

/* linear up to half scale (one oscillator at full velocity is untouched), then bends smoothly towards full scale,
 * like an analog mixer driven hard, instead of wrapping or clipping */
static inline float soft_limit(float x) {
    float a = fabsf(x);
    if (a <= 0.5f) return x;
    float u = (a - 0.5f) * 2.0f;
    return copysignf(0.5f + 0.5f * u / (1.0f + u), x);
}

/* an LFO's wave in its polarity: bipolar -1..1 or unipolar 0..1 */
static float lfo_polar(const one_t *s, int n, float y) {
    return (int)lfo_x(s, n)[L_POLARITY] == POL_UNIPOLAR ? 0.5f * (y + 1.0f) : y;
}

/* the free-running LFOs, once per control step */
static void control_lfos(one_t *s) {
    for (int n = 0; n < NLFO; n++) {
        lfo_advance(&s->lfo[n], fminf(s->lfo_hz[n] * CR / SR, 0.5f));
        float y = lfo_shape(&s->lfo[n], (int)s->val[lfo_base(n) + 1], lfo_x(s, n)[L_VAR] * 0.01f);
        s->lfo_val[n] = lfo_smooth(&s->lfo[n], lfo_polar(s, n, y), s->lfo_coef[n]);
    }
}

/* LFO n's intensity for this voice: 0 through its delay, rising through its fade in, then falling through its fade
 * out (when set), from the note-on; the matrix moves the three times (dm: the voice's LFO destinations) */
static float lfo_fade(const one_t *s, voice_t *v, int n, const float *dm, int *waiting) {
    const float *f = s->lfo_fade[n], *x = lfo_x(s, n);
    float t = v->lfo_time[n];
    if (t < 1.4e6f) v->lfo_time[n] = t + CR;   /* stops counting after about 30 s, past the longest settings */
    float d = dm[MDL_DELAY] != 0.0f ? lfo_samples(x[L_DELAY] + 100.0f * dm[MDL_DELAY]) : f[0];
    float fi = dm[MDL_FADE_IN] != 0.0f ? lfo_samples(x[L_FADE_IN] + 100.0f * dm[MDL_FADE_IN]) : f[1];
    float fo = dm[MDL_FADE_OUT] != 0.0f ? lfo_samples(x[L_FADE_OUT] + 100.0f * dm[MDL_FADE_OUT]) : f[2];
    if (t < d) { *waiting = 1; return 0.0f; }
    t -= d;
    if (t < fi) return t / fi;
    if (fo <= 0.0f) return 1.0f;
    t -= fi;
    return t < fo ? 1.0f - t / fo : 0.0f;
}

/* one voice's LFOs for this control step, with the matrix's LFO destinations from the last one. An LFO follows the
 * free-running one unless it restarts per note (note-on reset or a repeat count) or the matrix moves its rate or
 * variation: then the voice runs its own copy (taken over from the free-running one where it is, unless a note-on
 * restarted it). After its repeat count it stays at 0 until the next note-on. */
static void voice_lfos(const one_t *s, voice_t *v) {
    for (int n = 0; n < NLFO; n++) {
        const float *x = lfo_x(s, n), *dm = v->lfo_dm[n], *f = s->lfo_fade[n];
        int repeats = (int)x[L_REPEATS], waiting = 0;
        float y, g = 1.0f;
        if (f[0] > 0.0f || f[1] > 0.0f || f[2] > 0.0f
            || (s->mod_lfo && (dm[MDL_DELAY] != 0.0f || dm[MDL_FADE_IN] != 0.0f || dm[MDL_FADE_OUT] != 0.0f)))
            g = lfo_fade(s, v, n, dm, &waiting);
        float wdm = s->mod_lfo ? v->lfo_wdm[n] : 0.0f;   /* LFO WAVE as a destination (2026-10-29) */
        int wave = wdm != 0.0f ? md_step(s->val[lfo_base(n) + 1], wdm, LFO_WAVES) : (int)s->val[lfo_base(n) + 1];
        int high = lfo_high(s, n);   /* RANGE High: each voice runs it, per sample (render_voice, 2026-10-29) */
        int own = x[L_RESET] > 0.5f || repeats >= 1 || high
                  || (s->mod_lfo && (dm[MDL_RATE] != 0.0f || dm[MDL_SEMIS] != 0.0f || dm[MDL_VAR] != 0.0f || wdm != 0.0f));
        v->hi_run &= ~(1u << n);
        if (!own) {
            v->lfo_own &= ~(1u << n);
            y = s->lfo_val[n];
        } else {
            lfo_t *l = &v->lfo[n];
            if (!(v->lfo_own >> n & 1)) { *l = s->lfo[n]; v->lfo_own |= 1u << n; }
            int run = !waiting && (repeats < 1 || l->cycles < repeats);   /* a restarted LFO waits out its delay */
            float hz = 0.0f, var = x[L_VAR] * 0.01f + dm[MDL_VAR];
            if (run) {
                hz = dm[MDL_RATE] != 0.0f ? lfo_rate_hz(s, n, s->val[lfo_base(n)] + 100.0f * dm[MDL_RATE]) : s->lfo_hz[n];
                if (dm[MDL_SEMIS] != 0.0f) hz *= exp2f(dm[MDL_SEMIS] * MOD_PITCH_ST / 12.0f);
            }
            if (high) {   /* not moved on here: the samples of this step do it; this is its value now */
                v->hi_run |= (unsigned)run << n;
                v->hi_inc[n] = fminf(hz / SR, 0.49f);
                v->hi_wave[n] = wave;
                v->hi_var[n] = var;
                v->hi_g[n] = g;
                if (repeats >= 1 && l->cycles >= repeats) y = 0.0f;
                else if (s->lfo_coef1[n] < 1.0f) y = l->smooth;
                else y = lfo_polar(s, n, lfo_shape(l, wave, var));
                v->hi_y0[n] = y * g;
            } else {
                if (run) lfo_advance(l, fminf(hz * CR / SR, 0.5f));
                if (repeats >= 1 && l->cycles >= repeats) y = 0.0f;
                else y = lfo_smooth(l, lfo_polar(s, n, lfo_shape(l, wave, var)), s->lfo_coef[n]);
            }
        }
        v->lfo_out[n] = y * g;
    }
}

/* a stage time as a number that is cheap to work out and compare: log10 of the free time in ms (the knob's 1 ms .. 10 s
 * and the time scale's 10% .. 1000%, both log), 1000 + the division when synced, or ENV_Q_OFF for a delay or hold
 * at 0 */
#define ENV_Q_OFF -1e9f
#define ENV_Q_MIN 2000.0f   /* MIN (EG TIMES "MIN, 13 Syncs"); 1100 + a division: the 13-value list's */
static float env_q(const one_t *s, int n, float knob, int zero_off, float scale_knob, int sync) {
    (void)n;
    if (zero_off && knob <= 0.0f) return ENV_Q_OFF;
    if (sync) return eg13(s) ? 1100.0f + (float)env_div13(knob) : 1000.0f + (float)env_div(knob);
    if (!zero_off && knob <= 0.0f && eg13(s)) return ENV_Q_MIN;
    return 0.04f * knob + 0.02f * (scale_knob - 50.0f);
}
static float env_q_ms(const one_t *s, float q) {
    if (q <= ENV_Q_OFF * 0.5f) return 0.0f;
    if (q >= ENV_Q_MIN - 1.0f) return ENV_MIN_MS;
    if (q >= 1099.0f) return 60000.0f / s->bpm * LFO_DIV[(int)(q - 1100.0f)].beats;
    if (q >= 999.0f) return 60000.0f / s->bpm * SYNC_DIV[(int)(q - 1000.0f)].beats;
    return exp2f(q * 3.3219281f);   /* 10^q */
}

/* LATCH modulated off after the key went up (it held the envelope open): it lets go now, as LATCH switched off does.
 * Only while modulation reaches an envelope (otherwise synth_after does it when the switch moves). */
static void env_unlatch(const one_t *s, voice_t *v, int n) {
    env_t *e = &v->env[n];
    if (s->mod_env && !e->cfg->latch && e->held && !v->gate) env_release(e);
}

/* (2026-10-29) the noise EG with the matrix's NOISE EG ATTACK, SUSTAIN and RELEASE: its own settings while any of them
 * moves (worked out again when a time moves by more than 1% or the sustain switch flips), else the shared ones */
static void voice_nenv(const one_t *s, voice_t *v, const float *dm) {
    float ma = dm[MD_NOISE_A], ms = dm[MD_NOISE_SUS], mr = dm[MD_NOISE_R];
    if (!s->mod_nenv || (ma == 0.0f && ms == 0.0f && mr == 0.0f)) { v->nenv.cfg = &s->ncfg; return; }
    float a = clampf(s->val[P_NOISE_A] + 100.0f * ma, 0.0f, 100.0f), r = clampf(s->val[P_NOISE_R] + 100.0f * mr, 0.0f, 100.0f);
    float sus = (float)md_step(s->val[P_NOISE_SUS], ms, 2);
    env_cfg_t *c = &v->ncfg_v;
    if (v->nenv.cfg == c && fabsf(a - v->nq[0]) < 0.11f && fabsf(r - v->nq[1]) < 0.11f && sus == v->nq[2]
        && v->ngen == s->env_gen)
        return;   /* within 1% (the knob is 25 per decade: 1% of a time is 0.11 of it), the same switch */
    v->ngen = s->env_gen;
    v->nq[0] = a;
    v->nq[1] = r;
    v->nq[2] = sus;
    float na = env_ms(a), nr = env_ms(r);
    *c = s->ncfg;
    env_config(c, (const float[5]){0.0f, na, 0.0f, nr, nr}, sus, 0.0f, 0.5f, 0.5f, SR);
    v->nenv.cfg = c;
}

/* one voice's envelope settings with the matrix's envelope destinations: the stage times move on the time knobs'
 * scale (+-100% = the whole knob, so tempo-synced times step through the divisions), the time scale on its knob,
 * the sustain level by +-100%. An envelope without any keeps the shared settings. A stage's coefficients are worked
 * out again only when its time moves by more than 1% (an inaudible difference), so a time swept by an LFO costs a
 * few recalculations per cycle, not one every control step. The voice card's envelope times (ANALOG CHARACTER)
 * scale every free stage time of its envelopes by a fixed factor: worked out once after each settings change, then
 * nothing per control step unless the matrix moves the envelope too. */
static void voice_envs(const one_t *s, voice_t *v, const float *dm) {
    static const int stage[5] = {ENV_DELAY, ENV_ATTACK, ENV_HOLD, ENV_DECAY, ENV_RELEASE};
    static const int knob_md[5] = {MDE_DELAY, MDE_ATTACK, MDE_HOLD, MDE_DECAY, MDE_RELEASE};
    if (!s->mod_env && s->val[P_AN_ENV] <= 0.0f) {   /* no slot reaches an envelope, no card differences: shared */
        for (int n = 0; n < NENV; n++) v->env[n].cfg = &s->ecfg[n];
        return;
    }
    for (int n = 0; n < NENV; n++) {
        const float *m = &dm[MD_FENV_DELAY + MDE_COUNT * n], *sw = &dm[MD_FENV_MULTI + MDS_COUNT * n];
        const float *e = &s->val[P_ENV_A + ENV_STRIDE * n], *x = &s->val[P_FENV_DELAY + EX_STRIDE * n];
        float dq = v->an_env_dq[n];
        int any = 0, swm = 0;   /* (swm: the switches MULTI TRIG, SYNC, LOOP, LATCH are modulated, 2026-10-29) */
        for (int k = 0; k < MDE_COUNT && s->mod_env; k++) any |= m[k] != 0.0f;
        for (int k = 0; k < MDS_COUNT && s->mod_env; k++) swm |= sw[k] != 0.0f;
        if (!any && !swm && dq == 0.0f) {
            v->env[n].cfg = &s->ecfg[n];
            env_unlatch(s, v, n);
            continue;
        }
        const float knob[5] = {x[X_DELAY], e[0], x[X_HOLD], e[1], e[3]};
        int sync = swm ? md_step(x[X_SYNC], sw[MDS_SYNC], 2) : x[X_SYNC] > 0.5f;
        env_cfg_t *c = &v->ecfg[n];
        int fresh = v->env[n].cfg != c || v->egen[n] != s->env_gen;
        if (!any && !swm && !fresh && !v->env_mod[n]) continue;   /* the card's times alone, already worked out */
        v->env_mod[n] = any || swm;
        if (fresh) {   /* start from the shared settings */
            *c = s->ecfg[n];
            v->egen[n] = s->env_gen;
            for (int k = 0; k < 5; k++) v->eq[n][k] = env_q(s, n, knob[k], k == 0 || k == 2, x[X_SCALE], x[X_SYNC] > 0.5f);
        }
        const float bend[5] = {0.0f, -x[X_ACURVE] * 0.01f, 0.0f, x[X_DCURVE] * 0.01f, x[X_RCURVE] * 0.01f};
        float scale = clampf(x[X_SCALE] + 100.0f * m[MDE_SCALE], 0.0f, 100.0f);
        c->multi = swm ? md_step(x[X_MULTI], sw[MDS_MULTI], 2) : s->ecfg[n].multi;
        c->loop = swm ? md_step(x[X_LOOP], sw[MDS_LOOP], 2) : s->ecfg[n].loop;
        c->latch = swm ? md_step(x[X_LATCH], sw[MDS_LATCH], 2) : s->ecfg[n].latch;
        for (int k = 0; k < 5; k++) {
            float q = env_q(s, n, clampf(knob[k] + 100.0f * m[knob_md[k]], 0.0f, 100.0f), k == 0 || k == 2, scale, sync);
            if (q > ENV_Q_OFF * 0.5f && q < 999.0f) q += dq;   /* a free time (not off, not tempo-synced) */
            if (fabsf(q - v->eq[n][k]) < 0.0043f) continue;   /* within 1% */
            v->eq[n][k] = q;
            c->seg[stage[k]] = env_seg(env_q_ms(s, q), bend[k], SR);
        }
        c->has_delay = v->eq[n][0] > ENV_Q_OFF * 0.5f;
        c->has_hold = v->eq[n][2] > ENV_Q_OFF * 0.5f;
        c->sustain = clampf(e[2] * 0.01f + m[MDE_SUSTAIN], 0.0f, 1.0f);
        v->env[n].cfg = c;
        env_unlatch(s, v, n);
    }
}

/* (2026-10-29) the High range's per-sample reach: for each LFO running per sample, how far one unit of it moves the
 * pitch (all oscillators and each one), both cutoffs and the amp level, from the hard-wired amounts (LFO 1 to pitch,
 * LFO 2 to the cutoffs) and the matrix slots where it acts linearly (as the source or the controller of a slot with no
 * transform; not both). Everything else it reaches follows it at the control rate. hi_amp0: the amp level's
 * modulation without the High LFOs' share, which the per-sample path puts back. */
static void hi_coefs(const one_t *s, voice_t *v, const float *src, const float *dm) {
    static const int MS_OF[NLFO] = {MS_LFO1, MS_LFO2, MS_LFO3, MS_LFO4};
    const float *p = s->val;
    v->hi_reach = 0;
    v->hi_amp0 = v->hi_amp1 = 1.0f + dm[MD_AMP];
    for (int n = 0; n < NLFO; n++) {
        if (!(v->hi_run >> n & 1)) continue;
        float *k = v->hi_k[n];
        memset(k, 0, sizeof v->hi_k[n]);
        if (n == 0) k[HK_PITCH] += (p[P_PITCH_LFO] * 0.01f + dm[MD_PITCH_LFO1]) * LFO_PITCH_ST;
        if (n == 1) {
            k[HK_LAD] += (p[P_LAD_LFO] * 0.01f + dm[MD_LAD_LFO2]) * LFO_CUT_OCT;
            k[HK_SVF] += (p[P_SVF_LFO] * 0.01f + dm[MD_SVF_LFO2]) * LFO_CUT_OCT;
        }
        int ms = MS_OF[n];
        for (int j = 0; j < s->mod_n; j++) {
            int i = s->mod_list[j];
            const float *sl = &p[P_MOD_BASE + MOD_SLOT_PARAMS * i], *ex = &p[P_MODX_BASE + MOD_SLOT_EXTRA * i];
            int sr = (int)sl[0], d = (int)sl[1], via = (int)sl[3];
            if ((int)ex[1] != MX_NONE || (sr == ms && via == ms)) continue;
            int has_src = sr > MS_OFF && sr < MS_COUNT, has_via = via > MS_OFF && via < MS_COUNT;
            float c = ex[0] * 0.01f, amt = sl[2] * 0.01f, per;
            if (has_src && sr == ms) per = amt * (has_via ? (c >= 0.0f ? 1.0f - c + c * src[via] : 1.0f + c * src[via]) : 1.0f);
            else if (has_src && via == ms) per = src[sr] * c * amt;   /* the controller's gain is linear in it */
            else if (!has_src && via == ms) per = c;                  /* a controller alone: the source, its amount */
            else continue;
            if (d == MD_PITCH) k[HK_PITCH] += per * MOD_PITCH_ST;
            else if (d >= MD_O1_PITCH && d <= MD_O3_PITCH) k[HK_O1 + d - MD_O1_PITCH] += per * MOD_PITCH_ST;
            else if (d == MD_LAD_CUT) k[HK_LAD] += per * MOD_CUT_OCT;
            else if (d == MD_SVF_CUT) k[HK_SVF] += per * MOD_CUT_OCT;
            else if (d == MD_AMP) k[HK_AMP] += per;
        }
        for (int h = 0; h < HK_AMP + 1; h++)
            if (k[h] != 0.0f) v->hi_reach |= 1u << n;
        v->hi_amp0 -= v->hi_y0[n] * k[HK_AMP];
    }
}

/* one voice's modulation for the next control step: its LFOs, the matrix, pitch, both cutoffs and filter settings,
 * envelope settings, levels */
static void control_voice(one_t *s, voice_t *v) {
    const float *p = s->val;
    float *src = v->src, dm[MD_COUNT];
    glide_step(s, v);
    update_note_hz(s, v);
    voice_lfos(s, v);
    float key = voice_key(s, v);   /* the glided pitch drives keyboard tracking and the Keyboard source */
    v->noise_env = env_run(&v->nenv, CR);   /* the noise EG runs whether or not it shapes the noise (a source too) */
    src[MS_OFF] = 0.0f;
    src[MS_FENV] = env_run(&v->env[ENV_FILTER], CR) * env_vel(s, ENV_FILTER, v->velocity);
    src[MS_AENV] = v->env[ENV_AMP].level * env_vel(s, ENV_AMP, v->velocity);
    src[MS_MENV] = env_run(&v->env[ENV_MOD], CR) * env_vel(s, ENV_MOD, v->velocity);
    src[MS_LFO1] = v->lfo_out[0];
    src[MS_LFO2] = v->lfo_out[1];
    src[MS_LFO3] = v->lfo_out[2];
    src[MS_LFO4] = v->lfo_out[3];
    src[MS_VEL] = v->velocity;
    src[MS_WHEEL] = s->wheel;
    src[MS_AT] = s->aftertouch;
    float kc = key - s->center;   /* KEY TRACK CENTER: tracking pivots on it (default middle C) */
    src[MS_KEY] = kc / 60.0f;
    src[MS_RAND] = v->rand;
    src[MS_BEND] = s->bend;
    src[MS_GATE] = v->gate && !v->sustained ? 1.0f : 0.0f;   /* the key itself is down */
    src[MS_SUSTAIN] = s->sustain ? 1.0f : 0.0f;
    src[MS_EXP1] = s->exp1;
    src[MS_EXP2] = s->exp2;
    src[MS_RAND_UNI] = v->rand_u;
    src[MS_CONST] = 1.0f;
    src[MS_NOISE_EG] = v->noise_env;
    src[MS_SOSTENUTO] = s->sostenuto ? 1.0f : 0.0f;
    src[MS_RIBBON] = ribbon_value(s);
    src[MS_PAD_X] = pad_src(s, s->pad_x);
    src[MS_PAD_Y] = pad_src(s, s->pad_y);
    src[MS_PAD_GATE] = s->pad_gate;
    /* the voice's slots (none reaches an effect: those are the effect pass's); the MACRO destinations sit past them */
    memset(dm + MD_MAC_HARM, 0, sizeof(float) * (MD_COUNT - MD_MAC_HARM));
    mod_eval_list(&p[P_MOD_BASE], &p[P_MODX_BASE], s->mod_list, s->mod_n, src, s->bipolar, dm, MD_FX_FIRST);
    int rd = (int)p[P_RIBBON_DST];   /* the ribbon's quick assignment, to a voice destination */
    if (rd > MD_OFF && (rd < MD_FX_FIRST || (rd > MD_FX_END && rd < MD_COUNT)))
        dm[rd] += src[MS_RIBBON] * p[P_RIBBON_AMT] * 0.01f;
    for (int n = 0; n < NENV && s->mod_env; n++) v->multi_mat[n] = dm[MD_FENV_MULTI + MDS_COUNT * n + MDS_MULTI];
    if (s->sq.mod_live) sq_mod_voice(s, src, dm);   /* the sequencer's modulation lanes */
    memcpy(v->lfo_dm, &dm[MD_LFO1_RATE], sizeof v->lfo_dm);   /* the LFOs read these next step */
    memcpy(v->lfo_wdm, &dm[MD_LFO1_WAVE], sizeof v->lfo_wdm);
    voice_envs(s, v, dm);
    voice_nenv(s, v, dm);
    if (v->hi_run) hi_coefs(s, v, src, dm);   /* LFOs in the High range: their per-sample reach */
    else v->hi_reach = 0;
    float fenv = src[MS_FENV], key_oct = kc / 12.0f;

    float semis = v->lfo_out[0] * (p[P_PITCH_LFO] * 0.01f + dm[MD_PITCH_LFO1]) * LFO_PITCH_ST
                  + src[MS_MENV] * (p[P_PITCH_ENV] * 0.01f + dm[MD_PITCH_ENV]) * ENV_PITCH_ST
                  + bend_semis(s, -1) + dm[MD_PITCH] * MOD_PITCH_ST;
    float dr[NDRIFT] = {0};   /* analog drift, -1..1 x the amount per oscillator and for the cutoffs; off: none at all */
    float drift = clampf(p[P_DRIFT] * 0.01f + dm[MD_DRIFT], 0.0f, 1.0f);
    if (drift > 0.0f) drift_step(s, v, dr, drift);
    float osc_semis[NOSC] = {dm[MD_O1_PITCH] * MOD_PITCH_ST + bend_semis(s, 0) + dr[0] * DRIFT_ST + v->an_tune[0],
                             dm[MD_O2_PITCH] * MOD_PITCH_ST + bend_semis(s, 1) + dr[1] * DRIFT_ST + v->an_tune[1],
                             dm[MD_O3_PITCH] * MOD_PITCH_ST + bend_semis(s, 2) + dr[2] * DRIFT_ST + v->an_tune[2]};
    for (int n = 0; n < NOSC; n++) {
        v->beat_mod[n] = dm[MD_O1_BEAT + n] * BEAT_HZ;
        if (dm[MD_O1_OCT + n] != 0.0f) {   /* OCTAVE as a destination: whole octaves within the footage list */
            float o = p[P_O1_WAVE + OSC_STRIDE * n + O_OCT];
            osc_semis[n] += 12.0f * ((float)md_step(o, dm[MD_O1_OCT + n], NOCT) - o);
        }
    }
    set_pitch(s, v, semis, osc_semis);

    v->cut += (p[P_FLT_CUT] - v->cut) * s->glide;
    v->svf_cut += (p[P_SVF_CUT] - v->svf_cut) * s->glide;
    float lfo2 = v->lfo_out[1];
    float lad_oct = fenv * (p[P_LAD_ENV] * 0.01f + dm[MD_LAD_ENV]) * ENV_CUT_OCT
                    + lfo2 * (p[P_LAD_LFO] * 0.01f + dm[MD_LAD_LFO2]) * LFO_CUT_OCT
                    + key_oct * p[P_LAD_KBD] * 0.01f + dm[MD_LAD_CUT] * MOD_CUT_OCT + dr[NOSC] * DRIFT_CUT_OCT
                    + v->an_cut[0];
    float svf_oct = fenv * (p[P_SVF_ENV] * 0.01f + dm[MD_SVF_ENV]) * ENV_CUT_OCT
                    + lfo2 * (p[P_SVF_LFO] * 0.01f + dm[MD_SVF_LFO2]) * LFO_CUT_OCT
                    + key_oct * p[P_SVF_KBD] * 0.01f + dm[MD_SVF_CUT] * MOD_CUT_OCT + dr[NOSC] * DRIFT_CUT_OCT
                    + v->an_cut[1];
    ladder_set(&v->lad, cut_hz(v->cut) * exp2f(lad_oct),
               clampf(p[P_FLT_RES] * 0.01f + dm[MD_LAD_RES] + v->an_res[0], 0.0f, 1.0f),
               clampf(p[P_FLT_DRIVE] * 0.01f + dm[MD_LAD_DRIVE], 0.0f, 1.0f), (int)p[P_FLT_MODE], (int)p[P_FLT_SLOPE], SR);
    v->lad.nl = p[P_AN_LADDER] * 0.01f;
    svf2_set(&v->svf, cut_hz(v->svf_cut) * exp2f(svf_oct), clampf(p[P_SVF_SPACE] * 0.01f + dm[MD_SVF_SPACE], -1.0f, 1.0f),
             clampf(p[P_SVF_RES] * 0.01f + dm[MD_SVF_RES] + v->an_res[1], 0.0f, 1.0f), (int)p[P_SVF_MODE],
             (int)p[P_SVF_PAIR], SR);
    set_targets(s, v, dm, v->lfo_out[2]);
    if (v->hi_reach) v->amp_mod = clampf(v->hi_amp0, 0.0f, 2.0f);   /* (the High LFOs move it per sample) */
    if (mac_heard(v)) v->mac_note = mac_pitch(s, v, semis + dr[0] * DRIFT_ST + v->an_tune[0]);
    noise_targets(s, v, dm[MD_NOISE_MIX]);
    /* pan modulation, and spread's: the knob moved (within 0..100%) times the voice's place in the spread */
    float pan = dm[MD_PAN];
    if (dm[MD_SPREAD] != 0.0f)
        pan += (clampf(p[P_SPREAD] + 100.0f * dm[MD_SPREAD], 0.0f, 100.0f) - p[P_SPREAD]) * 0.01f * spread_place(s, v);
    pan_targets(s, v, pan, 0);
}

/* 2^x for the filter FM's few octaves: a 5th-order polynomial on the fraction (within 0.0002, a third of a cent) and
 * the whole part straight into the exponent; no library call */
static inline float exp2_fast(float x) {
    float xi = floorf(x), f = x - xi;
    union { float f; int32_t i; } u = {.i = ((int32_t)xi + 127) << 23};
    return u.f * (1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f)))));
}

/* a value ramped across a control step: its start (the step's target when mid-step) and its step per sample */
static inline float ramp_from(float from, float to, int first, float inv, float *step) {
    *step = first ? (to - from) * inv : 0.0f;
    return first ? from : to;
}

/* one sample of a noise colour from this sample's white noise w (purple also reads v->wprev, the one before) */
static inline float noise_colour(voice_t *v, int c, float w) {
    if (c == NC_WHITE) return w;
    if (c == NC_RED) return (v->red += (w - v->red) * RED_C) * RED_GAIN;
    return (w - v->wprev) * PURPLE_GAIN;
}

/* the noise channel for n samples of white noise wn: the pair's two colours at gains ga, gb (ramped by dga, dgb per
 * sample; a colour at gain 0 throughout is not worked out). Kept out of line: only a voice whose noise is heard calls
 * it, and inlined it would slow every voice's sample loop. */
__attribute__((noinline)) static void noise_block(voice_t *v, int ca, int cb, float ga, float gb, float dga, float dgb,
                                                  const float *wn, float *cn, int n) {
    int use_a = ga != 0.0f || dga != 0.0f, use_b = gb != 0.0f || dgb != 0.0f;
    for (int i = 0; i < n; i++) {
        float w = wn[i];
        cn[i] = (use_a ? noise_colour(v, ca, w) * ga : 0.0f) + (use_b ? noise_colour(v, cb, w) * gb : 0.0f);
        v->wprev = w;
        ga += dga;
        gb += dgb;
    }
}

/* this step's white noise (wn: the -100 dB floor, and the noise channel's source) and the noise channel (the returned
 * pointer: cn, coloured, while the channel is heard; else wn, as its levels are 0). Called before anything else in
 * render_voice, so the out-of-line call leaves the sample loop's registers alone. */
static inline const float *noise_channel(const one_t *s, voice_t *v, int first, float inv, int n, float *wn, float *cn) {
    for (int i = 0; i < n; i++) wn[i] = white(&v->noise);
    int heard = v->tgt_lad[NOISE] != 0.0f || v->tgt_svf[NOISE] != 0.0f
                || (first && (v->to_lad[NOISE] != 0.0f || v->to_svf[NOISE] != 0.0f));
    float dna, dnb;
    float na = ramp_from(v->nz[0], v->tgt_nz[0], first, inv, &dna), nb = ramp_from(v->nz[1], v->tgt_nz[1], first, inv, &dnb);
    memcpy(v->nz, v->tgt_nz, sizeof v->nz);
    if (!heard) return wn;
    const int *pair = NOISE_PAIR[(int)s->val[P_NOISE_COLOR_PAIR]];
    noise_block(v, pair[0], pair[1], na, nb, dna, dnb, wn, cn, n);
    return cn;
}

/* the MACRO channel for n samples, called only while it is heard: Plaits (its model, pitch, knobs, a pending note-on,
 * the gate) into mb. Out of line for the same reason as noise_block. */
__attribute__((noinline)) static void mac_channel(const one_t *s, voice_t *v, float *mb, int n) {
    macro_set(v->mac, (int)s->val[P_MAC_MODEL], v->mac_note, v->mac_harm, v->mac_timbre, v->mac_morph,
              s->val[P_MAC_AUX] * 0.01f);
    if (v->mac_trig) {
        macro_trigger(v->mac);
        v->mac_trig = 0;
    }
    macro_gate(v->mac, v->gate);
    macro_render(v->mac, mb, n);
}

/* (2026-10-29) the LFOs in the High range for n samples: each moves on and is read per sample (its wave, variation,
 * polarity, smoothing and fade gain as this control step has them); where it reaches pitch, a cutoff or the amp level
 * (hi_coefs), its difference from the control step's value becomes per-sample factors: each oscillator's frequency
 * (pm), the ladder's and the SVF's cutoff (lm, sm), the amp level (am). Returns which are in use: bits 0-2 the
 * oscillators, 3 the ladder, 4 the SVF, 5 the amp. Out of line: only a voice with a High LFO calls it. */
enum { HF_O1 = 1, HF_LAD = 8, HF_SVF = 16, HF_AMP = 32 };
__attribute__((noinline)) static int hi_lfo_block(const one_t *s, voice_t *v, int n, float pm[NOSC][CR], float *lm,
                                                  float *sm, float *am) {
    float dp[NOSC][CR] = {{0}}, dl[CR] = {0}, ds[CR] = {0}, da[CR] = {0};
    int used = 0;
    for (int k = 0; k < NLFO; k++) {
        if (!(v->hi_run >> k & 1)) continue;
        lfo_t *l = &v->lfo[k];
        float inc = v->hi_inc[k], coef = s->lfo_coef1[k], g = v->hi_g[k], y0 = v->hi_y0[k], var = v->hi_var[k];
        int wave = v->hi_wave[k], uni = (int)lfo_x(s, k)[L_POLARITY] == POL_UNIPOLAR, reach = v->hi_reach >> k & 1;
        if (!reach && coef >= 1.0f) { lfo_advance(l, inc * (float)n); continue; }   /* nothing heard per sample */
        const float *kk = v->hi_k[k];
        float kp[NOSC] = {kk[HK_PITCH] + kk[HK_O1], kk[HK_PITCH] + kk[HK_O2], kk[HK_PITCH] + kk[HK_O3]};
        for (int i = 0; i < n; i++) {
            lfo_advance(l, inc);
            float y = lfo_shape(l, wave, var);
            if (uni) y = 0.5f * (y + 1.0f);
            if (coef < 1.0f) y = l->smooth += (y - l->smooth) * coef;
            if (!reach) continue;
            float d = y * g - y0;
            for (int o = 0; o < NOSC; o++) dp[o][i] += d * kp[o];
            dl[i] += d * kk[HK_LAD];
            ds[i] += d * kk[HK_SVF];
            da[i] += d * kk[HK_AMP];
        }
        if (!reach) continue;
        for (int o = 0; o < NOSC; o++) used |= (kp[o] != 0.0f) << o;
        used |= (kk[HK_LAD] != 0.0f) * HF_LAD | (kk[HK_SVF] != 0.0f) * HF_SVF | (kk[HK_AMP] != 0.0f) * HF_AMP;
    }
    for (int o = 0; o < NOSC; o++)
        if (used & HF_O1 << o)
            for (int i = 0; i < n; i++) pm[o][i] = exp2_fast(dp[o][i] * (1.0f / 12.0f));
    if (used & HF_LAD) for (int i = 0; i < n; i++) lm[i] = exp2_fast(dl[i]);
    if (used & HF_SVF) for (int i = 0; i < n; i++) sm[i] = exp2_fast(ds[i]);
    if (used & HF_AMP) {
        float base = clampf(v->hi_amp0, 0.0f, 2.0f);
        if (base < 1e-4f) used &= ~HF_AMP;   /* (at no level there is nothing to move) */
        else for (int i = 0; i < n; i++) am[i] = clampf(v->hi_amp1 + da[i], 0.0f, 2.0f) / base;   /* the whole, over the rest */
    }
    return used;
}

/* render n (<= CR) samples of one voice, adding into acc (the mono sum: the mod effect's input, and the dry path of
 * every centred voice) and, for a voice off the centre, its difference from the centre into xl, xr (so the dry path
 * is acc + xl, acc + xr, and centred voices cost nothing extra); returns whether it did. Levels, wave shapes, FM
 * depth, the filter mix, the noise colours and the pan gains ramp from the last control step's values to this one's.
 * ext: the audio input for these n samples (the EXT channel), or NULL while it is silent: then EXT is not heard and
 * nothing it routes to runs on its account (no input connected changes nothing). */
static int render_voice(one_t *s, voice_t *v, const float *ext, float *acc, float *xl, float *xr, int n, int first) {
    const float *p = s->val;
    float inv = 1.0f / (float)n, out[CR], wn[CR], cn[CR];   /* the voice's output; white noise; the noise channel */
    const float *ncp = noise_channel(s, v, first, inv, n, wn, cn);
    float hp[NOSC][CR], hl[CR], hs[CR], ha[CR];   /* the High range's per-sample factors (pitch, cutoffs, amp: 2026-10-29) */
    int hf = v->hi_run ? hi_lfo_block(s, v, n, hp, hl, hs, ha) : 0;
    float lad[NSRC], svf[NSRC], dlad[NSRC], dsvf[NSRC];
    int sync2 = p[P_O2_SYNC] > 0.5f, sync3 = p[P_O3_SYNC] > 0.5f;
    int sync2_3 = sync2 && p[P_O2_SYNC_SRC] > 0.5f, sync3_2 = sync3 && p[P_O3_SYNC_SRC] > 0.5f;   /* else from osc 1 */
    int series = (int)p[P_FLT_ROUTE] == FLT_SERIES, any_svf = 0;
    int ring23 = (int)p[P_RING_SRC] == RING_2X3, route = (int)p[P_FM_ROUTE];
    int heard[NSRC], fading = 0;   /* fading: a level changes during this step */
    for (int k = 0; k < NSRC; k++) {
        lad[k] = ramp_from(v->to_lad[k], v->tgt_lad[k], first, inv, &dlad[k]);
        svf[k] = ramp_from(v->to_svf[k], v->tgt_svf[k], first, inv, &dsvf[k]);
        heard[k] = lad[k] != 0.0f || svf[k] != 0.0f || v->tgt_lad[k] != 0.0f || v->tgt_svf[k] != 0.0f;
        if (k == EXT && !ext) heard[k] = 0;   /* (silent: as if at level 0) */
        any_svf |= heard[k] && (svf[k] != 0.0f || v->tgt_svf[k] != 0.0f);
        fading |= dlad[k] != 0.0f || dsvf[k] != 0.0f;
        v->to_lad[k] = v->tgt_lad[k];
        v->to_svf[k] = v->tgt_svf[k];
    }
    int ext_on = heard[EXT];
    float mb[CR];   /* the MACRO channel: not run, and not mixed in, while it isn't heard (its levels are then 0) */
    int mac_on = heard[MACRO];
    if (mac_on) mac_channel(s, v, mb, n);
    osc_shape_t sh[NOSC];
    float da[NOSC], dpw[NOSC], dmx[NOSC], dml, dfm;
    int moving = 0;   /* a wave shape changes during this step */
    for (int k = 0; k < NOSC; k++) {
        const osc_shape_t *t = &v->tgt_shape[k];
        sh[k] = *t;
        sh[k].a = ramp_from(v->shape[k].a, t->a, first, inv, &da[k]);
        if (da[k] != 0.0f) osc_slopes(&sh[k]);
        sh[k].pw = ramp_from(v->shape[k].pw, t->pw, first, inv, &dpw[k]);
        sh[k].mix = ramp_from(v->shape[k].mix, t->mix, first, inv, &dmx[k]);
        moving |= da[k] != 0.0f || dpw[k] != 0.0f || dmx[k] != 0.0f;
        v->shape[k] = *t;
    }
    float ml = ramp_from(v->mix_lad, v->tgt_mix, first, inv, &dml);
    float fm = ramp_from(v->fm, v->tgt_fm, first, inv, &dfm);
    int fm_on = fm != 0.0f || v->tgt_fm != 0.0f;
    v->mix_lad = v->tgt_mix;
    v->fm = v->tgt_fm;
    int run_lad = series || ml != 0.0f || v->mix_lad != 0.0f;
    float dpl, dpr;   /* the pan gains */
    float pl = ramp_from(v->pl, v->tgt_pl, first, inv, &dpl), pr = ramp_from(v->pr, v->tgt_pr, first, inv, &dpr);
    v->pl = v->tgt_pl;
    v->pr = v->tgt_pr;
    /* filter FM (octaves at a full-scale sample) from oscillator lad_src / svf_src; off costs nothing per sample */
    int lad_src = (int)p[P_LAD_FM_SRC], svf_src = (int)p[P_SVF_FM_SRC];
    float lad_fm = run_lad ? v->lad_fm * FILTER_FM_OCT : 0.0f;
    float svf_fm = any_svf ? v->svf_fm * FILTER_FM_OCT : 0.0f;
    int lad_nl = v->lad.nl > 0.0f;
    /* oscillators 2 and 3 run only when heard or feeding the ring modulator, FM or filter FM (osc 1 always runs: it is
     * the sync master); osc 2 is in both ring pairs */
    int run2 = heard[1] || heard[RING] || (lad_fm > 0.0f && lad_src == 1) || (svf_fm > 0.0f && svf_src == 1);
    int run3 = heard[2] || (heard[RING] && ring23) || (fm_on && route == FM_3TO1) || (lad_fm > 0.0f && lad_src == 2)
               || (svf_fm > 0.0f && svf_src == 2);
    run2 |= run3 && sync3_2;   /* a sync source runs while its slave does */
    run3 |= run2 && sync2_3;
    if (v->reset_pend) {   /* NOTE-ON RESET: restart at phase 0 now; an oscillator synced to a restarted one follows */
        int r = v->reset_pend;
        v->reset_pend = 0;
        if (r & 1) r |= (sync2 && !sync2_3) << 1 | (sync3 && !sync3_2) << 2;
        if (r & 2 && sync3_2) r |= 4;
        if (r & 4 && sync2_3) r |= 2;
        for (int k = 0; k < NOSC; k++)
            if (r >> k & 1) osc_restart(&v->osc[k], &sh[k]);
    }
    if (!any_svf) svf2_reset(&v->svf);   /* bypassed: start clean when something is routed to it again */
    if (!run_lad) ladder_reset(&v->lad);
    float target = gain_target(s, v);
    osc_t *o1 = &v->osc[0], *o2 = &v->osc[1], *o3 = &v->osc[2];
    for (int i = 0; i < n; i++) {
        if (hf & 7) {   /* a High LFO on pitch: each oscillator's frequency this sample */
            if (hf & 1) o1->inc = clampf(v->inc0[0] * hp[0][i], 0.0f, 0.49f);
            if (hf & 2) o2->inc = clampf(v->inc0[1] * hp[1][i], 0.0f, 0.49f);
            if (hf & 4) o3->inc = clampf(v->inc0[2] * hp[2][i], 0.0f, 0.49f);
        }
        /* linear FM: the carrier's frequency moves with the modulator's output (3 > 1 uses osc 3's last sample) */
        if (fm_on && route == FM_3TO1)
            o1->inc = clampf((hf & 1 ? v->inc0[0] * hp[0][i] : v->inc0[0]) * (1.0f + fm * v->fm_prev), 0.0f, 0.49f);
        float a = osc_next(o1, &sh[0], -1.0f), wrap = o1->wrap_d;
        if (fm_on && route == FM_1TO2) o2->inc = clampf((hf & 2 ? v->inc0[1] * hp[1][i] : v->inc0[1]) * (1.0f + fm * a), 0.0f, 0.49f);
        if (fm_on && route == FM_1TO3) o3->inc = clampf((hf & 4 ? v->inc0[2] * hp[2][i] : v->inc0[2]) * (1.0f + fm * a), 0.0f, 0.49f);
        /* hard sync: osc 2 from osc 3 looks ahead to osc 3's wrap; osc 3 from osc 2 takes osc 2's wrap this sample
         * (its last one when the two sync to each other) */
        float m3 = sync3_2 ? o2->wrap_d : wrap;
        float b = run2 ? osc_next(o2, &sh[1], sync2 ? (sync2_3 ? osc_wrap_ahead(o3, sync3 ? m3 : -1.0f) : wrap) : -1.0f)
                       : 0.0f;
        float c = run3 ? osc_next(o3, &sh[2], sync3 ? (sync3_2 && !sync2_3 ? o2->wrap_d : m3) : -1.0f) : 0.0f;
        v->fm_prev = c;
        float rm = heard[RING] ? o2->tri_out * (ring23 ? o3->tri_out : o1->tri_out) : 0.0f;
        float nz = wn[i], nc = ncp[i], mc = mac_on ? mb[i] : 0.0f;
        float lad_in = a * lad[0] + b * lad[1] + c * lad[2] + nc * lad[NOISE] + rm * lad[RING];
        if (mac_on) lad_in += mc * lad[MACRO];
        if (ext_on) lad_in += ext[i] * lad[EXT];
        float svf_out = 0.0f, lad_out = 0.0f;
        if (any_svf) {
            float in = a * svf[0] + b * svf[1] + c * svf[2] + nc * svf[NOISE] + rm * svf[RING] + nz * NOISE_FLOOR;
            if (mac_on) in += mc * svf[MACRO];
            if (ext_on) in += ext[i] * svf[EXT];
            if (hf & HF_SVF)   /* a High LFO on its cutoff (with filter FM, if any) */
                svf_out = svf2_next_fm(&v->svf, in, (svf_fm > 0.0f ? exp2_fast(svf_fm * (svf_src == 0 ? a : svf_src == 1 ? b : c))
                                                                   : 1.0f) * hs[i]);
            else
                svf_out = svf_fm > 0.0f ? svf2_next_fm(&v->svf, in, exp2_fast(svf_fm * (svf_src == 0 ? a : svf_src == 1 ? b : c)))
                                        : svf2_next(&v->svf, in);
            if (series) lad_in += svf_out;
        }
        if (run_lad) {
            float in = lad_in + nz * NOISE_FLOOR;
            if (hf & HF_LAD) {   /* a High LFO on its cutoff (with filter FM, if any) */
                float e = (lad_fm > 0.0f ? exp2_fast(lad_fm * (lad_src == 0 ? a : lad_src == 1 ? b : c)) : 1.0f) * hl[i];
                lad_out = lad_nl ? ladder_next_nl_fm(&v->lad, in, e) : ladder_next_fm(&v->lad, in, e);
            } else if (lad_nl)   /* NON-LINEAR LADDER (analog character) */
                lad_out = lad_fm > 0.0f ? ladder_next_nl_fm(&v->lad, in, exp2_fast(lad_fm * (lad_src == 0 ? a : lad_src == 1 ? b : c)))
                                        : ladder_next_nl(&v->lad, in);
            else
                lad_out = lad_fm > 0.0f ? ladder_next_fm(&v->lad, in, exp2_fast(lad_fm * (lad_src == 0 ? a : lad_src == 1 ? b : c)))
                                        : ladder_next(&v->lad, in);
        }
        float f = lad_out * ml + svf_out * (1.0f - ml);
        v->gain += (target - v->gain) * s->gain_coef;
        if (fabsf(v->gain - target) < 1e-7f) v->gain = target;   /* settle exactly: no denormal tail */
        float y = f * env_next(&v->env[ENV_AMP]) * v->gain;
        if (hf & HF_AMP) y *= ha[i];   /* a High LFO on the amp level */
        acc[i] += y;
        out[i] = y;
        if (fading)
            for (int k = 0; k < NSRC; k++) { lad[k] += dlad[k]; svf[k] += dsvf[k]; }
        for (int k = 0; k < NOSC && moving; k++) {
            sh[k].pw += dpw[k];
            sh[k].mix += dmx[k];
            if (da[k] != 0.0f) { sh[k].a += da[k]; osc_slopes(&sh[k]); }   /* divides only while the angle moves */
        }
        ml += dml;
        fm += dfm;
    }
    if (pl == 1.0f && pr == 1.0f && dpl == 0.0f && dpr == 0.0f) return 0;   /* in the centre */
    pl -= 1.0f;
    pr -= 1.0f;
    for (int i = 0; i < n; i++) {   /* apart from the loop above, so this part vectorises */
        xl[i] += out[i] * (pl + dpl * (float)i);
        xr[i] += out[i] * (pr + dpr * (float)i);
    }
    return 1;
}

/* what CTRL 1/2 can move (fx.h's FXD_* order): each target's slot and its knob's range */
static const struct { short slot; float lo, hi; } FXD_SLOT[FXD_COUNT] = {
    [FXD_AUTO] = {-1, 0, 0}, [FXD_MIX] = {P_MODFX_MIX, 0, 100}, [FXD_FB] = {P_MODFX_FB, -100, 100},
    [FXD_FB_R] = {P_MODFX_FB_R, -100, 100}, [FXD_TIME] = {P_MODFX_TIME, 0, 100}, [FXD_TIME_R] = {P_MODFX_TIME_R, 0, 100},
    [FXD_DTIME] = {P_MODFX_DTIME, 0, 100}, [FXD_DFB] = {P_MODFX_DFB, 0, 100}, [FXD_RATE] = {P_MODFX_RATE, 0, 100},
    [FXD_RATE_R] = {P_MODFX_RATE_R, 0, 100}, [FXD_DEPTH] = {P_MODFX_DEPTH, 0, 100}, [FXD_TONE] = {P_MODFX_TONE, 0, 100},
    [FXD_SPREAD] = {P_MODFX_SPREAD, 0, 100}, [FXD_GAIN] = {P_MODFX_GAIN, 0, 100}, [FXD_SWEEP] = {P_MODFX_SWEEP, 0, 100},
    [FXD_NOISE] = {P_MODFX_NOISE, 0, 100}, [FXD_VRATE] = {P_MODFX_VRATE, 0, 100},
    [FXD_VDEPTH] = {P_MODFX_VDEPTH, 0, 100}, [FXD_FLUTTER] = {P_MODFX_FLUTTER, 0, 100}, [FXD_WOW] = {P_MODFX_WOW, 0, 100},
    [FXD_BIAS] = {P_MODFX_BIAS, 0, 100}, [FXD_ROOM] = {P_MODFX_ROOM, 0, 100}, [FXD_DECAY] = {P_MODFX_DECAY, 0, 100},
    [FXD_BITS] = {P_MODFX_BITS, 2, 24}, [FXD_SRATE] = {P_MODFX_SRATE, 0, 100}, [FXD_RESO] = {P_MODFX_RESO, 0, 100},
    [FXD_CUT1] = {P_MODFX_CUT1, 0, 100}, [FXD_CUT2] = {P_MODFX_CUT2, 0, 100}, [FXD_CUT3] = {P_MODFX_CUT3, 0, 100},
    [FXD_PITCH] = {P_MODFX_PITCH, -48, 48}, [FXD_STRUCT] = {P_MODFX_STRUCT, 0, 100}, [FXD_POS] = {P_MODFX_POS, 0, 100},
    [FXD_SIZE] = {P_MODFX_SIZE, 0, 100}, [FXD_DENSITY] = {P_MODFX_DENSITY, 0, 100},
    [FXD_TEXTURE] = {P_MODFX_TEXTURE, 0, 100}, [FXD_DRIVE] = {P_MODFX_DRIVE, 0, 100}, [FXD_BUMP] = {P_MODFX_BUMP, 0, 100},
    [FXD_LOWCUT] = {P_MODFX_LOWCUT, 0, 100},
    [FXD_DTIME_R] = {P_MODFX_DTIME_R, 0, 100}, [FXD_DEPTH_R] = {P_MODFX_DEPTH_R, 0, 100},
    [FXD_OFFSET] = {P_MODFX_OFFSET, 0, 100}, [FXD_FBL] = {P_MODFX_FBL, 0, 100}, [FXD_FBR] = {P_MODFX_FBR, 0, 100},
    [FXD_XOVER] = {P_MODFX_XOVER, 0, 100},
    [FXD_SIBIL] = {P_MODFX_SIBIL, 0, 100}, [FXD_SENS] = {P_MODFX_SENS, 0, 100},
};

static inline int fx_bit(const uint64_t *w, int k) { return (int)(w[k >> 6] >> (k & 63)) & 1; }

/* THE EFFECT PASS, once per control step while a matrix slot or the ribbon reaches an effect (else skipped, once its
 * modulation has settled at 0). The effects run once for all voices, so these slots are evaluated a second time
 * from global sources: the free-running LFOs, mod wheel, aftertouch, pitch bend, ribbon, X/Y pad, the pedals,
 * expression, constant; the per-voice sources (envelopes, velocity, keyboard, random, noise EG, gate) are the most
 * recently started sounding voice's, or 0 when nothing sounds. Each destination's sum is smoothed (a one-pole, FX_MOD_MS), so
 * a stepped source (S&H, a gate, the CC-driven ones) never zippers; the effects' gains then ramp across each step.
 * Sets `dirty` to the effect destinations whose settings need working out again (bit k: MDF_* k; fx_params_mod): those
 * modulated now or at the last step, and CTRL 1/2's targets when they are; returns whether there are any. */
static int fx_mod_step(one_t *s, uint64_t *dirty) {
    if (!s->fx_mod && !s->fx_live) return 0;
    float dm[MD_COUNT];
    const float *fx = dm + MD_FX_FIRST;
    memset(dm + MD_FX_FIRST, 0, sizeof(float) * MDF_COUNT);   /* (the slots here reach no other destination) */
    if (s->fx_mod) {
        const voice_t *nv = NULL;   /* the newest sounding voice */
        for (int i = 0; i < MAX_VOICES; i++) {
            const voice_t *v = &s->voice[i];
            if (v->env[ENV_AMP].stage != ENV_IDLE && (!nv || v->age > nv->age)) nv = v;
        }
        float src[MS_COUNT];
        if (nv) memcpy(src, nv->src, sizeof src);
        else memset(src, 0, sizeof src);
        static const int lfo_src[NLFO] = {MS_LFO1, MS_LFO2, MS_LFO3, MS_LFO4};
        for (int n = 0; n < NLFO; n++) src[lfo_src[n]] = s->lfo_val[n];   /* free-running, not the voice's copies */
        src[MS_WHEEL] = s->wheel;
        src[MS_AT] = s->aftertouch;
        src[MS_BEND] = s->bend;
        src[MS_SUSTAIN] = s->sustain ? 1.0f : 0.0f;
        src[MS_SOSTENUTO] = s->sostenuto ? 1.0f : 0.0f;
        src[MS_EXP1] = s->exp1;
        src[MS_EXP2] = s->exp2;
        src[MS_CONST] = 1.0f;
        src[MS_RIBBON] = ribbon_value(s);
        src[MS_PAD_X] = pad_src(s, s->pad_x);
        src[MS_PAD_Y] = pad_src(s, s->pad_y);
        src[MS_PAD_GATE] = s->pad_gate;
        mod_eval_list(&s->val[P_MOD_BASE], &s->val[P_MODX_BASE], s->fx_list, s->fx_n, src, s->bipolar, dm, 0);
        int rd = (int)s->val[P_RIBBON_DST];
        if (rd >= MD_FX_FIRST && rd <= MD_FX_END) dm[rd] += src[MS_RIBBON] * s->val[P_RIBBON_AMT] * 0.01f;
        if (s->sq.mod_live) sq_mod_fx(s, src, dm);   /* the sequencer's lanes to an effect (in fx_dst like a slot) */
    }
    int live = 0, any = 0;
    for (int w = 0; w < MDF_WORDS; w++) {
        uint64_t now = 0;
        for (uint64_t m = s->fx_dst[w] | s->fx_was[w]; m; m &= m - 1) {   /* reached now or still settling */
            int b = __builtin_ctzll(m), k = 64 * w + b;
            float d = fx[k] - s->fxm[k];
            s->fxm[k] = fabsf(d) < 1e-6f ? fx[k] : s->fxm[k] + d * s->fx_coef;
            now |= (uint64_t)(s->fxm[k] != 0.0f) << b;
        }
        dirty[w] = now | s->fx_was[w];
        s->fx_was[w] = now;
        live |= now != 0;
    }
    s->fx_live = live;
    for (int c = 0; c < 2; c++)   /* a modulated CTRL moves its target */
        if (fx_bit(dirty, MDF_CTRL1 + c)) {
            int k = FXD_SLOT[fx_ctrl_dst(s, c)].slot;
            if (k >= 0) dirty[s->fx_of[k] >> 6] |= 1ull << (s->fx_of[k] & 63);
        }
    for (int w = 0; w < MDF_WORDS; w++) any |= dirty[w] != 0;
    return any;
}

/* an effect control's value as the effects use it: the knob, moved by the matrix's effect modulation by up to +-100%
 * of its range, then (for CTRL 1/2's target) by CTRL 1/2, themselves destinations, by up to +-100% of the target's
 * range (a performance offset on top of the knob, as the panel's two quick controls) */
static float fx_eff(const one_t *s, int j, const int *ctrl) {
    int k = s->fx_of[j];
    float v = s->val[j];
    if (k >= 0 && s->fxm[k] != 0.0f) v = clampf(v + s->fxm[k] * (s->fx_hi[k] - s->fx_lo[k]), s->fx_lo[k], s->fx_hi[k]);
    for (int c = 0; c < 2; c++)
        if (ctrl[c] == j) {   /* (a CTRL is never a CTRL's target: no deeper than this) */
            int d = fx_ctrl_dst(s, c);
            v = clampf(v + fx_eff(s, P_MODFX_CTRL1 + c, ctrl) * 0.01f * (FXD_SLOT[d].hi - FXD_SLOT[d].lo), FXD_SLOT[d].lo,
                       FXD_SLOT[d].hi);
        }
    return v;
}

/* CTRL 1 and CTRL 2's target slots (-1: none) */
static void fx_ctrl_slots(const one_t *s, int *ctrl) {
    for (int c = 0; c < 2; c++) ctrl[c] = FXD_SLOT[fx_ctrl_dst(s, c)].slot;
}

/* a master slot's type (MFX_*) and whether its time and rate knobs are synced */
static int fx_bank_type(const one_t *s, int b) { return (int)clampf(s->val[BANK[b][BK_TYPE]], 0, MFX_NTYPES - 1); }

/* a master slot's decay time: its DECAY knob as the type reads it (plate and room 0.25-10 s, hall and shimmer
 * 0.1-20 s; the blackhole's from FEEDBACK, 1-100 s), times SIZE (a bigger space rings longer) */
static void fx_bank_rt(const one_t *s, int b, const int *ctrl, fxs_params_t *q) {
    const short *K = BANK[b];
    int mt = fx_bank_type(s, b);
    float dk = fx_eff(s, K[BK_DECAY], ctrl), sk = fx_eff(s, K[BK_SIZE], ctrl);
    q->rev_size = fx_rev_size(sk);
    q->rev_decay_k = dk * 0.01f;
    q->rev_size_k = sk * 0.01f;
    q->decay = dk * 0.01f;   /* (the echo's DECAY) */
    float rt = mt == MFX_HALL || mt == MFX_SHIMMER ? fx_hall_rt60(dk)
             : mt == MFX_BLACKHOLE ? fx_bh_rt60(clampf(fx_eff(s, K[BK_FB], ctrl), 0.0f, 100.0f)) : fx_rev_rt60(dk);
    q->rev_rt60 = rt * q->rev_size;
}

/* the setting a master slot's control gives (field f of BANK[b], value v), in the units fx.h reads */
static void fx_bank_field(const one_t *s, int b, int f, float v, const int *ctrl, fxs_params_t *q) {
    const int st = MFX_SFX[fx_bank_type(s, b)], sync = s->val[BANK[b][BK_SYNC]] > 0.5f;
    switch (f) {
    case BK_MIX: q->mix = v * 0.01f; break;
    case BK_GAIN: q->gain = fx_gain_x(v); break;
    case BK_TIME:   /* one knob for the delay types' times and the choruses' and flangers' */
        q->dtime_ms = sync ? fx_div_ms(v, s->bpm) : fx_delay_ms(v);
        q->time_ms = fx_mod_time_ms(st, v);
        break;
    case BK_TIME_R:
        q->dtime_r_ms = sync ? fx_div_ms(v, s->bpm) : fx_delay_ms(v);
        q->time_r_ms = fx_mod_time_ms(st, v);
        break;
    case BK_FB: q->fb = v * 0.0095f; fx_bank_rt(s, b, ctrl, q); break;   /* (the blackhole's decay) */
    case BK_FB_R: q->fb_r = v * 0.0095f; break;
    case BK_FBL: q->fbl = v * 0.01f; break;
    case BK_FBR: q->fbr = v * 0.01f; break;
    case BK_RATE: q->rate_hz = sync ? fx_div_hz(v, s->bpm) : fx_rate_hz(v); break;
    case BK_RATE_R: q->rate_r_hz = sync ? fx_div_hz(v, s->bpm) : fx_rate_hz(v); break;
    case BK_VRATE: q->vib_hz = sync ? fx_div_hz(v, s->bpm) : fx_vib_hz(v); break;
    case BK_DEPTH: q->depth = v * 0.01f; break;
    case BK_DEPTH_R: q->depth_r = v * 0.01f; break;
    case BK_TONE: q->tone = q->rev_tone = v * 0.01f; break;
    case BK_OFFSET: q->offset = v * 0.01f; break;
    case BK_SPREAD: q->spread = v * 0.01f; break;
    case BK_NOISE: q->noise = v * 0.01f; break;
    case BK_SWEEP: q->sweep = v * 0.01f; break;
    case BK_DFB: q->dfb = v * 0.0095f; break;
    case BK_VDEPTH: q->vib_depth = v * 0.01f; break;
    case BK_FLUTTER: q->flutter = v * 0.01f; break;
    case BK_WOW: q->wow = v * 0.01f; break;
    case BK_BIAS: q->bias = v * 0.01f; break;
    case BK_ROOM: q->room = v * 0.01f; break;
    case BK_DECAY: case BK_SIZE: fx_bank_rt(s, b, ctrl, q); break;
    case BK_PRE: q->rev_pre_ms = v; break;
    case BK_DAMP: q->rev_damp = v * 0.009f; break;
    case BK_LOWDAMP: q->rev_lowdamp = v * 0.01f; break;
    case BK_DIFF: q->rev_diff = v * 0.01f; break;
    case BK_MOD: q->rev_mod = v * 0.02f; break;
    case BK_EARLY: q->rev_early = v * 0.01f; break;
    case BK_DISTANCE: q->distance = v * 0.01f; break;
    case BK_HIFREQ: q->hifreq_hz = fx_hifreq_hz(v); break;
    case BK_HLEVEL: q->lvl_hi = fx_level(v); break;
    case BK_MLEVEL: q->lvl_mid = fx_level(v); break;
    case BK_LLEVEL: q->lvl_lo = fx_level(v); break;
    case BK_LDECAY: q->dec_lo = v; break;
    case BK_HDECAY: q->dec_hi = v; break;
    case BK_PITCH: q->pitch_a = v; break;
    case BK_PITCH_B: q->pitch_b = v; break;
    case BK_PDECAY: q->pdecay = v * 0.01f; break;
    case BK_GRAVITY: q->gravity = v * 0.01f; break;
    case BK_XOVER: q->xover_hz = fx_xover_hz(v); break;
    case BK_BITS: q->bits = v; break;
    case BK_SRATE: q->srate_hz = fx_srate_hz(v); break;
    case BK_RESO: q->reso = v * 0.01f; break;
    case BK_CUT1: case BK_CUT2: case BK_CUT3: q->cut_hz[f - BK_CUT1] = fx_cut_hz(v); break;
    case BK_LVL1: case BK_LVL2: case BK_LVL3: q->lvl[f - BK_LVL1] = v * 0.01f; break;
    case BK_SIBIL: q->sibil = v * 0.01f; break;
    case BK_SENS: q->sens = v * 0.01f; break;
    default: break;   /* the options: fx_params() */
    }
}

/* the setting(s) effect control j (an FXMD_SLOT) gives, in the units fx.h reads */
static void fx_field(const one_t *s, int j, const int *ctrl, fx_params_t *f) {
    int type = (int)s->val[P_MODFX_TYPE], sync = s->val[P_MODFX_SYNC] > 0.5f;
    float v = fx_eff(s, j, ctrl);
    fxs_params_t *q = &f->sfx;
    if (s->fx_bank[j] >= 0) {
        fx_bank_field(s, s->fx_bank[j], s->fx_bankf[j], v, ctrl, &f->mfx[s->fx_bank[j]]);
        return;
    }
    switch (j) {
    case P_MODFX_MIX: q->mix = v * 0.01f; break;
    case P_MODFX_GAIN: q->gain = fx_gain_x(v); break;
    case P_MODFX_FB: q->fb = v * 0.0095f; break;   /* +-95% */
    case P_MODFX_FB_R: q->fb_r = v * 0.0095f; break;
    case P_MODFX_FBL: q->fbl = v * 0.01f; break;
    case P_MODFX_FBR: q->fbr = v * 0.01f; break;
    case P_MODFX_DFB: q->dfb = v * 0.0095f; break;
    case P_MODFX_TIME: q->time_ms = fx_mod_time_ms(type, v); break;
    case P_MODFX_TIME_R: q->time_r_ms = fx_mod_time_ms(type, v); break;
    case P_MODFX_DTIME: q->dtime_ms = sync ? fx_div_ms(v, s->bpm) : fx_delay_ms(v); break;
    case P_MODFX_DTIME_R: q->dtime_r_ms = sync ? fx_div_ms(v, s->bpm) : fx_delay_ms(v); break;
    case P_MODFX_RATE: q->rate_hz = sync ? fx_div_hz(v, s->bpm) : fx_rate_hz(v); break;
    case P_MODFX_RATE_R: q->rate_r_hz = sync ? fx_div_hz(v, s->bpm) : fx_rate_hz(v); break;
    case P_MODFX_VRATE: q->vib_hz = sync ? fx_div_hz(v, s->bpm) : fx_vib_hz(v); break;
    case P_MODFX_DEPTH: q->depth = v * 0.01f; break;
    case P_MODFX_DEPTH_R: q->depth_r = v * 0.01f; break;
    case P_MODFX_TONE: q->tone = v * 0.01f; break;
    case P_MODFX_OFFSET: q->offset = v * 0.01f; break;
    case P_MODFX_SPREAD: q->spread = v * 0.01f; break;
    case P_MODFX_SWEEP: q->sweep = v * 0.01f; break;
    case P_MODFX_NOISE: q->noise = v * 0.01f; break;
    case P_MODFX_VDEPTH: q->vib_depth = v * 0.01f; break;
    case P_MODFX_FLUTTER: q->flutter = v * 0.01f; break;
    case P_MODFX_WOW: q->wow = v * 0.01f; break;
    case P_MODFX_BIAS: q->bias = v * 0.01f; break;
    case P_MODFX_ROOM: q->room = v * 0.01f; break;
    case P_MODFX_DECAY: q->decay = v * 0.01f; break;
    case P_MODFX_BITS: q->bits = v; break;
    case P_MODFX_SRATE: q->srate_hz = fx_srate_hz(v); break;
    case P_MODFX_XOVER: q->xover_hz = fx_xover_hz(v); break;
    case P_MODFX_RESO: q->reso = v * 0.01f; break;
    case P_MODFX_CUT1: case P_MODFX_CUT2: case P_MODFX_CUT3: q->cut_hz[j - P_MODFX_CUT1] = fx_cut_hz(v); break;
    case P_MODFX_LVL1: case P_MODFX_LVL2: case P_MODFX_LVL3: q->lvl[j - P_MODFX_LVL1] = v * 0.01f; break;
    case P_MODFX_PITCH: q->pitch_st = v; break;
    case P_MODFX_STRUCT: q->structure = v * 0.01f; break;
    case P_MODFX_POS: q->position = v * 0.01f; break;
    case P_MODFX_SIZE: q->size = v * 0.01f; break;
    case P_MODFX_DENSITY: q->density = v * 0.01f; break;
    case P_MODFX_TEXTURE: q->texture = v * 0.01f; break;
    case P_MODFX_DRIVE: q->drive = v * 0.01f; break;
    case P_MODFX_BUMP: q->bump = v * 0.01f; break;
    case P_MODFX_LOWCUT: q->lowcut = v * 0.01f; break;
    case P_MFX_SEND1: f->send1 = v * 0.01f; break;
    case P_MFX_SEND2: f->send2 = v * 0.01f; break;
    case P_MFX_SERIES: f->series = v * 0.01f; break;
    case P_MFX_WIDTH: f->width = v * 0.01f; break;
    default: break;   /* CTRL 1/2: only through their targets */
    }
}

/* the vocoders' SIBILANCE and SENSITIVITY (not matrix destinations: CTRL 1/2 move the synth effect's, so they are
 * worked out again with every effect pass) */
static void fx_voc_fields(const one_t *s, fx_params_t *f, const int *ctrl) {
    f->sfx.sibil = fx_eff(s, P_MODFX_SIBIL, ctrl) * 0.01f;
    f->sfx.sens = fx_eff(s, P_MODFX_SENS, ctrl) * 0.01f;
    for (int b = 0; b < 2; b++)
        for (int k = BK_SIBIL; k <= BK_SENS; k++) fx_bank_field(s, b, k, s->val[BANK[b][k]], ctrl, &f->mfx[b]);
}

/* every effect setting (once per render: the knobs may have moved) */
static void fx_params(const one_t *s, fx_params_t *f) {
    const float *p = s->val;
    fxs_params_t *q = &f->sfx;
    f->sfx_on = p[P_MODFX_ON] > 0.5f;
    q->type = (int)p[P_MODFX_TYPE];
    q->ftype = (int)p[P_MODFX_FTYPE];
    q->model = (int)p[P_MODFX_MODEL];
    q->follow = p[P_MODFX_FOLLOW] > 0.5f;
    q->gmode = (int)p[P_MODFX_GMODE];
    q->quality = (int)p[P_MODFX_QUALITY];
    q->freeze = p[P_MODFX_FREEZE] > 0.5f;
    f->mfx_on = p[P_MFX_ON] > 0.5f;
    f->buss = (int)p[P_MFX_BUSS];
    f->wet_pan = p[P_PAN] * 0.01f;
    for (int b = 0; b < 2; b++) {   /* each master slot's type and options */
        fxs_params_t *m = &f->mfx[b];
        m->mtype = fx_bank_type(s, b);
        m->type = MFX_SFX[m->mtype] < 0 ? m->mtype : MFX_SFX[m->mtype];
        m->hold = (int)p[BANK[b][BK_FREEZE]];
        m->ftype = (int)p[BANK[b][BK_FTYPE]];
    }
    int ctrl[2];
    fx_ctrl_slots(s, ctrl);
    for (int k = 0; k < MDF_COUNT; k++) fx_field(s, FXMD_SLOT[k], ctrl, f);
    fx_voc_fields(s, f, ctrl);
}

/* the effect pass's settings (bit k of `dirty`: MDF_* k), the others as they are: each control step, only what moves */
static void fx_params_mod(const one_t *s, fx_params_t *f, const uint64_t *dirty) {
    int ctrl[2];
    fx_ctrl_slots(s, ctrl);
    for (int k = 0; k < MDF_COUNT; k++)
        if (fx_bit(dirty, k)) fx_field(s, FXMD_SLOT[k], ctrl, f);
    fx_voc_fields(s, f, ctrl);
}

/* the Rings effect's note, each control step: the newest sounding voice's key (its glide, transpose and master tune;
 * not pitch bend or pitch modulation) or, with KEY FOLLOW off, middle C; plus PITCH. A new note strums it (it jumps
 * there instead of gliding); with nothing sounding it keeps the last key, so the tail rings on at its pitch. */
static void fx_note_step(one_t *s, fx_params_t *f) {
    if (f->sfx.type != SFX_RINGS) return;
    const voice_t *nv = NULL;
    for (int i = 0; i < MAX_VOICES; i++) {
        const voice_t *v = &s->voice[i];
        if (v->env[ENV_AMP].stage != ENV_IDLE && (!nv || v->age > nv->age)) nv = v;
    }
    if (nv) {
        s->fx_key = voice_key(s, nv) + 12.0f * log2f(s->tune_hz / 440.0f);
        if (nv->age != s->fx_age) { f->sfx.strum = 1; s->fx_age = nv->age; }
    }
    f->sfx.note = (f->sfx.follow ? (s->fx_age ? s->fx_key : 60.0f) : 60.0f) + f->sfx.pitch_st;
}

/* ---- one Synth's audio (src/synths.h render_to runs the Synths piece by piece, in lockstep) ---- */
/* once per render while the Synth is live: its effect settings, its clocks */
static void synth_block(one_t *s) {
    if (s->fx_stale) {   /* a knob moved (or the tempo): every effect setting again, the modulation included */
        s->fx_stale = 0;
        fx_params(s, &s->fxp);
    }
    if (s->seq_on) clock_block(s);
    if (sq_live(s) || s->sq.start_pending) sq_block(s);   /* the step sequencer's clock */
}

/* the steps and note ends due at this sample; *n shortened to the next one (they fall on their exact sample) */
static void synth_due(one_t *s, int *n) {
    int live = s->seq_on || s->npend > 0;   /* the step clock plays, or its last notes are still to end */
    int sql = sq_live(s);   /* the step sequencer runs, or has notes to start or end */
    if (live) seq_due(s);   /* steps and note ends at this sample, before the voices' control step */
    if (sql) sq_due(s);
    if (s->ph.on) { ph_due(s, n); live |= s->npend > 0; }   /* the balls' and particles' notes (their ends: above) */
    if (live) {
        int64_t d = seq_next(s);
        if (d < *n) *n = (int)d;
    }
    if (sql) {
        int64_t d = sq_next(s);
        if (d < *n) *n = (int)d;
    }
}

/* n samples of the Synth: its voices and its synth effect, into l, r */
static void synth_piece(one_t *s, int n, float *l, float *r) {
    float acc[128], xl[128], xr[128];   /* the voices: mono sum, panned voices' offsets */
    fx_params_t *fxp = &s->fxp;
    int first = s->phase == 0;
    if (first) { control_lfos(s); ribbon_step(s); }
    memset(acc, 0, sizeof(float) * n);
    memset(xl, 0, sizeof(float) * n);
    memset(xr, 0, sizeof(float) * n);
    int side = 0, sounding = 0;   /* a voice is off the centre; any sounds */
    for (int k = 0; k < MAX_VOICES; k++) {   /* voices past the current count still finish their release */
        voice_t *v = &s->voice[k];
        if (v->env[ENV_AMP].stage == ENV_IDLE) continue;
        sounding = 1;
        if (first) control_voice(s, v);
        side |= render_voice(s, v, s->ext, acc, xl, xr, n, first);
    }
    if (side)   /* the dry path in stereo: every voice at its place */
        for (int i = 0; i < n; i++) { xl[i] += acc[i]; xr[i] += acc[i]; }
    if (first) {   /* the effect pass, after the voices' sources: only the settings it moves */
        uint64_t dirty[MDF_WORDS];
        if (fx_mod_step(s, dirty)) fx_params_mod(s, fxp, dirty);
        fx_note_step(s, fxp);
    }
    fxp->ramp = CR - s->phase;   /* the effect gains ramp across the whole control step, however the host splits it */
    fxp->sfx.voc_mod = s->ext;   /* the vocoders' modulator: the audio input */
    fx_synth(&s->fx, fxp, acc, side ? xl : acc, side ? xr : acc, l, r, n);   /* synth effect: mono in */
    fxp->sfx.strum = 0;
    s->quiet = sounding ? 0 : s->quiet + n;
}

static void synth_advance(one_t *s, int n) {   /* every Synth, live or not: the clocks move on */
    s->phase = (s->phase + n) % CR;
    s->now += n;
    s->clk += n;
    s->sq.clk += n;
}

static unsigned next_age(one_t *s);

#include "synths.h"   /* THE SYNTHS: the instance, the engine API */

static unsigned next_age(one_t *s) { return ++s->in->age; }
