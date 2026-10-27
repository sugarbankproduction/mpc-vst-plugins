/* ONE-ish patch randomiser: RANDOMISE (a new sound to play), MUTATE (a variation of the current one), per-section locks.
 *
 * Self-contained: it works on the plugin's parameter array in VST order, in normalized form (0..1, what setParameter and
 * getParameter use), and knows the params only by key (params.h's PARAMS: range, default, options). It allocates
 * nothing, does no I/O, and is deterministic for a given seed (its own xorshift). The editor calls it on the UI thread;
 * the audio thread never does. Multitimbral parts later: a part's block of params can be passed the same way, through a
 * small key map (the rules below name keys, not slots).
 *
 * How RANDOMISE makes a sound (rnd_generate):
 *   1. every param in an unlocked section starts from its default, so nothing odd is left over from the last sound;
 *      params in locked sections, and those it never touches (the player's setup: volume, tuning, transpose, octave
 *      shift, key centre, bend ranges, velocity curve, voice stealing, note priority, hold, the ribbon, link cutoff,
 *      pitch tracking, bend offsets, sync sources), keep their current values
 *   2. an archetype (bass, lead, pad, pluck, poly, texture) shapes footages, envelopes, filter, effects and voicing so
 *      the parts suit each other
 *   3. each param draws a value from its rule in RND_RULES (a musical range, how often it stays off, option weights)
 *   4. code makes the choices that need several params to agree: the oscillators and their footages, the filter
 *      topology and the mixer buttons that feed it, envelopes per archetype, LFO rates for what they drive, 0-4 matrix
 *      rows from a list of tried pairings (RND_MODS), the effects
 *   5. a safety pass (rnd_safety, after MUTATE too) makes sure it is heard and is not harsh
 * MUTATE moves each continuous param (with probability `amount`) by a gaussian step of `amount` x its musical span
 * (RND_RULES' mut), switches an option now and then (probability amount / 4, by the rule's weights), nudges the matrix,
 * then runs the same safety pass. Things that are off (a level at 0, no FM) mostly stay off at small amounts. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "params.h"

/* the lock sections (bit n of `locks` keeps section n as it is) */
enum { RS_OSC, RS_MIX, RS_FILTER, RS_ENV, RS_MOD, RS_FX, RS_PERF, RS_COUNT, RS_NEVER = 255 };
__attribute__((unused)) static const char *const RND_SECTION[RS_COUNT] = {"OSCILLATORS", "MIXER", "FILTERS", "ENVELOPES", "MODULATION",
                                                  "EFFECTS", "PERFORMANCE"};
enum { RA_BASS, RA_LEAD, RA_PAD, RA_PLUCK, RA_POLY, RA_TEXTURE, RA_COUNT };
__attribute__((unused)) static const char *const RND_ARCH[RA_COUNT] = {"Bass", "Lead", "Pad", "Pluck", "Poly", "Texture"};

/* ---- the rules: one per key pattern, the first match wins ----
 * kind: RK_DEF  RANDOMISE resets it to its default (MUTATE may still nudge it by mut)
 *       RK_UNI  uniform in lo..hi, or z with probability pz
 *       RK_BELL the mean of two uniforms in lo..hi (most values near the middle), or z with probability pz
 *       RK_OPT  an option by the weights in w ("2 3": the second option 1.5x as likely as the first)
 *       RK_CODE set by the code below (structural); lo..hi bound MUTATE, and pz = 1 marks z as its "off" value
 * mut:  MUTATE's step at amount 100%, as a fraction of the param's whole range (0: MUTATE leaves it alone); for an
 *       option, any mut > 0 lets MUTATE switch it (by w, or evenly without w)
 * Values are in each param's own units (params.json). Knob scales used below: cutoffs 0..100 = 20 Hz..20 kHz, 10 per
 * octave (25 = 112 Hz, 50 = 632 Hz, 70 = 2.5 kHz); envelope times 25 per decade (0 = 1 ms, 50 = 100 ms, 75 = 1 s);
 * LFO rates in the Low range 0.01..10 Hz (43 = 0.2 Hz, 67 = 1 Hz, 90 = 5 Hz). A key matched by no rule is never
 * touched, so a param added later stays put until it gets a rule here. */
enum { RK_DEF, RK_UNI, RK_BELL, RK_OPT, RK_CODE };
typedef struct {
    const char *pat;   /* a key, or one '*' standing for any run of characters */
    unsigned char sec, kind;
    float lo, hi, pz, z;
    const char *w;
    float mut;
} rnd_rule_t;
#define RNEVER(p) {p, RS_NEVER, RK_DEF, 0, 0, 0, 0, NULL, 0}
#define RDEF(p, s, mut) {p, s, RK_DEF, 0, 0, 0, 0, NULL, mut}
#define RUNI(p, s, lo, hi, pz, z, mut) {p, s, RK_UNI, lo, hi, pz, z, NULL, mut}
#define RBELL(p, s, lo, hi, pz, z, mut) {p, s, RK_BELL, lo, hi, pz, z, NULL, mut}
#define ROPT(p, s, w, mut) {p, s, RK_OPT, 0, 0, 0, 0, w, mut}
#define RCODE(p, s, lo, hi, mut) {p, s, RK_CODE, lo, hi, 0, 0, NULL, mut}
#define RCODEZ(p, s, lo, hi, z, mut) {p, s, RK_CODE, lo, hi, 1, z, NULL, mut}   /* z: its "off" value */
#define RCODEW(p, s, w, mut) {p, s, RK_CODE, 0, 0, 0, 0, w, mut}
/* each envelope's extras: delay, hold, loop, latch and sync stay off (code adds a rare loop); curves vary a little */
#define RENVX(e) \
    RDEF(e "_delay", RS_ENV, 0), RDEF(e "_hold", RS_ENV, 0.02f), RBELL(e "_acurve", RS_ENV, -50, 50, 0.7f, 0, 0.08f), \
    RBELL(e "_dcurve", RS_ENV, 0, 90, 0.6f, 50, 0.08f), RBELL(e "_rcurve", RS_ENV, 0, 90, 0.6f, 50, 0.08f), \
    RDEF(e "_loop", RS_ENV, 0), RDEF(e "_latch", RS_ENV, 0), RDEF(e "_sync", RS_ENV, 0), RDEF(e "_multi", RS_ENV, 0), \
    ROPT(e "_reset", RS_ENV, "5 1", 1), RBELL(e "_scale", RS_ENV, 42, 58, 0.7f, 50, 0.03f), \
    RCODE(e "_vel", RS_ENV, 0, 100, 0.05f)

