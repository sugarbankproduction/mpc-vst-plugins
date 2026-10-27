/* Load the desktop ONE-ish.dll the way a DAW does (LoadLibrary + VSTPluginMain), play a note, save/restore its
 * state, then play every preset in its program list. Proves the DLL's export, MIDI path, audio path and presets on
 * Windows itself; with a second path, writes all the presets to one WAV to listen to:
 *   load_test.exe path\to\ONE-ish.dll [presets.wav] */
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

enum { SR = 44100, BLOCK = 512, PHRASE_BLOCKS = 8 * SR / BLOCK / 2, MAX_PRESETS = 64 };   /* 4 s per preset */

/* every preset through the DLL's program list: a short line then a chord. Each must sound and stay off the limiter's
 * ceiling. With a path, all of them go into one WAV to listen to, in program order. */
static void audition(AEffect *fx, const char *wav) {
    static int16_t pcm[MAX_PRESETS * PHRASE_BLOCKS * BLOCK * 2];
    static const unsigned char line[] = {48, 55, 60, 67}, chord[] = {48, 60, 64, 67};
    float l[BLOCK], r[BLOCK], *out[2] = {l, r};
    int np = fx->numPrograms < MAX_PRESETS ? fx->numPrograms : MAX_PRESETS, frames = 0;
    char what[160];
    snprintf(what, sizeof what, "%d presets in the program list", fx->numPrograms);
    check(fx->numPrograms > 1, what);
    for (int p = 0; p < np; p++) {
        char name[64] = "";
        fx->dispatcher(fx, 2 /* effSetProgram */, 0, p, NULL, 0);
        fx->dispatcher(fx, 5 /* effGetProgramName */, 0, 0, name, 0);
        double e = 0, peak = 0;
        for (int b = 0; b < PHRASE_BLOCKS; b++) {
            int t = b * BLOCK * 10 / SR;   /* tenths of a second: notes at 0, 0.4, 0.8, 1.2 s, chord 1.8-3 s */
            if (b * BLOCK % (SR * 4 / 10) < BLOCK && t < 16) {
                int k = t / 4;
                if (k > 0) send_midi(fx, 0x80, line[k - 1], 0);
                if (k < 4) send_midi(fx, 0x90, line[k], 100);
            }
            if (t == 18 && (b - 1) * BLOCK * 10 / SR < 18)
                for (int k = 0; k < 4; k++) send_midi(fx, 0x90, chord[k], 100);
            if (t == 30 && (b - 1) * BLOCK * 10 / SR < 30)
                for (int k = 0; k < 4; k++) send_midi(fx, 0x80, chord[k], 0);
            fx->processReplacing(fx, NULL, out, BLOCK);
            for (int i = 0; i < BLOCK; i++) {
                e += l[i] * l[i];
                if (fabsf(l[i]) > peak) peak = fabsf(l[i]);
                pcm[2 * frames] = (int16_t)(l[i] * 32767.0f);
                pcm[2 * frames + 1] = (int16_t)(r[i] * 32767.0f);
                frames++;
            }
        }
        send_midi(fx, 0xb0, 123, 0);   /* all notes off between presets */
        double rms = sqrt(e / (PHRASE_BLOCKS * BLOCK));
        snprintf(what, sizeof what, "preset %2d \"%s\": rms %.3f, peak %.2f", p, name, rms, peak);
        check(rms > 0.005 && peak < 0.999, what);
    }
    if (!wav) return;
    FILE *f = fopen(wav, "wb");
    if (!f) { printf("FAIL cannot write %s\n", wav); failures++; return; }
    uint32_t bytes = (uint32_t)frames * 4, v32;
    uint16_t v16;
    fwrite("RIFF", 1, 4, f); v32 = 36 + bytes; fwrite(&v32, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v32 = 16; fwrite(&v32, 4, 1, f); v16 = 1; fwrite(&v16, 2, 1, f); v16 = 2; fwrite(&v16, 2, 1, f);
    v32 = SR; fwrite(&v32, 4, 1, f); v32 = SR * 4; fwrite(&v32, 4, 1, f); v16 = 4; fwrite(&v16, 2, 1, f);
    v16 = 16; fwrite(&v16, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(pcm, 4, frames, f);
    fclose(f);
    printf("     wrote %s (%d presets, %.0f s)\n", wav, np, frames / (double)SR);
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

    audition(fx, argc > 2 ? argv[2] : NULL);

    fx->dispatcher(fx, 12, 0, 0, NULL, 0);
    fx->dispatcher(fx, 1 /* effClose */, 0, 0, NULL, 0);
    FreeLibrary(dll);
    printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures != 0;
}
