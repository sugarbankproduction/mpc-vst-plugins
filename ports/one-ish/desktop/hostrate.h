/* The desktop plugin at any host sample rate, with float output (desktop only; the MPC build has none of this).
 *
 * The engine keeps running at 44.1 kHz. editor.c's processReplacing hook hands each host block to hr_process(), which
 * has the shared wrapper render the engine one 128-frame block at a time (as on the MPC, so the wrapper's per-block
 * work, its engine lock and its song position all run as before) and converts the result to the host's rate
 * (resample.c). Host MIDI is held back (hr_events) and given to the engine before the engine block nearest its time,
 * instead of all at the start of the host block.
 *
 * Float output: the wrapper is compiled with -Dmpc_engine=one_ish_desktop_engine (desktop/build.sh), so it drives the
 * engine through this file's table, which forwards every call to src/engine.c. Its render() asks the engine for float
 * samples (one_ish_render_float), keeps them for hr_process(), and still gives the wrapper the int16 samples it always
 * had. So every engine call still goes through the wrapper's engine lock, one at a time. The wrapper's song_pos() call
 * is shifted to the engine block's own place on the host's timeline.
 *
 * Timing: engine frame e is heard at host sample e * fs / 44100 + latency, and the latency (the converter's half
 * width; none at 44.1 kHz) is reported to the host as initialDelay, so after the host's delay compensation engine
 * frame e sits on the timeline at e * fs / 44100: the song position each engine block is given matches it.
 * At 44.1 kHz no filter runs: the output is the engine's float samples, which give exactly the int16 samples of the
 * old path when quantised the engine's way. Nothing on the audio thread allocates or locks beyond the wrapper's own lock.
 *
 * Audio input (2026-10-29, desktop only: the MPC build has no input): the plugin declares two inputs (editor.c). Each
 * host block's input is converted the other way, host rate to 44.1 kHz (resample.c's rs_reset_io; a copy at 44.1 kHz),
 * into a FIFO indexed by engine frame, all of it before any output is written. Each engine block takes its 128 frames
 * from there and hands them to the engine (one_ish_set_input, under the wrapper's lock, just before its render; a
 * silent block is not sent). The FIFO starts a lead of silence ahead: an engine block is rendered as soon as the
 * output needs its first frame, so it can want input up to 127 frames past what the host has given, and the lead
 * (128 frames, 130 at other rates for the converters' rounding) means it never waits. So the input is heard
 * lead + the input converter's delay (in engine frames) later, plus the output converter's latency:
 * hr_input_delay() = (lead + rs_delay_io(fs, 44100)) * fs / 44100 + rs_delay_for(fs) host samples (128 at 44.1 kHz,
 * about 222.9 at 48 kHz). The reported latency stays the output's alone, so notes play as before; the input arrives
 * that fixed, exactly known amount later than its place on the timeline (about 3 ms at 44.1 kHz, 4.6 ms at 48 kHz).
 *
 * Multi-out (2026-10-30, desktop only: the MPC build stays stereo): the plugin declares eight outputs (editor.c): MAIN
 * (1-2) and a pair for each Synth (3-4 Synth 1, 5-6 Synth 2, 7-8 Synth 3). The engine renders them together
 * (one_ish_render_multi, src/synths.h): a Synth's OUTPUT (syn_out) puts it on MAIN (the default, as before), on its own
 * pair only, or on both; the shared master effects' returns stay on MAIN. Each pair has its own converter, fed the same
 * engine frames as MAIN's, so all eight outputs have the same latency and line up sample for sample. With every OUTPUT
 * at Main the pairs are silent and outputs 1-2 are exactly what they were, so a host that only listens to the first two
 * hears everything. */
#pragma once
#include <stdint.h>
#include "engine.h"

#define HR_PAIRS 3   /* (2026-10-30) the Synths' own output pairs after MAIN (the engine's ONEISH_SYNTHS at most) */
#define HR_OUTPUTS (2 + 2 * HR_PAIRS)