__attribute__((unused)) static const rnd_rule_t RND_RULES[] = {
    /* ---- never touched: the player's setup, not the sound ---- */
    RNEVER("volume"), RNEVER("master_tune"), RNEVER("fine_tune"), RNEVER("transpose"), RNEVER("octave_shift"),
    RNEVER("key_center"), RNEVER("bend_up"), RNEVER("bend_down"), RNEVER("vel_curve"), RNEVER("steal_mode"),
    RNEVER("note_priority"), RNEVER("hold"), RNEVER("ribbon_*"), RNEVER("flt_link"), RNEVER("o*_track"),
    RNEVER("o*_off"), RNEVER("o*_sync_src"),
    RNEVER("mac_*"),   /* the MACRO channel (Plaits) has no rules yet: RANDOMISE and MUTATE leave it as it is */

    /* ---- OSCILLATORS: shapes, footages, tuning, sync, FM, ring source, LFO 3's wave-shape amounts ---- */
    ROPT("o*_wave", RS_OSC, "2 3", 1),                    /* Tri, Saw */
    RCODEW("o*_oct", RS_OSC, "0.2 2 4 1.5 0.3", 1),      /* by archetype; MUTATE favours 16' and 8' */
    RCODE("o*_freq", RS_OSC, -7, 7, 0.006f),             /* a slight detune or an interval; the safety pass keeps it in tune */
    RBELL("o*_beat", RS_OSC, -1.2f, 1.2f, 0.8f, 0, 0.015f),
    RUNI("o*_angle", RS_OSC, 0, 100, 0.35f, 0, 0.2f),
    RBELL("o*_pw", RS_OSC, 15, 85, 0.35f, 50, 0.12f),
    RCODE("o*_mix", RS_OSC, 0, 100, 0.2f),               /* 0 (tri/saw), 100 (pulse) or between */
    RCODEW("o*_sync", RS_OSC, "9 1", 1),
    ROPT("o*_reset", RS_OSC, "6 1", 1),
    ROPT("ring_src", RS_OSC, "1 1", 1),
    RCODEZ("fm_amt", RS_OSC, 0, 60, 0, 0.06f),
    ROPT("fm_route", RS_OSC, "2 1 1", 1),
    RUNI("angle_lfo3", RS_OSC, 10, 50, 0.8f, 0, 0.08f),
    RUNI("pw_lfo3", RS_OSC, 10, 40, 0.85f, 0, 0.08f),

    /* ---- MIXER: levels, the filter buttons (set with the topology), noise colour and envelope ---- */
    RCODEZ("o1_level", RS_MIX, 50, 100, 0, 0.06f),
    RCODEZ("o*_level", RS_MIX, 0, 95, 0, 0.06f),
    RCODEZ("noise_level", RS_MIX, 0, 45, 0, 0.04f),
    RCODEZ("ring_level", RS_MIX, 0, 70, 0, 0.04f),
    RCODEW("*_svf", RS_MIX, NULL, 0),
    RCODEW("*_lad", RS_MIX, NULL, 0),
    ROPT("noise_color_pair", RS_MIX, "1 1 1", 1),
    RUNI("noise_color_mix", RS_MIX, 0, 100, 0, 0, 0.2f),
    ROPT("noise_env", RS_MIX, "6 1", 1),
    RUNI("noise_a", RS_MIX, 0, 75, 0, 0, 0.08f),
    RUNI("noise_r", RS_MIX, 40, 85, 0, 0, 0.08f),
    ROPT("noise_sus", RS_MIX, "1 2", 1),

    /* ---- FILTERS: both filters, their topology and mix, envelope / LFO / keyboard amounts, filter FM ---- */
    RCODE("flt_cut", RS_FILTER, 20, 90, 0.05f),
    RCODE("svf_cut", RS_FILTER, 20, 90, 0.05f),
    RCODE("flt_res", RS_FILTER, 0, 85, 0.07f),
    RCODE("svf_res", RS_FILTER, 0, 75, 0.07f),
    RBELL("flt_drive", RS_FILTER, 0, 50, 0.35f, 0, 0.07f),
    RCODEW("flt_mode", RS_FILTER, "9 1", 1),             /* LP, HP */
    ROPT("flt_slope", RS_FILTER, "6 1.5 2 0.5", 1),      /* 24, 18, 12, 6 dB */
    RCODEW("svf_mode", RS_FILTER, "4 3 1.5 1.5", 1),     /* LP, BP, HP, notch */
    RCODEW("flt_route", RS_FILTER, NULL, 0),
    RCODE("flt_mix", RS_FILTER, 0, 100, 0.04f),
    RCODE("lad_env", RS_FILTER, -40, 80, 0.04f),
    RCODE("svf_env", RS_FILTER, -40, 80, 0.04f),
    RBELL("lad_lfo", RS_FILTER, -40, 40, 0.8f, 0, 0.03f),
    RBELL("svf_lfo", RS_FILTER, -40, 40, 0.8f, 0, 0.03f),
    RBELL("lad_kbd", RS_FILTER, 0, 70, 0.15f, 0, 0.03f),
    RBELL("svf_kbd", RS_FILTER, 0, 70, 0.15f, 0, 0.03f),
    RBELL("svf_space", RS_FILTER, -50, 50, 0.65f, 0, 0.05f),
    ROPT("svf_pair", RS_FILTER, "1 1", 1),
    RBELL("lad_fm", RS_FILTER, 5, 30, 0.9f, 0, 0.04f),
    RBELL("svf_fm", RS_FILTER, 5, 30, 0.9f, 0, 0.04f),
    ROPT("lad_fm_src", RS_FILTER, "1 1 1", 1),
    ROPT("svf_fm_src", RS_FILTER, "1 1 1", 1),

    /* ---- ENVELOPES: filter, amp and mod envelopes (times by archetype) and their extras ---- */
    RCODE("amp_a", RS_ENV, 0, 85, 0.04f), RCODE("amp_d", RS_ENV, 30, 90, 0.04f), RCODE("amp_s", RS_ENV, 0, 100, 0.08f),
    RCODE("amp_r", RS_ENV, 25, 88, 0.04f),
    RCODE("fenv_a", RS_ENV, 0, 85, 0.04f), RCODE("fenv_d", RS_ENV, 30, 90, 0.04f),
    RCODE("fenv_s", RS_ENV, 0, 100, 0.08f), RCODE("fenv_r", RS_ENV, 25, 88, 0.04f),
    RCODE("menv_a", RS_ENV, 0, 85, 0.04f), RCODE("menv_d", RS_ENV, 20, 90, 0.04f),
    RCODE("menv_s", RS_ENV, 0, 100, 0.08f), RCODE("menv_r", RS_ENV, 25, 88, 0.04f),
    RENVX("fenv"), RENVX("amp"), RENVX("menv"),

    /* ---- EFFECTS: the synth effect (type and its controls by family), master delay and reverb sends ---- */
    RCODE("modfx_on", RS_FX, 0, 0, 0), RCODE("modfx_type", RS_FX, 0, 0, 0),
    RCODE("modfx_rate", RS_FX, 0, 70, 0.04f), RCODE("modfx_rate_r", RS_FX, 0, 70, 0.04f),
    RUNI("modfx_depth", RS_FX, 20, 70, 0, 0, 0.07f),
    RCODE("modfx_fb", RS_FX, -60, 60, 0.03f), RCODE("modfx_fb_r", RS_FX, -60, 60, 0.03f),
    RCODE("modfx_mix", RS_FX, 10, 60, 0.05f),
    RCODE("modfx_time", RS_FX, 0, 100, 0.03f), RCODE("modfx_time_r", RS_FX, 0, 100, 0.03f),
    RCODE("modfx_dtime", RS_FX, 0, 100, 0.02f), RDEF("modfx_dfb", RS_FX, 0.03f), RDEF("modfx_tone", RS_FX, 0.04f),
    RUNI("modfx_spread", RS_FX, 30, 90, 0, 0, 0.06f), RDEF("modfx_gain", RS_FX, 0), RCODE("modfx_sync", RS_FX, 0, 0, 0),
    RDEF("modfx_sweep", RS_FX, 0.04f), RDEF("modfx_noise", RS_FX, 0), RDEF("modfx_vrate", RS_FX, 0.04f),
    RDEF("modfx_vdepth", RS_FX, 0.04f), RDEF("modfx_flutter", RS_FX, 0.04f), RDEF("modfx_wow", RS_FX, 0.04f),
    RDEF("modfx_bias", RS_FX, 0.04f), RDEF("modfx_room", RS_FX, 0.04f), RDEF("modfx_decay", RS_FX, 0.04f),
    RCODE("modfx_bits", RS_FX, 4, 16, 0.03f), RCODE("modfx_srate", RS_FX, 40, 100, 0.03f),
    RDEF("modfx_reso", RS_FX, 0.04f), RDEF("modfx_cut*", RS_FX, 0.03f), RDEF("modfx_lvl*", RS_FX, 0),
    RDEF("modfx_ftype", RS_FX, 0), RDEF("modfx_ctrl*", RS_FX, 0), RDEF("modfx_c*_dst", RS_FX, 0),
    RCODE("mfx_on", RS_FX, 0, 0, 0),
    RCODEZ("mfx_send*", RS_FX, 0, 50, 0, 0.04f), RCODEZ("mfx_series", RS_FX, 0, 50, 0, 0.03f),
    RBELL("mfx_width", RS_FX, 80, 140, 0.6f, 100, 0.02f),
    RCODE("dly_time", RS_FX, 0, 100, 0.02f), RCODE("dly_time_r", RS_FX, 0, 100, 0.02f), RCODE("dly_sync", RS_FX, 0, 0, 0),
    RUNI("dly_fb", RS_FX, 15, 50, 0, 0, 0.05f), RUNI("dly_tone", RS_FX, 35, 75, 0, 0, 0.05f),
    ROPT("dly_type", RS_FX, "0 0 0 0 0 0 2 1", 1), RDEF("dly_offset", RS_FX, 0),
    RCODE("rev_decay", RS_FX, 30, 85, 0.03f), RUNI("rev_damp", RS_FX, 20, 60, 0, 0, 0.05f),
    RUNI("rev_pre", RS_FX, 0, 40, 0.3f, 10, 0.01f), ROPT("rev_type", RS_FX, "1 1 1", 1),
    RUNI("rev_size", RS_FX, 35, 75, 0, 0, 0.04f), RBELL("rev_diff", RS_FX, 70, 100, 0, 0, 0.04f),
    RUNI("rev_tone", RS_FX, 60, 100, 0, 0, 0.04f), RUNI("rev_lowdamp", RS_FX, 0, 30, 0, 0, 0.04f),
    RUNI("rev_mod", RS_FX, 20, 70, 0, 0, 0.05f), RUNI("rev_early", RS_FX, 30, 70, 0, 0, 0.05f),
    RDEF("rev_freeze", RS_FX, 0),

    /* ---- MODULATION: LFOs, the hard-wired pitch amounts, the matrix (rows made whole by rnd_gen_matrix) ---- */
    RCODE("lfo*_rate", RS_MOD, 43, 97, 0.03f),
    ROPT("lfo*_wave", RS_MOD, "3 4 1 1 1 1 0.5", 1),   /* triangle, sine, square, saw, ramp, S&H, noise */
    RCODEW("lfo*_sync", RS_MOD, NULL, 0),
    RDEF("lfo*_range", RS_MOD, 0),                      /* Low: the rates above */
    RUNI("lfo*_var", RS_MOD, 0, 60, 0.8f, 0, 0.08f),
    RDEF("lfo*_polarity", RS_MOD, 0),
    RUNI("lfo*_phase", RS_MOD, 0, 100, 0.5f, 0, 0.1f),
    ROPT("lfo*_reset", RS_MOD, "5 1", 1),
    RCODEZ("lfo*_delay", RS_MOD, 0, 70, 0, 0.03f),
    RCODEZ("lfo*_fade_in", RS_MOD, 0, 80, 0, 0.03f),
    RDEF("lfo*_fade_out", RS_MOD, 0), RDEF("lfo*_repeats", RS_MOD, 0),
    RUNI("lfo*_smooth", RS_MOD, 0, 50, 0.8f, 0, 0.05f),
    RDEF("lfo*_timing", RS_MOD, 0),
    RCODEZ("pitch_lfo", RS_MOD, 0, 4, 0, 0.004f),       /* LFO 1 vibrato: % of +-12 st */
    RCODEZ("pitch_env", RS_MOD, -15, 15, 0, 0.005f),    /* mod envelope to pitch: % of +-24 st */
    RCODE("mod*_via_amt", RS_MOD, -100, 100, 0), RCODE("mod*_xf_amt", RS_MOD, 0, 100, 0),
    RCODE("mod*_xf", RS_MOD, 0, 0, 0), RCODE("mod*_src", RS_MOD, 0, 0, 0), RCODE("mod*_dst", RS_MOD, 0, 0, 0),
    RCODE("mod*_via", RS_MOD, 0, 0, 0), RCODE("mod*_amt", RS_MOD, -100, 100, 0),

    /* ---- PERFORMANCE: voices, glide, unison, analog character, stereo ---- */
    RCODEW("voices", RS_PERF, NULL, 0), RCODEW("glide", RS_PERF, NULL, 0),
    RCODE("glide_time", RS_PERF, 25, 65, 0.03f), ROPT("glide_type", RS_PERF, "2 1 1", 1),
    ROPT("glide_legato", RS_PERF, "1 1", 1), ROPT("glide_gated", RS_PERF, "4 1", 1), ROPT("glissando", RS_PERF, "15 1", 1),
    RCODEW("unison", RS_PERF, NULL, 0), RCODEW("uni_count", RS_PERF, NULL, 0),
    RBELL("uni_detune", RS_PERF, 10, 40, 0, 0, 0.05f), RUNI("pitch_var", RS_PERF, 1, 8, 0.8f, 0, 0.02f),
    RCODEZ("an_*", RS_PERF, 0, 70, 0, 0.05f), RCODEZ("drift", RS_PERF, 0, 70, 0, 0.05f),
    RCODEZ("spread", RS_PERF, 0, 90, 0, 0.06f), RDEF("pan", RS_PERF, 0),
};
#define RND_NRULES ((int)(sizeof RND_RULES / sizeof RND_RULES[0]))

/* tried matrix pairings: source, destination, amount range (%), a condition (the destination is heard) and a weight;
 * bip: either sign. No pitch row here except the gentle ones: pitch moves by matrix only as vibrato (slot 1, the
 * default LFO 1 x Mod Wheel row) or as a sync sweep on a synced oscillator. */
