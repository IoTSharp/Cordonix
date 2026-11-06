#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "cdx/operations.h"
#include "cdx/process.h"
#include "cdx/filesystem.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <time.h>
#endif

int cdx_validate_utf8(const char *text, size_t length);

#define FIND_MAX_ARGS 112u
#define FIND_MAX_FILES 100000u
#define FIND_MAX_CAPTURE 4194304u
#define FIND_MAX_OUTPUT 1048576u

typedef struct find_request {
    cdx_exec_request execution;
    cdx_path_char *program, *cwd, *args[FIND_MAX_ARGS];
    int expected[2];
    size_t max_files, offset, limit, max_output;
} find_request;

typedef struct find_page {
    cdx_json_output entries;
    size_t total, returned;
    int complete, has_more, truncated;
    const char *reason;
} find_page;

static uint64_t milliseconds(void);
static const char *interrupted(cdx_cancel_handle, uint64_t);
static const char *prepare(const cdx_json *, int, cdx_cancel_handle, find_request *);
static const char *collect(cdx_exec_result *, const find_request *, uint64_t, char ***, size_t *);
static const char *paginate(char **, size_t, const find_request *, const cdx_exec_result *, uint64_t, find_page *);
static void write_result(cdx_json_output *, const char *, const find_request *, const cdx_exec_result *, const find_page *);
static void free_request(find_request *);

void cdx_discovery_call(const cdx_json *json, int arguments, cdx_cancel_handle cancellation,
                        cdx_json_output *output) {
    find_request request;
    find_page page;
    cdx_exec_result process;
    char **paths = NULL;
    size_t count = 0;
    uint64_t deadline;
    const char *status;
    memset(&request, 0, sizeof(request));
    memset(&page, 0, sizeof(page));
    cdx_result_init(&process);
    deadline = milliseconds();
    status = prepare(json, arguments, cancellation, &request);
    deadline += request.execution.timeout_ms;
    if (!status) status = interrupted(cancellation, deadline);
    if (!status) {
        uint64_t remaining = deadline - milliseconds();
        if (!remaining || remaining > request.execution.timeout_ms) status = "timed_out";
        else {
            request.execution.timeout_ms = (uint32_t)remaining;
            (void)cdx_execute(&request.execution, &process);
            if (process.status != CDX_COMPLETED) status = cdx_status_name(process.status);
            else status = collect(&process, &request, deadline, &paths, &count);
        }
    }
    if (!status) status = paginate(paths, count, &request, &process, deadline, &page);
    if (!page.reason && status && strcmp(status, "completed") && strcmp(status, "no_match")) page.reason = status;
    if (process.status == CDX_OUTPUT_LIMIT || process.output_truncated) { page.reason = "capture_limit_bytes"; page.truncated = 1; }
    else if (status && !strcmp(status, "max_files")) { status = "output_limit"; page.reason = "max_files"; page.truncated = 1; }
    write_result(output, status ? status : "completed", &request, &process, &page);
    free(paths);
    cdx_json_output_free(&page.entries);
    cdx_result_free(&process);
    free_request(&request);
}

static uint64_t milliseconds(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static const char *interrupted(cdx_cancel_handle cancellation, uint64_t deadline) {
#ifdef _WIN32
    if (cancellation && WaitForSingleObject(cancellation, 0) == WAIT_OBJECT_0) return "cancelled";
#else
    if (cancellation && atomic_load(cancellation)) return "cancelled";
#endif
    return milliseconds() >= deadline ? "timed_out" : NULL;
}

static cdx_path_char *native_text(const char *text) {
#ifdef _WIN32
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t *copy;
    if (!count) return NULL;
    copy = (wchar_t *)malloc((size_t)count * sizeof(*copy));
    if (copy && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, copy, count)) {
        free(copy); return NULL;
    }
    return copy;
#else
    size_t length = strlen(text) + 1u;
    char *copy = (char *)malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
#endif
}

static const char *validate_path(const cdx_path_char *path, cdx_cancel_handle cancellation) {
    cdx_path_char *normalized = NULL;
    unsigned long error;
    cdx_fs_status status = cdx_fs_normalize_path(path, cancellation, &normalized, &error);
    free(normalized);
    return status == CDX_FS_OK ? NULL : cdx_fs_status_name(status);
}

