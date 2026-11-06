#include "cdx/file_stat.h"
#include "file_stat_internal.h"
#include <stdlib.h>
#include <string.h>

cdx_fs_status cdx_stat_query(const cdx_stat_request *request, cdx_stat_result *result) {
    if (!result) return CDX_FS_VALIDATION_ERROR;
    memset(result, 0, sizeof(*result));
    result->status = CDX_FS_VALIDATION_ERROR;
    if (!request || !request->path || !request->timeout_ms || request->timeout_ms > CDX_STAT_MAX_TIMEOUT_MS)
        return result->status;
    cdx_stat_platform_query(request, result);
    return result->status;
}

void cdx_stat_result_free(cdx_stat_result *result) {
    if (!result) return;
    free(result->path);
    memset(result, 0, sizeof(*result));
}
