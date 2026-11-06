#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "cdx/operations.h"
#include "cdx/filesystem.h"
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <wchar.h>
#else
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

int cdx_validate_utf8(const char *text, size_t length);

#define CDX_PROGRAM_MAX_RESULTS 64u
#define CDX_PROGRAM_MAX_PATH_BYTES 32768u
#define CDX_PROGRAM_MAX_PATH_LIST 262144u
#define CDX_PROGRAM_DEFAULT_TIMEOUT 5000u
#define CDX_PROGRAM_MAX_TIMEOUT 60000u
#define CDX_PROGRAM_DEFAULT_OUTPUT 65536u
#define CDX_PROGRAM_MAX_OUTPUT 262144u

typedef struct program_request {
    const char *name;
    const char *path_list;
    char *owned_path_list;
    const char *explicit_paths[CDX_PROGRAM_MAX_RESULTS];
    size_t explicit_count;
    size_t max_results;
    size_t max_output;
    uint32_t timeout_ms;
    int search_path;
    cdx_cancel_handle cancellation;
} program_request;

typedef struct program_result {
    char *paths[CDX_PROGRAM_MAX_RESULTS];
    size_t count;
    size_t checked;
    int complete;
    int truncated;
    const char *stop_reason;
} program_result;

static uint64_t clock_ms(void);
static const char *checkpoint(const program_request *request, uint64_t deadline);
static int valid_text(const char *text, size_t maximum, int allow_empty);
static int absolute_text(const char *text);
static int number_arg(const cdx_json *json, int object, const char *name, size_t fallback,
                      size_t minimum, size_t maximum, size_t *value);
static char *environment_path(void);
static cdx_path_char *native_text(const char *text);
static char *utf8_text(const cdx_path_char *text);
static char *join_path(const char *directory, const char *name);
static int executable_path(const cdx_path_char *path);
static int already_seen(const program_result *result, const char *path);
static const char *consider(const char *candidate, const program_request *request,
                            uint64_t deadline, program_result *result);
static const char *scan_path_list(const program_request *request, uint64_t deadline,
                                  program_result *result);
static void free_request(program_request *request);
static void free_result(program_result *result);
static void write_result(cdx_json_output *output, const program_request *request,
                         const program_result *result, const char *status);

