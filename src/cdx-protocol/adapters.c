#include "cdx/operations.h"
#include "cdx/process.h"
#include "cdx/filesystem.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <time.h>
#endif

int cdx_validate_utf8(const char *text, size_t length);

#define ADAPTER_ARG_LIMIT 128u
#define ADAPTER_OUTPUT_LIMIT 262144u
#define ADAPTER_RECORD_LIMIT 100000u

typedef struct adapter_request {
    cdx_exec_request execution;
    cdx_path_char *program, *cwd, *args[ADAPTER_ARG_LIMIT];
    int expected[2];
    size_t max_entries, per_file;
    const char *encoding;
    int search;
} adapter_request;

typedef struct adapter_records {
    cdx_json_output entries, branch;
    size_t count;
    int truncated, per_file_limit_reached;
} adapter_records;

static const char *prepare_request(const char *, const cdx_json *, int, cdx_cancel_handle, adapter_request *);
static const char *parse_search(const cdx_exec_result *, const adapter_request *, adapter_records *, uint64_t);
static const char *parse_git(const cdx_exec_result *, const adapter_request *, adapter_records *, uint64_t);
static void write_result(cdx_json_output *, const char *, const adapter_request *, const cdx_exec_result *, const adapter_records *);
static uint64_t milliseconds(void);
static const char *interrupted(cdx_cancel_handle, uint64_t);
static void free_request(adapter_request *);

void cdx_adapter_call(const char *operation, const cdx_json *json, int arguments,
                      cdx_cancel_handle cancellation, cdx_json_output *output) {
    adapter_request request;
    adapter_records records;
    cdx_exec_result process;
    const char *status;
    uint64_t deadline;
    memset(&request, 0, sizeof(request));
    memset(&records, 0, sizeof(records));
    cdx_result_init(&process);
    status = prepare_request(operation, json, arguments, cancellation, &request);
    if (!status) {
        (void)cdx_execute(&request.execution, &process);
        status = cdx_status_name(process.status);
        if (process.status == CDX_COMPLETED) {
            deadline = milliseconds() + 2000u;
            status = request.search ? parse_search(&process, &request, &records, deadline) :
                                      parse_git(&process, &request, &records, deadline);
            if (!status) status = records.truncated ? "output_limit" :
                request.search && process.exit_code == 1 ? "no_match" : "completed";
        }
    }
    write_result(output, status, &request, &process, &records);
    cdx_json_output_free(&records.entries);
    cdx_json_output_free(&records.branch);
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
    int length;
    wchar_t *value;
    if (!text) return NULL;
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!length) return NULL;
    value = (wchar_t *)malloc((size_t)length * sizeof(*value));
    if (value && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, value, length)) { free(value); return NULL; }
    return value;
#else
    char *value;
    size_t length;
    if (!text) return NULL;
    length = strlen(text) + 1u;
    value = (char *)malloc(length);
    if (value) memcpy(value, text, length);
    return value;
#endif
}

static const char *string_field(const cdx_json *json, int arguments, const char *key, const char *fallback, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    if (node < 0) { if (!fallback) *valid = 0; return fallback; }
    if (json->nodes[node].type != CDX_JSON_STRING) { *valid = 0; return fallback; }
    return json->nodes[node].string;
}

static size_t bounded_integer(const cdx_json *json, int arguments, const char *key,
                              size_t fallback, size_t maximum, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    double value;
    if (node < 0) return fallback;
    value = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || value < 1 || value > (double)maximum || floor(value) != value) {
        *valid = 0; return fallback;
    }
    return (size_t)value;
}

static int append_arg(adapter_request *request, const char *text) {
    size_t index = request->execution.arg_count;
    if (index == ADAPTER_ARG_LIMIT || !text || !(request->args[index] = native_text(text))) return 0;
    ++request->execution.arg_count;
    return 1;
}

static const char *normalize(cdx_path_char **path, cdx_cancel_handle cancellation) {
    cdx_path_char *normalized = NULL;
    unsigned long error;
    cdx_fs_status status = cdx_fs_normalize_path(*path, cancellation, &normalized, &error);
    if (status != CDX_FS_OK) { free(normalized); return cdx_fs_status_name(status); }
    free(*path); *path = normalized;
    return NULL;
}

