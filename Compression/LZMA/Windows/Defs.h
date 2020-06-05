// Windows/Defs.h

#ifndef __WINDOWS_DEFS_H
#define __WINDOWS_DEFS_H

#include "../Common/MyWindows.h"

#ifndef FREEARC_WIN
#include <pthread.h>
#include <errno.h>
#define WINAPI
#define WAIT_OBJECT_0   0
#define WAIT_TIMEOUT    ETIMEDOUT
#define INFINITE	0xFFFFFFFF
#undef BOOL
typedef int WINBOOL;
typedef WINBOOL BOOL;
typedef void *PVOID,*LPVOID;
typedef void *HANDLE;
#endif

inline bool BOOLToBool(BOOL value)
  { return (value != FALSE); }

inline BOOL BoolToBOOL(bool value)
  { return (value ? TRUE: FALSE); }

inline VARIANT_BOOL BoolToVARIANT_BOOL(bool value)
  { return (value ? VARIANT_TRUE: VARIANT_FALSE); }

inline bool VARIANT_BOOLToBool(VARIANT_BOOL value)
  { return (value != VARIANT_FALSE); }

#endif
