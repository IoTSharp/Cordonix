#include "cdx/process.h"

#include <stdlib.h>
#include <string.h>

void cdx_result_init(cdx_exec_result *result) {
    if (!result) return;
    memset(result, 0, sizeof(*result));
    result->status = CDX_VALIDATION_ERROR;
    result->cleanup_complete = 1;
}

void cdx_result_free(cdx_exec_result *result) {
    if (!result) return;
    free(result->stdout_bytes.data);
    free(result->stderr_bytes.data);
    result->stdout_bytes.data = NULL;
    result->stderr_bytes.data = NULL;
    result->stdout_bytes.size = result->stderr_bytes.size = 0;
}

const char *cdx_status_name(cdx_status status) {
    switch (status) {
    case CDX_COMPLETED: return "completed";
    case CDX_FAILED: return "failed";
    case CDX_TIMED_OUT: return "timed_out";
    case CDX_CANCELLED: return "cancelled";
    case CDX_OUTPUT_LIMIT: return "output_limit";
    case CDX_VALIDATION_ERROR: return "validation_error";
    case CDX_SPAWN_ERROR: return "spawn_error";
    case CDX_EXECUTION_ERROR: return "execution_error";
    default: return "unknown";
    }
}
