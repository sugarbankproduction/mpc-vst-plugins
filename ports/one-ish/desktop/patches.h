/* User patches for the desktop editor (patches.c): the owner's own sounds as files, browsed together with the factory
 * presets on the centre screen's PATCHES page. Included by editor.h (ed_t holds a patches_t); the functions that take
 * an ed_t are declared at the end of editor.h. File format: docs/PATCHES.md and the top of patches.c. */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum { PATCH_NAME_MAX = 32, PATCH_STATE_MAX = 8192 };   /* a name's characters; the engine's state and the chunk */
enum { SCR_PATCHES = -10 };                             /* the centre screen showing the patch list (see SCR_MATRIX) */
enum { Z_PATCH_ACT = 100300, Z_PATCH_ITEM = 200000 };   /* hit codes: an action button (+ PA_), a list entry (+ pos) */
enum { PA_SAVE, PA_SAVE_AS, PA_RENAME, PA_DELETE, PA_IMPORT, PA_EXPORT, PA_COUNT };
enum { effGetChunk = 23, effSetChunk = 24 };              /* VST2 opcodes editor.h doesn't name */
enum { BANK_NONE, BANK_FACTORY, BANK_USER };            /* where the current sound came from */
/* results of the patch operations (patches.c) */
enum { PE_OK = 1, PE_FAIL = 0, PE_EXISTS = -1, PE_BADNAME = -2, PE_NOT_USER = -3, PE_BADFILE = -4 };

/* load_test drives the operations through effVendorSpecific: index TEST_PATCH, value = PT_ op, ptr = a 1 KB text
 * buffer (a name or a path in, a title or a message out), opt = a number (a step, or 1 to allow overwriting) */
#define TEST_PATCH 0x4F315054
enum { PT_SAVE = 1, PT_SAVE_AS, PT_RENAME, PT_DELETE, PT_IMPORT, PT_EXPORT, PT_LOAD, PT_STEP, PT_TITLE, PT_EDITED,
       PT_COUNT, PT_DIR, PT_MESSAGE, PT_LOAD_POS, PT_POS };

#ifdef NPARAMS   /* the editor (params.h); load_test only needs the numbers above */
typedef struct {
    int bank, prog;                       /* BANK_; the factory program when BANK_FACTORY */
    char name[PATCH_NAME_MAX + 1];         /* the user patch's name (BANK_USER), or the unsaved sound's */
    int forced_edit;                       /* edited, whatever the values say (a project saved with edits, a deleted file) */
    int edited;                            /* the last check (patches_tick) */
    float base[NPARAMS];                   /* the values when the patch was loaded or saved: edited = any differs */
    char (*list)[PATCH_NAME_MAX + 1];      /* the user patches in the folder, A to Z (UI thread only) */
    int count, cap;
    char msg[240];                         /* the last operation's result, shown on the screen for a while */
    DWORD msg_at;
    char chunk[PATCH_STATE_MAX];           /* effGetChunk: the engine's state plus the patch's identity */
} patches_t;
#endif
