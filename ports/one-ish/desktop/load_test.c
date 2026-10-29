/* Load the desktop ONE-ish.dll the way a DAW does (LoadLibrary + VSTPluginMain), play a note, save/restore its
 * state, then play every preset in its program list, then drive the editor window (keyboard, ribbon, the randomiser
 * page: RANDOMISE, UNDO, REDO, MUTATE, locks, and clicks while another thread renders), times its painting (a full
 * repaint, a knob turned, a rate LED flashing: only its own square) and checks it gives back its GDI handles over 20
 * opens; the MORE pages' pictures and the master effect pages by type are pictured and checked. Proves the DLL's
 * export, MIDI path, audio path and presets on Windows itself; with a second path, writes all the presets to one WAV to listen
 * to; with a third, pictures of the editor. User patches (save, load, rename, delete, import, export) are tested in
 * a temporary folder; so are .oneseq sequences, after notes are entered into the step sequencer's page from the window's
 * keyboard and with clicks. Then the host sample rate checks (rate_test.c): other rates and block sizes, float output;
 * with ONEISH_BASELINE_DLL set to an older build, its 44.1 kHz output must match this one's to the 16-bit sample. Last,
 * as a host reloading a rebuilt plugin: closed and unloaded, nothing of it stays registered, and it loads and opens again.
 *   load_test.exe path\to\ONE-ish.dll [presets.wav] [editor.bmp] */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "layout.h"   /* build/desktop/layout.h: where the editor's ribbon, buttons and MORE triangles are */
#include "params.h"   /* build/params.h: the parameters' keys, in VST order */
#include "../src/randomize.h"   /* which lock section each parameter is in */
#define TEST_MIDI_LOG 0x4F314D4C   /* as editor.h: effVendorSpecific index for the MIDI the window passed on */
#define TEST_FOCUS 0x4F314643      /* as editor.h: the Panel Focus */
#include "patches.h"   /* TEST_PATCH and its operations, for the user patch tests */
#include "seq_page.h"   /* TEST_SEQ and its operations, for the step sequencer's page */
#include "settings.h"   /* TEST_SETTINGS and its operations, for the SETTINGS page */
#include "../src/stepseq.h"   /* to read back the sequence the engine holds */

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

double host_tempo;   /* library_test.c sets it: the tempo the host reports (audioMasterGetTime; 0: none) */
static struct { double d[8]; int32_t i[6]; } host_time;
static int automated[NPARAMS], begins, ends;   /* what the plugin told the host per param (audioMasterAutomate, Begin/EndEdit) */
static int automated_n, begun, ended;          /* and in total: audioMasterAutomate / BeginEdit / EndEdit calls */
static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)v; (void)p; (void)o;
    if (op == 0 && i >= 0 && i < NPARAMS) automated[i]++;
    if (op == 0) automated_n++;
    if (op == 43) { begins++; begun++; }
    if (op == 44) { ends++; ended++; }
    if (op == 7 && host_tempo > 0) {   /* audioMasterGetTime: the tempo only (kVstTempoValid) */
        host_time.d[1] = 44100.0;
        host_time.d[4] = host_tempo;
        host_time.i[5] = 1 << 10;
        return (intptr_t)&host_time;
    }
    return op == 1 ? 2400 : 0;   /* audioMasterVersion */
}

int rate_tests(void *entry, void *old_entry);   /* rate_test.c: returns its failures */
int library_tests(void *fx, HWND child, float s, const char *bmp, int w, int h, const char *patch_dir);   /* library_test.c */

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

/* a picture of the editor window as a 24-bit BMP (through WM_PRINTCLIENT, as a host's thumbnail would) */
static void shot(HWND child, int w, int h, const char *bmp, const char *suffix) {
    char path[512];
    const char *dot = strrchr(bmp, '.');
    snprintf(path, sizeof path, "%.*s%s.bmp", dot ? (int)(dot - bmp) : (int)strlen(bmp), bmp, suffix);
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bm = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(dc, bm);
    SendMessageA(child, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
    BITMAPINFOHEADER bi = {sizeof bi, w, h, 1, 24, BI_RGB, 0, 0, 0, 0, 0};
    int stride = (w * 3 + 3) & ~3;
    static unsigned char px[4096 * 2048 * 3];
    if (stride * h <= (int)sizeof px) {
        GetDIBits(dc, bm, 0, h, px, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
        FILE *f = fopen(path, "wb");
        if (f) {
            BITMAPFILEHEADER fh = {0x4d42, (DWORD)(sizeof fh + sizeof bi + stride * h), 0, 0, sizeof fh + sizeof bi};
            fwrite(&fh, sizeof fh, 1, f); fwrite(&bi, sizeof bi, 1, f); fwrite(px, stride * h, 1, f);
            fclose(f);
            printf("     wrote %s\n", path);
        }
    }
    SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc); ReleaseDC(NULL, screen);
}

/* a click at a point given in the layout's base units (s: pixels per base unit) */
static void click(HWND child, float s, float x, float y) {
    LPARAM at = MAKELPARAM((int)lroundf(x * s), (int)lroundf(y * s));
    SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, at);
    SendMessageA(child, WM_LBUTTONUP, 0, at);
}
static void click_zone(HWND child, float s, const short z[4]) { click(child, s, z[0] + z[2] / 2.0f, z[1] + z[3] / 2.0f); }
static int click_more(HWND child, float s, int page) {   /* a module's MORE triangle */
    for (int k = 0; k < NMORES; k++)
        if (MORES[k].page == page && page >= 0) {
            click(child, s, MORES[k].x + MORES[k].w * 0.7f, MORES[k].y + MORES[k].h * 0.3f);
            return 1;
        }
    return 0;
}

/* the ribbon: a drag from left to right must reach the engine as CC 17 = 127 (touch), CC 16 rising (position) and
 * CC 17 = 0 (release), through the window's MIDI queue and the audio thread (the editor keeps a copy of what it passed
 * on, read back with effVendorSpecific / TEST_MIDI_LOG) */
static void ribbon_test(AEffect *fx, HWND child, float s) {
    unsigned char log[64][3];
    play(fx, 2, 512);
    fx->dispatcher(fx, 50, TEST_MIDI_LOG, 0, log, 0);   /* forget what came before */
    float x0 = ZONE_RIBBON[0] + ZONE_RIBBON[2] * 0.1f, x1 = ZONE_RIBBON[0] + ZONE_RIBBON[2] * 0.8f;
    int y = (int)lroundf((ZONE_RIBBON[1] + ZONE_RIBBON[3] / 2.0f) * s);
    SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((int)lroundf(x0 * s), y));
    play(fx, 1, 512);
    for (int k = 1; k <= 10; k++) {
        SendMessageA(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM((int)lroundf((x0 + (x1 - x0) * k / 10) * s), y));
        play(fx, 1, 512);
    }
    SendMessageA(child, WM_LBUTTONUP, 0, MAKELPARAM((int)lroundf(x1 * s), y));
    play(fx, 2, 512);
    int n = (int)fx->dispatcher(fx, 50, TEST_MIDI_LOG, 0, log, 0), on = -1, off = -1, pos = 0, rising = 1, first = -1, last = -1;
    for (int k = 0; k < n; k++) {
        if (log[k][0] != 0xB0) continue;
        if (log[k][1] == 17 && log[k][2] == 127 && on < 0) on = k;
        if (log[k][1] == 17 && log[k][2] == 0) off = k;
        if (log[k][1] == 16) {
            if (last >= 0 && log[k][2] <= last) rising = 0;
            if (first < 0) first = log[k][2];
            last = log[k][2];
            pos++;
        }
    }
    char what[160];
    snprintf(what, sizeof what, "a drag on the ribbon reaches the engine: %d messages, touch at %d, release at %d, "
             "%d positions %d..%d", n, on, off, pos, first, last);
    check(on >= 0 && off > on && pos >= 5 && rising && abs(first - 13) <= 2 && abs(last - 102) <= 2, what);
}

/* ---- the randomiser page ---- */
/* where its controls sit, in base units: mirrors editor.c's layout() (the screen's rows) and rnd_rect() */
enum { RP_GO, RP_MUTATE, RP_UNDO, RP_REDO, RP_ALL, RP_LOCK };
static void rnd_point(int what, int k, float *x, float *y) {
    float x0 = ZONE_SCREEN[0] + 8.0f, x1 = ZONE_SCREEN[0] + ZONE_SCREEN[2] - 8.0f, top = ZONE_SCREEN[1] + 6.0f + 36 + 20 + 18;
    float half = (x1 - x0 - 8) / 2, xr = x0 + half + 8, q = (half - 8) / 2;
    switch (what) {
    case RP_GO: *x = x0 + half / 2; *y = top + 22; break;
    case RP_MUTATE: *x = xr + half / 2; *y = top + 22; break;
    case RP_UNDO: *x = x0 + q / 2; *y = top + 22 * 7 + 11; break;
    case RP_REDO: *x = x0 + q + 8 + q / 2; *y = top + 22 * 7 + 11; break;
    case RP_ALL: *x = xr + half / 2; *y = top + 22 * 7 + 11; break;
    default: *x = (k < 4 ? x0 : xr) + half / 2; *y = top + 22 * (3 + k % 4) + 11; break;
    }
}
static void rnd_click(HWND child, float s, int what, int k) {
    float x, y;
    rnd_point(what, k, &x, &y);
    click(child, s, x, y);
}
static void snapshot(AEffect *fx, float *v) { for (int i = 0; i < NPARAMS; i++) v[i] = fx->getParameter(fx, i); }
static double max_diff(const float *a, const float *b) {
    double m = 0;
    for (int i = 0; i < NPARAMS; i++) if (fabs(a[i] - b[i]) > m) m = fabs(a[i] - b[i]);
    return m;
}
static int count_diff(const float *a, const float *b) {
    int n = 0;
    for (int i = 0; i < NPARAMS; i++) n += a[i] != b[i];
    return n;
}

/* the audio thread for the clicks-while-playing check: a held chord, blocks rendered until told to stop */
static volatile LONG audio_stop;
static volatile LONG audio_blocks, audio_bad;
static DWORD WINAPI audio_thread(LPVOID arg) {
    AEffect *fx = arg;
    float l[256], r[256], *out[2] = {l, r};
    while (!audio_stop) {
        fx->processReplacing(fx, NULL, out, 256);
        for (int i = 0; i < 256; i++)
            if (!isfinite(l[i]) || !isfinite(r[i]) || fabsf(l[i]) > 1.0f || fabsf(r[i]) > 1.0f) audio_bad++;
        audio_blocks++;
    }
    return 0;
}

