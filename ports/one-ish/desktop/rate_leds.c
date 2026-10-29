/* The rate LEDs on the desktop editor's panel (see editor.c): one per LFO beside its RATE knob, one over the
 * arpeggiator's RATE and one beside the sequencer's, flashing at those rates, and the master clock's TEMPO LED on each
 * beat. Where they sit comes from gen_layout.py (RATES in layout.h).
 *
 * Nothing here touches the audio thread. The rates come from the params as the window reads every other value
 * (getParameter, and each LFO's rate as the engine words it, "6.4 Hz" or "1/4 T", through effGetParamDisplay); the
 * sequencer's playing step from its "seq_pos" readout (seq_page.c reads it); the tempo and the beat from the host's
 * time info, asked for on the UI thread (the tempo the engine is given too), 120 BPM when the host gives none. Synced
 * rates line up with the host's beat while it plays; free ones run on the window's own clock.
 *
 * A timer (RATE_TIMER_MS) works out which LEDs are lit and asks to repaint only those that changed, each its own small
 * square (rate_led_rect); a paint draws them as they were worked out. UI thread only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "skin.h"

_Static_assert(NRATES <= RATE_MAX, "rate_ui_t holds every rate LED");

#define T_LABEL RGB(214, 214, 216)   /* as editor_draw.c */
#define READ_S 0.1                   /* the rates are read from the params this often */
#define STEADY_S 0.09                /* a cycle shorter than this: lit steadily (too fast to see flashing) */

/* the host's time info (the VST2 SDK's VstTimeInfo layout), only the fields read here */
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} time_info_t;
enum { audioMasterGetTime = 7, kTransportPlaying = 1 << 1, kPpqPosValid = 1 << 9, kTempoValid = 1 << 10 };

static double now_s(void) {
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)f.QuadPart;
}

static int key_ix(const char *k) {   /* a param's index by key, -1 if it has none */
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, k)) return i;
    return -1;
}

/* beats in a synced LFO's rate as the engine words it: "4 bars", "1 bar", "1/16", then " D" (dotted) or " T" */
static float text_beats(const char *s) {
    float b = 0.0f;
    int d;
    if (sscanf(s, "1/%d", &d) == 1 && d > 0) b = 4.0f / d;
    else if (atof(s) > 0.0f && strstr(s, "bar")) b = 4.0f * (float)atof(s);
    if (strstr(s, " D")) b *= 1.5f;
    else if (strstr(s, " T")) b *= 2.0f / 3.0f;
    return b;
}

/* a step of the arpeggiator or sequencer: DIVISION (1/1 .. 1/128) times TIMING, at the host tempo (SYNC) or the
 * module's FREE RATE in BPM */
static void step_rate(ed_t *e, rate_ui_t *r, int k, const char *pre) {
    char a[32];
    int i;
    snprintf(a, sizeof a, "%s_div", pre);
    float beats = (i = key_ix(a)) >= 0 ? 4.0f / (float)(1 << option_of(e, i)) : 0.25f;
    snprintf(a, sizeof a, "%s_timing", pre);
    if ((i = key_ix(a)) >= 0) beats *= option_of(e, i) == 1 ? 1.5f : option_of(e, i) == 2 ? 2.0f / 3.0f : 1.0f;
    r->beats[k] = beats;
    snprintf(a, sizeof a, "%s_sync", pre);
    int synced = (i = key_ix(a)) < 0 || option_of(e, i) == 1;
    snprintf(a, sizeof a, "%s_rate", pre);
    r->bpm[k] = !synced && (i = key_ix(a)) >= 0 ? real_of(e, i) : 0.0f;
    snprintf(a, sizeof a, "%s_on", pre);
    r->on[k] = (i = key_ix(a)) < 0 || option_of(e, i) == 1;
}

/* each LED's rate, from the focused Synth's params */
static void read_rates(ed_t *e, rate_ui_t *r) {
    for (int k = 0; k < NRATES; k++) {
        const lay_rate_t *q = &RATES[k];
        r->beats[k] = r->bpm[k] = r->hz[k] = 0.0f;
        r->on[k] = 1;
        if (q->kind == RL_LFO) {
            char a[16], t[64];
            snprintf(a, sizeof a, "lfo%d_rate", q->n + 1);
            int i = key_ix(a);
            if (i < 0) { r->on[k] = 0; continue; }
            param_text(e, i, t, sizeof t);
            if (strstr(t, "Hz")) r->hz[k] = (float)atof(t);
            else r->beats[k] = text_beats(t);
            r->on[k] = r->hz[k] > 0.0f || r->beats[k] > 0.0f;
        } else if (q->kind == RL_ARP) {
            step_rate(e, r, k, "arp");
        } else if (q->kind == RL_SEQ) {
            step_rate(e, r, k, "seq");
        } else {   /* RL_TEMPO: every beat */
            r->beats[k] = 1.0f;
        }
    }
}

