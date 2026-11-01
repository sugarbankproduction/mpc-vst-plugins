/* web build: see windows.h */
#pragma once
#include "windows.h"
typedef UINT_PTR (*LPOFNHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef struct {
    DWORD lStructSize;
    HWND hwndOwner;
    HINSTANCE hInstance;
    LPCSTR lpstrFilter;
    LPSTR lpstrCustomFilter;
    DWORD nMaxCustFilter, nFilterIndex;
    LPSTR lpstrFile;
    DWORD nMaxFile;
    LPSTR lpstrFileTitle;
    DWORD nMaxFileTitle;
    LPCSTR lpstrInitialDir, lpstrTitle;
    DWORD Flags;
    WORD nFileOffset, nFileExtension;
    LPCSTR lpstrDefExt;
    LPARAM lCustData;
    LPOFNHOOKPROC lpfnHook;
    LPCSTR lpTemplateName;
    void *pvReserved;
    DWORD dwReserved, FlagsEx;
} OPENFILENAMEA;
#define OFN_READONLY 0x1
#define OFN_OVERWRITEPROMPT 0x2
#define OFN_HIDEREADONLY 0x4
#define OFN_NOCHANGEDIR 0x8
#define OFN_ALLOWMULTISELECT 0x200
#define OFN_PATHMUSTEXIST 0x800
#define OFN_FILEMUSTEXIST 0x1000
#define OFN_EXPLORER 0x80000
/* the page's file picker (open: a file the user picks is copied into the browser's file system; save: written there,
 * then handed to the browser as a download) */
BOOL GetOpenFileNameA(OPENFILENAMEA *o);
BOOL GetSaveFileNameA(OPENFILENAMEA *o);
DWORD CommDlgExtendedError(void);
