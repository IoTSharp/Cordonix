#define _POSIX_C_SOURCE 200809L
#include "cdx/file_stat.h"
#include "file_stat_internal.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static uint64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static cdx_fs_status status_from_error(int error) {
    switch (error) {
    case ENOENT:
    case ENOTDIR: return CDX_FS_NOT_FOUND;
    case EACCES:
    case EPERM: return CDX_FS_PERMISSION_DENIED;
    case EILSEQ: return CDX_FS_ENCODING_ERROR;
    default: return CDX_FS_IO_ERROR;
    }
}

static int stopped(cdx_cancel_handle cancellation, uint64_t deadline) {
    if (cancellation && atomic_load(cancellation)) return 1;
    return monotonic_ms() >= deadline;
}

static cdx_fs_kind kind_from_mode(mode_t mode) {
    if (S_ISREG(mode)) return CDX_FS_FILE;
    if (S_ISDIR(mode)) return CDX_FS_DIRECTORY;
    if (S_ISLNK(mode)) return CDX_FS_LINK;
    return CDX_FS_OTHER;
}

static int timespec_to_ns(const struct timespec *value, int64_t *output) {
    const int64_t billion = 1000000000LL;
    if (value->tv_sec >= 0) {
        if (value->tv_sec > INT64_MAX / billion ||
            (value->tv_sec == INT64_MAX / billion && value->tv_nsec > INT64_MAX % billion)) return 0;
        *output = (int64_t)value->tv_sec * billion + value->tv_nsec;
    } else {
        if (value->tv_sec < INT64_MIN / billion - 1 ||
            (value->tv_sec == INT64_MIN / billion - 1 && value->tv_nsec < billion + INT64_MIN % billion)) return 0;
        *output = ((int64_t)value->tv_sec + 1) * billion - (billion - value->tv_nsec);
    }
    return 1;
}

void cdx_stat_platform_query(const cdx_stat_request *request, cdx_stat_result *result) {
    struct stat metadata;
    char *normalized = NULL;
    unsigned long native_error = 0;
    cdx_fs_status status;
    uint64_t deadline = monotonic_ms() + request->timeout_ms;
    const char *name;

    status = cdx_fs_normalize_path(request->path, request->cancellation, &normalized, &native_error);
    if (status != CDX_FS_OK) {
        result->status = status;
        result->native_error = native_error;
        return;
    }
    /* The normalizer allocates input length + 1; keep .. traversal through links. */
    memcpy(normalized, request->path, strlen(request->path) + 1u);
    result->path = normalized;
    if (stopped(request->cancellation, deadline)) {
        result->status = request->cancellation && atomic_load(request->cancellation) ? CDX_FS_CANCELLED : CDX_FS_TIMED_OUT;
        result->native_error = result->status == CDX_FS_CANCELLED ? ECANCELED : ETIMEDOUT;
        return;
    }
    /* The final link itself is queried; intermediate links retain POSIX traversal. */
    if (lstat(request->path, &metadata)) {
        result->status = status_from_error(errno);
        result->native_error = (unsigned long)errno;
        return;
    }
    result->kind = kind_from_mode(metadata.st_mode);
    result->mode_known = 1;
    result->mode_bits = (uint32_t)metadata.st_mode;
    result->size_known = result->kind == CDX_FS_FILE && metadata.st_size >= 0;
    if (result->size_known) result->size_bytes = (uint64_t)metadata.st_size;
    name = strrchr(result->path, '/');
    name = name ? name + 1 : result->path;
    result->hidden = name[0] == '.' && name[1] != 0;
    result->hidden_known = 1;
    result->system_known = 0;
    result->time_precision_ns = 1;
    result->time_precision_known = 1;
    result->access_time_known = timespec_to_ns(&metadata.st_atim, &result->access_time_ns);
    result->modification_time_known = timespec_to_ns(&metadata.st_mtim, &result->modification_time_ns);
    result->creation_time_known = 0;
    if (stopped(request->cancellation, deadline)) {
        result->status = request->cancellation && atomic_load(request->cancellation) ? CDX_FS_CANCELLED : CDX_FS_TIMED_OUT;
        result->native_error = result->status == CDX_FS_CANCELLED ? ECANCELED : ETIMEDOUT;
        return;
    }
    result->status = CDX_FS_OK;
}
