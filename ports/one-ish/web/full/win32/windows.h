/* The web build's stand-in for <windows.h> (web/full): just the part of Win32, GDI and the shell that the desktop editor
 * (the .c files in desktop/) uses, so that code compiles for the browser unchanged. win32.c (windows, messages, timers, menus,
 * dialogs, files), gdi.c (device contexts, bitmaps, text, regions) and gdiplus.c (the flat GDI+ calls in desktop/gfx.h)
 * implement it, drawing into memory with plutovg; the page (site/full.js) shows the result and sends the mouse and keys.
 * Values of constants are this file's own unless code depends on the real ones (messages, keys, flags are internal). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>

#define WINAPI
#define CALLBACK
#define APIENTRY
#define __stdcall
#define __declspec(x)
#define FAR
#define NEAR
#define CONST const

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef uint32_t DWORD;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef int64_t INT64;
typedef uint64_t UINT64;
typedef uint64_t ULONGLONG;
typedef int64_t LONGLONG;
typedef unsigned int UINT;
typedef int INT;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef short SHORT;
typedef unsigned short USHORT;
typedef char CHAR;
typedef wchar_t WCHAR;
typedef float FLOAT;
typedef intptr_t INT_PTR, LONG_PTR, LRESULT, LPARAM;
typedef uintptr_t UINT_PTR, ULONG_PTR, DWORD_PTR, WPARAM;
typedef long HRESULT;
typedef DWORD COLORREF;
typedef void *LPVOID, *PVOID, *HANDLE;
typedef const void *LPCVOID;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef wchar_t *LPWSTR;
typedef const wchar_t *LPCWSTR;
typedef DWORD *LPDWORD;
typedef BYTE *LPBYTE;
typedef WORD ATOM;
typedef int64_t __int64;

#define DECLARE_HANDLE(n) typedef struct n##__ *n
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HKEY);
typedef HINSTANCE HMODULE;
typedef HICON HCURSOR;
typedef void *HGDIOBJ;
typedef HGDIOBJ HBRUSH, HPEN, HFONT, HBITMAP, HRGN;
typedef intptr_t (*FARPROC)(void);

#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_FAIL ((HRESULT)0x80004005L)
#define SUCCEEDED(h) ((HRESULT)(h) >= 0)
#define FAILED(h) ((HRESULT)(h) < 0)
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFFu
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80

#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xFFFF))
#define HIWORD(l) ((WORD)(((DWORD_PTR)(l) >> 16) & 0xFFFF))
#define LOBYTE(w) ((BYTE)((DWORD_PTR)(w) & 0xFF))
#define MAKELONG(a, b) ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKEINTRESOURCEA(i) ((LPSTR)(ULONG_PTR)((WORD)(i)))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(c) (LOBYTE(c))
#define GetGValue(c) (LOBYTE(((WORD)(c)) >> 8))
#define GetBValue(c) (LOBYTE((c) >> 16))
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

typedef struct { LONG left, top, right, bottom; } RECT, *LPRECT, *PRECT;
typedef const RECT *LPCRECT;
typedef struct { LONG x, y; } POINT, *LPPOINT, *PPOINT;
typedef struct { LONG cx, cy; } SIZE, *LPSIZE;
typedef struct { short left, top, right, bottom; } SMALL_RECT;
typedef union { struct { DWORD LowPart; LONG HighPart; }; LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
typedef struct { FLOAT eM11, eM12, eM21, eM22, eDx, eDy; } XFORM;

/* ---- errors, threads, time, system ---- */
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_DATA 13
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_WRITE_PROTECT 19
#define ERROR_SHARING_VIOLATION 32
#define ERROR_LOCK_VIOLATION 33
#define ERROR_HANDLE_DISK_FULL 39
#define ERROR_ALREADY_EXISTS 183
#define ERROR_DISK_FULL 112
DWORD GetLastError(void);
void SetLastError(DWORD e);
DWORD GetTickCount(void);
ULONGLONG GetTickCount64(void);
BOOL QueryPerformanceCounter(LARGE_INTEGER *c);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *f);
void GetLocalTime(SYSTEMTIME *t);
void GetSystemTime(SYSTEMTIME *t);
void Sleep(DWORD ms);
DWORD GetCurrentThreadId(void);
LONG InterlockedExchange(volatile LONG *t, LONG v);
LONG InterlockedIncrement(volatile LONG *t);
LONG InterlockedDecrement(volatile LONG *t);
LONG InterlockedCompareExchange(volatile LONG *t, LONG v, LONG cmp);
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buf, DWORD size);
BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value);
void OutputDebugStringA(LPCSTR s);
#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 2
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 4
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x800
HMODULE GetModuleHandleA(LPCSTR name);
BOOL GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE *out);
DWORD GetModuleFileNameA(HMODULE m, LPSTR buf, DWORD size);
HMODULE LoadLibraryA(LPCSTR name);
HMODULE LoadLibraryExA(LPCSTR name, HANDLE f, DWORD flags);
FARPROC GetProcAddress(HMODULE m, LPCSTR name);
BOOL FreeLibrary(HMODULE m);
typedef struct { DWORD dwOSVersionInfoSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformId; WCHAR szCSDVersion[128]; } OSVERSIONINFOW;
#define SPI_GETWORKAREA 0x30
BOOL SystemParametersInfoA(UINT action, UINT p, void *pv, UINT win_ini);
int GetSystemMetrics(int i);
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXDOUBLECLK 36
#define SM_CYDOUBLECLK 37
UINT GetDoubleClickTime(void);
BOOL MessageBeep(UINT t);