static void random_test(AEffect *fx, HWND child, float s, const char *bmp, int w, int h) {
    static float before[NPARAMS], after[NPARAMS], now[NPARAMS];
    char what[200];
    click_zone(child, s, ZONE_TAB_RANDOM);
    snapshot(fx, before);
    memset(automated, 0, sizeof automated);
    begins = ends = 0;
    rnd_click(child, s, RP_GO, 0);
    snapshot(fx, after);
    int changed = count_diff(before, after), told = 0, never_moved = 0;
    for (int i = 0; i < NPARAMS; i++) {
        told += after[i] != before[i] && automated[i] > 0;
        never_moved += rnd_section_of(i) == RS_NEVER && after[i] != before[i];
    }
    snprintf(what, sizeof what, "RANDOMISE changes %d params, the host is told of %d of them (%d edits begun, %d ended), "
             "%d never-touched params moved", changed, told, begins, ends, never_moved);
    check(changed > 40 && told == changed && begins == ends && begins >= changed && never_moved == 0, what);
    send_midi(fx, 0x90, 60, 100);
    send_midi(fx, 0x90, 64, 100);
    double rms = play(fx, 60, 512);
    send_midi(fx, 0xB0, 123, 0);
    play(fx, 200, 512);
    snprintf(what, sizeof what, "the random sound plays (rms %.3f)", rms);
    check(rms > 0.002, what);

    rnd_click(child, s, RP_UNDO, 0);
    snapshot(fx, now);
    snprintf(what, sizeof what, "UNDO restores the sound before (largest difference %.2g)", max_diff(now, before));
    check(max_diff(now, before) < 1e-6, what);   /* 1e-6: a value read back can differ in its last float bit */
    rnd_click(child, s, RP_REDO, 0);
    snapshot(fx, now);
    snprintf(what, sizeof what, "REDO brings the random sound back (largest difference %.2g)", max_diff(now, after));
    check(max_diff(now, after) < 1e-6, what);

    rnd_click(child, s, RP_MUTATE, 0);
    snapshot(fx, now);
    int mutated = count_diff(after, now);
    snprintf(what, sizeof what, "MUTATE at 20%% changes %d params (RANDOMISE changed %d)", mutated, changed);
    check(mutated > 0 && mutated < changed, what);

    rnd_click(child, s, RP_LOCK, RS_FILTER);   /* lock FILTERS and ENVELOPES, then RANDOMISE */
    rnd_click(child, s, RP_LOCK, RS_ENV);
    snapshot(fx, before);
    rnd_click(child, s, RP_GO, 0);
    snapshot(fx, after);
    int locked_moved = 0, others = 0;
    for (int i = 0; i < NPARAMS; i++) {
        int sec = rnd_section_of(i);
        if (sec == RS_FILTER || sec == RS_ENV || sec == RS_NEVER) locked_moved += after[i] != before[i];
        else others += after[i] != before[i];
    }
    snprintf(what, sizeof what, "with FILTERS and ENVELOPES locked, RANDOMISE leaves them alone (%d moved) and changes "
             "%d others", locked_moved, others);
    check(locked_moved == 0 && others > 20, what);
    rnd_click(child, s, RP_LOCK, RS_FILTER);
    rnd_click(child, s, RP_LOCK, RS_ENV);

    /* the keys R and M, and ALL 3 SYNTHS: params changed per Synth (by the key's "s2_" / "s3_" prefix) */
    {
        int per[3];
        static float a[NPARAMS], b[NPARAMS];
#define PER_SYNTH(x, y) do { per[0] = per[1] = per[2] = 0;             for (int i = 0; i < NPARAMS; i++) if (x[i] != y[i]) {                 const char *k = PARAMS[i].key;                 per[k[0] == 's' && (k[1] == '2' || k[1] == '3') && k[2] == '_' ? k[1] - '1' : 0]++; } } while (0)
        snapshot(fx, a);
        SendMessageA(child, WM_KEYDOWN, 'R', 1);
        snapshot(fx, b);
        PER_SYNTH(a, b);
        snprintf(what, sizeof what, "the R key randomises the Synth in focus (%d / %d / %d params changed in Synths 1-3)",
                 per[0], per[1], per[2]);
        check(per[0] > 40 && per[1] == 0 && per[2] == 0, what);
        SendMessageA(child, WM_KEYDOWN, 'M', 1);
        snapshot(fx, a);
        PER_SYNTH(b, a);
        snprintf(what, sizeof what, "the M key mutates it (%d / %d / %d)", per[0], per[1], per[2]);
        check(per[0] > 0 && per[1] == 0 && per[2] == 0, what);
        rnd_click(child, s, RP_ALL, 0);   /* ALL 3 SYNTHS on, then RANDOMISE */
        rnd_click(child, s, RP_GO, 0);
        snapshot(fx, b);
        PER_SYNTH(a, b);
        snprintf(what, sizeof what, "with ALL 3 SYNTHS lit, RANDOMISE changes all three (%d / %d / %d)",
                 per[0], per[1], per[2]);
        check(per[0] > 40 && per[1] > 40 && per[2] > 40, what);
        int setup_moved = 0;
        static const char *const SETUP[] = {"syn_on", "s2_syn_on", "s3_syn_on", "syn_ch", "s2_syn_ch", "s3_syn_ch",
                                            "key_lo", "s2_key_lo", "s3_key_hi", "s2_volume", "s3_volume"};
        for (unsigned k = 0; k < sizeof SETUP / sizeof SETUP[0]; k++)
            for (int i = 0; i < NPARAMS; i++)
                if (!strcmp(PARAMS[i].key, SETUP[k])) setup_moved += a[i] != b[i];
        snprintf(what, sizeof what, "... and leaves each Synth's ON, channel, key range and level alone (%d moved)",
                 setup_moved);
        check(setup_moved == 0, what);
        rnd_click(child, s, RP_ALL, 0);   /* (off again) */
        int got[3];   /* the keys 1-3 pick the Synth in focus (TEST_FOCUS: focus + 4 * mask) */
        for (int c = 0; c < 3; c++) {
            SendMessageA(child, WM_KEYDOWN, '1' + (c + 1) % 3, 1);
            got[(c + 1) % 3] = (int)fx->dispatcher(fx, 50, TEST_FOCUS, -1, NULL, 0);
        }
        snprintf(what, sizeof what, "the keys 1 / 2 / 3 pick Synth 1 / 2 / 3 (focus + 4 x mask: %d %d %d)", got[0], got[1], got[2]);
        check(got[0] == 0 + 4 * 1 && got[1] == 1 + 4 * 2 && got[2] == 2 + 4 * 4, what);
        SendMessageA(child, WM_KEYDOWN, '1', 1);   /* (back to Synth 1) */
#undef PER_SYNTH
    }

    /* clicks while another thread renders, as in a host: no crash, nothing but finite audio within full scale */
    send_midi(fx, 0x90, 48, 100);
    send_midi(fx, 0x90, 55, 100);
    send_midi(fx, 0x90, 64, 100);
    audio_stop = audio_blocks = audio_bad = 0;
    HANDLE t = CreateThread(NULL, 0, audio_thread, fx, 0, NULL);
    for (int k = 0; k < 40; k++) {
        rnd_click(child, s, k % 4 == 3 ? RP_UNDO : k % 2 ? RP_MUTATE : RP_GO, 0);
        Sleep(5);
    }
    audio_stop = 1;
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    send_midi(fx, 0xB0, 123, 0);
    snprintf(what, sizeof what, "40 RANDOMISE / MUTATE / UNDO clicks while another thread rendered %ld blocks: %ld bad samples",
             (long)audio_blocks, (long)audio_bad);
    check(audio_blocks > 10 && audio_bad == 0, what);
    play(fx, 200, 512);
    if (bmp) {   /* a picture of the page after one more RANDOMISE, with the mouse over the button */
        rnd_click(child, s, RP_GO, 0);
        rnd_click(child, s, RP_LOCK, RS_FX);
        float x, y;
        rnd_point(RP_GO, 0, &x, &y);
        SendMessageA(child, WM_MOUSEMOVE, 0, MAKELPARAM((int)lroundf(x * s), (int)lroundf(y * s)));
        shot(child, w, h, bmp, "_random");
        rnd_click(child, s, RP_LOCK, RS_FX);
        SendMessageA(child, WM_MOUSEMOVE, 0, MAKELPARAM(0, 0));
    }
}

/* ---- user patches (desktop/patches.c), in a temporary folder (ONEISH_PATCH_DIR) ---- */
static char patch_dir[MAX_PATH];

static intptr_t pt(AEffect *fx, int op, const char *in, char *out, float opt) {   /* a TEST_PATCH operation */
    static char buf[1024];
    snprintf(buf, sizeof buf, "%s", in ? in : "");
    intptr_t r = fx->dispatcher(fx, 50, TEST_PATCH, op, buf, opt);
    if (out) snprintf(out, 1024, "%s", buf);
    return r;
}

static void snap(AEffect *fx, float *v) {
    for (int i = 0; i < fx->numParams; i++) v[i] = fx->getParameter(fx, i);
}
static int differs(AEffect *fx, const float *v) {   /* the first param not exactly as in v, or -1 */
    for (int i = 0; i < fx->numParams; i++)
        if (fx->getParameter(fx, i) != v[i]) return i;
    return -1;
}

static int exists(const char *name) {   /* a patch file in the folder */
    char p[MAX_PATH + 64];
    snprintf(p, sizeof p, "%s\\%s.oneish", patch_dir, name);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(text, 1, strlen(text), f); fclose(f); }
}

static int title_is(AEffect *fx, const char *want, int exact) {   /* the browser's title starts with (or is) want */
    char t[1024];
    pt(fx, PT_TITLE, NULL, t, 0);
    int ok = exact ? !strcmp(t, want) : !strncmp(t, want, strlen(want));
    if (!ok) printf("     title is \"%s\", expected \"%s\"\n", t, want);
    return ok;
}

static void empty_dir(const char *dir) {   /* the test folder's files (and its "out" folder) */
    char pat[MAX_PATH + 8], p[2 * MAX_PATH];
    WIN32_FIND_DATAA f;
    snprintf(pat, sizeof pat, "%s\\*", dir);
    HANDLE h = FindFirstFileA(pat, &f);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(f.cFileName, ".") || !strcmp(f.cFileName, "..")) continue;
        snprintf(p, sizeof p, "%s\\%s", dir, f.cFileName);
        if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { empty_dir(p); RemoveDirectoryA(p); }
        else DeleteFileA(p);
    } while (FindNextFileA(h, &f));
    FindClose(h);
}

static void patch_tests(AEffect *fx) {
    static float saved[4096], other[4096];
    char what[400], buf[1024], path[MAX_PATH + 64];
    int np = fx->numPrograms, d;
    pt(fx, PT_DIR, NULL, buf, 0);
    snprintf(what, sizeof what, "the patch folder follows ONEISH_PATCH_DIR (%s)", buf);
    check(!_stricmp(buf, patch_dir), what);
    check(pt(fx, PT_COUNT, NULL, NULL, 0) == 0, "no user patches in a new folder");

    /* a sound: factory preset 4, edited */
    fx->dispatcher(fx, 2 /* effSetProgram */, 0, 3, NULL, 0);
    check(title_is(fx, "F 004", 0) && !pt(fx, PT_EDITED, NULL, NULL, 0), "factory preset 4 shows as F 004, not edited");
    static const int idx[] = {0, 1, 5, 13, 37, 60, 120, 200, 300, 401};
    static const float val[] = {0.0f, 0.37f, 0.81f, 0.0f, 0.42f, 0.66f, 0.123f, 0.9f, 0.25f, 0.7f};
    for (unsigned k = 0; k < sizeof idx / sizeof idx[0]; k++)
        if (idx[k] < fx->numParams) fx->setParameter(fx, idx[k], val[k]);
    check(pt(fx, PT_EDITED, NULL, NULL, 0) == 1 && title_is(fx, "F 004", 0), "a knob moved: the sound shows as edited");
    pt(fx, PT_TITLE, NULL, buf, 0);
    check(strstr(buf, " *") != NULL, "the edited sound's title ends in *");
    snap(fx, saved);

    /* SAVE AS, then load it back over another sound: every value exactly as saved */
    check(pt(fx, PT_SAVE_AS, "Test Pad", NULL, 0) == PE_OK && exists("Test Pad"), "SAVE AS writes Test Pad.oneish");
    check(title_is(fx, "U  Test Pad", 1) && pt(fx, PT_COUNT, NULL, NULL, 0) == 1, "it is now the current user patch, not edited");
    fx->dispatcher(fx, 2, 0, 0, NULL, 0);
    fx->setParameter(fx, 1, 0.99f);
    check(differs(fx, saved) >= 0, "another sound loaded");
    automated_n = begun = ended = 0;
    check(pt(fx, PT_LOAD, "Test Pad", NULL, 0) == PE_OK, "the saved patch loads");
    d = differs(fx, saved);
    snprintf(what, sizeof what, "every one of the %d params is restored exactly (first difference: %d)", fx->numParams, d);
    check(d < 0, what);
    snprintf(what, sizeof what, "the host hears each change, inside begin/end edit (%d automate, %d begin, %d end)",
             automated_n, begun, ended);
    check(automated_n > 0 && begun == automated_n && ended == automated_n, what);
    check(!pt(fx, PT_EDITED, NULL, NULL, 0) && title_is(fx, "U  Test Pad", 1), "after loading: not edited");

    /* duplicates and names */
    check(pt(fx, PT_SAVE_AS, "Test Pad", NULL, 0) == PE_EXISTS, "SAVE AS an existing name asks first (exists)");
    check(pt(fx, PT_SAVE_AS, "test pad", NULL, 0) == PE_EXISTS, "names are the same whatever the case");
    check(pt(fx, PT_SAVE_AS, "Test Pad", NULL, 1) == PE_OK && pt(fx, PT_COUNT, NULL, NULL, 0) == 1, "... and replaces it when told to");
    static const char *const bad[] = {"", "   ", "a/b", "a\\b", "x:y", "what?", "con", "LPT1", "semi;colon", "dot.",
                                      "this name is far too long for a patch"};
    int nbad = 0;
    for (unsigned k = 0; k < sizeof bad / sizeof bad[0]; k++) nbad += pt(fx, PT_SAVE_AS, bad[k], NULL, 0) == PE_BADNAME;
    snprintf(what, sizeof what, "bad names are refused (%d of %d)", nbad, (int)(sizeof bad / sizeof bad[0]));
    check(nbad == (int)(sizeof bad / sizeof bad[0]) && pt(fx, PT_COUNT, NULL, NULL, 0) == 1, what);

    /* SAVE over the current user patch */
    fx->setParameter(fx, 37, 0.77f);
    snap(fx, saved);
    check(pt(fx, PT_SAVE, NULL, NULL, 0) == PE_OK && !pt(fx, PT_EDITED, NULL, NULL, 0), "SAVE stores the change in Test Pad");
    fx->dispatcher(fx, 2, 0, 1, NULL, 0);
    pt(fx, PT_LOAD, "Test Pad", NULL, 0);
    check(differs(fx, saved) < 0, "Test Pad now holds the change");
    fx->dispatcher(fx, 2, 0, 7, NULL, 0);   /* (a program other than the wrapper's current one: it skips a re-pick) */
    check(pt(fx, PT_SAVE, NULL, NULL, 0) == PE_NOT_USER, "SAVE on a factory preset needs a name (it never changes factory presets)");

    /* rename */
    pt(fx, PT_LOAD, "Test Pad", NULL, 0);
    check(pt(fx, PT_SAVE_AS, "Other", NULL, 0) == PE_OK, "a second patch, Other");
    pt(fx, PT_LOAD, "Test Pad", NULL, 0);
    check(pt(fx, PT_RENAME, "Other", NULL, 0) == PE_EXISTS, "RENAME onto an existing name asks first");
    check(pt(fx, PT_RENAME, "Bright Pad", NULL, 0) == PE_OK && !exists("Test Pad") && exists("Bright Pad") &&
          title_is(fx, "U  Bright Pad", 1), "RENAME Test Pad -> Bright Pad");
    fx->dispatcher(fx, 2, 0, 8, NULL, 0);
    pt(fx, PT_LOAD, "Bright Pad", NULL, 0);
    check(differs(fx, saved) < 0, "the renamed patch holds the same sound");
    snprintf(path, sizeof path, "%s\\Bright Pad.oneish", patch_dir);
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f) fclose(f);
    buf[n] = 0;
    check(!strncmp(buf, "ONE-ish patch 2\nname=Bright Pad\n", 32), "the file starts with its header (version 2) and its new name");

    /* the PRESET knob / < >: factory presets, then user patches A to Z */
    pt(fx, PT_LOAD_POS, NULL, NULL, (float)(np - 1));
    check(pt(fx, PT_STEP, NULL, NULL, 1) == np && title_is(fx, "U  Bright Pad", 1), "after the last factory preset: the first user patch");
    check(pt(fx, PT_STEP, NULL, NULL, 1) == np + 1 && title_is(fx, "U  Other", 1), "then the next one");
    check(pt(fx, PT_STEP, NULL, NULL, -2) == np - 1, "and back to the last factory preset");

    /* export / import */
    pt(fx, PT_LOAD, "Bright Pad", NULL, 0);
    snap(fx, other);
    snprintf(path, sizeof path, "%s\\out", patch_dir);
    CreateDirectoryA(path, NULL);
    snprintf(path, sizeof path, "%s\\out\\Shared Sound.oneish", patch_dir);
    check(pt(fx, PT_EXPORT, path, NULL, 0) == PE_OK && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES, "EXPORT writes a file");
    check(pt(fx, PT_IMPORT, path, NULL, 0) == PE_OK && exists("Shared Sound") && pt(fx, PT_COUNT, NULL, NULL, 0) == 3,
          "IMPORT copies it into the patches");
    check(pt(fx, PT_IMPORT, path, NULL, 0) == PE_EXISTS && pt(fx, PT_IMPORT, path, NULL, 1) == PE_OK,
          "IMPORT of a name already there asks first, then replaces");
    fx->dispatcher(fx, 2, 0, 5, NULL, 0);
    check(pt(fx, PT_LOAD, "Shared Sound", NULL, 0) == PE_OK && differs(fx, other) < 0, "the imported patch is the exported sound");
    snprintf(path, sizeof path, "%s\\out\\notes.oneish", patch_dir);
    write_file(path, "hello, this is not a patch\n");
    check(pt(fx, PT_IMPORT, path, NULL, 0) == PE_BADFILE && pt(fx, PT_COUNT, NULL, NULL, 0) == 3, "IMPORT refuses a file that isn't a patch");

    /* a damaged file: the defaults plus the lines that make sense, no crash */
    snprintf(path, sizeof path, "%s\\Broken.oneish", patch_dir);
    write_file(path, "ONE-ish patch 1\r\nname=Broken\r\nnonsense line\r\n=5\r\nflt_res=abc\r\nno_such_param=3\r\n"
                     "o2_level = 55 \r\no2_wave=tri\r\n\x01\x02junk=\r\nflt_res\r\n");
    int r = (int)pt(fx, PT_LOAD, "Broken", NULL, 0);
    pt(fx, PT_MESSAGE, NULL, buf, 0);
    snprintf(what, sizeof what, "a damaged file loads what makes sense and says so (\"%.80s...\")", buf);
    check(r == PE_BADFILE && strstr(buf, "made no sense") != NULL, what);
    void *chunk = NULL;
    intptr_t len = fx->dispatcher(fx, 23 /* effGetChunk */, 0, 0, &chunk, 0);
    static char state[PATCH_STATE_MAX];
    state[0] = 0;
    if (len > 0 && chunk) snprintf(state, sizeof state, "%s", (const char *)chunk);
    char *tag = strstr(state, ";ui_patch=");
    if (tag) *tag = 0;
    snprintf(what, sizeof what, "... the rest at the defaults: state \"%s\"", state);
    check(!strcmp(state, "v=1;o2_wave=0;o2_level=55") || !strcmp(state, "v=1;o2_level=55;o2_wave=0"), what);

    /* the * marker against the loaded values */
    pt(fx, PT_LOAD, "Bright Pad", NULL, 0);
    float was = fx->getParameter(fx, 1);
    fx->setParameter(fx, 1, was > 0.5f ? 0.2f : 0.8f);
    int on = (int)pt(fx, PT_EDITED, NULL, NULL, 0);
    fx->setParameter(fx, 1, was);
    check(on == 1 && pt(fx, PT_EDITED, NULL, NULL, 0) == 0, "the * comes and goes as a knob leaves and returns to the saved value");

    /* a project keeps which patch it is */
    len = fx->dispatcher(fx, 23, 0, 0, &chunk, 0);
    static char proj[PATCH_STATE_MAX];
    memcpy(proj, chunk, len > 0 && len <= PATCH_STATE_MAX ? len : 0);
    check(len > 0 && strstr(proj, ";ui_patch=0,U,") != NULL, "the project state names the patch (ui_patch)");
    fx->dispatcher(fx, 2, 0, 6, NULL, 0);
    fx->dispatcher(fx, 24 /* effSetChunk */, 0, len, proj, 0);
    check(title_is(fx, "U  Bright Pad", 1) && differs(fx, other) < 0, "reopening the project shows U Bright Pad again");

    /* delete */
    pt(fx, PT_LOAD, "Other", NULL, 0);
    check(pt(fx, PT_DELETE, NULL, NULL, 0) == PE_OK && !exists("Other") && title_is(fx, "-  Other *", 1),
          "DELETE removes Other; the sound stays, unsaved");
    check(pt(fx, PT_DELETE, NULL, NULL, 0) == PE_NOT_USER, "DELETE needs a user patch");
    pt(fx, PT_LOAD, "Broken", NULL, 0);
    pt(fx, PT_DELETE, NULL, NULL, 0);
    pt(fx, PT_LOAD, "Bright Pad", NULL, 0);
}

