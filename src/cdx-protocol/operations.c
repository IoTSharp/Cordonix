#include "cdx/operations.h"
#include "cdx/process.h"
#include "cdx/document.h"
#include "cdx/filesystem.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <time.h>
#endif

#define CDX_WORKSPACE_SLOTS 16
#define CDX_TEXT_PAGE 32768u
#define CDX_LINE_PAGE_MAX 262144u
#define CDX_LINE_COUNT_MAX 1000u
#define CDX_VERIFY_MAX_FILES 32u

typedef struct text_read_options {
    size_t start_line, line_count, max_bytes, tail_lines;
    int raw;
} text_read_options;

typedef struct workspace_slot { int active; unsigned long long id; cdx_document document; } workspace_slot;
static workspace_slot documents[CDX_WORKSPACE_SLOTS];
static unsigned long long next_document_id = 1;
static unsigned long long call_count;
static unsigned long long disk_reads;
static unsigned long long disk_writes;
static unsigned long long commit_checks;

static void error_result(cdx_json_output *output, const char *status, const char *message) {
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, status, strlen(status));
    cdx_json_raw(output, ",\"message\":"); cdx_json_quote(output, message, strlen(message));
    cdx_json_raw(output, "}");
}
static const char *string_arg(const cdx_json *json, int arguments, const char *key, const char *fallback, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    if (node < 0) { if (!fallback) *valid = 0; return fallback; }
    if (json->nodes[node].type != CDX_JSON_STRING) { *valid = 0; return fallback; }
    return json->nodes[node].string;
}
static unsigned long long integer_arg(const cdx_json *json, int arguments, const char *key,
                                      unsigned long long fallback, unsigned long long maximum, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    double value;
    if (node < 0) return fallback;
    value = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || value < 0 || value > (double)maximum || floor(value) != value) { *valid = 0; return fallback; }
    return (unsigned long long)value;
}
static int boolean_arg(const cdx_json *json, int arguments, const char *key, int fallback, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    if (node < 0) return fallback;
    if (json->nodes[node].type != CDX_JSON_BOOL) { *valid = 0; return fallback; }
    return json->nodes[node].boolean;
}
static cdx_path_char *native_text(const char *text) {
#ifdef _WIN32
    int count;
    wchar_t *value;
    if (!text) return NULL;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!count) return NULL;
    value = (wchar_t *)malloc((size_t)count * sizeof(*value));
    if (value && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, value, count)) { free(value); value = NULL; }
    return value;
#else
    size_t size;
    char *value;
    if (!text) return NULL;
    size = strlen(text) + 1u;
    value = (char *)malloc(size);
    if (value) memcpy(value, text, size);
    return value;
#endif
}
static cdx_encoding encoding_arg(const char *name, int *valid) {
    if (!strcmp(name, "auto")) return CDX_ENCODING_AUTO;
    if (!strcmp(name, "utf-8")) return CDX_ENCODING_UTF8;
    if (!strcmp(name, "utf-8-bom")) return CDX_ENCODING_UTF8_BOM;
    if (!strcmp(name, "utf-16-le")) return CDX_ENCODING_UTF16LE;
    if (!strcmp(name, "utf-16-be")) return CDX_ENCODING_UTF16BE;
    if (!strcmp(name, "cp936") || !strcmp(name, "gbk")) return CDX_ENCODING_CP936;
    *valid = 0; return CDX_ENCODING_AUTO;
}
static workspace_slot *find_document(unsigned long long id) {
    int i;
    for (i = 0; i < CDX_WORKSPACE_SLOTS; ++i) if (documents[i].active && documents[i].id == id) return &documents[i];
    return NULL;
}
static void document_result(cdx_json_output *output, unsigned long long id, const cdx_document *doc) {
    cdx_json_raw(output, "{\"status\":\"ok\",\"document_id\":"); cdx_json_uint(output, id);
    cdx_json_raw(output, ",\"encoding\":"); cdx_json_quote(output, cdx_encoding_name(doc->encoding), strlen(cdx_encoding_name(doc->encoding)));
    cdx_json_raw(output, ",\"eol\":"); cdx_json_quote(output, cdx_eol_name(doc->eol), strlen(cdx_eol_name(doc->eol)));
    cdx_json_raw(output, ",\"disk_sha256\":"); cdx_json_quote(output, doc->original_sha256, 64);
    cdx_json_raw(output, ",\"memory_sha256\":"); cdx_json_quote(output, doc->current_sha256, 64);
    cdx_json_raw(output, ",\"bytes\":"); cdx_json_uint(output, doc->current_size);
    cdx_json_raw(output, ",\"dirty_in_memory\":"); cdx_json_raw(output, doc->dirty_in_memory ? "true" : "false");
    cdx_json_raw(output, ",\"written_to_disk\":false}");
}
static void open_document(const cdx_json *json, int arguments, cdx_json_output *output) {
    int valid = 1, i;
    const char *path = string_arg(json, arguments, "path", NULL, &valid);
    const char *name = string_arg(json, arguments, "encoding", "auto", &valid);
    cdx_encoding encoding = encoding_arg(name, &valid);
    cdx_path_char *wpath;
    cdx_document_status status;
    if (!valid || !(wpath = native_text(path))) { error_result(output, "validation_error", "path and encoding must be valid UTF-8 strings"); return; }
    for (i = 0; i < CDX_WORKSPACE_SLOTS; ++i) if (documents[i].active &&
#ifdef _WIN32
        !_wcsicmp(documents[i].document.path, wpath)) {
#else
        !strcmp(documents[i].document.path, wpath)) {
#endif
        if (encoding != CDX_ENCODING_AUTO && encoding != documents[i].document.encoding &&
            !(encoding == CDX_ENCODING_UTF8 && documents[i].document.encoding == CDX_ENCODING_UTF8_BOM)) {
            free(wpath); error_result(output, "validation_error", "This path is already open with a different encoding; close it explicitly first"); return;
        }
        free(wpath); document_result(output, documents[i].id, &documents[i].document); return;
    }
    for (i = 0; i < CDX_WORKSPACE_SLOTS && documents[i].active; ++i) { }
    if (i == CDX_WORKSPACE_SLOTS) { free(wpath); error_result(output, "workspace_full", "Close a document before opening another"); return; }
    status = cdx_document_open(&documents[i].document, wpath, encoding);
    free(wpath); ++disk_reads;
    if (status != CDX_STATUS_OK) { error_result(output, cdx_document_status_name(status), "Document was not opened; choose an explicit encoding if needed"); return; }
    documents[i].active = 1; documents[i].id = next_document_id++;
    document_result(output, documents[i].id, &documents[i].document);
}
static int parse_read_options(const cdx_json *json, int arguments, text_read_options *options) {
    int valid = 1;
    int has_tail = cdx_json_get(json, arguments, "tail_lines") >= 0;
    options->start_line = (size_t)integer_arg(json, arguments, "start_line", 1, CDX_DOCUMENT_MAX_UTF8_BYTES + 1u, &valid);
    options->line_count = (size_t)integer_arg(json, arguments, "line_count", 200, CDX_LINE_COUNT_MAX, &valid);
    options->max_bytes = (size_t)integer_arg(json, arguments, "max_bytes", CDX_TEXT_PAGE, CDX_LINE_PAGE_MAX, &valid);
    options->tail_lines = (size_t)integer_arg(json, arguments, "tail_lines", 0, CDX_LINE_COUNT_MAX, &valid);
    options->raw = boolean_arg(json, arguments, "raw", 0, &valid);
    if ((options->raw && has_tail) || (has_tail && !options->tail_lines) ||
        ((options->raw || has_tail) && (cdx_json_get(json, arguments, "start_line") >= 0 ||
                                      cdx_json_get(json, arguments, "line_count") >= 0))) return 0;
    return valid && options->start_line && options->line_count && options->max_bytes &&
        cdx_json_get(json, arguments, "offset") < 0 && cdx_json_get(json, arguments, "limit") < 0;
}