static const char *append_array(adapter_request *request, const cdx_json *json, int arguments,
                                 const char *key, const char *flag, size_t maximum, int required, int paths) {
    int array = cdx_json_get(json, arguments, key), node;
    size_t count = 0;
    if (array < 0) return required ? "validation_error" : NULL;
    if (json->nodes[array].type != CDX_JSON_ARRAY) return "validation_error";
    for (node = json->nodes[array].first; node >= 0 && count < maximum; node = json->nodes[node].next, ++count) {
        const char *status;
        if (json->nodes[node].type != CDX_JSON_STRING || (paths && !*json->nodes[node].string)) return "validation_error";
        if (flag && !append_arg(request, flag)) return "io_error";
        if (!append_arg(request, json->nodes[node].string)) return "io_error";
        if (paths && (status = normalize(&request->args[request->execution.arg_count - 1u], request->execution.cancel_event)) != NULL)
            return status;
    }
    return node >= 0 || (required && !count) ? "validation_error" : NULL;
}

static const char *prepare_request(const char *operation, const cdx_json *json, int arguments,
                                    cdx_cancel_handle cancellation, adapter_request *request) {
    int valid = 1, literal, node;
    const char *program = string_field(json, arguments, "program", NULL, &valid);
    const char *cwd = string_field(json, arguments, "cwd", NULL, &valid);
    const char *status, *untracked;
    char limit[32], untracked_arg[40];
    request->search = !strcmp(operation, "search_text");
    if (!request->search && strcmp(operation, "git_status")) return "validation_error";
    request->execution.cancel_event = cancellation;
    request->execution.timeout_ms = (uint32_t)bounded_integer(json, arguments, "timeout_ms", 30000u, 60000u, &valid);
    request->execution.output_limit_bytes = bounded_integer(json, arguments, "output_limit_bytes", ADAPTER_OUTPUT_LIMIT, ADAPTER_OUTPUT_LIMIT, &valid);
    request->max_entries = bounded_integer(json, arguments, "max_entries", 200u, 1000u, &valid);
    if (!valid || !(request->program = native_text(program)) || !(request->cwd = native_text(cwd))) return "validation_error";
    if ((status = normalize(&request->program, cancellation)) != NULL ||
        (status = normalize(&request->cwd, cancellation)) != NULL) return status;
    request->execution.program = request->program;
    request->execution.cwd = request->cwd;
    request->execution.args = (const cdx_path_char *const *)request->args;
    request->execution.expected_exit_codes = request->expected;
    request->execution.expected_exit_count = request->search ? 2u : 1u;
    request->expected[1] = 1;
    if (!request->search) {
        untracked = string_field(json, arguments, "untracked", "normal", &valid);
        if (!valid || (strcmp(untracked, "normal") && strcmp(untracked, "all") && strcmp(untracked, "no"))) return "validation_error";
        (void)snprintf(untracked_arg, sizeof(untracked_arg), "--untracked-files=%s", untracked);
        return append_arg(request, "--no-optional-locks") && append_arg(request, "-c") &&
            append_arg(request, "core.quotepath=false") && append_arg(request, "status") &&
            append_arg(request, "--porcelain=v2") && append_arg(request, "--branch") && append_arg(request, "-z") &&
            append_arg(request, untracked_arg) ? NULL : "io_error";
    }
    request->encoding = string_field(json, arguments, "encoding", "none", &valid);
    if (!valid) return "validation_error";
    if (!strcmp(request->encoding, "cp936")) request->encoding = "gbk";
    else if (!strcmp(request->encoding, "utf-16-le")) request->encoding = "utf-16le";
    else if (!strcmp(request->encoding, "utf-16-be")) request->encoding = "utf-16be";
    if (strcmp(request->encoding, "none") && strcmp(request->encoding, "utf-8") && strcmp(request->encoding, "gbk") &&
        strcmp(request->encoding, "utf-16le") && strcmp(request->encoding, "utf-16be")) return "validation_error";
    request->per_file = bounded_integer(json, arguments, "max_matches_per_file", 50u, 1000u, &valid);
    node = cdx_json_get(json, arguments, "literal");
    if (node >= 0 && json->nodes[node].type != CDX_JSON_BOOL) valid = 0;
    literal = node < 0 || json->nodes[node].boolean;
    if (!valid) return "validation_error";
    (void)snprintf(limit, sizeof(limit), "%zu", request->per_file);
    if (!append_arg(request, "--no-config") || !append_arg(request, "--json") || !append_arg(request, "--color") ||
        !append_arg(request, "never") || !append_arg(request, "--encoding") || !append_arg(request, request->encoding) ||
        !append_arg(request, "--max-count") || !append_arg(request, limit) || (literal && !append_arg(request, "--fixed-strings"))) return "io_error";
    if ((status = append_array(request, json, arguments, "patterns", "-e", 32u, 1, 0)) != NULL ||
        (status = append_array(request, json, arguments, "globs", "--glob", 16u, 0, 0)) != NULL) return status;
    if (!append_arg(request, "--")) return "io_error";
    return append_array(request, json, arguments, "roots", NULL, 8u, 1, 1);
}

