#include "cdx/create_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static cdx_create_status checkpoint(cdx_cancel_handle cancellation, ULONGLONG deadline);
static cdx_create_status error_status(DWORD error);
static cdx_create_status write_temporary(HANDLE file, const unsigned char *bytes, size_t length,
                                         cdx_cancel_handle cancellation, ULONGLONG deadline);

cdx_create_status cdx_create_publish(const wchar_t *path, const unsigned char *bytes,
                                     size_t length, cdx_cancel_handle cancellation,
                                     cdx_create_result *result) {
    ULONGLONG deadline = GetTickCount64() + 15000u;
    size_t capacity = wcslen(path) + 80u;
    wchar_t *temporary_path = NULL;
    HANDLE temporary = INVALID_HANDLE_VALUE;
    int created = 0;
    cdx_create_status status = checkpoint(cancellation, deadline);
    if (status != CDX_CREATE_OK) return status;
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return CDX_CREATE_ALREADY_EXISTS;
    temporary_path = (wchar_t *)malloc(capacity * sizeof(*temporary_path));
    if (!temporary_path) return CDX_CREATE_IO_ERROR;
    if (_snwprintf_s(temporary_path, capacity, _TRUNCATE, L"%ls.cdx.tmp.%lu.%llu", path,
                     (unsigned long)GetCurrentProcessId(), GetTickCount64()) < 0) {
        status = CDX_CREATE_VALIDATION_ERROR; goto done;
    }
    temporary = CreateFileW(temporary_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (temporary == INVALID_HANDLE_VALUE) { status = error_status(GetLastError()); goto done; }
    created = 1;
    status = write_temporary(temporary, bytes, length, cancellation, deadline);
    if (status != CDX_CREATE_OK) goto done;
    if (!CloseHandle(temporary)) { temporary = INVALID_HANDLE_VALUE; status = CDX_CREATE_IO_ERROR; goto done; }
    temporary = INVALID_HANDLE_VALUE;
    status = checkpoint(cancellation, deadline);
    if (status != CDX_CREATE_OK) goto done;
    /* Omitting REPLACE_EXISTING makes publication fail if the target already exists. */
    if (!MoveFileExW(temporary_path, path, MOVEFILE_WRITE_THROUGH)) {
        DWORD error = GetLastError();
        status = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES ? CDX_CREATE_ALREADY_EXISTS : error_status(error);
        goto done;
    }
    created = 0;
    result->written_to_disk = 1;
done:
    if (temporary != INVALID_HANDLE_VALUE && !CloseHandle(temporary)) status = CDX_CREATE_IO_ERROR;
    if (created && !DeleteFileW(temporary_path)) { result->cleanup_complete = 0; status = CDX_CREATE_IO_ERROR; }
    free(temporary_path);
    return status;
}

static cdx_create_status checkpoint(cdx_cancel_handle cancellation, ULONGLONG deadline) {
    if (cancellation && WaitForSingleObject(cancellation, 0) == WAIT_OBJECT_0) return CDX_CREATE_CANCELLED;
    return GetTickCount64() >= deadline ? CDX_CREATE_TIMED_OUT : CDX_CREATE_OK;
}

static cdx_create_status error_status(DWORD error) {
    switch (error) {
    case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: return CDX_CREATE_ALREADY_EXISTS;
    case ERROR_PATH_NOT_FOUND: case ERROR_FILE_NOT_FOUND: case ERROR_DIRECTORY: return CDX_CREATE_NOT_FOUND;
    case ERROR_ACCESS_DENIED: case ERROR_PRIVILEGE_NOT_HELD: return CDX_CREATE_PERMISSION_DENIED;
    case ERROR_INVALID_NAME: case ERROR_BAD_PATHNAME: case ERROR_FILENAME_EXCED_RANGE: return CDX_CREATE_VALIDATION_ERROR;
    default: return CDX_CREATE_IO_ERROR;
    }
}

static cdx_create_status write_temporary(HANDLE file, const unsigned char *bytes, size_t length,
                                         cdx_cancel_handle cancellation, ULONGLONG deadline) {
    size_t offset = 0, step;
    cdx_create_status status;
    for (step = 0; offset < length && step < CDX_DOCUMENT_MAX_BYTES / 65536u + 1u; ++step) {
        DWORD count = 0, requested = (DWORD)(length - offset < 65536u ? length - offset : 65536u);
        status = checkpoint(cancellation, deadline);
        if (status != CDX_CREATE_OK) return status;
        if (!WriteFile(file, bytes + offset, requested, &count, NULL) || count != requested) return CDX_CREATE_IO_ERROR;
        offset += count;
    }
    if (offset != length) return CDX_CREATE_IO_ERROR;
    status = checkpoint(cancellation, deadline);
    if (status != CDX_CREATE_OK) return status;
    return FlushFileBuffers(file) ? CDX_CREATE_OK : CDX_CREATE_IO_ERROR;
}
