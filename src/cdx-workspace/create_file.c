#include "cdx/create_file.h"
#include "cdx/filesystem.h"
#include "document_internal.h"
#include <stdlib.h>
#include <string.h>

cdx_create_status cdx_create_publish(const cdx_path_char *path, const unsigned char *bytes,
                                     size_t length, cdx_cancel_handle cancellation,
                                     cdx_create_result *result);

static cdx_create_status path_status(cdx_fs_status status);

cdx_create_status cdx_create_file(const cdx_path_char *path, const char *text_utf8,
                                  size_t text_length, cdx_encoding encoding,
                                  cdx_cancel_handle cancellation, cdx_create_result *result) {
    cdx_path_char *normalized = NULL;
    unsigned char *bytes = NULL;
    size_t length = 0;
    char hash[CDX_SHA256_HEX_LENGTH + 1];
    cdx_create_status status;
    if (!result) return CDX_CREATE_VALIDATION_ERROR;
    memset(result, 0, sizeof(*result));
    result->cleanup_complete = 1;
    result->encoding = encoding;
    if (!text_utf8 || encoding <= CDX_ENCODING_AUTO || encoding > CDX_ENCODING_CP936)
        return result->status = CDX_CREATE_VALIDATION_ERROR;
    if (text_length > CDX_DOCUMENT_MAX_BYTES) return result->status = CDX_CREATE_TOO_LARGE;
    if (!cdx_validate_utf8(text_utf8, text_length)) return result->status = CDX_CREATE_ENCODING_ERROR;
    status = path_status(cdx_fs_normalize_path(path, cancellation, &normalized, NULL));
    if (status != CDX_CREATE_OK) goto done;
    if (!cdx_encode_text(text_utf8, text_length, encoding, &bytes, &length)) {
        status = CDX_CREATE_ENCODING_ERROR; goto done;
    }
    if (length > CDX_DOCUMENT_MAX_BYTES) { status = CDX_CREATE_TOO_LARGE; goto done; }
    if (!cdx_sha256(bytes, length, hash)) { status = CDX_CREATE_IO_ERROR; goto done; }
    status = cdx_create_publish(normalized, bytes, length, cancellation, result);
    if (result->written_to_disk) result->bytes = length;
    if (status == CDX_CREATE_OK) memcpy(result->disk_sha256, hash, sizeof(hash));
done:
    free(normalized);
    free(bytes);
    return result->status = status;
}

static cdx_create_status path_status(cdx_fs_status status) {
    switch (status) {
    case CDX_FS_OK: return CDX_CREATE_OK;
    case CDX_FS_VALIDATION_ERROR: return CDX_CREATE_VALIDATION_ERROR;
    case CDX_FS_ENCODING_ERROR: return CDX_CREATE_ENCODING_ERROR;
    case CDX_FS_CANCELLED: return CDX_CREATE_CANCELLED;
    case CDX_FS_TIMED_OUT: return CDX_CREATE_TIMED_OUT;
    default: return CDX_CREATE_IO_ERROR;
    }
}

const char *cdx_create_status_name(cdx_create_status status) {
    switch (status) {
    case CDX_CREATE_OK: return "ok";
    case CDX_CREATE_VALIDATION_ERROR: return "validation_error";
    case CDX_CREATE_ENCODING_ERROR: return "encoding_error";
    case CDX_CREATE_TOO_LARGE: return "too_large";
    case CDX_CREATE_ALREADY_EXISTS: return "already_exists";
    case CDX_CREATE_NOT_FOUND: return "not_found";
    case CDX_CREATE_PERMISSION_DENIED: return "permission_denied";
    case CDX_CREATE_IO_ERROR: return "io_error";
    case CDX_CREATE_TIMED_OUT: return "timed_out";
    case CDX_CREATE_CANCELLED: return "cancelled";
    default: return "unknown";
    }
}