/* the PATCHES page through the window: dialogs answered by a helper thread, as a person would */
typedef struct { const char *title, *text; int button; } answer_t;
static const char *dialog_bmp;   /* where to put a picture of the name box (with the editor's pictures) */
static void dialog_shot(HWND d) {
    RECT r;
    GetWindowRect(d, &r);
    int w = r.right - r.left, h = r.bottom - r.top, stride = (w * 3 + 3) & ~3;
    char path[512];
    const char *dot = strrchr(dialog_bmp, '.');
    snprintf(path, sizeof path, "%.*s_patches_name.bmp", dot ? (int)(dot - dialog_bmp) : (int)strlen(dialog_bmp), dialog_bmp);
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bm = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(dc, bm);
    PrintWindow(d, dc, 0);
    BITMAPINFOHEADER bi = {sizeof bi, w, h, 1, 24, BI_RGB, 0, 0, 0, 0, 0};
    unsigned char *px = malloc((size_t)stride * h);
    FILE *f = px ? fopen(path, "wb") : NULL;
    if (f) {
        GetDIBits(dc, bm, 0, h, px, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
        BITMAPFILEHEADER fh = {0x4d42, (DWORD)(sizeof fh + sizeof bi + stride * h), 0, 0, sizeof fh + sizeof bi};
        fwrite(&fh, sizeof fh, 1, f); fwrite(&bi, sizeof bi, 1, f); fwrite(px, (size_t)stride * h, 1, f);
        fclose(f);
        printf("     wrote %s\n", path);
    }
    free(px);
    SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc); ReleaseDC(NULL, screen);
}
/* waits (up to 10 s) for the dialog to be shown: a dialog is shown only after its WM_INITDIALOG has filled it in, so the
 * name typed here can't be overwritten by the suggestion; the name is read back before OK is pressed */
static DWORD WINAPI answer(void *arg) {
    const answer_t *a = arg;
    for (int t = 0; t < 1000; t++, Sleep(10)) {
        HWND d = FindWindowA("#32770", a->title);
        if (!d || !IsWindowVisible(d)) continue;
        if (a->text) {
            char got[128] = "";
            SetDlgItemTextA(d, 101 /* ID_EDIT */, a->text);
            GetDlgItemTextA(d, 101, got, sizeof got);
            if (strcmp(got, a->text)) continue;
        }
        if (a->text && dialog_bmp) { Sleep(100); dialog_shot(d); }
        PostMessageA(d, WM_COMMAND, MAKEWPARAM(a->button, BN_CLICKED), 0);
        return 1;
    }
    return 0;
}
static void click_answered(HWND child, float s, float x, float y, const answer_t *a) {
    HANDLE th = CreateThread(NULL, 0, answer, (void *)a, 0, NULL);
    click(child, s, x, y);
    WaitForSingleObject(th, 11000);
    DWORD found = 0;
    GetExitCodeThread(th, &found);
    CloseHandle(th);
    if (!found) printf("     (no \"%s\" window appeared)\n", a->title);
}

static void patch_page(AEffect *fx, HWND child, float s, const char *bmp, int w, int h) {
    float sx = ZONE_SCREEN[0], sy = ZONE_SCREEN[1];
    dialog_bmp = bmp;
    float act_y = sy + 6 + 36 + 20 + 9, act_w = (384 - 4 * (PA_COUNT - 1)) / (float)PA_COUNT + 4;   /* the PATCHES page's buttons (patches.c) */
    click(child, s, sx + 200, sy + 23);   /* the patch name: the list */
    static const answer_t save_as = {"Save as", "UI Lead", IDOK}, yes = {"ONE-ish", NULL, IDYES};
    click_answered(child, s, sx + 8 + act_w * PA_SAVE_AS + act_w / 2 - 2, act_y, &save_as);
    int saved = exists("UI Lead") && title_is(fx, "U  UI Lead", 1);
    check(saved, "PATCHES page: SAVE AS asks for a name and saves");
    if (saved)   /* (without it DELETE would remove another patch) */
        click_answered(child, s, sx + 8 + act_w * PA_DELETE + act_w / 2 - 2, act_y, &yes);
    check(saved && !exists("UI Lead") && pt(fx, PT_COUNT, NULL, NULL, 0) == 2, "PATCHES page: DELETE asks, then removes it");
    pt(fx, PT_LOAD, "Bright Pad", NULL, 0);
    fx->setParameter(fx, 37, 0.3f);   /* edited: the * and the lit SAVE in the picture */
    SendMessageA(child, WM_TIMER, 1, 0);
    if (bmp) shot(child, w, h, bmp, "_patches");
    click(child, s, sx + 200, sy + 23);   /* the name again: back to the matrix */
}

