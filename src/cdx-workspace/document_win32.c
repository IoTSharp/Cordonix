#include "cdx/document.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cdx_decode_to_utf8(const unsigned char *, size_t, cdx_encoding, char **, size_t *);
int cdx_encode_text(const char *, size_t, cdx_encoding, unsigned char **, size_t *);
int cdx_sha256(const unsigned char *, size_t, char[CDX_SHA256_HEX_LENGTH + 1]);
int cdx_validate_utf8(const char *, size_t);

static void free_bytes(unsigned char **value) { if (*value) HeapFree(GetProcessHeap(), 0, *value); *value = NULL; }
static void free_text(char **value) { if (*value) HeapFree(GetProcessHeap(), 0, *value); *value = NULL; }

static cdx_document_status edit_status(cdx_edit_result *result, cdx_document_status status) {
    if (result) result->status = status;
    return status;
}

static int read_file(const wchar_t *path, unsigned char **bytes, size_t *length, cdx_file_version *version) {
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
    buffer = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, capacity);
    if (!buffer) { CloseHandle(handle); return 0; }
    for (iteration = 0; iteration <= CDX_DOCUMENT_MAX_BYTES / 65536u + 1u && GetTickCount64() < deadline; ++iteration) {
        DWORD requested = capacity - size < 65536u ? (DWORD)(capacity - size) : 65536u;
        if (!ReadFile(handle, buffer + size, requested, &read_count, NULL)) { free_bytes(&buffer); CloseHandle(handle); return 0; }
        if (read_count == 0) break;
        size += read_count;
        if (size >= capacity) { free_bytes(&buffer); CloseHandle(handle); return 0; }
    }
    if (size != (size_t)file_size.QuadPart || GetTickCount64() >= deadline) { free_bytes(&buffer); CloseHandle(handle); return 0; }
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

static int same_version(const cdx_file_version *left, const cdx_file_version *right) {
    return left->volume_serial == right->volume_serial && left->file_index_high == right->file_index_high && left->file_index_low == right->file_index_low &&
        left->size.QuadPart == right->size.QuadPart && left->last_write_time.dwLowDateTime == right->last_write_time.dwLowDateTime && left->last_write_time.dwHighDateTime == right->last_write_time.dwHighDateTime;
}

static cdx_encoding detect_encoding(const unsigned char *bytes, size_t length, cdx_encoding requested, size_t *offset) {
    if (length >= 3u && bytes[0] == 0xefu && bytes[1] == 0xbbu && bytes[2] == 0xbfu) { *offset = 3u; return CDX_ENCODING_UTF8_BOM; }
    if (length >= 2u && bytes[0] == 0xffu && bytes[1] == 0xfeu) { *offset = 2u; return CDX_ENCODING_UTF16LE; }
    if (length >= 2u && bytes[0] == 0xfeu && bytes[1] == 0xffu) { *offset = 2u; return CDX_ENCODING_UTF16BE; }
    *offset = 0;
    return requested;
}

static cdx_eol detect_eol(const char *text, size_t length) {
    size_t index;
    int crlf = 0, lf = 0, cr = 0;
    for (index = 0; index < length; ++index) {
        if (text[index] == '\r') { if (index + 1u < length && text[index + 1u] == '\n') { ++crlf; ++index; } else ++cr; }
        else if (text[index] == '\n') ++lf;
    }
    if ((crlf != 0) + (lf != 0) + (cr != 0) > 1) return CDX_EOL_MIXED;
    if (crlf) return CDX_EOL_CRLF;
    if (lf) return CDX_EOL_LF;
    if (cr) return CDX_EOL_CR;
    return CDX_EOL_NONE;
}