void cdx_program_discovery_call(const cdx_json *json, int arguments,
                                cdx_cancel_handle cancellation, cdx_json_output *output) {
    program_request request;
    program_result result;
    const char *status = NULL;
    int node, valid = 1;
    uint64_t deadline;
    size_t i;

    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.max_results = 16u;
    request.max_output = CDX_PROGRAM_DEFAULT_OUTPUT;
    request.timeout_ms = CDX_PROGRAM_DEFAULT_TIMEOUT;
    request.search_path = 1;
    request.cancellation = cancellation;
    node = cdx_json_get(json, arguments, "name");
    if (node < 0 || json->nodes[node].type != CDX_JSON_STRING ||
        !valid_text(json->nodes[node].string, CDX_PROGRAM_MAX_PATH_BYTES, 0) ||
        strchr(json->nodes[node].string, '/') || strchr(json->nodes[node].string, '\\')) valid = 0;
    else request.name = json->nodes[node].string;
    node = cdx_json_get(json, arguments, "path");
    if (node >= 0) {
        if (json->nodes[node].type != CDX_JSON_STRING ||
            !valid_text(json->nodes[node].string, CDX_PROGRAM_MAX_PATH_LIST, 1)) valid = 0;
        else request.path_list = json->nodes[node].string;
    }
    node = cdx_json_get(json, arguments, "search_path");
    if (node >= 0) {
        if (json->nodes[node].type != CDX_JSON_BOOL) valid = 0;
        else request.search_path = json->nodes[node].boolean;
    }
    if (!number_arg(json, arguments, "max_results", request.max_results, 1u,
                    CDX_PROGRAM_MAX_RESULTS, &request.max_results)) valid = 0;
    if (!number_arg(json, arguments, "max_output_bytes", request.max_output,
                    1024u, CDX_PROGRAM_MAX_OUTPUT, &request.max_output)) valid = 0;
    {
        size_t timeout = request.timeout_ms;
        if (!number_arg(json, arguments, "timeout_ms", timeout, 1u,
                        CDX_PROGRAM_MAX_TIMEOUT, &timeout)) valid = 0;
        request.timeout_ms = (uint32_t)timeout;
    }
    node = cdx_json_get(json, arguments, "candidates");
    if (node >= 0) {
        int item;
        if (json->nodes[node].type != CDX_JSON_ARRAY) valid = 0;
        else for (item = json->nodes[node].first; item >= 0; item = json->nodes[item].next) {
            if (request.explicit_count == CDX_PROGRAM_MAX_RESULTS ||
                json->nodes[item].type != CDX_JSON_STRING ||
                !valid_text(json->nodes[item].string, CDX_PROGRAM_MAX_PATH_BYTES, 0) ||
                !absolute_text(json->nodes[item].string)) { valid = 0; break; }
            request.explicit_paths[request.explicit_count++] = json->nodes[item].string;
        }
    }
    if (!request.search_path && !request.explicit_count) valid = 0;
    if (!valid) {
        cdx_json_raw(output, "{\"status\":\"validation_error\",\"scan_complete\":false,\"truncated\":false,\"candidates\":[]}");
        free_request(&request);
        return;
    }
    if (request.search_path && !request.path_list) request.owned_path_list = environment_path();
    if (request.search_path && !request.path_list && !request.owned_path_list) {
        status = "io_error";
    } else {
        if (!request.path_list) request.path_list = request.owned_path_list;
        deadline = clock_ms() + request.timeout_ms;
        status = checkpoint(&request, deadline);
        for (i = 0; !status && i < request.explicit_count; ++i)
            status = consider(request.explicit_paths[i], &request, deadline, &result);
        if (!status && request.search_path) status = scan_path_list(&request, deadline, &result);
        if (!status) status = result.count ? "ok" : "not_found";
    }
    if (status && !strcmp(status, "timed_out")) result.stop_reason = status;
    else if (status && !strcmp(status, "cancelled")) result.stop_reason = status;
    else if (status && !strcmp(status, "output_limit") && !result.stop_reason) result.stop_reason = status;
    result.complete = (status && !strcmp(status, "ok")) || (status && !strcmp(status, "not_found"));
    write_result(output, &request, &result, status ? status : "io_error");
    free_result(&result);
    free_request(&request);
}

static uint64_t clock_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static const char *checkpoint(const program_request *request, uint64_t deadline) {
#ifdef _WIN32
    if (request->cancellation && WaitForSingleObject(request->cancellation, 0) == WAIT_OBJECT_0) return "cancelled";
#else
    if (request->cancellation && atomic_load(request->cancellation)) return "cancelled";
#endif
    return clock_ms() >= deadline ? "timed_out" : NULL;
}

static int valid_text(const char *text, size_t maximum, int allow_empty) {
    size_t length;
    if (!text) return 0;
    length = strlen(text);
    return (allow_empty || length != 0u) && length <= maximum && cdx_validate_utf8(text, length) &&
        !strchr(text, '\r') && !strchr(text, '\n') && !strchr(text, '`') &&
        !strstr(text, "$(") && !strstr(text, "${");
}

static int absolute_text(const char *text) {
    size_t length;
    if (!text) return 0;
    length = strlen(text);
#ifdef _WIN32
    return length >= 3u && (((text[0] >= 'A' && text[0] <= 'Z') || (text[0] >= 'a' && text[0] <= 'z')) &&
        text[1] == ':' && (text[2] == '\\' || text[2] == '/')) ||
        (length >= 2u && text[0] == '\\' && text[1] == '\\');
#else
    return length > 0u && text[0] == '/';
#endif
}

static int number_arg(const cdx_json *json, int object, const char *name, size_t fallback,
                      size_t minimum, size_t maximum, size_t *value) {
    int node = cdx_json_get(json, object, name);
    double number;
    if (node < 0) { *value = fallback; return 1; }
    number = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || !isfinite(number) ||
        number < (double)minimum || number > (double)maximum || floor(number) != number) return 0;
    *value = (size_t)number;
    return 1;
}

