/* The web build's help for fingers in the CABLES view (the editor's own cables.c is unchanged; it is asked through the
 * same effVendorSpecific calls load_test uses). Runs on the editor thread only, never touches sound.
 *   - touch_state: what the page needs to know, polled by host.c's timer: cables shown, the drag going on, ...
 *   - touch_snap: a jack, a cable's middle knob or end dot, or a button of its box, is small for a finger (5 to 11 panel
 *     pixels' radius at the panel's base size): a press this near one lands on it (win32.c shifts the press by the
 *     answer, for as long as it lasts). Nothing else moves: the "+" dots stay exact, so a knob under them still turns. */
#include <stdlib.h>
#include "editor.h"
#include "win32_web.h"

static AEffect *fx;
void touch_init(void *plugin) { fx = plugin; }

static intptr_t ask(int op, int arg) { return fx->dispatcher(fx, VENDOR_SPECIFIC, TEST_CB, op, NULL, (float)arg); }

void touch_state(int32_t out[4]) {
    if (!fx) return;
    out[0] = (int32_t)ask(CBT_ON, 0);
    out[1] = out[0] ? (int32_t)ask(CBT_DRAG, 0) : 0;
    out[2] = (int32_t)ask(CBT_N, 0);
    out[3] = out[0] ? (int32_t)ask(CBT_SEL, 0) : -1;
}

static long best;
static int snap_x, snap_y, *out_dx, *out_dy;
static void try_at(intptr_t xy, int weight) {   /* (distances squared x weight) */
    if (xy < 0) return;
    int ex = (int)(xy & 0xFFFF) - snap_x, ey = (int)(xy >> 16 & 0xFFFF) - snap_y;
    long d = ((long)ex * ex + (long)ey * ey) * weight;
    if (d < best) { best = d; *out_dx = ex; *out_dy = ey; }
}
void touch_snap(int x, int y, int radius, int *dx, int *dy) {
    *dx = *dy = 0;
    if (!fx || radius <= 0 || !ask(CBT_ON, 0)) return;
    enum { PLAIN = 2, JACK = 1 };   /* a jack wins over an end dot beside it: a new cable, not a re-patch */
    best = (long)PLAIN * radius * radius + 1;
    snap_x = x;
    snap_y = y;
    out_dx = dx;
    out_dy = dy;
    try_at(ask(CBT_X_XY, 0), PLAIN);
    try_at(ask(CBT_OFF_XY, 0), PLAIN);
    for (int k = CBX_BG + 1; k < CBX_COUNT; k++) try_at(ask(CBT_BOX_XY, k), PLAIN);
    int n = (int)ask(CBT_N, 0);
    for (int c = 0; c < n; c++) {
        try_at(ask(CBT_KNOB_XY, c), PLAIN);
        try_at(ask(CBT_WIRE_XY, c), PLAIN);
        try_at(ask(CBT_HANDLE_XY, 2 * c), PLAIN);
        try_at(ask(CBT_HANDLE_XY, 2 * c + 1), PLAIN);
    }
    for (int s = 1; s < 128; s++) try_at(ask(CBT_JACK_XY, s), JACK);
}
