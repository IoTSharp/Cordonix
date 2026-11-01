#include "cdx/process_query.h"
#include <math.h>
#include <string.h>

static int invalid_request(const cdx_json *json, int arguments) {
    const char *strings[] = {"name", "command_contains"};
    const char *numbers[] = {"max_entries", "timeout_ms", "output_limit_bytes"};
    size_t i;
    if (arguments < 0 || json->nodes[arguments].type != CDX_JSON_OBJECT) return 1;
    for (i = 0; i < sizeof(strings) / sizeof(strings[0]); ++i) {
        int node = cdx_json_get(json, arguments, strings[i]);
        if (node >= 0 && json->nodes[node].type != CDX_JSON_STRING) return 1;
    }
    for (i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        int node = cdx_json_get(json, arguments, numbers[i]);
        if (node >= 0 && (json->nodes[node].type != CDX_JSON_NUMBER ||
                          json->nodes[node].number < 1.0 || !isfinite(json->nodes[node].number) ||
                          floor(json->nodes[node].number) != json->nodes[node].number)) return 1;
    }
    return 0;
}

void cdx_query_processes_call(const cdx_json *request, int arguments,
                              cdx_cancel_handle cancellation, cdx_json_output *output) {
    (void)cancellation;
    if (invalid_request(request, arguments)) {
        cdx_json_raw(output, "{\"status\":\"validation_error\",\"message\":\"query_processes arguments are invalid\"}");
        return;
    }
    cdx_json_raw(output, "{\"status\":\"not_supported\",\"message\":\"Process command-line inspection is not supported on this backend\"}");
}