static size_t integer(const cdx_json *json, int arguments, const char *key, size_t fallback,
                      size_t minimum, size_t maximum, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    double value;
    if (node < 0) return fallback;
    value = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || value < (double)minimum ||
        value > (double)maximum || floor(value) != value) { *valid = 0; return fallback; }
    return (size_t)value;
}

static int append_arg(find_request *request, const char *text) {
    size_t index = request->execution.arg_count;
    if (index == FIND_MAX_ARGS || !(request->args[index] = native_text(text))) return 0;
    ++request->execution.arg_count;
    return 1;
}

static const char *append_array(find_request *request, const cdx_json *json, int arguments,
                                const char *key, const char *flag, int required, int paths) {
    int array = cdx_json_get(json, arguments, key), node;
    size_t count = 0, bytes = 0;
    if (array < 0) return required ? "validation_error" : NULL;
    if (json->nodes[array].type != CDX_JSON_ARRAY) return "validation_error";
    for (node = json->nodes[array].first; node >= 0 && count < 32u; node = json->nodes[node].next, ++count) {
        const char *status, *text;
        if (json->nodes[node].type != CDX_JSON_STRING) return "validation_error";
        text = json->nodes[node].string;
        bytes += strlen(text);
        if (!*text || bytes > 65536u) return "validation_error";
        if (flag && !append_arg(request, flag)) return "io_error";
        if (!append_arg(request, text)) return "io_error";
        if (paths && (status = validate_path(request->args[request->execution.arg_count - 1u],
                                         request->execution.cancel_event)) != NULL) return status;
    }
    return node >= 0 || (required && !count) ? "validation_error" : NULL;
}

static const char *prepare(const cdx_json *json, int arguments, cdx_cancel_handle cancellation,
                           find_request *request) {
    int valid = 1, program, cwd, hidden;
    const char *status;
    request->execution.cancel_event = cancellation;
    request->execution.timeout_ms = (uint32_t)integer(json, arguments, "timeout_ms", 10000u, 1u, 60000u, &valid);
    request->execution.output_limit_bytes = integer(json, arguments, "capture_limit_bytes", 1048576u, 1024u, FIND_MAX_CAPTURE, &valid);
    request->max_files = integer(json, arguments, "max_files", 10000u, 1u, FIND_MAX_FILES, &valid);
    request->offset = integer(json, arguments, "offset", 0u, 0u, FIND_MAX_FILES, &valid);
    request->limit = integer(json, arguments, "limit", 200u, 1u, 1000u, &valid);
    request->max_output = integer(json, arguments, "max_output_bytes", 65536u, 1024u, FIND_MAX_OUTPUT, &valid);
    program = cdx_json_get(json, arguments, "program"); cwd = cdx_json_get(json, arguments, "cwd");
    hidden = cdx_json_get(json, arguments, "include_hidden");
    if (!valid || program < 0 || cwd < 0 || json->nodes[program].type != CDX_JSON_STRING ||
        json->nodes[cwd].type != CDX_JSON_STRING ||
        (hidden >= 0 && json->nodes[hidden].type != CDX_JSON_BOOL)) return "validation_error";
    request->program = native_text(json->nodes[program].string);
    request->cwd = native_text(json->nodes[cwd].string);
    if (!request->program || !request->cwd) return "io_error";
    if ((status = validate_path(request->program, cancellation)) != NULL ||
        (status = validate_path(request->cwd, cancellation)) != NULL) return status;
    request->execution.program = request->program; request->execution.cwd = request->cwd;
    request->execution.args = (const cdx_path_char *const *)request->args;
    request->execution.expected_exit_codes = request->expected;
    request->execution.expected_exit_count = 2u; request->expected[1] = 1;
    if (!append_arg(request, "--no-config") || !append_arg(request, "--files") ||
        !append_arg(request, "--null") || !append_arg(request, "--color=never") ||
        (hidden >= 0 && json->nodes[hidden].boolean && !append_arg(request, "--hidden"))) return "io_error";
    if ((status = append_array(request, json, arguments, "globs", "--glob", 0, 0)) != NULL) return status;
    if (!append_arg(request, "--")) return "io_error";
    return append_array(request, json, arguments, "roots", NULL, 1, 1);
}

