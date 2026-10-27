/* ONE-ish: four Airwindows effects, ported to plain C from the Airwindows repository (plugins/LinuxVST/src/<Name>/
 * <Name>Proc.cpp, processReplacing, commit dfa54018 of github.com/airwindows/airwindows):
 *   ToTape6         tape emulation: flutter, soften, head bump, Mojo saturation, UnBox, ADClip safety clip
 *                   ("Tape Color" synth effect)
 *   Density         sine-curve saturation / anti-saturation with a high-pass ("Density" synth effect)
 *   Console7Channel into Console7Buss: the console's channel saturation then its buss decode, run as a pair on the
 *                   one stereo signal ("Console" synth effect)
 *   Galactic        the big pad/ambient reverb: a vibrato pre-delay into three stages of four-line feedback networks
 *                   ("Space" master reverb type)
 * The algorithms are as written, per sample in double precision, at 44.1 kHz (the engine's only rate: Galactic's
 * undersampling for higher rates, cycleEnd > 1, never runs, so it is left out). Changes, none of which alter the
 * sound: Galactic's delay memory is float rather than double (half the memory; within float rounding); the 32-bit
 * floating-point output dither is left out (its random state still advances every sample, as ToTape6's flutter reads
 * it); pow(2, x) for integer x as ldexp; the seeds of the random states are fixed rather than from rand(). The
 * parameters are the plugins' own 0..1 controls (A, B, C...), set per call like a host block.
 *
 * Airwindows is under the MIT licence:
 *
 * MIT License
 *
 * Copyright (c) 2018 Chris Johnson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

#define AW_PI 3.141592653589793238

/* the random state every Airwindows plugin keeps per channel (xorshift32), advanced once per output sample */
static inline void aw_fpd_next(uint32_t *fpd) { *fpd ^= *fpd << 13; *fpd ^= *fpd >> 17; *fpd ^= *fpd << 5; }
#define AW_SEED_L 0x2545F491u   /* fixed seeds (the plugins draw them from rand(), anything >= 16386) */
#define AW_SEED_R 0x9E3779B9u

/* ---------------- ToTape6 ---------------- */
typedef struct {
    double dL[502], dR[502];
    int gcount;
    double rateof, sweep, nextmax;
    double iirMidRollerAL, iirMidRollerBL, iirHeadBumpAL, iirHeadBumpBL;
    double iirMidRollerAR, iirMidRollerBR, iirHeadBumpAR, iirHeadBumpBR;
    double biquadAL[9], biquadBL[9], biquadCL[9], biquadDL[9];
    double biquadAR[9], biquadBR[9], biquadCR[9], biquadDR[9];
    int flip;
    double lastSampleL, lastSampleR;
    uint32_t fpdL, fpdR;
} aw_totape6_t;

static void aw_totape6_init(aw_totape6_t *t) {
    memset(t, 0, sizeof *t);
    t->rateof = 0.5;
    t->sweep = AW_PI;
    t->nextmax = 0.5;
    t->fpdL = AW_SEED_L;
    t->fpdR = AW_SEED_R;
}

/* one channel of ToTape6's tape stage: head bump (an interleaved biquad between sin and asin) and the sine/asine
 * bandpass, alternating between the A and B filter sets each sample */