static int quote_data(cdx_json_output *output, const cdx_json *json, int object) {
    int text = cdx_json_get(json, object, "text"), bytes = cdx_json_get(json, object, "bytes");
    int value = text >= 0 ? text : bytes;
    if (value < 0 || (text >= 0 && bytes >= 0) || json->nodes[value].type != CDX_JSON_STRING) return 0;
    cdx_json_raw(output, text >= 0 ? "{\"text\":" : "{\"bytes\":");
    cdx_json_quote(output, json->nodes[value].string, strlen(json->nodes[value].string));
    cdx_json_raw(output, "}");
    return 1;
}

static int quote_number(cdx_json_output *output, const cdx_json *json, int object, const char *key) {
    int node = cdx_json_get(json, object, key);
    double value;
    if (node < 0) return 0;
    value = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || value < 0 || value > 9007199254740991.0 || floor(value) != value) return 0;
    cdx_json_rawn(output, json->nodes[node].number_text, json->nodes[node].number_length);
    return 1;
}

static int search_match(cdx_json_output *output, const cdx_json *json, int data) {
    int array, node;
    size_t count = 0;
    cdx_json_raw(output, "{\"path\":");
    if (!quote_data(output, json, cdx_json_get(json, data, "path"))) return 0;
    cdx_json_raw(output, ",\"lines\":");
    if (!quote_data(output, json, cdx_json_get(json, data, "lines"))) return 0;
    cdx_json_raw(output, ",\"line_number\":");
    if (!quote_number(output, json, data, "line_number")) return 0;
    cdx_json_raw(output, ",\"absolute_offset\":");
    if (!quote_number(output, json, data, "absolute_offset")) return 0;
    array = cdx_json_get(json, data, "submatches");
    if (array < 0 || json->nodes[array].type != CDX_JSON_ARRAY) return 0;
    cdx_json_raw(output, ",\"submatches\":[");
    for (node = json->nodes[array].first; node >= 0 && count < 65536u; node = json->nodes[node].next, ++count) {
        if (count) cdx_json_raw(output, ",");
        cdx_json_raw(output, "{\"match\":");
        if (!quote_data(output, json, cdx_json_get(json, node, "match"))) return 0;
        cdx_json_raw(output, ",\"start\":");
        if (!quote_number(output, json, node, "start")) return 0;
        cdx_json_raw(output, ",\"end\":");
        if (!quote_number(output, json, node, "end")) return 0;
        cdx_json_raw(output, "}");
    }
    cdx_json_raw(output, "]}");
    return node < 0 && !output->failed;
}

static const char *parse_search(const cdx_exec_result *process, const adapter_request *request,
                                adapter_records *records, uint64_t deadline) {
    size_t offset = 0, steps = 0;
    int summary = 0;
    const char *stopped;
    while (offset < process->stdout_bytes.size && steps++ < ADAPTER_RECORD_LIMIT) {
        const char *begin = (const char *)process->stdout_bytes.data + offset;
        const char *newline = (const char *)memchr(begin, '\n', process->stdout_bytes.size - offset);
        cdx_json json;
        int type, data, valid = 1;
        size_t length;
        if ((stopped = interrupted(request->execution.cancel_event, deadline)) != NULL) return stopped;
        if (!newline || summary) return "parse_error";
        length = (size_t)(newline - begin);
        offset += length + 1u;
        if (!cdx_json_parse(&json, begin, length)) { cdx_json_free(&json); return "parse_error"; }
        type = cdx_json_get(&json, 0, "type"); data = cdx_json_get(&json, 0, "data");
        if (type < 0 || json.nodes[type].type != CDX_JSON_STRING || data < 0 || json.nodes[data].type != CDX_JSON_OBJECT) valid = 0;
        else if (!strcmp(json.nodes[type].string, "match")) {
            if (records->count == request->max_entries) records->truncated = 1;
            else {
                if (records->count) cdx_json_raw(&records->entries, ",");
                valid = search_match(&records->entries, &json, data);
                ++records->count;
            }
        } else if (!strcmp(json.nodes[type].string, "summary")) summary = 1;
        else if (!strcmp(json.nodes[type].string, "end")) {
            int stats = cdx_json_get(&json, data, "stats"), matched = cdx_json_get(&json, stats, "matched_lines");
            if (matched >= 0 && json.nodes[matched].type == CDX_JSON_NUMBER && json.nodes[matched].number >= (double)request->per_file)
                records->per_file_limit_reached = 1;
        } else if (strcmp(json.nodes[type].string, "begin")) valid = 0;
        cdx_json_free(&json);
        if (!valid) return "parse_error";
    }
    if ((stopped = interrupted(request->execution.cancel_event, deadline)) != NULL) return stopped;
    return offset != process->stdout_bytes.size || !summary || (process->exit_code == 1 && records->count) ? "parse_error" : NULL;
}