enum { MC_ANY, MC_LAD, MC_SVF, MC_PW1, MC_ANGLE1, MC_O2, MC_FM, MC_SYNC2, MC_SFX, MC_POLY };
typedef struct { const char *src, *dst; float lo, hi; unsigned char cond, bip; float wt; } rnd_mod_t;
__attribute__((unused)) static const rnd_mod_t RND_MODS[] = {
    {"Velocity", "Ladder Cutoff", 10, 30, MC_LAD, 0, 3},     {"Velocity", "SVF Cutoff", 10, 30, MC_SVF, 0, 2},
    {"Velocity", "Ladder EG Amt", 10, 30, MC_LAD, 0, 2},     {"Velocity", "Amp Level", 10, 25, MC_ANY, 0, 1},
    {"Mod Wheel", "Ladder Cutoff", 20, 50, MC_LAD, 0, 2},    {"Mod Wheel", "SVF Cutoff", 20, 50, MC_SVF, 0, 1},
    {"Aftertouch", "Ladder Cutoff", 15, 40, MC_LAD, 0, 2},   {"Aftertouch", "Pitch LFO1 Amt", 1, 3, MC_ANY, 0, 1},
    {"LFO 2", "Osc 1 PW", 10, 30, MC_PW1, 1, 2},             {"LFO 3", "Osc 1 Angle", 10, 35, MC_ANGLE1, 1, 2},
    {"LFO 4", "Osc 2 Angle", 10, 35, MC_O2, 1, 1},           {"LFO 3", "Osc 2 Level", 10, 30, MC_O2, 1, 1},
    {"Mod Env", "FM Amount", 10, 40, MC_FM, 0, 3},           {"Mod Env", "Osc 2 Pitch", 15, 45, MC_SYNC2, 0, 6},
    {"LFO 4", "Pan", 15, 50, MC_POLY, 1, 1},                 {"Random", "Ladder Cutoff", 3, 10, MC_LAD, 0, 1},
    {"Keyboard", "Amp Env Decay", -25, -5, MC_ANY, 0, 1},    {"LFO 4", "SVF Cutoff", 10, 35, MC_SVF, 1, 2},
    {"Mod Env", "Ladder Res", 10, 30, MC_LAD, 0, 1},         {"LFO 2", "Synth FX Mix", 10, 25, MC_SFX, 1, 1},
    {"Mod Wheel", "Synth FX Mix", 15, 40, MC_SFX, 0, 1},     {"Mod Wheel", "LFO 2 Rate", 20, 50, MC_ANY, 0, 1},
    {"Mod Env", "Noise Level", 10, 35, MC_ANY, 0, 1},        {"Aftertouch", "Amp Level", 5, 20, MC_ANY, 0, 1},
};
#define RND_NMODS ((int)(sizeof RND_MODS / sizeof RND_MODS[0]))

/* ---- small helpers ---- */
typedef struct {
    float w[NPARAMS + 1];          /* working values in each param's own units; [NPARAMS] takes writes to unknown keys */
    unsigned char rule[NPARAMS];   /* each param's rule (255: none, never touched) */
    unsigned locks;
    uint32_t *seed;
    int arch;
} rnd_ctx_t;

static inline float rnd_u(rnd_ctx_t *c) {   /* xorshift32: 0..1 */
    uint32_t x = *c->seed ? *c->seed : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *c->seed = x;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}
static inline float rnd_range(rnd_ctx_t *c, float lo, float hi) { return lo + (hi - lo) * rnd_u(c); }
static inline float rnd_bell(rnd_ctx_t *c, float lo, float hi) { return lo + (hi - lo) * 0.5f * (rnd_u(c) + rnd_u(c)); }
static inline float rnd_logr(rnd_ctx_t *c, float lo, float hi) { return lo * powf(hi / lo, rnd_u(c)); }   /* log-uniform */
static inline int rnd_chance(rnd_ctx_t *c, float p) { return rnd_u(c) < p; }
static inline float rnd_gauss(rnd_ctx_t *c) {   /* about a unit gaussian, within +-3.5 */
    return (rnd_u(c) + rnd_u(c) + rnd_u(c) + rnd_u(c) - 2.0f) * 1.7320508f;
}
static inline int rnd_pickw(rnd_ctx_t *c, const char *w, int n) {   /* an index by the weights in w (missing: 0) */
    float wt[64], sum = 0.0f;
    if (n > 64) n = 64;
    for (int k = 0; k < n; k++) {
        char *end;
        wt[k] = w && *w ? strtof(w, &end) : 0.0f;
        if (w && *w) w = end;
        sum += wt[k] > 0.0f ? wt[k] : 0.0f;
    }
    if (sum <= 0.0f) return (int)(rnd_u(c) * n) % n;
    float r = rnd_u(c) * sum;
    for (int k = 0; k < n; k++) {
        if (wt[k] <= 0.0f) continue;
        if ((r -= wt[k]) < 0.0f) return k;
    }
    for (int k = n - 1; k >= 0; k--)
        if (wt[k] > 0.0f) return k;
    return 0;
}

/* knob positions from musical units (as presets/make_presets.py) */
static inline float rnd_ms(float t) { return t <= 1.0f ? 0.0f : 25.0f * log10f(t); }            /* envelope times */
static inline float rnd_hz(float f) { return 100.0f * logf(f / 20.0f) / logf(1000.0f); }        /* cutoffs */
static inline float rnd_lfo(float f) { return 100.0f * logf(f / 0.01f) / logf(1000.0f); }       /* LFO rate, Low range */
static inline float rnd_logk(float v, float lo, float hi) { return 100.0f * logf(v / lo) / logf(hi / lo); }
static inline float rnd_fxrate(float hz) { return rnd_logk(hz, 0.01f, 20.0f); }                 /* synth effect rates */
static inline float rnd_dms(float t) { return rnd_logk(t, 0.1f, 2000.0f); }                     /* delay times */
static inline float rnd_rt(float s) { return rnd_logk(s, 0.25f, 10.0f); }                       /* reverb decay */
static inline float rnd_fxdiv(int idx) { return idx * 100.0f / 20.0f; }   /* one of the effects' 21 tempo divisions */
static inline float rnd_lfodiv(int idx) { return idx * 100.0f / 12.0f; }  /* one of the LFOs' 13 tempo divisions */
static inline float rnd_clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline int rnd_match(const char *pat, const char *key) {
    const char *star = strchr(pat, '*');
    if (!star) return !strcmp(pat, key);
    size_t pre = (size_t)(star - pat), suf = strlen(star + 1), kl = strlen(key);
    return kl >= pre + suf && !strncmp(key, pat, pre) && !strcmp(key + kl - suf, star + 1);
}
static inline int rnd_rule_of(int i) {
    for (int r = 0; r < RND_NRULES; r++)
        if (rnd_match(RND_RULES[r].pat, PARAMS[i].key)) return r;
    return 255;
}
/* param i's lock section, or RS_NEVER (never touched) */
static inline int rnd_section_of(int i) {
    int r = i >= 0 && i < NPARAMS ? rnd_rule_of(i) : 255;
    return r == 255 ? RS_NEVER : RND_RULES[r].sec;
}
static inline int rnd_key(const char *key) {   /* a param's index; NPARAMS (the scratch slot) if there is none */
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, key)) return i;
    return NPARAMS;
}
static inline int rnd_keyf(const char *fmt, int n) {   /* rnd_key of a formatted key ("mod%d_src", 3) */
    char k[48];
    snprintf(k, sizeof k, fmt, n);
    return rnd_key(k);
}
static inline int rnd_opt(int i, const char *name) {   /* an option's index by its name, -1 if none */
    if (i >= NPARAMS) return -1;
    for (int o = 0; o < PARAMS[i].nopts; o++)
        if (!strcmp(PARAMS[i].opts[o], name)) return o;
    return -1;
}
static inline float rnd_real(int i, float n) {   /* normalized to the param's units (an option's index) */
    const param_t *p = &PARAMS[i];
    n = rnd_clamp(n, 0.0f, 1.0f);
    if (p->nopts) return p->nopts > 1 ? roundf(n * (p->nopts - 1)) : 0.0f;
    return p->min + n * (p->max - p->min);
}
static inline int rnd_free_i(const rnd_ctx_t *c, int i) {   /* may this call change param i? */
    if (i < 0 || i >= NPARAMS || c->rule[i] == 255) return 0;
    int s = RND_RULES[c->rule[i]].sec;
    return s != RS_NEVER && !(c->locks >> s & 1u);
}
static inline int rnd_open(const rnd_ctx_t *c, int sec) { return !(c->locks >> sec & 1u); }
static inline float rnd_get(const rnd_ctx_t *c, const char *key) { return c->w[rnd_key(key)]; }
static inline int rnd_on(const rnd_ctx_t *c, const char *key) { return c->w[rnd_key(key)] > 0.5f; }
static inline void rnd_seti(rnd_ctx_t *c, int i, float v) { if (rnd_free_i(c, i)) c->w[i] = v; }
static inline void rnd_set(rnd_ctx_t *c, const char *key, float v) { rnd_seti(c, rnd_key(key), v); }
static inline void rnd_seto(rnd_ctx_t *c, const char *key, const char *opt) {   /* an option by name */
    int i = rnd_key(key), o = rnd_opt(i, opt);
    if (o >= 0) rnd_seti(c, i, (float)o);
}
static inline int rnd_is(const rnd_ctx_t *c, const char *key, const char *opt) {
    int i = rnd_key(key);
    return i < NPARAMS && (int)c->w[i] == rnd_opt(i, opt);
}
static inline void rnd_max(rnd_ctx_t *c, const char *key, float hi) { if (rnd_get(c, key) > hi) rnd_set(c, key, hi); }
static inline void rnd_min(rnd_ctx_t *c, const char *key, float lo) { if (rnd_get(c, key) < lo) rnd_set(c, key, lo); }

static inline void rnd_init(rnd_ctx_t *c, const float *cur, unsigned locks, uint32_t *seed) {
    c->locks = locks;
    c->seed = seed;
    c->arch = RA_POLY;
    for (int i = 0; i < NPARAMS; i++) {
        c->rule[i] = (unsigned char)rnd_rule_of(i);
        c->w[i] = rnd_real(i, cur[i]);
    }
    c->w[NPARAMS] = 0.0f;
}

/* a value from param i's rule (RK_UNI, RK_BELL, RK_OPT; the others keep theirs) */
static inline void rnd_sample(rnd_ctx_t *c, int i) {
    const rnd_rule_t *r = &RND_RULES[c->rule[i]];
    if (r->kind == RK_UNI) c->w[i] = rnd_chance(c, r->pz) ? r->z : rnd_range(c, r->lo, r->hi);
    else if (r->kind == RK_BELL) c->w[i] = rnd_chance(c, r->pz) ? r->z : rnd_bell(c, r->lo, r->hi);
    else if (r->kind == RK_OPT && PARAMS[i].nopts > 1) c->w[i] = (float)rnd_pickw(c, r->w, PARAMS[i].nopts);
}

