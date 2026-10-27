// ONE-ish: Rings and Clouds as synth effect types (see mi_effects.h). The modules themselves are Mutable Instruments'
// code, unmodified, in third_party/ (Copyright 2014-2015 Emilie Gillet, MIT licence; notices in
// third_party/THIRD_PARTY.md). This file only feeds them: fixed-size blocks through a FIFO, the knobs into their
// parameter structs, and the sample-rate corrections below. Built with -DTEST (stmlib's portable, non-ARM-asm paths).
//
// Sample rate: both modules run at 44.1 kHz rather than their own rates, with no resampling (no extra CPU, no
// resampling filters in the signal path):
//   Rings is written for 48 kHz: its frequencies are fractions of the sample rate, so at 44.1 kHz everything sits
//   12 x log2(48000 / 44100) = 1.468 semitones low and decays 8.8% slower. The pitch is put right by adding that offset
//   to Rings' tonic; the slightly longer decays and 8% lower filter corners are left as they are.
//   Clouds is written for 32 kHz: pitch shifting is a ratio and sounds the same; times measured in samples get
//   shorter. Its recording buffers are made larger than the module's (77568 bytes per channel instead of 65408, as
//   large as its fixed-point grain positions allow), so it holds 0.88 s in 16-bit stereo (the module 1.02 s); in
//   Granular mode the SIZE knob is offset so the shortest grain is the module's 32 ms (the longest is 0.37 s instead of
//   0.51 s). The reverb, diffuser and filters are 38% "smaller" in time, which is not obvious by ear.
#include "mi_effects.h"

#include <new>
#include <cmath>
#include <cstring>

#include "rings/dsp/part.h"
#include "clouds/dsp/granular_processor.h"

namespace {

const float kRingsTonic = 1.4680239f;   // 12 * log2(48000 / 44100): Rings' pitch made right at 44.1 kHz
const size_t kRingsBlock = 24;           // rings::kMaxBlockSize: its control rate is set for this block length

struct RingsFx {
  rings::Part part;
  uint16_t reverb[32768];                // the String and Reverb model's reverb memory (as the module's)
  float in[kRingsBlock], out_l[kRingsBlock], out_r[kRingsBlock];   // FIFO: input gathered, output being played
  size_t pos;
  int strum;                             // a strum waiting for the next block
};

const size_t kCloudsBlock = 32;          // clouds::kMaxBlockSize
// The module's buffers are 65408 bytes (CCM) and 118784 (the sample memory plus a 53376-byte FX workspace). Here the
// small one is 1.19x larger, the large one by the same number of bytes, so the workspace stays as it was: as large as
// they can go, because a grain's read position is 16.16 fixed point over up to a quarter of the buffer, so 8-bit mono
// (one byte a sample, the whole large buffer) must stay under 131072 bytes (the module has 118784).
const size_t kCloudsSmall = 77568;
const size_t kCloudsLarge = kCloudsSmall + (118784 - 65408);   // 130944
const float kCloudsMax = 0.9999f;        // the knobs' top (see mi_clouds_process)

struct CloudsFx {
  clouds::GranularProcessor proc;
  uint8_t large[kCloudsLarge];
  uint8_t small[kCloudsSmall];
  clouds::ShortFrame in[kCloudsBlock], out[kCloudsBlock];
  size_t pos;
};

static_assert(sizeof(RingsFx) <= MI_MEM_BYTES && sizeof(CloudsFx) <= MI_MEM_BYTES, "fits the memory lent");
static_assert(kCloudsLarge < 131072, "Clouds' grain positions (16.16 over a quarter of the buffer) must not overflow");
static_assert(alignof(RingsFx) <= 8 && alignof(CloudsFx) <= 8, "the memory lent is 8-byte aligned");

inline float Clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

inline short ToShort(float x) {
  x = Clamp(x, -1.0f, 32767.0f / 32768.0f);
  return static_cast<short>(lrintf(x * 32768.0f));
}

}  // namespace

