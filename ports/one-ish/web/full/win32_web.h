/* The web build's Win32 internals shared by win32.c, host.c and the page (site/full.js reads pres_t and sends EV_*). */
#pragma once
#include <stdint.h>
#include "windows.h"

/* what the editor thread publishes after painting: the page copies [x0,y0)-(x1,y1) of px to its canvas when seq is
 * even and differs from ack, then sets ack = seq (byte offsets: px 0, w 4, h 8, seq 12, ack 16, x0 20 .. y1 32;
 * paint_ms 36: the last paints' average time, max_ms 40: the slowest of the last second) */
typedef struct {
    uint32_t *px;
    int32_t w, h;
    int32_t seq, ack;
    int32_t x0, y0, x1, y1;
    float paint_ms, max_ms;
} pres_t;

/* web_event() types (site/full.js): mouse x, y in panel pixels; a = buttons (MOVE) or button (DOWN, UP: 0 left,
 * 1 middle, 2 right, 3 back, 4 forward) or wheel delta (120 a notch); b = modifier keys (1 Shift, 2 Ctrl, 4 Alt).
 * KEYDOWN/KEYUP: x = Windows key code, y = the character typed (0: none). RESULT: x = token, y = value,
 * a = text (malloc'd by the page, freed here). SNAP: a finger is about to press at x, y: a = how near a cable's part it
 * may land on it, in panel pixels (touch.c); the press is shifted by that until its UP */
enum { EV_MOVE = 1, EV_DOWN, EV_UP, EV_WHEEL, EV_LEAVE, EV_KEYDOWN, EV_KEYUP, EV_RESULT, EV_BLUR, EV_SNAP };
/* Module.mwUI(kind, token, json) */
enum { UI_MENU = 1, UI_MSGBOX, UI_DIALOG, UI_OPEN, UI_SAVE };

void web_event(int type, int x, int y, int a, int b);
HWND win32_root(int w, int h);
void win32_run(void);
pres_t *win32_pres(void);
const char *web_path(const char *in, char *out, size_t n);
void touch_init(void *plugin);   /* touch.c */
void touch_state(int32_t out[4]);
void touch_snap(int x, int y, int radius, int *dx, int *dy);
void web_files_changed(void);   /* host.c: something was written; save the browser's copy soon */
