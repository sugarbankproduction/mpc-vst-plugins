/* ONE-ish: two Mutable Instruments modules as synth effect types, behind a small C interface (src/fx/mi_effects.cc).
 *   RINGS  (Emilie Gillet, MIT; third_party/rings): the resonator, excited by the voices ("external exciter" input),
 *          polyphony 1: its odd and even partials (or its two pickups) go left and right.
 *   CLOUDS (Emilie Gillet, MIT; third_party/clouds): the granular texture processor, its four playback modes and four
 *          quality settings, on the voices in stereo.
 * Each runs in the caller's memory (mem, at least the *_bytes() it asks for, 8-byte aligned, zeroed before *_init()):
 * the synth effect slot lends them its delay lines, since only one type runs at a time. *_init() builds the module
 * there (no allocation); *_process() never allocates, locks or touches files.
 * Both modules work in fixed blocks (Rings 24 samples, Clouds 32), so each keeps a small FIFO: any n per call, and its
 * output is that block length late (0.5 / 0.7 ms) against the dry signal.
 * Sample rate: both run at 44.1 kHz on their own constants (Rings is written for 48 kHz, Clouds for 32 kHz); see the
 * .cc for what that changes and how it is made up for. */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MI_MEM_BYTES (2u * 131072u * 4u)   /* what the caller lends: the synth effect's two delay lines (1 MB) */

enum { MI_RINGS_MODAL, MI_RINGS_SYMPATHETIC, MI_RINGS_STRING, MI_RINGS_FM, MI_RINGS_CHORDS, MI_RINGS_STRING_VERB,
       MI_RINGS_MODELS };   /* Rings' six resonator models, in its own order ("modfx_model" options) */
enum { MI_CLOUDS_GRANULAR, MI_CLOUDS_STRETCH, MI_CLOUDS_LOOPING, MI_CLOUDS_SPECTRAL, MI_CLOUDS_MODES };   /* "modfx_gmode" */

typedef struct {
    int model;           /* MI_RINGS_* */
    float note;          /* the pitch to ring at, MIDI note number (60 = middle C), any fraction */
    int strum;           /* a new note started (Rings follows it at once rather than gliding there) */
    float structure, brightness, damping, position;   /* 0..1, as Rings' knobs */
} mi_rings_in_t;

typedef struct {
    int mode;            /* MI_CLOUDS_* */
    int quality;         /* 0: 16-bit stereo, 1: 16-bit mono, 2: 8-bit stereo, 3: 8-bit mono (the last two at half rate) */
    int freeze;
    float position, size, density, texture;   /* 0..1, as Clouds' knobs */
    float pitch;         /* semitones, -48..48 */
    float spread, feedback, reverb;           /* 0..1 (Clouds' BLEND menu); its own dry/wet stays at full wet */
} mi_clouds_in_t;

size_t mi_rings_bytes(void);
void mi_rings_init(void *mem);
/* inL/inR summed to mono as Rings' input; out: its two outputs (out, aux) as left and right */
void mi_rings_process(void *mem, const mi_rings_in_t *p, const float *inL, const float *inR, float *outL, float *outR,
                      int n);

size_t mi_clouds_bytes(void);
void mi_clouds_init(void *mem);
/* stereo in, its wet signal out (level about as the input's) */
void mi_clouds_process(void *mem, const mi_clouds_in_t *p, const float *inL, const float *inR, float *outL, float *outR,
                       int n);

#ifdef __cplusplus
}
#endif
