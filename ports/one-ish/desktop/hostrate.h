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
 * old path when quantised the engine's way. Nothing on the audio thread allocates or locks beyond the wrapper's own lock. */
#pragma once
#include <stdint.h>
#include "engine.h"

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
/* one host block of n frames into out[0], out[1] (added to them when add is set); `render` is the wrapper's own
 * processReplacing */
void hr_process(hostrate_t *h, struct AEffect *fx, hr_render_t render, float **out, int32_t n, int add);
/* load_test only (effVendorSpecific TEST_LEGACY): 1 = the old path (the wrapper's own int16 output at 44.1 kHz, MIDI
 * at the block start), to compare against; set before the first block */
void hr_set_legacy(hostrate_t *h, int on);
#define TEST_LEGACY 0x4F31534C