static uint64_t read_clock_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static const char *line_scan_stopped(cdx_cancel_handle cancellation, uint64_t deadline) {
#ifdef _WIN32
    if (cancellation && WaitForSingleObject(cancellation, 0) == WAIT_OBJECT_0) return "cancelled";
#else
    if (cancellation && atomic_load(cancellation)) return "cancelled";
#endif
    return read_clock_ms() >= deadline ? "timed_out" : NULL;
}

static void read_output_limit(cdx_json_output *output, size_t required, size_t maximum) {
    cdx_json_raw(output, "{\"status\":\"output_limit\",\"bytes_required\":"); cdx_json_uint(output, required);
    cdx_json_raw(output, ",\"max_bytes\":"); cdx_json_uint(output, maximum);
    cdx_json_raw(output, ",\"message\":\"The complete requested text exceeds max_bytes; raise it up to 262144 or use line/byte paging\"}");
}

static void read_text_range(const cdx_document *doc, const text_read_options *options, const char *source,
                       cdx_cancel_handle cancellation, cdx_json_output *output) {
    size_t position = 0, line = 1, begin, end, count = 0, first = options->start_line;
    const char *mode = options->raw ? "raw" : options->tail_lines ? "tail" : "lines";
    const char *stopped;
    uint64_t deadline = read_clock_ms() + 2000u;
    if (options->raw || options->tail_lines) {
        size_t starts[CDX_LINE_COUNT_MAX], total = 0;
        if (options->raw && doc->text_length > options->max_bytes) {
            read_output_limit(output, doc->text_length, options->max_bytes); return;
        }
        while (position < doc->text_length) {
            if (options->tail_lines) starts[total % options->tail_lines] = position;
            ++total;
            while (position < doc->text_length) {
                char c = doc->text_utf8[position++];
                if (c == '\r' && position < doc->text_length && doc->text_utf8[position] == '\n') ++position;
                if ((position & 4095u) < 2u && (stopped = line_scan_stopped(cancellation, deadline)) != NULL) {
                    error_result(output, stopped, "Text scan stopped without changing the document"); return;
                }
                if (c == '\r' || c == '\n') break;
            }
        }
        count = options->tail_lines && total > options->tail_lines ? options->tail_lines : total;
        first = total - count + 1u;
        begin = options->tail_lines && total > options->tail_lines ? starts[total % options->tail_lines] : 0;
        end = doc->text_length;
        if (end - begin > options->max_bytes) { read_output_limit(output, end - begin, options->max_bytes); return; }
        goto page_complete;
    }
    while (position < doc->text_length && line < options->start_line) {
        char c = doc->text_utf8[position++];
        if (c == '\r') {
            if (position < doc->text_length && doc->text_utf8[position] == '\n') ++position;
            ++line;
        } else if (c == '\n') ++line;
        if ((position & 4095u) < 2u && (stopped = line_scan_stopped(cancellation, deadline)) != NULL) {
            error_result(output, stopped, "Line scan stopped without changing the document"); return;
        }
    }
    begin = end = position;
    while (position < doc->text_length && count < options->line_count) {
        while (position < doc->text_length && doc->text_utf8[position] != '\r' && doc->text_utf8[position] != '\n') {
            ++position;
            if (position - begin > options->max_bytes) goto page_complete;
            if ((position & 4095u) == 0 && (stopped = line_scan_stopped(cancellation, deadline)) != NULL) {
                error_result(output, stopped, "Line scan stopped without changing the document"); return;
            }
        }
        if (position < doc->text_length) {
            char c = doc->text_utf8[position++];
            if (c == '\r' && position < doc->text_length && doc->text_utf8[position] == '\n') ++position;
        }
        if (position - begin > options->max_bytes) break;
        end = position;
        ++count;
    }
page_complete:
    stopped = line_scan_stopped(cancellation, deadline);
    if (stopped) { error_result(output, stopped, "Line scan stopped without changing the document"); return; }
    if (!count && begin < doc->text_length) {
        error_result(output, "line_too_long", "The next whole line exceeds max_bytes; raise it up to 262144 or use workspace_read byte paging"); return;
    }
    cdx_json_raw(output, "{\"status\":\"ok\",\"text\":"); cdx_json_quote(output, doc->text_utf8 + begin, end - begin);
    cdx_json_raw(output, ",\"mode\":"); cdx_json_quote(output, mode, strlen(mode));
    cdx_json_raw(output, ",\"start_line\":"); cdx_json_uint(output, first);
    cdx_json_raw(output, ",\"lines_returned\":"); cdx_json_uint(output, count);
    cdx_json_raw(output, ",\"next_line\":");
    if (end < doc->text_length) cdx_json_uint(output, first + count); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"eof\":"); cdx_json_raw(output, end == doc->text_length ? "true" : "false");
    cdx_json_raw(output, ",\"truncated\":"); cdx_json_raw(output, end < doc->text_length && count < options->line_count ? "true" : "false");
    cdx_json_raw(output, ",\"source\":"); cdx_json_quote(output, source, strlen(source));
    cdx_json_raw(output, ",\"encoding\":"); cdx_json_quote(output, cdx_encoding_name(doc->encoding), strlen(cdx_encoding_name(doc->encoding)));
    cdx_json_raw(output, ",\"eol\":"); cdx_json_quote(output, cdx_eol_name(doc->eol), strlen(cdx_eol_name(doc->eol)));
    cdx_json_raw(output, ",\"memory_sha256\":"); cdx_json_quote(output, doc->current_sha256, 64);
    cdx_json_raw(output, ",\"dirty_in_memory\":"); cdx_json_raw(output, doc->dirty_in_memory ? "true}" : "false}");
}

