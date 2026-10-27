/* ONE-ish engine. Per voice: three band-limited dual-wave oscillators (src/osc.h: a triangle/saw with a wave angle
 * blended with a pulse of any width; octave, frequency +-7 semitones, beat frequency +-7 Hz; 2 and 3 can hard-sync
 * to 1), linear FM (1 to 2, 1 to 3 or 3 to 1), a ring modulator (1 x 2 or 2 x 3, from the triangle/saw waves) and
 * white noise, each sent to the SVF, the ladder, both or neither by its own two buttons; a state-variable filter
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
 * hard-wired amounts, envelope times (per voice) and LFO rates, variation and times; mod wheel, aftertouch, pitch
 * bend (separate up and down amounts, plus per-oscillator offsets), sustain and sostenuto pedals, HOLD, two
 * expression CCs (11 and 4). Up to 8 voices: a new note takes an idle voice, else one by the stealing mode (default:
 * the quietest releasing one, else the oldest held one); the same key replays its own voice. Mono (1 voice) plays
 * legato with low, high or last-note priority. Velocity curves, transpose, octave shift, key-track centre, master
 * tune; per oscillator pitch tracking, note-on phase reset and a choice of hard sync source. Glide (portamento): each
 * voice slides from its own
 * last pitch at a constant rate (LCR), in a constant time (LCT) or exponentially (EXP); optionally only on legato
 * notes, paused while the key is up (gated), or in semitone steps (glissando). Unison stacks 2-8 voices on each note
 * (a stack is allocated and stolen as one), detuned unevenly around the centre; pitch variance gives each new note a
 * small random pitch offset. After the voices, in stereo (src/fx.h): a synth effect of a chosen type (delays, chorus,
 * flanger, phaser, bit crusher, resonator...) with CTRL 1/2, then the master effects on two sends: a stereo or
 * ping-pong delay and a plate, room or hall reverb.
 *
 * Audio runs per sample; modulation (envelopes 1 and 3, LFOs, the matrix, pitch, cutoffs) every CR samples, with levels and the
 * filter mix ramped across each step so no knob or mod move steps audibly.
 * Implements mpc_engine() (wrapper/engine.h): 44.1 kHz, interleaved int16 stereo, up to 128 frames per render().
 * Everything is allocated in create(); render() and midi() never allocate, lock or touch files. */
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

#define SR 44100.0f
#define CR 16              /* control-rate step in samples: modulation is worked out every 0.36 ms */
#define MAX_HELD 16
#define MAX_VOICES 8
#define NOSC 3
#define NSRC 5             /* the mixer's channels: oscillators 1-3, noise, ring mod */
#define NLFO 4
#define HEADROOM 0.5f      /* one full-scale saw at full velocity and volume peaks at -6 dBFS */
#define STATE_MAX 8192     /* the wrapper's chunk size (wrapper/vst2_wrap.c) */
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
    P_COUNT
};
enum { XO_TRACK, XO_RESET, XO_BEND_UP, XO_BEND_DN, XO_STRIDE = P_O2_TRACK - P_O1_TRACK };
enum { PRIO_LOW, PRIO_HIGH, PRIO_LAST };                        /* "note_priority" options */
enum { STEAL_NONE, STEAL_OLDEST, STEAL_QUIETEST, STEAL_NEWEST }; /* "steal_mode" options */
enum { VEL_SOFT, VEL_MEDIUM, VEL_HARD, VEL_COMPRESS, VEL_EXPAND }; /* "vel_curve" options */
#define OCT_SHIFT_CENTRE 2   /* option index of 0 in the "octave_shift" list */
enum { GLIDE_LCR, GLIDE_LCT, GLIDE_EXP };       /* "glide_type" options */
enum { OSC_STRIDE = P_O2_WAVE - P_O1_WAVE, O_WAVE = 0, O_OCT, O_FREQ, O_BEAT, O_ANGLE, O_PW, O_MIX };
/* each mixer channel's level and filter buttons, in channel order (osc 1-3, noise, ring mod: the matrix's order) */
static const int LEVEL_SLOT[NSRC] = {P_O1_LEVEL, P_O2_LEVEL, P_O3_LEVEL, P_NOISE_LEVEL, P_RING_LEVEL};
static const int SVF_SLOT[NSRC] = {P_O1_SVF, P_O2_SVF, P_O3_SVF, P_NOISE_SVF, P_RING_SVF};
static const int LAD_SLOT[NSRC] = {P_O1_LAD, P_O2_LAD, P_O3_LAD, P_NOISE_LAD, P_RING_LAD};
enum { NOISE = 3, RING = 4 };                   /* the noise and ring modulator channels */
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
    "dly_time", "dly_sync", "dly_fb", "dly_tone", "dly_pp", "mfx_send1",
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
    "rev_type", "rev_size", "rev_diff", "rev_tone", "rev_lowdamp", "rev_mod", "rev_early", "rev_freeze"
};
#define OCT_CENTRE 2   /* option index of 8' (no octave shift) in the "oct" lists */
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
static const float LFO_RANGE_HZ[] = {0.001f, 0.01f, 0.1f};   /* "lfoN_range": Slow, Low, Medium (lowest rate) */
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
    env_cfg_t ecfg[NENV];    /* each envelope's settings with this voice's matrix modulation (when it has any) */
    float eq[NENV][5];       /* the stage times ecfg was worked out for, as env_q() values */
    unsigned egen[NENV];     /* and the shared settings' generation */
    unsigned lfo_own;        /* bit n: the voice runs LFO n itself (restarted per note or modulated) */
    int gate;                /* key down, or released while the sustain pedal is down */
    int sustained;           /* released while the pedal (or HOLD, or sostenuto) is down: let go when it comes up */
    unsigned age;            /* when its note started, for stealing the oldest */
    int sost;                /* sounding when the sostenuto pedal went down: its key's release waits for the pedal */
    int reset_pend;          /* NOTE-ON RESET: a bit per oscillator to restart at phase 0 on the next sample */
} voice_t;