static inline double aw_tt6_biquad(double x, double *b) {
    double t = (x * b[2]) + b[7];
    b[7] = (x * b[3]) - (t * b[5]) + b[8];
    b[8] = (x * b[4]) - (t * b[6]);
    return t;
}
static inline double aw_tt6_stage(double in, double roll, double bumpfreq, double *mid, double *bump, double *bqBump,
                                  double *bqTape, double *highs) {
    *mid = (*mid * (1.0 - roll)) + (in * roll);
    *highs = in - *mid;
    *bump += (in * 0.05);
    *bump -= (*bump * *bump * *bump * bumpfreq);
    *bump = sin(*bump);
    *bump = aw_tt6_biquad(*bump, bqBump);   /* interleaved biquad */
    if (*bump > 1.0) *bump = 1.0;
    if (*bump < -1.0) *bump = -1.0;
    *bump = asin(*bump);
    double x = sin(in);
    x = aw_tt6_biquad(x, bqTape);
    if (x > 1.0) x = 1.0;
    if (x < -1.0) x = -1.0;
    return asin(x);
}
/* ADClip, ToTape6's safety clipper */
static inline double aw_adclip(double x, double *last) {
    const double refclip = 0.99, softness = 0.618033988749894848204586;
    if (*last >= refclip) *last = x < refclip ? (refclip * softness) + (x * (1.0 - softness)) : refclip;
    if (*last <= -refclip) *last = x > -refclip ? (-refclip * softness) + (x * (1.0 - softness)) : -refclip;
    if (x > refclip) x = *last < refclip ? (refclip * softness) + (*last * (1.0 - softness)) : refclip;
    if (x < -refclip) x = *last > -refclip ? (-refclip * softness) + (*last * (1.0 - softness)) : -refclip;
    *last = x;
    if (x > refclip) x = refclip;   /* final iron bar */
    if (x < -refclip) x = -refclip;
    return x;
}
static inline double aw_soften(double x, double highs) {   /* Soften, by the polarity of the highs */
    double a = fabs(highs) * 1.57079633;
    if (a > 1.57079633) a = 1.57079633;
    a = 1 - cos(a);
    if (highs > 0) x -= a;
    if (highs < 0) x += a;
    return x;
}
static inline double aw_mojo(double x) {   /* flattens out very softly before wavefolding */
    if (x > 1.0) x = 1.0;
    if (x < -1.0) x = -1.0;
    double mojo = pow(fabs(x), 0.25);
    if (mojo > 0.0) x = (sin(x * mojo * AW_PI * 0.5) / mojo);
    return x;
}