static int make_replacement(const char *text, size_t text_length, const char *old_text, size_t old_length, const char *new_text, size_t new_length, size_t expected_count, char **updated, size_t *updated_length, size_t *actual_count) {
    size_t index = 0, count = 0, cursor = 0, output_cursor = 0;
    size_t previous_end = 0;
    int overlap = 0;
    size_t result_length;
    char *result;
    ULONGLONG deadline = GetTickCount64() + 15000u;
    while (index + old_length <= text_length && GetTickCount64() < deadline) {
        if (memcmp(text + index, old_text, old_length) == 0) {
            if (count != 0 && index < previous_end) overlap = 1;
            ++count; previous_end = index + old_length;
        }
        ++index;
    }
    if (GetTickCount64() >= deadline) return 0;
    *actual_count = count;
    if (overlap) return -1;
    if (count != expected_count || count == 0) return 1;
    if (new_length > SIZE_MAX / count || old_length > SIZE_MAX / count) return 0;
    result_length = text_length + count * new_length - count * old_length;
    /* CP936 can expand to three UTF-8 bytes per source byte; the disk limit is checked after encoding. */
    if (result_length > CDX_DOCUMENT_MAX_UTF8_BYTES) return 0;
    result = (char *)HeapAlloc(GetProcessHeap(), 0, result_length + 1u);
    if (!result) return 0;
    index = 0; cursor = 0; output_cursor = 0;
    while (index + old_length <= text_length && GetTickCount64() < deadline) {
        if (memcmp(text + index, old_text, old_length) == 0) {
            size_t before = index - cursor;
            memcpy(result + output_cursor, text + cursor, before); output_cursor += before;
            memcpy(result + output_cursor, new_text, new_length); output_cursor += new_length;
            index += old_length; cursor = index;
        } else {
            ++index;
        }
    }
    if (GetTickCount64() >= deadline) { HeapFree(GetProcessHeap(), 0, result); return 0; }
    memcpy(result + output_cursor, text + cursor, text_length - cursor); output_cursor += text_length - cursor;
    result[result_length] = '\0';
    *updated = result;
    *updated_length = result_length;
    return 1;
}

const char *cdx_document_status_name(cdx_document_status status) {
    switch (status) {
    case CDX_STATUS_OK: return "ok"; case CDX_STATUS_PREVIEW: return "preview"; case CDX_STATUS_VALIDATION_ERROR: return "validation_error";
    case CDX_STATUS_NOT_FOUND: return "not_found"; case CDX_STATUS_ENCODING_ERROR: return "encoding_error"; case CDX_STATUS_FILE_CHANGED: return "file_changed";
    case CDX_STATUS_ANCHOR_MISMATCH: return "anchor_mismatch"; case CDX_STATUS_AMBIGUOUS_MATCH: return "ambiguous_match"; case CDX_STATUS_MIXED_EOL: return "mixed_eol";
    case CDX_STATUS_TOO_LARGE: return "too_large"; case CDX_STATUS_IO_ERROR: return "io_error"; case CDX_STATUS_COMMIT_ERROR: return "commit_error";
    default: return "unknown";
    }
}

const char *cdx_encoding_name(cdx_encoding encoding) {
    switch (encoding) { case CDX_ENCODING_UTF8: return "utf-8"; case CDX_ENCODING_UTF8_BOM: return "utf-8-bom"; case CDX_ENCODING_UTF16LE: return "utf-16le"; case CDX_ENCODING_UTF16BE: return "utf-16be"; case CDX_ENCODING_CP936: return "cp936"; default: return "auto"; }
}

const char *cdx_eol_name(cdx_eol eol) {
    switch (eol) { case CDX_EOL_LF: return "lf"; case CDX_EOL_CRLF: return "crlf"; case CDX_EOL_CR: return "cr"; case CDX_EOL_MIXED: return "mixed"; default: return "none"; }
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
    if (!read_file(path, &document->original_bytes, &document->original_size, &document->original_version)) {
        DWORD error = GetLastError(); cdx_document_free(document);
        return error == ERROR_FILE_TOO_LARGE ? CDX_STATUS_TOO_LARGE :
            (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? CDX_STATUS_NOT_FOUND : CDX_STATUS_IO_ERROR;
    }
    if (!cdx_sha256(document->original_bytes, document->original_size, document->original_sha256)) { cdx_document_free(document); return CDX_STATUS_IO_ERROR; }
    encoding = detect_encoding(document->original_bytes, document->original_size, requested_encoding, &offset);
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
        free_bytes(&roundtrip); cdx_document_free(document); return CDX_STATUS_ENCODING_ERROR;
    }
    free_bytes(&roundtrip);
    document->has_bom = (offset != 0);
    document->eol = detect_eol(document->text_utf8, document->text_length);
    document->current_bytes = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, document->original_size + 1u);
    if (!document->current_bytes) { cdx_document_free(document); return CDX_STATUS_IO_ERROR; }
    memcpy(document->current_bytes, document->original_bytes, document->original_size);
    document->current_size = document->original_size;
    memcpy(document->current_sha256, document->original_sha256, sizeof(document->current_sha256));
    return CDX_STATUS_OK;
}