typedef struct {
    float val[P_COUNT];      /* each value in its own units: option index, semitones, cents, % or a 0..100 knob */
    float def[P_COUNT];      /* the defaults, so a saved state lists only what differs from them */
    float ratio[NOSC];       /* each oscillator's pitch against the played note (octave and frequency) */
    voice_t voice[MAX_VOICES];
    unsigned age;            /* counts note starts */
    int voices;              /* the voices in use last applied (a change lets go of voices past it) */
    float glide_frac;        /* one control step as a fraction of the glide time (>= 1: no glide) */
    float glide_exp;         /* EXP glide: the fraction of the remaining distance covered per control step */
    fx_t fx;                 /* effect buffers (about 3.1 MB) */
    int sustain;             /* the pedal (CC 64) is down */
    lfo_t lfo[NLFO];         /* the free-running LFOs */
    float lfo_val[NLFO];     /* this control step's free-running LFO outputs (polarity and smoothing applied) */
    float lfo_hz[NLFO];      /* and their rates (apply_lfos) */
    float lfo_coef[NLFO];    /* each LFO's smoothing coefficient per control step (1: none) */
    float lfo_fade[NLFO][3]; /* each LFO's delay, fade in and fade out, samples (0: off) */
    uint32_t bipolar;        /* bit s: matrix source s is bipolar (for the limit transforms) */
    uint8_t mod_list[MOD_SLOTS];   /* the matrix slots in use, and how many */
    int mod_n;
    int mod_env, mod_lfo;    /* a slot in use goes to an envelope's / an LFO's settings */
    unsigned env_gen;        /* counts changes to the shared envelope settings */
    float exp1, exp2;        /* expression controllers (CC 11, CC 4), 0..1 */
    float bpm;               /* the host's tempo, for synced LFOs, delay and envelopes */
    env_cfg_t ecfg[NENV];    /* each envelope's settings (times, curves, loop, latch...), shared by the voices */
    float wheel, aftertouch, bend;   /* 0..1, 0..1, -1..1 (times the bend range) */
    float gain_coef, glide;  /* per sample / per control step */
    int phase;               /* samples into the current control step */
    float link_off;          /* LINK CUTOFF: the SVF cutoff knob minus the ladder's when link went on */
    int held[MAX_HELD];      /* keys down, oldest first (mono: releasing the newest falls back to the one before) */
    int nheld;
    int sostenuto;           /* the sostenuto pedal (CC 66) is down */
    int hold;                /* HOLD as last applied (turning it off lets go of the notes it latched) */
    float expression;        /* expression pedal (CC 11), 0..1: stored for the matrix, not yet routed */
    float tune_hz;           /* A above middle C: master tune with fine tune */
    float shift, center;     /* transpose + octave shift, and the key track centre, in whole semitones */
    float track[NOSC];       /* each oscillator's pitch tracking - 1 (0 at 100%) */
    float bend_off[NOSC][2]; /* each oscillator's bend up / down offset, whole semitones */
    char state_buf[STATE_MAX];   /* set_state's working copy (allocated with the instance, not per call) */
} one_t;

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

/* store a value in its param's range; NaN, infinities and unknown keys leave the old value (a damaged project) */
static void store(one_t *s, const char *key, const char *text) {
    int k = slot(key);
    float v = (float)atof(text);
    if (!strcmp(key, "bend_range") && isfinite(v)) {   /* retired: an older project's one range, both ways */
        s->val[P_BEND_UP] = clampf(v, 0.0f, 24.0f);
        s->val[P_BEND_DOWN] = -s->val[P_BEND_UP];
        return;
    }
    if (k < 0 || !isfinite(v)) return;
    for (int i = 0; i < NPARAMS; i++) {
        const param_t *p = &PARAMS[i];
        if (strcmp(p->key, key)) continue;
        s->val[k] = p->nopts ? clampf(roundf(v), 0, p->nopts - 1) : clampf(v, p->min, p->max);
        return;
    }
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
static float env_time_scaled(const one_t *s, int n, float knob, int zero_off, float scale_knob) {
    const float *x = &s->val[P_FENV_DELAY + EX_STRIDE * n];
    if (zero_off && knob <= 0.0f) return 0.0f;
    if (x[X_SYNC] > 0.5f) return 60000.0f / s->bpm * SYNC_DIV[env_div(knob)].beats;
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
    return LFO_RANGE_HZ[(int)clampf(x[L_RANGE], 0, 2)] * powf(LFO_RANGE, knob * 0.01f);
}
static float lfo_hz(const one_t *s, int n) { return lfo_rate_hz(s, n, s->val[lfo_base(n)]); }

/* an LFO delay or fade knob (0: off, then 1 ms .. 10 s) in samples */
static float lfo_samples(float knob) { return knob > 0.0f ? env_ms(fminf(knob, 100.0f)) * 0.001f * SR : 0.0f; }

/* the LFOs' settings that don't change per voice */
static void apply_lfos(one_t *s) {
    s->bipolar = 1u << MS_KEY | 1u << MS_RAND | 1u << MS_BEND;
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
    }
}

/* the matrix slots in use, and whether any reaches envelope or LFO settings (else those per-voice paths are skipped) */
static void apply_matrix(one_t *s) {
    s->mod_n = mod_active(&s->val[P_MOD_BASE], &s->val[P_MODX_BASE], s->mod_list);
    s->mod_env = s->mod_lfo = 0;
    for (int k = 0; k < s->mod_n; k++) {
        int d = (int)s->val[P_MOD_BASE + MOD_SLOT_PARAMS * s->mod_list[k] + 1];
        s->mod_env |= d >= MD_FENV_DELAY && d <= MD_ENV_END;
        s->mod_lfo |= d >= MD_LFO1_RATE && d <= MD_LFO_END;
    }
}

/* recompute what the audio path reads from the parameter values */
static void apply(one_t *s) {
    for (int n = 0; n < NOSC; n++) {
        const float *o = &s->val[P_O1_WAVE + OSC_STRIDE * n];
        s->ratio[n] = powf(2.0f, (o[O_OCT] - OCT_CENTRE) + o[O_FREQ] / 12.0f);
    }
    apply_envs(s);
    apply_lfos(s);
    apply_matrix(s);
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
}

static const int VOICE_COUNTS[] = {1, 2, 3, 4, 5, 6, 7, 8};   /* the "voices" options */
static int voice_count(const one_t *s) { return VOICE_COUNTS[(int)clampf(s->val[P_VOICES], 0, 7)]; }
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
    return env_vel(s, ENV_AMP, v->velocity) * s->val[P_VOLUME] * 0.01f * HEADROOM * v->amp_mod * v->uni_gain;
}