static char *environment_path(void) {
#ifdef _WIN32
    DWORD size = GetEnvironmentVariableW(L"PATH", NULL, 0);
    wchar_t *wide;
    char *text;
    if (!size) return NULL;
    wide = (wchar_t *)malloc((size_t)size * sizeof(*wide));
    if (!wide || GetEnvironmentVariableW(L"PATH", wide, size) >= size) { free(wide); return NULL; }
    text = utf8_text(wide);
    free(wide);
    return text;
#else
    const char *value = getenv("PATH");
    char *copy;
    if (!value || !valid_text(value, CDX_PROGRAM_MAX_PATH_LIST, 1)) return NULL;
    copy = (char *)malloc(strlen(value) + 1u);
    if (copy) memcpy(copy, value, strlen(value) + 1u);
    return copy;
#endif
}

static cdx_path_char *native_text(const char *text) {
#ifdef _WIN32
    int count;
    wchar_t *copy;
    if (!text || strlen(text) >= CDX_PROGRAM_MAX_PATH_BYTES) return NULL;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!count || (size_t)count > CDX_PROGRAM_MAX_PATH_BYTES) return NULL;
    copy = (wchar_t *)malloc((size_t)count * sizeof(*copy));
    if (!copy || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, copy, count)) { free(copy); return NULL; }
    return copy;
#else
    char *copy;
    size_t length = text ? strlen(text) : 0u;
    if (!text || length >= CDX_PROGRAM_MAX_PATH_BYTES) return NULL;
    copy = (char *)malloc(length + 1u);
    if (copy) memcpy(copy, text, length + 1u);
    return copy;
#endif
}

static char *utf8_text(const cdx_path_char *text) {
#ifdef _WIN32
    int count;
    char *copy;
    if (!text) return NULL;
    count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, NULL, 0, NULL, NULL);
    if (!count || (size_t)count > CDX_PROGRAM_MAX_PATH_BYTES) return NULL;
    copy = (char *)malloc((size_t)count);
    if (!copy || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, copy, count, NULL, NULL)) { free(copy); return NULL; }
    return copy;
#else
    char *copy;
    size_t length = text ? strlen(text) : 0u;
    if (!text || length >= CDX_PROGRAM_MAX_PATH_BYTES) return NULL;
    copy = (char *)malloc(length + 1u);
    if (copy) memcpy(copy, text, length + 1u);
    return copy;
#endif
}

#ifdef _WIN32
static int has_exe_suffix(const char *text) {
    size_t length = strlen(text);
    return length >= 4u && !(_stricmp(text + length - 4u, ".exe"));
}
#endif

static char *join_path(const char *directory, const char *name) {
    const char *separator = "";
    size_t directory_length = directory ? strlen(directory) : 0u;
    size_t name_length = strlen(name);
    size_t suffix = 0u;
    char *result;
#ifdef _WIN32
    if (!has_exe_suffix(name)) suffix = 4u;
    if (directory_length && directory[directory_length - 1u] != '\\' && directory[directory_length - 1u] != '/') separator = "\\";
#else
    if (directory_length && directory[directory_length - 1u] != '/') separator = "/";
#endif
    if (directory_length + strlen(separator) + name_length + suffix + 1u > CDX_PROGRAM_MAX_PATH_BYTES) return NULL;
    result = (char *)malloc(directory_length + strlen(separator) + name_length + suffix + 1u);
    if (!result) return NULL;
    memcpy(result, directory, directory_length);
    memcpy(result + directory_length, separator, strlen(separator));
    memcpy(result + directory_length + strlen(separator), name, name_length);
#ifdef _WIN32
    if (suffix) memcpy(result + directory_length + strlen(separator) + name_length, ".exe", 5u);
    else
#endif
        result[directory_length + strlen(separator) + name_length] = 0;
    if (suffix) result[directory_length + strlen(separator) + name_length + suffix] = 0;
    return result;
}

