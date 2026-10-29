/* The SETTINGS page and the settings file (settings.c): global settings that are not part of any sound, after the
 * Moog One's SETTINGS screen (manual p124-139, p144-148). Included by editor.h (ed_t holds a settings_ui_t); the
 * functions that take an ed_t are declared at the end of editor.h. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum { SCR_SETTINGS = -12 };          /* the centre screen showing the SETTINGS page (see SCR_MATRIX) */
#define Z_SET_BASE 650000             /* its hit codes: Z_SET_BASE and up (settings.c) */
#define Z_SET_END 651000
enum { ST_GLOBAL, ST_LIBRARY, ST_TUNING, ST_TABS };   /* its tabs */

/* load_test drives it through effVendorSpecific: index TEST_SETTINGS, value = STT_ op, ptr = a 1 KB text buffer */
#define TEST_SETTINGS 0x4F315354
enum { STT_GET = 1,   /* buf <- the settings as the file holds them */
       STT_SET,       /* buf "key=value" (as in the file): set, apply to every instance, save; 1 done */
       STT_RESTORE,   /* RESTORE DEFAULT SETTINGS, without asking */
       STT_PATH,      /* buf <- the settings file's path */
       STT_RELOAD,    /* read the file again (as a new process would) and apply it */
       STT_CUE,       /* the patch the PRESET knob points at without autoload (-1: none) */
       STT_CUE_LOAD,  /* load it (as a click on the PRESET knob); 1 done */
       STT_TAB,       /* show the page at tab opt */
       STT_COUNT_ };

typedef struct {
    int tab;                  /* ST_ */
    int cue, cue_base;        /* PRESET AUTOLOAD off: the patch the knob points at (-1 none), and the one loaded then */
    char msg[240];            /* the last action's result, shown on the screen for a while */
    DWORD msg_at;
} settings_ui_t;