static const char *git_header(adapter_records *records, const char *record, size_t length) {
    static const char *const names[] = {"branch.oid", "branch.head", "branch.upstream", "branch.ab"};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        size_t prefix = strlen(names[i]);
        if (length > prefix + 3u && !memcmp(record + 2u, names[i], prefix) && record[prefix + 2u] == ' ') {
            if (records->branch.size) cdx_json_raw(&records->branch, ",");
            cdx_json_quote(&records->branch, names[i] + 7, prefix - 7u);
            cdx_json_raw(&records->branch, ":");
            cdx_json_quote(&records->branch, record + prefix + 3u, length - prefix - 3u);
            return NULL;
        }
    }
    return "parse_error";
}

static const char *git_entry(adapter_records *records, const char *record, size_t length,
                             const char *original, size_t original_length) {
    const char *kind, *path = record + 2u, *cursor = record;
    const char *fields[10] = {0};
    size_t sizes[10] = {0}, count = 0, required = 0, i;
    char type = record[0];
    if (type == '1') { kind = "ordinary"; required = 8u; }
    else if (type == '2') { kind = "renamed"; required = 9u; }
    else if (type == 'u') { kind = "unmerged"; required = 10u; }
    else if (type == '?') kind = "untracked";
    else if (type == '!') kind = "ignored";
    else return "parse_error";
    for (i = 0; i < required; ++i) {
        const char *space = (const char *)memchr(cursor, ' ', (size_t)(record + length - cursor));
        if (!space || space == cursor) return "parse_error";
        fields[count] = cursor; sizes[count++] = (size_t)(space - cursor); cursor = space + 1u;
    }
    if (required) path = cursor;
    if (path >= record + length || (required && (sizes[1] != 2u || sizes[2] != 4u))) return "parse_error";
    if (records->count) cdx_json_raw(&records->entries, ",");
    cdx_json_raw(&records->entries, "{\"kind\":"); cdx_json_quote(&records->entries, kind, strlen(kind));
    cdx_json_raw(&records->entries, ",\"index_status\":"); cdx_json_quote(&records->entries, required ? fields[1] : record, 1);
    cdx_json_raw(&records->entries, ",\"worktree_status\":"); cdx_json_quote(&records->entries, required ? fields[1] + 1 : record, 1);
    cdx_json_raw(&records->entries, ",\"path\":"); cdx_json_quote(&records->entries, path, (size_t)(record + length - path));
    if (required) {
        cdx_json_raw(&records->entries, ",\"submodule\":"); cdx_json_quote(&records->entries, fields[2], sizes[2]);
    }
    if (original) {
        cdx_json_raw(&records->entries, ",\"original_path\":"); cdx_json_quote(&records->entries, original, original_length);
        cdx_json_raw(&records->entries, ",\"score\":"); cdx_json_quote(&records->entries, fields[8], sizes[8]);
    }
    cdx_json_raw(&records->entries, "}");
    ++records->count;
    return records->entries.failed ? "io_error" : NULL;
}

static const char *parse_git(const cdx_exec_result *process, const adapter_request *request,
                             adapter_records *records, uint64_t deadline) {
    size_t offset = 0, steps = 0;
    const char *status;
    while (offset < process->stdout_bytes.size && steps++ < ADAPTER_RECORD_LIMIT) {
        const char *record = (const char *)process->stdout_bytes.data + offset;
        const char *end = (const char *)memchr(record, 0, process->stdout_bytes.size - offset);
        const char *original = NULL;
        size_t length, original_length = 0;
        if ((status = interrupted(request->execution.cancel_event, deadline)) != NULL) return status;
        if (!end || (length = (size_t)(end - record)) < 3u || record[1] != ' ') return "parse_error";
        if (!cdx_validate_utf8(record, length)) return "encoding_error";
        offset += length + 1u;
        if (record[0] == '#') {
            if ((status = git_header(records, record, length)) != NULL) return status;
            continue;
        }
        if (record[0] == '2') {
            original = (const char *)process->stdout_bytes.data + offset;
            end = (const char *)memchr(original, 0, process->stdout_bytes.size - offset);
            if (!end || !(original_length = (size_t)(end - original))) return "parse_error";
            if (!cdx_validate_utf8(original, original_length)) return "encoding_error";
            offset += original_length + 1u;
        }
        if (records->count == request->max_entries) { records->truncated = 1; continue; }
        if ((status = git_entry(records, record, length, original, original_length)) != NULL) return status;
    }
    if ((status = interrupted(request->execution.cancel_event, deadline)) != NULL) return status;
    return offset == process->stdout_bytes.size ? NULL : "parse_error";
}

