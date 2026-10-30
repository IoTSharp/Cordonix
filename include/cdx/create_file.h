#ifndef CDX_CREATE_FILE_H
#define CDX_CREATE_FILE_H

#include "cdx/document.h"

typedef enum cdx_create_status {
    CDX_CREATE_OK = 0,
    CDX_CREATE_VALIDATION_ERROR,
    CDX_CREATE_ENCODING_ERROR,
    CDX_CREATE_TOO_LARGE,
    CDX_CREATE_ALREADY_EXISTS,
    CDX_CREATE_NOT_FOUND,
    CDX_CREATE_PERMISSION_DENIED,
    CDX_CREATE_IO_ERROR,
    CDX_CREATE_TIMED_OUT,
    CDX_CREATE_CANCELLED
} cdx_create_status;

typedef struct cdx_create_result {
    cdx_create_status status;
    cdx_encoding encoding;
    size_t bytes;
    int written_to_disk;
    int cleanup_complete;
    char disk_sha256[CDX_SHA256_HEX_LENGTH + 1];
} cdx_create_result;

cdx_create_status cdx_create_file(const cdx_path_char *path, const char *text_utf8,
                                  size_t text_length, cdx_encoding encoding,
                                  cdx_cancel_handle cancellation, cdx_create_result *result);
const char *cdx_create_status_name(cdx_create_status status);

#endif