/* ---- the step sequencer's page (desktop/seq_page.c), sequences in a temporary folder (ONEISH_SEQ_DIR) ---- */
static char seq_dir[MAX_PATH];
static int seq_now(AEffect *fx, sq_seq_t *q) {   /* the sequence the engine holds (TEST_SEQ / SQT_TEXT) */
    static char buf[SQT_BUF];
    buf[0] = 0;
    fx->dispatcher(fx, 50, TEST_SEQ, SQT_TEXT, buf, 0);
    sq_clear(q);
    return sq_decode(buf, q, 4096, 4096);
}
static void click_ctl(HWND child, float s, const char *key) {   /* a control on the panel, by its key */
    int i = rnd_key(key);
    for (int c = 0; c < NCTLS; c++)
        if (CTLS[c].param == i) click(child, s, CTLS[c].x + CTLS[c].w / 2.0f, CTLS[c].y + CTLS[c].h / 2.0f);
}
static void click_white(AEffect *fx, HWND child, float s, int j) {   /* white key j of the window's keyboard, low on it */
    click(child, s, ZONE_KEYS[0] + (j + 0.5f) * ZONE_KEYS[2] / 36.0f, ZONE_KEYS[1] + ZONE_KEYS[3] * 0.92f);
    play(fx, 2, 512);   /* the MIDI reaches the engine with the next block */
}
static void cell_xy(int col, int row, float *x, float *y) {   /* a step cell on the page (seq_page.c's geometry) */
    float x0 = ZONE_SCREEN[0] + 8 + 24, w = (ZONE_SCREEN[2] - 16 - 24) / 16.0f;
    *x = x0 + (col + 0.5f) * w;
    *y = ZONE_SCREEN[1] + 6 + 36 + 20 + 18 + 22 * row + 11;
}
static void seq_page_test(AEffect *fx, HWND child, float s, const char *bmp, int w, int h) {
    char what[256], buf[SQT_BUF];
    sq_seq_t q;
    float x, y;
    click_zone(child, s, ZONE_TAB_SEQ);   /* the step editor */
    float bx = ZONE_SCREEN[0] + 8 + (ZONE_SCREEN[2] - 16 - 24) / 8.0f * 4.5f + 6;   /* (8 buttons: CLEAR is the fifth) */
    float by = ZONE_SCREEN[1] + 6 + 36 + 20 + 18 + 176 + 8 + 24 + 9;   /* CLEAR, in the buttons' row */
    static const answer_t yes = {"ONE-ish", NULL, IDYES};
    click_answered(child, s, bx, by, &yes);   /* CLEAR (asked; Yes): an empty sequence to start with */
    check(seq_now(fx, &q) && sq_used_steps(&q) == 0, "SEQ page: CLEAR (after asking) empties the sequence");
    click_ctl(child, s, "seq_rec");
    check(fx->getParameter(fx, rnd_key("seq_rec")) > 0.5f, "SEQ page: the REC button on the SEQUENCER module lights");
    cell_xy(0, 0, &x, &y);
    click(child, s, x, y);   /* step 1 selected */
    click_white(fx, child, s, 14);   /* C4, E4, G4 from the window's keyboard: steps 1-3 */
    click_white(fx, child, s, 16);
    click_white(fx, child, s, 18);
    SendMessageA(child, WM_TIMER, 1, 0);
    int ok = seq_now(fx, &q) && q.n[0][0].note == 60 && q.n[1][0].note == 64 && q.n[2][0].note == 67;
    snprintf(what, sizeof what, "SEQ page: three keys played on the window's keyboard with REC on went into steps 1-3 "
             "(%d %d %d), the selection on step %d", q.n[0][0].note, q.n[1][0].note, q.n[2][0].note,
             (int)fx->dispatcher(fx, 50, TEST_SEQ, SQT_SELECTED, buf, 0) / 16 + 1);
    check(ok && fx->dispatcher(fx, 50, TEST_SEQ, SQT_SELECTED, buf, 0) / 16 == 3, what);
    static const int more[5] = {13, 14, 16, 14, 11};   /* five more steps: a phrase for the picture */
    for (int k = 0; k < 5; k++) click_white(fx, child, s, more[k]);
    click_ctl(child, s, "seq_rec");   /* REC off: clicks edit */
    cell_xy(5, 1, &x, &y);   /* a double-click on an empty cell: a new note (the last one's values) */
    LPARAM at = MAKELPARAM((int)lroundf(x * s), (int)lroundf(y * s));
    SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, at);
    SendMessageA(child, WM_LBUTTONUP, 0, at);
    SendMessageA(child, WM_LBUTTONDBLCLK, MK_LBUTTON, at);
    SendMessageA(child, WM_LBUTTONUP, 0, at);
    seq_now(fx, &q);
    snprintf(what, sizeof what, "SEQ page: a double-click on step 6, lane 2 makes a note there (%d)", q.n[5][1].note);
    check(sq_has(&q.n[5][1]), what);
    SendMessageA(child, WM_MOUSEWHEEL, MAKEWPARAM(0, 120), MAKELPARAM((int)lroundf(x * s), (int)lroundf(y * s)));
    int was = q.n[5][1].note;
    seq_now(fx, &q);
    check(q.n[5][1].note == was + 1, "SEQ page: the wheel over a note moves it up a semitone");
    cell_xy(1, 0, &x, &y);   /* a drag up on step 2's note: louder */
    int vel0 = q.n[1][0].vel;
    at = MAKELPARAM((int)lroundf(x * s), (int)lroundf(y * s));
    SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, at);
    SendMessageA(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM((int)lroundf(x * s), (int)lroundf((y - 30) * s)));
    SendMessageA(child, WM_LBUTTONUP, 0, MAKELPARAM((int)lroundf(x * s), (int)lroundf((y - 30) * s)));
    seq_now(fx, &q);
    snprintf(what, sizeof what, "SEQ page: dragging a note up raises its velocity (%d to %d)", vel0, q.n[1][0].vel);
    check(q.n[1][0].vel > vel0, what);
    cell_xy(5, 1, &x, &y);   /* a right-click deletes */
    SendMessageA(child, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM((int)lroundf(x * s), (int)lroundf(y * s)));
    seq_now(fx, &q);
    check(!sq_has(&q.n[5][1]), "SEQ page: a right-click deletes the note");
    /* .oneseq files: save, CLEAR (asked), load it back */
    char saved[SQT_BUF];
    fx->dispatcher(fx, 50, TEST_SEQ, SQT_TEXT, saved, 0);
    snprintf(buf, sizeof buf, "Walking Test");
    intptr_t r = fx->dispatcher(fx, 50, TEST_SEQ, SQT_SAVE, buf, 0);
    char path[MAX_PATH + 64];
    snprintf(path, sizeof path, "%s\\Walking Test.oneseq", seq_dir);
    check(r == 1 && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES, "SEQ page: SAVE writes Walking Test.oneseq (ONEISH_SEQ_DIR)");
    click_answered(child, s, bx, by, &yes);   /* CLEAR again, then Yes */
    seq_now(fx, &q);
    check(sq_used_steps(&q) == 0, "SEQ page: CLEAR again before loading the file");
    snprintf(buf, sizeof buf, "Walking Test");
    r = fx->dispatcher(fx, 50, TEST_SEQ, SQT_LOAD, buf, 0);
    char back[SQT_BUF];
    fx->dispatcher(fx, 50, TEST_SEQ, SQT_TEXT, back, 0);
    check(r == 1 && !strcmp(back, saved), "SEQ page: LOAD brings the saved sequence back exactly");
    /* (2026-10-29) performance notes in the file; ALL (a step's notes together) and DEL STEP; RTZ */
    {
        float x0 = ZONE_SCREEN[0] + 8, bw = (ZONE_SCREEN[2] - 16 - 4 * 7) / 8.0f, ty = ZONE_SCREEN[1] + 6 + 36 + 9;
        float fy = ZONE_SCREEN[1] + 6 + 36 + 20 + 18 + 176 + 8 + 10;   /* the values row */
        snprintf(buf, sizeof buf, "Wheel half up on bar 2");
        fx->dispatcher(fx, 50, TEST_SEQ, SQT_SET_NOTES, buf, 0);
        snprintf(buf, sizeof buf, "Notes Test");
        fx->dispatcher(fx, 50, TEST_SEQ, SQT_SAVE, buf, 0);
        char npath[MAX_PATH + 64], ntext[SQT_BUF] = "";
        snprintf(npath, sizeof npath, "%s\\Notes Test.oneseq", seq_dir);
        FILE *nf = fopen(npath, "rb");
        if (nf) { ntext[fread(ntext, 1, sizeof ntext - 1, nf)] = 0; fclose(nf); }
        buf[0] = 0;
        fx->dispatcher(fx, 50, TEST_SEQ, SQT_SET_NOTES, buf, 0);
        snprintf(buf, sizeof buf, "Notes Test");
        fx->dispatcher(fx, 50, TEST_SEQ, SQT_LOAD, buf, 0);
        fx->dispatcher(fx, 50, TEST_SEQ, SQT_NOTES, buf, 0);
        snprintf(what, sizeof what, "SEQ page: performance notes saved in the .oneseq file (%d) and read back on LOAD (\"%s\")",
                 strstr(ntext, "\nnotes=Wheel half up on bar 2\n") != NULL, buf);
        check(strstr(ntext, "\nnotes=Wheel half up on bar 2\n") && !strcmp(buf, "Wheel half up on bar 2"), what);
        cell_xy(0, 0, &x, &y);
        click(child, s, x, y);   /* step 1 (C4) */
        click(child, s, x0 + 2 * (bw + 4) + bw / 2, by);   /* ALL */
        int before = seq_now(fx, &q) ? q.n[0][0].note : -1;
        SendMessageA(child, WM_MOUSEWHEEL, MAKEWPARAM(0, 120), MAKELPARAM((int)lroundf((x0 + 110) * s), (int)lroundf(fy * s)));
        seq_now(fx, &q);
        int moved = q.n[0][0].note == before + 1;
        click(child, s, x0 + 3 * (bw + 4) + bw / 2, by);   /* DEL STEP */
        seq_now(fx, &q);
        snprintf(what, sizeof what, "SEQ page: ALL, then the wheel on NOTE moves step 1's notes (%d to %d), DEL STEP empties "
                 "it (%d) and keeps step 2 (%d)", before, before + moved, sq_step_empty(&q, 0), sq_has(&q.n[1][0]));
        check(moved && sq_step_empty(&q, 0) && sq_has(&q.n[1][0]), what);
        click(child, s, x0 + 2 * (bw + 4) + bw / 2, by);   /* ALL off */
        click(child, s, x0 + 216, ty);   /* RTZ */
        fx->dispatcher(fx, 50, TEST_SEQ, SQT_MESSAGE, buf, 0);
        snprintf(what, sizeof what, "SEQ page: RTZ on the title line (\"%s\")", buf);
        check(!strncmp(buf, "Return to zero", 14), what);
        snprintf(buf, sizeof buf, "Walking Test");   /* the phrase back, for what follows */
        fx->dispatcher(fx, 50, TEST_SEQ, SQT_LOAD, buf, 0);
    }
    /* a user patch keeps the sequence (its "seq=" line) */
    pt(fx, PT_SAVE_AS, "Seq Patch", NULL, 1);
    char ppath[MAX_PATH + 64], ptext[SQT_BUF] = "";
    snprintf(ppath, sizeof ppath, "%s\\Seq Patch.oneish", patch_dir);
    FILE *pf = fopen(ppath, "rb");
    if (pf) { ptext[fread(ptext, 1, sizeof ptext - 1, pf)] = 0; fclose(pf); }
    click_answered(child, s, bx, by, &yes);   /* CLEAR, then the patch again */
    pt(fx, PT_LOAD, "Seq Patch", NULL, 0);
    fx->dispatcher(fx, 50, TEST_SEQ, SQT_TEXT, back, 0);
    check(strstr(ptext, "\nseq=") && !strcmp(back, saved), "SEQ page: a user patch saves the sequence (a seq= line) and loads it back");
    /* the picture: the entered steps playing (ON, PLAY) on the step editor */
    click_ctl(child, s, "seq_on");
    click_ctl(child, s, "seq_play");
    play(fx, 30, 512);
    SendMessageA(child, WM_TIMER, 1, 0);
    check(fx->getParameter(fx, rnd_key("seq_on")) > 0.5f && fx->getParameter(fx, rnd_key("seq_play")) > 0.5f,
          "SEQ page: ON and PLAY on the SEQUENCER module start it");
    {   /* (2026-10-29) AUTO (step edit auto-scroll): page 3 picked while it plays; on, the page shown follows the playing
         * step back to page 1; off, it stays */
        float pbx = ZONE_SCREEN[0] + ZONE_SCREEN[2] - 8 - 2 * 25 + 11, ty = ZONE_SCREEN[1] + 6 + 36 + 9;
        click(child, s, pbx, ty);   /* page 3 */
        SendMessageA(child, WM_TIMER, 1, 0);
        play(fx, 4, 512);
        SendMessageA(child, WM_TIMER, 1, 0);
        int followed = (int)fx->dispatcher(fx, 50, TEST_SEQ, SQT_PAGE, buf, 0) == 0;
        click(child, s, ZONE_SCREEN[0] + 8 + 252, ty);   /* AUTO off */
        click(child, s, pbx, ty);
        play(fx, 4, 512);
        SendMessageA(child, WM_TIMER, 1, 0);
        int stayed = (int)fx->dispatcher(fx, 50, TEST_SEQ, SQT_PAGE, buf, 0) == 2;
        click(child, s, ZONE_SCREEN[0] + 8 + 252, ty);   /* AUTO on again */
        snprintf(what, sizeof what, "SEQ page: AUTO on, the page shown follows the playing step (%d); off, it stays (%d)",
                 followed, stayed);
        check(followed && stayed, what);
    }
    cell_xy(1, 0, &x, &y);
    click(child, s, x, y);
    if (bmp) shot(child, w, h, bmp, "_seq");
    /* a factory sequence's modulation lane (MODS view) and the settings page, for the pictures */
    fx->setParameter(fx, rnd_key("seq_pattern"), 1.0f / 3.0f);   /* Bass Line */
    play(fx, 30, 512);
    click_zone(child, s, ZONE_TAB_SEQ);   /* again: MODS */
    SendMessageA(child, WM_TIMER, 1, 0);
    if (bmp) shot(child, w, h, bmp, "_seq_mods");
    click_zone(child, s, ZONE_TAB_SEQ);   /* NOTES again */
    SendMessageA(child, WM_TIMER, 1, 0);
    if (bmp) shot(child, w, h, bmp, "_seq_bass");
    if (click_more(child, s, PG_SEQ) && bmp) shot(child, w, h, bmp, "_more_seq");
    click_ctl(child, s, "seq_play");
    click_ctl(child, s, "seq_on");
    fx->setParameter(fx, rnd_key("seq_pattern"), 0.0f);
    click_zone(child, s, ZONE_TAB_MATRIX);
}

/* ---- painting: how long the window takes to draw, and that it gives back what it takes ---- */
static double now_ms(void) {
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return t.QuadPart * 1000.0 / f.QuadPart;
}
static double print_ms(HWND child, HDC dc) {   /* one paint into dc (through WM_PRINTCLIENT, honouring dc's clip) */
    double t0 = now_ms();
    SendMessageA(child, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
    return now_ms() - t0;
}
/* the first paint (it makes the static layer and the knob sprites), a full repaint, and a knob turned with the mouse:
 * only what the window asks to repaint for it (its update region) is painted */
static void paint_timing(AEffect *fx, HWND child, int w, int h) {
    char what[200];
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bm = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(dc, bm);
    double first = print_ms(child, dc), full = 0, part = 0;
    for (int k = 0; k < 20; k++) full += print_ms(child, dc) / 20;
    int c = 0;
    while (c < NCTLS && CTLS[c].kind != K_KNOB) c++;
    if (c < NCTLS) {
        float s = w / (float)BASE_W, was = fx->getParameter(fx, CTLS[c].param);
        int x = (int)lroundf((CTLS[c].x + CTLS[c].w / 2.0f) * s), y = (int)lroundf((CTLS[c].y + CTLS[c].h / 3.0f) * s);
        RECT box = {0, 0, 0, 0};
        HWND parent = GetParent(child);   /* shown (off the screen, never activated): a hidden window keeps no update region */
        RECT home;
        GetWindowRect(parent, &home);
        SetWindowPos(parent, NULL, -w - 400, -h - 400, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ValidateRect(child, NULL);
        SendMessageA(child, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));   /* hovering first: its own repaint */
        ValidateRect(child, NULL);
        SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageA(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, y - 12));
        HRGN rgn = CreateRectRgn(0, 0, 0, 0);
        int kind = GetUpdateRgn(child, rgn, FALSE);
        GetRgnBox(rgn, &box);
        SelectClipRgn(dc, rgn);
        for (int k = 0; k < 20; k++) part += print_ms(child, dc) / 20;
        SelectClipRgn(dc, NULL);
        DeleteObject(rgn);
        SendMessageA(child, WM_LBUTTONUP, 0, MAKELPARAM(x, y - 12));
        SendMessageA(child, WM_MOUSEMOVE, 0, MAKELPARAM(0, 0));
        ValidateRect(child, NULL);
        SetWindowPos(parent, NULL, home.left, home.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
        /* (back where it was, hidden: later tests pass window positions as screen positions) */
        fx->setParameter(fx, CTLS[c].param, was);
        snprintf(what, sizeof what, "a knob turned with the mouse repaints only part of the window (%ld x %ld of %d x %d)",
                 box.right - box.left, box.bottom - box.top, w, h);
        check(kind != NULLREGION && kind != ERROR && (box.right - box.left) * (box.bottom - box.top) < w * h / 4, what);
    }
    printf("     paint: first %.1f ms (static layer and sprites), full repaint %.2f ms, a knob turned %.2f ms\n", first, full, part);
    snprintf(what, sizeof what, "full repaint %.2f ms (target under 16), knob repaint %.2f ms (target under 2)", full, part);
    check(full < 16 && part < 2, what);
    SelectObject(dc, old);
    DeleteObject(bm);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
}
/* the plugin window's corners: Windows 11 rounds top-level windows; the editor asks for square ones when it sits alone
 * in its own window (TEST_CORNERS: 1 done, 0 not that window, -1 Windows refused), and leaves a bigger window alone */
#define TEST_CORNERS 0x4F31434E   /* as editor.h */
static int windows_build(void) {
    typedef LONG(WINAPI * rtl_t)(OSVERSIONINFOW *);
    OSVERSIONINFOW v = {sizeof v, 0, 0, 0, 0, {0}};
    rtl_t f = (rtl_t)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    return f && f(&v) == 0 ? (int)v.dwBuildNumber : 0;
}
static void corner_test(AEffect *fx) {
    char what[200];
    struct { int16_t top, left, bottom, right; } *rect = NULL;
    fx->dispatcher(fx, 13 /* effEditGetRect */, 0, 0, &rect, 0);
    int w = rect ? rect->right - rect->left : 0, h = rect ? rect->bottom - rect->top : 0, got[2];
    for (int k = 0; k < 2; k++) {   /* 0: a window just our size; 1: a bigger one (a DAW's window with us in it) */
        HWND parent = CreateWindowExA(0, "STATIC", "host", WS_POPUP, 0, 0, w + k * 300, h + k * 200, NULL, NULL,
                                      GetModuleHandleA(NULL), NULL);
        fx->dispatcher(fx, 14 /* effEditOpen */, 0, 0, parent, 0);
        got[k] = (int)fx->dispatcher(fx, 50, TEST_CORNERS, 0, NULL, 0);
        fx->dispatcher(fx, 15 /* effEditClose */, 0, 0, NULL, 0);
        DestroyWindow(parent);
    }
    int build = windows_build(), win11 = build >= 22000;
    snprintf(what, sizeof what, "alone in its own window the editor asks for square corners (%d; Windows build %d)", got[0], build);
    check(win11 ? got[0] == 1 : got[0] != 0, what);
    snprintf(what, sizeof what, "inside a bigger window it leaves the corners alone (%d)", got[1]);
    check(got[1] == 0, what);
}

/* open, paint and close the editor 21 times: after the first, the GDI handle count must not grow */
static void gdi_test(AEffect *fx) {
    DWORD gdi0 = 0, user0 = 0;
    char what[200];
    for (int k = 0; k <= 20; k++) {
        struct { int16_t top, left, bottom, right; } *rect = NULL;
        fx->dispatcher(fx, 13 /* effEditGetRect */, 0, 0, &rect, 0);
        int w = rect ? rect->right - rect->left : 0, h = rect ? rect->bottom - rect->top : 0;
        HWND parent = CreateWindowExA(0, "STATIC", "host", WS_POPUP, 0, 0, w, h, NULL, NULL, GetModuleHandleA(NULL), NULL);
        fx->dispatcher(fx, 14 /* effEditOpen */, 0, 0, parent, 0);
        HWND child = GetWindow(parent, GW_CHILD);
        HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
        HBITMAP bm = CreateCompatibleBitmap(screen, w, h);
        HGDIOBJ old = SelectObject(dc, bm);
        if (child) print_ms(child, dc);
        SelectObject(dc, old);
        DeleteObject(bm);
        DeleteDC(dc);
        ReleaseDC(NULL, screen);
        fx->dispatcher(fx, 15 /* effEditClose */, 0, 0, NULL, 0);
        DestroyWindow(parent);
        if (k == 0) {
            gdi0 = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            user0 = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        }
    }
    DWORD gdi1 = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), user1 = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    snprintf(what, sizeof what, "the editor opened, painted and closed 20 more times: GDI handles %lu -> %lu, USER %lu -> %lu",
             (unsigned long)gdi0, (unsigned long)gdi1, (unsigned long)user0, (unsigned long)user1);
    check(gdi1 <= gdi0 + 2 && user1 <= user0 + 2, what);
}

