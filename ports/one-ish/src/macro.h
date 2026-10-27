/* The MACRO channel: Mutable Instruments Plaits (Emilie Gillet, MIT licence; third_party/THIRD_PARTY.md) as one more
 * oscillator per voice, behind a small C API so engine.c stays C. src/macro.cc wraps the Plaits engines directly (not
 * Plaits' own Voice class: no hardware user data, no 1 ms trigger delay, no internal low-pass gate, float output).
 * Plaits renders in blocks of 12 samples; macro_render() hands out any number of samples, rendering a new block whenever
 * the last one is used up (so up to 11 samples sit in the buffer). Plaits runs at 44.1 kHz here: its sample-rate
 * constants come from third_party/patched/plaits/dsp/dsp.h.
 * Memory: macro_create() allocates one voice's state (every model's engine object and the 16 KB they share) in create();
 * nothing else allocates, locks or touches files. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#define MACRO_MODELS 24   /* the "mac_model" options, in params.json order */

typedef struct macro_s macro_t;

macro_t *macro_create(void);       /* NULL when out of memory */
void macro_destroy(macro_t *m);
/* the settings for the next blocks: model (0 .. MACRO_MODELS - 1), pitch as a MIDI note number (69 = 440 Hz, fractions
 * welcome), harmonics / timbre / morph 0..1, aux 0..1 (0: Plaits' main output, 1: its aux output, between: a blend) */
void macro_set(macro_t *m, int model, float note, float harmonics, float timbre, float morph, float aux);
void macro_trigger(macro_t *m);       /* a note-on: the next block sees a rising edge on the trigger input */
void macro_gate(macro_t *m, int gate); /* the key (or pedal) is holding the note: the trigger input stays high */
void macro_render(macro_t *m, float *out, int n);   /* n samples, -1..1 */
const char *macro_model_name(int model);

#ifdef __cplusplus
}
#endif