static int compare_paths(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static const char *collect(cdx_exec_result *process, const find_request *request, uint64_t deadline,
                           char ***paths, size_t *count) {
    size_t offset = 0, unique, i;
    const char *status;
    if (process->output_truncated) return "output_limit";
    *paths = (char **)malloc(request->max_files * sizeof(**paths));
    if (!*paths) return "io_error";
    while (offset < process->stdout_bytes.size && *count < request->max_files) {
        char *begin = (char *)process->stdout_bytes.data + offset;
        const char *end = (const char *)memchr(begin, 0, process->stdout_bytes.size - offset);
        size_t length;
        if ((status = interrupted(request->execution.cancel_event, deadline)) != NULL) return status;
        if (!end || !(length = (size_t)(end - begin))) return "parse_error";
        if (!cdx_validate_utf8(begin, length)) return "encoding_error";
        (*paths)[(*count)++] = begin;
        offset += length + 1u;
    }
    if (offset != process->stdout_bytes.size) return "max_files";
    if (process->exit_code == 1 && *count) return "parse_error";
    if ((status = interrupted(request->execution.cancel_event, deadline)) != NULL) return status;
    qsort(*paths, *count, sizeof(**paths), compare_paths);
    for (i = 0, unique = 0; i < *count; ++i) {
        if (!unique || strcmp((*paths)[i], (*paths)[unique - 1u])) (*paths)[unique++] = (*paths)[i];
    }
    *count = unique;
    return interrupted(request->execution.cancel_event, deadline);
}

static const char *paginate(char **paths, size_t count, const find_request *request,
                            const cdx_exec_result *process, uint64_t deadline, find_page *page) {
    cdx_json_output base, item;
    size_t index = request->offset, budget;
    const char *status;
    page->complete = 1; page->total = count;
    cdx_json_output_init(&base); cdx_json_output_init(&item);
    write_result(&base, "output_limit", request, process, page);
    if (base.failed) { cdx_json_output_free(&base); return "io_error"; }
    /* Counts, next_offset and stop_reason can grow after this empty-page measurement. */
    budget = request->max_output > base.size + 96u ? request->max_output - base.size - 96u : 0u;
    cdx_json_output_free(&base);
    while (index < count && page->returned < request->limit) {
        if ((status = interrupted(request->execution.cancel_event, deadline)) != NULL) {
            page->complete = 0; page->returned = 0; page->entries.size = 0;
            cdx_json_output_free(&item); return status;
        }
        item.size = 0;
        cdx_json_quote(&item, paths[index], strlen(paths[index]));
        if (item.failed) { cdx_json_output_free(&item); return "io_error"; }
        if (page->entries.size + (page->returned ? 1u : 0u) + item.size > budget) {
            page->reason = "max_output_bytes"; break;
        }
        if (page->returned) cdx_json_raw(&page->entries, ",");
        cdx_json_rawn(&page->entries, item.data, item.size);
        ++page->returned; ++index;
    }
    cdx_json_output_free(&item);
    page->has_more = index < count;
    page->truncated = page->has_more;
    if (!page->reason && page->has_more) page->reason = "page_limit";
    if (page->entries.failed) return "io_error";
    return page->reason && !strcmp(page->reason, "max_output_bytes") ? "output_limit" : count ? "completed" : "no_match";
}

static void write_result(cdx_json_output *output, const char *status, const find_request *request,
                          const cdx_exec_result *process, const find_page *page) {
    size_t stderr_size = process->stderr_bytes.size;
    size_t stderr_limit = request->max_output / 64u;
    if (stderr_limit > 1024u) stderr_limit = 1024u;
    if (stderr_size > stderr_limit) stderr_size = stderr_limit;
    while (stderr_size && !cdx_validate_utf8((const char *)process->stderr_bytes.data, stderr_size)) --stderr_size;
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, status, strlen(status));
    cdx_json_raw(output, ",\"process_status\":");
    if (process->pid || process->exit_code_known) cdx_json_quote(output, cdx_status_name(process->status), strlen(cdx_status_name(process->status)));
    else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"exit_code\":");
    if (process->exit_code_known) cdx_json_uint(output, process->exit_code); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"elapsed_ms\":"); cdx_json_uint(output, process->elapsed_ms);
    cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, process->cleanup_complete ? "true" : "false");
    cdx_json_raw(output, ",\"scan_complete\":"); cdx_json_raw(output, page->complete ? "true" : "false");
    cdx_json_raw(output, ",\"truncated\":"); cdx_json_raw(output, page->truncated ? "true" : "false");
    cdx_json_raw(output, ",\"stop_reason\":");
    if (page->reason) cdx_json_quote(output, page->reason, strlen(page->reason)); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"total_files\":");
    if (page->complete) cdx_json_uint(output, page->total); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"offset\":"); cdx_json_uint(output, request->offset);
    cdx_json_raw(output, ",\"returned\":"); cdx_json_uint(output, page->returned);
    cdx_json_raw(output, ",\"next_offset\":");
    if (page->complete && page->has_more) cdx_json_uint(output, request->offset + page->returned); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"stderr\":"); cdx_json_quote(output, (const char *)process->stderr_bytes.data, stderr_size);
    cdx_json_raw(output, ",\"stderr_truncated\":"); cdx_json_raw(output, stderr_size < process->stderr_bytes.size ? "true" : "false");
    cdx_json_raw(output, ",\"paths\":[");
    if (page->complete && page->entries.size) cdx_json_rawn(output, page->entries.data, page->entries.size);
    cdx_json_raw(output, "]}");
}