static void read_file(const cdx_json *json, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output) {
    int valid = 1;
    const char *path = string_arg(json, arguments, "path", NULL, &valid);
    const char *name = string_arg(json, arguments, "encoding", "auto", &valid);
    cdx_encoding encoding = encoding_arg(name, &valid);
    text_read_options options;
    cdx_document document;
    cdx_path_char *native_path;
    cdx_document_status status;
    if (!valid || !parse_read_options(json, arguments, &options) || !(native_path = native_text(path))) {
        error_result(output, "validation_error", "Use path/encoding strings and one read mode: start_line/line_count, tail_lines (1..1000), or raw=true; max_bytes is 1..262144; omit byte offset/limit"); return;
    }
    status = cdx_document_open(&document, native_path, encoding);
    free(native_path); ++disk_reads;
    if (status == CDX_STATUS_OK) read_text_range(&document, &options, "disk", cancellation, output);
    else error_result(output, cdx_document_status_name(status), "File was not read; choose an explicit encoding if needed");
    cdx_document_free(&document);
}

static void read_document(workspace_slot *slot, const cdx_json *json, int arguments,
                          cdx_cancel_handle cancellation, cdx_json_output *output) {
    int valid = 1;
    if (cdx_json_get(json, arguments, "start_line") >= 0 || cdx_json_get(json, arguments, "line_count") >= 0 ||
        cdx_json_get(json, arguments, "max_bytes") >= 0 || cdx_json_get(json, arguments, "tail_lines") >= 0 ||
        cdx_json_get(json, arguments, "raw") >= 0) {
        text_read_options options;
        if (!parse_read_options(json, arguments, &options)) {
            error_result(output, "validation_error", "Choose start_line/line_count, tail_lines (1..1000), or raw=true; max_bytes is 1..262144; omit byte offset/limit"); return;
        }
        read_text_range(&slot->document, &options, "memory", cancellation, output); return;
    }
    size_t offset = (size_t)integer_arg(json, arguments, "offset", 0, CDX_DOCUMENT_MAX_BYTES * 4u, &valid);
    size_t limit = (size_t)integer_arg(json, arguments, "limit", CDX_TEXT_PAGE, CDX_TEXT_PAGE, &valid);
    size_t end, length = slot->document.text_length;
    if (!valid || offset > length || !limit || (offset < length && ((unsigned char)slot->document.text_utf8[offset] & 0xc0u) == 0x80u)) {
        error_result(output, "validation_error", "offset must be a UTF-8 byte boundary; limit must be 1..32768"); return;
    }
    end = offset + limit < length ? offset + limit : length;
    while (end > offset && end < length && ((unsigned char)slot->document.text_utf8[end] & 0xc0u) == 0x80u) --end;
    if (end == offset && offset < length) { error_result(output, "validation_error", "limit is smaller than the next UTF-8 character; use at least 4 bytes"); return; }
    cdx_json_raw(output, "{\"status\":\"ok\",\"text\":"); cdx_json_quote(output, slot->document.text_utf8 + offset, end - offset);
    cdx_json_raw(output, ",\"next_offset\":"); cdx_json_uint(output, end);
    cdx_json_raw(output, ",\"truncated\":"); cdx_json_raw(output, end < length ? "true" : "false");
    cdx_json_raw(output, ",\"memory_sha256\":"); cdx_json_quote(output, slot->document.current_sha256, 64);
    cdx_json_raw(output, ",\"dirty_in_memory\":"); cdx_json_raw(output, slot->document.dirty_in_memory ? "true}" : "false}");
}
static void edit_document(workspace_slot *slot, const cdx_json *json, int arguments, cdx_json_output *output) {
    int valid = 1;
    const char *hash = string_arg(json, arguments, "expected_sha256", NULL, &valid);
    const char *old_text = string_arg(json, arguments, "old", NULL, &valid);
    const char *new_text = string_arg(json, arguments, "new", NULL, &valid);
    size_t count = (size_t)integer_arg(json, arguments, "expected_count", 1, 10000, &valid);
    int dry_run = boolean_arg(json, arguments, "dry_run", 1, &valid);
    cdx_edit_result result;
    cdx_document_status status;
    if (!valid) { error_result(output, "validation_error", "Use string hash/old/new, integer count and boolean dry_run"); return; }
    status = cdx_document_edit_utf8(&slot->document, hash, old_text, new_text, count, dry_run, &result);
    if (status != CDX_STATUS_OK && status != CDX_STATUS_PREVIEW) { error_result(output, cdx_document_status_name(status), "No disk write; reread the current revision and regenerate the edit"); return; }
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, cdx_document_status_name(result.status), strlen(cdx_document_status_name(result.status)));
    cdx_json_raw(output, ",\"written_to_disk\":false,\"dirty_in_memory\":"); cdx_json_raw(output, slot->document.dirty_in_memory ? "true" : "false");
    cdx_json_raw(output, ",\"memory_sha256\":"); cdx_json_quote(output, slot->document.current_sha256, 64);
    cdx_json_raw(output, ",\"matches\":"); cdx_json_uint(output, result.actual_count); cdx_json_raw(output, "}");
}
static void commit_document(workspace_slot *slot, cdx_json_output *output) {
    cdx_commit_result result;
    int dirty = slot->document.dirty_in_memory;
    cdx_document_status status = cdx_document_commit(&slot->document, &result);
    if (dirty) ++commit_checks;
    if (result.written_to_disk) ++disk_writes;
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, cdx_document_status_name(status), strlen(cdx_document_status_name(status)));
    cdx_json_raw(output, ",\"written_to_disk\":"); cdx_json_raw(output, result.written_to_disk ? "true" : "false");
    cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, result.cleanup_complete ? "true" : "false");
    cdx_json_raw(output, ",\"dirty_in_memory\":"); cdx_json_raw(output, slot->document.dirty_in_memory ? "true" : "false");
    cdx_json_raw(output, ",\"disk_sha256\":"); cdx_json_quote(output, result.disk_sha256, strlen(result.disk_sha256)); cdx_json_raw(output, "}");
}
static void convert_document(workspace_slot *slot, const cdx_json *json, int arguments, cdx_json_output *output) {
    int valid = 1;
    const char *hash = string_arg(json, arguments, "expected_sha256", NULL, &valid);
    const char *name = string_arg(json, arguments, "encoding", NULL, &valid);
    int dry_run = boolean_arg(json, arguments, "dry_run", 1, &valid);
    cdx_encoding target = name ? encoding_arg(name, &valid) : CDX_ENCODING_AUTO;
    cdx_edit_result result;
    cdx_document_status status;
    if (!valid || target == CDX_ENCODING_AUTO) { error_result(output, "validation_error", "Choose an explicit target encoding and supply the current memory SHA"); return; }
    status = cdx_document_convert_encoding(&slot->document, hash, target, dry_run, &result);
    if (status != CDX_STATUS_OK) { error_result(output, cdx_document_status_name(status), "Unicode content cannot be converted losslessly, or memory revision changed; nothing was written"); return; }
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, cdx_document_status_name(result.status), strlen(cdx_document_status_name(result.status)));
    cdx_json_raw(output, ",\"encoding\":"); cdx_json_quote(output, cdx_encoding_name(slot->document.encoding), strlen(cdx_encoding_name(slot->document.encoding)));
    cdx_json_raw(output, ",\"memory_sha256\":"); cdx_json_quote(output, slot->document.current_sha256, 64);
    cdx_json_raw(output, ",\"dirty_in_memory\":"); cdx_json_raw(output, slot->document.dirty_in_memory ? "true" : "false");
    cdx_json_raw(output, ",\"written_to_disk\":false}");
}
static void search_document(workspace_slot *slot, const cdx_json *json, int arguments, cdx_json_output *output) {
    int valid = 1;
    const char *pattern = string_arg(json, arguments, "pattern", NULL, &valid);
    const char *cursor = slot->document.text_utf8;
    size_t count = 0, pattern_size;
    if (!valid || !*pattern) { error_result(output, "validation_error", "pattern must be a nonempty literal UTF-8 string"); return; }
    pattern_size = strlen(pattern);
    cdx_json_raw(output, "{\"offsets\":[");
    for (count = 0; count < 100; ++count) {
        const char *found = strstr(cursor, pattern);
        if (!found) break;
        if (count) cdx_json_raw(output, ",");
        cdx_json_uint(output, (unsigned long long)(found - slot->document.text_utf8));
        cursor = found + pattern_size;
    }
    cdx_json_raw(output, "],\"status\":"); cdx_json_raw(output, count ? "\"ok\"" : "\"no_match\"");
    cdx_json_raw(output, ",\"truncated\":"); cdx_json_raw(output, count == 100 && strstr(cursor, pattern) ? "true" : "false");
    cdx_json_raw(output, ",\"source\":\"memory\"}");
}
typedef struct decoded_output { char *text; size_t length; int valid; } decoded_output;
static decoded_output decode_output(const cdx_byte_buffer *buffer, const char *encoding) {
#ifdef _WIN32
    int wide_count, utf8_count;
    UINT codepage = !strcmp(encoding, "cp936") || !strcmp(encoding, "gbk") ? 936u : CP_UTF8;
    wchar_t *wtext;
    decoded_output decoded = {NULL, 0, 0};
    if (!buffer->size) { decoded.valid = 1; return decoded; }
    wide_count = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, (const char *)buffer->data, (int)buffer->size, NULL, 0);
    if (!wide_count) return decoded;
    wtext = (wchar_t *)malloc((size_t)wide_count * sizeof(*wtext));
    if (!wtext) return decoded;
    (void)MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, (const char *)buffer->data, (int)buffer->size, wtext, wide_count);
    utf8_count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wtext, wide_count, NULL, 0, NULL, NULL);
    if (!utf8_count) { free(wtext); return decoded; }
    decoded.text = (char *)malloc((size_t)utf8_count + 1u);
    if (!decoded.text) { free(wtext); return decoded; }
    (void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wtext, wide_count, decoded.text, utf8_count, NULL, NULL);
    decoded.valid = 1; decoded.length = (size_t)utf8_count; free(wtext); return decoded;