/* is LED k lit at time t (seconds), with the host at bpm and (playing) at beat position ppq? */
static int lit(const rate_ui_t *r, int k, double t, double bpm, int playing, double ppq) {
    if (!r->on[k]) return 0;
    double period = r->beats[k] > 0.0f ? r->beats[k] * 60.0 / (r->bpm[k] > 0.0f ? r->bpm[k] : bpm)
                  : r->hz[k] > 0.0f ? 1.0 / r->hz[k] : 0.0;
    if (period <= 0.0) return 0;
    if (period < STEADY_S) return 1;
    double on = period * 0.3, phase;
    on = on < 0.045 ? 0.045 : on > 0.15 ? 0.15 : on;
    if (r->beats[k] > 0.0f && r->bpm[k] <= 0.0f && playing) phase = fmod(ppq / r->beats[k], 1.0) * period;
    else phase = fmod(t, period);
    if (phase < 0.0) phase += period;
    return phase < on;
}

RECT rate_led_rect(int k) {
    int cx = sc(RATES[k].x), cy = sc(RATES[k].y), h = sc(6) + 1;
    RECT r = {cx - h, cy - h, cx + h, cy + h};
    return r;
}

void rate_leds_tick(ed_t *e) {
    rate_ui_t *r = &e->rl;
    double t = now_s(), bpm = 120.0, ppq = 0.0;
    int playing = 0;
    if (t - r->read_at > READ_S || r->read_at <= 0.0) {
        read_rates(e, r);
        r->read_at = t;
    }
    if (e->host) {   /* the host's tempo and, while it plays, where its beat is */
        const time_info_t *ti = (const time_info_t *)e->host(e->fx, audioMasterGetTime, 0,
                                                             kPpqPosValid | kTempoValid, NULL, 0.0f);
        if (ti && (ti->flags & kTempoValid) && ti->tempo >= 10.0 && ti->tempo <= 999.0) bpm = ti->tempo;
        if (ti && (ti->flags & kTransportPlaying) && (ti->flags & kPpqPosValid)) { playing = 1; ppq = ti->ppqPos; }
    }
    float own;
    if (settings_tempo(&own)) { bpm = own; playing = 0; }   /* TEMPO OWN (2026-10-30): the Synths' own tempo, free */
    unsigned now = 0;
    for (int k = 0; k < NRATES; k++) {
        int on;
        if (RATES[k].kind == RL_SEQ && r->on[k] && e->sq.play >= 0) {   /* running: a flash on each step it plays */
            if (e->sq.play != r->seq_step) { r->seq_step = e->sq.play; r->seq_at = t; }
            double period = r->beats[k] * 60.0 / (r->bpm[k] > 0.0f ? r->bpm[k] : bpm), f = period * 0.3;
            f = f < 0.045 ? 0.045 : f > 0.15 ? 0.15 : f;
            on = period < STEADY_S || t - r->seq_at < f;
        } else {
            if (RATES[k].kind == RL_SEQ) r->seq_step = -1;
            on = lit(r, k, t, bpm, playing, ppq);
        }
        now |= (unsigned)on << k;
    }
    unsigned changed = now ^ r->lit;
    r->lit = now;
    for (int k = 0; k < NRATES && e->wnd; k++)
        if (changed >> k & 1u) {
            RECT b = rate_led_rect(k);
            InvalidateRect(e->wnd, &b, FALSE);
        }
}

void rate_leds_draw(ed_t *e, const RECT *clip) {
    for (int k = 0; k < NRATES; k++) {
        RECT b = rate_led_rect(k), o;
        if (!IntersectRect(&o, &b, clip)) continue;
        skin_led((float)sc(RATES[k].x), (float)sc(RATES[k].y), 3.0f * L.s + 0.5f, (int)(e->rl.lit >> k & 1u));
    }
}

void rate_leds_static(HDC dc) {
    for (int k = 0; k < NRATES; k++) {
        if (!RATES[k].label[0]) continue;
        RECT r = {sc(RATES[k].x) + sc(6), sc(RATES[k].y) - sc(7), sc(RATES[k].x) + sc(60), sc(RATES[k].y) + sc(7)};
        if (!skin_text(&r, RATES[k].label, font_label, T_LABEL, DT_LEFT | DT_VCENTER | DT_SINGLELINE)) {
            SelectObject(dc, font_label);
            SetTextColor(dc, T_LABEL);
            SetBkMode(dc, TRANSPARENT);
            DrawTextA(dc, RATES[k].label, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
}
