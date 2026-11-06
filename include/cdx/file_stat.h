#ifndef CDX_FILE_STAT_H
#define CDX_FILE_STAT_H

#include "cdx/filesystem.h"
#include <stdint.h>

#define CDX_STAT_MAX_TIMEOUT_MS 60000u

typedef struct cdx_stat_request {
    const cdx_path_char *path;
    unsigned timeout_ms;
    cdx_cancel_handle cancellation;
} cdx_stat_request;

typedef struct cdx_stat_result {
    cdx_fs_status status;
    char *path;
    cdx_fs_kind kind;
    uint64_t size_bytes;
    uint32_t mode_bits;
    int size_known;
    int mode_known;
    int hidden;
    int hidden_known;
    int system;
    int system_known;
    int64_t creation_time_ns;
    int64_t access_time_ns;
    int64_t modification_time_ns;
    int creation_time_known;
    int access_time_known;
    int modification_time_known;
    uint32_t time_precision_ns;
    int time_precision_known;
    unsigned long native_error;
} cdx_stat_result;

cdx_fs_status cdx_stat_query(const cdx_stat_request *request, cdx_stat_result *result);
void cdx_stat_result_free(cdx_stat_result *result);

#endif