/* ---- files (on the browser's own file system: win32.c) ---- */
typedef struct {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
    CHAR cFileName[MAX_PATH];
    CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA;
typedef struct {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;
typedef enum { GetFileExInfoStandard } GET_FILEEX_INFO_LEVELS;
HANDLE FindFirstFileA(LPCSTR pattern, WIN32_FIND_DATAA *d);
BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *d);
BOOL FindClose(HANDLE h);
DWORD GetFileAttributesA(LPCSTR path);
BOOL GetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS lvl, void *out);
BOOL CreateDirectoryA(LPCSTR path, void *sec);
BOOL RemoveDirectoryA(LPCSTR path);
BOOL DeleteFileA(LPCSTR path);
BOOL MoveFileA(LPCSTR from, LPCSTR to);
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
BOOL MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags);
BOOL CopyFileA(LPCSTR from, LPCSTR to, BOOL fail_if_exists);
DWORD GetFullPathNameA(LPCSTR path, DWORD size, LPSTR buf, LPSTR *file_part);
DWORD GetTempPathA(DWORD size, LPSTR buf);
DWORD GetCurrentDirectoryA(DWORD size, LPSTR buf);
LONG CompareFileTime(const FILETIME *a, const FILETIME *b);
#define CSIDL_PERSONAL 5
#define CSIDL_APPDATA 0x1A
#define CSIDL_FLAG_CREATE 0x8000
#define SHGFP_TYPE_CURRENT 0
HRESULT SHGetFolderPathA(HWND w, int csidl, HANDLE token, DWORD flags, LPSTR path);
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOW 5
HINSTANCE ShellExecuteA(HWND w, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show);

/* ---- windows and messages ---- */
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef INT_PTR (*DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef void (*TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);
typedef struct {
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra, cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName, lpszClassName;
} WNDCLASSA;
typedef struct {
    LPVOID lpCreateParams;
    HINSTANCE hInstance;
    HMENU hMenu;
    HWND hwndParent;
    int cy, cx, y, x;
    LONG style;
    LPCSTR lpszName, lpszClass;
    DWORD dwExStyle;
} CREATESTRUCTA;
typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; BYTE rgbReserved[32]; } PAINTSTRUCT;
typedef struct { DWORD cbSize, dwFlags; HWND hwndTrack; DWORD dwHoverTime; } TRACKMOUSEEVENT;
#define TME_LEAVE 2
#define HOVER_DEFAULT 0xFFFFFFFFu
BOOL TrackMouseEvent(TRACKMOUSEEVENT *t);

