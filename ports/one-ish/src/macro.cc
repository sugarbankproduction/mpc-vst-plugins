// The MACRO channel's Plaits wrapper (see macro.h). Built as C++11 without exceptions or RTTI, like the vendored code.
// It does what Plaits' own Voice::Render does for one engine, minus the hardware: no user data from flash (the 6-op FM
// models use the three factory banks in resources.cc), no 1 ms trigger delay, no decay envelope or low-pass gate (the
// voice's own envelopes shape the sound), float output instead of 16-bit.
#include <cmath>
#include <new>
#include <stdlib.h>
#include <string.h>

#include "macro.h"

#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/engine2/chiptune_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"
#include "plaits/resources.h"
#include "stmlib/dsp/limiter.h"
#include "stmlib/utils/buffer_allocator.h"

using namespace plaits;

namespace {

const int kBlock = static_cast<int>(kBlockSize);   // 12 samples
const size_t kSharedBytes = 16384;                // the RAM the engines share, as on the module
// Plaits' output scale (1.0: its 16-bit full scale) to ONE-ish's (an oscillator at full level peaks near 1)
const float kOutScale = 1.0f;

// how each model is played. trigger: the trigger input is "patched" (a rising edge on every note-on, high while the
// note is held): the drums, string, modal, 6-op FM and speech need it to sound or to say words; the other models run
// free (unpatched), as continuous oscillators. Gains: Plaits' own (voice.cc), a negative one meaning "through the
// limiter at that gain". bank: the 6-op FM factory bank, else -1.
struct Model {
  const char* name;
  bool trigger;
  float out_gain, aux_gain;
  int bank;
};

const Model kModels[MACRO_MODELS] = {
  {"VA", false, 0.8f, 0.8f, -1},
  {"Waveshaper", false, 0.7f, 0.6f, -1},
  {"2-Op FM", false, 0.6f, 0.6f, -1},
  {"Formant", false, 0.7f, 0.6f, -1},
  {"Additive", false, 0.8f, 0.8f, -1},
  {"Wavetable", false, 0.6f, 0.6f, -1},
  {"Chords", false, 0.8f, 0.8f, -1},
  {"Speech", true, -0.7f, 0.8f, -1},
  {"Swarm", false, -3.0f, 1.0f, -1},
  {"Noise Filter", false, -1.0f, -1.0f, -1},
  {"Particles", false, -2.0f, 1.0f, -1},
  {"String", true, -1.0f, 0.8f, -1},
  {"Modal", true, -1.0f, 0.8f, -1},
  {"Bass Drum", true, 0.8f, 0.8f, -1},
  {"Snare", true, 0.8f, 0.8f, -1},
  {"Hi-Hat", true, 0.8f, 0.8f, -1},
  {"VA Filter", false, 1.0f, 1.0f, -1},
  {"Phase Dist", false, 0.7f, 0.7f, -1},
  {"6-Op FM 1", true, 1.0f, 1.0f, 0},
  {"6-Op FM 2", true, 1.0f, 1.0f, 1},
  {"6-Op FM 3", true, 1.0f, 1.0f, 2},
  {"Wave Terrain", false, 0.7f, 0.7f, -1},
  {"String Machine", false, 0.8f, 0.8f, -1},
  {"Chiptune", false, 0.5f, 0.5f, -1},
};

}  // namespace

struct macro_s {
  VirtualAnalogEngine virtual_analog;
  WaveshapingEngine waveshaping;
  FMEngine fm;
  GrainEngine grain;
  AdditiveEngine additive;
  WavetableEngine wavetable;
  ChordEngine chord;
  SpeechEngine speech;
  SwarmEngine swarm;
  NoiseEngine noise;
  ParticleEngine particle;
  StringEngine string;
  ModalEngine modal;
  BassDrumEngine bass_drum;
  SnareDrumEngine snare_drum;
  HiHatEngine hi_hat;
  VirtualAnalogVCFEngine virtual_analog_vcf;
  PhaseDistortionEngine phase_distortion;
  SixOpEngine six_op;
  WaveTerrainEngine wave_terrain;
  StringMachineEngine string_machine;
  ChiptuneEngine chiptune;

  Engine* engine[MACRO_MODELS];   // in kModels order (the three 6-op FM models share one engine)
  stmlib::Limiter out_limiter, aux_limiter;
  float shared[kSharedBytes / sizeof(float)];   // the engines' shared RAM (float: aligned for every type they use)

  int model, active;              // the model asked for; the one the engine state belongs to (-1: none yet)
  float note, harmonics, timbre, morph, aux, aux_prev;
  bool trig, gate;
  float out[kMaxBlockSize], aux_out[kMaxBlockSize], buf[kMaxBlockSize];
  int pos;                        // samples of buf already handed out (kBlock: empty)

