/* ONE-ish effects: shared pieces. The settings the effects read (fx_params_t, in their own units), the effect type
 * and control lists (their order is the option order in params.json), knob-to-unit conversions shared by the engine
 * and the display text, the tempo divisions, delay lines, interpolation, LFO shapes and small filters. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

#define FX_SR 44100.0f
#define FX_PI 3.14159265f
#define FX_DENORM 1e-18f   /* a tiny offset in feedback paths: tails never sink into slow denormal numbers */

/* synth effect types: "modfx_type" options */
enum { SFX_DELAY, SFX_BBD, SFX_TAPE, SFX_ECHO, SFX_FLANGER, SFX_SFLANGER, SFX_CHORUS, SFX_DCHORUS, SFX_ENSEMBLE,
       SFX_SCHORUS, SFX_PHASER6, SFX_PHASER12, SFX_TPHASER, SFX_CRUSH, SFX_RESON,
       /* the open-source ones: Mutable Instruments Rings and Clouds (mi_effects.h), Airwindows ToTape6, Density and
        * Console7 (airwindows.h) */
       SFX_RINGS, SFX_CLOUDS, SFX_AWTAPE, SFX_DENSITY, SFX_CONSOLE,
       /* the manual's two-tap delays (once the master delay alone; now any slot's) */
       SFX_SDELAY, SFX_PINGPONG,
       /* the manual's vocoders (vocoder.h): the audio input is the modulator, the slot's input the carrier */
       SFX_VOC10, SFX_VOC16, SFX_NTYPES };
#define SFX_IS_PHASER(t) ((t) == SFX_PHASER6 || (t) == SFX_PHASER12 || (t) == SFX_TPHASER)
#define SFX_IS_VOCODER(t) ((t) == SFX_VOC10 || (t) == SFX_VOC16)

/* what CTRL 1 and CTRL 2 can move: "modfx_c1_dst" / "modfx_c2_dst" options (FXD_AUTO: the type's own pick) */
enum { FXD_AUTO, FXD_MIX, FXD_FB, FXD_FB_R, FXD_TIME, FXD_TIME_R, FXD_DTIME, FXD_DFB, FXD_RATE, FXD_RATE_R, FXD_DEPTH,
       FXD_TONE, FXD_SPREAD, FXD_GAIN, FXD_SWEEP, FXD_NOISE, FXD_VRATE, FXD_VDEPTH, FXD_FLUTTER, FXD_WOW, FXD_BIAS,
       FXD_ROOM, FXD_DECAY, FXD_BITS, FXD_SRATE, FXD_RESO, FXD_CUT1, FXD_CUT2, FXD_CUT3,
       FXD_PITCH, FXD_STRUCT, FXD_POS, FXD_SIZE, FXD_DENSITY, FXD_TEXTURE, FXD_DRIVE, FXD_BUMP, FXD_LOWCUT,
       FXD_DTIME_R, FXD_DEPTH_R, FXD_OFFSET, FXD_FBL, FXD_FBR, FXD_XOVER, FXD_SIBIL, FXD_SENS, FXD_COUNT };
static const char *const FXD_NAME[FXD_COUNT] = {
    "Auto", "Mix", "Feedback", "Feedback R", "Time", "Time R", "Delay Time", "Delay FB", "Rate", "Rate R", "Depth",
    "Tone", "Spread", "Gain", "Sweep", "Noise", "Vib Rate", "Vib Depth", "Flutter", "Wow", "Bias", "Room", "Decay",
    "Bits", "Sample Rate", "Resonance", "Cutoff Low", "Cutoff Mid", "Cutoff High",
    "Pitch", "Structure", "Position", "Size", "Density", "Texture", "Drive", "Head Bump", "Low Cut",
    "Delay Time R", "Depth R", "Offset", "Feedback Left", "Feedback Right", "Crossover", "Sibilance", "Sensitivity"};