/* A input (0.5: 0 dB, +-12 dB), B soften, C head bump, D flutter, E output (0.5: 0 dB), F dry/wet */
static void aw_totape6(aw_totape6_t *t, float A, float B, float C, float D, float E, float F, const float *in1,
                       const float *in2, float *out1, float *out2, int sampleFrames) {
    const double overallscale = 1.0;   /* 44.1 kHz */
    double inputgain = pow(10.0, ((A - 0.5) * 24.0) / 20.0);
    double SoftenControl = pow(B, 2);
    double RollAmount = (1.0 - (SoftenControl * 0.45)) / overallscale;
    double HeadBumpControl = C * 0.25 * inputgain;
    double HeadBumpFreq = 0.12 / overallscale;
    t->biquadAL[0] = t->biquadBL[0] = t->biquadAR[0] = t->biquadBR[0] = 0.007 / overallscale;
    t->biquadAL[1] = t->biquadBL[1] = t->biquadAR[1] = t->biquadBR[1] = 0.0009;
    double K = tan(AW_PI * t->biquadBR[0]);
    double norm = 1.0 / (1.0 + K / t->biquadBR[1] + K * K);
    t->biquadAL[2] = t->biquadBL[2] = t->biquadAR[2] = t->biquadBR[2] = K / t->biquadBR[1] * norm;
    t->biquadAL[4] = t->biquadBL[4] = t->biquadAR[4] = t->biquadBR[4] = -t->biquadBR[2];
    t->biquadAL[5] = t->biquadBL[5] = t->biquadAR[5] = t->biquadBR[5] = 2.0 * (K * K - 1.0) * norm;
    t->biquadAL[6] = t->biquadBL[6] = t->biquadAR[6] = t->biquadBR[6] = (1.0 - K / t->biquadBR[1] + K * K) * norm;
    t->biquadCL[0] = t->biquadDL[0] = t->biquadCR[0] = t->biquadDR[0] = 0.032 / overallscale;
    t->biquadCL[1] = t->biquadDL[1] = t->biquadCR[1] = t->biquadDR[1] = 0.0007;
    K = tan(AW_PI * t->biquadDR[0]);
    norm = 1.0 / (1.0 + K / t->biquadDR[1] + K * K);
    t->biquadCL[2] = t->biquadDL[2] = t->biquadCR[2] = t->biquadDR[2] = K / t->biquadDR[1] * norm;
    t->biquadCL[4] = t->biquadDL[4] = t->biquadCR[4] = t->biquadDR[4] = -t->biquadDR[2];
    t->biquadCL[5] = t->biquadDL[5] = t->biquadCR[5] = t->biquadDR[5] = 2.0 * (K * K - 1.0) * norm;
    t->biquadCL[6] = t->biquadDL[6] = t->biquadCR[6] = t->biquadDR[6] = (1.0 - K / t->biquadDR[1] + K * K) * norm;
    double depth = pow(D, 2) * overallscale * 70;
    double fluttertrim = (0.0024 * pow(D, 2)) / overallscale;
    double outputgain = pow(10.0, ((E - 0.5) * 24.0) / 20.0);
    double wet = F;
    for (int i = 0; i < sampleFrames; i++) {
        double inputSampleL = in1[i], inputSampleR = in2[i];
        if (fabs(inputSampleL) < 1.18e-23) inputSampleL = t->fpdL * 1.18e-17;
        if (fabs(inputSampleR) < 1.18e-23) inputSampleR = t->fpdR * 1.18e-17;
        double drySampleL = inputSampleL, drySampleR = inputSampleR;
        if (inputgain < 1.0) { inputSampleL *= inputgain; inputSampleR *= inputgain; }   /* gain cut before */
        double flutterrandy = ((double)t->fpdL / UINT32_MAX);
        if (t->gcount < 0 || t->gcount > 499) t->gcount = 499;
        t->dL[t->gcount] = inputSampleL;
        t->dR[t->gcount] = inputSampleR;
        int count = t->gcount;
        if (depth != 0.0) {   /* flutter: a random-rate pitch wobble, interpolated */
            double offset = depth + (depth * pow(t->rateof, 2) * sin(t->sweep));
            count += (int)floor(offset);
            double fr = offset - floor(offset);
            inputSampleL = (t->dL[count - ((count > 499) ? 500 : 0)] * (1 - fr));
            inputSampleR = (t->dR[count - ((count > 499) ? 500 : 0)] * (1 - fr));
            inputSampleL += (t->dL[count + 1 - ((count + 1 > 499) ? 500 : 0)] * fr);
            inputSampleR += (t->dR[count + 1 - ((count + 1 > 499) ? 500 : 0)] * fr);
            t->rateof = (t->rateof * (1.0 - fluttertrim)) + (t->nextmax * fluttertrim);
            t->sweep += t->rateof * fluttertrim;
            if (t->sweep >= (AW_PI * 2.0)) {
                t->sweep -= AW_PI;
                t->nextmax = 0.24 + (flutterrandy * 0.74);
            }
        }
        t->gcount--;
        double vibDrySampleL = inputSampleL, vibDrySampleR = inputSampleR, HighsSampleL, HighsSampleR;
        if (t->flip) {
            inputSampleL = aw_tt6_stage(inputSampleL, RollAmount, HeadBumpFreq, &t->iirMidRollerAL, &t->iirHeadBumpAL,
                                        t->biquadAL, t->biquadCL, &HighsSampleL);
            inputSampleR = aw_tt6_stage(inputSampleR, RollAmount, HeadBumpFreq, &t->iirMidRollerAR, &t->iirHeadBumpAR,
                                        t->biquadAR, t->biquadCR, &HighsSampleR);
        } else {
            inputSampleL = aw_tt6_stage(inputSampleL, RollAmount, HeadBumpFreq, &t->iirMidRollerBL, &t->iirHeadBumpBL,
                                        t->biquadBL, t->biquadDL, &HighsSampleL);
            inputSampleR = aw_tt6_stage(inputSampleR, RollAmount, HeadBumpFreq, &t->iirMidRollerBR, &t->iirHeadBumpBR,
                                        t->biquadBR, t->biquadDR, &HighsSampleR);
        }
        t->flip = !t->flip;
        double groundSampleL = vibDrySampleL - inputSampleL;   /* set up UnBox on fluttered audio */
        double groundSampleR = vibDrySampleR - inputSampleR;
        if (inputgain > 1.0) { inputSampleL *= inputgain; inputSampleR *= inputgain; }
        inputSampleL = aw_soften(inputSampleL, HighsSampleL);
        inputSampleR = aw_soften(inputSampleR, HighsSampleR);
        double suppress = (1.0 - fabs(inputSampleL)) * 0.00013;   /* restrain the head bump's resonance */
        if (t->iirHeadBumpAL > suppress) t->iirHeadBumpAL -= suppress;
        if (t->iirHeadBumpAL < -suppress) t->iirHeadBumpAL += suppress;
        if (t->iirHeadBumpBL > suppress) t->iirHeadBumpBL -= suppress;
        if (t->iirHeadBumpBL < -suppress) t->iirHeadBumpBL += suppress;
        suppress = (1.0 - fabs(inputSampleR)) * 0.00013;
        if (t->iirHeadBumpAR > suppress) t->iirHeadBumpAR -= suppress;
        if (t->iirHeadBumpAR < -suppress) t->iirHeadBumpAR += suppress;
        if (t->iirHeadBumpBR > suppress) t->iirHeadBumpBR -= suppress;
        if (t->iirHeadBumpBR < -suppress) t->iirHeadBumpBR += suppress;
        inputSampleL += ((t->iirHeadBumpAL + t->iirHeadBumpBL) * HeadBumpControl);   /* apply Fatten */
        inputSampleR += ((t->iirHeadBumpAR + t->iirHeadBumpBR) * HeadBumpControl);
        inputSampleL = aw_mojo(inputSampleL);
        inputSampleR = aw_mojo(inputSampleR);
        inputSampleL += groundSampleL;   /* apply UnBox processing */
        inputSampleR += groundSampleR;
        if (outputgain != 1.0) { inputSampleL *= outputgain; inputSampleR *= outputgain; }
        inputSampleL = aw_adclip(inputSampleL, &t->lastSampleL);
        inputSampleR = aw_adclip(inputSampleR, &t->lastSampleR);
        if (wet != 1.0) {
            inputSampleL = (inputSampleL * wet) + (drySampleL * (1.0 - wet));
            inputSampleR = (inputSampleR * wet) + (drySampleR * (1.0 - wet));
        }
        aw_fpd_next(&t->fpdL);
        aw_fpd_next(&t->fpdR);
        out1[i] = (float)inputSampleL;
        out2[i] = (float)inputSampleR;
    }
}

