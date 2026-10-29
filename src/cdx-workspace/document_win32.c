#include "cdx/document.h"
#include "document_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cdx_read_file(const wchar_t *path, unsigned char **bytes, size_t *length, cdx_file_version *version) {
    HANDLE handle;
    BY_HANDLE_FILE_INFORMATION info;
    LARGE_INTEGER file_size;
    DWORD read_count;
    unsigned char *buffer = NULL;
    size_t size = 0, capacity, iteration;
    ULONGLONG deadline = GetTickCount64() + 15000u;
    handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileInformationByHandle(handle, &info) || !GetFileSizeEx(handle, &file_size)) { CloseHandle(handle); return 0; }
    if (file_size.QuadPart < 0 || (unsigned long long)file_size.QuadPart > CDX_DOCUMENT_MAX_BYTES) { CloseHandle(handle); SetLastError(ERROR_FILE_TOO_LARGE); return 0; }
    if (info.nNumberOfLinks != 1u || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) { CloseHandle(handle); SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    capacity = (size_t)file_size.QuadPart + 1u;
    buffer = (unsigned char *)malloc(capacity);
    if (!buffer) { CloseHandle(handle); return 0; }
    for (iteration = 0; iteration <= CDX_DOCUMENT_MAX_BYTES / 65536u + 1u && GetTickCount64() < deadline; ++iteration) {
        DWORD requested = capacity - size < 65536u ? (DWORD)(capacity - size) : 65536u;
        if (!ReadFile(handle, buffer + size, requested, &read_count, NULL)) { cdx_free_bytes(&buffer); CloseHandle(handle); return 0; }
        if (read_count == 0) break;
        size += read_count;
        if (size >= capacity) { cdx_free_bytes(&buffer); CloseHandle(handle); return 0; }
    }
    if (size != (size_t)file_size.QuadPart || GetTickCount64() >= deadline) { cdx_free_bytes(&buffer); CloseHandle(handle); return 0; }
    CloseHandle(handle);
    version->volume_serial = info.dwVolumeSerialNumber;
    version->file_index_high = info.nFileIndexHigh;
    version->file_index_low = info.nFileIndexLow;
    version->size = file_size;
    version->last_write_time = info.ftLastWriteTime;
    *bytes = buffer;
    *length = size;
    return 1;
}

int cdx_same_version(const cdx_file_version *left, const cdx_file_version *right) {
    return left->volume_serial == right->volume_serial && left->file_index_high == right->file_index_high && left->file_index_low == right->file_index_low &&
        left->size.QuadPart == right->size.QuadPart && left->last_write_time.dwLowDateTime == right->last_write_time.dwLowDateTime && left->last_write_time.dwHighDateTime == right->last_write_time.dwHighDateTime;
}