static void free_request(find_request *request) {
    size_t i;
    for (i = 0; i < request->execution.arg_count; ++i) free(request->args[i]);
    free(request->program); free(request->cwd);
}

const char *cdx_discovery_tool_schema(void) {
    return
    "{\"name\":\"find_files\",\"title\":\"发现文件并分页\",\"description\":\"Use when recursively finding file names with globs and sorted pages. Use list_directory for immediate children; search_text for content. Require installed rg executable, cwd and roots as absolute paths. Runs literal --no-config --files --null argv; no shell or executable lookup. Ignore rules apply; hidden opt-in does not disable ignores, and positive globs can include hidden names. No --follow. Names retain rg form and are sorted/deduplicated. Incomplete scans return no page. Check scan_complete/stop_reason; complete output_limit pages remain usable. Follow next_offset on unchanged files; raise output budget if it stalls. Each call rescans, without a snapshot.\",\"annotations\":{\"readOnlyHint\":true},\"inputSchema\":{\"type\":\"object\",\"required\":[\"program\",\"cwd\",\"roots\"],\"properties\":{"
    "\"program\":{\"type\":\"string\",\"description\":\"Absolute installed native rg/rg.exe executable, not bare 'rg', shell, script or command string.\"},"
    "\"cwd\":{\"type\":\"string\",\"description\":\"Absolute existing working directory, separate from program and roots.\"},"
    "\"roots\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":32,\"items\":{\"type\":\"string\",\"minLength\":1},\"description\":\"1..32 absolute file/directory roots, passed unchanged; native link/.. traversal applies.\"},"
    "\"globs\":{\"type\":\"array\",\"maxItems\":32,\"items\":{\"type\":\"string\",\"minLength\":1},\"description\":\"Literal rg --glob patterns, e.g. '*.c', '!build/**'; no shell expansion. Positive globs can include hidden files.\"},"
    "\"include_hidden\":{\"type\":\"boolean\",\"default\":false,\"description\":\"Add --hidden; ignore rules still apply. Does not add --follow.\"},"
    "\"offset\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100000,\"default\":0,\"description\":\"Zero-based sorted/deduplicated offset. Continue next_offset only on unchanged files.\"},"
    "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000,\"default\":200,\"description\":\"Returned paths per page; scanning is bounded separately.\"},"
    "\"max_files\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100000,\"default\":10000,\"description\":\"Captured names before deduplication. Exceeding this yields no page with stop_reason=max_files.\"},"
    "\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":60000,\"default\":10000,\"description\":\"Milliseconds for process/adapter work. Timeout/cancel gives no page; inspect cleanup_complete.\"},"
    "\"capture_limit_bytes\":{\"type\":\"integer\",\"minimum\":1024,\"maximum\":4194304,\"default\":1048576,\"description\":\"Process capture budget. Exceeding it gives no page with stop_reason=capture_limit_bytes.\"},"
    "\"max_output_bytes\":{\"type\":\"integer\",\"minimum\":1024,\"maximum\":1048576,\"default\":65536,\"description\":\"JSON result budget excluding MCP envelope. Complete scans can return shorter pages; raise if next_offset stalls.\"}"
    "}}}";
}