/* each type's CTRL 1 and CTRL 2 when set to Auto (the manual's pre-assignments, p103-108) */
static const unsigned char FX_AUTO_CTRL[SFX_NTYPES][2] = {
    [SFX_DELAY] = {FXD_MIX, FXD_DTIME},   [SFX_BBD] = {FXD_MIX, FXD_FB},        [SFX_TAPE] = {FXD_MIX, FXD_FB},
    [SFX_ECHO] = {FXD_MIX, FXD_TONE},     [SFX_FLANGER] = {FXD_TIME, FXD_RATE}, [SFX_SFLANGER] = {FXD_TIME, FXD_TIME_R},
    [SFX_CHORUS] = {FXD_RATE, FXD_DEPTH}, [SFX_DCHORUS] = {FXD_MIX, FXD_FB},    [SFX_ENSEMBLE] = {FXD_MIX, FXD_FB},
    [SFX_SCHORUS] = {FXD_MIX, FXD_FB},    [SFX_PHASER6] = {FXD_MIX, FXD_FB},    [SFX_PHASER12] = {FXD_MIX, FXD_FB},
    [SFX_TPHASER] = {FXD_MIX, FXD_FB},    [SFX_CRUSH] = {FXD_MIX, FXD_BITS},    [SFX_RESON] = {FXD_MIX, FXD_RESO},
    /* (not in the manual: the controls each is played by) */
    [SFX_RINGS] = {FXD_STRUCT, FXD_POS},  [SFX_CLOUDS] = {FXD_POS, FXD_DENSITY}, [SFX_AWTAPE] = {FXD_DRIVE, FXD_FLUTTER},
    [SFX_DENSITY] = {FXD_DRIVE, FXD_MIX}, [SFX_CONSOLE] = {FXD_DRIVE, FXD_MIX},
    [SFX_SDELAY] = {FXD_MIX, FXD_FB},     [SFX_PINGPONG] = {FXD_MIX, FXD_FB},
    [SFX_VOC10] = {FXD_MIX, FXD_SIBIL},   [SFX_VOC16] = {FXD_MIX, FXD_SIBIL}};

/* the master reverbs (original designs; SPACE is Airwindows Galactic) */
enum { REV_PLATE, REV_ROOM, REV_HALL, REV_SPACE, REV_SHIMMER, REV_BLACKHOLE, REV_NTYPES };
/* MASTER EFFECT 1 / 2 types ("dly_type" / "rev_type" options): the reverbs first (so "rev_type" kept its first four
 * values), then every type of the manual's list the synth effect has (the open-source ones stay synth-only) */
enum { MFX_PLATE, MFX_ROOM, MFX_HALL, MFX_SPACE, MFX_SHIMMER, MFX_BLACKHOLE, MFX_SDELAY, MFX_PINGPONG, MFX_DELAY,
       MFX_BBD, MFX_TAPE, MFX_ECHO, MFX_FLANGER, MFX_SFLANGER, MFX_CHORUS, MFX_DCHORUS, MFX_ENSEMBLE, MFX_SCHORUS,
       MFX_PHASER6, MFX_PHASER12, MFX_TPHASER, MFX_CRUSH, MFX_RESON,
       MFX_VOC10, MFX_VOC16,   /* the vocoders on a send (the manual's types may sit in either slot) */
       MFX_NTYPES };
/* a master type's synth effect type (-1: a reverb, REV_* = MFX_*) */
static const signed char MFX_SFX[MFX_NTYPES] = {
    -1, -1, -1, -1, -1, -1, SFX_SDELAY, SFX_PINGPONG, SFX_DELAY, SFX_BBD, SFX_TAPE, SFX_ECHO, SFX_FLANGER,
    SFX_SFLANGER, SFX_CHORUS, SFX_DCHORUS, SFX_ENSEMBLE, SFX_SCHORUS, SFX_PHASER6, SFX_PHASER12, SFX_TPHASER,
    SFX_CRUSH, SFX_RESON, SFX_VOC10, SFX_VOC16};
enum { HOLD_OFF, HOLD_FREEZE, HOLD_INFINITE, HOLD_PITCH };   /* "rev_freeze" / "dly_freeze" options */
enum { MB_DUAL, MB_STEREO };              /* "mfx_buss" options: two effects, or one with a stereo input */
enum { RES_LP, RES_BP, RES_HP };          /* "modfx_ftype" options */

