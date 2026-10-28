/* The desktop editor's materials and 3D parts (skin.c): brushed aluminium, black anodised panels, ash wood, glass,
 * lit knobs, push buttons, LEDs, keys and wheels. All of it is computed here pixel by pixel (no image files): the
 * static layer (wood, frames, panels, scales, the unplayed keyboard) once per window size into an offscreen bitmap,
 * the moving parts as small sprites rendered once per size (supersampled) and blended in when the window is painted.
 *
 * Use (editor_draw.c): paint() calls skin_frame() for an offscreen drawing surface that already holds the static
 * layer (baked by its callback the first time), draws the moving parts on it with GDI, GDI+ (gfx.h) and the skin_
 * functions below, then skin_present() copies it to the window. The skin_ functions write pixels directly, so they
 * flush pending GDI / GDI+ drawing first (skin_bind tells them which GDI+ surface is drawing). UI thread only. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

/* lifetime: one per open editor window; the caches go when the last window closes */
void skin_acquire(void);
void skin_release(void);

/* the frame: an offscreen DC (a 32-bit DIB) for a w x h window at scale s, the static layer copied in over clip.
 * bake(dc) draws the static layer the first time (or after a size change). NULL if it can't be made. */
typedef void (*skin_bake_fn)(HDC dc);
HDC skin_frame(int w, int h, float s, const RECT *clip, skin_bake_fn bake);
void skin_present(HDC out, const RECT *clip);
void skin_bind(void *gdiplus_graphics);   /* the GDI+ surface drawing now (flushed before pixel writes) or NULL */
double skin_bake_ms(void);                /* how long the last static layer took to make */
void skin_clip_push(const RECT *r);       /* the skin_ pixel writes only inside r, until skin_clip_pop() */
void skin_clip_pop(void);

/* materials, painted straight into the surface (the static layer while baking, else the frame) */
void skin_chassis(int x, int y, int w, int h);
void skin_wood(int x, int y, int w, int h, int vertical, unsigned seed);
void skin_metal_frame(int x, int y, int w, int h, int border);    /* raised brushed aluminium rim, bevelled */
void skin_metal_plate(int x, int y, int w, int h);                /* a flat brushed aluminium strip (ribbon rim) */
void skin_panel(int x, int y, int w, int h);                      /* black anodised, fine grain, vignette */
void skin_shadow(float x, float y, float w, float h, float soft, float alpha);   /* soft shadow of a box */
void skin_recess(int x, int y, int w, int h, float depth);        /* an inner shadow: a well sunk into the panel */
void skin_lcd(int x, int y, int w, int h);                        /* a display's dark glass */
void skin_glare(int x, int y, int w, int h);                      /* the glass's faint reflection, over its content */
void skin_touch(int x, int y, int w, int h);                      /* a matte touch surface (pad, ribbon) */
void skin_white_key(float x0, float x1, int top, int h, int down);
void skin_black_key(float x0, float x1, int top, int h, int down);
void skin_key_shadow(float x0, float x1, int top, int h);         /* a black key's cast shadow on the white keys */
void skin_wheel(int x, int y, int w, int h, float pos);           /* a ridged rubber wheel, pos -1..1 */

/* text: GDI's ClearType text, drawn once per string, font, format and box size and kept; blended in with colour c.
 * 0 if it couldn't (draw it with DrawText then). */
int skin_text(const RECT *r, const char *s, HFONT f, COLORREF c, UINT fmt);

/* sprites */
enum { SK_BLACK, SK_RED, SK_CONSOLE };                         /* button kinds */
enum { SK_UP, SK_HOT, SK_ON };                                  /* states: SK_ON = latched in and lit */
void skin_knob(float cx, float cy, float r, float angle_deg, int big);   /* the body; the pointer is drawn on top */
void skin_button(float x, float y, float w, float h, int kind, int state);
enum { SK_LED_RED = 2 };                                       /* skin_led's on: 0 off, 1 warm white, 2 red */
void skin_led(float cx, float cy, float r, int on);
