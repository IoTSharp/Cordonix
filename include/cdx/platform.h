#ifndef CDX_PLATFORM_H
#define CDX_PLATFORM_H

#ifdef _WIN32
#include <windows.h>
typedef wchar_t cdx_path_char;
typedef HANDLE cdx_cancel_handle;
#else
#include <stdatomic.h>
typedef char cdx_path_char;
typedef atomic_int *cdx_cancel_handle;
#endif

#endif