cdx_document_status cdx_document_open(cdx_document *document, const wchar_t *path, cdx_encoding requested_encoding) {
    size_t offset = 0, decode_length;
    cdx_encoding encoding;
    unsigned char *roundtrip = NULL;
    size_t roundtrip_size = 0;
    if (!document) return CDX_STATUS_VALIDATION_ERROR;
    memset(document, 0, sizeof(*document));
    if (!path || !*path || wcslen(path) >= MAX_PATH * 2u - 40u || wcschr(path, L'\n') || wcschr(path, L'\r') ||
        !((path[0] && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) || (path[0] == L'\\' && path[1] == L'\\')) ||
        requested_encoding < CDX_ENCODING_AUTO || requested_encoding > CDX_ENCODING_CP936) return CDX_STATUS_VALIDATION_ERROR;
    document->path = _wcsdup(path);
    if (!document->path) return CDX_STATUS_IO_ERROR;
    if (!cdx_read_file(path, &document->original_bytes, &document->original_size, &document->original_version)) {
        DWORD error = GetLastError(); cdx_document_free(document);
        return error == ERROR_FILE_TOO_LARGE ? CDX_STATUS_TOO_LARGE :
            (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? CDX_STATUS_NOT_FOUND : CDX_STATUS_IO_ERROR;
    }
    if (!cdx_sha256(document->original_bytes, document->original_size, document->original_sha256)) { cdx_document_free(document); return CDX_STATUS_IO_ERROR; }
    encoding = cdx_detect_encoding(document->original_bytes, document->original_size, requested_encoding, &offset);
    if (encoding == CDX_ENCODING_AUTO) {
        char *utf8 = NULL; size_t utf8_length = 0;
        if (cdx_decode_to_utf8(document->original_bytes, document->original_size, CDX_ENCODING_UTF8, &utf8, &utf8_length)) { document->text_utf8 = utf8; document->text_length = utf8_length; encoding = CDX_ENCODING_UTF8; }
        else { cdx_document_free(document); return CDX_STATUS_ENCODING_ERROR; }
    }
    if (!document->text_utf8) {
        decode_length = document->original_size - offset;
        if (!cdx_decode_to_utf8(document->original_bytes + offset, decode_length, encoding, &document->text_utf8, &document->text_length)) { cdx_document_free(document); return CDX_STATUS_ENCODING_ERROR; }
    }
    document->encoding = encoding;
    if (memchr(document->text_utf8, 0, document->text_length) != NULL) { cdx_document_free(document); return CDX_STATUS_ENCODING_ERROR; }
    if (!cdx_encode_text(document->text_utf8, document->text_length, encoding, &roundtrip, &roundtrip_size) ||
        roundtrip_size != document->original_size || memcmp(roundtrip, document->original_bytes, roundtrip_size) != 0) {
        cdx_free_bytes(&roundtrip); cdx_document_free(document); return CDX_STATUS_ENCODING_ERROR;
    }
    cdx_free_bytes(&roundtrip);
    document->has_bom = (offset != 0);
    document->eol = cdx_detect_eol(document->text_utf8, document->text_length);
    document->current_bytes = (unsigned char *)malloc(document->original_size + 1u);
    if (!document->current_bytes) { cdx_document_free(document); return CDX_STATUS_IO_ERROR; }
    memcpy(document->current_bytes, document->original_bytes, document->original_size);
    document->current_size = document->original_size;
    memcpy(document->current_sha256, document->original_sha256, sizeof(document->current_sha256));
    return CDX_STATUS_OK;
}

cdx_document_status cdx_document_commit(cdx_document *document, cdx_commit_result *result) {
    unsigned char *disk_bytes = NULL;
    unsigned char *committed_bytes = NULL;
    size_t disk_size = 0;
    cdx_file_version disk_version;
    HANDLE temporary = INVALID_HANDLE_VALUE;
    wchar_t temp_path[MAX_PATH * 2];
    DWORD written = 0;
    char disk_hash[CDX_SHA256_HEX_LENGTH + 1];
    cdx_document_status status = CDX_STATUS_IO_ERROR;
    int temporary_created = 0;
    if (result) memset(result, 0, sizeof(*result));
    if (!document || !document->path) { if (result) result->status = CDX_STATUS_VALIDATION_ERROR; return CDX_STATUS_VALIDATION_ERROR; }
    if (!document->dirty_in_memory) { if (result) { result->status = CDX_STATUS_OK; result->cleanup_complete = 1; memcpy(result->disk_sha256, document->original_sha256, sizeof(result->disk_sha256)); } return CDX_STATUS_OK; }
    if (!cdx_read_file(document->path, &disk_bytes, &disk_size, &disk_version) || !cdx_sha256(disk_bytes, disk_size, disk_hash)) goto done;
    if (!cdx_same_version(&document->original_version, &disk_version) || strcmp(document->original_sha256, disk_hash) != 0) { status = CDX_STATUS_FILE_CHANGED; goto done; }
    cdx_free_bytes(&disk_bytes);
    committed_bytes = (unsigned char *)malloc(document->current_size + 1u);
    if (!committed_bytes) goto done;
    memcpy(committed_bytes, document->current_bytes, document->current_size);
    _snwprintf_s(temp_path, _countof(temp_path), _TRUNCATE, L"%ls.cdx.tmp.%lu.%llu", document->path, (unsigned long)GetCurrentProcessId(), GetTickCount64());
    temporary = CreateFileW(temp_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
    status = CDX_STATUS_COMMIT_ERROR;
    if (temporary == INVALID_HANDLE_VALUE) goto done;
    temporary_created = 1;
    if (!WriteFile(temporary, document->current_bytes, (DWORD)document->current_size, &written, NULL) || written != (DWORD)document->current_size || !FlushFileBuffers(temporary)) goto done;
    CloseHandle(temporary); temporary = INVALID_HANDLE_VALUE;
    if (!cdx_read_file(document->path, &disk_bytes, &disk_size, &disk_version) || !cdx_sha256(disk_bytes, disk_size, disk_hash)) goto done;
    if (!cdx_same_version(&document->original_version, &disk_version) || strcmp(document->original_sha256, disk_hash) != 0) { status = CDX_STATUS_FILE_CHANGED; goto done; }
    cdx_free_bytes(&disk_bytes);
    /* ReplaceFile preserves target metadata. A failed replace is not retried as a different write. */
    if (!ReplaceFileW(document->path, temp_path, NULL, 0, NULL, NULL)) goto done;
    temporary_created = 0;
    if (result) result->written_to_disk = 1;
    if (!cdx_read_file(document->path, &disk_bytes, &disk_size, &disk_version) || !cdx_sha256(disk_bytes, disk_size, disk_hash) ||
        strcmp(disk_hash, document->current_sha256) != 0) goto done;
    cdx_free_bytes(&document->original_bytes);
    document->original_bytes = committed_bytes;
    committed_bytes = NULL;
    document->original_size = document->current_size;
    memcpy(document->original_sha256, document->current_sha256, sizeof(document->original_sha256));
    document->original_version = disk_version;
    document->dirty_in_memory = 0;
    status = CDX_STATUS_OK;
done:
    if (temporary != INVALID_HANDLE_VALUE) CloseHandle(temporary);
    if (temporary_created && !DeleteFileW(temp_path)) { if (result) result->cleanup_complete = 0; }
    else if (result) result->cleanup_complete = 1;
    cdx_free_bytes(&disk_bytes);
    cdx_free_bytes(&committed_bytes);
    if (result) { result->status = status; if (status == CDX_STATUS_OK) memcpy(result->disk_sha256, document->current_sha256, sizeof(result->disk_sha256)); }
    return status;
}
