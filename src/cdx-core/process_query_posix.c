#include "cdx/process_query.h"
#include <string.h>

void cdx_query_processes(const cdx_process_query_request *request, cdx_process_query_result *result) {
    (void)request;
    memset(result, 0, sizeof(*result));
    result->status = "not_supported";
    result->stop_reason = "windows_process_snapshot_only";
}

void cdx_process_query_free(cdx_process_query_result *result) {
    memset(result, 0, sizeof(*result));
}