extern "C" {

size_t mi_rings_bytes(void) { return sizeof(RingsFx); }

void mi_rings_init(void *mem) {
  RingsFx *r = new (mem) RingsFx;   // the memory is zeroed by the caller: members Init() skips start at 0
  r->part.Init(r->reverb);
  r->part.set_polyphony(1);
  r->pos = 0;
  r->strum = 0;
}

void mi_rings_process(void *mem, const mi_rings_in_t *p, const float *inL, const float *inR, float *outL, float *outR,
                      int n) {
  RingsFx *r = static_cast<RingsFx *>(mem);
  if (p->strum) r->strum = 1;
  for (int i = 0; i < n; i++) {
    outL[i] = r->out_l[r->pos];
    outR[i] = r->out_r[r->pos];
    r->in[r->pos] = 0.5f * (inL[i] + inR[i]);
    if (++r->pos < kRingsBlock) continue;
    r->pos = 0;
    int model = p->model < 0 || p->model >= MI_RINGS_MODELS ? 0 : p->model;
    r->part.set_model(static_cast<rings::ResonatorModel>(model));
    rings::PerformanceState ps;
    ps.strum = r->strum != 0;
    ps.internal_exciter = false;   // the voices excite it
    ps.internal_strum = false;
    ps.internal_note = false;
    ps.tonic = kRingsTonic;
    ps.note = Clamp(p->note, 0.0f, 120.0f);
    ps.fm = 0.0f;
    ps.chord = static_cast<int32_t>(Clamp(p->structure, 0.0f, 1.0f) * (rings::kNumChords - 1) + 0.5f);   // as the module
    rings::Patch patch;
    // just under 1, as the module's structure (cv_scaler.cc): at exactly 1 its table lookups (e.g. damping into
    // lut_4_decades) read one entry past the end
    patch.structure = Clamp(p->structure, 0.0f, 0.9995f);
    patch.brightness = Clamp(p->brightness, 0.0f, 0.9995f);
    patch.damping = Clamp(p->damping, 0.0f, 0.9995f);
    patch.position = Clamp(p->position, 0.0f, 0.9995f);
    r->part.Process(ps, patch, r->in, r->out_l, r->out_r, kRingsBlock);
    r->strum = 0;
  }
}

size_t mi_clouds_bytes(void) { return sizeof(CloudsFx); }

void mi_clouds_init(void *mem) {
  CloudsFx *c = new (mem) CloudsFx;
  c->proc.Init(c->large, kCloudsLarge, c->small, kCloudsSmall);
  c->proc.set_playback_mode(clouds::PLAYBACK_MODE_GRANULAR);
  c->proc.set_quality(0);
  clouds::Parameters *q = c->proc.mutable_parameters();
  std::memset(q, 0, sizeof *q);
  q->dry_wet = kCloudsMax;
  c->proc.set_silence(false);
  c->proc.set_bypass(false);
  c->proc.Prepare();
  c->pos = 0;
}

void mi_clouds_process(void *mem, const mi_clouds_in_t *p, const float *inL, const float *inR, float *outL, float *outR,
                       int n) {
  CloudsFx *c = static_cast<CloudsFx *>(mem);
  for (int i = 0; i < n; i++) {
    // Clouds' output is its soft-limited signal at half scale (stmlib's SoftConvert): x2 back to the input's level
    outL[i] = c->out[c->pos].l * (2.0f / 32768.0f);
    outR[i] = c->out[c->pos].r * (2.0f / 32768.0f);
    c->in[c->pos].l = ToShort(inL[i]);
    c->in[c->pos].r = ToShort(inR[i]);
    if (++c->pos < kCloudsBlock) continue;
    c->pos = 0;
    int mode = p->mode < 0 || p->mode >= MI_CLOUDS_MODES ? 0 : p->mode;
    c->proc.set_playback_mode(static_cast<clouds::PlaybackMode>(mode));
    c->proc.set_quality(p->quality & 3);
    clouds::Parameters *q = c->proc.mutable_parameters();
    // the knobs 0..1 as on the module, but just under 1: at exactly 1 its table lookups (grain size, the dry/wet
    // crossfade) read one entry past the end (with a weight of 0, so the sound is the same)
    float size = Clamp(p->size, 0.0f, 1.0f);
    if (mode == MI_CLOUDS_GRANULAR) size = 0.1157f + 0.8843f * size;   // 32 ms grains at the bottom, as at 32 kHz
    q->position = Clamp(p->position, 0.0f, kCloudsMax);
    q->size = Clamp(size, 0.0f, kCloudsMax);
    q->pitch = Clamp(p->pitch, -48.0f, 48.0f);
    q->density = Clamp(p->density, 0.0f, kCloudsMax);
    q->texture = Clamp(p->texture, 0.0f, kCloudsMax);
    q->dry_wet = kCloudsMax;   // full wet (fx.h mixes the dry signal in)
    q->stereo_spread = Clamp(p->spread, 0.0f, kCloudsMax);
    q->feedback = Clamp(p->feedback, 0.0f, kCloudsMax);
    q->reverb = Clamp(p->reverb, 0.0f, kCloudsMax);
    q->freeze = p->freeze != 0;
    q->trigger = false;
    q->gate = false;
    c->proc.Process(c->in, c->out, kCloudsBlock);
    c->proc.Prepare();   // as the module's main loop does between blocks (a mode or quality change takes effect here)
  }
}

}  // extern "C"
