#ifndef CDX_FILE_OPS_INTERNAL_H
#define CDX_FILE_OPS_INTERNAL_H

#include "cdx/file_ops.h"
#include <stddef.h>

typedef struct cdx_file_sha256 {
    uint32_t state[8];
    uint64_t length;
    unsigned char block[64];
    size_t used;
} cdx_file_sha256;

void cdx_file_sha256_init(cdx_file_sha256 *context);
void cdx_file_sha256_update(cdx_file_sha256 *context, const unsigned char *bytes, size_t length);
void cdx_file_sha256_finish(cdx_file_sha256 *context, char output[65]);
uint64_t cdx_file_clock_ms(void);
const char *cdx_file_checkpoint(cdx_cancel_handle cancellation, uint64_t deadline);
void cdx_file_platform_execute(const cdx_file_request *request, uint64_t deadline, cdx_file_result *result);

#endif