  void Init() {
    Engine* e[MACRO_MODELS] = {
      &virtual_analog, &waveshaping, &fm, &grain, &additive, &wavetable, &chord, &speech,
      &swarm, &noise, &particle, &string, &modal, &bass_drum, &snare_drum, &hi_hat,
      &virtual_analog_vcf, &phase_distortion, &six_op, &six_op, &six_op, &wave_terrain, &string_machine, &chiptune,
    };
    stmlib::BufferAllocator allocator(shared, sizeof shared);
    for (int i = 0; i < MACRO_MODELS; ++i) {
      engine[i] = e[i];
      engine[i]->post_processing_settings.out_gain = kModels[i].out_gain;
      engine[i]->post_processing_settings.aux_gain = kModels[i].aux_gain;
      engine[i]->post_processing_settings.already_enveloped = false;
      allocator.Free();   // every engine gets the same RAM, as on the module: a model change resets the new one
      engine[i]->Init(&allocator);
    }
    out_limiter.Init();
    aux_limiter.Init();
    model = 0;
    active = -1;
    note = 60.0f;
    harmonics = timbre = morph = 0.5f;
    aux = aux_prev = 0.0f;
    trig = gate = false;
    pos = kBlock;
  }

  void RenderBlock() {
    if (model != active) {   // a new model (or a healed one): its state and the shared RAM start afresh
      // The engines share `shared`, and some keep state there that their Reset() leaves alone (6-op FM's voice
      // accumulator, for one): what the last model left would be read as floats, NaN included. So the RAM is cleared
      // and the new engine laid out in it again (Init takes its buffers from the allocator: no allocation here).
      const Model& m = kModels[model];
      memset(shared, 0, sizeof shared);
      stmlib::BufferAllocator allocator(shared, sizeof shared);
      engine[model]->Init(&allocator);
      engine[model]->LoadUserData(m.bank >= 0 ? fm_patches_table[m.bank] : NULL);
      engine[model]->Reset();
      out_limiter.Init();
      aux_limiter.Init();
      active = model;
    }
    const Model& m = kModels[active];
    Engine* e = engine[active];
    EngineParameters p;
    p.trigger = m.trigger ? (trig ? TRIGGER_RISING_EDGE : TRIGGER_LOW) | (gate || trig ? TRIGGER_HIGH : TRIGGER_LOW)
                          : TRIGGER_UNPATCHED;
    trig = false;
    p.note = note;
    p.harmonics = harmonics;
    p.timbre = timbre;
    p.morph = morph;
    p.accent = 0.8f;   // the module's level with its LEVEL input unpatched
    bool enveloped = false;
    e->Render(p, out, aux_out, kBlock, &enveloped);
    // a non-finite sample never leaves here (nor reaches the limiters, whose state it would poison): it is 0, and the
    // engine starts afresh at the next block
    bool bad = false;
    for (int i = 0; i < kBlock; ++i) {
      if (!std::isfinite(out[i])) { out[i] = 0.0f; bad = true; }
      if (!std::isfinite(aux_out[i])) { aux_out[i] = 0.0f; bad = true; }
    }
    if (bad) active = -1;
    Post(m.out_gain, &out_limiter, out);
    Post(m.aux_gain, &aux_limiter, aux_out);
    // the main/aux blend glides across the block; then the module's own ceiling (its DAC clips at 16-bit full scale):
    // the integrated wavetables' 1/f gain, a sudden jump far down in pitch or an extreme setting can overshoot it
    float a = aux_prev, da = (aux - aux_prev) / kBlock;
    for (int i = 0; i < kBlock; ++i, a += da) {
      float y = out[i] + (aux_out[i] - out[i]) * a;
      CONSTRAIN(y, -1.0f, 1.0f);
      buf[i] = y * kOutScale;
    }
    aux_prev = aux;
    pos = 0;
  }

  // ChannelPostProcessor (voice.h) in float: a negative gain runs the limiter at that gain, else a plain gain
  static void Post(float gain, stmlib::Limiter* limiter, float* x) {
    if (gain < 0.0f) {
      limiter->Process(-gain, x, kBlock);
      return;
    }
    for (int i = 0; i < kBlock; ++i) x[i] *= gain;
  }
};

extern "C" {

macro_t* macro_create(void) {
  void* mem = malloc(sizeof(macro_s));
  if (!mem) return NULL;
  memset(mem, 0, sizeof(macro_s));
  macro_t* m = new (mem) macro_s;
  m->Init();
  return m;
}

void macro_destroy(macro_t* m) {
  if (!m) return;
  m->~macro_s();
  free(m);
}

void macro_set(macro_t* m, int model, float note, float harmonics, float timbre, float morph, float aux) {
  m->model = model < 0 ? 0 : model >= MACRO_MODELS ? MACRO_MODELS - 1 : model;
  CONSTRAIN(note, -119.0f, 120.0f);   // Plaits' own range
  m->note = note;
  CONSTRAIN(harmonics, 0.0f, 1.0f);
  CONSTRAIN(timbre, 0.0f, 1.0f);
  CONSTRAIN(morph, 0.0f, 1.0f);
  CONSTRAIN(aux, 0.0f, 1.0f);
  m->harmonics = harmonics;
  m->timbre = timbre;
  m->morph = morph;
  m->aux = aux;
}

void macro_trigger(macro_t* m) { m->trig = true; }

void macro_gate(macro_t* m, int gate) { m->gate = gate != 0; }

void macro_render(macro_t* m, float* out, int n) {
  while (n > 0) {
    if (m->pos >= kBlock) m->RenderBlock();
    int k = kBlock - m->pos;
    if (k > n) k = n;
    memcpy(out, m->buf + m->pos, sizeof(float) * k);
    m->pos += k;
    out += k;
    n -= k;
  }
}

const char* macro_model_name(int model) {
  return model >= 0 && model < MACRO_MODELS ? kModels[model].name : "";
}

}  // extern "C"
