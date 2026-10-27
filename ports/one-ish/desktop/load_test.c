/* Load the desktop ONE-ish.dll the way a DAW does (LoadLibrary + VSTPluginMain), play a note, save/restore its
 * state, then play every preset in its program list, then drive the editor window (keyboard, ribbon, the randomiser
 * page: RANDOMISE, UNDO, REDO, MUTATE, locks, and clicks while another thread renders). Proves the DLL's export, MIDI
 * path, audio path and presets on Windows itself; with a second path, writes all the presets to one WAV to listen
 * to; with a third, pictures of the editor. User patches (save, load, rename, delete, import, export) are tested in
 * a temporary folder. Then the host sample rate checks (rate_test.c): other rates and block sizes, float output; with
 * ONEISH_BASELINE_DLL set to an older build, its 44.1 kHz output must match this one's to the 16-bit sample.
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
#include "patches.h"   /* TEST_PATCH and its operations, for the user patch tests */

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

static int automated[NPARAMS], begins, ends;   /* what the plugin told the host per param (audioMasterAutomate, Begin/EndEdit) */
static int automated_n, begun, ended;          /* and in total: audioMasterAutomate / BeginEdit / EndEdit calls */
static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)v; (void)p; (void)o;
    if (op == 0 && i >= 0 && i < NPARAMS) automated[i]++;
    if (op == 0) automated_n++;
    if (op == 43) { begins++; begun++; }
    if (op == 44) { ends++; ended++; }
    return op == 1 ? 2400 : 0;   /* audioMasterVersion */
}

int rate_tests(void *entry, void *old_entry);   /* rate_test.c: returns its failures */

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
enum { RP_GO, RP_MUTATE, RP_UNDO, RP_REDO, RP_LOCK };
static void rnd_point(int what, int k, float *x, float *y) {
    float x0 = ZONE_SCREEN[0] + 8.0f, x1 = ZONE_SCREEN[0] + ZONE_SCREEN[2] - 8.0f, top = ZONE_SCREEN[1] + 6.0f + 36 + 20 + 18;
    float half = (x1 - x0 - 8) / 2, xr = x0 + half + 8, q = (half - 8) / 2;
    switch (what) {
    case RP_GO: *x = x0 + half / 2; *y = top + 22; break;
    case RP_MUTATE: *x = xr + half / 2; *y = top + 22; break;
    case RP_UNDO: *x = x0 + q / 2; *y = top + 22 * 7 + 11; break;
    case RP_REDO: *x = x0 + q + 8 + q / 2; *y = top + 22 * 7 + 11; break;
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
    check(!strncmp(buf, "ONE-ish patch 1\nname=Bright Pad\n", 32), "the file starts with its header and its new name");

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
    char state[8192] = "";
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
    static char proj[8192];
    memcpy(proj, chunk, len > 0 && len <= 8192 ? len : 0);
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
static DWORD WINAPI answer(void *arg) {
    const answer_t *a = arg;
    for (int t = 0; t < 500; t++, Sleep(10)) {
        HWND d = FindWindowA("#32770", a->title);
        if (!d) continue;
        Sleep(50);
        if (a->text) SetDlgItemTextA(d, 101 /* ID_EDIT */, a->text);
        if (a->text && dialog_bmp) { Sleep(100); dialog_shot(d); }
        PostMessageA(d, WM_COMMAND, MAKEWPARAM(a->button, BN_CLICKED), 0);
        return 1;
    }
    return 0;
}
static void click_answered(HWND child, float s, float x, float y, const answer_t *a) {
    HANDLE th = CreateThread(NULL, 0, answer, (void *)a, 0, NULL);
    click(child, s, x, y);
    WaitForSingleObject(th, 6000);
    DWORD found = 0;
    GetExitCodeThread(th, &found);
    CloseHandle(th);
    if (!found) printf("     (no \"%s\" window appeared)\n", a->title);
}

static void patch_page(AEffect *fx, HWND child, float s, const char *bmp, int w, int h) {
    float sx = ZONE_SCREEN[0], sy = ZONE_SCREEN[1];
    dialog_bmp = bmp;
    float act_y = sy + 6 + 36 + 20 + 9, act_w = (384 - 20) / 6.0f + 4;   /* the PATCHES page's button row (patches.c) */
    click(child, s, sx + 200, sy + 23);   /* the patch name: the list */
    static const answer_t save_as = {"Save as", "UI Lead", IDOK}, yes = {"ONE-ish", NULL, IDYES};
    click_answered(child, s, sx + 8 + act_w * PA_SAVE_AS + act_w / 2 - 2, act_y, &save_as);
    check(exists("UI Lead") && title_is(fx, "U  UI Lead", 1), "PATCHES page: SAVE AS asks for a name and saves");
    click_answered(child, s, sx + 8 + act_w * PA_DELETE + act_w / 2 - 2, act_y, &yes);
    check(!exists("UI Lead") && pt(fx, PT_COUNT, NULL, NULL, 0) == 2, "PATCHES page: DELETE asks, then removes it");
    pt(fx, PT_LOAD, "Bright Pad", NULL, 0);
    fx->setParameter(fx, 37, 0.3f);   /* edited: the * and the lit SAVE in the picture */
    SendMessageA(child, WM_TIMER, 1, 0);
    if (bmp) shot(child, w, h, bmp, "_patches");
    click(child, s, sx + 200, sy + 23);   /* the name again: back to the matrix */
}

/* open the editor the way a host does (a parent window, effEditGetRect, effEditOpen), paint it, save a picture of it
 * as a 24-bit BMP if asked (and of the matrix's second page and some MORE pages), close it */
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
        if (PG_SFX >= 0) {   /* the synth effect's page for one type (stereo flanger: two sub-pages) */
            int type = ITEMS[PAGES[PG_SFX].first].param;
            float was = fx->getParameter(fx, type);
            fx->setParameter(fx, type, 5.0f / 14.0f);
            click_more(child, s, PG_SFX);
            shot(child, w, h, bmp, "_more_synthfx");
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
    }
    fx->dispatcher(fx, 15 /* effEditClose */, 0, 0, NULL, 0);
    check(!IsWindow(child), "the editor window closes");
    DestroyWindow(parent);
}

int main(int argc, char **argv) {
    char tmp[MAX_PATH];   /* user patches go to a fresh temporary folder, not the owner's Documents */
    GetTempPathA(sizeof tmp, tmp);
    snprintf(patch_dir, sizeof patch_dir, "%soneish_patch_test_%lu", tmp, (unsigned long)GetCurrentProcessId());
    CreateDirectoryA(patch_dir, NULL);
    empty_dir(patch_dir);
    SetEnvironmentVariableA("ONEISH_PATCH_DIR", patch_dir);
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
    const char *base = getenv("ONEISH_BASELINE_DLL");   /* an older build to compare 44.1 kHz output with */
    HMODULE old = base && base[0] ? LoadLibraryA(base) : NULL;
    if (base && base[0] && !old) check(0, "ONEISH_BASELINE_DLL loads");
    failures += rate_tests((void *)entry, old ? (void *)GetProcAddress(old, "VSTPluginMain") : NULL);
    if (old) FreeLibrary(old);

    fx->dispatcher(fx, 12, 0, 0, NULL, 0);
    fx->dispatcher(fx, 1 /* effClose */, 0, 0, NULL, 0);
    FreeLibrary(dll);
    empty_dir(patch_dir);
    RemoveDirectoryA(patch_dir);
    printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures != 0;
}
