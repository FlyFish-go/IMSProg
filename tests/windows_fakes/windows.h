#ifndef IMSPROG_TEST_FAKE_WINDOWS_H
#define IMSPROG_TEST_FAKE_WINDOWS_H
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#define WINAPI
#define WINAPIV
#define MAX_PATH 260
#define TRUE 1
#define FALSE 0
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
typedef uint8_t UCHAR;
typedef char CHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG, DWORD;
typedef int BOOL;
typedef void VOID;
typedef wchar_t WCHAR;
typedef void *HANDLE, *HMODULE, *PVOID;
typedef ULONG *PULONG;
typedef void (*FARPROC)(void);
DWORD GetModuleFileNameW(HMODULE, WCHAR *, DWORD);
DWORD GetLastError(void);
void SetLastError(DWORD);
HMODULE LoadLibraryExW(const WCHAR *, HANDLE, DWORD);
FARPROC GetProcAddress(HMODULE, const char *);
BOOL FreeLibrary(HMODULE);
#endif