/* a param set to its option named `name`, as a host would (0 if the param or the option isn't there) */
static int set_option(AEffect *fx, const char *key, const char *name) {
    int i = rnd_key(key);
    if (i >= NPARAMS) return 0;
    for (int k = 0; k < PARAMS[i].nopts; k++)
        if (!strcmp(PARAMS[i].opts[k], name)) {
            fx->setParameter(fx, i, PARAMS[i].nopts > 1 ? (float)k / (PARAMS[i].nopts - 1) : 0.0f);
            return 1;
        }
    return 0;
}

/* the MORE pages with pictures (more_pics.c): two envelopes, the filters, the mixer, two LFOs; some settings changed
 * for the pictures, then put back */
static void pictures(AEffect *fx, HWND child, float s, const char *bmp, int w, int h) {
    static const struct { const char *key; float v; } SET[] = {
        {"amp_s", 0.6f}, {"amp_acurve", 0.9f}, {"amp_hold", 0.35f}, {"fenv_loop", 1.0f}, {"svf_res", 0.6f},
        {"flt_res", 0.75f}, {"flt_route", 1.0f}, {"flt_mix", 0.6f}, {"svf_space", 0.7f}, {"svf_mode", 1.0f / 3},
        {"o2_level", 0.7f}, {"o2_svf", 1.0f}, {"noise_level", 0.4f}, {"noise_svf", 1.0f}, {"noise_lad", 0.0f},
        {"lfo2_wave", 4.0f / 6}, {"lfo2_var", 0.3f}, {"lfo2_phase", 0.25f}, {"lfo3_wave", 5.0f / 6},
        {"lfo3_smooth", 0.35f}, {"lfo3_polarity", 1.0f}};
    enum { NSET = sizeof SET / sizeof SET[0] };
    float was[NSET];
    for (int k = 0; k < NSET; k++) {
        int i = rnd_key(SET[k].key);
        was[k] = i < NPARAMS ? fx->getParameter(fx, i) : 0.0f;
        if (i < NPARAMS) fx->setParameter(fx, i, SET[k].v);
    }
    static const struct { int page; const char *suffix; } P[] = {
        {PG_ENV1, "_more_env1"}, {PG_ENV2, "_more_env2"}, {PG_FILTERS, "_more_filters"}, {PG_MIXER, "_more_mixer"},
        {PG_LFO2, "_more_lfo2"}, {PG_LFO3, "_more_lfo3"}};
    for (unsigned k = 0; k < sizeof P / sizeof P[0]; k++) {
        click_zone(child, s, ZONE_TAB_MATRIX);
        char what[96];
        snprintf(what, sizeof what, "the %s page (with its picture) opens from its MORE triangle", P[k].suffix + 6);
        check(P[k].page >= 0 && PAGES[P[k].page].pic && click_more(child, s, P[k].page), what);
        if (bmp) shot(child, w, h, bmp, P[k].suffix);
    }
    for (int k = 0; k < NSET; k++)
        if (rnd_key(SET[k].key) < NPARAMS) fx->setParameter(fx, rnd_key(SET[k].key), was[k]);
    click_zone(child, s, ZONE_TAB_MATRIX);
}

/* the master effect slots' pages by type (gen_layout.py MFX_USES): a hall in slot 1 and a blackhole in slot 2 show
 * their own controls and not the delays'; pictures of them, of the MASTER EFFECTS page and of the synth effect's
 * stereo delay. Whatever has no home is on an OTHER page (none expected) */
static int page_has(AEffect *fx, int page, const char *key) {   /* is key among the items page shows now? */
    int i = rnd_key(key);
    for (int k = PAGES[page].first; k < PAGES[page].first + PAGES[page].count; k++) {
        const lay_item_t *it = &ITEMS[k];
        if (it->param != i) continue;
        if (it->cond < 0) return 1;
        int n = PARAMS[it->cond].nopts, o = n > 1 ? (int)lroundf(fx->getParameter(fx, it->cond) * (n - 1)) : 0;
        if (o < 32 && (it->mask >> o & 1u)) return 1;
    }
    return 0;
}
/* the bouncing balls and particles' pages (2026-10-29): both open from the MORE index, show their params and their box,
 * and the box moves (two pictures of it a little apart differ) */
