#include "cdx/operations.h"
#include "cdx/process.h"
#include "cdx/document.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CDX_WORKSPACE_SLOTS 16
#define CDX_TEXT_PAGE 32768u

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
static wchar_t *wide(const char *text) {
    int count;
    wchar_t *value;
    if (!text) return NULL;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!count) return NULL;
    value = (wchar_t *)malloc((size_t)count * sizeof(*value));
    if (value && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, value, count)) { free(value); value = NULL; }
    return value;
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
    wchar_t *wpath;
    cdx_document_status status;
    if (!valid || !(wpath = wide(path))) { error_result(output, "validation_error", "path and encoding must be valid UTF-8 strings"); return; }
    for (i = 0; i < CDX_WORKSPACE_SLOTS; ++i) if (documents[i].active && !_wcsicmp(documents[i].document.path, wpath)) {
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
static void read_document(workspace_slot *slot, const cdx_json *json, int arguments, cdx_json_output *output) {
    int valid = 1;
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
static void execute_program(const cdx_json *json, int arguments, HANDLE cancellation, cdx_json_output *output) {
    int valid = 1, array, node;
    const char *program = string_arg(json, arguments, "program", NULL, &valid);
    const char *cwd = string_arg(json, arguments, "cwd", NULL, &valid);
    const char *stdin_text = string_arg(json, arguments, "stdin", "", &valid);
    const char *encoding = string_arg(json, arguments, "output_encoding", "utf-8", &valid);
    wchar_t *wprogram = NULL, *wcwd = NULL, *argv[CDX_MAX_ARGS] = {0};
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
        argv[arg_count] = wide(json->nodes[node].string);
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
    if (valid) { wprogram = wide(program); wcwd = wide(cwd); valid = wprogram && wcwd; }
    if (!valid) { error_result(output, "validation_error", "Use program/cwd strings, args array, and bounded numeric limits"); }
    else {
        decoded_output stdout_text, stderr_text;
        const char *status;
        request.program = wprogram; request.cwd = wcwd; request.args = (const wchar_t *const *)argv; request.arg_count = arg_count;
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
        cdx_json_raw(output, ",\"win32_error\":"); cdx_json_uint(output, result.win32_error); cdx_json_raw(output, "}");
        cdx_result_free(&result); free(stdout_text.text); free(stderr_text.text);
    }
    for (i = 0; i < arg_count; ++i) free(argv[i]);
    free(wprogram); free(wcwd);
}

void cdx_call(const char *operation, const cdx_json *request, int arguments, HANDLE cancellation, cdx_json_output *output) {
    int valid = 1;
    unsigned long long id;
    workspace_slot *slot;
    ++call_count;
    if (arguments >= 0 && request->nodes[arguments].type != CDX_JSON_OBJECT) { error_result(output, "validation_error", "arguments must be an object"); return; }
    if (!strcmp(operation, "tool_info")) {
        cdx_json_raw(output, "{\"status\":\"ok\",\"name\":\"Cordonix\",\"version\":\"0.1.0-dev\",\"platform\":\"windows\",\"runtime\":\"C11\",\"calls\":");
        cdx_json_uint(output, call_count); cdx_json_raw(output, ",\"document_loads\":"); cdx_json_uint(output, disk_reads);
        cdx_json_raw(output, ",\"document_commits\":"); cdx_json_uint(output, disk_writes);
        cdx_json_raw(output, ",\"commit_checks\":"); cdx_json_uint(output, commit_checks);
        cdx_json_raw(output, ",\"counter_scope\":\"logical operations, not physical disk IO\",\"max_documents\":16,\"max_document_bytes\":8388608}"); return;
    }
    if (arguments < 0) { error_result(output, "validation_error", "arguments must be an object"); return; }
    if (!strcmp(operation, "execute_program")) { execute_program(request, arguments, cancellation, output); return; }
    if (!strcmp(operation, "workspace_open")) { open_document(request, arguments, output); return; }
    id = integer_arg(request, arguments, "document_id", 0, 9007199254740991ull, &valid);
    slot = valid ? find_document(id) : NULL;
    if (!slot) { error_result(output, "not_found", "No live document with this id; open it in this MCP session"); return; }
    if (!strcmp(operation, "workspace_read")) read_document(slot, request, arguments, output);
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