/* the pitch the voice plays (semitones): the glided pitch, in whole semitones with glissando, shifted by semitone
 * transpose and octave shift (they follow live, and keyboard tracking and the Keyboard source follow them too) */
static float voice_key(const one_t *s, const voice_t *v) {
    return (s->val[P_GLISSANDO] > 0.5f ? roundf(v->pitch) : v->pitch) + s->shift;
}

/* the voice's base frequency: its key, pitch variance and place in the unison stack (detune follows the knob live),
 * tuned to the master tune */
static void update_note_hz(const one_t *s, voice_t *v) {
    float st = voice_key(s, v) + v->pvar + v->uni_pos * s->val[P_UNI_DETUNE] * 0.01f * UNI_DETUNE_ST;
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
        float l = clampf(p[LEVEL_SLOT[k]] * 0.01f + dm[MD_O1_LEVEL + k], 0.0f, 1.0f);
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
    float from_centre = voice_key(s, v) - s->center;
    for (int n = 0; n < NOSC; n++) {
        float track = from_centre * s->track[n];
        float hz = v->note_hz * s->ratio[n] * exp2f((semis + osc_semis[n] + track) / 12.0f) + s->val[P_O1_BEAT + OSC_STRIDE * n]
                   + v->beat_mod[n];
        osc_set_freq(&v->osc[n], hz, SR);
        v->inc0[n] = v->osc[n].inc;
    }
}

static void load_defaults(one_t *s) {
    for (int i = 0; i < NPARAMS; i++) {
        const param_t *p = &PARAMS[i];
        int k = slot(p->key);
        if (k < 0) continue;
        s->val[k] = p->nopts ? roundf(p->def * (p->nopts - 1)) : p->min + p->def * (p->max - p->min);
    }
}

static void *create(const char *data_dir) {
    (void)data_dir;
    one_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    fx_init(&s->fx);   /* the effect buffers' pages, mapped now rather than in the audio thread */
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
        v->cut = s->val[P_FLT_CUT];
        v->svf_cut = s->val[P_SVF_CUT];
    }
    for (int n = 0; n < NLFO; n++) lfo_init(&s->lfo[n], 0x2545f491u * (n + 1));
    s->bpm = 120.0f;
    s->voices = voices_used(s);
    s->link_off = s->val[P_SVF_CUT] - s->val[P_FLT_CUT];
    s->gain_coef = env_coef(5.0f, SR);
    s->glide = 1.0f - expf(-(float)CR / (GLIDE_MS * 0.001f * SR));
    apply(s);
    return s;
}

static void destroy(void *inst) { free(inst); }

/* legato: another key is held as this note starts. The voice glides from its own last pitch when glide is on, it has
 * played before, and (with legato glide) the note is legato; else it jumps straight to the note. */