/* ---- what is heard: each mixer channel's level, where it goes, and what reaches the output ---- */
typedef struct { float lvl[5]; int svf[5], lad[5]; float mix; int series; } rnd_route_t;
static inline rnd_route_t rnd_route(const rnd_ctx_t *c) {
    static const char *const SRC[5] = {"o1", "o2", "o3", "noise", "ring"};
    rnd_route_t r;
    char k[32];
    for (int s = 0; s < 5; s++) {
        snprintf(k, sizeof k, "%s_level", SRC[s]);
        r.lvl[s] = rnd_get(c, k);
        snprintf(k, sizeof k, "%s_svf", SRC[s]);
        r.svf[s] = rnd_on(c, k);
        snprintf(k, sizeof k, "%s_lad", SRC[s]);
        r.lad[s] = rnd_on(c, k);
    }
    r.mix = rnd_get(c, "flt_mix") * 0.01f;
    r.series = rnd_is(c, "flt_route", "Series");
    return r;
}
/* how much of mixer channel s reaches the output (0..1, before the filters' own shape) */
static inline float rnd_path(const rnd_route_t *r, int s) {
    float to_lad = r->lad[s] ? r->mix : 0.0f, to_svf = r->svf[s] ? (1.0f - r->mix) + (r->series ? r->mix : 0.0f) : 0.0f;
    return fminf(1.0f, to_lad + to_svf);
}
static inline float rnd_heard(const rnd_route_t *r) {   /* the channels' levels that reach the output, in % */
    static const float WT[5] = {1.0f, 1.0f, 1.0f, 0.6f, 0.7f};   /* noise and ring count a bit less */
    float h = 0.0f;
    for (int s = 0; s < 5; s++) h += r->lvl[s] * WT[s] * rnd_path(r, s);
    return h;
}
static inline int rnd_lad_used(const rnd_route_t *r) {   /* the ladder's output is heard */
    int in = 0;
    for (int s = 0; s < 5; s++) in |= r->lvl[s] > 0.0f && (r->lad[s] || (r->series && r->svf[s]));
    return in && r->mix > 0.05f;
}
static inline int rnd_svf_used(const rnd_route_t *r) {   /* the SVF is heard, directly or through the ladder */
    int in = 0;
    for (int s = 0; s < 5; s++) in |= r->lvl[s] > 0.0f && r->svf[s];
    return in && (r->mix < 0.95f || r->series);
}

/* ---- RANDOMISE, section by section (each only sets what its locks allow) ---- */
static inline void rnd_gen_osc(rnd_ctx_t *c) {
    int a = c->arch, o1;
    switch (a) {   /* footages: 0 = 32', 1 = 16', 2 = 8', 3 = 4', 4 = 2' */
    case RA_BASS: o1 = rnd_chance(c, 0.5f) ? 1 : 2; break;
    case RA_LEAD: o1 = rnd_chance(c, 0.7f) ? 2 : 3; break;
    case RA_PAD: o1 = rnd_chance(c, 0.8f) ? 2 : 1; break;
    case RA_PLUCK: o1 = rnd_chance(c, 0.6f) ? 2 : 3; break;
    case RA_TEXTURE: o1 = 1 + (int)(rnd_u(c) * 4) % 4; break;
    default: o1 = 2; break;
    }
    int o2 = o1 + rnd_pickw(c, "1 6 2 0.3", 4) - 1, o3 = o1 + rnd_pickw(c, "3 4 1.5", 3) - 1;
    rnd_set(c, "o1_oct", (float)o1);
    rnd_set(c, "o2_oct", rnd_clamp((float)o2, 0, 4));
    rnd_set(c, "o3_oct", rnd_clamp((float)o3, 0, 4));
    for (int n = 2; n <= 3; n++) {   /* a slight detune, an interval, or in tune (a beat frequency may still move it) */
        float r = rnd_u(c), f = 0.0f, sign = rnd_chance(c, 0.5f) ? 1.0f : -1.0f;
        if (r < 0.6f) f = sign * rnd_range(c, 0.03f, 0.15f);
        else if (r < 0.78f) {
            static const float IV[] = {7, 5, -5, 3, 4, -7};
            f = IV[rnd_pickw(c, a == RA_TEXTURE ? "2 1 1 1 1 1" : "4 1.5 1", 6)] + sign * rnd_range(c, 0.0f, 0.05f);
        }
        rnd_seti(c, rnd_keyf("o%d_freq", n), f);
    }
    for (int n = 1; n <= 3; n++) {   /* tri/saw, pulse, or a blend */
        float r = rnd_u(c);
        rnd_seti(c, rnd_keyf("o%d_mix", n), r < 0.5f ? 0.0f : r < 0.7f ? 100.0f : rnd_range(c, 0, 100));
    }
    float sync = a == RA_LEAD ? 0.25f : a == RA_TEXTURE ? 0.2f : 0.05f;
    if (rnd_chance(c, sync)) {   /* hard sync: osc 2 pitched up against osc 1, same footage */
        rnd_seto(c, "o2_sync", "On");
        rnd_set(c, "o2_oct", (float)o1);
        rnd_set(c, "o2_freq", rnd_range(c, 3.0f, 7.0f));
    } else {
        rnd_seto(c, "o2_sync", "Off");
    }
    rnd_seto(c, "o3_sync", rnd_chance(c, 0.04f) ? "On" : "Off");
    float fm = a == RA_PLUCK || a == RA_TEXTURE ? 0.3f : 0.12f;
    rnd_set(c, "fm_amt", rnd_chance(c, fm) ? rnd_range(c, 10, 45) : 0.0f);
}

static inline void rnd_gen_mix(rnd_ctx_t *c) {
    static const float P2[RA_COUNT] = {0.7f, 0.7f, 0.85f, 0.6f, 0.8f, 0.7f}, P3[RA_COUNT] = {0.5f, 0.4f, 0.6f, 0.4f, 0.45f, 0.5f};
    int a = c->arch;
    rnd_set(c, "o1_level", rnd_range(c, 70, 100));
    rnd_set(c, "o2_level", rnd_chance(c, P2[a]) ? rnd_range(c, 40, 90) : 0.0f);
    rnd_set(c, "o3_level", rnd_chance(c, P3[a]) ? rnd_range(c, 30, 80) : 0.0f);
    if (rnd_on(c, "o2_sync") && rnd_chance(c, 0.4f)) {   /* the synced oscillator alone */
        rnd_set(c, "o1_level", 0.0f);
        rnd_set(c, "o2_level", rnd_range(c, 85, 100));
    }
    float r = rnd_u(c);
    rnd_set(c, "noise_level", r < 0.78f ? 0.0f : r < 0.94f ? rnd_range(c, 5, 20)
                              : rnd_range(c, 20, a == RA_TEXTURE ? 60.0f : 40.0f));
    rnd_set(c, "ring_level", rnd_chance(c, a == RA_TEXTURE ? 0.3f : 0.1f) ? rnd_range(c, 30, 70) : 0.0f);
}

/* the filter topology (FILTERS) and the mixer buttons that feed it (MIXER): 0 ladder, 1 SVF, 2 SVF into the ladder,
 * 3 both in parallel, blended */
static inline void rnd_gen_filters(rnd_ctx_t *c) {
    static const char *const SRC[5] = {"o1", "o2", "o3", "noise", "ring"};
    int a = c->arch, topo;
    if (rnd_open(c, RS_FILTER)) {
        topo = rnd_pickw(c, "55 15 15 15", 4);
        rnd_seto(c, "flt_route", topo == 2 ? "Series" : "Parallel");
        rnd_set(c, "flt_mix", topo == 0 ? 100.0f : topo == 1 ? 0.0f : topo == 2 ? (rnd_chance(c, 0.8f) ? 100.0f : rnd_range(c, 70, 100))
                                                                     : rnd_range(c, 30, 70));
    } else {   /* the filters are locked: feed what they are set to */
        float m = rnd_get(c, "flt_mix");
        topo = rnd_is(c, "flt_route", "Series") ? 2 : m >= 75.0f ? 0 : m <= 25.0f ? 1 : 3;
    }
    int got_svf = 0, got_lad = 0;
    for (int s = 0; s < 5; s++) {
        int svf = topo == 1 || topo == 2, lad = topo == 0;
        if (topo == 2 && s == 2 && rnd_chance(c, 0.25f)) { svf = 0; lad = 1; }   /* osc 3 straight to the ladder */
        if (topo == 3) {
            int k = rnd_pickw(c, "2 2 1", 3);
            svf = k != 1;
            lad = k != 0;
        }
        char ks[32], kl[32];
        snprintf(ks, sizeof ks, "%s_svf", SRC[s]);
        snprintf(kl, sizeof kl, "%s_lad", SRC[s]);
        rnd_set(c, ks, (float)svf);
        rnd_set(c, kl, (float)lad);
        if (rnd_get(c, "o1_level") > 0.0f || s > 0) {
            got_svf |= svf && s < 3;
            got_lad |= lad && s < 3;
        }
    }
    if (topo == 3 && (!got_svf || !got_lad)) { rnd_set(c, "o1_svf", 1.0f); rnd_set(c, "o1_lad", 1.0f); }
    /* the ladder (cutoff, envelope amount by archetype; resonance mostly moderate) */
    static const float CUT[RA_COUNT][2] = {{18, 50}, {40, 75}, {35, 70}, {18, 50}, {28, 55}, {25, 90}};
    static const float ENV[RA_COUNT][2] = {{25, 70}, {5, 45}, {-10, 35}, {40, 80}, {25, 60}, {-40, 60}};
    float r = rnd_u(c);
    rnd_set(c, "flt_res", r < 0.55f ? rnd_range(c, 0, 35) : r < 0.9f ? rnd_range(c, 35, 65) : rnd_range(c, 65, 88));
    if (rnd_chance(c, a == RA_TEXTURE ? 0.25f : 0.07f)) {   /* high-pass: thin and bright */
        rnd_seto(c, "flt_mode", "HP");
        rnd_set(c, "flt_cut", rnd_bell(c, 20, 50));
        rnd_set(c, "lad_env", rnd_range(c, -10, 15));
    } else {
        rnd_seto(c, "flt_mode", "LP");
        rnd_set(c, "flt_cut", rnd_bell(c, CUT[a][0], CUT[a][1]));
        rnd_set(c, "lad_env", rnd_bell(c, ENV[a][0], ENV[a][1]));
    }
    /* the SVF: as the main filter it is like the ladder; before the ladder (series) it mostly thins or shapes */
    int mode = rnd_pickw(c, topo == 2 ? "4 2 3 0" : "4 3 1 1.5", 4);
    rnd_set(c, "svf_mode", (float)mode);
    rnd_set(c, "svf_res", rnd_bell(c, 0, mode == 1 ? 60.0f : 70.0f));
    if (mode == 2) { rnd_set(c, "svf_cut", rnd_bell(c, 20, 45)); rnd_set(c, "svf_env", rnd_range(c, -10, 10)); }
    else if (mode == 1) { rnd_set(c, "svf_cut", rnd_bell(c, 35, 70)); rnd_set(c, "svf_env", rnd_bell(c, -10, 40)); }
    else { rnd_set(c, "svf_cut", rnd_bell(c, CUT[a][0], CUT[a][1])); rnd_set(c, "svf_env", rnd_bell(c, ENV[a][0], ENV[a][1])); }
}