#define CS_DBLCLKS 0x8
#define CS_HREDRAW 0x2
#define CS_VREDRAW 0x1
#define WS_CHILD 0x40000000L
#define WS_VISIBLE 0x10000000L
#define WS_POPUP 0x80000000L
#define WS_CAPTION 0x00C00000L
#define WS_SYSMENU 0x00080000L
#define WS_BORDER 0x00800000L
#define WS_TABSTOP 0x00010000L
#define WS_GROUP 0x00020000L
#define WS_VSCROLL 0x00200000L
#define WS_HSCROLL 0x00100000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_EX_TOOLWINDOW 0x80L
#define WS_EX_TOPMOST 0x8L
#define WS_EX_NOACTIVATE 0x08000000L
#define DS_MODALFRAME 0x80L
#define DS_CENTER 0x800L
#define DS_SETFONT 0x40L
#define DS_SHELLFONT (DS_SETFONT | 0x8L)
#define ES_AUTOHSCROLL 0x80L
#define ES_MULTILINE 0x4L
#define ES_WANTRETURN 0x1000L
#define ES_AUTOVSCROLL 0x40L
#define BS_DEFPUSHBUTTON 0x1L
#define BS_PUSHBUTTON 0x0L
#define BS_AUTOCHECKBOX 0x3L
#define SS_LEFT 0x0L
#define CBS_DROPDOWNLIST 0x3L
#define HWND_MESSAGE ((HWND)(intptr_t)-3)
#define HWND_TOP ((HWND)0)
#define HWND_TOPMOST ((HWND)(intptr_t)-1)
#define GWLP_USERDATA (-21)
#define GWLP_HINSTANCE (-6)
#define GWLP_WNDPROC (-4)
#define DWLP_USER 16
#define DWLP_MSGRESULT 0
#define GA_PARENT 1
#define GA_ROOT 2
#define SWP_NOSIZE 0x1
#define SWP_NOMOVE 0x2
#define SWP_NOZORDER 0x4
#define SWP_NOACTIVATE 0x10
#define SWP_SHOWWINDOW 0x40
#define SWP_HIDEWINDOW 0x80
#define PRF_CLIENT 0x4
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7
#define IDC_ARROW MAKEINTRESOURCEA(32512)
#define IDC_HAND MAKEINTRESOURCEA(32649)
#define IDC_IBEAM MAKEINTRESOURCEA(32513)
#define IDC_SIZEWE MAKEINTRESOURCEA(32644)
#define IDC_SIZENS MAKEINTRESOURCEA(32645)
#define IDC_SIZEALL MAKEINTRESOURCEA(32646)
#define LoadCursor LoadCursorA
HCURSOR LoadCursorA(HINSTANCE i, LPCSTR name);
HCURSOR SetCursor(HCURSOR c);

#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_GETTEXTLENGTH 0x000E
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_ERASEBKGND 0x0014
#define WM_SETCURSOR 0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_SETFONT 0x0030
#define WM_PRINTCLIENT 0x0318
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_GETDLGCODE 0x0087
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MOUSEWHEEL 0x020A
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP 0x020C
#define WM_MOUSEHWHEEL 0x020E
#define WM_CAPTURECHANGED 0x0215
#define WM_MOUSELEAVE 0x02A3
#define WM_USER 0x0400
#define WM_APP 0x8000
#define DLGC_WANTALLKEYS 0x4
#define DLGC_WANTCHARS 0x80
#define DLGC_WANTARROWS 0x1
#define MA_NOACTIVATE 3
#define MK_LBUTTON 0x1
#define MK_RBUTTON 0x2
#define MK_SHIFT 0x4
#define MK_CONTROL 0x8
#define MK_MBUTTON 0x10
#define MK_XBUTTON1 0x20
#define MK_XBUTTON2 0x40
#define XBUTTON1 1
#define XBUTTON2 2
#define WHEEL_DELTA 120
#define GET_WHEEL_DELTA_WPARAM(wp) ((short)HIWORD(wp))
#define GET_KEYSTATE_WPARAM(wp) (LOWORD(wp))
#define GET_XBUTTON_WPARAM(wp) (HIWORD(wp))
#define EM_SETSEL 0x00B1
#define EM_LIMITTEXT 0x00C5
#define EM_SETLIMITTEXT EM_LIMITTEXT
#define BM_GETCHECK 0x00F0
#define BM_SETCHECK 0x00F1
#define BST_CHECKED 1
#define BST_UNCHECKED 0
#define CB_ADDSTRING 0x0143
#define CB_GETCURSEL 0x0147
#define CB_SETCURSEL 0x014E
#define CB_RESETCONTENT 0x014B
#define CB_GETLBTEXT 0x0148
#define CB_GETCOUNT 0x0146
#define EN_CHANGE 0x0300
#define CBN_SELCHANGE 1
#define BN_CLICKED 0