cdx_document_status cdx_document_edit_utf8(cdx_document *document, const char *expected_sha256, const char *old_text, const char *new_text, size_t expected_count, int dry_run, cdx_edit_result *result) {
    char *updated = NULL; size_t updated_length = 0, actual_count = 0; unsigned char *encoded = NULL; size_t encoded_length = 0; cdx_document_status status = CDX_STATUS_OK;
    char next_hash[CDX_SHA256_HEX_LENGTH + 1];
    size_t old_length, new_length;
    int replacement_result;
    if (result) memset(result, 0, sizeof(*result));
    if (!document || !document->text_utf8 || !expected_sha256 || !old_text || !new_text || !*old_text || expected_count == 0 || expected_count > 10000u || (dry_run != 0 && dry_run != 1)) return edit_status(result, CDX_STATUS_VALIDATION_ERROR);
    if (result) result->dirty_in_memory = document->dirty_in_memory;
    old_length = strnlen(old_text, CDX_DOCUMENT_MAX_BYTES + 1u); new_length = strnlen(new_text, CDX_DOCUMENT_MAX_BYTES + 1u);
    if (!cdx_validate_utf8(old_text, old_length) || !cdx_validate_utf8(new_text, new_length)) return edit_status(result, CDX_STATUS_ENCODING_ERROR);
    if (strcmp(expected_sha256, document->current_sha256) != 0) { if (result) result->status = CDX_STATUS_FILE_CHANGED; return CDX_STATUS_FILE_CHANGED; }
    if (document->eol == CDX_EOL_MIXED || document->eol == CDX_EOL_CR) return edit_status(result, CDX_STATUS_MIXED_EOL);
    replacement_result = make_replacement(document->text_utf8, document->text_length, old_text, old_length, new_text, new_length, expected_count, &updated, &updated_length, &actual_count);
    if (replacement_result == 0) return edit_status(result, CDX_STATUS_TOO_LARGE);
    if (replacement_result < 0) { if (result) { result->status = CDX_STATUS_AMBIGUOUS_MATCH; result->actual_count = actual_count; result->expected_count = expected_count; } return CDX_STATUS_AMBIGUOUS_MATCH; }
    if (actual_count != expected_count) { status = actual_count > expected_count ? CDX_STATUS_AMBIGUOUS_MATCH : CDX_STATUS_ANCHOR_MISMATCH; if (result) { result->status = status; result->expected_count = expected_count; result->actual_count = actual_count; } return status; }
    if (detect_eol(updated, updated_length) == CDX_EOL_MIXED || (document->eol == CDX_EOL_CRLF && detect_eol(updated, updated_length) == CDX_EOL_LF) ||
        (document->eol == CDX_EOL_LF && detect_eol(updated, updated_length) == CDX_EOL_CRLF)) { HeapFree(GetProcessHeap(), 0, updated); return edit_status(result, CDX_STATUS_MIXED_EOL); }
    if (!cdx_encode_text(updated, updated_length, document->encoding, &encoded, &encoded_length)) { HeapFree(GetProcessHeap(), 0, updated); return edit_status(result, CDX_STATUS_ENCODING_ERROR); }
    if (encoded_length > CDX_DOCUMENT_MAX_BYTES) { HeapFree(GetProcessHeap(), 0, updated); free_bytes(&encoded); return edit_status(result, CDX_STATUS_TOO_LARGE); }
    if (!cdx_sha256(encoded, encoded_length, next_hash)) { HeapFree(GetProcessHeap(), 0, updated); free_bytes(&encoded); return edit_status(result, CDX_STATUS_IO_ERROR); }
    if (result) { result->status = dry_run ? CDX_STATUS_PREVIEW : CDX_STATUS_OK; result->expected_count = expected_count; result->actual_count = actual_count; result->bytes_before = document->current_size; result->bytes_after = encoded_length; result->dirty_in_memory = document->dirty_in_memory; }
    if (!dry_run) {
        free_bytes(&document->current_bytes);
        document->current_bytes = encoded; document->current_size = encoded_length;
        free_text(&document->text_utf8); document->text_utf8 = updated; document->text_length = updated_length;
        memcpy(document->current_sha256, next_hash, sizeof(next_hash));
        document->dirty_in_memory = strcmp(document->original_sha256, next_hash) != 0;
        document->eol = detect_eol(document->text_utf8, document->text_length);
        if (result) result->dirty_in_memory = document->dirty_in_memory;
        encoded = NULL; updated = NULL;
    }
    if (encoded) HeapFree(GetProcessHeap(), 0, encoded);
    if (updated) HeapFree(GetProcessHeap(), 0, updated);
    return status;
}

