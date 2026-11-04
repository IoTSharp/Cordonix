#include "cdx/operations.h"
#include "cdx/process_query.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static size_t integer(const cdx_json *json, int object, const char *name, size_t fallback, size_t maximum, int *valid) {
    int node = cdx_json_get(json, object, name);
    double value;
    if (node < 0) return fallback;
    value = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || value < 1 || value > (double)maximum || floor(value) != value) {
        *valid = 0; return fallback;
    }
    return (size_t)value;
}

void cdx_process_query_call(const cdx_json *json, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output) {
    cdx_process_query_request request;
    cdx_process_query_result result;
    int array, node, valid = 1;
    size_t i, term_bytes = 0;
    memset(&request, 0, sizeof(request));
    request.cancellation = cancellation;
    request.limit = integer(json, arguments, "limit", 10, CDX_QUERY_MAX_RESULTS, &valid);
    request.max_processes = integer(json, arguments, "max_processes", 4096, 4096, &valid);
    request.timeout_ms = (uint32_t)integer(json, arguments, "timeout_ms", 5000, 10000, &valid);
    request.output_limit_bytes = integer(json, arguments, "output_limit_bytes", 65536, 262144, &valid);
    node = cdx_json_get(json, arguments, "include_self");
    if (node >= 0) {
        if (json->nodes[node].type != CDX_JSON_BOOL) valid = 0;
        else request.include_self = json->nodes[node].boolean;
    }
    array = cdx_json_get(json, arguments, "contains");
    if (array < 0 || json->nodes[array].type != CDX_JSON_ARRAY) valid = 0;
    else for (node = json->nodes[array].first; node >= 0; node = json->nodes[node].next) {
        if (request.term_count == CDX_QUERY_MAX_TERMS || json->nodes[node].type != CDX_JSON_STRING ||
            !*json->nodes[node].string || (term_bytes += strlen(json->nodes[node].string)) > 4096u) { valid = 0; break; }
        request.terms[request.term_count++] = json->nodes[node].string;
    }
    array = cdx_json_get(json, arguments, "pids");
    if (array >= 0) {
        if (json->nodes[array].type != CDX_JSON_ARRAY || json->nodes[array].first < 0) valid = 0;
        else for (node = json->nodes[array].first; node >= 0; node = json->nodes[node].next) {
            double value = json->nodes[node].number;
            if (request.pid_count == CDX_QUERY_MAX_PIDS || json->nodes[node].type != CDX_JSON_NUMBER ||
                value < 1 || value > UINT32_MAX || floor(value) != value) { valid = 0; break; }
            request.pids[request.pid_count++] = (uint32_t)value;
        }
    }
    if (!valid || !request.term_count) {
        cdx_json_raw(output, "{\"status\":\"validation_error\",\"message\":\"Supply 1..16 literal contains terms and bounded numeric fields; pids, when supplied, contains 1..32 positive IDs\"}"); return;
    }
    cdx_query_processes(&request, &result);
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, result.status, strlen(result.status));
    cdx_json_raw(output, ",\"read_only\":true,\"scan_complete\":"); cdx_json_raw(output, result.scan_complete ? "true" : "false");
    cdx_json_raw(output, ",\"stop_reason\":");
    if (result.stop_reason) cdx_json_quote(output, result.stop_reason, strlen(result.stop_reason)); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"examined\":"); cdx_json_uint(output, result.examined);
    cdx_json_raw(output, ",\"access_denied\":"); cdx_json_uint(output, result.access_denied);
    cdx_json_raw(output, ",\"disappeared\":"); cdx_json_uint(output, result.disappeared);
    cdx_json_raw(output, ",\"unavailable_command_lines\":"); cdx_json_uint(output, result.unavailable);
    cdx_json_raw(output, ",\"encoding_errors\":"); cdx_json_uint(output, result.encoding_errors);
    cdx_json_raw(output, ",\"excluded_self_pid\":"); cdx_json_uint(output, result.excluded_self_pid);
    cdx_json_raw(output, ",\"elapsed_ms\":"); cdx_json_uint(output, result.elapsed_ms);
    cdx_json_raw(output, ",\"native_error\":"); cdx_json_uint(output, result.native_error);
    cdx_json_raw(output, ",\"matches\":[");
    for (i = 0; i < result.count; ++i) {
        const cdx_process_entry *entry = &result.entries[i];
        char created[32];
        if (i) cdx_json_raw(output, ",");
        cdx_json_raw(output, "{\"pid\":"); cdx_json_uint(output, entry->pid);
        cdx_json_raw(output, ",\"parent_pid\":"); cdx_json_uint(output, entry->parent_pid);
        (void)snprintf(created, sizeof(created), "%llu", (unsigned long long)entry->creation_filetime);
        cdx_json_raw(output, ",\"creation_filetime\":"); cdx_json_quote(output, created, strlen(created));
        cdx_json_raw(output, ",\"created_utc\":"); cdx_json_quote(output, entry->created_utc, strlen(entry->created_utc));
        cdx_json_raw(output, ",\"command_line\":"); cdx_json_quote(output, entry->command_line, strlen(entry->command_line));
        cdx_json_raw(output, ",\"command_line_source\":\"win32_original\"}");
    }
    cdx_json_raw(output, "]}");
    cdx_process_query_free(&result);
}