#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE

ATOM RegisterClassA(const WNDCLASSA *wc);
BOOL UnregisterClassA(LPCSTR name, HINSTANCE i);
HWND CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h, HWND parent,
                     HMENU menu, HINSTANCE inst, LPVOID param);
#define CreateWindowA(c, t, s, x, y, w, h, p, m, i, a) CreateWindowExA(0, c, t, s, x, y, w, h, p, m, i, a)
BOOL DestroyWindow(HWND w);
LRESULT DefWindowProcA(HWND w, UINT msg, WPARAM wp, LPARAM lp);
LRESULT CallWindowProcA(WNDPROC p, HWND w, UINT msg, WPARAM wp, LPARAM lp);
LRESULT SendMessageA(HWND w, UINT msg, WPARAM wp, LPARAM lp);
BOOL PostMessageA(HWND w, UINT msg, WPARAM wp, LPARAM lp);
BOOL IsWindow(HWND w);
BOOL IsWindowVisible(HWND w);
BOOL ShowWindow(HWND w, int cmd);
BOOL EnableWindow(HWND w, BOOL on);
HWND GetParent(HWND w);
HWND GetAncestor(HWND w, UINT flags);
HWND GetDesktopWindow(void);
HWND GetForegroundWindow(void);
BOOL SetForegroundWindow(HWND w);
HWND GetFocus(void);
HWND SetFocus(HWND w);
HWND GetActiveWindow(void);
HWND SetActiveWindow(HWND w);
HWND WindowFromDC(HDC dc);
HWND WindowFromPoint(POINT p);
LONG_PTR GetWindowLongPtrA(HWND w, int idx);
LONG_PTR SetWindowLongPtrA(HWND w, int idx, LONG_PTR v);
#define GetWindowLongPtr GetWindowLongPtrA
#define SetWindowLongPtr SetWindowLongPtrA
BOOL GetClientRect(HWND w, LPRECT r);
BOOL GetWindowRect(HWND w, LPRECT r);
BOOL SetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT flags);
BOOL MoveWindow(HWND w, int x, int y, int cx, int cy, BOOL repaint);
BOOL ClientToScreen(HWND w, LPPOINT p);
BOOL ScreenToClient(HWND w, LPPOINT p);
int MapWindowPoints(HWND from, HWND to, LPPOINT p, UINT n);
BOOL GetCursorPos(LPPOINT p);
BOOL SetWindowTextA(HWND w, LPCSTR s);
int GetWindowTextA(HWND w, LPSTR s, int n);
int GetWindowTextLengthA(HWND w);
BOOL InvalidateRect(HWND w, const RECT *r, BOOL erase);
BOOL ValidateRect(HWND w, const RECT *r);
BOOL UpdateWindow(HWND w);
BOOL RedrawWindow(HWND w, const RECT *r, HRGN rgn, UINT flags);
#define RDW_INVALIDATE 0x1
#define RDW_UPDATENOW 0x100
#define RDW_ALLCHILDREN 0x80
BOOL GetUpdateRect(HWND w, LPRECT r, BOOL erase);
int GetUpdateRgn(HWND w, HRGN rgn, BOOL erase);
HDC BeginPaint(HWND w, PAINTSTRUCT *ps);
BOOL EndPaint(HWND w, const PAINTSTRUCT *ps);
HDC GetDC(HWND w);
int ReleaseDC(HWND w, HDC dc);
HWND SetCapture(HWND w);
BOOL ReleaseCapture(void);
HWND GetCapture(void);
SHORT GetKeyState(int vk);
SHORT GetAsyncKeyState(int vk);
UINT_PTR SetTimer(HWND w, UINT_PTR id, UINT ms, TIMERPROC fn);
BOOL KillTimer(HWND w, UINT_PTR id);
BOOL GetMessageA(MSG *m, HWND w, UINT lo, UINT hi);
BOOL PeekMessageA(MSG *m, HWND w, UINT lo, UINT hi, UINT rm);
#define PM_REMOVE 1
#define PM_NOREMOVE 0
BOOL TranslateMessage(const MSG *m);
LRESULT DispatchMessageA(const MSG *m);
void PostQuitMessage(int code);