cdx_document_status cdx_document_convert_encoding(cdx_document *document, const char *expected_sha256,
                                                  cdx_encoding target_encoding, int dry_run,
                                                  cdx_edit_result *result) {
    unsigned char *encoded = NULL;
    size_t encoded_length = 0;
    char next_hash[CDX_SHA256_HEX_LENGTH + 1];
    if (result) memset(result, 0, sizeof(*result));
    if (!document || !document->text_utf8 || !expected_sha256 || target_encoding <= CDX_ENCODING_AUTO ||
        target_encoding > CDX_ENCODING_CP936 || (dry_run != 0 && dry_run != 1)) return edit_status(result, CDX_STATUS_VALIDATION_ERROR);
    if (result) result->dirty_in_memory = document->dirty_in_memory;
    if (strcmp(expected_sha256, document->current_sha256) != 0) return edit_status(result, CDX_STATUS_FILE_CHANGED);
    if (!cdx_encode_text(document->text_utf8, document->text_length, target_encoding, &encoded, &encoded_length)) return edit_status(result, CDX_STATUS_ENCODING_ERROR);
    if (encoded_length > CDX_DOCUMENT_MAX_BYTES) { free_bytes(&encoded); return edit_status(result, CDX_STATUS_TOO_LARGE); }
    if (!cdx_sha256(encoded, encoded_length, next_hash)) { free_bytes(&encoded); return edit_status(result, CDX_STATUS_IO_ERROR); }
    if (result) { result->bytes_before = document->current_size; result->bytes_after = encoded_length; }
    if (!dry_run) {
        free_bytes(&document->current_bytes);
        document->current_bytes = encoded;
        document->current_size = encoded_length;
        document->encoding = target_encoding;
        document->has_bom = target_encoding == CDX_ENCODING_UTF8_BOM || target_encoding == CDX_ENCODING_UTF16LE || target_encoding == CDX_ENCODING_UTF16BE;
        memcpy(document->current_sha256, next_hash, sizeof(next_hash));
        document->dirty_in_memory = strcmp(document->original_sha256, next_hash) != 0;
        if (result) result->dirty_in_memory = document->dirty_in_memory;
        encoded = NULL;
    }
    free_bytes(&encoded);
    if (result) result->status = dry_run ? CDX_STATUS_PREVIEW : CDX_STATUS_OK;
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
    if (!read_file(document->path, &disk_bytes, &disk_size, &disk_version) || !cdx_sha256(disk_bytes, disk_size, disk_hash)) goto done;
    if (!same_version(&document->original_version, &disk_version) || strcmp(document->original_sha256, disk_hash) != 0) { status = CDX_STATUS_FILE_CHANGED; goto done; }
    free_bytes(&disk_bytes);
    committed_bytes = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, document->current_size + 1u);
    if (!committed_bytes) goto done;
    memcpy(committed_bytes, document->current_bytes, document->current_size);
    _snwprintf_s(temp_path, _countof(temp_path), _TRUNCATE, L"%ls.cdx.tmp.%lu.%llu", document->path, (unsigned long)GetCurrentProcessId(), GetTickCount64());
    temporary = CreateFileW(temp_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
    status = CDX_STATUS_COMMIT_ERROR;
    if (temporary == INVALID_HANDLE_VALUE) goto done;
    temporary_created = 1;
    if (!WriteFile(temporary, document->current_bytes, (DWORD)document->current_size, &written, NULL) || written != (DWORD)document->current_size || !FlushFileBuffers(temporary)) goto done;
    CloseHandle(temporary); temporary = INVALID_HANDLE_VALUE;
    if (!read_file(document->path, &disk_bytes, &disk_size, &disk_version) || !cdx_sha256(disk_bytes, disk_size, disk_hash)) goto done;
    if (!same_version(&document->original_version, &disk_version) || strcmp(document->original_sha256, disk_hash) != 0) { status = CDX_STATUS_FILE_CHANGED; goto done; }
    free_bytes(&disk_bytes);
    /* ReplaceFile preserves target metadata. A failed replace is not retried as a different write. */
    if (!ReplaceFileW(document->path, temp_path, NULL, 0, NULL, NULL)) goto done;
    temporary_created = 0;
    if (result) result->written_to_disk = 1;
    if (!read_file(document->path, &disk_bytes, &disk_size, &disk_version) || !cdx_sha256(disk_bytes, disk_size, disk_hash) ||
        strcmp(disk_hash, document->current_sha256) != 0) goto done;
    free_bytes(&document->original_bytes);
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
    free_bytes(&disk_bytes);
    free_bytes(&committed_bytes);
    if (result) { result->status = status; if (status == CDX_STATUS_OK) memcpy(result->disk_sha256, document->current_sha256, sizeof(result->disk_sha256)); }
    return status;
}

void cdx_document_free(cdx_document *document) {
    if (!document) return;
    if (document->path) free(document->path);
    free_bytes(&document->original_bytes); free_bytes(&document->current_bytes); free_text(&document->text_utf8);
    memset(document, 0, sizeof(*document));
}