static inline void rnd_set_env(rnd_ctx_t *c, const char *e, const float *t) {   /* t: a, a, d, d (ms), s, s (%), r, r */
    char k[24];
    snprintf(k, sizeof k, "%s_a", e); rnd_set(c, k, rnd_ms(rnd_logr(c, t[0], t[1])));
    snprintf(k, sizeof k, "%s_d", e); rnd_set(c, k, rnd_ms(rnd_logr(c, t[2], t[3])));
    snprintf(k, sizeof k, "%s_s", e); rnd_set(c, k, rnd_range(c, t[4], t[5]));
    snprintf(k, sizeof k, "%s_r", e); rnd_set(c, k, rnd_ms(rnd_logr(c, t[6], t[7])));
}
static inline void rnd_gen_env(rnd_ctx_t *c) {
    /* attack lo, hi, decay lo, hi (ms), sustain lo, hi (%), release lo, hi (ms) */
    static const float AMP[RA_COUNT][8] = {
        {1, 8, 150, 800, 30, 100, 30, 250},      {1, 30, 200, 1000, 60, 100, 80, 500},
        {150, 1500, 500, 3000, 60, 100, 500, 2500}, {1, 5, 150, 1500, 0, 30, 120, 900},
        {8, 80, 300, 1200, 50, 90, 150, 700},    {1, 800, 100, 3000, 0, 100, 100, 3000}};
    static const float FENV[RA_COUNT][8] = {
        {1, 5, 100, 500, 0, 40, 50, 300},        {1, 20, 200, 900, 20, 70, 100, 500},
        {100, 2000, 800, 3000, 20, 70, 500, 2500}, {1, 3, 100, 800, 0, 20, 100, 600},
        {30, 150, 300, 1000, 30, 70, 150, 600},  {1, 1000, 100, 3000, 0, 80, 100, 2500}};
    static const float MENV[8] = {1, 200, 100, 2000, 0, 50, 100, 1000};
    int a = c->arch;
    rnd_set_env(c, "amp", AMP[a]);
    rnd_set_env(c, "fenv", FENV[a]);
    rnd_set_env(c, "menv", MENV);
    if (a == RA_PAD && rnd_chance(c, 0.1f)) rnd_set(c, "amp_a", rnd_ms(rnd_range(c, 1500, 2500)));   /* a rare slow swell */
    if (a == RA_BASS && rnd_chance(c, 0.3f)) rnd_set(c, "amp_hold", rnd_ms(rnd_range(c, 10, 60)));
    rnd_set(c, "amp_vel", rnd_bell(c, 40, 100));
    rnd_set(c, "fenv_vel", rnd_chance(c, 0.4f) ? rnd_range(c, 10, 50) : 0.0f);
    if (rnd_chance(c, a == RA_TEXTURE ? 0.15f : 0.03f)) rnd_seto(c, rnd_chance(c, 0.5f) ? "fenv_loop" : "menv_loop", "On");
}

static inline void rnd_gen_fx(rnd_ctx_t *c) {
    static const float PSFX[RA_COUNT] = {0.25f, 0.5f, 0.75f, 0.5f, 0.6f, 0.6f};
    static const float PDLY[RA_COUNT] = {0.1f, 0.45f, 0.25f, 0.45f, 0.25f, 0.4f};
    static const float PREV[RA_COUNT] = {0.15f, 0.5f, 0.85f, 0.7f, 0.6f, 0.7f};
    /* families: chorus, phaser, flanger, delay, bit crusher, resonator */
    static const char *const FAM[RA_COUNT] = {"3 2 1 1 1 0.3", "2 1 1 4 0 0", "6 2 1 1 0 0.2", "3 1 0.5 3 0.3 0.3",
                                              "6 1.5 1 1 0 0", "1 2 2 2 1 1"};
    int a = c->arch;
    rnd_seto(c, "mfx_on", "On");
    if (rnd_chance(c, PSFX[a])) {
        rnd_seto(c, "modfx_on", "On");
        switch (rnd_pickw(c, FAM[a], 6)) {
        case 0: {
            static const char *const T[] = {"Chorus", "Dual Chorus", "Ensemble", "Stereo Chorus"};
            rnd_seto(c, "modfx_type", T[rnd_pickw(c, "4 2 2 3", 4)]);
            rnd_set(c, "modfx_rate", rnd_fxrate(rnd_logr(c, 0.12f, 0.9f)));
            rnd_set(c, "modfx_rate_r", rnd_fxrate(rnd_logr(c, 0.12f, 0.9f)));
            rnd_set(c, "modfx_depth", rnd_range(c, 25, 60));
            rnd_set(c, "modfx_mix", rnd_range(c, 25, 50));
            break;
        }
        case 1: {
            static const char *const T[] = {"6-Stage Phaser", "12-Stage Phaser", "Time Phaser"};
            rnd_seto(c, "modfx_type", T[rnd_pickw(c, "3 2 1", 3)]);
            rnd_set(c, "modfx_rate", rnd_fxrate(rnd_logr(c, 0.08f, 0.8f)));
            rnd_set(c, "modfx_depth", rnd_range(c, 40, 80));
            rnd_set(c, "modfx_fb", rnd_range(c, 0, 60));
            rnd_set(c, "modfx_mix", rnd_range(c, 30, 50));
            break;
        }
        case 2:
            rnd_seto(c, "modfx_type", rnd_chance(c, 0.5f) ? "Flanger" : "Stereo Flanger");
            rnd_set(c, "modfx_rate", rnd_fxrate(rnd_logr(c, 0.05f, 0.4f)));
            rnd_set(c, "modfx_rate_r", rnd_fxrate(rnd_logr(c, 0.05f, 0.4f)));
            rnd_set(c, "modfx_depth", rnd_range(c, 40, 80));
            rnd_set(c, "modfx_fb", (rnd_chance(c, 0.5f) ? 1.0f : -1.0f) * rnd_range(c, 20, 60));
            rnd_set(c, "modfx_time", rnd_range(c, 20, 60));
            rnd_set(c, "modfx_mix", rnd_range(c, 30, 50));
            break;
        case 3: {
            static const char *const T[] = {"Delay", "BBD Delay", "Tape Delay", "Echo"};
            static const int DIV[] = {7, 9, 10, 11, 13};   /* 1/16, 1/8T, 1/8, 1/8., 1/4 */
            rnd_seto(c, "modfx_type", T[rnd_pickw(c, "1 2 2 1", 4)]);
            if (rnd_chance(c, 0.5f)) {
                rnd_seto(c, "modfx_sync", "On");
                rnd_set(c, "modfx_dtime", rnd_fxdiv(DIV[rnd_pickw(c, "1 1 2 2 1", 5)]));
            } else {
                rnd_set(c, "modfx_dtime", rnd_dms(rnd_logr(c, 120, 450)));
            }
            rnd_set(c, "modfx_fb", rnd_range(c, 20, 50));
            rnd_set(c, "modfx_mix", rnd_range(c, 15, 35));
            break;
        }
        case 4:
            rnd_seto(c, "modfx_type", "Bit Crusher");
            rnd_set(c, "modfx_bits", roundf(rnd_range(c, 6, 12)));
            rnd_set(c, "modfx_srate", rnd_logk(rnd_logr(c, 4000, 16000), 128.0f, 44100.0f));
            rnd_set(c, "modfx_mix", rnd_range(c, 30, 60));
            break;
        default:
            rnd_seto(c, "modfx_type", "Resonator");
            rnd_set(c, "modfx_mix", rnd_range(c, 20, 40));
            break;
        }
    } else {
        rnd_seto(c, "modfx_on", "Off");
    }
    int dly = rnd_chance(c, PDLY[a]);
    if (dly) {   /* the master delay on send 1, free or synced */
        static const int DIV[] = {9, 10, 11, 13};   /* 1/8T, 1/8, 1/8., 1/4 */
        rnd_set(c, "mfx_send1", rnd_range(c, 10, 30));
        if (rnd_chance(c, 0.5f)) {
            float t = rnd_fxdiv(DIV[rnd_pickw(c, "1 2 2 1", 4)]);
            rnd_seto(c, "dly_sync", "On");
            rnd_set(c, "dly_time", t);
            rnd_set(c, "dly_time_r", t);
        } else {
            float t = rnd_logr(c, 180, 550);
            rnd_seto(c, "dly_sync", "Off");
            rnd_set(c, "dly_time", rnd_dms(t));
            rnd_set(c, "dly_time_r", rnd_dms(t * rnd_range(c, 0.75f, 1.33f)));
        }
    }
    if (rnd_chance(c, PREV[a])) {   /* the reverb on send 2 */
        rnd_set(c, "mfx_send2", a == RA_BASS ? rnd_range(c, 5, 15) : rnd_range(c, 12, 40));
        rnd_set(c, "rev_decay", rnd_rt(rnd_logr(c, 1.0f, a == RA_PAD ? 5.0f : 3.5f)));
        if (dly && rnd_chance(c, 0.25f)) rnd_set(c, "mfx_series", rnd_range(c, 15, 40));
    }
}