/* ---- menus (shown by the page: win32.c, site/full.js) ---- */
#define MF_STRING 0x0
#define MF_ENABLED 0x0
#define MF_UNCHECKED 0x0
#define MF_GRAYED 0x1
#define MF_DISABLED 0x2
#define MF_CHECKED 0x8
#define MF_POPUP 0x10
#define MF_MENUBARBREAK 0x20
#define MF_MENUBREAK 0x40
#define MF_SEPARATOR 0x800
#define MF_BYCOMMAND 0x0
#define MF_BYPOSITION 0x400
#define TPM_LEFTALIGN 0x0
#define TPM_CENTERALIGN 0x4
#define TPM_RIGHTALIGN 0x8
#define TPM_TOPALIGN 0x0
#define TPM_VCENTERALIGN 0x10
#define TPM_BOTTOMALIGN 0x20
#define TPM_LEFTBUTTON 0x0
#define TPM_RIGHTBUTTON 0x2
#define TPM_NONOTIFY 0x80
#define TPM_RETURNCMD 0x100
HMENU CreatePopupMenu(void);
HMENU CreateMenu(void);
BOOL AppendMenuA(HMENU m, UINT flags, UINT_PTR id, LPCSTR text);
BOOL InsertMenuA(HMENU m, UINT pos, UINT flags, UINT_PTR id, LPCSTR text);
BOOL DestroyMenu(HMENU m);
int GetMenuItemCount(HMENU m);
DWORD CheckMenuItem(HMENU m, UINT id, UINT flags);
BOOL EnableMenuItem(HMENU m, UINT id, UINT flags);
BOOL TrackPopupMenu(HMENU m, UINT flags, int x, int y, int reserved, HWND w, const RECT *r);
BOOL EndMenu(void);

/* ---- message boxes and dialogs (the page draws them; the editor thread waits for the answer) ---- */
#define MB_OK 0x0
#define MB_OKCANCEL 0x1
#define MB_YESNOCANCEL 0x3
#define MB_YESNO 0x4
#define MB_ICONERROR 0x10
#define MB_ICONHAND MB_ICONERROR
#define MB_ICONQUESTION 0x20
#define MB_ICONWARNING 0x30
#define MB_ICONEXCLAMATION MB_ICONWARNING
#define MB_ICONINFORMATION 0x40
#define MB_DEFBUTTON2 0x100
#define MB_TOPMOST 0x40000
#define MB_SETFOREGROUND 0x10000
#define MB_TASKMODAL 0x2000
int MessageBoxA(HWND w, LPCSTR text, LPCSTR caption, UINT type);

#pragma pack(push, 2)
typedef struct { DWORD style, dwExtendedStyle; WORD cdit; short x, y, cx, cy; } DLGTEMPLATE;
typedef struct { DWORD style, dwExtendedStyle; short x, y, cx, cy; WORD id; } DLGITEMTEMPLATE;
#pragma pack(pop)
typedef const DLGTEMPLATE *LPCDLGTEMPLATEA;
INT_PTR DialogBoxIndirectParamA(HINSTANCE i, LPCDLGTEMPLATEA t, HWND owner, DLGPROC proc, LPARAM lp);
BOOL EndDialog(HWND d, INT_PTR result);
HWND GetDlgItem(HWND d, int id);
UINT GetDlgItemTextA(HWND d, int id, LPSTR s, int n);
BOOL SetDlgItemTextA(HWND d, int id, LPCSTR s);
UINT GetDlgItemInt(HWND d, int id, BOOL *ok, BOOL sign);
BOOL SetDlgItemInt(HWND d, int id, UINT v, BOOL sign);
LRESULT SendDlgItemMessageA(HWND d, int id, UINT msg, WPARAM wp, LPARAM lp);
int GetDlgCtrlID(HWND w);
BOOL CheckDlgButton(HWND d, int id, UINT check);
UINT IsDlgButtonChecked(HWND d, int id);

/* ---- rectangles ---- */
BOOL SetRect(LPRECT r, int l, int t, int ri, int b);
BOOL SetRectEmpty(LPRECT r);
BOOL CopyRect(LPRECT d, const RECT *s);
BOOL IsRectEmpty(const RECT *r);
BOOL PtInRect(const RECT *r, POINT p);
BOOL InflateRect(LPRECT r, int dx, int dy);
BOOL OffsetRect(LPRECT r, int dx, int dy);
BOOL IntersectRect(LPRECT d, const RECT *a, const RECT *b);
BOOL UnionRect(LPRECT d, const RECT *a, const RECT *b);
BOOL SubtractRect(LPRECT d, const RECT *a, const RECT *b);
BOOL EqualRect(const RECT *a, const RECT *b);