#else
    extern int cdx_decode_to_utf8(const unsigned char *, size_t, cdx_encoding, char **, size_t *);
    decoded_output decoded = {NULL, 0, 0};
    cdx_encoding selected = !strcmp(encoding, "utf-8") ? CDX_ENCODING_UTF8 : CDX_ENCODING_CP936;
    decoded.valid = cdx_decode_to_utf8(buffer->data, buffer->size, selected, &decoded.text, &decoded.length);
    return decoded;
#endif
}
static void base64_output(cdx_json_output *output, const cdx_byte_buffer *buffer) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i;
    cdx_json_raw(output, "\"");
    for (i = 0; i < buffer->size; i += 3) {
        unsigned a = buffer->data[i], b = i + 1 < buffer->size ? buffer->data[i + 1] : 0, c = i + 2 < buffer->size ? buffer->data[i + 2] : 0;
        char group[4] = {alphabet[a >> 2], alphabet[((a & 3u) << 4) | (b >> 4)],
            i + 1 < buffer->size ? alphabet[((b & 15u) << 2) | (c >> 6)] : '=', i + 2 < buffer->size ? alphabet[c & 63u] : '='};
        cdx_json_rawn(output, group, 4);
    }
    cdx_json_raw(output, "\"");
}
static void execute_program(const cdx_json *json, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output) {
    int valid = 1, array, node;
    const char *program = string_arg(json, arguments, "program", NULL, &valid);
    const char *cwd = string_arg(json, arguments, "cwd", NULL, &valid);
    const char *stdin_text = string_arg(json, arguments, "stdin", "", &valid);
    const char *encoding = string_arg(json, arguments, "output_encoding", "utf-8", &valid);
    cdx_path_char *wprogram = NULL, *wcwd = NULL, *argv[CDX_MAX_ARGS] = {0};
    int expected[32] = {0};
    size_t arg_count = 0, expected_count = 1, i;
    cdx_exec_request request;
    cdx_exec_result result;
    memset(&request, 0, sizeof(request));
    request.timeout_ms = (uint32_t)integer_arg(json, arguments, "timeout_ms", 30000, CDX_MAX_TIMEOUT_MS, &valid);
    request.output_limit_bytes = (size_t)integer_arg(json, arguments, "output_limit_bytes", 32768, 262144, &valid);
    array = cdx_json_get(json, arguments, "args");
    if (array < 0 || json->nodes[array].type != CDX_JSON_ARRAY) valid = 0;
    else for (node = json->nodes[array].first; node >= 0; node = json->nodes[node].next) {
        if (arg_count == CDX_MAX_ARGS || json->nodes[node].type != CDX_JSON_STRING) { valid = 0; break; }
        argv[arg_count] = native_text(json->nodes[node].string);
        if (!argv[arg_count]) { valid = 0; break; }
        ++arg_count;
    }
    array = cdx_json_get(json, arguments, "expected_exit_codes");
    if (array >= 0) {
        expected_count = 0;
        if (json->nodes[array].type != CDX_JSON_ARRAY) valid = 0;
        else for (node = json->nodes[array].first; node >= 0; node = json->nodes[node].next) {
            double code = json->nodes[node].number;
            if (expected_count == 32 || json->nodes[node].type != CDX_JSON_NUMBER || code < 0 || code > 2147483647 || floor(code) != code) { valid = 0; break; }
            expected[expected_count++] = (int)code;
        }
    }
    if (strcmp(encoding, "utf-8") && strcmp(encoding, "cp936") && strcmp(encoding, "gbk")) valid = 0;
    if (valid) { wprogram = native_text(program); wcwd = native_text(cwd); valid = wprogram && wcwd; }
    if (!valid) { error_result(output, "validation_error", "Use program/cwd strings, args array, and bounded numeric limits"); }
    else {
        decoded_output stdout_text, stderr_text;
        const char *status;
        request.program = wprogram; request.cwd = wcwd; request.args = (const cdx_path_char *const *)argv; request.arg_count = arg_count;
        request.expected_exit_codes = expected; request.expected_exit_count = expected_count;
        request.stdin_utf8 = (const unsigned char *)stdin_text; request.stdin_size = strlen(stdin_text); request.cancel_event = cancellation;
        cdx_result_init(&result); (void)cdx_execute(&request, &result);
        stdout_text = decode_output(&result.stdout_bytes, encoding); stderr_text = decode_output(&result.stderr_bytes, encoding);
        status = stdout_text.valid && stderr_text.valid ? cdx_status_name(result.status) : "encoding_error";
        cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, status, strlen(status));
        cdx_json_raw(output, ",\"process_status\":"); cdx_json_quote(output, cdx_status_name(result.status), strlen(cdx_status_name(result.status)));
        cdx_json_raw(output, ",\"exit_code\":"); if (result.exit_code_known) cdx_json_uint(output, result.exit_code); else cdx_json_raw(output, "null");
        cdx_json_raw(output, ",\"pid\":"); cdx_json_uint(output, result.pid);
        cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, result.cleanup_complete ? "true" : "false");
        cdx_json_raw(output, ",\"stdout\":"); if (stdout_text.valid) cdx_json_quote(output, stdout_text.text, stdout_text.length); else cdx_json_raw(output, "null");
        cdx_json_raw(output, ",\"stderr\":"); if (stderr_text.valid) cdx_json_quote(output, stderr_text.text, stderr_text.length); else cdx_json_raw(output, "null");
        if (!stdout_text.valid) { cdx_json_raw(output, ",\"stdout_base64\":"); base64_output(output, &result.stdout_bytes); }
        if (!stderr_text.valid) { cdx_json_raw(output, ",\"stderr_base64\":"); base64_output(output, &result.stderr_bytes); }
        cdx_json_raw(output, ",\"output_truncated\":"); cdx_json_raw(output, result.output_truncated ? "true" : "false");
        cdx_json_raw(output, ",\"elapsed_ms\":"); cdx_json_uint(output, result.elapsed_ms);
        cdx_json_raw(output, ",\"output_encoding\":"); cdx_json_quote(output, encoding, strlen(encoding));
