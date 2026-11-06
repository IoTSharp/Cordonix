#ifndef CDX_FILE_OPS_H
#define CDX_FILE_OPS_H

#include "cdx/platform.h"
#include <stdint.h>

#define CDX_FILE_MAX_BYTES UINT64_C(1073741824)

typedef enum cdx_file_operation {
    CDX_FILE_HASH = 0,
    CDX_FILE_COPY,
    CDX_FILE_MOVE
} cdx_file_operation;

typedef struct cdx_file_request {
    cdx_file_operation operation;
    const cdx_path_char *source;
    const cdx_path_char *destination;
    uint64_t max_bytes;
    unsigned timeout_ms;
    cdx_cancel_handle cancellation;
} cdx_file_request;

typedef struct cdx_file_result {
    const char *status;
    uint64_t bytes_processed;
    uint64_t size_bytes;
    uint64_t elapsed_ms;
    unsigned long native_error;
    char sha256[65];
    int written_to_disk;
    int source_removed;
    int file_changed;
    int cleanup_complete;
    int metadata_checked;
    int metadata_preserved;
} cdx_file_result;

/* Regular files only. Copy/move never replace an existing destination. */
void cdx_file_execute(const cdx_file_request *request, cdx_file_result *result);

#endif