static void base64(cdx_json_output *output, const cdx_byte_buffer *bytes) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i;
    cdx_json_raw(output, "\"");
    for (i = 0; i < bytes->size; i += 3u) {
        unsigned value = (unsigned)bytes->data[i] << 16;
        char encoded[4];
        if (i + 1u < bytes->size) value |= (unsigned)bytes->data[i + 1u] << 8;
        if (i + 2u < bytes->size) value |= bytes->data[i + 2u];
        encoded[0] = alphabet[value >> 18]; encoded[1] = alphabet[(value >> 12) & 63u];
        encoded[2] = i + 1u < bytes->size ? alphabet[(value >> 6) & 63u] : '=';
        encoded[3] = i + 2u < bytes->size ? alphabet[value & 63u] : '=';
        cdx_json_rawn(output, encoded, sizeof(encoded));
    }
    cdx_json_raw(output, "\"");
}

static void write_result(cdx_json_output *output, const char *status, const adapter_request *request,
                         const cdx_exec_result *process, const adapter_records *records) {
    int parsed = !strcmp(status, "completed") || !strcmp(status, "no_match") ||
        (!strcmp(status, "output_limit") && process->status == CDX_COMPLETED && records->truncated);
    int stderr_valid = cdx_validate_utf8((const char *)process->stderr_bytes.data, process->stderr_bytes.size);
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, status, strlen(status));
    cdx_json_raw(output, ",\"process_status\":");
    if (process->pid || process->exit_code_known) cdx_json_quote(output, cdx_status_name(process->status), strlen(cdx_status_name(process->status)));
    else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"exit_code\":");
    if (process->exit_code_known) cdx_json_uint(output, process->exit_code); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"pid\":"); cdx_json_uint(output, process->pid);
    cdx_json_raw(output, ",\"parent_pid\":"); cdx_json_uint(output, process->parent_pid);
    cdx_json_raw(output, ",\"creation_filetime\":"); cdx_json_uint(output, process->creation_filetime);
    cdx_json_raw(output, ",\"elapsed_ms\":"); cdx_json_uint(output, process->elapsed_ms);
    cdx_json_raw(output, ",\"cleanup_complete\":"); cdx_json_raw(output, process->cleanup_complete ? "true" : "false");
    cdx_json_raw(output, ",\"truncated\":"); cdx_json_raw(output, records->truncated || process->output_truncated ? "true" : "false");
    cdx_json_raw(output, ",\"stderr\":");
    if (stderr_valid) cdx_json_quote(output, (const char *)process->stderr_bytes.data, process->stderr_bytes.size);
    else { cdx_json_raw(output, "null,\"stderr_base64\":"); base64(output, &process->stderr_bytes); }
    if (!parsed) { cdx_json_raw(output, ",\"stdout_base64\":"); base64(output, &process->stdout_bytes); }
    cdx_json_raw(output, request->search ? ",\"matches\":[" : ",\"entries\":[");
    if (parsed && records->entries.size) cdx_json_rawn(output, records->entries.data, records->entries.size);
    cdx_json_raw(output, "]");
    if (request->search) {
        cdx_json_raw(output, ",\"max_matches_per_file\":"); cdx_json_uint(output, request->per_file);
        cdx_json_raw(output, ",\"per_file_limit_reached\":"); cdx_json_raw(output, records->per_file_limit_reached ? "true" : "false");
        cdx_json_raw(output, ",\"encoding\":");
        if (request->encoding) cdx_json_quote(output, request->encoding, strlen(request->encoding)); else cdx_json_raw(output, "null");
    } else {
        cdx_json_raw(output, ",\"branch\":{");
        if (parsed && records->branch.size) cdx_json_rawn(output, records->branch.data, records->branch.size);
        cdx_json_raw(output, "}");
    }
    cdx_json_raw(output, "}");
}

static void free_request(adapter_request *request) {
    size_t i;
    for (i = 0; i < request->execution.arg_count; ++i) free(request->args[i]);
    free(request->program); free(request->cwd);
}