#ifdef _WIN32
        cdx_json_raw(output, ",\"win32_error\":"); cdx_json_uint(output, result.win32_error);
#else
        cdx_json_raw(output, ",\"posix_error\":"); cdx_json_uint(output, (unsigned)result.posix_error);
#endif
        cdx_json_raw(output, "}");
        cdx_result_free(&result); free(stdout_text.text); free(stderr_text.text);
    }
    for (i = 0; i < arg_count; ++i) free(argv[i]);
    free(wprogram); free(wcwd);
}

typedef struct encoding_verification {
    const char *status;
    cdx_encoding actual_encoding;
    size_t bytes;
    char sha256[CDX_SHA256_HEX_LENGTH + 1];
    int checked, has_metadata;
} encoding_verification;

static const char *prepare_verification_paths(const char *const *paths, size_t count, const char *root,
                                              cdx_path_char **normalized, cdx_cancel_handle cancellation,
                                              uint64_t deadline, const char **message) {
    cdx_path_char *native = NULL, *normalized_root = NULL;
    cdx_fs_status status;
    unsigned long native_error = 0;
    const char *error = NULL;
    size_t i;
    *message = "Expected normalized absolute file paths and an absolute allowed_root";
    if (*root) {
        native = native_text(root);
        if (!native) { error = "io_error"; goto done; }
        status = cdx_fs_normalize_path(native, cancellation, &normalized_root, &native_error);
        free(native); native = NULL;
        if (status != CDX_FS_OK) { error = cdx_fs_status_name(status); goto done; }
    }
    for (i = 0; i < count; ++i) {
        error = line_scan_stopped(cancellation, deadline);
        if (error) goto done;
        native = native_text(paths[i]);
        if (!native) { error = "io_error"; goto done; }
        status = cdx_fs_normalize_path(native, cancellation, &normalized[i], &native_error);
        free(native); native = NULL;
        if (status != CDX_FS_OK) { error = cdx_fs_status_name(status); goto done; }
        if (normalized_root && !cdx_fs_path_within(normalized[i], normalized_root)) {
            *message = "A normalized path is outside allowed_root; no file contents were read";
            error = "validation_error"; goto done;
        }
    }
    error = line_scan_stopped(cancellation, deadline);
done:
    free(native); free(normalized_root);
    return error;
}

