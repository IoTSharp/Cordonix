#include "cdx/file_ops.h"
#include "file_ops_internal.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define FILE_CHUNK_BYTES 65536u

uint64_t cdx_file_clock_ms(void) { return GetTickCount64(); }

const char *cdx_file_checkpoint(cdx_cancel_handle cancellation, uint64_t deadline) {
    if (cancellation) {
        DWORD wait = WaitForSingleObject(cancellation, 0);
        if (wait == WAIT_OBJECT_0) { SetLastError(ERROR_CANCELLED); return "cancelled"; }
        if (wait == WAIT_FAILED) return "io_error";
    }
    if (GetTickCount64() >= deadline) { SetLastError(ERROR_TIMEOUT); return "timed_out"; }
    return NULL;
}

static const char *error_status(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND: return "not_found";
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION: return "permission_denied";
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS: return "already_exists";
    case ERROR_NOT_SAME_DEVICE: return "cross_device";
    default: return "io_error";
    }
}

static HANDLE temporary_file(const cdx_file_request *request, uint64_t deadline, wchar_t *path,
                              size_t capacity, cdx_file_result *result) {
    unsigned attempt;
    HANDLE file;
    for (attempt = 0; attempt < 32u; ++attempt) {
        result->status = cdx_file_checkpoint(request->cancellation, deadline);
        if (result->status) return INVALID_HANDLE_VALUE;
        if (_snwprintf_s(path, capacity, _TRUNCATE, L"%ls.cdx-copy-%lu-%llu-%u.tmp", request->destination,
                        GetCurrentProcessId(), (unsigned long long)GetTickCount64(), attempt) < 0) {
            result->status = "validation_error"; SetLastError(ERROR_FILENAME_EXCED_RANGE); return INVALID_HANDLE_VALUE;
        }
        file = CreateFileW(path, GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) return file;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    result->status = error_status(GetLastError());
    return INVALID_HANDLE_VALUE;
}

static const char *stream_file(const cdx_file_request *request, HANDLE source, HANDLE destination,
                               unsigned char *buffer, uint64_t deadline, cdx_file_result *result) {
    cdx_file_sha256 hash;
    unsigned step;
    const char *status;
    cdx_file_sha256_init(&hash);
    for (step = 0; step <= (unsigned)(CDX_FILE_MAX_BYTES / FILE_CHUNK_BYTES); ++step) {
        DWORD received = 0, offset = 0;
        status = cdx_file_checkpoint(request->cancellation, deadline);
        if (status) return status;
        if (!ReadFile(source, buffer, FILE_CHUNK_BYTES, &received, NULL)) return error_status(GetLastError());
        if (!received) {
            if (result->bytes_processed != result->size_bytes) { result->file_changed = 1; SetLastError(ERROR_FILE_INVALID); return "file_changed"; }
            cdx_file_sha256_finish(&hash, result->sha256); return "ok";
        }
        if (result->bytes_processed + received > request->max_bytes || result->bytes_processed + received > result->size_bytes) {
            result->file_changed = 1; SetLastError(ERROR_FILE_INVALID); return "file_changed";
        }
        cdx_file_sha256_update(&hash, buffer, received);
        for (unsigned write_step = 0; destination != INVALID_HANDLE_VALUE && offset < received && write_step < FILE_CHUNK_BYTES; ++write_step) {
            DWORD written = 0;
            status = cdx_file_checkpoint(request->cancellation, deadline);
            if (status) return status;
            if (!WriteFile(destination, buffer + offset, received - offset, &written, NULL) || !written) return "io_error";
            offset += written;
        }
        if (destination != INVALID_HANDLE_VALUE && offset != received) return "io_error";
        result->bytes_processed += received;
    }
    SetLastError(ERROR_FILE_TOO_LARGE); return "too_large";
}

void cdx_file_platform_execute(const cdx_file_request *request, uint64_t deadline, cdx_file_result *result) {
    HANDLE source = INVALID_HANDLE_VALUE, destination = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION metadata, copied_metadata;
    unsigned char *buffer = NULL;
    wchar_t *temporary = NULL;
    int temporary_created = 0;
    int copying = request->operation == CDX_FILE_COPY;
    DWORD error = ERROR_SUCCESS;
    result->status = cdx_file_checkpoint(request->cancellation, deadline);
    if (result->status) goto done;
    source = CreateFileW(request->source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (source == INVALID_HANDLE_VALUE) { result->status = error_status(GetLastError()); goto done; }
    if (!GetFileInformationByHandle(source, &metadata)) { result->status = "io_error"; goto done; }
    if (GetFileType(source) != FILE_TYPE_DISK ||
        (metadata.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE))) {
        result->status = "not_regular_file"; SetLastError(ERROR_INVALID_PARAMETER); goto done;
    }
    result->size_bytes = ((uint64_t)metadata.nFileSizeHigh << 32) | metadata.nFileSizeLow;
    if (request->operation != CDX_FILE_HASH) {
        DWORD attributes = GetFileAttributesW(request->destination);
        if (attributes != INVALID_FILE_ATTRIBUTES) { SetLastError(ERROR_ALREADY_EXISTS); result->status = "already_exists"; goto done; }
        if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
            result->status = error_status(GetLastError()); goto done;
        }
    }
    if (request->operation == CDX_FILE_MOVE) {
        if (!CloseHandle(source)) { source = INVALID_HANDLE_VALUE; result->status = "io_error"; goto done; }
        source = INVALID_HANDLE_VALUE;
        result->status = cdx_file_checkpoint(request->cancellation, deadline);
        if (result->status) goto done;
        if (!MoveFileExW(request->source, request->destination, 0)) { result->status = error_status(GetLastError()); goto done; }
        result->written_to_disk = 1; result->source_removed = 1; result->status = "ok"; goto done;
    }
    if (result->size_bytes > request->max_bytes) { result->status = "too_large"; SetLastError(ERROR_FILE_TOO_LARGE); goto done; }
    buffer = (unsigned char *)malloc(FILE_CHUNK_BYTES);
    if (!buffer) { result->status = "io_error"; SetLastError(ERROR_NOT_ENOUGH_MEMORY); goto done; }
    if (copying) {
        size_t capacity = wcslen(request->destination) + 80u;
        if (capacity > 32768u) { result->status = "validation_error"; SetLastError(ERROR_FILENAME_EXCED_RANGE); goto done; }
        temporary = (wchar_t *)malloc(capacity * sizeof(wchar_t));
        if (!temporary) { result->status = "io_error"; SetLastError(ERROR_NOT_ENOUGH_MEMORY); goto done; }
        destination = temporary_file(request, deadline, temporary, capacity, result);
        if (destination == INVALID_HANDLE_VALUE) goto done;
        temporary_created = 1;
    }
    result->status = stream_file(request, source, destination, buffer, deadline, result);
    if (strcmp(result->status, "ok")) goto done;
    if (!copying) goto done;
    if (!SetFileTime(destination, &metadata.ftCreationTime, &metadata.ftLastAccessTime, &metadata.ftLastWriteTime) ||
        !FlushFileBuffers(destination) || !GetFileInformationByHandle(destination, &copied_metadata)) { result->status = "io_error"; goto done; }
    result->metadata_checked = 1;
    result->metadata_preserved = CompareFileTime(&metadata.ftCreationTime, &copied_metadata.ftCreationTime) == 0 &&
        CompareFileTime(&metadata.ftLastAccessTime, &copied_metadata.ftLastAccessTime) == 0 &&
        CompareFileTime(&metadata.ftLastWriteTime, &copied_metadata.ftLastWriteTime) == 0;
    if (!CloseHandle(destination)) { destination = INVALID_HANDLE_VALUE; result->status = "io_error"; goto done; }
    destination = INVALID_HANDLE_VALUE;
    result->status = cdx_file_checkpoint(request->cancellation, deadline);
    if (result->status) goto done;
    if (!MoveFileExW(temporary, request->destination, 0)) { result->status = error_status(GetLastError()); goto done; }
    temporary_created = 0; result->written_to_disk = 1; result->status = "ok";
done:
    if (!result->status) result->status = "io_error";
    if (strcmp(result->status, "ok")) error = GetLastError();
    if (destination != INVALID_HANDLE_VALUE && !CloseHandle(destination) && !error) error = GetLastError();
    if (source != INVALID_HANDLE_VALUE && !CloseHandle(source) && !error) error = GetLastError();
    if (temporary_created && !DeleteFileW(temporary)) result->cleanup_complete = 0;
    if (error && !strcmp(result->status, "ok")) result->status = "io_error";
    if (!result->written_to_disk) { result->metadata_checked = 0; result->metadata_preserved = 0; }
    if (strcmp(result->status, "ok")) result->sha256[0] = 0;
    result->native_error = error;
    free(buffer);
    free(temporary);
}
