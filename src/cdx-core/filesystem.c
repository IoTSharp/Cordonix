#include "cdx/filesystem.h"
#include <stdlib.h>
#include <string.h>

void cdx_fs_result_free(cdx_fs_result *result) {
    size_t i;
    if (!result) return;
    for (i = 0; i < result->entry_count; ++i) free(result->entries[i].name);
    free(result->entries);
    free(result->path);
    memset(result, 0, sizeof(*result));
}

const char *cdx_fs_status_name(cdx_fs_status status) {
    switch (status) {
    case CDX_FS_OK: return "ok";
    case CDX_FS_VALIDATION_ERROR: return "validation_error";
    case CDX_FS_NOT_FOUND: return "not_found";
    case CDX_FS_NOT_DIRECTORY: return "not_directory";
    case CDX_FS_PERMISSION_DENIED: return "permission_denied";
    case CDX_FS_ENCODING_ERROR: return "encoding_error";
    case CDX_FS_IO_ERROR: return "io_error";
    case CDX_FS_TIMED_OUT: return "timed_out";
    case CDX_FS_CANCELLED: return "cancelled";
    default: return "unknown";
    }
}

const char *cdx_fs_kind_name(cdx_fs_kind kind) {
    switch (kind) {
    case CDX_FS_FILE: return "file";
    case CDX_FS_DIRECTORY: return "directory";
    case CDX_FS_LINK: return "link";
    default: return "other";
    }
}