enum { PT_ROWS = 8, PT_SLOTS = 2 * PT_ROWS };   /* as editor.h's ROWS and SLOTS */
static unsigned char phys_px[2][4096 * 2048 * 3];
static int phys_grab(HWND child, int w, int h, unsigned char *px) {   /* the window's pixels (24-bit rows, bottom up) */
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bm = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(dc, bm);
    SendMessageA(child, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
    BITMAPINFOHEADER bi = {sizeof bi, w, -h, 1, 24, BI_RGB, 0, 0, 0, 0, 0};   /* (top down) */
    int stride = (w * 3 + 3) & ~3, ok = stride * h <= (int)sizeof phys_px[0];
    if (ok) GetDIBits(dc, bm, 0, h, px, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc); ReleaseDC(NULL, screen);
    return ok ? stride : 0;
}
static void open_page(HWND child, float s, int page) {   /* through the MORE index: its sub-page, then the page's slot */
    for (int k = 0; k <= page / PT_SLOTS; k++) click_zone(child, s, ZONE_TAB_MORE);
    int slot = page % PT_SLOTS, r = slot % PT_ROWS, col = slot / PT_ROWS;
    float x0 = ZONE_SCREEN[0] + 8.0f, half = (ZONE_SCREEN[2] - 16.0f - 8.0f) / 2;
    click(child, s, x0 + col * (half + 8.0f) + half * 0.3f, ZONE_SCREEN[1] + 80.0f + 22.0f * r + 11.0f);
}
static void phys_test(AEffect *fx, HWND child, float s, const char *bmp, int w, int h) {
    char what[200];
    int ok = PG_BALLS >= 0 && PG_PARTICLES >= 0 && PAGES[PG_BALLS].pic == PIC_PHYS && PAGES[PG_PARTICLES].pic == PIC_PHYS;
    check(ok, "BOUNCING BALLS and PARTICLES: two MORE pages, each with the box");
    if (!ok) return;
    check(page_has(fx, PG_BALLS, "phys_balls") && page_has(fx, PG_BALLS, "phys_gravity") &&
          page_has(fx, PG_PARTICLES, "phys_rate") && page_has(fx, PG_PARTICLES, "phys_trigger") &&
          page_has(fx, PG_PARTICLES, "phys_max"), "... their params are on them");
    set_option(fx, "phys_balls_on", "On");
    set_option(fx, "phys_parts_on", "On");
    static const struct { int page; const char *suffix; } P[] = {{PG_BALLS, "_more_balls"}, {PG_PARTICLES, "_more_particles"}};
    /* the picture's area (editor.c's rows, the last PIC_ROWS of them), in pixels */
    int x0 = (int)((ZONE_SCREEN[0] + 8) * s), x1 = (int)((ZONE_SCREEN[0] + ZONE_SCREEN[2] - 8) * s);
    int y0 = (int)((ZONE_SCREEN[1] + 80 + 22 * (PT_ROWS - PIC_ROWS)) * s), y1 = (int)((ZONE_SCREEN[1] + 80 + 22 * PT_ROWS) * s);
    for (int k = 0; k < 2; k++) {
        click_zone(child, s, ZONE_TAB_MATRIX);
        int st0 = phys_grab(child, w, h, phys_px[0]);
        open_page(child, s, P[k].page);
        play(fx, 30, 512);
        int st = phys_grab(child, w, h, phys_px[1]), opened = 0, moved = 0;
        for (int y = y0; st && st0 && y < y1 && y < h; y++)
            for (int x = x0; x < x1 && x < w; x++) opened += memcmp(&phys_px[0][y * st + 3 * x], &phys_px[1][y * st + 3 * x], 3) != 0;
        memcpy(phys_px[0], phys_px[1], (size_t)st * h);
        play(fx, 30, 512);
        phys_grab(child, w, h, phys_px[1]);
        for (int y = y0; st && y < y1 && y < h; y++)
            for (int x = x0; x < x1 && x < w; x++) moved += memcmp(&phys_px[0][y * st + 3 * x], &phys_px[1][y * st + 3 * x], 3) != 0;
        snprintf(what, sizeof what, "the %s page opens from the MORE index and draws its box (%d pixels new), which moves "
                 "(%d pixels changed 0.35 s later)", P[k].suffix + 6, opened, moved);
        check(opened > 500 && moved > 20, what);
        if (bmp) shot(child, w, h, bmp, P[k].suffix);
    }
    set_option(fx, "phys_balls_on", "Off");
    set_option(fx, "phys_parts_on", "Off");
    click_zone(child, s, ZONE_TAB_MATRIX);
}

static void master_pages(AEffect *fx, HWND child, float s, const char *bmp, int w, int h) {
    char what[160];
    if (PG_DELAY < 0 || PG_REVERB < 0) return;
    int t1 = rnd_key("dly_type"), t2 = rnd_key("rev_type"), st = rnd_key("modfx_type");
    float was1 = fx->getParameter(fx, t1), was2 = fx->getParameter(fx, t2), was3 = fx->getParameter(fx, st);
    int ok = set_option(fx, "dly_type", "Hall") && set_option(fx, "rev_type", "Blackhole");
    snprintf(what, sizeof what, "master FX 1 as a hall shows its decays and levels, not the delay's times (%d %d %d)",
             page_has(fx, PG_DELAY, "dly_ldecay"), page_has(fx, PG_DELAY, "dly_mlevel"), page_has(fx, PG_DELAY, "dly_time"));
    check(ok && page_has(fx, PG_DELAY, "dly_ldecay") && page_has(fx, PG_DELAY, "dly_mlevel") &&
          !page_has(fx, PG_DELAY, "dly_time") && page_has(fx, PG_DELAY, "dly_mix"), what);
    snprintf(what, sizeof what, "master FX 2 as a blackhole shows GRAVITY, not the plate's DISTANCE (%d %d)",
             page_has(fx, PG_REVERB, "rev_gravity"), page_has(fx, PG_REVERB, "rev_distance"));
    check(page_has(fx, PG_REVERB, "rev_gravity") && !page_has(fx, PG_REVERB, "rev_distance"), what);
    static const struct { int page; const char *suffix; } P[] = {
        {PG_DELAY, "_more_fx1_hall"}, {PG_REVERB, "_more_fx2_blackhole"}, {PG_MFX, "_more_mfx"}};
    for (unsigned k = 0; k < sizeof P / sizeof P[0] && bmp; k++) {
        click_zone(child, s, ZONE_TAB_MATRIX);
        if (click_more(child, s, P[k].page)) shot(child, w, h, bmp, P[k].suffix);
    }
    if (set_option(fx, "modfx_type", "Stereo Delay")) {
        check(page_has(fx, PG_SFX, "modfx_fbl") && !page_has(fx, PG_SFX, "modfx_vrate"),
              "the synth effect as a stereo delay shows FEEDBACK LEFT, not the choruses' vibrato");
        click_zone(child, s, ZONE_TAB_MATRIX);
        if (bmp && click_more(child, s, PG_SFX)) shot(child, w, h, bmp, "_more_sfx_sdelay");
    }
    fx->setParameter(fx, t1, was1);
    fx->setParameter(fx, t2, was2);
    fx->setParameter(fx, st, was3);
    click_zone(child, s, ZONE_TAB_MATRIX);
    printf("     MORE pages: %d, OTHER page: %s\n", NPAGES, PG_OTHER >= 0 ? "yes (params without a home)" : "none");
}

/* the rate LEDs (rate_leds.c): over 1.5 s of their timer they flash (the TEMPO LED on the beat at 120 BPM: this host
 * gives no tempo; LFO 1 at a few Hz), and each repaint asks for only the LEDs' own small squares; one LED's repaint
 * is timed */
#define RATE_TIMER 2   /* as editor.c's timer id */
static void rate_led_test(AEffect *fx, HWND child, int w, int h) {
    char what[200];
    if (NRATES == 0) return;
    float s = w / (float)BASE_W;
    int lfo = rnd_key("lfo1_rate"), lsync = rnd_key("lfo1_sync");
    float was = lfo < NPARAMS ? fx->getParameter(fx, lfo) : 0, wsync = lsync < NPARAMS ? fx->getParameter(fx, lsync) : 0;
    if (lsync < NPARAMS) fx->setParameter(fx, lsync, 0.0f);
    if (lfo < NPARAMS) fx->setParameter(fx, lfo, 0.5f);   /* a few Hz */
    HWND parent = GetParent(child);   /* shown (off the screen): a hidden window keeps no update region */
    RECT home;
    GetWindowRect(parent, &home);
    SetWindowPos(parent, NULL, -w - 400, -h - 400, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ValidateRect(child, NULL);
    int ticks = 0, changes = 0, biggest = 0, rects = 0;
    HRGN rgn = CreateRectRgn(0, 0, 0, 0);
    RECT one = {0, 0, 0, 0};
    for (double t0 = now_ms(); now_ms() - t0 < 1500.0; ticks++) {
        Sleep(15);
        SendMessageA(child, WM_TIMER, RATE_TIMER, 0);
        int kind = GetUpdateRgn(child, rgn, FALSE);
        if (kind != NULLREGION && kind != ERROR) {
            changes++;
            DWORD size = GetRegionData(rgn, 0, NULL);
            RGNDATA *d = size ? malloc(size) : NULL;
            if (d && GetRegionData(rgn, size, d))
                for (DWORD k = 0; k < d->rdh.nCount; k++, rects++) {
                    RECT r = ((RECT *)d->Buffer)[k];
                    int big = r.right - r.left > r.bottom - r.top ? r.right - r.left : r.bottom - r.top;
                    if (big > biggest) biggest = big;
                    one = r;
                }
            free(d);
        }
        ValidateRect(child, NULL);
    }
    DeleteObject(rgn);
    snprintf(what, sizeof what, "the rate LEDs flash: %d repaints in %d timer ticks over 1.5 s", changes, ticks);
    check(changes >= 4, what);
    snprintf(what, sizeof what, "a flashing LED repaints only its own square (%d rects, the biggest %d px)", rects, biggest);
    check(rects > 0 && biggest <= (int)lroundf(16 * s) + 2, what);
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bm = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(dc, bm);
    HRGN clip = CreateRectRgnIndirect(&one);
    SelectClipRgn(dc, clip);
    double ms = 0;
    for (int k = 0; k < 20; k++) ms += print_ms(child, dc) / 20;
    SelectClipRgn(dc, NULL);
    DeleteObject(clip);
    SelectObject(dc, old);
    DeleteObject(bm);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    printf("     paint: a rate LED %.3f ms\n", ms);
    snprintf(what, sizeof what, "a rate LED repaints in %.3f ms (target under 2)", ms);
    check(ms < 2, what);
    SetWindowPos(parent, NULL, home.left, home.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    if (lfo < NPARAMS) fx->setParameter(fx, lfo, was);
    if (lsync < NPARAMS) fx->setParameter(fx, lsync, wsync);
}

/* open the editor the way a host does (a parent window, effEditGetRect, effEditOpen), paint it, save a picture of it
 * as a 24-bit BMP if asked (and of the matrix's second page and some MORE pages), close it */
static void focus_test(AEffect *fx, HWND child, float s, const char *bmp, int w, int h);
static void settings_page_test(AEffect *fx, HWND child, float s, const char *bmp, int w, int h);
static void editor(AEffect *fx, const char *bmp) {
    char what[128];
    check(fx->flags & 1, "the plugin says it has an editor window");
    struct { int16_t top, left, bottom, right; } *rect = NULL;
    fx->dispatcher(fx, 13 /* effEditGetRect */, 0, 0, &rect, 0);
    int w = rect ? rect->right - rect->left : 0, h = rect ? rect->bottom - rect->top : 0;
    snprintf(what, sizeof what, "editor size %d x %d", w, h);
    check(w > 600 && h > 300, what);
    if (w <= 0 || h <= 0) return;
    HWND parent = CreateWindowExA(0, "STATIC", "host", WS_POPUP, 0, 0, w, h, NULL, NULL, GetModuleHandleA(NULL), NULL);
    intptr_t ok = fx->dispatcher(fx, 14 /* effEditOpen */, 0, 0, parent, 0);
    HWND child = GetWindow(parent, GW_CHILD);
    check(ok && child, "the editor window opens inside the host's window");
    if (child) paint_timing(fx, child, w, h);
    if (child) rate_led_test(fx, child, w, h);
    if (child) {   /* play the on-screen keyboard: a click on a white key must sound, letting go must stop it */
        fx->dispatcher(fx, 2 /* effSetProgram */, 0, 0, NULL, 0);
        play(fx, 700, 512);   /* let the last preset's tails die away */
        double quiet = play(fx, 20, 512);
        RECT cr;
        GetClientRect(child, &cr);
        LPARAM at = MAKELPARAM(cr.right * 6 / 10, cr.bottom * 95 / 100);   /* low on a white key, near middle C */
        SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, at);
        double held = play(fx, 40, 512);
        snprintf(what, sizeof what, "a click on the window's keyboard plays a note (rms %.3f, before %.4f)", held, quiet);
        check(held > 0.01 && held > quiet * 4, what);
        SendMessageA(child, WM_LBUTTONUP, 0, at);
        play(fx, 400, 512);
        double after = play(fx, 20, 512);
        snprintf(what, sizeof what, "letting go of the key stops it (rms %.4f)", after);
        check(after < held / 10, what);
        ribbon_test(fx, child, cr.right / (float)BASE_W);
        random_test(fx, child, cr.right / (float)BASE_W, bmp, w, h);
        phys_test(fx, child, cr.right / (float)BASE_W, bmp, w, h);
        click_zone(child, cr.right / (float)BASE_W, ZONE_TAB_MATRIX);   /* the matrix's first rows again */
        fx->dispatcher(fx, 2 /* effSetProgram */, 0, 0, NULL, 0);   /* back to the first preset for the pictures */
        play(fx, 400, 512);
        SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, at);   /* held again for the picture */
        play(fx, 4, 512);
    }
    if (child && bmp) {
        RECT cr;
        GetClientRect(child, &cr);
        float s = cr.right / (float)BASE_W;
        shot(child, w, h, bmp, "");
        SendMessageA(child, WM_LBUTTONUP, 0, 0);
        click_zone(child, s, ZONE_TAB_MATRIX);   /* the matrix again: rows 9-16 */
        shot(child, w, h, bmp, "_matrix2");
        if (click_more(child, s, PG_LFO1)) shot(child, w, h, bmp, "_more_lfo1");
        pictures(fx, child, s, bmp, w, h);
        master_pages(fx, child, s, bmp, w, h);
        if (PG_SFX >= 0) {   /* the synth effect's page for some of its types (stereo flanger: two sub-pages) */
            static const char *const T[][2] = {{"Stereo Flanger", "_more_synthfx"}, {"Modal Resonator", "_more_sfx_rings"},
                                               {"Granular", "_more_sfx_clouds"}, {"Tape Color", "_more_sfx_tape"}};
            int type = ITEMS[PAGES[PG_SFX].first].param;
            float was = fx->getParameter(fx, type);
            for (unsigned k = 0; k < sizeof T / sizeof T[0]; k++) {
                if (!set_option(fx, "modfx_type", T[k][0])) continue;
                click_zone(child, s, ZONE_TAB_MATRIX);
                click_more(child, s, PG_SFX);
                shot(child, w, h, bmp, T[k][1]);
            }
            fx->setParameter(fx, type, was);
        }
        click_zone(child, s, ZONE_TAB_MORE);
        shot(child, w, h, bmp, "_more_index");
        click_zone(child, s, ZONE_TAB_ANALOG);
        shot(child, w, h, bmp, "_analog");
        /* the arpeggiator's and the generative section's pages, switched on (and the ring rotated) for the pictures */
        static const char *const keys[] = {"arp_on", "gen_euclid_on", "gen_rot"};
        static const float set[] = {1.0f, 1.0f, 2.0f / 31.0f};
        float was[3];
        for (int k = 0; k < 3; k++) {
            int i = rnd_key(keys[k]);
            was[k] = i < NPARAMS ? fx->getParameter(fx, i) : 0.0f;
            if (i < NPARAMS) fx->setParameter(fx, i, set[k]);
        }
        if (click_more(child, s, PG_ARP)) shot(child, w, h, bmp, "_more_arp");
        click_zone(child, s, ZONE_TAB_MATRIX);
        if (EUCLID_P[1] >= 0) click_zone(child, s, ZONE_EUCLID);   /* the ring opens the generative page too */
        else click_more(child, s, PG_GEN);
        if (PG_GEN >= 0) shot(child, w, h, bmp, "_more_gen");
        for (int k = 0; k < 3; k++)
            if (rnd_key(keys[k]) < NPARAMS) fx->setParameter(fx, rnd_key(keys[k]), was[k]);
        if (click_more(child, s, PG_MACRO)) shot(child, w, h, bmp, "_more_macro");
    }
    if (child) {
        RECT cr;
        GetClientRect(child, &cr);
        SendMessageA(child, WM_LBUTTONUP, 0, 0);
        patch_page(fx, child, cr.right / (float)BASE_W, bmp, w, h);
        seq_page_test(fx, child, cr.right / (float)BASE_W, bmp, w, h);
        focus_test(fx, child, cr.right / (float)BASE_W, bmp, w, h);
        failures += library_tests(fx, child, cr.right / (float)BASE_W, bmp, w, h, patch_dir);   /* undo, compare, sets ... */
        settings_page_test(fx, child, cr.right / (float)BASE_W, bmp, w, h);
    }
    fx->dispatcher(fx, 15 /* effEditClose */, 0, 0, NULL, 0);
    check(!IsWindow(child), "the editor window closes");
    DestroyWindow(parent);
}

/* ---- the SETTINGS page (desktop/settings.c): its file in the temporary folder (ONEISH_SETTINGS) ---- */
static char settings_file[MAX_PATH];
static intptr_t st(AEffect *fx, int op, const char *in, char *out, float opt) {   /* a TEST_SETTINGS operation */
    static char buf[1024];
    snprintf(buf, sizeof buf, "%s", in ? in : "");
    intptr_t r = fx->dispatcher(fx, 50, TEST_SETTINGS, op, buf, opt);
    if (out) snprintf(out, 1024, "%s", buf);
    return r;
}
static int file_has(const char *path, const char *text) {   /* the settings file holds this line */
    char t[4096] = "";
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(t, 1, sizeof t - 1, f);
    fclose(f);
    t[n] = 0;
    return strstr(t, text) != NULL;
}
static void settings_page_test(AEffect *fx, HWND child, float s, const char *bmp, int w, int h) {
    char buf[1024], what[300];
    (void)s;
    check(st(fx, STT_RESTORE, NULL, NULL, 0) == 1 && file_has(settings_file, "autoload=1")
              && file_has(settings_file, "ONE-ish settings 1"),
          "SETTINGS: RESTORE DEFAULT SETTINGS writes the settings file (ONEISH_SETTINGS) with the defaults");
    st(fx, STT_PATH, NULL, buf, 0);
    snprintf(what, sizeof what, "SETTINGS: the file is where ONEISH_SETTINGS says (%s)", buf);
    check(!_stricmp(buf, settings_file), what);

    /* PRESET AUTOLOAD off: NEXT only points; a click on the PRESET knob loads it */
    pt(fx, PT_LOAD_POS, NULL, NULL, 2);
    int pos0 = (int)pt(fx, PT_POS, NULL, NULL, 0);
    check(st(fx, STT_SET, "autoload=0", NULL, 0) == 1 && file_has(settings_file, "autoload=0"),
          "SETTINGS: PRESET AUTOLOAD off is saved at once");
    int after = (int)pt(fx, PT_STEP, NULL, NULL, 1), cue = (int)st(fx, STT_CUE, NULL, NULL, 0);
    pt(fx, PT_STEP, NULL, NULL, 1);
    int cue2 = (int)st(fx, STT_CUE, NULL, NULL, 0);
    pt(fx, PT_TITLE, NULL, buf, 0);
    snprintf(what, sizeof what, "autoload off: NEXT twice leaves patch %d loaded and points at %d, then %d (\"%s\")",
             after, cue, cue2, buf);
    check(after == pos0 && cue == pos0 + 1 && cue2 == pos0 + 2 && buf[0] == '>', what);
    check(st(fx, STT_CUE_LOAD, NULL, NULL, 0) == 1 && pt(fx, PT_POS, NULL, NULL, 0) == pos0 + 2
              && st(fx, STT_CUE, NULL, NULL, 0) == -1,
          "autoload off: a click on the PRESET knob loads the one it points at");
    st(fx, STT_SET, "autoload=1", NULL, 0);
    check(file_has(settings_file, "bank_preview=1") && st(fx, STT_SET, "bank_preview=0", NULL, 0) == 1
              && file_has(settings_file, "bank_preview=0") && st(fx, STT_SET, "bank_preview=1", NULL, 0) == 1,
          "SETTINGS: SHOW BANK PREVIEW (the SET page's) is a setting, on by default");
    check(pt(fx, PT_STEP, NULL, NULL, 1) == pos0 + 3 && st(fx, STT_CUE, NULL, NULL, 0) == -1,
          "autoload on: NEXT loads at once, as before");

    /* MIDI I/O FROM PRESET off: a loaded patch keeps the MIDI channel */
    int ch = rnd_key("midi_ch");
    if (ch < NPARAMS) {
        float omni = 0.0f, five = 5.0f / (PARAMS[ch].nopts - 1);
        fx->setParameter(fx, ch, omni);
        pt(fx, PT_SAVE_AS, "MIDI IO Test", NULL, 1);
        fx->setParameter(fx, ch, five);
        st(fx, STT_SET, "midi_io_preset=0", NULL, 0);
        pt(fx, PT_LOAD, "MIDI IO Test", NULL, 0);
        float kept = fx->getParameter(fx, ch);
        fx->dispatcher(fx, 2 /* effSetProgram */, 0, 1, NULL, 0);
        float kept2 = fx->getParameter(fx, ch);
        st(fx, STT_SET, "midi_io_preset=1", NULL, 0);
        pt(fx, PT_LOAD, "MIDI IO Test", NULL, 0);
        float loaded = fx->getParameter(fx, ch);
        snprintf(what, sizeof what, "MIDI I/O FROM PRESET: off, a patch and a preset keep MIDI channel 5 (%.3f, %.3f); "
                 "on, the patch's Omni loads (%.3f)", kept, kept2, loaded);
        check(fabsf(kept - five) < 1e-4f && fabsf(kept2 - five) < 1e-4f && fabsf(loaded - omni) < 1e-4f, what);
        pt(fx, PT_DELETE, NULL, NULL, 0);
    }

    /* the pedals: a fixed CC is skipped; the engine follows a new sustain number */
    st(fx, STT_SET, "sustain_cc=66", NULL, 0);
    check(file_has(settings_file, "sustain_cc=67"), "pedals: SUSTAIN can't take CC 66 (sostenuto): it moves on to 67");
    st(fx, STT_SET, "exp1_cc=67", NULL, 0);
    check(file_has(settings_file, "exp1_cc=68"), "pedals: two pedals can't share a CC (EXP 1 goes on to 68)");
    st(fx, STT_SET, "sustain_cc=69", NULL, 0);
    fx->dispatcher(fx, 2 /* effSetProgram */, 0, 0, NULL, 0);
    play(fx, 400, 512);
    send_midi(fx, 0xB0, 69, 127);
    send_midi(fx, 0x90, 60, 100);
    play(fx, 20, 512);
    send_midi(fx, 0x80, 60, 0);
    play(fx, 200, 512);
    double held = play(fx, 10, 512);
    send_midi(fx, 0xB0, 69, 0);
    play(fx, 400, 512);
    double gone = play(fx, 10, 512);
    send_midi(fx, 0xB0, 64, 127);   /* the old number now does nothing */
    send_midi(fx, 0x90, 60, 100);
    play(fx, 20, 512);
    send_midi(fx, 0x80, 60, 0);
    play(fx, 400, 512);
    double old = play(fx, 10, 512);
    send_midi(fx, 0xB0, 64, 0);
    snprintf(what, sizeof what, "pedals: sustain on CC 69 holds the note (rms %.4f), lets go (%.5f); CC 64 doesn't "
             "hold any more (%.5f)", held, gone, old);
    check(held > 0.01 && gone < held / 20 && old < held / 20, what);

    /* the file is read again as a new process would; unknown lines are skipped */
    FILE *f = fopen(settings_file, "wb");
    if (f) { fputs("ONE-ish settings 1\r\nautoload=1\r\ntempo=99\r\nrx_clock=0\r\nnonsense line\r\nfuture_key=3\r\n", f); fclose(f); }
    st(fx, STT_RELOAD, NULL, NULL, 0);
    st(fx, STT_GET, NULL, buf, 0);
    check(strstr(buf, "tempo=99") && strstr(buf, "rx_clock=0") && strstr(buf, "sustain_cc=64"),
          "SETTINGS: a hand-edited file loads (unknown lines skipped, the rest at their defaults)");

    /* MIDI LEARN: LEARN, a control, a CC from the host: that CC moves it (and the host hears of it) */
    st(fx, STT_RESTORE, NULL, NULL, 0);
    int cut = rnd_key("flt_cut"), svf2 = -1, wave = rnd_key("lfo1_wave");
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, "s2_svf_cut")) svf2 = i;
    if (cut < NPARAMS && svf2 >= 0 && wave < NPARAMS) {
        st(fx, STT_LEARN, NULL, NULL, 0);
        int picked = (int)st(fx, STT_LEARN_PICK, NULL, NULL, (float)cut);
        send_midi(fx, 0xB0, 64, 100);   /* reserved (the sustain pedal): refused, still waiting */
        int stage = (int)st(fx, STT_LEARN_TICK, NULL, NULL, 0);
        send_midi(fx, 0xB0, 64, 0);   /* (the pedal up again: it went to the engine as the sustain pedal) */
        st(fx, STT_LEARN_TICK, NULL, NULL, 0);
        st(fx, STT_MESSAGE, NULL, buf, 0);
        snprintf(what, sizeof what, "LEARN: a reserved CC is refused (\"%s\")", buf);
        check(picked == 1 && stage == 2 && strstr(buf, "reserved"), what);
        send_midi(fx, 0xB0, 74, 10);
        stage = (int)st(fx, STT_LEARN_TICK, NULL, NULL, 0);
        int nmap = (int)st(fx, STT_MAP, NULL, buf, 0);
        snprintf(what, sizeof what, "LEARN: CC 74 is learned for the ladder cutoff (\"%s\"), saved in the file", buf);
        check(stage == 0 && nmap == 1 && !strcmp(buf, "74,flt_cut,7;") && file_has(settings_file, "map=74,flt_cut,7"),
              what);
        int told = automated[cut];
        send_midi(fx, 0xB0, 74, 127);
        play(fx, 8, 512);
        float hi = fx->getParameter(fx, cut);
        send_midi(fx, 0xB0, 74, 0);
        play(fx, 8, 512);
        snprintf(what, sizeof what, "LEARN: CC 74 then moves the cutoff through the DLL (%.3f, %.3f), the host told (%d)",
                 hi, fx->getParameter(fx, cut), automated[cut] - told);
        check(hi == 1.0f && fx->getParameter(fx, cut) == 0.0f && automated[cut] > told, what);
        st(fx, STT_LEARN, NULL, NULL, 0);   /* 14-bit: the CC's LSB (CC + 32) follows it */
        st(fx, STT_LEARN_PICK, NULL, NULL, (float)svf2);
        send_midi(fx, 0xB0, 7, 64);
        send_midi(fx, 0xB0, 39, 0);
        st(fx, STT_LEARN_TICK, NULL, NULL, 0);
        st(fx, STT_LEARN, NULL, NULL, 0);
        st(fx, STT_LEARN_PICK, NULL, NULL, (float)wave);
        send_midi(fx, 0xB0, 21, 0);
        st(fx, STT_LEARN_TICK, NULL, NULL, 0);
        st(fx, STT_MAP, NULL, buf, 0);
        send_midi(fx, 0xB0, 7, 100);
        send_midi(fx, 0xB0, 39, 50);
        play(fx, 4, 512);
        float v14 = fx->getParameter(fx, svf2);
        snprintf(what, sizeof what, "LEARN: CC 7+39 is found to be 14-bit for Synth 2's SVF cutoff (%.5f), CC 21 an "
                 "option (\"%s\")", v14, buf);
        check(strstr(buf, "7,s2_svf_cut,14;") && strstr(buf, "21,lfo1_wave,7;")
                  && fabsf(v14 - (100 * 128 + 50) / 16383.0f) < 1e-4f, what);
        st(fx, STT_MAP_BITS, NULL, NULL, 0);   /* CC 74 can't be 14-bit; CC 7 goes back to 7; CC 21 to 14 */
        st(fx, STT_MAP_BITS, NULL, NULL, 1);
        st(fx, STT_MAP_BITS, NULL, NULL, 2);
        st(fx, STT_MAP, NULL, buf, 0);
        snprintf(what, sizeof what, "LEARN: 7/14-BIT toggles, 14 only on CC 0-31 (\"%s\")", buf);
        check(strstr(buf, "74,flt_cut,7;") && strstr(buf, "7,s2_svf_cut,7;") && strstr(buf, "21,lfo1_wave,14;"), what);
        st(fx, STT_SET, "exp1_cc=21", NULL, 0);   /* a pedal moved onto a learned CC: the pedal wins */
        st(fx, STT_MAP, NULL, buf, 0);
        check(!strstr(buf, "21,"), "LEARN: a pedal moved onto a learned CC takes it (the mapping goes)");
        st(fx, STT_SET, "exp1_cc=11", NULL, 0);
        /* NRPN after the manual's table reaches the engine through the DLL (the wrapper's own NRPN is off) */
        send_midi(fx, 0xB0, 99, 1150 >> 7);
        send_midi(fx, 0xB0, 98, 1150 & 127);
        send_midi(fx, 0xB0, 6, 64);
        send_midi(fx, 0xB0, 38, 0);
        play(fx, 4, 512);
        snprintf(what, sizeof what, "NRPN 1150 (ladder cutoff) sets it through the DLL (%.4f)", fx->getParameter(fx, cut));
        check(fabsf(fx->getParameter(fx, cut) - 8192 / 16383.0f) < 1e-3f, what);
        if (bmp) {
            st(fx, STT_TAB, NULL, NULL, (float)ST_MIDI);
            st(fx, STT_LEARN, NULL, NULL, 0);
            shot(child, w, h, bmp, "_settings_midi");
        }
        st(fx, STT_MAP_CLEAR, NULL, NULL, 0);
        check(st(fx, STT_MAP, NULL, NULL, 0) == 0 && !file_has(settings_file, "map="), "LEARN: CLEAR ALL empties the map");
    }

    /* MICROTUNING: a Scala scale and keyboard map from the repo's test files into Synth 1, kept in the project and in a
     * patch file; the TUNING tab's picture shows them (then they are cleared) */
    const char *scl = "ports/one-ish/test/scales/ji-5-limit.scl", *kbm = "ports/one-ish/test/scales/ji-on-d.kbm";
    if (GetFileAttributesA(scl) != INVALID_FILE_ATTRIBUTES) {
        int ok1 = (int)st(fx, STT_TUN_LOAD, scl, NULL, 0), ok2 = (int)st(fx, STT_TUN_LOAD, kbm, NULL, 1);
        st(fx, STT_TUN_NAMES, NULL, buf, 0);
        void *chunk = NULL;
        intptr_t n = fx->dispatcher(fx, 23 /* effGetChunk */, 0, 0, &chunk, 0);
        int in_project = n > 0 && chunk && strstr(chunk, ";tun_scl=ji-5-limit|12,") && strstr(chunk, ";tun_kbm=ji-on-d|");
        snprintf(what, sizeof what, "TUNING: a .scl and a .kbm load into Synth 1 (\"%s\") and go into the project", buf);
        check(ok1 == 1 && ok2 == 1 && !strcmp(buf, "ji-5-limit|ji-on-d") && in_project, what);
        pt(fx, PT_SAVE_AS, "Tuning Test", NULL, 1);
        st(fx, STT_TUN_CLEAR, NULL, NULL, 0);
        st(fx, STT_TUN_CLEAR, NULL, NULL, 1);
        st(fx, STT_TUN_NAMES, NULL, buf, 0);
        int cleared = !strcmp(buf, "|");
        pt(fx, PT_LOAD, "Tuning Test", NULL, 0);
        st(fx, STT_TUN_NAMES, NULL, buf, 0);
        snprintf(what, sizeof what, "TUNING: CLEAR takes them off (%s); a user patch brings them back (\"%s\")",
                 cleared ? "yes" : "NO", buf);
        check(cleared && !strcmp(buf, "ji-5-limit|ji-on-d"), what);
        check(st(fx, STT_TUN_LOAD, "ports/one-ish/test/scales/ji-on-d.kbm", NULL, 0) == 0,
              "TUNING: a keyboard map loaded as a scale is refused");
    } else {
        printf("     (TUNING: the test scales aren't here; run from the repo root to check them)\n");
    }

    if (bmp) {
        for (int t = 0; t < ST_TABS; t++) {
            static const char *const SUFFIX[ST_TABS] = {"_settings", "_settings_midi_empty", "_settings_library",
                                                        "_settings_tuning"};
            st(fx, STT_TAB, NULL, NULL, (float)t);
            shot(child, w, h, bmp, SUFFIX[t]);
        }
    }
    if (pt(fx, PT_LOAD, "Tuning Test", NULL, 0) == PE_OK) pt(fx, PT_DELETE, NULL, NULL, 0);
    st(fx, STT_TUN_CLEAR, NULL, NULL, 0);
    st(fx, STT_TUN_CLEAR, NULL, NULL, 1);
    st(fx, STT_RESTORE, NULL, NULL, 0);
}