typedef struct {   /* one effect slot's settings: the synth effect's, or a master effect's */
    int type;                             /* SFX_* (the synth effect, a master slot's non-reverb type) or REV_* */
    int mtype;                            /* a master slot: its MFX_* type (the synth effect: unused) */
    float mix, gain;                      /* 0..1 dry/wet; wet gain 0..2 (1 = unity) */
    float fb, fb_r, dfb;                  /* feedback -0.95..0.95; dfb: the Time Phaser's delay feedback 0..0.95 */
    float fbl, fbr;                       /* the stereo delay's FEEDBACK LEFT / RIGHT: shares of FEEDBACK, 0..1 */
    float time_ms, time_r_ms;             /* chorus/flanger delay times (left, right) */
    float dtime_ms, dtime_r_ms;           /* delay-type times (free or synced; right: the two-tap delays) */
    float rate_hz, rate_r_hz;             /* LFO rates (left, right) */
    float depth, depth_r, tone, spread, sweep, noise, offset;   /* 0..1 (depth_r: the stereo delay's right side) */
    float vib_hz, vib_depth;              /* the choruses' vibrato */
    float flutter, wow, bias, room, decay;     /* 0..1: tape and echo */
    float bits, srate_hz, xover_hz;       /* bit crusher; xover: its crossover (0: one band) */
    float reso, cut_hz[3], lvl[3];        /* resonator: 0..1, Hz, 0..1 */
    int ftype;
    /* the open-source types: Rings (resonator), Clouds (granular), Airwindows tape, Density and Console */
    float pitch_st;                       /* Rings: semitones from the note it follows; Clouds: its pitch, -48..48 */
    float structure, position, size, density, texture;   /* 0..1, their knobs (Rings: structure, position) */
    float drive, bump, lowcut;            /* 0..1: Airwindows input/density/fader, head bump, high-pass */
    int model, follow, gmode, quality, freeze;   /* Rings model, key follow; Clouds mode, quality, freeze */
    float note;                           /* Rings: the note it rings at (follow: the newest voice's, plus pitch_st) */
    int strum;                            /* Rings: a new note started since the last block */
    /* the reverbs (master effects) */
    int hold;                             /* HOLD_*: freeze (input shut, no decay), infinite (no decay), pitch freeze */
    float rev_rt60, rev_size;             /* decay time (s) and size (0.5..2) */
    float rev_damp, rev_lowdamp, rev_pre_ms, rev_diff, rev_tone, rev_mod, rev_early;   /* 0..1 except pre and mod (0..2) */
    float rev_decay_k, rev_size_k;        /* the decay and size knobs, 0..1 (SPACE uses them as they are) */
    float distance;                       /* plate: the early reflections' spread, 0..1 (0: none) */
    float hifreq_hz;                      /* room HIGH FREQ: the high band's crossover (hall and shimmer too) */
    float lvl_lo, lvl_mid, lvl_hi;        /* band levels as gains, 0..2 (1: as it is) */
    float dec_lo, dec_hi;                 /* hall and shimmer LOW / HIGH DECAY: the bands' decay times x0.1..5 */
    float pitch_a, pitch_b, pdecay;       /* shimmer: A and B pitch (cents), how much of the pitched sound goes round */
    float gravity;                        /* blackhole: -1 (blooms, swells in) .. +1 (pulled down, shorter) */
    /* the vocoders (vocoder.h) */
    float sibil, sens;                    /* SIBILANCE and SENSITIVITY, 0..1 (TONE, NOISE and MIX as above) */
    const float *voc_mod;                 /* the modulator for this piece (the audio input, mono), NULL: silence */
} fxs_params_t;

typedef struct {   /* what the effects read; worked out by the engine (fx_params() in engine.c) from the knobs */
    int sfx_on;
    fxs_params_t sfx;                     /* the synth effect: one type at a time, mono or stereo in, stereo out */
    /* the master effects: two slots on two sends, their returns added to the dry signal */
    int mfx_on, buss;                     /* buss: MB_DUAL (slot 1 on SEND 1, slot 2 on SEND 2) or MB_STEREO */
    int off1, off2;                       /* (2026-10-30) slot 1 / slot 2 switched off on its own (0: on, as before) */
    float send1, send2, series, width;    /* 0..1, 0..1, 0..1 (effect 1 into 2), 0..2 */
    fxs_params_t mfx[2];
    float wet_pan;                        /* the synth pan, -1..1: where the synth effect's wet signal sits */
    int ramp;                             /* samples to the settings' next change (0: the block): the gains' ramp length */
} fx_params_t;