static void start_note(one_t *s, voice_t *v, int note, int legato) {
    static const float no_mod[MD_COUNT], no_semis[NOSC];
    int glide = s->val[P_GLIDE] > 0.5f && v->note >= 0 && (legato || s->val[P_GLIDE_LEGATO] < 0.5f);
    if (glide) v->glide_span = fabsf((float)note - v->pitch);
    else v->pitch = (float)note;
    v->gliding = glide && v->glide_span > 0.0f;
    v->note = note;
    update_note_hz(s, v);
    set_pitch(s, v, 0.0f, no_semis);   /* pitch at once; the next control step adds the modulation */
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
        v->gain = gain_target(s, v);
        v->cut = s->val[P_FLT_CUT];
        v->svf_cut = s->val[P_SVF_CUT];
        memset(v->to_lad, 0, sizeof v->to_lad);   /* the levels ramp up from 0 in the first control step */
        memset(v->to_svf, 0, sizeof v->to_svf);
        /* the filters' unmodulated settings, for the samples before the next control step works out the rest */
        ladder_set(&v->lad, cut_hz(v->cut), s->val[P_FLT_RES] * 0.01f, s->val[P_FLT_DRIVE] * 0.01f,
                   (int)s->val[P_FLT_MODE], (int)s->val[P_FLT_SLOPE], SR);
        svf2_set(&v->svf, cut_hz(v->svf_cut), s->val[P_SVF_SPACE] * 0.01f, s->val[P_SVF_RES] * 0.01f,
                 (int)s->val[P_SVF_MODE], (int)s->val[P_SVF_PAIR], SR);
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

static void gate_on(voice_t *v) {
    /* a legato note (this voice's key still down) restarts an envelope only with its MULTI TRIG on */
    int legato = v->gate && !v->sustained && v->env[ENV_AMP].stage != ENV_IDLE;
    for (int n = 0; n < NENV; n++)
        if (!legato || v->env[n].cfg->multi) env_gate_on(&v->env[n]);
    v->gate = 1;
    v->sustained = 0;
    v->sost = 0;   /* a note played now: the sostenuto pedal, if down, doesn't hold it */
}

static void gate_off(voice_t *v) {
    for (int n = 0; n < NENV; n++) env_gate_off(&v->env[n]);
    v->gate = 0;
    v->sustained = 0;
    v->sost = 0;
}

static int hold_on(const one_t *s) { return s->val[P_HOLD] > 0.5f; }

/* a released key: held on by the sustain pedal, HOLD or (a note sounding when it went down) the sostenuto pedal, or
 * let go */
static void key_up(one_t *s, voice_t *v) {
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

static void note_on(one_t *s, int note, int vel) {
    if (s->nheld == 0) release_held(s, 0);   /* a non-legato note under HOLD (no pedal down) replaces what it latched */
    forget(s, note);              /* a repeated key moves to the top instead of being held twice */
    if (s->nheld == MAX_HELD) forget(s, s->held[0]);   /* drop the oldest key */
    s->held[s->nheld++] = note;
    int legato = s->nheld > 1, first = 0;
    if (is_mono(s)) {   /* the priority key: a new key that loses to a held one changes nothing */
        int key = mono_key(s);
        if (key != note && s->voice[0].gate && s->voice[0].note == key) return;
        note = key;
    } else if ((first = pick_stack(s, note)) < 0) {
        return;   /* No Stealing and every voice held */
    }
    int stack = stack_size(s);
    unsigned age = ++s->age;
    float var = s->val[P_PITCH_VAR] * 0.01f;   /* cents to semitones */
    for (int k = 0; k < stack; k++) {
        voice_t *v = &s->voice[first + k];
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
        gate_on(v);
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
                if (start) { gate_on(v); lfo_note_on(s, v); }
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
        if (hard)   /* All Sound Off: silent within a few ms, without a click */
            for (int n = 0; n < NENV; n++) env_kill(&v->env[n]);
        else   /* latched envelopes too */
            for (int n = 0; n < NENV; n++) env_release(&v->env[n]);
        v->gate = 0;
        v->sustained = 0;
        v->sost = 0;
    }
}

static void midi(void *inst, const uint8_t *m, int len) {
    one_t *s = inst;
    if (len < 2) return;
    int st = m[0] & 0xf0, d1 = m[1] & 0x7f, d2 = len > 2 ? m[2] & 0x7f : 0;
    if (st == 0xd0) { s->aftertouch = d1 / 127.0f; return; }   /* channel pressure: two bytes */
    if (len < 3) return;
    if (st == 0x90 && d2) note_on(s, d1, d2);
    else if (st == 0x80 || st == 0x90) note_off(s, d1);
    else if (st == 0xe0) s->bend = (((d2 << 7) | d1) - 8192) / 8192.0f;
    else if (st == 0xb0 && d1 == 1) s->wheel = d2 / 127.0f;
    else if (st == 0xb0 && d1 == 64) sustain_pedal(s, d2 >= 64);
    else if (st == 0xb0 && d1 == 66) sostenuto_pedal(s, d2 >= 64);
    else if (st == 0xb0 && d1 == 11) s->exp1 = s->expression = d2 / 127.0f;   /* expression: matrix sources Exp 1, 2 */
    else if (st == 0xb0 && d1 == 4) s->exp2 = d2 / 127.0f;
    else if (st == 0xb0 && d1 == 121) {   /* reset all controllers */
        s->wheel = s->aftertouch = s->bend = s->expression = s->exp1 = s->exp2 = 0.0f;
        sustain_pedal(s, 0);
        sostenuto_pedal(s, 0);
    }
    else if (st == 0xb0 && (d1 == 120 || d1 == 123)) all_off(s, d1 == 120);
}

/* "state": every value as key=value;... (the wrapper saves it with the project) */
static void set_state(one_t *s, const char *str) {
    char *buf = s->state_buf;
    if (strlen(str) >= sizeof s->state_buf) return;   /* never restore half a state */
    strcpy(buf, str);
    load_defaults(s);   /* a state saved before a param existed leaves that param at its default */
    char *save = NULL;
    for (char *kv = strtok_r(buf, ";", &save); kv; kv = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(kv, '=');
        if (!eq) continue;
        *eq = 0;
        store(s, kv, eq + 1);
    }
}

/* only the values that differ from their defaults (set_state starts from the defaults), so a state of hundreds of
 * params fits the wrapper's 8 KB chunk; "v=1" first keeps it non-empty (an empty state would be no chunk at all) */
static int get_state(const one_t *s, char *buf, int n) {
    int used = snprintf(buf, n, "v=1");
    for (int i = 0; i < P_COUNT && used < n; i++)
        if (KEYS[i][0] && s->val[i] != s->def[i]) used += snprintf(buf + used, n - used, ";%s=%g", KEYS[i], s->val[i]);
    return used < n ? used : 0;
}

/* LINK CUTOFF, after param k was set (-1: a whole state): with link on, a cutoff knob's move takes the other knob
 * along at the offset they had when link went on. A knob pushed to its end stays there and the offset comes back
 * once it leaves the end. A state restores both knobs as saved (store() never links). */
static void link_cutoffs(one_t *s, int k) {
    if (k == P_FLT_LINK || k < 0 || s->val[P_FLT_LINK] < 0.5f) s->link_off = s->val[P_SVF_CUT] - s->val[P_FLT_CUT];
    else if (k == P_FLT_CUT) s->val[P_SVF_CUT] = clampf(s->val[P_FLT_CUT] + s->link_off, 0.0f, 100.0f);
    else if (k == P_SVF_CUT) s->val[P_FLT_CUT] = clampf(s->val[P_SVF_CUT] - s->link_off, 0.0f, 100.0f);
}

static void set_param(void *inst, const char *key, const char *val) {
    one_t *s = inst;
    if (!strcmp(key, "lfo_bpm")) {   /* the host's tempo (HAS_LFO_BPM), not a param: only synced envelopes need it applied */
        float bpm = (float)atof(val);
        if (isfinite(bpm) && bpm >= 20.0f && bpm <= 400.0f && bpm != s->bpm) {
            s->bpm = bpm;
            apply_envs(s);   /* synced envelope times and LFO rates follow the tempo */
            apply_lfos(s);
        }
        return;
    }
    if (!strcmp(key, "state")) set_state(s, val);
    else store(s, key, val);
    link_cutoffs(s, slot(key));
    apply(s);
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
            if (!s->ecfg[n].latch && s->voice[i].env[n].held && !s->voice[i].gate) env_release(&s->voice[i].env[n]);
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

/* "<key>_display": the text MPC shows for a knob whose 0..100 position is a log scale or a tempo division */
static int display_text(const one_t *s, int k, char *buf, int n) {
    if (k == P_FLT_CUT || k == P_SVF_CUT) {
        float hz = cut_hz(s->val[k]);
        return hz < 1000.0f ? snprintf(buf, n, "%.0f Hz", hz) : snprintf(buf, n, "%.2f kHz", hz * 0.001f);
    }
    if (k >= P_ENV_A && k < P_ENV_A + NENV * ENV_STRIDE && (k - P_ENV_A) % ENV_STRIDE != 2) {   /* A, D, R: not S */
        if (s->val[P_FENV_SYNC + EX_STRIDE * ((k - P_ENV_A) / ENV_STRIDE)] > 0.5f)
            return snprintf(buf, n, "%s", SYNC_DIV[env_div(s->val[k])].name);
        return time_text(buf, n, env_ms(s->val[k]));
    }
    if (k >= P_FENV_DELAY && k < P_FENV_DELAY + NENV * EX_STRIDE) {   /* delay, hold, time scale */
        int x = (k - P_FENV_DELAY) % EX_STRIDE, e = (k - P_FENV_DELAY) / EX_STRIDE;
        if (x == X_SCALE) return snprintf(buf, n, "%.0f%%", env_scale(s->val[k]) * 100.0f);
        if (x != X_DELAY && x != X_HOLD) return 0;
        if (s->val[k] <= 0.0f) return snprintf(buf, n, "0 ms");
        if (s->val[P_FENV_SYNC + EX_STRIDE * e] > 0.5f) return snprintf(buf, n, "%s", SYNC_DIV[env_div(s->val[k])].name);
        return time_text(buf, n, env_ms(s->val[k]));
    }
    if (k == P_KEY_CENTER) {   /* a note name, middle C (60) as C4, and the MIDI note number */
        static const char *const NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int note = (int)s->center;
        return snprintf(buf, n, "%s%d (%d)", NAMES[note % 12], note / 12 - 1, note);
    }
    if (k == P_GLIDE_TIME) return s->val[k] > 0.0f ? time_text(buf, n, glide_ms(s->val[k])) : snprintf(buf, n, "0 ms");
    if (k == P_MODFX_RATE || k == P_MODFX_RATE_R || k == P_MODFX_VRATE) {
        if (s->val[P_MODFX_SYNC] > 0.5f) return snprintf(buf, n, "%s", fx_div_rate_name(s->val[k]));
        return hz_text(buf, n, k == P_MODFX_VRATE ? fx_vib_hz(s->val[k]) : fx_rate_hz(s->val[k]));
    }
    if (k == P_MODFX_TIME || k == P_MODFX_TIME_R) return time_text(buf, n, fx_mod_time_ms((int)s->val[P_MODFX_TYPE], s->val[k]));
    if (k == P_MODFX_DTIME || k == P_DLY_TIME || k == P_DLY_TIME_R) {
        if (s->val[k == P_MODFX_DTIME ? P_MODFX_SYNC : P_DLY_SYNC] > 0.5f) return snprintf(buf, n, "%s", FX_DIV[fx_div(s->val[k])].name);
        return time_text(buf, n, fx_delay_ms(s->val[k]));
    }
    if (k == P_MODFX_GAIN) {
        float g = fx_gain_x(s->val[k]);
        return g > 0.0f ? snprintf(buf, n, "%+.1f dB", 20.0f * log10f(g)) : snprintf(buf, n, "-inf dB");
    }
    if (k == P_MODFX_SRATE) return hz_text(buf, n, fx_srate_hz(s->val[k]));
    if (k >= P_MODFX_CUT1 && k <= P_MODFX_CUT3) return hz_text(buf, n, fx_cut_hz(s->val[k]));
    if (k == P_MODFX_CTRL1 || k == P_MODFX_CTRL2)   /* the parameter it moves, and by how much */
        return snprintf(buf, n, "%s %+.0f%%", FXD_NAME[fx_ctrl_dst(s, k - P_MODFX_CTRL1)], s->val[k]);
    if (k == P_REV_DECAY)   /* the decay time at this size */
        return snprintf(buf, n, "%.2f s", fx_rev_rt60(s->val[k]) * fx_rev_size(s->val[P_REV_SIZE]));
    if (k == P_REV_SIZE) return snprintf(buf, n, "%.0f%%", fx_rev_size(s->val[k]) * 100.0f);
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

static int get_param(void *inst, const char *key, char *buf, int n) {
    one_t *s = inst;
    if (!strcmp(key, "state")) return get_state(s, buf, n);
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
        int own = x[L_RESET] > 0.5f || repeats >= 1
                  || (s->mod_lfo && (dm[MDL_RATE] != 0.0f || dm[MDL_SEMIS] != 0.0f || dm[MDL_VAR] != 0.0f));
        if (!own) {
            v->lfo_own &= ~(1u << n);
            y = s->lfo_val[n];
        } else {
            lfo_t *l = &v->lfo[n];
            if (!(v->lfo_own >> n & 1)) { *l = s->lfo[n]; v->lfo_own |= 1u << n; }
            if (!waiting && (repeats < 1 || l->cycles < repeats)) {   /* a restarted LFO waits out its delay */
                float hz = dm[MDL_RATE] != 0.0f ? lfo_rate_hz(s, n, s->val[lfo_base(n)] + 100.0f * dm[MDL_RATE]) : s->lfo_hz[n];
                if (dm[MDL_SEMIS] != 0.0f) hz *= exp2f(dm[MDL_SEMIS] * MOD_PITCH_ST / 12.0f);
                lfo_advance(l, fminf(hz * CR / SR, 0.5f));
            }
            if (repeats >= 1 && l->cycles >= repeats) y = 0.0f;
            else y = lfo_smooth(l, lfo_polar(s, n, lfo_shape(l, (int)s->val[lfo_base(n) + 1], x[L_VAR] * 0.01f + dm[MDL_VAR])),
                                s->lfo_coef[n]);
        }
        v->lfo_out[n] = y * g;
    }
}

/* a stage time as a number that is cheap to work out and compare: log10 of the free time in ms (the knob's 1 ms .. 10 s
 * and the time scale's 10% .. 1000%, both log), 1000 + the division when synced, or ENV_Q_OFF for a delay or hold
 * at 0 */
#define ENV_Q_OFF -1e9f
static float env_q(const one_t *s, int n, float knob, int zero_off, float scale_knob) {
    if (zero_off && knob <= 0.0f) return ENV_Q_OFF;
    if (s->val[P_FENV_SYNC + EX_STRIDE * n] > 0.5f) return 1000.0f + (float)env_div(knob);
    return 0.04f * knob + 0.02f * (scale_knob - 50.0f);
}
static float env_q_ms(const one_t *s, float q) {
    if (q <= ENV_Q_OFF * 0.5f) return 0.0f;
    if (q >= 999.0f) return 60000.0f / s->bpm * SYNC_DIV[(int)(q - 1000.0f)].beats;
    return exp2f(q * 3.3219281f);   /* 10^q */
}

/* one voice's envelope settings with the matrix's envelope destinations: the stage times move on the time knobs'
 * scale (+-100% = the whole knob, so tempo-synced times step through the divisions), the time scale on its knob,
 * the sustain level by +-100%. An envelope without any keeps the shared settings. A stage's coefficients are worked
 * out again only when its time moves by more than 1% (an inaudible difference), so a time swept by an LFO costs a
 * few recalculations per cycle, not one every control step. */
static void voice_envs(const one_t *s, voice_t *v, const float *dm) {
    static const int stage[5] = {ENV_DELAY, ENV_ATTACK, ENV_HOLD, ENV_DECAY, ENV_RELEASE};
    static const int knob_md[5] = {MDE_DELAY, MDE_ATTACK, MDE_HOLD, MDE_DECAY, MDE_RELEASE};
    if (!s->mod_env) {   /* no slot reaches an envelope: the shared settings */
        for (int n = 0; n < NENV; n++) v->env[n].cfg = &s->ecfg[n];
        return;
    }
    for (int n = 0; n < NENV; n++) {
        const float *m = &dm[MD_FENV_DELAY + MDE_COUNT * n];
        int any = 0;
        for (int k = 0; k < MDE_COUNT; k++) any |= m[k] != 0.0f;
        if (!any) { v->env[n].cfg = &s->ecfg[n]; continue; }
        const float *e = &s->val[P_ENV_A + ENV_STRIDE * n], *x = &s->val[P_FENV_DELAY + EX_STRIDE * n];
        const float knob[5] = {x[X_DELAY], e[0], x[X_HOLD], e[1], e[3]};
        env_cfg_t *c = &v->ecfg[n];
        if (v->env[n].cfg != c || v->egen[n] != s->env_gen) {   /* start from the shared settings */
            *c = s->ecfg[n];
            v->egen[n] = s->env_gen;
            for (int k = 0; k < 5; k++) v->eq[n][k] = env_q(s, n, knob[k], k == 0 || k == 2, x[X_SCALE]);
        }
        const float bend[5] = {0.0f, -x[X_ACURVE] * 0.01f, 0.0f, x[X_DCURVE] * 0.01f, x[X_RCURVE] * 0.01f};
        float scale = clampf(x[X_SCALE] + 100.0f * m[MDE_SCALE], 0.0f, 100.0f);
        for (int k = 0; k < 5; k++) {
            float q = env_q(s, n, clampf(knob[k] + 100.0f * m[knob_md[k]], 0.0f, 100.0f), k == 0 || k == 2, scale);
            if (fabsf(q - v->eq[n][k]) < 0.0043f) continue;   /* within 1% */
            v->eq[n][k] = q;
            c->seg[stage[k]] = env_seg(env_q_ms(s, q), bend[k], SR);
        }
        c->has_delay = v->eq[n][0] > ENV_Q_OFF * 0.5f;
        c->has_hold = v->eq[n][2] > ENV_Q_OFF * 0.5f;
        c->sustain = clampf(e[2] * 0.01f + m[MDE_SUSTAIN], 0.0f, 1.0f);
        v->env[n].cfg = c;
    }
}

/* one voice's modulation for the next control step: its LFOs, the matrix, pitch, both cutoffs and filter settings,
 * envelope settings, levels */
static void control_voice(one_t *s, voice_t *v) {
    const float *p = s->val;
    float src[MS_COUNT], dm[MD_COUNT];
    glide_step(s, v);
    update_note_hz(s, v);
    voice_lfos(s, v);
    float key = voice_key(s, v);   /* the glided pitch drives keyboard tracking and the Keyboard source */
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
    mod_eval_list(&p[P_MOD_BASE], &p[P_MODX_BASE], s->mod_list, s->mod_n, src, s->bipolar, dm);
    memcpy(v->lfo_dm, &dm[MD_LFO1_RATE], sizeof v->lfo_dm);   /* the LFOs read these next step */
    voice_envs(s, v, dm);
    float fenv = src[MS_FENV], key_oct = kc / 12.0f;

    float semis = v->lfo_out[0] * (p[P_PITCH_LFO] * 0.01f + dm[MD_PITCH_LFO1]) * LFO_PITCH_ST
                  + src[MS_MENV] * (p[P_PITCH_ENV] * 0.01f + dm[MD_PITCH_ENV]) * ENV_PITCH_ST
                  + bend_semis(s, -1) + dm[MD_PITCH] * MOD_PITCH_ST;
    const float osc_semis[NOSC] = {dm[MD_O1_PITCH] * MOD_PITCH_ST + bend_semis(s, 0),
                                   dm[MD_O2_PITCH] * MOD_PITCH_ST + bend_semis(s, 1),
                                   dm[MD_O3_PITCH] * MOD_PITCH_ST + bend_semis(s, 2)};
    for (int n = 0; n < NOSC; n++) v->beat_mod[n] = dm[MD_O1_BEAT + n] * BEAT_HZ;
    set_pitch(s, v, semis, osc_semis);

    v->cut += (p[P_FLT_CUT] - v->cut) * s->glide;
    v->svf_cut += (p[P_SVF_CUT] - v->svf_cut) * s->glide;
    float lfo2 = v->lfo_out[1];
    float lad_oct = fenv * (p[P_LAD_ENV] * 0.01f + dm[MD_LAD_ENV]) * ENV_CUT_OCT
                    + lfo2 * (p[P_LAD_LFO] * 0.01f + dm[MD_LAD_LFO2]) * LFO_CUT_OCT
                    + key_oct * p[P_LAD_KBD] * 0.01f + dm[MD_LAD_CUT] * MOD_CUT_OCT;
    float svf_oct = fenv * (p[P_SVF_ENV] * 0.01f + dm[MD_SVF_ENV]) * ENV_CUT_OCT
                    + lfo2 * (p[P_SVF_LFO] * 0.01f + dm[MD_SVF_LFO2]) * LFO_CUT_OCT
                    + key_oct * p[P_SVF_KBD] * 0.01f + dm[MD_SVF_CUT] * MOD_CUT_OCT;
    ladder_set(&v->lad, cut_hz(v->cut) * exp2f(lad_oct), clampf(p[P_FLT_RES] * 0.01f + dm[MD_LAD_RES], 0.0f, 1.0f),
               clampf(p[P_FLT_DRIVE] * 0.01f + dm[MD_LAD_DRIVE], 0.0f, 1.0f), (int)p[P_FLT_MODE], (int)p[P_FLT_SLOPE], SR);
    svf2_set(&v->svf, cut_hz(v->svf_cut) * exp2f(svf_oct), clampf(p[P_SVF_SPACE] * 0.01f + dm[MD_SVF_SPACE], -1.0f, 1.0f),
             clampf(p[P_SVF_RES] * 0.01f + dm[MD_SVF_RES], 0.0f, 1.0f), (int)p[P_SVF_MODE], (int)p[P_SVF_PAIR], SR);
    set_targets(s, v, dm, v->lfo_out[2]);
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

/* render n (<= CR) samples of one voice, adding into acc; levels, wave shapes, FM depth and the filter mix ramp from
 * the last control step's values to this one's */
static void render_voice(one_t *s, voice_t *v, float *acc, int n, int first) {
    const float *p = s->val;
    float lad[NSRC], svf[NSRC], dlad[NSRC], dsvf[NSRC];
    int sync2 = p[P_O2_SYNC] > 0.5f, sync3 = p[P_O3_SYNC] > 0.5f;
    int sync2_3 = sync2 && p[P_O2_SYNC_SRC] > 0.5f, sync3_2 = sync3 && p[P_O3_SYNC_SRC] > 0.5f;   /* else from osc 1 */
    int series = (int)p[P_FLT_ROUTE] == FLT_SERIES, any_svf = 0;
    int ring23 = (int)p[P_RING_SRC] == RING_2X3, route = (int)p[P_FM_ROUTE];
    float inv = 1.0f / (float)n;
    int heard[NSRC], fading = 0;   /* fading: a level changes during this step */
    for (int k = 0; k < NSRC; k++) {
        lad[k] = ramp_from(v->to_lad[k], v->tgt_lad[k], first, inv, &dlad[k]);
        svf[k] = ramp_from(v->to_svf[k], v->tgt_svf[k], first, inv, &dsvf[k]);
        heard[k] = lad[k] != 0.0f || svf[k] != 0.0f || v->tgt_lad[k] != 0.0f || v->tgt_svf[k] != 0.0f;
        any_svf |= svf[k] != 0.0f || v->tgt_svf[k] != 0.0f;
        fading |= dlad[k] != 0.0f || dsvf[k] != 0.0f;
        v->to_lad[k] = v->tgt_lad[k];
        v->to_svf[k] = v->tgt_svf[k];
    }
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
    /* filter FM (octaves at a full-scale sample) from oscillator lad_src / svf_src; off costs nothing per sample */
    int lad_src = (int)p[P_LAD_FM_SRC], svf_src = (int)p[P_SVF_FM_SRC];
    float lad_fm = run_lad ? v->lad_fm * FILTER_FM_OCT : 0.0f;
    float svf_fm = any_svf ? v->svf_fm * FILTER_FM_OCT : 0.0f;
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
        /* linear FM: the carrier's frequency moves with the modulator's output (3 > 1 uses osc 3's last sample) */
        if (fm_on && route == FM_3TO1) o1->inc = clampf(v->inc0[0] * (1.0f + fm * v->fm_prev), 0.0f, 0.49f);
        float a = osc_next(o1, &sh[0], -1.0f), wrap = o1->wrap_d;
        if (fm_on && route == FM_1TO2) o2->inc = clampf(v->inc0[1] * (1.0f + fm * a), 0.0f, 0.49f);
        if (fm_on && route == FM_1TO3) o3->inc = clampf(v->inc0[2] * (1.0f + fm * a), 0.0f, 0.49f);
        /* hard sync: osc 2 from osc 3 looks ahead to osc 3's wrap; osc 3 from osc 2 takes osc 2's wrap this sample
         * (its last one when the two sync to each other) */
        float m3 = sync3_2 ? o2->wrap_d : wrap;
        float b = run2 ? osc_next(o2, &sh[1], sync2 ? (sync2_3 ? osc_wrap_ahead(o3, sync3 ? m3 : -1.0f) : wrap) : -1.0f)
                       : 0.0f;
        float c = run3 ? osc_next(o3, &sh[2], sync3 ? (sync3_2 && !sync2_3 ? o2->wrap_d : m3) : -1.0f) : 0.0f;
        v->fm_prev = c;
        float rm = heard[RING] ? o2->tri_out * (ring23 ? o3->tri_out : o1->tri_out) : 0.0f;
        float nz = white(&v->noise);
        float lad_in = a * lad[0] + b * lad[1] + c * lad[2] + nz * lad[NOISE] + rm * lad[RING];
        float svf_out = 0.0f, lad_out = 0.0f;
        if (any_svf) {
            float in = a * svf[0] + b * svf[1] + c * svf[2] + nz * svf[NOISE] + rm * svf[RING] + nz * NOISE_FLOOR;
            svf_out = svf_fm > 0.0f ? svf2_next_fm(&v->svf, in, exp2_fast(svf_fm * (svf_src == 0 ? a : svf_src == 1 ? b : c)))
                                    : svf2_next(&v->svf, in);
            if (series) lad_in += svf_out;
        }
        if (run_lad)
            lad_out = lad_fm > 0.0f ? ladder_next_fm(&v->lad, lad_in + nz * NOISE_FLOOR,
                                                     exp2_fast(lad_fm * (lad_src == 0 ? a : lad_src == 1 ? b : c)))
                                    : ladder_next(&v->lad, lad_in + nz * NOISE_FLOOR);
        float f = lad_out * ml + svf_out * (1.0f - ml);
        v->gain += (target - v->gain) * s->gain_coef;
        if (fabsf(v->gain - target) < 1e-7f) v->gain = target;   /* settle exactly: no denormal tail */
        acc[i] += f * env_next(&v->env[ENV_AMP]) * v->gain;
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
};

/* the effect settings in the units fx.h reads. CTRL 1 and CTRL 2 move their targets by up to +-100% of the target's
 * range (a performance offset on top of the knob, as the panel's two quick controls). */
static void fx_params(const one_t *s, fx_params_t *f) {
    float p[P_COUNT];
    memcpy(p, s->val, sizeof p);
    for (int c = 0; c < 2; c++) {
        int d = fx_ctrl_dst(s, c), k = FXD_SLOT[d].slot;
        if (k >= 0) p[k] = clampf(p[k] + s->val[P_MODFX_CTRL1 + c] * 0.01f * (FXD_SLOT[d].hi - FXD_SLOT[d].lo),
                                  FXD_SLOT[d].lo, FXD_SLOT[d].hi);
    }
    int type = (int)p[P_MODFX_TYPE], sync = p[P_MODFX_SYNC] > 0.5f, dsync = p[P_DLY_SYNC] > 0.5f;
    f->sfx_on = p[P_MODFX_ON] > 0.5f;
    f->sfx_type = type;
    f->mix = p[P_MODFX_MIX] * 0.01f;
    f->gain = fx_gain_x(p[P_MODFX_GAIN]);
    f->fb = p[P_MODFX_FB] * 0.0095f;   /* +-95% */
    f->fb_r = p[P_MODFX_FB_R] * 0.0095f;
    f->dfb = p[P_MODFX_DFB] * 0.0095f;
    f->time_ms = fx_mod_time_ms(type, p[P_MODFX_TIME]);
    f->time_r_ms = fx_mod_time_ms(type, p[P_MODFX_TIME_R]);
    f->dtime_ms = sync ? fx_div_ms(p[P_MODFX_DTIME], s->bpm) : fx_delay_ms(p[P_MODFX_DTIME]);
    f->rate_hz = sync ? fx_div_hz(p[P_MODFX_RATE], s->bpm) : fx_rate_hz(p[P_MODFX_RATE]);
    f->rate_r_hz = sync ? fx_div_hz(p[P_MODFX_RATE_R], s->bpm) : fx_rate_hz(p[P_MODFX_RATE_R]);
    f->vib_hz = sync ? fx_div_hz(p[P_MODFX_VRATE], s->bpm) : fx_vib_hz(p[P_MODFX_VRATE]);
    f->depth = p[P_MODFX_DEPTH] * 0.01f;
    f->tone = p[P_MODFX_TONE] * 0.01f;
    f->spread = p[P_MODFX_SPREAD] * 0.01f;
    f->sweep = p[P_MODFX_SWEEP] * 0.01f;
    f->noise = p[P_MODFX_NOISE] * 0.01f;
    f->vib_depth = p[P_MODFX_VDEPTH] * 0.01f;
    f->flutter = p[P_MODFX_FLUTTER] * 0.01f;
    f->wow = p[P_MODFX_WOW] * 0.01f;
    f->bias = p[P_MODFX_BIAS] * 0.01f;
    f->room = p[P_MODFX_ROOM] * 0.01f;
    f->decay = p[P_MODFX_DECAY] * 0.01f;
    f->bits = p[P_MODFX_BITS];
    f->srate_hz = fx_srate_hz(p[P_MODFX_SRATE]);
    f->reso = p[P_MODFX_RESO] * 0.01f;
    for (int j = 0; j < 3; j++) {
        f->cut_hz[j] = fx_cut_hz(p[P_MODFX_CUT1 + j]);
        f->lvl[j] = p[P_MODFX_LVL1 + j] * 0.01f;
    }
    f->ftype = (int)p[P_MODFX_FTYPE];
    f->mfx_on = p[P_MFX_ON] > 0.5f;
    f->send1 = p[P_MFX_SEND1] * 0.01f;
    f->send2 = p[P_MFX_SEND2] * 0.01f;
    f->series = p[P_MFX_SERIES] * 0.01f;
    f->width = p[P_MFX_WIDTH] * 0.01f;
    f->dly_l_ms = dsync ? fx_div_ms(p[P_DLY_TIME], s->bpm) : fx_delay_ms(p[P_DLY_TIME]);
    f->dly_r_ms = dsync ? fx_div_ms(p[P_DLY_TIME_R], s->bpm) : fx_delay_ms(p[P_DLY_TIME_R]);
    f->dly_fb = p[P_DLY_FB] * 0.0095f;
    f->dly_tone = p[P_DLY_TONE] * 0.01f;
    f->dly_offset = p[P_DLY_OFFSET] * 0.01f;
    f->dly_pp = p[P_DLY_PP] > 0.5f;
    f->rev_type = (int)p[P_REV_TYPE];
    f->rev_freeze = p[P_REV_FREEZE] > 0.5f;
    f->rev_size = fx_rev_size(p[P_REV_SIZE]);
    f->rev_rt60 = fx_rev_rt60(p[P_REV_DECAY]) * f->rev_size;   /* a bigger space rings longer */
    f->rev_damp = p[P_REV_DAMP] * 0.009f;
    f->rev_lowdamp = p[P_REV_LOWDAMP] * 0.01f;
    f->rev_pre_ms = p[P_REV_PRE];
    f->rev_diff = p[P_REV_DIFF] * 0.01f;
    f->rev_tone = p[P_REV_TONE] * 0.01f;
    f->rev_mod = p[P_REV_MOD] * 0.02f;
    f->rev_early = p[P_REV_EARLY] * 0.01f;
}

static void render(void *inst, int16_t *out, int frames) {
    one_t *s = inst;
    float acc[128], l[128], r[128];
    fx_params_t fxp;
    fx_params(s, &fxp);
    int done = 0;
    while (done < frames) {
        int first = s->phase == 0;
        int n = CR - s->phase;
        if (n > frames - done) n = frames - done;
        if (first) control_lfos(s);
        if (n > 128) n = 128;
        memset(acc, 0, sizeof(float) * n);
        for (int k = 0; k < MAX_VOICES; k++) {   /* voices past the current count still finish their release */
            voice_t *v = &s->voice[k];
            if (v->env[ENV_AMP].stage == ENV_IDLE) continue;
            if (first) control_voice(s, v);
            render_voice(s, v, acc, n, first);
        }
        fx_process(&s->fx, &fxp, acc, acc, l, r, n);   /* stereo in: the mono voice sum on both sides for now */
        for (int i = 0; i < n; i++) {   /* a last guard: never NaN to the host */
            float yl = isfinite(l[i]) ? soft_limit(l[i]) : 0.0f, yr = isfinite(r[i]) ? soft_limit(r[i]) : 0.0f;
            out[2 * (done + i)] = (int16_t)(yl * 32767.0f);
            out[2 * (done + i) + 1] = (int16_t)(yr * 32767.0f);
        }
        done += n;
        s->phase = (s->phase + n) % CR;
    }
}

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, NULL, NULL};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