/* ---- PANEL FOCUS (multitimbral): the panel shows the focused Synth's values ---- */
static unsigned char *grab(HWND child, int w, int h) {   /* the window as 24-bit pixels (free() them) */
    int stride = (w * 3 + 3) & ~3;
    unsigned char *px = malloc((size_t)stride * h);
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bm = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(dc, bm);
    SendMessageA(child, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
    BITMAPINFOHEADER bi = {sizeof bi, w, -h, 1, 24, BI_RGB, 0, 0, 0, 0, 0};
    if (px) GetDIBits(dc, bm, 0, h, px, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc); ReleaseDC(NULL, screen);
    return px;
}
static int region_same(const unsigned char *a, const unsigned char *b, int w, RECT r) {   /* r in pixels */
    int stride = (w * 3 + 3) & ~3;
    for (int y = r.top; y < r.bottom; y++)
        if (memcmp(a + y * stride + r.left * 3, b + y * stride + r.left * 3, (size_t)(r.right - r.left) * 3)) return 0;
    return 1;
}
static void diff_box(const unsigned char *a, const unsigned char *b, int w, RECT r, const char *what) {   /* (a FAIL's why) */
    int stride = (w * 3 + 3) & ~3, x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    for (int y = r.top; y < r.bottom; y++)
        for (int x = r.left; x < r.right; x++)
            if (memcmp(a + y * stride + x * 3, b + y * stride + x * 3, 3)) {
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
    if (x1 >= 0) printf("     %s: pixels differ in x %d-%d, y %d-%d (of %ld-%ld, %ld-%ld)\n", what, x0, x1, y0, y1,
                        (long)r.left, (long)r.right, (long)r.top, (long)r.bottom);
}
static RECT ctl_px(const char *key, float s) {   /* a panel control's cell, in pixels, less a margin (neighbours spill) */
    RECT r = {0, 0, 0, 0};
    int i = rnd_key(key);
    for (int c = 0; c < NCTLS; c++)
        if (CTLS[c].param == i)
            SetRect(&r, (int)(CTLS[c].x * s) + 6, (int)(CTLS[c].y * s), (int)((CTLS[c].x + CTLS[c].w) * s) - 6,
                    (int)((CTLS[c].y + CTLS[c].h) * s));
    return r;
}
static void setkey(AEffect *fx, const char *key, float v) { if (rnd_key(key) < NPARAMS) fx->setParameter(fx, rnd_key(key), v); }
static float getkey(AEffect *fx, const char *key) { return rnd_key(key) < NPARAMS ? fx->getParameter(fx, rnd_key(key)) : -1; }
static void focus_test(AEffect *fx, HWND child, float s, const char *bmp, int w, int h) {
    char what[256];
    RECT k = ctl_px("flt_cut", s), sw = ctl_px("o1_wave", s), scr;
    SetRect(&scr, (int)(ZONE_SCREEN[0] * s), (int)((ZONE_SCREEN[1] + 80) * s), (int)((ZONE_SCREEN[0] + ZONE_SCREEN[2]) * s),
            (int)((ZONE_SCREEN[1] + 80 + 8 * 22) * s));   /* the screen's rows (the matrix) */
    /* three different sounds: cutoff 10 / 90 / 50%, osc 1 tri / saw / tri, matrix row 1's amount */
    setkey(fx, "flt_cut", 0.10f); setkey(fx, "s2_flt_cut", 0.90f); setkey(fx, "s3_flt_cut", 0.50f);
    setkey(fx, "o1_wave", 0.0f); setkey(fx, "s2_o1_wave", 1.0f); setkey(fx, "s3_o1_wave", 0.0f);
    for (int m = 1; m <= 9; m += 8) {   /* (rows 1 and 9: the matrix page may show either half) */
        char k1[16], k2[16], k3[16];
        snprintf(k1, sizeof k1, "mod%d_amt", m); snprintf(k2, sizeof k2, "s2_mod%d_amt", m); snprintf(k3, sizeof k3, "s3_mod%d_amt", m);
        setkey(fx, k1, 0.55f); setkey(fx, k2, 0.95f); setkey(fx, k3, 0.20f);
    }
    click_zone(child, s, ZONE_TAB_MATRIX);
    unsigned char *px[4];
    for (int c = 0; c < 3; c++) {   /* the SYNTH buttons, clicked */
        click_zone(child, s, c == 0 ? ZONE_FOCUS1 : c == 1 ? ZONE_FOCUS2 : ZONE_FOCUS3);
        intptr_t f = fx->dispatcher(fx, 50, TEST_FOCUS, -1, NULL, 0);
        snprintf(what, sizeof what, "SYNTH %d button: the panel shows Synth %d (focus %d, mask %d)", c + 1, c + 1,
                 (int)(f % 4), (int)(f / 4));
        check(f % 4 == c && f / 4 == 1 << c, what);
        px[c] = grab(child, w, h);
        if (c == 1 && bmp) shot(child, w, h, bmp, "_focus2");
    }
    {   /* the M | S halves beside each SYNTH button, and a double-click on the button itself */
        static const short *const FZ[3] = {ZONE_FOCUS1, ZONE_FOCUS2, ZONE_FOCUS3};
        float on0 = getkey(fx, "s2_syn_on"), s3_was = getkey(fx, "s3_syn_on");
        click(child, s, FZ[1][0] + 30.5f, FZ[1][1] + 12.0f);   /* Synth 2's M */
        float on1 = getkey(fx, "s2_syn_on");
        click(child, s, FZ[1][0] + 30.5f, FZ[1][1] + 12.0f);
        float on2 = getkey(fx, "s2_syn_on");
        snprintf(what, sizeof what, "Synth 2's M switches it off and on (ON %.0f -> %.0f -> %.0f)", on0, on1, on2);
        check(on0 != on1 && on2 == on0, what);
        click(child, s, FZ[2][0] + 40.5f, FZ[2][1] + 12.0f);   /* Synth 3's S */
        float so1 = getkey(fx, "s3_syn_solo"), so_other = getkey(fx, "s2_syn_solo");
        click(child, s, FZ[2][0] + 40.5f, FZ[2][1] + 12.0f);
        float so2 = getkey(fx, "s3_syn_solo");
        snprintf(what, sizeof what, "Synth 3's S solos it and lets go (%.0f, %.0f; Synth 2's solo %.0f)", so1, so2, so_other);
        check(so1 == 1.0f && so2 == 0.0f && so_other == 0.0f, what);
        if (bmp) {   /* a picture: Synth 2 muted, Synth 3 soloed */
            setkey(fx, "s2_syn_on", 0.0f); setkey(fx, "s3_syn_on", 1.0f); setkey(fx, "s3_syn_solo", 1.0f);
            InvalidateRect(child, NULL, FALSE);
            shot(child, w, h, bmp, "_mute_solo");
            setkey(fx, "s3_syn_solo", 0.0f);
        }
        float m0 = getkey(fx, "s3_syn_on");
        LPARAM at = MAKELPARAM((int)lroundf((FZ[2][0] + 12) * s), (int)lroundf((FZ[2][1] + 12) * s));
        SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, at);
        SendMessageA(child, WM_LBUTTONUP, 0, at);
        SendMessageA(child, WM_LBUTTONDBLCLK, MK_LBUTTON, at);
        SendMessageA(child, WM_LBUTTONUP, 0, at);
        float m1 = getkey(fx, "s3_syn_on");
        intptr_t f = fx->dispatcher(fx, 50, TEST_FOCUS, -1, NULL, 0);
        snprintf(what, sizeof what, "a double-click on SYNTH 3 picks it and mutes it (focus %d, ON %.0f -> %.0f)",
                 (int)(f % 4), m0, m1);
        check(f % 4 == 2 && m0 != m1, what);
        setkey(fx, "s2_syn_on", on0);
        setkey(fx, "s3_syn_on", s3_was);
    }
    click_zone(child, s, ZONE_FOCUS1);
    px[3] = grab(child, w, h);
    int ok = px[0] && px[1] && px[2] && px[3];
    if (ok) {
        int back = region_same(px[0], px[3], w, k), k12 = region_same(px[0], px[1], w, k), k23 = region_same(px[1], px[2], w, k);
        int w12 = region_same(px[0], px[1], w, sw), w13 = region_same(px[0], px[2], w, sw);
        int m12 = region_same(px[0], px[1], w, scr), m23 = region_same(px[1], px[2], w, scr);
        snprintf(what, sizeof what, "switching focus redraws the cutoff knob (1/2 %s, 2/3 %s, back to 1 the same: %s), "
                 "the osc 1 wave button (1/2 %s, 1/3 %s) and the matrix on the screen (1/2 %s, 2/3 %s)",
                 k12 ? "same" : "differ", k23 ? "same" : "differ", back ? "yes" : "no", w12 ? "same" : "differ",
                 w13 ? "same" : "differ", m12 ? "same" : "differ", m23 ? "same" : "differ");
        ok = back && !k12 && !k23 && !w12 && w13 && !m12 && !m23;
        if (!w13) diff_box(px[0], px[2], w, sw, "osc 1 wave, Synth 1 vs 3");
        check(ok, what);
        /* the drawn value is Synth 2's: Synth 1 set to Synth 2's values draws exactly what focus 2 drew */
        setkey(fx, "flt_cut", 0.90f); setkey(fx, "o1_wave", 1.0f); setkey(fx, "mod1_amt", 0.95f); setkey(fx, "mod9_amt", 0.95f);
        unsigned char *p1 = grab(child, w, h);
        snprintf(what, sizeof what, "Synth 1 given Synth 2's cutoff, wave and matrix amount draws them exactly as focus "
                 "on Synth 2 did (the panel showed Synth 2's values)");
        if (p1) { diff_box(p1, px[1], w, k, "cutoff"); diff_box(p1, px[1], w, sw, "wave"); diff_box(p1, px[1], w, scr, "matrix"); }
        check(p1 && region_same(p1, px[1], w, k) && region_same(p1, px[1], w, sw) && region_same(p1, px[1], w, scr), what);
        free(p1);
    } else {
        check(0, "pictures of the window for the focus test");
    }
    for (int c = 0; c < 4; c++) free(px[c]);
    /* a knob turned with Synth 2 in focus moves Synth 2 only; with Synths 2 and 3 both in focus, both */
    fx->dispatcher(fx, 50, TEST_FOCUS, 2, NULL, 1.0f);
    float s1 = getkey(fx, "flt_res"), s3 = getkey(fx, "s3_flt_res");
    RECT r = ctl_px("flt_res", s);
    POINT at = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(at.x, at.y));
    SendMessageA(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(at.x, at.y - 60));
    SendMessageA(child, WM_LBUTTONUP, 0, MAKELPARAM(at.x, at.y - 60));
    float s2 = getkey(fx, "s2_flt_res");
    snprintf(what, sizeof what, "a knob turned with Synth 2 in focus moves Synth 2's (%.2f) and not Synth 1's (%.2f = "
             "%.2f) or 3's (%.2f = %.2f)", s2, getkey(fx, "flt_res"), s1, getkey(fx, "s3_flt_res"), s3);
    check(s2 > 0.2f && getkey(fx, "flt_res") == s1 && getkey(fx, "s3_flt_res") == s3, what);
    fx->dispatcher(fx, 50, TEST_FOCUS, 6, NULL, 1.0f);   /* Synths 2 and 3 (Ctrl-click) */
    SendMessageA(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(at.x, at.y));
    SendMessageA(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(at.x, at.y + 20));
    SendMessageA(child, WM_LBUTTONUP, 0, MAKELPARAM(at.x, at.y + 20));
    snprintf(what, sizeof what, "with Synths 2 and 3 in focus a knob moves both (%.2f, %.2f), Synth 1 stays (%.2f)",
             getkey(fx, "s2_flt_res"), getkey(fx, "s3_flt_res"), getkey(fx, "flt_res"));
    check(fabsf(getkey(fx, "s2_flt_res") - getkey(fx, "s3_flt_res")) < 1e-6f && getkey(fx, "s3_flt_res") != s3
          && getkey(fx, "flt_res") == s1, what);
    /* a shared control (the master delay's feedback) is the same whatever the focus */
    float d1 = getkey(fx, "dly_fb");
    unsigned char *a = grab(child, w, h);
    fx->dispatcher(fx, 50, TEST_FOCUS, 1, NULL, 0.0f);
    unsigned char *b = grab(child, w, h);
    RECT fb = ctl_px("dly_fb", s);
    check(a && b && region_same(a, b, w, fb) && getkey(fx, "dly_fb") == d1, "a SHARED control (master delay feedback) "
          "draws the same with Synth 1 and Synth 3 in focus");
    free(a); free(b);
    /* SETUP: the three Synths side by side; a click on Synth 2's ON switches Synth 2 on */
    click_zone(child, s, ZONE_TAB_SETUP);
    float on2 = getkey(fx, "s2_syn_on");
    float cx = ZONE_SCREEN[0] + 8 + 384 * 0.30f + (384 - 384 * 0.30f) / 3 * 1.5f, cy = ZONE_SCREEN[1] + 80 + 11;
    click(child, s, cx, cy);
    snprintf(what, sizeof what, "SETUP page: a click on Synth 2's ON cell switches Synth 2 %s -> %s; Synth 1 stays on",
             on2 > 0.5f ? "on" : "off", getkey(fx, "s2_syn_on") > 0.5f ? "on" : "off");
    check((getkey(fx, "s2_syn_on") > 0.5f) != (on2 > 0.5f) && getkey(fx, "syn_on") > 0.5f, what);
    if (bmp) shot(child, w, h, bmp, "_setup");
    click_zone(child, s, ZONE_TAB_SETUP);   /* its second page: pan, sends, synth effect, the shared settings */
    if (bmp) shot(child, w, h, bmp, "_setup2");
    click(child, s, cx, cy);   /* (Synth 2's pan: a drag cell; a click leaves it) */
    setkey(fx, "s2_syn_on", 0.0f);
    fx->dispatcher(fx, 50, TEST_FOCUS, 1, NULL, 0.0f);
    click_zone(child, s, ZONE_TAB_MATRIX);
}