/* ---------------- Density ---------------- */
typedef struct {
    double iirSampleAL, iirSampleBL, iirSampleAR, iirSampleBR;
    int fpFlip;
    uint32_t fpdL, fpdR;
} aw_density_t;

static void aw_density_init(aw_density_t *d) {
    memset(d, 0, sizeof *d);
    d->fpFlip = 1;
    d->fpdL = AW_SEED_L;
    d->fpdR = AW_SEED_R;
}

/* one channel of Density's curve: `count` whole sine stages, then the blend (boost, or 1 - cos: starved) */
static inline double aw_density_curve(double x, double density, double out) {
    double count = density;
    while (count > 1.0) {
        double b = fabs(x) * 1.57079633;
        if (b > 1.57079633) b = 1.57079633;
        b = sin(b);
        x = x > 0.0 ? b : -b;
        count = count - 1.0;
    }
    double b = fabs(x) * 1.57079633;
    if (b > 1.57079633) b = 1.57079633;
    b = density > 0 ? sin(b) : 1 - cos(b);
    return x > 0 ? (x * (1 - out)) + (b * out) : (x * (1 - out)) - (b * out);
}

/* A density (0.2: none, 0..1 = -1..4), B highpass, C output level, D dry/wet */
static void aw_density(aw_density_t *d, float A, float B, float C, float D, const float *in1, const float *in2,
                       float *out1, float *out2, int sampleFrames) {
    const double overallscale = 1.0;
    double density = (A * 5.0) - 1.0;
    double iirAmount = pow(B, 3) / overallscale;
    double output = C, wet = D, dry = 1.0 - wet;
    double out = fabs(density);
    density = density * fabs(density);
    while (out > 1.0) out = out - 1.0;
    for (int i = 0; i < sampleFrames; i++) {
        double inputSampleL = in1[i], inputSampleR = in2[i];
        if (fabs(inputSampleL) < 1.18e-23) inputSampleL = d->fpdL * 1.18e-17;
        if (fabs(inputSampleR) < 1.18e-23) inputSampleR = d->fpdR * 1.18e-17;
        double drySampleL = inputSampleL, drySampleR = inputSampleR;
        if (d->fpFlip) {   /* highpass section */
            d->iirSampleAL = (d->iirSampleAL * (1.0 - iirAmount)) + (inputSampleL * iirAmount);
            inputSampleL -= d->iirSampleAL;
            d->iirSampleAR = (d->iirSampleAR * (1.0 - iirAmount)) + (inputSampleR * iirAmount);
            inputSampleR -= d->iirSampleAR;
        } else {
            d->iirSampleBL = (d->iirSampleBL * (1.0 - iirAmount)) + (inputSampleL * iirAmount);
            inputSampleL -= d->iirSampleBL;
            d->iirSampleBR = (d->iirSampleBR * (1.0 - iirAmount)) + (inputSampleR * iirAmount);
            inputSampleR -= d->iirSampleBR;
        }
        d->fpFlip = !d->fpFlip;
        inputSampleL = aw_density_curve(inputSampleL, density, out);
        inputSampleR = aw_density_curve(inputSampleR, density, out);
        if (output < 1.0) { inputSampleL *= output; inputSampleR *= output; }
        if (wet < 1.0) {
            inputSampleL = (drySampleL * dry) + (inputSampleL * wet);
            inputSampleR = (drySampleR * dry) + (inputSampleR * wet);
        }
        aw_fpd_next(&d->fpdL);
        aw_fpd_next(&d->fpdR);
        out1[i] = (float)inputSampleL;
        out2[i] = (float)inputSampleR;
    }
}