static void encoding_verification_result(const char *const *paths, size_t count, cdx_encoding encoding,
                                         const encoding_verification *results, size_t checked, size_t passed,
                                         const char *stopped, cdx_json_output *output) {
    size_t i, failed = checked - passed;
    int complete = !stopped && passed == count;
    const char *status = stopped ? stopped : complete ? "ok" : "failed";
    const char *name = cdx_encoding_name(encoding);
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, status, strlen(status));
    cdx_json_raw(output, ",\"encoding\":"); cdx_json_quote(output, name, strlen(name));
    cdx_json_raw(output, ",\"files\":"); cdx_json_uint(output, count);
    cdx_json_raw(output, ",\"checked\":"); cdx_json_uint(output, checked);
    cdx_json_raw(output, ",\"passed\":"); cdx_json_uint(output, passed);
    cdx_json_raw(output, ",\"failed\":"); cdx_json_uint(output, failed);
    cdx_json_raw(output, ",\"not_run\":"); cdx_json_uint(output, count - checked);
    cdx_json_raw(output, complete ?
        ",\"strict_decode\":\"PASS\",\"no_nul\":\"PASS\",\"roundtrip\":\"PASS\"" :
        ",\"strict_decode\":\"UNKNOWN\",\"no_nul\":\"UNKNOWN\",\"roundtrip\":\"UNKNOWN\"");
    cdx_json_raw(output, ",\"written_to_disk\":false,\"results\":[");
    for (i = 0; i < count; ++i) {
        const encoding_verification *item = &results[i];
        const char *item_status = item->checked ? item->status : "not_run";
        const char *verification = !item->checked ? "NOT_RUN" : !strcmp(item_status, "ok") ? "PASS" : "FAIL";
        if (i) cdx_json_raw(output, ",");
        cdx_json_raw(output, "{\"path\":"); cdx_json_quote(output, paths[i], strlen(paths[i]));
        cdx_json_raw(output, ",\"status\":"); cdx_json_quote(output, item_status, strlen(item_status));
        cdx_json_raw(output, ",\"verification\":"); cdx_json_quote(output, verification, strlen(verification));
        if (item->has_metadata) {
            name = cdx_encoding_name(item->actual_encoding);
            cdx_json_raw(output, ",\"encoding\":"); cdx_json_quote(output, name, strlen(name));
            cdx_json_raw(output, ",\"bytes\":"); cdx_json_uint(output, item->bytes);
            cdx_json_raw(output, ",\"sha256\":"); cdx_json_quote(output, item->sha256, CDX_SHA256_HEX_LENGTH);
        }
        cdx_json_raw(output, "}");
    }
    cdx_json_raw(output, "]}");
}