/* a host unloading the DLL and loading it again (Carla after a rebuild): nothing of the old DLL stays registered, and
 * the new one opens its editor and plays */
static void reload_test(const char *path, HMODULE old) {
    WNDCLASSA wc;
    check(!GetClassInfoA(old, "ONEishEditor", &wc), "after FreeLibrary no window class points into the unloaded DLL");
    HMODULE dll = LoadLibraryA(path);
    AEffect *(*entry)(audioMasterCallback) =
        dll ? (AEffect * (*)(audioMasterCallback))(void *)GetProcAddress(dll, "VSTPluginMain") : NULL;
    AEffect *fx = entry ? entry(host) : NULL;
    check(fx != NULL, "the DLL loads again");
    if (!fx) { if (dll) FreeLibrary(dll); return; }
    fx->dispatcher(fx, 0, 0, 0, NULL, 0);
    fx->dispatcher(fx, 10, 0, 0, NULL, 44100.0f);
    fx->dispatcher(fx, 12, 0, 1, NULL, 0);
    struct { int16_t top, left, bottom, right; } *rect = NULL;
    fx->dispatcher(fx, 13 /* effEditGetRect */, 0, 0, &rect, 0);
    int w = rect ? rect->right - rect->left : 0, h = rect ? rect->bottom - rect->top : 0;
    HWND parent = CreateWindowExA(0, "STATIC", "host", WS_POPUP, 0, 0, w, h, NULL, NULL, GetModuleHandleA(NULL), NULL);
    intptr_t ok = fx->dispatcher(fx, 14 /* effEditOpen */, 0, 0, parent, 0);
    HWND child = GetWindow(parent, GW_CHILD);
    check(ok && child && GetClassInfoA(dll, "ONEishEditor", &wc), "the reloaded DLL opens its editor (its own class)");
    send_midi(fx, 0x90, 60, 100);
    double rms = play(fx, 20, 512);
    char what[96];
    snprintf(what, sizeof what, "the reloaded DLL plays (rms %.3f)", rms);
    check(rms > 0.01, what);
    fx->dispatcher(fx, 15 /* effEditClose */, 0, 0, NULL, 0);
    DestroyWindow(parent);
    fx->dispatcher(fx, 12, 0, 0, NULL, 0);
    fx->dispatcher(fx, 1 /* effClose */, 0, 0, NULL, 0);
    check(!GetClassInfoA(dll, "ONEishEditor", &wc), "closed again: its window class is gone again");
    FreeLibrary(dll);
}

int main(int argc, char **argv) {
    char tmp[MAX_PATH];   /* user patches go to a fresh temporary folder, not the owner's Documents */
    GetTempPathA(sizeof tmp, tmp);
    snprintf(patch_dir, sizeof patch_dir, "%soneish_patch_test_%lu", tmp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(patch_dir, NULL);
    empty_dir(patch_dir);
    SetEnvironmentVariableA("ONEISH_PATCH_DIR", patch_dir);
    snprintf(seq_dir, sizeof seq_dir, "%soneish_seq_test_%lu", tmp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(seq_dir, NULL);
    empty_dir(seq_dir);
    SetEnvironmentVariableA("ONEISH_SEQ_DIR", seq_dir);
    snprintf(settings_file, sizeof settings_file, "%s\\Settings.txt", patch_dir);   /* (removed with the folder) */
    SetEnvironmentVariableA("ONEISH_SETTINGS", settings_file);
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

    audition(fx, argc > 2 && argv[2][0] ? argv[2] : NULL);
    patch_tests(fx);
    editor(fx, argc > 3 ? argv[3] : NULL);
    gdi_test(fx);
    corner_test(fx);
    const char *base = getenv("ONEISH_BASELINE_DLL");   /* an older build to compare 44.1 kHz output with */
    HMODULE old = base && base[0] ? LoadLibraryA(base) : NULL;
    if (base && base[0] && !old) check(0, "ONEISH_BASELINE_DLL loads");
    failures += rate_tests((void *)entry, old ? (void *)GetProcAddress(old, "VSTPluginMain") : NULL);
    if (old) FreeLibrary(old);

    fx->dispatcher(fx, 12, 0, 0, NULL, 0);
    fx->dispatcher(fx, 1 /* effClose */, 0, 0, NULL, 0);
    WNDCLASSA wc;
    check(!GetClassInfoA(dll, "ONEishEditor", &wc), "the last instance closed: the editor's window class is gone");
    FreeLibrary(dll);
    reload_test(argc > 1 ? argv[1] : "ONE-ish.dll", dll);
    empty_dir(patch_dir);
    RemoveDirectoryA(patch_dir);
    empty_dir(seq_dir);
    RemoveDirectoryA(seq_dir);
    printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures != 0;
}