/* ---- GDI (gdi.c) ---- */
typedef struct {
    DWORD biSize;
    LONG biWidth, biHeight;
    WORD biPlanes, biBitCount;
    DWORD biCompression, biSizeImage;
    LONG biXPelsPerMeter, biYPelsPerMeter;
    DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;
typedef struct { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO;
#pragma pack(push, 2)
typedef struct { WORD bfType; DWORD bfSize; WORD bfReserved1, bfReserved2; DWORD bfOffBits; } BITMAPFILEHEADER;
#pragma pack(pop)
typedef struct { LONG bmType, bmWidth, bmHeight, bmWidthBytes; WORD bmPlanes, bmBitsPixel; LPVOID bmBits; } BITMAP;
typedef struct { DWORD dwSize, iType, nCount, nRgnSize; RECT rcBound; } RGNDATAHEADER;
typedef struct { RGNDATAHEADER rdh; char Buffer[1]; } RGNDATA;
typedef struct {
    LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading, tmAveCharWidth, tmMaxCharWidth, tmWeight,
        tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
    BYTE tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar, tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily,
        tmCharSet;
} TEXTMETRICA;
#define BI_RGB 0
#define DIB_RGB_COLORS 0
#define SRCCOPY 0x00CC0020u
#define TRANSPARENT 1
#define OPAQUE 2
#define NULLREGION 1
#define SIMPLEREGION 2
#define COMPLEXREGION 3
#define ERROR 0
#define RGN_AND 1
#define RGN_OR 2
#define RGN_XOR 3
#define RGN_DIFF 4
#define RGN_COPY 5
#define SYSRGN 4
#define FW_THIN 100
#define FW_LIGHT 300
#define FW_NORMAL 400
#define FW_MEDIUM 500
#define FW_SEMIBOLD 600
#define FW_BOLD 700
#define FW_HEAVY 900
#define DEFAULT_CHARSET 1
#define ANSI_CHARSET 0
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_QUALITY 0
#define ANTIALIASED_QUALITY 4
#define CLEARTYPE_QUALITY 5
#define DEFAULT_PITCH 0
#define FF_DONTCARE 0
#define PS_SOLID 0
#define PS_NULL 5
#define NULL_BRUSH 5
#define NULL_PEN 8
#define WHITE_BRUSH 0
#define BLACK_BRUSH 4
#define DC_BRUSH 18
#define DC_PEN 19
#define DEFAULT_GUI_FONT 17
#define DT_TOP 0x0
#define DT_LEFT 0x0
#define DT_CENTER 0x1
#define DT_RIGHT 0x2
#define DT_VCENTER 0x4
#define DT_BOTTOM 0x8
#define DT_WORDBREAK 0x10
#define DT_SINGLELINE 0x20
#define DT_EXPANDTABS 0x40
#define DT_NOCLIP 0x100
#define DT_CALCRECT 0x400
#define DT_NOPREFIX 0x800
#define DT_END_ELLIPSIS 0x8000
#define DT_PATH_ELLIPSIS 0x4000
#define DT_WORD_ELLIPSIS 0x40000
#define DT_MODIFYSTRING 0x10000
#define DT_EDITCONTROL 0x2000
HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
int SaveDC(HDC dc);
BOOL RestoreDC(HDC dc, int n);
HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO *bi, UINT usage, void **bits, HANDLE sec, DWORD off);
HBITMAP CreateCompatibleBitmap(HDC dc, int w, int h);
int GetDIBits(HDC dc, HBITMAP bm, UINT start, UINT lines, LPVOID bits, BITMAPINFO *bi, UINT usage);
int GetObjectA(HGDIOBJ o, int n, LPVOID out);
HGDIOBJ GetCurrentObject(HDC dc, UINT type);
#define OBJ_BITMAP 7
HGDIOBJ SelectObject(HDC dc, HGDIOBJ o);
BOOL DeleteObject(HGDIOBJ o);
HGDIOBJ GetStockObject(int i);
HBRUSH CreateSolidBrush(COLORREF c);
HPEN CreatePen(int style, int w, COLORREF c);
HFONT CreateFontA(int h, int w, int esc, int orient, int weight, DWORD italic, DWORD underline, DWORD strike,
                  DWORD charset, DWORD out_prec, DWORD clip_prec, DWORD quality, DWORD pitch, LPCSTR face);
COLORREF SetTextColor(HDC dc, COLORREF c);
COLORREF GetTextColor(HDC dc);
COLORREF SetBkColor(HDC dc, COLORREF c);
int SetBkMode(HDC dc, int mode);
COLORREF SetDCBrushColor(HDC dc, COLORREF c);
COLORREF SetDCPenColor(HDC dc, COLORREF c);
int FillRect(HDC dc, const RECT *r, HBRUSH b);
int FrameRect(HDC dc, const RECT *r, HBRUSH b);
BOOL Rectangle(HDC dc, int l, int t, int r, int b);
BOOL Ellipse(HDC dc, int l, int t, int r, int b);
BOOL RoundRect(HDC dc, int l, int t, int r, int b, int w, int h);
BOOL Polygon(HDC dc, const POINT *p, int n);
BOOL Polyline(HDC dc, const POINT *p, int n);
BOOL MoveToEx(HDC dc, int x, int y, LPPOINT old);
BOOL LineTo(HDC dc, int x, int y);
COLORREF SetPixel(HDC dc, int x, int y, COLORREF c);
COLORREF GetPixel(HDC dc, int x, int y);
BOOL PatBlt(HDC dc, int x, int y, int w, int h, DWORD rop);
BOOL BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop);
BOOL StretchBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, DWORD rop);
int DrawTextA(HDC dc, LPCSTR s, int n, LPRECT r, UINT fmt);
#define DrawText DrawTextA
BOOL TextOutA(HDC dc, int x, int y, LPCSTR s, int n);
BOOL ExtTextOutA(HDC dc, int x, int y, UINT opt, const RECT *r, LPCSTR s, UINT n, const INT *dx);
BOOL GetTextExtentPoint32A(HDC dc, LPCSTR s, int n, LPSIZE sz);
BOOL GetTextMetricsA(HDC dc, TEXTMETRICA *tm);
#define ETO_OPAQUE 2
#define ETO_CLIPPED 4
BOOL GdiFlush(void);
HRGN CreateRectRgn(int l, int t, int r, int b);
HRGN CreateRectRgnIndirect(const RECT *r);
int CombineRgn(HRGN d, HRGN a, HRGN b, int mode);
BOOL SetRectRgn(HRGN r, int l, int t, int ri, int b);
int GetRgnBox(HRGN r, LPRECT box);
DWORD GetRegionData(HRGN r, DWORD n, RGNDATA *d);
int GetRandomRgn(HDC dc, HRGN r, INT i);
int SelectClipRgn(HDC dc, HRGN r);
int ExtSelectClipRgn(HDC dc, HRGN r, int mode);
int GetClipBox(HDC dc, LPRECT r);
int IntersectClipRect(HDC dc, int l, int t, int r, int b);
int ExcludeClipRect(HDC dc, int l, int t, int r, int b);
BOOL PtInRegion(HRGN r, int x, int y);
BOOL RectInRegion(HRGN r, const RECT *rc);
int SetGraphicsMode(HDC dc, int mode);
BOOL SetWorldTransform(HDC dc, const XFORM *x);
BOOL SetViewportOrgEx(HDC dc, int x, int y, LPPOINT old);
BOOL GetViewportOrgEx(HDC dc, LPPOINT p);
BOOL OffsetViewportOrgEx(HDC dc, int x, int y, LPPOINT old);
int GetDeviceCaps(HDC dc, int i);
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define GM_ADVANCED 2

int GetClipRgn(HDC dc, HRGN r);
int OffsetRgn(HRGN r, int dx, int dy);
#define CB_ERR (-1)
#define CB_GETLBTEXTLEN 0x0149
#define CB_FINDSTRINGEXACT 0x0158
int SHCreateDirectoryExA(HWND w, LPCSTR path, const void *sec);
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp

/* fopen takes Windows paths here too (win32.c: "C:\Users\web\x" is /Users/web/x on the browser's file system) */
FILE *web_fopen(const char *path, const char *mode);
#define fopen web_fopen

/* (the shell, OLE: nothing more is needed) */
#define CoInitialize(x) 0
#define CoUninitialize()