static void verify_encoding(const cdx_json *json, int arguments, cdx_cancel_handle cancellation,
                            cdx_json_output *output) {
    int valid = 1, array = cdx_json_get(json, arguments, "paths"), node;
    const char *paths[CDX_VERIFY_MAX_FILES] = {0};
    cdx_path_char *normalized[CDX_VERIFY_MAX_FILES] = {0};
    encoding_verification results[CDX_VERIFY_MAX_FILES] = {{0}};
    const char *name = string_arg(json, arguments, "encoding", NULL, &valid);
    const char *root = string_arg(json, arguments, "allowed_root", "", &valid);
    const char *stopped = NULL, *message = NULL;
    cdx_encoding encoding = name ? encoding_arg(name, &valid) : CDX_ENCODING_AUTO;
    size_t count = 0, checked = 0, passed = 0, expected, i;
    unsigned long long timeout = integer_arg(json, arguments, "timeout_ms", 10000, 60000, &valid);
    uint64_t deadline = read_clock_ms() + timeout;
    if (array < 0 || json->nodes[array].type != CDX_JSON_ARRAY) valid = 0;
    else for (node = json->nodes[array].first; node >= 0; node = json->nodes[node].next) {
        if (count == CDX_VERIFY_MAX_FILES || json->nodes[node].type != CDX_JSON_STRING) { valid = 0; break; }
        paths[count++] = json->nodes[node].string;
    }
    expected = (size_t)integer_arg(json, arguments, "expected_count", count, CDX_VERIFY_MAX_FILES, &valid);
    if (!valid || !count || expected != count || !timeout || encoding == CDX_ENCODING_AUTO ||
        (cdx_json_get(json, arguments, "allowed_root") >= 0 && !*root)) {
        error_result(output, "validation_error", "Supply 1..32 paths, an explicit encoding (not auto), an optional matching expected_count, nonempty allowed_root and timeout_ms 1..60000"); return;
    }
    stopped = prepare_verification_paths(paths, count, root, normalized, cancellation, deadline, &message);
    if (stopped && strcmp(stopped, "timed_out") && strcmp(stopped, "cancelled")) {
        error_result(output, stopped, message); goto done;
    }
    for (i = 0; !stopped && i < count; ++i) {
        cdx_document document;
        cdx_document_status status;
        stopped = line_scan_stopped(cancellation, deadline);
        if (stopped) break;
        status = cdx_document_open(&document, normalized[i], encoding);
        ++disk_reads; ++checked;
        results[i].checked = 1;
        results[i].status = cdx_document_status_name(status);
        if (status == CDX_STATUS_OK) {
            results[i].has_metadata = 1;
            results[i].actual_encoding = document.encoding;
            results[i].bytes = document.original_size;
            memcpy(results[i].sha256, document.original_sha256, sizeof(results[i].sha256));
            /* A BOM may select a different decoder during document_open. */
            if (document.encoding != encoding && !(encoding == CDX_ENCODING_UTF8 && document.encoding == CDX_ENCODING_UTF8_BOM))
                results[i].status = "encoding_mismatch";
            else ++passed;
        }
        cdx_document_free(&document);
        stopped = line_scan_stopped(cancellation, deadline);
    }
    encoding_verification_result(paths, count, encoding, results, checked, passed, stopped, output);
done:
    for (i = 0; i < count; ++i) free(normalized[i]);
}

