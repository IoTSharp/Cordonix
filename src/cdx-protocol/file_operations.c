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

#define FILE_TIMEOUT_SCHEMA "\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":60000,\"default\":10000}"
#define FILE_BYTES_SCHEMA "\"max_bytes\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1073741824,\"default\":67108864}"

const char *cdx_file_ops_tool_schema(void) {
    return
    "{\"name\":\"hash_file\",\"title\":\"文件 SHA256\",\"description\":\"Stream the exact bytes of an absolute regular file through SHA256, without text decoding or full-file allocation. Reject symlinks/reparse points. max_bytes defaults 64 MiB, maximum 1 GiB. Deadline and cancellation checked between 64 KiB chunks; synchronous filesystem I/O is not forcibly interrupted. POSIX detects size/mtime changes, without claiming an atomic snapshot.\",\"inputSchema\":{\"type\":\"object\",\"required\":[\"path\"],\"properties\":{\"path\":{\"type\":\"string\"}," FILE_BYTES_SCHEMA "," FILE_TIMEOUT_SCHEMA "}},\"annotations\":{\"readOnlyHint\":true}}"
    ",{\"name\":\"copy_file\",\"title\":\"复制文件（不覆盖）\",\"description\":\"Copy one absolute regular file byte-for-byte to a new absolute destination in an existing directory. SHA256 describes the source byte stream. Publish a flushed same-directory temporary file atomically without replacement; collision/cancel/timeout before publication leaves destination untouched. Windows requests creation/access/write times; Linux requests permission bits (0777) and access/write times. Published copies report metadata_checked=true and metadata_preserved=true only when those fields read back exactly; filesystem rounding can yield successful byte copy with metadata_preserved=false. ACLs, ownership, alternate streams, xattrs and sparse layout are not cloned. No directory recursion. Linux publication uses a no-replace hardlink, then removes the temporary name; filesystems must support hardlinks. written_to_disk identifies published destination; cleanup_complete reports temporary cleanup, including a published file whose temporary-name removal failed.\",\"inputSchema\":{\"type\":\"object\",\"required\":[\"source\",\"destination\"],\"properties\":{\"source\":{\"type\":\"string\"},\"destination\":{\"type\":\"string\"}," FILE_BYTES_SCHEMA "," FILE_TIMEOUT_SCHEMA "}},\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":false,\"idempotentHint\":false}}"
    ",{\"name\":\"move_file\",\"title\":\"移动文件（不覆盖）\",\"description\":\"Atomically rename one absolute regular file to a new absolute destination on the same filesystem. Existing destination is never replaced. Cross-filesystem requests return cross_device with no copy/delete fallback. Linux requires renameat2 RENAME_NOREPLACE; unavailable kernels/filesystems return not_supported. Success reports written_to_disk=true and source_removed=true. No recursive directory moves.\",\"inputSchema\":{\"type\":\"object\",\"required\":[\"source\",\"destination\"],\"properties\":{\"source\":{\"type\":\"string\"},\"destination\":{\"type\":\"string\"}," FILE_TIMEOUT_SCHEMA "}},\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":true,\"idempotentHint\":false}}";
}