static inline void rnd_gen_perf(rnd_ctx_t *c) {
    int a = c->arch, solo = a == RA_BASS || a == RA_LEAD;
    if (solo && rnd_chance(c, 0.55f)) rnd_set(c, "voices", 0.0f);   /* mono */
    else rnd_set(c, "voices", (float)rnd_pickw(c, "0 0 0 1 0.5 2 1 4", 8));
    if (rnd_chance(c, solo ? 0.3f : 0.04f)) {
        rnd_seto(c, "glide", "On");
        rnd_set(c, "glide_time", rnd_ms(rnd_logr(c, 30, 220)));
    } else {
        rnd_seto(c, "glide", "Off");
    }
    static const float UNI[RA_COUNT] = {0.1f, 0.15f, 0.08f, 0.03f, 0.03f, 0.05f};
    if (rnd_chance(c, UNI[a])) {
        rnd_seto(c, "unison", "On");
        rnd_set(c, "uni_count", (float)rnd_pickw(c, "3 3 2 1", 4));   /* 2 .. 5 */
    } else {
        rnd_seto(c, "unison", "Off");
    }
    if (rnd_chance(c, 0.35f)) {   /* analog character */
        static const char *const K[] = {"an_cut", "an_env", "an_lvl", "an_tune", "an_res", "an_ladder"};
        static const float HI[] = {50, 60, 40, 50, 40, 60};
        for (int k = 0; k < 6; k++) rnd_set(c, K[k], rnd_chance(c, 0.3f) ? 0.0f : rnd_range(c, 10, HI[k]));
    }
    if (rnd_chance(c, 0.35f)) rnd_set(c, "drift", rnd_range(c, 10, 60));
    if (rnd_get(c, "voices") > 0.5f && rnd_chance(c, 0.35f)) rnd_set(c, "spread", rnd_range(c, 20, 80));
}

/* is a matrix pairing's destination heard / meaningful now? */
static inline int rnd_mod_ok(const rnd_ctx_t *c, int cond) {
    rnd_route_t r = rnd_route(c);
    switch (cond) {
    case MC_LAD: return rnd_lad_used(&r);
    case MC_SVF: return rnd_svf_used(&r);
    case MC_PW1: return r.lvl[0] > 0.0f && rnd_get(c, "o1_mix") > 20.0f;
    case MC_ANGLE1: return r.lvl[0] > 0.0f && rnd_get(c, "o1_mix") < 80.0f;
    case MC_O2: return r.lvl[1] > 0.0f;
    case MC_FM: return rnd_get(c, "fm_amt") > 0.0f;
    case MC_SYNC2: return rnd_on(c, "o2_sync") && r.lvl[1] > 0.0f;
    case MC_SFX: return rnd_on(c, "modfx_on");
    case MC_POLY: return rnd_get(c, "voices") > 0.5f;
    default: return 1;
    }
}
/* matrix row k (1..16) from pairing m */
static inline void rnd_put_mod(rnd_ctx_t *c, int k, const rnd_mod_t *m, float scale) {
    int src = rnd_keyf("mod%d_src", k), dst = rnd_keyf("mod%d_dst", k);
    float amt = rnd_range(c, m->lo, m->hi) * scale * (m->bip && rnd_chance(c, 0.5f) ? -1.0f : 1.0f);
    rnd_seti(c, src, (float)rnd_opt(src, m->src));
    rnd_seti(c, dst, (float)rnd_opt(dst, m->dst));
    rnd_seti(c, rnd_keyf("mod%d_amt", k), amt);
    rnd_seti(c, rnd_keyf("mod%d_via", k), 0.0f);
    rnd_seti(c, rnd_keyf("mod%d_via_amt", k), 100.0f);
    int xf = rnd_chance(c, 0.1f);
    rnd_seti(c, rnd_keyf("mod%d_xf", k), xf ? (float)(3 + (int)(rnd_u(c) * 2) % 2) : 0.0f);   /* squared / cubed */
    rnd_seti(c, rnd_keyf("mod%d_xf_amt", k), xf ? rnd_range(c, 30, 80) : 0.0f);
}
static inline int rnd_row_active(const rnd_ctx_t *c, int k) {
    return c->w[rnd_keyf("mod%d_src", k)] > 0.5f && c->w[rnd_keyf("mod%d_dst", k)] > 0.5f;
}
/* a pairing that fits and whose destination no active row already moves; -1 if none */
static inline int rnd_pick_mod(rnd_ctx_t *c) {
    float wt[RND_NMODS], sum = 0.0f;
    int d1 = rnd_key("mod1_dst");
    for (int m = 0; m < RND_NMODS; m++) {
        int ok = rnd_mod_ok(c, RND_MODS[m].cond), dst = rnd_opt(d1, RND_MODS[m].dst);
        for (int k = 1; k <= 16 && ok; k++)
            if (rnd_row_active(c, k) && (int)c->w[rnd_keyf("mod%d_dst", k)] == dst) ok = 0;
        wt[m] = ok && dst > 0 ? RND_MODS[m].wt : 0.0f;
        sum += wt[m];
    }
    if (sum <= 0.0f) return -1;
    float r = rnd_u(c) * sum;
    for (int m = 0; m < RND_NMODS; m++)
        if (wt[m] > 0.0f && (r -= wt[m]) < 0.0f) return m;
    return -1;
}

static inline void rnd_gen_mod(rnd_ctx_t *c) {
    int a = c->arch;
    /* LFO rates: mostly 0.2 .. 8 Hz; now and then tempo-synced (1/2 .. 1/16) */
    for (int n = 1; n <= 4; n++) {
        if (rnd_chance(c, a == RA_PAD ? 0.05f : 0.12f)) {
            rnd_seti(c, rnd_keyf("lfo%d_sync", n), 1.0f);
            rnd_seti(c, rnd_keyf("lfo%d_rate", n), rnd_lfodiv(5 + (int)(rnd_u(c) * 4) % 4));
        } else {
            rnd_seti(c, rnd_keyf("lfo%d_sync", n), 0.0f);
            rnd_seti(c, rnd_keyf("lfo%d_rate", n), rnd_lfo(rnd_logr(c, 0.2f, 8.0f)));
        }
    }
    /* LFO 1 is the vibrato (slot 1: LFO 1 x Mod Wheel to pitch, and PITCH LFO1 AMT) */
    if (rnd_chance(c, 0.75f)) {
        rnd_set(c, "lfo1_sync", 0.0f);
        rnd_set(c, "lfo1_rate", rnd_lfo(rnd_range(c, 4.5f, 6.5f)));
        rnd_seto(c, "lfo1_wave", rnd_chance(c, 0.6f) ? "Sine" : "Triangle");
    }
    rnd_set(c, "mod1_amt", rnd_range(c, 2, 5));
    if (rnd_chance(c, a == RA_LEAD || a == RA_PAD ? 0.2f : 0.08f)) {
        rnd_set(c, "lfo1_sync", 0.0f);
        rnd_set(c, "lfo1_rate", rnd_lfo(rnd_range(c, 4.5f, 6.5f)));
        rnd_set(c, "pitch_lfo", rnd_range(c, 0.8f, 2.5f));
        if (rnd_chance(c, 0.5f)) rnd_set(c, "lfo1_delay", rnd_ms(rnd_range(c, 150, 600)));
        if (rnd_chance(c, 0.5f)) rnd_set(c, "lfo1_fade_in", rnd_ms(rnd_range(c, 300, 1200)));
    }
    /* a pitch blip from the mod envelope (plucks, basses, textures) */
    if (rnd_chance(c, a == RA_BASS || a == RA_PLUCK || a == RA_TEXTURE ? 0.18f : 0.05f)) {
        rnd_set(c, "pitch_env", (rnd_chance(c, 0.8f) ? 1.0f : -1.0f) * rnd_range(c, 2, 10));
        rnd_set(c, "menv_a", 0.0f);
        rnd_set(c, "menv_d", rnd_ms(rnd_logr(c, 15, 120)));
        rnd_set(c, "menv_s", 0.0f);
    }
    /* LFO 2 drives the filters' LFO amounts, LFO 3 the wave shapes: slow and smooth when they do */
    if (rnd_get(c, "lad_lfo") != 0.0f || rnd_get(c, "svf_lfo") != 0.0f) {
        if (rnd_chance(c, 0.3f)) { rnd_set(c, "lfo2_sync", 1.0f); rnd_set(c, "lfo2_rate", rnd_lfodiv(5 + (int)(rnd_u(c) * 4) % 4)); }
        else { rnd_set(c, "lfo2_sync", 0.0f); rnd_set(c, "lfo2_rate", rnd_lfo(rnd_logr(c, 0.1f, 2.0f))); }
        rnd_seto(c, "lfo2_wave", rnd_chance(c, 0.6f) ? "Sine" : "Triangle");
    }
    if (rnd_get(c, "angle_lfo3") > 0.0f || rnd_get(c, "pw_lfo3") > 0.0f) {
        rnd_set(c, "lfo3_sync", 0.0f);
        rnd_set(c, "lfo3_rate", rnd_lfo(rnd_logr(c, 0.1f, 1.5f)));
        rnd_seto(c, "lfo3_wave", rnd_chance(c, 0.6f) ? "Sine" : "Triangle");
    }
    /* 0-4 more matrix rows from the tried pairings (a synced oscillator gets its sweep first, usually) */
    int n = rnd_pickw(c, "30 35 20 10 5", 5), k = 2;
    if (rnd_mod_ok(c, MC_SYNC2) && rnd_chance(c, 0.7f)) {
        for (int m = 0; m < RND_NMODS; m++)
            if (RND_MODS[m].cond == MC_SYNC2) { rnd_put_mod(c, k++, &RND_MODS[m], 1.0f); break; }
        rnd_set(c, "menv_d", rnd_ms(rnd_logr(c, 200, 1500)));
    }
    for (; k <= 1 + n; k++) {
        int m = rnd_pick_mod(c);
        if (m < 0) break;
        rnd_put_mod(c, k, &RND_MODS[m], 1.0f);
    }
}