static int executable_path(const cdx_path_char *path) {
#ifdef _WIN32
    DWORD attributes, binary_type;
    size_t length;
    if (!path) return 0;
    length = wcslen(path);
    if (length < 4u || _wcsicmp(path + length - 4u, L".exe")) return 0;
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    return GetBinaryTypeW(path, &binary_type) != 0;
#else
    struct stat metadata;
    return path && !stat(path, &metadata) && S_ISREG(metadata.st_mode) && !access(path, X_OK);
#endif
}

static int already_seen(const program_result *result, const char *path) {
    size_t i;
    for (i = 0; i < result->count; ++i) if (!strcmp(result->paths[i], path)) return 1;
    return 0;
}

static const char *consider(const char *candidate, const program_request *request,
                            uint64_t deadline, program_result *result) {
    cdx_path_char *native = NULL, *normalized = NULL;
    char *joined = NULL, *normalized_text = NULL;
    size_t candidate_length, suffix = 0u;
    unsigned long error = 0;
    cdx_fs_status fs_status;
    const char *status;
    if ((status = checkpoint(request, deadline)) != NULL) return status;
    ++result->checked;
    candidate_length = strlen(candidate);
#ifdef _WIN32
    if (!has_exe_suffix(candidate)) suffix = 4u;
#endif
    if (candidate_length + suffix + 1u > CDX_PROGRAM_MAX_PATH_BYTES) return "validation_error";
    joined = (char *)malloc(candidate_length + suffix + 1u);
    if (!joined) return "io_error";
    memcpy(joined, candidate, candidate_length);
    if (suffix) memcpy(joined + candidate_length, ".exe", suffix);
    joined[candidate_length + suffix] = 0;
    native = native_text(joined);
    free(joined);
    if (!native) return "encoding_error";
    fs_status = cdx_fs_normalize_path(native, request->cancellation, &normalized, &error);
    if (fs_status != CDX_FS_OK) {
        free(native);
        free(normalized);
        if (fs_status == CDX_FS_CANCELLED) return "cancelled";
        if (fs_status == CDX_FS_VALIDATION_ERROR) return "validation_error";
        if (fs_status == CDX_FS_ENCODING_ERROR) return "encoding_error";
        return NULL;
    }
#ifndef _WIN32
    free(normalized);
    /* Resolve the original traversal; lexical .. removal can change its target. */
    normalized = realpath(native, NULL);
#endif
    free(native);
    if (executable_path(normalized)) normalized_text = utf8_text(normalized);
    free(normalized);
    if (!normalized_text) return NULL;
    if (!cdx_validate_utf8(normalized_text, strlen(normalized_text))) { free(normalized_text); return "encoding_error"; }
    if (!already_seen(result, normalized_text)) {
        if (result->count == request->max_results) {
            free(normalized_text);
            result->truncated = 1;
            result->stop_reason = "max_results";
            return "output_limit";
        }
        result->paths[result->count++] = normalized_text;
    } else free(normalized_text);
    return checkpoint(request, deadline);
}

static const char *scan_path_list(const program_request *request, uint64_t deadline,
                                  program_result *result) {
    const char *cursor = request->path_list;
    const char delimiter =
#ifdef _WIN32
        ';';
#else
        ':';
#endif
    while (cursor && *cursor) {
        const char *end = strchr(cursor, delimiter);
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        char *directory;
        char *candidate;
        const char *status;
        if ((status = checkpoint(request, deadline)) != NULL) return status;
        if (length) {
            directory = (char *)malloc(length + 1u);
            if (!directory) return "io_error";
            memcpy(directory, cursor, length); directory[length] = 0;
            if (!absolute_text(directory)) { free(directory); return "validation_error"; }
            candidate = join_path(directory, request->name);
            free(directory);
            if (!candidate) return "io_error";
            status = consider(candidate, request, deadline, result);
            free(candidate);
            if (status) return status;
        }
        if (!end) break;
        cursor = end + 1;
    }
    return NULL;
}

static void free_request(program_request *request) { free(request->owned_path_list); }

