#include "cdx/operations.h"
#include "cdx/json.h"
#include <stdio.h>
#include <string.h>

static unsigned total;
static unsigned passed;

static void record(const char *name, int success) {
    ++total;
    if (success) ++passed;
    printf("%s: %s\n", name, success ? "PASS" : "FAIL");
}

static int string_is(const cdx_json *json, int object, const char *key, const char *value) {
    int node = cdx_json_get(json, object, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_STRING && !strcmp(json->nodes[node].string, value);
}

#ifdef _WIN32
static int has_process_entry_shape(const cdx_json *json) {
    int entries = cdx_json_get(json, 0, "entries");
    int node;
    unsigned checked = 0;
    if (entries < 0 || json->nodes[entries].type != CDX_JSON_ARRAY) return 0;
    for (node = json->nodes[entries].first; node >= 0 && checked++ < 1000u; node = json->nodes[node].next) {
        int command = cdx_json_get(json, node, "command_line");
        int created = cdx_json_get(json, node, "creation_filetime");
        int pid = cdx_json_get(json, node, "pid");
        if (command >= 0 && created >= 0 && pid >= 0 &&
            (json->nodes[command].type == CDX_JSON_STRING || json->nodes[command].type == CDX_JSON_NULL) &&
            (json->nodes[created].type == CDX_JSON_NUMBER || json->nodes[created].type == CDX_JSON_NULL) &&
            json->nodes[pid].type == CDX_JSON_NUMBER) return 1;
    }
    return 0;
}
#endif

static int call(const char *arguments, cdx_json *result) {
    cdx_json request;
    cdx_json_output output;
    char input[1024];
    int success;
    (void)snprintf(input, sizeof(input), "{%s}", arguments ? arguments : "");
    memset(result, 0, sizeof(*result));
    cdx_json_output_init(&output);
    success = cdx_json_parse(&request, input, strlen(input));
    if (success) {
        cdx_call("query_processes", &request, 0, NULL, &output);
        success = !output.failed && cdx_json_parse(result, output.data, output.size);
        cdx_json_free(&request);
    }
    cdx_json_output_free(&output);
    return success;
}

int main(void) {
    cdx_json result;
    int valid;
    valid = call("\"max_entries\":0", &result);
    record("query_processes rejects zero max_entries", valid && string_is(&result, 0, "status", "validation_error"));
    if (valid) cdx_json_free(&result);
    valid = call("\"max_entries\":1000,\"timeout_ms\":10000,\"output_limit_bytes\":262144", &result);
#ifdef _WIN32
    record("query_processes returns a bounded Windows snapshot", valid &&
        (string_is(&result, 0, "status", "ok") || string_is(&result, 0, "status", "partial") ||
         (string_is(&result, 0, "status", "output_limit") && cdx_json_get(&result, 0, "truncated") >= 0 &&
          result.nodes[cdx_json_get(&result, 0, "truncated")].type == CDX_JSON_BOOL &&
          result.nodes[cdx_json_get(&result, 0, "truncated")].boolean)) && has_process_entry_shape(&result));
#else
    record("query_processes is explicit on POSIX", valid && string_is(&result, 0, "status", "not_supported"));
#endif
    if (valid) cdx_json_free(&result);
    printf("%u/%u process-query checks passed\n", passed, total);
    return total && passed == total ? 0 : 1;
}