/* ---------------- Console7Channel into Console7Buss ---------------- */
typedef struct {
    double gainchase, chasespeed, biquadA[15];
    uint32_t fpdL, fpdR;
} aw_c7ch_t;
typedef struct {
    double gainchase, chasespeed, biquadA[15], biquadB[15];
    uint32_t fpdL, fpdR;
} aw_c7bus_t;
typedef struct { aw_c7ch_t ch; aw_c7bus_t bus; } aw_console7_t;

static void aw_console7_init(aw_console7_t *c) {
    memset(c, 0, sizeof *c);
    c->ch.gainchase = c->bus.gainchase = -1.0;
    c->ch.chasespeed = c->bus.chasespeed = 64.0;
    c->ch.fpdL = c->bus.fpdL = AW_SEED_L;
    c->ch.fpdR = c->bus.fpdR = AW_SEED_R;
}

/* the low-pass both use (20 kHz at 44.1 kHz), resonance q, into b[2..6] */
static inline void aw_c7_lowpass(double *b, double q) {
    b[0] = 20000.0 / 44100.0;
    b[1] = q;
    double K = tan(AW_PI * b[0]);
    double norm = 1.0 / (1.0 + K / b[1] + K * K);
    b[2] = K * K * norm;
    b[3] = 2.0 * b[2];
    b[4] = b[2];
    b[5] = 2.0 * (K * K - 1.0) * norm;
    b[6] = (1.0 - K / b[1] + K * K) * norm;
}
/* direct form 1, left (state 7-10) or right (11-14) */
static inline double aw_c7_df1(double x, double *b, int s) {
    double y = b[2] * x + b[3] * b[s] + b[4] * b[s + 1] - b[5] * b[s + 2] - b[6] * b[s + 3];
    b[s + 1] = b[s]; b[s] = x; b[s + 3] = b[s + 2]; b[s + 2] = y;
    return y;
}