static void query_filesystem(const cdx_json *json, int arguments, int list,
                             cdx_cancel_handle cancellation, cdx_json_output *output) {
    int valid = 1;
    const char *path = string_arg(json, arguments, "path", NULL, &valid);
    cdx_fs_request request;
    cdx_fs_result result;
    cdx_path_char *native_path;
    const char *status, *kind;
    size_t i;
    memset(&request, 0, sizeof(request));
    request.list_directory = list;
    request.include_hidden = list ? boolean_arg(json, arguments, "include_hidden", 0, &valid) : 0;
    request.max_entries = list ? (size_t)integer_arg(json, arguments, "max_entries", 200, CDX_FS_MAX_ENTRIES, &valid) : 200;
    request.cancellation = cancellation;
    if (!valid || !request.max_entries || !(native_path = native_text(path))) {
        error_result(output, "validation_error", "Use an absolute path string; list_directory accepts boolean include_hidden and max_entries 1..1000"); return;
    }
    request.path = native_path;
    (void)cdx_fs_query(&request, &result);
    free(native_path);
    status = cdx_fs_status_name(result.status);
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, status, strlen(status));
    if (result.path) { cdx_json_raw(output, ",\"path\":"); cdx_json_quote(output, result.path, strlen(result.path)); }
    if (result.status == CDX_FS_OK) {
        kind = cdx_fs_kind_name(result.kind);
        cdx_json_raw(output, ",\"type\":"); cdx_json_quote(output, kind, strlen(kind));
        if (list) {
            cdx_json_raw(output, ",\"entries\":[");
            for (i = 0; i < result.entry_count; ++i) {
                const cdx_fs_entry *entry = &result.entries[i];
                if (i) cdx_json_raw(output, ",");
                cdx_json_raw(output, "{\"name\":"); cdx_json_quote(output, entry->name, strlen(entry->name));
                kind = cdx_fs_kind_name(entry->kind);
                cdx_json_raw(output, ",\"type\":"); cdx_json_quote(output, kind, strlen(kind));
                cdx_json_raw(output, ",\"size_bytes\":");
                if (entry->size_known) cdx_json_uint(output, entry->size_bytes); else cdx_json_raw(output, "null");
                cdx_json_raw(output, ",\"hidden\":"); cdx_json_raw(output, entry->hidden ? "true" : "false");
                cdx_json_raw(output, ",\"system\":"); cdx_json_raw(output, entry->system ? "true}" : "false}");
            }
            cdx_json_raw(output, "],\"returned\":"); cdx_json_uint(output, result.entry_count);
            cdx_json_raw(output, ",\"truncated\":"); cdx_json_raw(output, result.truncated ? "true" : "false");
            cdx_json_raw(output, ",\"include_hidden\":"); cdx_json_raw(output, request.include_hidden ? "true" : "false");
        }
    } else {
        cdx_json_raw(output, ",\"message\":\"Filesystem query did not complete; no files were changed\""
#ifdef _WIN32
            ",\"win32_error\":");
#else
            ",\"posix_error\":");
#endif
        cdx_json_uint(output, result.native_error);
    }
    cdx_json_raw(output, "}");
    cdx_fs_result_free(&result);
}

void cdx_call(const char *operation, const cdx_json *request, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output) {
    int valid = 1;
    unsigned long long id;
    workspace_slot *slot;
    ++call_count;
    if (arguments >= 0 && request->nodes[arguments].type != CDX_JSON_OBJECT) { error_result(output, "validation_error", "arguments must be an object"); return; }
    if (!strcmp(operation, "tool_info")) {
        cdx_json_raw(output, "{\"status\":\"ok\",\"name\":\"Cordonix\",\"version\":\"0.1.0-dev\",\"platform\":\""
#ifdef _WIN32
            "windows"
#else
            "linux"
#endif
            "\",\"runtime\":\"C11\",\"calls\":");
        cdx_json_uint(output, call_count); cdx_json_raw(output, ",\"document_loads\":"); cdx_json_uint(output, disk_reads);
        cdx_json_raw(output, ",\"document_commits\":"); cdx_json_uint(output, disk_writes);
        cdx_json_raw(output, ",\"commit_checks\":"); cdx_json_uint(output, commit_checks);
        cdx_json_raw(output, ",\"counter_scope\":\"logical operations, not physical disk IO\",\"max_documents\":16,\"max_document_bytes\":8388608}"); return;
    }
    if (arguments < 0) { error_result(output, "validation_error", "arguments must be an object"); return; }
    if (!strcmp(operation, "execute_program")) { execute_program(request, arguments, cancellation, output); return; }
    if (!strcmp(operation, "workspace_open")) { open_document(request, arguments, output); return; }
    if (!strcmp(operation, "read_file")) { read_file(request, arguments, cancellation, output); return; }
    if (!strcmp(operation, "verify_encoding")) { verify_encoding(request, arguments, cancellation, output); return; }
    if (!strcmp(operation, "list_directory")) { query_filesystem(request, arguments, 1, cancellation, output); return; }
    if (!strcmp(operation, "resolve_path")) { query_filesystem(request, arguments, 0, cancellation, output); return; }
    id = integer_arg(request, arguments, "document_id", 0, 9007199254740991ull, &valid);
    slot = valid ? find_document(id) : NULL;
    if (!slot) { error_result(output, "not_found", "No live document with this id; open it in this MCP session"); return; }
    if (!strcmp(operation, "workspace_read")) read_document(slot, request, arguments, cancellation, output);
    else if (!strcmp(operation, "workspace_edit")) edit_document(slot, request, arguments, output);
    else if (!strcmp(operation, "workspace_commit")) commit_document(slot, output);
    else if (!strcmp(operation, "workspace_convert_encoding")) convert_document(slot, request, arguments, output);
    else if (!strcmp(operation, "workspace_status")) document_result(output, id, &slot->document);
    else if (!strcmp(operation, "workspace_search")) search_document(slot, request, arguments, output);
    else if (!strcmp(operation, "workspace_close")) {
        int discard = boolean_arg(request, arguments, "discard", 0, &valid);
        if (!valid || (slot->document.dirty_in_memory && !discard)) { error_result(output, "dirty_in_memory", "Commit first, or explicitly set discard=true"); return; }
        cdx_document_free(&slot->document); memset(slot, 0, sizeof(*slot)); cdx_json_raw(output, "{\"status\":\"ok\",\"closed\":true}");
    } else error_result(output, "validation_error", "Unknown operation");
}
void cdx_workspace_clear(void) {
    int i;
    for (i = 0; i < CDX_WORKSPACE_SLOTS; ++i) if (documents[i].active) {
        if (documents[i].document.dirty_in_memory) (void)fprintf(stderr, "Cordonix: discarded uncommitted document %llu on session end\n", documents[i].id);
        cdx_document_free(&documents[i].document); memset(&documents[i], 0, sizeof(documents[i]));
    }
}