typedef struct hostrate hostrate_t;
struct AEffect;
typedef void (*hr_render_t)(struct AEffect *, float **, float **, int32_t);

/* the engine table the wrapper sees (vst2_wrap.c, renamed by -D), and its mpc_engine_changed() */
const mpc_engine_t *one_ish_desktop_engine(void);
int one_ish_desktop_changed(void *inst, char *key, int len);

hostrate_t *hr_last_created(void);   /* the instance the wrapper's VSTPluginMain just made on this thread */
int hr_on(const hostrate_t *h);      /* converting (not the old path, see hr_set_legacy) */
/* effSetSampleRate (any thread, normally with processing suspended): applied before the next block. Returns the
 * latency to report, in host samples. Rates outside 8 kHz .. 768 kHz play as 44.1 kHz. */
int hr_set_rate(hostrate_t *h, float rate);
void hr_restart(hostrate_t *h);      /* effMainsChanged on: start clean before the next block */
int hr_latency(const hostrate_t *h);
/* effProcessEvents (audio thread): MIDI queued for its engine block; 1 = taken (0: pass to the wrapper as before) */
int hr_events(hostrate_t *h, struct AEffect *fx, const void *vst_events);
/* one host block of n frames into out[0], out[1] (MAIN) and the Synths' pairs out[2] .. out[nout - 1] (added to them
 * when add is set; nout up to HR_OUTPUTS), the host's audio input in in[0], in[1] (NULL: silence; it may be the same
 * buffers as out); `render` is the wrapper's own processReplacing */
void hr_process(hostrate_t *h, struct AEffect *fx, hr_render_t render, float **in, float **out, int nout, int32_t n,
                int add);
/* the audio input's delay to the output, in host samples (input sample h is heard, through a voice with no delay of
 * its own, at h + this); not part of the reported latency (see the top of this file). -1 on the old path. */
double hr_input_delay(const hostrate_t *h);
long hr_input_dry(const hostrate_t *h);   /* engine frames that found no input in time since the instance began (0) */
/* (2026-10-29) the balls and particles' view ("phys_view", "s2_phys_view" ...) straight from the engine, without the
 * wrapper's lock: only for readouts the engine makes safe from any thread (src/physgen_run.h's seqlock). UI thread */
int hr_peek_view(hostrate_t *h, int synth, char *buf, int n);
/* (2026-10-30) the same for a view by name ("rain_view", "markov_view": src/gens_b_run.h's seqlock). UI thread */
int hr_peek_named(hostrate_t *h, int synth, const char *name, char *buf, int n);
int hr_peek(hostrate_t *h, const char *key, char *buf, int n);   /* any readout of the engine (the tests' "bpm") */
/* (2026-10-30) MIDI OUT: the engine's notes (src/midi_out.h) go to the host from hr_process, with
 * audioMasterProcessEvents through this callback (set once, from VSTPluginMain), each at the host sample its sound is
 * heard at: engine frame e at e * fs / 44100 + the reported latency (at 44.1 kHz exactly e), sorted, in the host block it
 * falls in. hr_midi_out_lost: events that found the queue full (0; load_test reads it) */
void hr_set_host(hostrate_t *h, intptr_t (*host)(struct AEffect *, int32_t, int32_t, intptr_t, void *, float));
long hr_midi_out_lost(const hostrate_t *h);
/* load_test only (effVendorSpecific TEST_LEGACY): 1 = the old path (the wrapper's own int16 output at 44.1 kHz, MIDI
 * at the block start), to compare against; set before the first block */
void hr_set_legacy(hostrate_t *h, int on);
#define TEST_LEGACY 0x4F31534C
/* load_test only (effVendorSpecific TEST_INPUT): returns hr_input_delay() x 1000 (v = 0) or hr_input_dry() (v = 1) */
#define TEST_INPUT 0x4F31494E