/* A: Console7Channel's fader (0.772: neutral with a full-scale sine); then Console7Buss's at 1.0 (its default) */
static void aw_console7(aw_console7_t *c, float A, const float *in1, const float *in2, float *out1, float *out2,
                        int sampleFrames) {
    aw_c7ch_t *h = &c->ch;
    double inputgain = A * 1.272019649514069;   /* the square root of 1.618...: channels go to 12 */
    if (h->gainchase != inputgain) h->chasespeed *= 2.0;
    if (h->chasespeed > sampleFrames) h->chasespeed = sampleFrames;
    if (h->gainchase < 0.0) h->gainchase = inputgain;
    aw_c7_lowpass(h->biquadA, 1.618033988749894848204586);
    aw_c7bus_t *u = &c->bus;
    double busgain = 1.0 * 1.03;
    if (u->gainchase != busgain) u->chasespeed *= 2.0;
    if (u->chasespeed > sampleFrames) u->chasespeed = sampleFrames;
    if (u->gainchase < 0.0) u->gainchase = busgain;
    aw_c7_lowpass(u->biquadA, 0.618033988749894848204586);
    aw_c7_lowpass(u->biquadB, 0.5);
    for (int i = 0; i < sampleFrames; i++) {
        /* Console7Channel */
        double l = in1[i], r = in2[i];
        if (fabs(l) < 1.18e-23) l = h->fpdL * 1.18e-17;
        if (fabs(r) < 1.18e-23) r = h->fpdR * 1.18e-17;
        l = aw_c7_df1(l, h->biquadA, 7);
        r = aw_c7_df1(r, h->biquadA, 11);
        h->chasespeed *= 0.9999; h->chasespeed -= 0.01; if (h->chasespeed < 64.0) h->chasespeed = 64.0;
        h->gainchase = (((h->gainchase * h->chasespeed) + inputgain) / (h->chasespeed + 1.0));
        if (1.0 != h->gainchase) { l *= pow(h->gainchase, 3); r *= pow(h->gainchase, 3); }
        if (l > 1.097) l = 1.097;
        if (l < -1.097) l = -1.097;
        l = ((sin(l * fabs(l)) / ((fabs(l) == 0.0) ? 1 : fabs(l))) * 0.8) + (sin(l) * 0.2);
        if (r > 1.097) r = 1.097;
        if (r < -1.097) r = -1.097;
        r = ((sin(r * fabs(r)) / ((fabs(r) == 0.0) ? 1 : fabs(r))) * 0.8) + (sin(r) * 0.2);
        if (1.0 != h->gainchase && 0.0 != h->gainchase) { l /= h->gainchase; r /= h->gainchase; }
        aw_fpd_next(&h->fpdL);
        aw_fpd_next(&h->fpdR);
        l = (float)l;   /* the channel plugin's float output, as into the buss plugin */
        r = (float)r;
        /* Console7Buss */
        if (fabs(l) < 1.18e-23) l = u->fpdL * 1.18e-17;
        if (fabs(r) < 1.18e-23) r = u->fpdR * 1.18e-17;
        l = aw_c7_df1(l, u->biquadA, 7);
        r = aw_c7_df1(r, u->biquadA, 11);
        u->chasespeed *= 0.9999; u->chasespeed -= 0.01; if (u->chasespeed < 64.0) u->chasespeed = 64.0;
        u->gainchase = (((u->gainchase * u->chasespeed) + busgain) / (u->chasespeed + 1.0));
        if (1.0 != u->gainchase) { l *= sqrt(u->gainchase); r *= sqrt(u->gainchase); }
        if (l > 1.0) l = 1.0;
        if (l < -1.0) l = -1.0;
        l = ((asin(l * fabs(l)) / ((fabs(l) == 0.0) ? 1 : fabs(l))) * 0.618033988749894848204586) + (asin(l) * 0.381966011250105);
        if (r > 1.0) r = 1.0;
        if (r < -1.0) r = -1.0;
        r = ((asin(r * fabs(r)) / ((fabs(r) == 0.0) ? 1 : fabs(r))) * 0.618033988749894848204586) + (asin(r) * 0.381966011250105);
        l = aw_c7_df1(l, u->biquadB, 7);
        r = aw_c7_df1(r, u->biquadB, 11);
        if (1.0 != u->gainchase) { l *= sqrt(u->gainchase); r *= sqrt(u->gainchase); }
        aw_fpd_next(&u->fpdL);
        aw_fpd_next(&u->fpdR);
        out1[i] = (float)l;
        out2[i] = (float)r;
    }
}

/* ---------------- Galactic ---------------- */
typedef struct {
    double iirAL, iirBL, iirAR, iirBR;
    float aIL[6480], aJL[3660], aKL[1720], aLL[680];
    float aAL[9700], aBL[6000], aCL[2320], aDL[940];
    float aEL[15220], aFL[8460], aGL[4540], aHL[3200];
    float aIR[6480], aJR[3660], aKR[1720], aLR[680];
    float aAR[9700], aBR[6000], aCR[2320], aDR[940];
    float aER[15220], aFR[8460], aGR[4540], aHR[3200];
    float aML[3111], aMR[3111];
    double vibM, oldfpd;
    double feedbackAL, feedbackBL, feedbackCL, feedbackDL;
    double feedbackAR, feedbackBR, feedbackCR, feedbackDR;
    int countA, countB, countC, countD, countE, countF, countG, countH, countI, countJ, countK, countL, countM;
    uint32_t fpdL, fpdR;
} aw_galactic_t;

static void aw_galactic_init(aw_galactic_t *g) {
    memset(g, 0, sizeof *g);
    g->countA = g->countB = g->countC = g->countD = g->countE = g->countF = g->countG = g->countH = 1;
    g->countI = g->countJ = g->countK = g->countL = g->countM = 1;
    g->vibM = 3.0;
    g->oldfpd = 429496.7295;
    g->fpdL = AW_SEED_L;
    g->fpdR = AW_SEED_R;
}

/* one step of a delay line `len` + 1 long: the place after c, wrapping as the plugin does */
static inline int aw_step(int c, int len) { c++; return (c < 0 || c > len) ? 0 : c; }

