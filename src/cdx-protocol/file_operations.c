#include "cdx/operations.h"
#include "cdx/file_ops.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *text_field(const cdx_json *json, int object, const char *name, int *valid) {
    int node = cdx_json_get(json, object, name);
    if (node < 0 || json->nodes[node].type != CDX_JSON_STRING || !*json->nodes[node].string) {
        *valid = 0; return "";
    }
    return json->nodes[node].string;
}

static uint64_t number_field(const cdx_json *json, int object, const char *name,
                             uint64_t fallback, uint64_t maximum, int *valid) {
    int node = cdx_json_get(json, object, name);
    double value;
    if (node < 0) return fallback;
    value = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || !isfinite(value) || value < 1.0 ||
        value > (double)maximum || floor(value) != value) { *valid = 0; return fallback; }
    return (uint64_t)value;
}

static cdx_path_char *native_path(const char *text) {
    cdx_path_char *path;
#ifdef _WIN32
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (size <= 0 || size > 32768) return NULL;
    path = (wchar_t *)malloc((size_t)size * sizeof(wchar_t));
    if (path && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, path, size)) { free(path); return NULL; }
#else
    size_t size = strlen(text) + 1u;
    if (size > 32768u) return NULL;
    path = (char *)malloc(size);
    if (path) memcpy(path, text, size);
#endif
    return path;
}

void cdx_file_ops_call(const char *operation, const cdx_json *json, int arguments,
                        cdx_cancel_handle cancellation, cdx_json_output *output) {
    cdx_file_request request;
    cdx_file_result result;
    const char *source, *destination = NULL;
    cdx_path_char *native_source = NULL, *native_destination = NULL;
    int valid = 1;
    memset(&request, 0, sizeof(request));
    if (!strcmp(operation, "hash_file")) request.operation = CDX_FILE_HASH;
    else if (!strcmp(operation, "copy_file")) request.operation = CDX_FILE_COPY;
    else if (!strcmp(operation, "move_file")) request.operation = CDX_FILE_MOVE;
    else valid = 0;
    source = text_field(json, arguments, request.operation == CDX_FILE_HASH ? "path" : "source", &valid);
    if (request.operation != CDX_FILE_HASH) destination = text_field(json, arguments, "destination", &valid);
    request.max_bytes = number_field(json, arguments, "max_bytes", UINT64_C(67108864), CDX_FILE_MAX_BYTES, &valid);
    request.timeout_ms = (unsigned)number_field(json, arguments, "timeout_ms", 10000u, 60000u, &valid);
    if (request.operation == CDX_FILE_MOVE && cdx_json_get(json, arguments, "max_bytes") >= 0) valid = 0;
    if (request.operation == CDX_FILE_HASH &&
        (cdx_json_get(json, arguments, "source") >= 0 || cdx_json_get(json, arguments, "destination") >= 0)) valid = 0;
    if (request.operation != CDX_FILE_HASH && cdx_json_get(json, arguments, "path") >= 0) valid = 0;
    if (!valid) {
        cdx_json_raw(output, "{\"status\":\"validation_error\",\"written_to_disk\":false,\"source_removed\":false,\"cleanup_complete\":true,\"message\":\"hash_file takes path; copy_file/move_file take source and destination. Use positive bounded max_bytes (hash/copy only) and timeout_ms.\"}"); return;
    }
    native_source = native_path(source);
    if (destination) native_destination = native_path(destination);
    if (!native_source || (destination && !native_destination)) {
        free(native_source); free(native_destination);
        cdx_json_raw(output, "{\"status\":\"encoding_error\",\"written_to_disk\":false,\"source_removed\":false,\"cleanup_complete\":true}"); return;
    }
    request.source = native_source; request.destination = native_destination; request.cancellation = cancellation;
    cdx_file_execute(&request, &result);
    free(native_source); free(native_destination);
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, result.status, strlen(result.status));
    cdx_json_raw(output, ",\"operation\":"); cdx_json_quote(output, operation, strlen(operation));
    cdx_json_raw(output, ",\"source\":"); cdx_json_quote(output, source, strlen(source));
    cdx_json_raw(output, ",\"destination\":");
    if (destination) cdx_json_quote(output, destination, strlen(destination)); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"written_to_disk\":"); cdx_json_raw(output, result.written_to_disk ? "true" : "false");
    cdx_json_raw(output, ",\"source_removed\":"); cdx_json_raw(output, result.source_removed ? "true" : "false");
    cdx_json_raw(output, ",\"file_changed\":"); cdx_json_raw(output, result.file_changed ? "true" : "false");
    cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, result.cleanup_complete ? "true" : "false");
    cdx_json_raw(output, ",\"metadata_checked\":"); cdx_json_raw(output, result.metadata_checked ? "true" : "false");
    cdx_json_raw(output, ",\"metadata_preserved\":"); cdx_json_raw(output, result.metadata_preserved ? "true" : "false");
    cdx_json_raw(output, ",\"size_bytes\":"); cdx_json_uint(output, result.size_bytes);
    cdx_json_raw(output, ",\"bytes_processed\":"); cdx_json_uint(output, result.bytes_processed);
    cdx_json_raw(output, ",\"sha256\":");
    if (*result.sha256) cdx_json_quote(output, result.sha256, 64u); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"native_error\":"); cdx_json_uint(output, result.native_error);
    cdx_json_raw(output, ",\"elapsed_ms\":"); cdx_json_uint(output, result.elapsed_ms); cdx_json_raw(output, "}");
}

