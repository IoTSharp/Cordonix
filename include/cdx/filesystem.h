#ifndef CDX_FILESYSTEM_H
#define CDX_FILESYSTEM_H

#include "cdx/platform.h"
#include <stddef.h>
#include <stdint.h>

#define CDX_FS_MAX_ENTRIES 1000u

typedef enum cdx_fs_status {
    CDX_FS_OK = 0,
    CDX_FS_VALIDATION_ERROR,
    CDX_FS_NOT_FOUND,
    CDX_FS_NOT_DIRECTORY,
    CDX_FS_PERMISSION_DENIED,
    CDX_FS_ENCODING_ERROR,
    CDX_FS_IO_ERROR,
    CDX_FS_TIMED_OUT,
    CDX_FS_CANCELLED
} cdx_fs_status;

typedef enum cdx_fs_kind {
    CDX_FS_FILE = 0,
    CDX_FS_DIRECTORY,
    CDX_FS_LINK,
    CDX_FS_OTHER
} cdx_fs_kind;

typedef struct cdx_fs_entry {
    char *name;
    cdx_fs_kind kind;
    uint64_t size_bytes;
    int size_known;
    int hidden;
    int system;
} cdx_fs_entry;

typedef struct cdx_fs_request {
    const cdx_path_char *path;
    int list_directory;
    int include_hidden;
    size_t max_entries; /* Applies only to directory listing. */
    cdx_cancel_handle cancellation;
} cdx_fs_request;

typedef struct cdx_fs_result {
    cdx_fs_status status;
    char *path;
    cdx_fs_kind kind;
    cdx_fs_entry *entries;
    size_t entry_count;
    int truncated;
    unsigned long native_error;
} cdx_fs_result;

cdx_fs_status cdx_fs_query(const cdx_fs_request *request, cdx_fs_result *result);
/* Pure lexical normalization; caller frees the native path, even when it does not exist. */
cdx_fs_status cdx_fs_normalize_path(const cdx_path_char *path, cdx_cancel_handle cancellation,
                                    cdx_path_char **normalized, unsigned long *native_error);
/* Inputs are normalized absolute paths. This is a lexical boundary, not symlink containment. */
int cdx_fs_path_within(const cdx_path_char *path, const cdx_path_char *root);
void cdx_fs_result_free(cdx_fs_result *result);
const char *cdx_fs_status_name(cdx_fs_status status);
const char *cdx_fs_kind_name(cdx_fs_kind kind);

#endif