/* ---- knob (0..100, or the param's own range) to units, shared by fx_params() and the display text ---- */

/* the effects' tempo divisions (manual p103): beats (quarter notes) per step; a synced time knob picks one */
static const struct { const char *name; float beats; } FX_DIV[] = {
    {"1/64T", 1.0f / 24}, {"1/64", 1.0f / 16}, {"1/64.", 3.0f / 32}, {"1/32T", 1.0f / 12}, {"1/32", 1.0f / 8},
    {"1/32.", 3.0f / 16}, {"1/16T", 1.0f / 6}, {"1/16", 0.25f}, {"1/16.", 0.375f}, {"1/8T", 1.0f / 3}, {"1/8", 0.5f},
    {"1/8.", 0.75f}, {"1/4T", 2.0f / 3}, {"1/4", 1}, {"1/4.", 1.5f}, {"1/2T", 4.0f / 3}, {"1/2", 2}, {"1/2.", 3},
    {"1", 4}, {"2", 8}, {"4", 16}};
#define FX_NDIV ((int)(sizeof FX_DIV / sizeof FX_DIV[0]))

static inline float fx_clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float fx_logk(float lo, float hi, float knob) { return lo * powf(hi / lo, fx_clamp(knob, 0, 100) * 0.01f); }
static inline int fx_div(float knob) { return (int)fx_clamp(roundf(knob * 0.01f * (FX_NDIV - 1)), 0, FX_NDIV - 1); }
static inline float fx_div_ms(float knob, float bpm) { return 60000.0f / bpm * FX_DIV[fx_div(knob)].beats; }
/* a synced rate: one LFO cycle per division (longest on the left, as for free rates) */
static inline float fx_div_hz(float knob, float bpm) { return bpm / 60.0f / FX_DIV[FX_NDIV - 1 - fx_div(knob)].beats; }
static inline const char *fx_div_rate_name(float knob) { return FX_DIV[FX_NDIV - 1 - fx_div(knob)].name; }

static inline float fx_rate_hz(float k) { return fx_logk(0.01f, 20.0f, k); }        /* LFO rates 0.01 .. 20 Hz */
static inline float fx_vib_hz(float k) { return fx_logk(0.1f, 10.0f, k); }          /* vibrato 0.1 .. 10 Hz */
static inline float fx_delay_ms(float k) { return fx_logk(0.1f, 2000.0f, k); }      /* delay times 0.1 ms .. 2 s */
static inline float fx_srate_hz(float k) { return fx_logk(128.0f, FX_SR, k); }      /* crusher 128 Hz .. 44.1 kHz */
static inline float fx_cut_hz(float k) { return fx_logk(10.0f, 22000.0f, k); }      /* resonator 10 Hz .. 22 kHz */
static inline float fx_gain_x(float k) { return fx_clamp(k, 0, 100) * 0.02f; }      /* 0 .. 2x, 50% = unity */
static inline float fx_rev_rt60(float k) { return fx_logk(0.25f, 10.0f, k); }       /* 0.25 .. 10 s at size 100% */
static inline float fx_rev_size(float k) { return fx_logk(0.5f, 2.0f, k); }         /* 50% .. 200%, 100% centred */
static inline float fx_hall_rt60(float k) { return fx_logk(0.1f, 20.0f, k); }      /* hall, shimmer: 0.1 .. 20 s at 100% */
/* the blackhole's FEEDBACK (0..100) as a decay time: 1 s .. 100 s at size 100% */
static inline float fx_bh_rt60(float k) { return fx_logk(1.0f, 100.0f, k); }
static inline float fx_xover_hz(float k) { return k <= 0.0f ? 0.0f : fx_logk(50.0f, 5000.0f, k); }   /* 0: off */
static inline float fx_hifreq_hz(float k) { return fx_logk(1000.0f, 5000.0f, k); }  /* room HIGH FREQ 1 .. 5 kHz */
static inline float fx_level(float v) { return fx_clamp(1.0f + v * 0.01f, 0.0f, 2.0f); }   /* -100..100: x0 .. x2 */