/* ---- the safety pass: heard, not closed, not harsh, in tune (only what the locks allow) ---- */
static inline void rnd_safety(rnd_ctx_t *c) {
    for (int i = 0; i < NPARAMS; i++) {   /* in range, options whole */
        const param_t *p = &PARAMS[i];
        if (!rnd_free_i(c, i)) continue;
        if (p->nopts) c->w[i] = rnd_clamp(roundf(c->w[i]), 0.0f, (float)(p->nopts > 1 ? p->nopts - 1 : 0));
        else c->w[i] = rnd_clamp(c->w[i], p->min, p->max);
    }
    /* the amp envelope: a note is heard and ends without a click */
    rnd_max(c, "amp_a", rnd_ms(3000));
    rnd_max(c, "amp_delay", rnd_ms(200));
    rnd_min(c, "amp_r", rnd_ms(20));
    rnd_min(c, "amp_vel", 0.0f);
    rnd_max(c, "amp_scale", 70.0f);
    rnd_min(c, "amp_scale", 30.0f);
    if (rnd_get(c, "amp_s") < 10.0f && rnd_get(c, "amp_d") < rnd_ms(150)) rnd_set(c, "amp_d", rnd_ms(150));
    if (rnd_on(c, "amp_loop") && rnd_get(c, "amp_s") < 20.0f) rnd_set(c, "amp_s", 20.0f);
    /* oscillators in tune: an interval's detune within 0.25 st, beat frequencies within 3 Hz, gentle FM */
    for (int n = 1; n <= 3; n++) {
        int f = rnd_keyf("o%d_freq", n), b = rnd_keyf("o%d_beat", n);
        float v = c->w[f], whole = roundf(v);
        if (fabsf(v - whole) > 0.25f) rnd_seti(c, f, whole + (v > whole ? 0.25f : -0.25f));
        rnd_seti(c, b, rnd_clamp(c->w[b], -3.0f, 3.0f));
    }
    rnd_max(c, "fm_amt", 70.0f);
    /* levels: not too many sources at full, noise and ring kept below the oscillators */
    rnd_max(c, "noise_level", 70.0f);
    rnd_max(c, "ring_level", 80.0f);
    if (rnd_open(c, RS_MIX)) {   /* a rough loudness: each channel's level x its wave's RMS x where it goes */
        rnd_route_t r = rnd_route(c);
        float est = 0.0f, drive = 1.0f + 0.15f * rnd_get(c, "flt_drive");
        for (int s = 0; s < 5; s++) {
            char k[32];
            snprintf(k, sizeof k, "o%d_mix", s + 1);
            float wrms = s < 3 ? 0.58f + 0.42f * rnd_get(c, k) * 0.01f : s == 3 ? 0.6f : 0.6f;   /* saw .58, pulse 1 */
            float lad = r.lad[s] || (r.series && r.svf[s]) ? r.mix * fminf(sqrtf(drive), 2.0f) : 0.0f;
            float svf = r.svf[s] ? 1.0f - r.mix : 0.0f;
            est += r.lvl[s] * 0.01f * wrms * fminf(2.0f, lad + svf);
        }
        float cap = rnd_get(c, "voices") > 0.5f ? 0.85f : 1.3f;   /* a 4-note chord adds up about twice one note */
        if (est > cap) {
            static const char *const L[5] = {"o1_level", "o2_level", "o3_level", "noise_level", "ring_level"};
            for (int s = 0; s < 5; s++) rnd_set(c, L[s], roundf(rnd_get(c, L[s]) * cap / est));
        }
    }
    /* heard at all: something reaches the filter whose output is heard */
    rnd_route_t r = rnd_route(c);
    if (rnd_heard(&r) < 40.0f) {
        if (rnd_open(c, RS_MIX)) {   /* osc 1 up and into the side the filter mix listens to */
            rnd_min(c, "o1_level", 75.0f);
            if (r.mix >= 0.5f || r.series) rnd_set(c, "o1_lad", 1.0f);
            if (r.mix < 0.5f || r.series) rnd_set(c, "o1_svf", 1.0f);
        }
        r = rnd_route(c);
        if (rnd_heard(&r) < 40.0f && rnd_open(c, RS_FILTER)) {   /* the filters follow where the sources go */
            float to_lad = 0.0f, to_svf = 0.0f;
            for (int s = 0; s < 5; s++) { to_lad += r.lvl[s] * r.lad[s]; to_svf += r.lvl[s] * r.svf[s]; }
            rnd_seto(c, "flt_route", "Parallel");
            rnd_set(c, "flt_mix", to_lad >= to_svf ? 100.0f : 0.0f);
        }
        r = rnd_route(c);
    }
    /* the filters: not closed (cutoff at the envelope's peak and at sustain), not harsh (resonance with drive) */
    int lad = rnd_lad_used(&r), svf = rnd_svf_used(&r);
    float sus_amp = rnd_get(c, "amp_s"), oct_hi = 0.0f;   /* the highest heard footage above 8', in octaves */
    for (int s = 0; s < 3; s++) {
        char k[32];
        snprintf(k, sizeof k, "o%d_oct", s + 1);
        if (r.lvl[s] > 0.0f && rnd_path(&r, s) > 0.0f) oct_hi = fmaxf(oct_hi, rnd_get(c, k) - 2.0f);
    }
    static const char *const FK[2][4] = {{"flt_cut", "lad_env", "flt_res", "flt_mode"}, {"svf_cut", "svf_env", "svf_res", "svf_mode"}};
    for (int f = 0; f < 2; f++) {
        if (!(f ? svf : lad)) continue;
        float cut = rnd_get(c, FK[f][0]), env = rnd_get(c, FK[f][1]), fs = rnd_get(c, "fenv_s");
        int mode = (int)rnd_get(c, FK[f][3]);   /* ladder: 0 LP, 1 HP; SVF: 0 LP, 1 BP, 2 HP, 3 notch */
        int hp = f ? mode == 2 : mode == 1, bp = f && mode == 1;
        if (hp) {   /* not so high that only fizz is left */
            rnd_max(c, FK[f][0], 55.0f);
            rnd_max(c, FK[f][1], 20.0f);
        } else if (bp) {
            if (cut < 30.0f) rnd_set(c, FK[f][0], 30.0f);
            if (cut > 80.0f) rnd_set(c, FK[f][0], 80.0f);
        } else if (f == 0 || mode == 0) {   /* low-pass: 10 knob units an octave, the envelope at +-100% +-5 octaves */
            float peak = cut + fmaxf(0.0f, env) * 0.5f, low = cut + fminf(0.0f, env) * 0.5f;
            float sus = cut + env * 0.5f * fs * 0.01f, up = 10.0f * oct_hi;   /* higher footages need it higher */
            float need = fmaxf(46.0f + up - peak, sus_amp > 30.0f ? fmaxf(32.0f + up - sus, 26.0f + up - low) : 0.0f);
            if (need > 0.0f) rnd_set(c, FK[f][0], fminf(100.0f, cut + need));
            const char *kbd = f ? "svf_kbd" : "lad_kbd";   /* the top of the keyboard not muffled away */
            if (rnd_get(c, FK[f][0]) + fmaxf(0.0f, env) * 0.5f < 65.0f) rnd_min(c, kbd, 40.0f);
        }
        rnd_max(c, FK[f][2], f ? 88.0f : 92.0f);
    }
    float res = rnd_get(c, "flt_res");
    if (res > 85.0f) rnd_max(c, "flt_drive", 15.0f);
    else if (res > 70.0f) rnd_max(c, "flt_drive", 30.0f);
    rnd_max(c, "flt_drive", fmaxf(0.0f, 120.0f - res));
    rnd_max(c, "lad_fm", 50.0f);
    rnd_max(c, "svf_fm", 50.0f);
    rnd_min(c, "lad_kbd", -20.0f);
    rnd_min(c, "svf_kbd", -20.0f);
    /* effects: no runaway feedback, sends and mixes that leave the dry sound in front */
    static const char *const FB[] = {"modfx_fb", "modfx_fb_r"};
    for (int k = 0; k < 2; k++) { rnd_max(c, FB[k], 70.0f); rnd_min(c, FB[k], -70.0f); }
    rnd_max(c, "modfx_dfb", 70.0f);
    rnd_max(c, "dly_fb", 65.0f);
    rnd_max(c, "modfx_mix", rnd_is(c, "modfx_type", "Bit Crusher") ? 80.0f : 70.0f);
    rnd_max(c, "modfx_gain", 60.0f);
    rnd_min(c, "modfx_gain", 40.0f);
    rnd_min(c, "modfx_bits", 4.0f);
    rnd_max(c, "mfx_send1", 45.0f);
    rnd_max(c, "mfx_send2", 55.0f);
    rnd_max(c, "mfx_series", 60.0f);
    /* the matrix: pitch only by a little (a sync sweep on a synced oscillator may go further), levels by up to 40% */
    int d1 = rnd_key("mod1_dst"), s1 = rnd_key("mod1_src");
    int env_src[3] = {rnd_opt(s1, "Filter Env"), rnd_opt(s1, "Amp Env"), rnd_opt(s1, "Mod Env")};
    for (int k = 1; k <= 16; k++) {
        int ai = rnd_keyf("mod%d_amt", k);
        if (!rnd_free_i(c, ai)) continue;
        int src = (int)c->w[rnd_keyf("mod%d_src", k)], dst = (int)c->w[rnd_keyf("mod%d_dst", k)];
        const char *dn = dst >= 0 && d1 < NPARAMS && dst < PARAMS[d1].nopts ? PARAMS[d1].opts[dst] : "";
        float cap = 100.0f;
        if (!strcmp(dn, "Pitch") || !strcmp(dn, "Osc 1 Pitch") || !strcmp(dn, "Osc 2 Pitch") || !strcmp(dn, "Osc 3 Pitch")) {
            int envs = src == env_src[0] || src == env_src[1] || src == env_src[2];
            int synced = (!strcmp(dn, "Osc 2 Pitch") && rnd_on(c, "o2_sync")) || (!strcmp(dn, "Osc 3 Pitch") && rnd_on(c, "o3_sync"));
            cap = envs && synced ? 50.0f : 6.0f;
        } else if (!strcmp(dn, "Pitch LFO1 Amt") || !strcmp(dn, "Pitch EG Amt")) {
            cap = 10.0f;
        } else if (!strcmp(dn, "Amp Level")) {
            cap = 40.0f;
        }
        c->w[ai] = rnd_clamp(c->w[ai], -cap, cap);
    }
    rnd_max(c, "pitch_lfo", 5.0f);
    rnd_max(c, "pitch_env", 20.0f);
    rnd_min(c, "pitch_env", -20.0f);
    /* performance */
    rnd_max(c, "glide_time", rnd_ms(500));
    rnd_max(c, "uni_detune", 50.0f);
    rnd_max(c, "pitch_var", 12.0f);
    rnd_max(c, "drift", 80.0f);
    static const char *const AN[] = {"an_cut", "an_env", "an_lvl", "an_tune", "an_res", "an_ladder"};
    for (int k = 0; k < 6; k++) rnd_max(c, AN[k], 70.0f);
}

/* the working values back to normalized: only params this call may change, and only where they differ */
static inline int rnd_finish(const rnd_ctx_t *c, const float *cur, float *out) {
    int changed = 0;
    for (int i = 0; i < NPARAMS; i++) {
        const param_t *p = &PARAMS[i];
        out[i] = cur[i];
        if (!rnd_free_i(c, i)) continue;
        if (p->nopts > 1) {
            int o = (int)rnd_clamp(roundf(c->w[i]), 0.0f, (float)(p->nopts - 1));
            if (o != (int)lroundf(rnd_clamp(cur[i], 0.0f, 1.0f) * (p->nopts - 1))) { out[i] = (float)o / (p->nopts - 1); changed++; }
        } else if (!p->nopts && p->max > p->min) {
            float v = rnd_clamp(p->int_display ? roundf(c->w[i]) : c->w[i], p->min, p->max);
            if (fabsf(v - rnd_real(i, cur[i])) > 1e-4f * (p->max - p->min)) { out[i] = (v - p->min) / (p->max - p->min); changed++; }
        }
    }
    return changed;
}

