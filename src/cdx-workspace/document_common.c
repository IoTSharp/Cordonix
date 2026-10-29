#define _POSIX_C_SOURCE 200809L
#include "cdx/document.h"
#include "document_internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <time.h>
#endif

static uint64_t document_milliseconds(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

void cdx_free_bytes(unsigned char **value) { if (value && *value) free(*value); if (value) *value = NULL; }
void cdx_free_text(char **value) { if (value && *value) free(*value); if (value) *value = NULL; }

static cdx_document_status edit_status(cdx_edit_result *result, cdx_document_status status) {
    if (result) result->status = status;
    return status;
}

cdx_encoding cdx_detect_encoding(const unsigned char *bytes, size_t length, cdx_encoding requested, size_t *offset) {
    if (length >= 3u && bytes[0] == 0xefu && bytes[1] == 0xbbu && bytes[2] == 0xbfu) { *offset = 3u; return CDX_ENCODING_UTF8_BOM; }
    if (length >= 2u && bytes[0] == 0xffu && bytes[1] == 0xfeu) { *offset = 2u; return CDX_ENCODING_UTF16LE; }
    if (length >= 2u && bytes[0] == 0xfeu && bytes[1] == 0xffu) { *offset = 2u; return CDX_ENCODING_UTF16BE; }
    *offset = 0;
    return requested;
}

cdx_eol cdx_detect_eol(const char *text, size_t length) {
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
    uint64_t deadline = document_milliseconds() + 15000u;
    while (index + old_length <= text_length && document_milliseconds() < deadline) {
        if (memcmp(text + index, old_text, old_length) == 0) {
            if (count != 0 && index < previous_end) overlap = 1;
            ++count; previous_end = index + old_length;
        }
        ++index;
    }
    if (document_milliseconds() >= deadline) return 0;
    *actual_count = count;
    if (overlap) return -1;
    if (count != expected_count || count == 0) return 1;
    if (new_length > SIZE_MAX / count || old_length > SIZE_MAX / count) return 0;
    result_length = text_length + count * new_length - count * old_length;
    /* CP936 can expand to three UTF-8 bytes per source byte; the disk limit is checked after encoding. */
    if (result_length > CDX_DOCUMENT_MAX_UTF8_BYTES) return 0;
    result = (char *)malloc(result_length + 1u);
    if (!result) return 0;
    index = 0; cursor = 0; output_cursor = 0;
    while (index + old_length <= text_length && document_milliseconds() < deadline) {
        if (memcmp(text + index, old_text, old_length) == 0) {
            size_t before = index - cursor;
            memcpy(result + output_cursor, text + cursor, before); output_cursor += before;
            memcpy(result + output_cursor, new_text, new_length); output_cursor += new_length;
            index += old_length; cursor = index;
        } else {
            ++index;
        }
    }
    if (document_milliseconds() >= deadline) { free(result); return 0; }
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
    if (cdx_detect_eol(updated, updated_length) == CDX_EOL_MIXED || (document->eol == CDX_EOL_CRLF && cdx_detect_eol(updated, updated_length) == CDX_EOL_LF) ||
        (document->eol == CDX_EOL_LF && cdx_detect_eol(updated, updated_length) == CDX_EOL_CRLF)) { free(updated); return edit_status(result, CDX_STATUS_MIXED_EOL); }
    if (!cdx_encode_text(updated, updated_length, document->encoding, &encoded, &encoded_length)) { free(updated); return edit_status(result, CDX_STATUS_ENCODING_ERROR); }
    if (encoded_length > CDX_DOCUMENT_MAX_BYTES) { free(updated); cdx_free_bytes(&encoded); return edit_status(result, CDX_STATUS_TOO_LARGE); }
    if (!cdx_sha256(encoded, encoded_length, next_hash)) { free(updated); cdx_free_bytes(&encoded); return edit_status(result, CDX_STATUS_IO_ERROR); }
    if (result) { result->status = dry_run ? CDX_STATUS_PREVIEW : CDX_STATUS_OK; result->expected_count = expected_count; result->actual_count = actual_count; result->bytes_before = document->current_size; result->bytes_after = encoded_length; result->dirty_in_memory = document->dirty_in_memory; }
    if (!dry_run) {
        cdx_free_bytes(&document->current_bytes);
        document->current_bytes = encoded; document->current_size = encoded_length;
        cdx_free_text(&document->text_utf8); document->text_utf8 = updated; document->text_length = updated_length;
        memcpy(document->current_sha256, next_hash, sizeof(next_hash));
        document->dirty_in_memory = strcmp(document->original_sha256, next_hash) != 0;
        document->eol = cdx_detect_eol(document->text_utf8, document->text_length);
        if (result) result->dirty_in_memory = document->dirty_in_memory;
        encoded = NULL; updated = NULL;
    }
    if (encoded) free(encoded);
    if (updated) free(updated);
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
    if (encoded_length > CDX_DOCUMENT_MAX_BYTES) { cdx_free_bytes(&encoded); return edit_status(result, CDX_STATUS_TOO_LARGE); }
    if (!cdx_sha256(encoded, encoded_length, next_hash)) { cdx_free_bytes(&encoded); return edit_status(result, CDX_STATUS_IO_ERROR); }
    if (result) { result->bytes_before = document->current_size; result->bytes_after = encoded_length; }
    if (!dry_run) {
        cdx_free_bytes(&document->current_bytes);
        document->current_bytes = encoded;
        document->current_size = encoded_length;
        document->encoding = target_encoding;
        document->has_bom = target_encoding == CDX_ENCODING_UTF8_BOM || target_encoding == CDX_ENCODING_UTF16LE || target_encoding == CDX_ENCODING_UTF16BE;
        memcpy(document->current_sha256, next_hash, sizeof(next_hash));
        document->dirty_in_memory = strcmp(document->original_sha256, next_hash) != 0;
        if (result) result->dirty_in_memory = document->dirty_in_memory;
        encoded = NULL;
    }
    cdx_free_bytes(&encoded);
    if (result) result->status = dry_run ? CDX_STATUS_PREVIEW : CDX_STATUS_OK;
    return CDX_STATUS_OK;
}


void cdx_document_free(cdx_document *document) {
    if (!document) return;
    free(document->path);
    cdx_free_bytes(&document->original_bytes); cdx_free_bytes(&document->current_bytes); cdx_free_text(&document->text_utf8);
    memset(document, 0, sizeof(*document));
}