static void free_result(program_result *result) {
    size_t i;
    for (i = 0; i < result->count; ++i) free(result->paths[i]);
}

static void write_result(cdx_json_output *output, const program_request *request,
                         const program_result *result, const char *status) {
    size_t i, count = result->count, estimate = 256u;
    int output_limited = 0;
    const char *name = request->name ? request->name : "";
    while (count) {
        estimate = 256u + strlen(name) * 6u + strlen(result->paths[0]) * 6u;
        for (i = 0; i < count; ++i) estimate += strlen(result->paths[i]) * 6u + 4u;
        if (estimate <= request->max_output) break;
        --count;
        output_limited = 1;
    }
    if (256u + strlen(name) * 6u > request->max_output) { name = ""; output_limited = 1; }
    if (count < result->count) output_limited = 1;
    cdx_json_raw(output, "{\"status\":");
    cdx_json_quote(output, output_limited ? "output_limit" : status,
                   strlen(output_limited ? "output_limit" : status));
    cdx_json_raw(output, ",\"name\":"); cdx_json_quote(output, name, strlen(name));
    cdx_json_raw(output, ",\"selected\":");
    if (count) cdx_json_quote(output, result->paths[0], strlen(result->paths[0])); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"candidates\":[");
    for (i = 0; i < count; ++i) { if (i) cdx_json_raw(output, ","); cdx_json_quote(output, result->paths[i], strlen(result->paths[i])); }
    cdx_json_raw(output, "],\"checked\":"); cdx_json_uint(output, result->checked);
    cdx_json_raw(output, ",\"scan_complete\":"); cdx_json_raw(output, result->complete && !output_limited ? "true" : "false");
    cdx_json_raw(output, ",\"truncated\":"); cdx_json_raw(output, result->truncated || output_limited ? "true" : "false");
    cdx_json_raw(output, ",\"stop_reason\":");
    if (output_limited) cdx_json_quote(output, "max_output_bytes", 16u);
    else if (result->stop_reason) cdx_json_quote(output, result->stop_reason, strlen(result->stop_reason));
    else cdx_json_raw(output, "null");
    cdx_json_raw(output, "}");
}

const char *cdx_program_discovery_tool_schema(void) {
    return "{\"name\":\"find_program\",\"title\":\"发现可执行程序\",\"description\":\"Resolve a native executable without a shell. Explicit candidates are checked first, then absolute directories from path or inherited PATH. Windows accepts native .exe files only; POSIX requires a regular executable file. Results are absolute, unique and bounded; selected is the first match. Relative PATH entries are rejected. No wildcard or PATHEXT expansion and no command-string interpretation. POSIX returns physical paths; identical result strings are deduplicated. Check scan_complete, truncated and stop_reason before executing a result.\",\"inputSchema\":{\"type\":\"object\",\"required\":[\"name\"],\"properties\":{\"name\":{\"type\":\"string\",\"minLength\":1,\"description\":\"Bare literal name; no path separators, backticks, newlines or command substitution.\"},\"candidates\":{\"type\":\"array\",\"maxItems\":64,\"description\":\"Up to 64 absolute candidates, checked in array order before PATH.\",\"items\":{\"type\":\"string\",\"description\":\"Absolute literal candidate path; checked before PATH.\"}},\"path\":{\"type\":\"string\",\"description\":\"Optional platform PATH list using ; on Windows or : on POSIX; omitted uses inherited PATH.\"},\"search_path\":{\"type\":\"boolean\",\"default\":true,\"description\":\"Search path after explicit candidates; false requires at least one candidate.\"},\"max_results\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":64,\"default\":16,\"description\":\"Maximum returned candidates; additional matches keep the scan incomplete.\"},\"max_output_bytes\":{\"type\":\"integer\",\"minimum\":1024,\"maximum\":262144,\"default\":65536,\"description\":\"Serialized operation JSON budget excluding the MCP envelope.\"},\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":60000,\"default\":5000,\"description\":\"Deadline in milliseconds, checked between filesystem probes.\"}}},\"annotations\":{\"readOnlyHint\":true}}";
}
