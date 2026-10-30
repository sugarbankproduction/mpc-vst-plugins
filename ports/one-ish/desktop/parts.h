/* (2026-10-30) Library parts and user libraries for the desktop editor (parts.c; docs/PATCHES.md): EFFECT PRESETS
 * (each effect slot's settings by name), MATRIX PRESETS (a Synth's whole matrix, added to its free rows or replacing
 * them), the SYNTHS' NAMES (timbre names, saved with the sound by the engine), USER LIBRARIES (separate collections of
 * user patches) and DUPLICATE NAMES (what IMPORT does with a same-named patch). Included by editor.h (ed_t holds a
 * parts_ui_t); the functions that take an ed_t are declared at the end of editor.h. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum { SCR_PARTS = -17 };   /* the library's PARTS page (a tab beside TAGS; library.c's pages are -12 .. -16) */
enum { SYNNAME_MAX = 12 };  /* a Synth's name: characters (as the engine's SYN_NAME_MAX) */
#define Z_PARTS 695000      /* hit codes: Z_PARTS and up (parts.c) */
#define Z_PARTS_END 696000
enum { ZP_SLOT = Z_PARTS,           /* + PS_: the PARTS page's slot buttons */
       ZP_ACT = Z_PARTS + 10,       /* + PA2_: its action buttons */
       ZP_MORE = Z_PARTS + 30,      /* PRESETS on an effect's MORE page */
       ZP_NAME = Z_PARTS + 40,      /* + c: the SETUP page's Synth heads (a click names the Synth) */
       ZP_LIB = Z_PARTS + 50,       /* + PL_: the SETTINGS page's LIBRARY rows */
       ZP_ITEM = Z_PARTS + 100 };   /* + k: entry k of the PARTS page's list */
enum { PS_SFX, PS_FX1, PS_FX2, PS_MTX, PS_COUNT };   /* the slots: the Synth effect, master FX 1 and 2, the matrix */
enum { DUP_ASK, DUP_OVERWRITE, DUP_RENAME, DUP_IGNORE, DUP_COUNT };   /* DUPLICATE NAMES */

/* load_test: effVendorSpecific index TEST_PARTS, value = PAT_ op, ptr = a 1 KB text buffer, opt = a number */
#define TEST_PARTS 0x4F315041
enum { PAT_ZONE = 1,   /* zone opt on the window now: its middle, x | y << 16 (-1: not shown) */
       PAT_SLOT,       /* the PARTS page's slot (PS_) */
       PAT_COUNT,      /* entries in its list now */
       PAT_ITEM,       /* entry opt: buf <- "name|type|F or U|rows|when|notes" */
       PAT_SEL,        /* the entry selected (-1: none) */
       PAT_NAME,       /* buf <- Synth opt's name ("" none) */
       PAT_SPACE,      /* buf <- the user library in use ("" the default) */
       PAT_SPACES,     /* buf <- every library, "|" between ("Default" first); returns how many */
       PAT_DUP,        /* DUPLICATE NAMES now (DUP_) */
       PAT_MESSAGE,    /* buf <- the last message */
       PAT_DIR,        /* buf <- the folder of the effect presets (opt 0) or matrix presets (1) */
       PAT_IMPORT,     /* buf: a patch file's path: IMPORT it as the IMPORT button would, after the file dialog */
       PAT_ANSWER,     /* opt: the answer COPY SOUND TO's menu gives next (1-based; 0: show the menu) */
       PAT_LIBDIR };   /* buf <- the folder of library opt (0 the default) */

typedef struct pitem {      /* an effect or matrix preset in the PARTS page's list */
    char name[33];
    char type[32];          /* an effect's type ("" a matrix) */
    char when[24];          /* when it was saved */
    char notes[200];
    int factory;            /* one of Moo Won's own (not a file; can't be deleted) */
    int rows;               /* a matrix's rows */
} pitem_t;

typedef struct {
    int slot;               /* PS_ */
    int sel;                /* the entry selected (-1: none) */
    int all_types;          /* the list shows every type's effect presets, not only the slot's type */
    pitem_t *it;            /* the list (read again whenever the page opens or a button acts) */
    int n, cap;
    char shown[3][SYNNAME_MAX + 1];   /* the names last painted (the timer repaints when they change) */
    char msg[240];          /* the last action's result, for the help line */
    DWORD msg_at;
    int answer;             /* load_test: COPY SOUND TO's menu choice (0: show the menu) */
} parts_ui_t;