/* A replace (how much of the incoming sound replaces the space: 0 = it holds), B brightness, C detune, D bigness,
 * E dry/wet */
static void aw_galactic(aw_galactic_t *g, float A, float B, float C, float D, float E, const float *in1,
                        const float *in2, float *out1, float *out2, int sampleFrames) {
    const double overallscale = 1.0;
    double regen = 0.0625 + ((1.0 - A) * 0.0625);
    double attenuate = (1.0 - (regen / 0.125)) * 1.333;
    double lowpass = pow(1.00001 - (1.0 - B), 2.0) / sqrt(overallscale);
    double drift = pow(C, 3) * 0.001;
    double size = (D * 1.77) + 0.1;
    double wet = 1.0 - (pow(1.0 - E, 3));
    int delayI = 3407.0 * size, delayJ = 1823.0 * size, delayK = 859.0 * size, delayL = 331.0 * size;
    int delayA = 4801.0 * size, delayB = 2909.0 * size, delayC = 1153.0 * size, delayD = 461.0 * size;
    int delayE = 7607.0 * size, delayF = 4217.0 * size, delayG = 2269.0 * size, delayH = 1597.0 * size;
    const int delayM = 256;
    for (int i = 0; i < sampleFrames; i++) {
        double inputSampleL = in1[i], inputSampleR = in2[i];
        if (fabs(inputSampleL) < 1.18e-23) inputSampleL = g->fpdL * 1.18e-17;
        if (fabs(inputSampleR) < 1.18e-23) inputSampleR = g->fpdR * 1.18e-17;
        double drySampleL = inputSampleL, drySampleR = inputSampleR;
        g->vibM += (g->oldfpd * drift);
        if (g->vibM > (AW_PI * 2.0)) {
            g->vibM = 0.0;
            g->oldfpd = 0.4294967295 + (g->fpdL * 0.0000000000618);
        }
        g->aML[g->countM] = (float)(inputSampleL * attenuate);
        g->aMR[g->countM] = (float)(inputSampleR * attenuate);
        g->countM++; if (g->countM < 0 || g->countM > delayM) g->countM = 0;
        double offsetML = (sin(g->vibM) + 1.0) * 127;
        double offsetMR = (sin(g->vibM + (AW_PI / 2.0)) + 1.0) * 127;
        int workingML = g->countM + offsetML, workingMR = g->countM + offsetMR;
        double fl = offsetML - floor(offsetML), fr = offsetMR - floor(offsetMR);
        double interpolML = (g->aML[workingML - ((workingML > delayM) ? delayM + 1 : 0)] * (1 - fl));
        interpolML += (g->aML[workingML + 1 - ((workingML + 1 > delayM) ? delayM + 1 : 0)] * fl);
        double interpolMR = (g->aMR[workingMR - ((workingMR > delayM) ? delayM + 1 : 0)] * (1 - fr));
        interpolMR += (g->aMR[workingMR + 1 - ((workingMR + 1 > delayM) ? delayM + 1 : 0)] * fr);
        inputSampleL = interpolML;   /* the pre-delay that applies vibrato */
        inputSampleR = interpolMR;
        g->iirAL = (g->iirAL * (1.0 - lowpass)) + (inputSampleL * lowpass); inputSampleL = g->iirAL;
        g->iirAR = (g->iirAR * (1.0 - lowpass)) + (inputSampleR * lowpass); inputSampleR = g->iirAR;
        /* first block: four lines per side, each fed by the other side's feedback; then four outputs */
        g->aIL[g->countI] = (float)(inputSampleL + (g->feedbackAR * regen));
        g->aJL[g->countJ] = (float)(inputSampleL + (g->feedbackBR * regen));
        g->aKL[g->countK] = (float)(inputSampleL + (g->feedbackCR * regen));
        g->aLL[g->countL] = (float)(inputSampleL + (g->feedbackDR * regen));
        g->aIR[g->countI] = (float)(inputSampleR + (g->feedbackAL * regen));
        g->aJR[g->countJ] = (float)(inputSampleR + (g->feedbackBL * regen));
        g->aKR[g->countK] = (float)(inputSampleR + (g->feedbackCL * regen));
        g->aLR[g->countL] = (float)(inputSampleR + (g->feedbackDL * regen));
        g->countI = aw_step(g->countI, delayI); g->countJ = aw_step(g->countJ, delayJ);
        g->countK = aw_step(g->countK, delayK); g->countL = aw_step(g->countL, delayL);
        double outIL = g->aIL[g->countI], outJL = g->aJL[g->countJ], outKL = g->aKL[g->countK], outLL = g->aLL[g->countL];
        double outIR = g->aIR[g->countI], outJR = g->aJR[g->countJ], outKR = g->aKR[g->countK], outLR = g->aLR[g->countL];
        /* second block: four more outputs */
        g->aAL[g->countA] = (float)(outIL - (outJL + outKL + outLL));
        g->aBL[g->countB] = (float)(outJL - (outIL + outKL + outLL));
        g->aCL[g->countC] = (float)(outKL - (outIL + outJL + outLL));
        g->aDL[g->countD] = (float)(outLL - (outIL + outJL + outKL));
        g->aAR[g->countA] = (float)(outIR - (outJR + outKR + outLR));
        g->aBR[g->countB] = (float)(outJR - (outIR + outKR + outLR));
        g->aCR[g->countC] = (float)(outKR - (outIR + outJR + outLR));
        g->aDR[g->countD] = (float)(outLR - (outIR + outJR + outKR));
        g->countA = aw_step(g->countA, delayA); g->countB = aw_step(g->countB, delayB);
        g->countC = aw_step(g->countC, delayC); g->countD = aw_step(g->countD, delayD);
        double outAL = g->aAL[g->countA], outBL = g->aBL[g->countB], outCL = g->aCL[g->countC], outDL = g->aDL[g->countD];
        double outAR = g->aAR[g->countA], outBR = g->aBR[g->countB], outCR = g->aCR[g->countC], outDR = g->aDR[g->countD];
        /* third block: the final outputs */
        g->aEL[g->countE] = (float)(outAL - (outBL + outCL + outDL));
        g->aFL[g->countF] = (float)(outBL - (outAL + outCL + outDL));
        g->aGL[g->countG] = (float)(outCL - (outAL + outBL + outDL));
        g->aHL[g->countH] = (float)(outDL - (outAL + outBL + outCL));
        g->aER[g->countE] = (float)(outAR - (outBR + outCR + outDR));
        g->aFR[g->countF] = (float)(outBR - (outAR + outCR + outDR));
        g->aGR[g->countG] = (float)(outCR - (outAR + outBR + outDR));
        g->aHR[g->countH] = (float)(outDR - (outAR + outBR + outCR));
        g->countE = aw_step(g->countE, delayE); g->countF = aw_step(g->countF, delayF);
        g->countG = aw_step(g->countG, delayG); g->countH = aw_step(g->countH, delayH);
        double outEL = g->aEL[g->countE], outFL = g->aFL[g->countF], outGL = g->aGL[g->countG], outHL = g->aHL[g->countH];
        double outER = g->aER[g->countE], outFR = g->aFR[g->countF], outGR = g->aGR[g->countG], outHR = g->aHR[g->countH];
        /* fed back into the input again, a bit */
        g->feedbackAL = (outEL - (outFL + outGL + outHL));
        g->feedbackBL = (outFL - (outEL + outGL + outHL));
        g->feedbackCL = (outGL - (outEL + outFL + outHL));
        g->feedbackDL = (outHL - (outEL + outFL + outGL));
        g->feedbackAR = (outER - (outFR + outGR + outHR));
        g->feedbackBR = (outFR - (outER + outGR + outHR));
        g->feedbackCR = (outGR - (outER + outFR + outHR));
        g->feedbackDR = (outHR - (outER + outFR + outGR));
        inputSampleL = (outEL + outFL + outGL + outHL) / 8.0;   /* the final combined sum of outputs */
        inputSampleR = (outER + outFR + outGR + outHR) / 8.0;
        g->iirBL = (g->iirBL * (1.0 - lowpass)) + (inputSampleL * lowpass); inputSampleL = g->iirBL;
        g->iirBR = (g->iirBR * (1.0 - lowpass)) + (inputSampleR * lowpass); inputSampleR = g->iirBR;
        if (wet < 1.0) {
            inputSampleL = (inputSampleL * wet) + (drySampleL * (1.0 - wet));
            inputSampleR = (inputSampleR * wet) + (drySampleR * (1.0 - wet));
        }
        aw_fpd_next(&g->fpdL);
        aw_fpd_next(&g->fpdR);
        out1[i] = (float)inputSampleL;
        out2[i] = (float)inputSampleR;
    }
}