/* RANDOMISE: a new sound in `out` (normalized, NPARAMS values) from the current one `cur`, keeping locked sections and the
 * never-touched params. Returns its archetype (RND_ARCH). */
static inline int rnd_generate(const float *cur, float *out, unsigned locks, uint32_t *seed) {
    rnd_ctx_t c;
    rnd_init(&c, cur, locks, seed);
    for (int i = 0; i < NPARAMS; i++)   /* every free param from its default first */
        if (rnd_free_i(&c, i)) c.w[i] = rnd_real(i, PARAMS[i].def);
    c.arch = rnd_pickw(&c, "18 16 20 18 16 12", RA_COUNT);
    for (int i = 0; i < NPARAMS; i++)
        if (rnd_free_i(&c, i)) rnd_sample(&c, i);
    rnd_gen_osc(&c);
    rnd_gen_mix(&c);
    rnd_gen_filters(&c);
    rnd_gen_env(&c);
    rnd_gen_fx(&c);
    rnd_gen_perf(&c);
    rnd_gen_mod(&c);
    rnd_safety(&c);
    rnd_finish(&c, cur, out);
    return c.arch;
}

/* MUTATE helpers: a param that cannot be heard now is left alone (a silent oscillator's shape, an effect that is off) */
static inline int rnd_relevant(const rnd_ctx_t *c, int i) {
    const char *k = PARAMS[i].key;
    if (!strncmp(k, "mod", 3) && k[3] >= '0' && k[3] <= '9') return 0;   /* the matrix: rnd_mutate_matrix */
    if (k[0] == 'o' && (k[1] == '2' || k[1] == '3') && k[2] == '_' && strcmp(k + 3, "level")) {
        char lv[16];
        snprintf(lv, sizeof lv, "o%c_level", k[1]);
        if (rnd_get(c, lv) <= 0.0f && rnd_get(c, "ring_level") <= 0.0f && rnd_get(c, "fm_amt") <= 0.0f) return 0;
    }
    if (!strncmp(k, "modfx_", 6) && strcmp(k, "modfx_on")) return rnd_on(c, "modfx_on");
    if (!strncmp(k, "dly_", 4)) return rnd_get(c, "mfx_send1") > 0.0f;
    if (!strncmp(k, "rev_", 4)) return rnd_get(c, "mfx_send2") > 0.0f || (rnd_get(c, "mfx_send1") > 0.0f && rnd_get(c, "mfx_series") > 0.0f);
    if (!strncmp(k, "noise_", 6) && strcmp(k, "noise_level")) return rnd_get(c, "noise_level") > 0.0f;
    if (!strncmp(k, "glide_", 6) || !strcmp(k, "glissando")) return rnd_on(c, "glide");
    if (!strncmp(k, "uni_", 4)) return rnd_on(c, "unison");
    return 1;
}
static inline void rnd_mutate_matrix(rnd_ctx_t *c, float amount) {
    if (!rnd_open(c, RS_MOD)) return;
    for (int k = 1; k <= 16; k++) {
        int ai = rnd_keyf("mod%d_amt", k);
        if (rnd_row_active(c, k) && rnd_chance(c, fmaxf(0.05f, amount)))
            c->w[ai] += rnd_gauss(c) * amount * (k == 1 ? 1.0f : 12.0f);   /* slot 1 is the vibrato: tiny steps */
    }
    if (rnd_chance(c, amount * 0.12f))   /* a new row */
        for (int k = 2; k <= 16; k++)
            if (!rnd_row_active(c, k)) {
                int m = rnd_pick_mod(c);
                if (m >= 0) rnd_put_mod(c, k, &RND_MODS[m], fminf(1.0f, 0.3f + amount));
                break;
            }
    if (rnd_chance(c, amount * 0.06f)) {   /* a row goes */
        int k = 2 + (int)(rnd_u(c) * 15) % 15;
        if (rnd_row_active(c, k)) {
            rnd_seti(c, rnd_keyf("mod%d_src", k), 0.0f);
            rnd_seti(c, rnd_keyf("mod%d_dst", k), 0.0f);
            rnd_seti(c, rnd_keyf("mod%d_amt", k), 0.0f);
        }
    }
}

/* MUTATE: a variation of `cur` in `out`; amount 0..1 (0.2: a nudge, 1: a lot). Returns how many params changed. */
static inline int rnd_mutate(const float *cur, float *out, float amount, unsigned locks, uint32_t *seed) {
    rnd_ctx_t c;
    rnd_init(&c, cur, locks, seed);
    amount = rnd_clamp(amount, 0.0f, 1.0f);
    float pm = fmaxf(0.05f, amount);
    for (int i = 0; i < NPARAMS; i++) {
        if (!rnd_free_i(&c, i)) continue;
        const rnd_rule_t *r = &RND_RULES[c.rule[i]];
        const param_t *p = &PARAMS[i];
        if (r->mut <= 0.0f || !rnd_relevant(&c, i)) continue;
        if (p->nopts > 1) {
            if (rnd_chance(&c, amount * 0.25f)) c.w[i] = (float)rnd_pickw(&c, r->w, p->nopts);
            continue;
        }
        float span = p->max - p->min, v = c.w[i], lo = r->lo, hi = r->hi;
        if (span <= 0.0f) continue;
        if (r->pz > 0.0f && fabsf(v - r->z) < 1e-6f * span) {   /* off (or centred): mostly stays so */
            if (!rnd_chance(&c, amount * 0.15f)) continue;
        } else if (!rnd_chance(&c, pm)) {
            continue;
        }
        if (lo == hi) { lo = p->min; hi = p->max; }
        lo = fminf(lo, v);
        hi = fmaxf(hi, v);
        c.w[i] = rnd_clamp(v + rnd_gauss(&c) * amount * r->mut * span, fmaxf(lo, p->min), fminf(hi, p->max));
    }
    rnd_mutate_matrix(&c, amount);
    rnd_safety(&c);
    return rnd_finish(&c, cur, out);
}

/* a one-line summary of a sound (normalized values): oscillators, filter, envelope, effects, voicing */
static inline int rnd_describe(const float *norm, char *buf, int n) {
    rnd_ctx_t c;
    uint32_t seed = 1;
    rnd_init(&c, norm, 0, &seed);
    rnd_route_t r = rnd_route(&c);
    int used = 0;
#define RND_ADD(...) do { if (used < n) used += snprintf(buf + used, (size_t)(n - used), __VA_ARGS__); } while (0)
    int oscs = 0;
    for (int o = 0; o < 3; o++) {
        if (r.lvl[o] <= 0.0f || rnd_path(&r, o) <= 0.0f) continue;
        char k[32];
        snprintf(k, sizeof k, "o%d_mix", o + 1);
        float mx = rnd_get(&c, k);
        snprintf(k, sizeof k, "o%d_wave", o + 1);
        const char *wave = mx > 60.0f ? "pulse" : rnd_get(&c, k) > 0.5f ? (mx > 20.0f ? "saw/pulse" : "saw")
                                                                      : (mx > 20.0f ? "tri/pulse" : "tri");
        snprintf(k, sizeof k, "o%d_oct", o + 1);
        int oct = (int)rnd_get(&c, k);
        static const char *const FOOT[5] = {"32'", "16'", "8'", "4'", "2'"};
        RND_ADD("%s%s %s", oscs ? " + " : "", wave, FOOT[oct < 0 ? 0 : oct > 4 ? 4 : oct]);
        oscs++;
    }
    if (!oscs) RND_ADD("no oscillator");
    if (rnd_on(&c, "o2_sync")) RND_ADD(", sync");
    if (rnd_get(&c, "fm_amt") > 0.0f) RND_ADD(", FM %.0f%%", rnd_get(&c, "fm_amt"));
    if (r.lvl[4] > 0.0f && rnd_path(&r, 4) > 0.0f) RND_ADD(", ring");
    if (r.lvl[3] > 0.0f && rnd_path(&r, 3) > 0.0f) RND_ADD(", noise");
    int lad = rnd_lad_used(&r), svf = rnd_svf_used(&r);
    static const char *const SVF_MODE[4] = {"LP", "BP", "HP", "notch"};
    float lc = rnd_get(&c, "flt_cut"), sc = rnd_get(&c, "svf_cut");
    float lhz = 20.0f * powf(1000.0f, lc * 0.01f), shz = 20.0f * powf(1000.0f, sc * 0.01f);
    int sm = (int)rnd_get(&c, "svf_mode");
    if (svf && r.series && lad) RND_ADD("; SVF %s > ", SVF_MODE[sm & 3]);
    else if (svf && lad) RND_ADD("; SVF %s + ", SVF_MODE[sm & 3]);
    else RND_ADD("; ");
    if (lad)
        RND_ADD("ladder %s %.0f Hz res %.0f%% env %+.0f%%", rnd_get(&c, "flt_mode") > 0.5f ? "HP" : "LP", lhz,
                rnd_get(&c, "flt_res"), rnd_get(&c, "lad_env"));
    else if (svf)
        RND_ADD("SVF %s %.0f Hz res %.0f%% env %+.0f%%", SVF_MODE[sm & 3], shz, rnd_get(&c, "svf_res"), rnd_get(&c, "svf_env"));
    else
        RND_ADD("no filter heard");
    float att = powf(10.0f, rnd_get(&c, "amp_a") * 0.04f), dec = powf(10.0f, rnd_get(&c, "amp_d") * 0.04f);
    float sus = rnd_get(&c, "amp_s");
    RND_ADD("; %s", att > 120.0f ? "slow attack" : sus < 20.0f ? (dec < 500.0f ? "short pluck" : "long decay")
                                                               : "held");
    if (rnd_on(&c, "modfx_on")) {
        int t = rnd_key("modfx_type"), o = (int)c.w[t];
        RND_ADD("; %s", t < NPARAMS && o >= 0 && o < PARAMS[t].nopts ? PARAMS[t].opts[o] : "fx");
    }
    if (rnd_on(&c, "mfx_on") && rnd_get(&c, "mfx_send1") > 0.0f) RND_ADD("; delay");
    if (rnd_on(&c, "mfx_on") && rnd_get(&c, "mfx_send2") > 0.0f) RND_ADD("; reverb");
    int rows = 0;
    for (int k = 2; k <= 16; k++) rows += rnd_row_active(&c, k);
    if (rows) RND_ADD("; %d mod row%s", rows, rows > 1 ? "s" : "");
    if (rnd_get(&c, "voices") < 0.5f) RND_ADD("; mono");
    if (rnd_on(&c, "glide")) RND_ADD(" glide");
    if (rnd_on(&c, "unison")) RND_ADD("; unison x%d", (int)rnd_get(&c, "uni_count") + 2);
#undef RND_ADD
    return used < n ? used : n - 1;
}
