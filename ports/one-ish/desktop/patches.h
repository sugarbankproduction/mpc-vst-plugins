/* User patches for the desktop editor (patches.c): the owner's own sounds as files, browsed together with the factory
 * presets on the centre screen's PATCHES page. Included by editor.h (ed_t holds a patches_t); the functions that take
 * an ed_t are declared at the end of editor.h. File format: docs/PATCHES.md and the top of patches.c. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef VST2_CHUNK_MAX
#define VST2_CHUNK_MAX 8192   /* the wrapper's chunk (wrapper/vst2_wrap.c); desktop/build.sh sets the desktop's */
#endif
enum { PATCH_NAME_MAX = 32, PATCH_STATE_MAX = VST2_CHUNK_MAX };   /* a name's characters; the engine's state and the chunk */
enum { SCR_PATCHES = -10 };                             /* the centre screen showing the patch list (see SCR_MATRIX) */
enum { Z_PATCH_ACT = 100300, Z_PATCH_ITEM = 200000 };   /* hit codes: an action button (+ PA_), a list entry (+ pos) */
enum { Z_PATCH_CAT = 100450, PATCH_CATS = 32 };          /* (2026-10-30) a category tab (+ k; 0 is ALL) */
/* the library's pages (library.c): performance SET, ALL SETS, ARRANGE (sets), SNAPSHOTS, TAGS; and their hit codes:
 * the page tabs, the buttons on each page, the console's COMPARE and SNAPSHOT, the list entries */
enum { SCR_SET = -12, SCR_SETS = -13, SCR_ARRANGE = -14, SCR_SNAPS = -15, SCR_TAGS = -16 };
enum { Z_LIB_TAB = 100320, Z_SNAP_ACT = 100340, Z_TAG_ACT = 100360, Z_ARR_ACT = 100380, Z_SET_BANK = 100400,
       Z_SET_SIDE = 100410, Z_COMPARE = 100430, Z_SNAPSHOT = 100431,
       Z_SNAP_ITEM = 210000, Z_TAG_ITEM = 220000 /* + 1000 * kind + tag */, Z_SET_SLOT = 230000, Z_SETS_ITEM = 240000 };
/* (2026-10-31) IMPORT .M1P and REPORT: Moog One presets into user patches, the last import's report (m1p_import.c) */
enum { PA_SAVE, PA_SAVE_AS, PA_RENAME, PA_DELETE, PA_IMPORT, PA_EXPORT, PA_INTO, PA_SAVE_SYNTH, PA_M1P, PA_REPORT, PA_COUNT };
enum { effGetChunk = 23, effSetChunk = 24 };              /* VST2 opcodes editor.h doesn't name */
enum { BANK_NONE, BANK_FACTORY, BANK_USER };            /* where the current sound came from */
/* results of the patch operations (patches.c) */
enum { PE_OK = 1, PE_FAIL = 0, PE_EXISTS = -1, PE_BADNAME = -2, PE_NOT_USER = -3, PE_BADFILE = -4 };

/* load_test drives the operations through effVendorSpecific: index TEST_PATCH, value = PT_ op, ptr = a 1 KB text
 * buffer (a name or a path in, a title or a message out), opt = a number (a step, or 1 to allow overwriting) */
#define TEST_PATCH 0x4F315054
enum { PT_SAVE = 1, PT_SAVE_AS, PT_RENAME, PT_DELETE, PT_IMPORT, PT_EXPORT, PT_LOAD, PT_STEP, PT_TITLE, PT_EDITED,
       PT_COUNT, PT_DIR, PT_MESSAGE, PT_LOAD_POS, PT_POS, PT_INTO_POS, PT_SAVE_SYNTH,
       /* (2026-10-31, m1p_import.c) PICK: the files ('|' between) the next IMPORT .M1P click takes, no dialog ("" none;
        * while set, REPORT opens nothing); IMPORT: a .m1p file or a folder of them, opt = DUPLICATE NAMES (parts.h
        * DUP_), returns how many were imported; REPORT: the report's path, 1 when it is there; OPENED: what REPORT
        * opened */
       PT_M1P_PICK, PT_M1P_IMPORT, PT_M1P_REPORT, PT_M1P_OPENED };
/* ... and the library (library.c): index TEST_LIB, value = LT_ op, ptr = a 1 KB text buffer out, opt = a number in.
 * LT_ZONE: where hit code opt is on the window now, (x | y << 16) in pixels, or -1 */
#define TEST_LIB 0x4F314C42
enum { LT_UNDO = 1, LT_REDO, LT_STEPS, LT_COMPARING, LT_SNAPS, LT_AUTOS, LT_SNAP_INFO, LT_SNAP_SEL, LT_ZONE, LT_SCREEN,
       LT_META, LT_VIEW, LT_SETS, LT_SLOT, LT_SET_INFO, LT_IDLE, LT_TAGS, LT_HOME, LT_LABEL, LT_NOTES_SHOWN, LT_TICK, LT_FORGET,
       LT_CATS /* (2026-10-30) "sub/subs|ALL=n;Pad=n;..." the PATCHES page's page and its category tabs */ };

#ifdef NPARAMS   /* the editor (params.h); load_test only needs the numbers above */
typedef struct {
    int bank, prog;                       /* BANK_; the factory program when BANK_FACTORY */
    char name[PATCH_NAME_MAX + 1];         /* the user patch's name (BANK_USER), or the unsaved sound's */
    int forced_edit;                       /* edited, whatever the values say (a project saved with edits, a deleted file) */
    int edited;                            /* the last check (patches_tick) */
    int into;                              /* INTO SYNTH: a pick from the list loads into the focused Synth only */
    float base[NPARAMS];                   /* the values when the patch was loaded or saved: edited = any differs */
    char (*list)[PATCH_NAME_MAX + 1];      /* the user patches in the folder, A to Z (UI thread only) */
    int count, cap;
    char msg[240];                         /* the last operation's result, shown on the screen for a while */
    DWORD msg_at;
    char chunk[PATCH_STATE_MAX];           /* effGetChunk: the engine's state plus the patch's identity */
} patches_t;
#endif