/* a chorus/flanger time knob's range per type (manual p105-107), in ms; right: the right-channel knob */
static inline float fx_mod_time_ms(int type, float k) {
    switch (type) {
    case SFX_FLANGER: return fx_logk(0.1f, 15.0f, k);
    case SFX_SFLANGER: return fx_logk(0.1f, 25.0f, k);
    case SFX_DCHORUS: case SFX_ENSEMBLE: return fx_logk(3.0f, 60.0f, k);
    case SFX_SCHORUS: return fx_logk(1.0f, 50.0f, k);
    default: return fx_logk(0.1f, 50.0f, k);   /* chorus */
    }
}

/* ---- delay lines over power-of-two buffers ---- */
#define DL(name, size) struct { float buf[size]; int w; } name
#define DL_MASK(d) ((int)(sizeof (d).buf / sizeof (d).buf[0]) - 1)
#define DL_WRITE(d, x) do { (d).buf[(d).w] = (x); (d).w = ((d).w + 1) & DL_MASK(d); } while (0)
#define DL_TAP(d, n) ((d).buf[((d).w - 1 - (n)) & DL_MASK(d)])   /* the sample written n writes ago */
/* Schroeder all-pass on a fixed delay of n samples: w = x + g*d, y = d - g*w */
#define AP(d, n, g, x, y) do { float d_ = DL_TAP(d, (n) - 1); float w_ = (x) + (g) * d_; DL_WRITE(d, w_); \
                               (y) = d_ - (g) * w_; } while (0)

/* 4-point Hermite read `dist` samples back from write position w (dist >= 1) */
static inline float fx_hermite(const float *buf, int mask, int w, float dist) {
    float pos = (float)w - 1.0f - dist;
    int i = (int)floorf(pos);
    float t = pos - (float)i;
    float xm1 = buf[(i - 1) & mask], x0 = buf[i & mask], x1 = buf[(i + 1) & mask], x2 = buf[(i + 2) & mask];
    float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

/* ---- modulation shapes, phase 0..1 ---- */
static inline float fx_wrap(float ph) { return ph >= 1.0f ? ph - 1.0f : ph; }
static inline float fx_tri(float ph) { return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; }   /* -1..1 */
static inline float fx_sin(float ph) {   /* sin(2 pi ph), parabolic with one refinement step (within 0.1%) */
    float x = 2.0f * ph;
    if (x > 1.0f) x -= 2.0f;   /* -1..1: sin(pi x) */
    float y = 4.0f * x * (1.0f - fabsf(x));
    return 0.225f * (y * fabsf(y) - y) + y;
}
static inline float fx_white(uint32_t *x) {   /* xorshift32: -1..1 */
    *x ^= *x << 13; *x ^= *x >> 17; *x ^= *x << 5;
    return (int32_t)*x * (1.0f / 2147483648.0f);
}
/* one-pole low-pass coefficient for cutoff hz (1: open) */
static inline float fx_lp_coef(float hz) { return hz >= 0.45f * FX_SR ? 1.0f : 1.0f - expf(-2.0f * FX_PI * hz / FX_SR); }
/* a knob's 0..1 tone to a low-pass coefficient: 200 Hz .. 20 kHz, fully open at the top */
static inline float fx_tone_coef(float tone) { return tone >= 0.999f ? 1.0f : fx_lp_coef(200.0f * powf(100.0f, tone)); }
/* soft saturation, about tanh: linear near 0, bends smoothly to +-1 */
static inline float fx_sat(float x) {
    x = fx_clamp(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
/* a value ramped across a block: from its last block's value to this one's. len: the ramp's length when the block is
 * only its first part (the engine's control step split by the host's block end; 0 or n: the block); the next block,
 * at the same target, carries on at the same slope */
static inline float fx_ramp(float *last, float to, int n, int len, float *step) {
    float from = *last;
    if (len < n) len = n;
    *step = (to - from) / (float)len;
    *last = len == n ? to : from + *step * (float)n;
    return from;
}
