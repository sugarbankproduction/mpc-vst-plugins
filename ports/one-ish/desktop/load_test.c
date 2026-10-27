/* Load the desktop ONE-ish.dll the way a DAW does (LoadLibrary + VSTPluginMain), play a note and save/restore its
 * state. Proves the DLL's export, MIDI path and audio path on Windows itself:
 *   load_test.exe path\to\ONE-ish.dll */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(AEffect *, float **, float **, int32_t);
    void (*setParameter)(AEffect *, int32_t, float);
    float (*getParameter)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect *, float **, float **, int32_t);
    void (*processDoubleReplacing)(AEffect *, double **, double **, int32_t);
    char future[56];
};
typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    unsigned char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
} VstMidiEvent;
typedef struct { int32_t numEvents; intptr_t reserved; void *events[2]; } VstEvents;

static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)i; (void)v; (void)p; (void)o;
    return op == 1 ? 2400 : 0;   /* audioMasterVersion */
}

static int failures;
static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static void send_midi(AEffect *fx, unsigned char status, unsigned char note, unsigned char vel) {
    VstMidiEvent ev = {.type = 1, .byteSize = sizeof ev, .midiData = {status, note, vel}};
    VstEvents list = {.numEvents = 1, .events = {&ev}};
    fx->dispatcher(fx, 25 /* effProcessEvents */, 0, 0, &list, 0);
}

static double play(AEffect *fx, int blocks, int frames) {   /* rms of `blocks` host blocks of `frames` */
    float l[1024], r[1024], *out[2] = {l, r};
    double e = 0;
    for (int b = 0; b < blocks; b++) {
        fx->processReplacing(fx, NULL, out, frames);
        for (int i = 0; i < frames; i++) e += l[i] * l[i];
    }
    return sqrt(e / (blocks * frames));
}

int main(int argc, char **argv) {
    HMODULE dll = LoadLibraryA(argc > 1 ? argv[1] : "ONE-ish.dll");
    if (!dll) { printf("FAIL LoadLibrary (error %lu)\n", GetLastError()); return 1; }
    AEffect *(*entry)(audioMasterCallback) = (AEffect * (*)(audioMasterCallback))(void *)GetProcAddress(dll, "VSTPluginMain");
    check(entry != NULL, "the DLL exports VSTPluginMain");
    if (!entry) return 1;
    AEffect *fx = entry(host);
    check(fx && fx->magic == 0x56737450, "VSTPluginMain returns a VST2 plugin");
    if (!fx) return 1;
    char name[256] = "";
    fx->dispatcher(fx, 45 /* effGetEffectName */, 0, 0, name, 0);
    printf("     name \"%s\", %d params, synth flag %d\n", name, fx->numParams, !!(fx->flags & (1 << 8)));
    fx->dispatcher(fx, 0 /* effOpen */, 0, 0, NULL, 0);
    fx->dispatcher(fx, 10 /* effSetSampleRate */, 0, 0, NULL, 44100.0f);
    fx->dispatcher(fx, 11 /* effSetBlockSize */, 0, 512, NULL, 0);
    fx->dispatcher(fx, 12 /* effMainsChanged */, 0, 1, NULL, 0);

    check(play(fx, 20, 512) == 0, "silent before any note");
    send_midi(fx, 0x90, 60, 100);
    double rms = play(fx, 40, 512);   /* a DAW-sized block, not MPC's 128 */
    char what[96];
    snprintf(what, sizeof what, "note 60 sounds in 512-frame blocks (rms %.3f)", rms);
    check(rms > 0.01, what);
    send_midi(fx, 0x80, 60, 0);
    play(fx, 200, 512);
    check(play(fx, 4, 512) == 0, "silent again after the release");

    void *chunk = NULL;
    intptr_t n = fx->dispatcher(fx, 23 /* effGetChunk */, 0, 0, &chunk, 0);
    snprintf(what, sizeof what, "saves its settings (%ld bytes)", (long)n);
    check(n > 0 && chunk, what);
    float before = fx->getParameter(fx, 1);
    fx->setParameter(fx, 1, before > 0.5f ? 0.1f : 0.9f);
    if (n > 0) fx->dispatcher(fx, 24 /* effSetChunk */, 0, n, chunk, 0);
    check(fabsf(fx->getParameter(fx, 1) - before) < 1e-3f, "restores them");

    fx->dispatcher(fx, 12, 0, 0, NULL, 0);
    fx->dispatcher(fx, 1 /* effClose */, 0, 0, NULL, 0);
    FreeLibrary(dll);
    printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures != 0;
}