const char *cdx_file_ops_tool_schema(void) {
    return
    "{\"name\":\"hash_file\",\"title\":\"文件 SHA256\",\"description\":\"Use when hashing raw bytes of one local file with SHA256; use read_file for text. Require an absolute regular file; no links/reparse points or directories. Only a complete successful stream returns sha256. file_changed discards digest; retry after source settles. POSIX size/mtime checks are not a snapshot. Cooperative timeout/cancel between 64 KiB chunks cannot interrupt synchronous I/O.\",\"annotations\":{\"readOnlyHint\":true},\"inputSchema\":{\"type\":\"object\",\"required\":[\"path\"],\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Absolute existing regular-file path; hash raw bytes, no links/directories.\"},"
    "\"max_bytes\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1073741824,\"default\":67108864,\"description\":\"Hash/copy source-byte cap. too_large gives no digest/publication; forbidden for move.\"},"
    "\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":60000,\"default\":10000,\"description\":\"Cooperative milliseconds; chunks or pre-rename check. Synchronous I/O may exceed it.\"}"
    "}}}"
    ",{\"name\":\"copy_file\",\"title\":\"复制文件（不覆盖）\",\"description\":\"Use when copying one file while retaining source; move_file relocates, create_file writes new text. Absolute regular source and absent destination with existing parent required; reject source links/reparse points. No replace/recursion. Atomic flushed-temp publication; Linux needs hardlinks. SHA256 covers stream bytes. Windows requests creation/access/write times; Linux mode 0777 and access/write times. metadata_checked/metadata_preserved report exact readback; bytes can succeed with metadata loss. ACLs/owner/alternate streams/xattrs/sparse layout are not cloned. written_to_disk=true means published even on cleanup error; cleanup_complete=false means temporary cleanup failed. Inspect destination after unknown transport outcome before retry. Timeout/cancel before publication leaves it untouched.\",\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":false,\"idempotentHint\":false},\"inputSchema\":{\"type\":\"object\",\"required\":[\"source\",\"destination\"],\"properties\":{"
    "\"source\":{\"type\":\"string\",\"description\":\"Absolute regular source file; no links/reparse points or directories.\"},"
    "\"destination\":{\"type\":\"string\",\"description\":\"Absolute absent file path with existing parent. Existing paths return already_exists; never replaced.\"},"
    "\"max_bytes\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1073741824,\"default\":67108864,\"description\":\"Hash/copy source-byte cap. too_large gives no digest/publication; forbidden for move.\"},"
    "\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":60000,\"default\":10000,\"description\":\"Cooperative milliseconds; chunks or pre-rename check. Synchronous I/O may exceed it.\"}"
    "}}}"
    ",{\"name\":\"move_file\",\"title\":\"移动文件（不覆盖）\",\"description\":\"Use when relocating one regular file without replacement; use copy_file to retain source. Require absolute source and new destination with existing parent; reject source links/reparse points and directories. Same-filesystem atomic rename only. cross_device has no copy/delete fallback; Linux renameat2 RENAME_NOREPLACE may return not_supported. Success sets written_to_disk/source_removed=true. Check timeout/cancel before rename; report known completed mutation. Inspect both paths after unknown transport outcomes before retrying. No recursion or max_bytes.\",\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":true,\"idempotentHint\":false},\"inputSchema\":{\"type\":\"object\",\"required\":[\"source\",\"destination\"],\"properties\":{"
    "\"source\":{\"type\":\"string\",\"description\":\"Absolute regular source file; no links/reparse points or directories.\"},"
    "\"destination\":{\"type\":\"string\",\"description\":\"Absolute absent file path with existing parent. Existing paths return already_exists; never replaced.\"},"
    "\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":60000,\"default\":10000,\"description\":\"Cooperative milliseconds; chunks or pre-rename check. Synchronous I/O may exceed it.\"}"
    "}}}";
}
