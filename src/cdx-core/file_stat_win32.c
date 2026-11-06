#include "cdx/file_stat.h"
#include "file_stat_internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static cdx_fs_status status_from_error(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
        return CDX_FS_NOT_FOUND;
    case ERROR_ACCESS_DENIED:
    case ERROR_NETWORK_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
        return CDX_FS_PERMISSION_DENIED;
    case ERROR_NO_UNICODE_TRANSLATION:
        return CDX_FS_ENCODING_ERROR;
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_FILENAME_EXCED_RANGE:
        return CDX_FS_VALIDATION_ERROR;
    default:
        return CDX_FS_IO_ERROR;
    }
}

static int64_t tick_deadline(unsigned timeout_ms) {
    ULONGLONG now = GetTickCount64();
    ULONGLONG deadline = now + timeout_ms;
    return deadline < now ? INT64_MAX : (int64_t)deadline;
}

static int stopped(cdx_cancel_handle cancellation, int64_t deadline) {
    if (cancellation && WaitForSingleObject(cancellation, 0) == WAIT_OBJECT_0) return 1;
    return (int64_t)GetTickCount64() >= deadline;
}

static int filetime_to_ns(FILETIME value, int64_t *output) {
    ULARGE_INTEGER ticks;
    uint64_t relative;
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    if (ticks.QuadPart < UINT64_C(116444736000000000)) {
        relative = UINT64_C(116444736000000000) - ticks.QuadPart;
        if (relative > (uint64_t)INT64_MAX / 100u) return 0;
        *output = -(int64_t)(relative * 100u);
        return 1;
    }
    relative = ticks.QuadPart - UINT64_C(116444736000000000);
    if (relative > (uint64_t)INT64_MAX / 100u) return 0;
    *output = (int64_t)(relative * 100u);
    return 1;
}

static cdx_fs_kind kind_from_attributes(DWORD attributes) {
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return CDX_FS_LINK;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return CDX_FS_DIRECTORY;
    if (attributes & FILE_ATTRIBUTE_DEVICE) return CDX_FS_OTHER;
    return CDX_FS_FILE;
}

static cdx_fs_status utf8_path(const wchar_t *path, char **output) {
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, NULL, 0, NULL, NULL);
    char *text;
    if (!count) return status_from_error(GetLastError());
    text = (char *)malloc((size_t)count);
    if (!text) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return CDX_FS_IO_ERROR; }
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, text, count, NULL, NULL)) {
        DWORD error = GetLastError();
        free(text);
        SetLastError(error);
        return status_from_error(error);
    }
    *output = text;
    return CDX_FS_OK;
}

void cdx_stat_platform_query(const cdx_stat_request *request, cdx_stat_result *result) {
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    wchar_t *normalized = NULL;
    unsigned long native_error = ERROR_SUCCESS;
    cdx_fs_status status;
    int64_t deadline = tick_deadline(request->timeout_ms);

    status = cdx_fs_normalize_path(request->path, request->cancellation, &normalized, &native_error);
    if (status != CDX_FS_OK) {
        result->status = status;
        result->native_error = native_error;
        return;
    }
    status = utf8_path(normalized, &result->path);
    if (status != CDX_FS_OK) {
        result->status = status;
        result->native_error = GetLastError();
        free(normalized);
        return;
    }
    free(normalized);
    if (stopped(request->cancellation, deadline)) {
        result->status = request->cancellation && WaitForSingleObject(request->cancellation, 0) == WAIT_OBJECT_0
            ? CDX_FS_CANCELLED : CDX_FS_TIMED_OUT;
        result->native_error = result->status == CDX_FS_CANCELLED ? ERROR_CANCELLED : ERROR_TIMEOUT;
        return;
    }
    /* Query the caller's lexical path so a reparse-point name is not replaced by its target. */
    if (!GetFileAttributesExW(request->path, GetFileExInfoStandard, &attributes)) {
        DWORD error = GetLastError();
        result->status = status_from_error(error);
        result->native_error = error;
        return;
    }
    result->kind = kind_from_attributes(attributes.dwFileAttributes);
    result->hidden = (attributes.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    result->hidden_known = 1;
    result->system = (attributes.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
    result->system_known = 1;
    result->size_known = result->kind == CDX_FS_FILE;
    if (result->size_known)
        result->size_bytes = ((uint64_t)attributes.nFileSizeHigh << 32) | attributes.nFileSizeLow;
    result->time_precision_ns = 100;
    result->time_precision_known = 1;
    if (result->kind != CDX_FS_LINK) {
        result->creation_time_known = filetime_to_ns(attributes.ftCreationTime, &result->creation_time_ns);
        result->access_time_known = filetime_to_ns(attributes.ftLastAccessTime, &result->access_time_ns);
        result->modification_time_known = filetime_to_ns(attributes.ftLastWriteTime, &result->modification_time_ns);
    }
    if (stopped(request->cancellation, deadline)) {
        result->status = request->cancellation && WaitForSingleObject(request->cancellation, 0) == WAIT_OBJECT_0
            ? CDX_FS_CANCELLED : CDX_FS_TIMED_OUT;
        result->native_error = result->status == CDX_FS_CANCELLED ? ERROR_CANCELLED : ERROR_TIMEOUT;
        return;
    }
    result->status = CDX_FS_OK;
}
