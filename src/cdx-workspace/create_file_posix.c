#define _POSIX_C_SOURCE 200809L
#include "cdx/create_file.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static uint64_t milliseconds(void);
static cdx_create_status checkpoint(cdx_cancel_handle cancellation, uint64_t deadline);
static cdx_create_status error_status(int error);
static cdx_create_status write_temporary(int file, const unsigned char *bytes, size_t length,
                                         cdx_cancel_handle cancellation, uint64_t deadline);

cdx_create_status cdx_create_publish(const char *path, const unsigned char *bytes,
                                     size_t length, cdx_cancel_handle cancellation,
                                     cdx_create_result *result) {
    uint64_t deadline = milliseconds() + 15000u;
    size_t capacity = strlen(path) + sizeof(".cdx.tmp.XXXXXX");
    char *temporary_path = NULL, *directory = NULL, *separator;
    int temporary = -1, dir = -1, created = 0;
    struct stat metadata;
    cdx_create_status status = checkpoint(cancellation, deadline);
    if (status != CDX_CREATE_OK) return status;
    if (lstat(path, &metadata) == 0) return CDX_CREATE_ALREADY_EXISTS;
    if (errno != ENOENT && errno != ENOTDIR) return error_status(errno);
    directory = strdup(path);
    temporary_path = (char *)malloc(capacity);
    if (!directory || !temporary_path) { status = CDX_CREATE_IO_ERROR; goto done; }
    separator = strrchr(directory, '/');
    if (separator == directory) separator[1] = 0; else *separator = 0;
    dir = open(directory, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (dir < 0) { status = error_status(errno); goto done; }
    if (snprintf(temporary_path, capacity, "%s.cdx.tmp.XXXXXX", path) >= (int)capacity) {
        status = CDX_CREATE_VALIDATION_ERROR; goto done;
    }
    temporary = mkstemp(temporary_path);
    if (temporary < 0) { status = error_status(errno); goto done; }
    created = 1;
    status = write_temporary(temporary, bytes, length, cancellation, deadline);
    if (status != CDX_CREATE_OK) goto done;
    if (close(temporary)) { temporary = -1; status = CDX_CREATE_IO_ERROR; goto done; }
    temporary = -1;
    status = checkpoint(cancellation, deadline);
    if (status != CDX_CREATE_OK) goto done;
    /* link publishes the prepared inode without replacing an existing directory entry. */
    if (link(temporary_path, path)) { status = error_status(errno); goto done; }
    result->written_to_disk = 1;
    if (unlink(temporary_path)) { result->cleanup_complete = 0; status = CDX_CREATE_IO_ERROR; goto done; }
    created = 0;
    if (fsync(dir)) status = CDX_CREATE_IO_ERROR;
done:
    if (temporary >= 0 && close(temporary)) status = CDX_CREATE_IO_ERROR;
    if (created && result->cleanup_complete && unlink(temporary_path)) { result->cleanup_complete = 0; status = CDX_CREATE_IO_ERROR; }
    if (dir >= 0 && close(dir)) status = CDX_CREATE_IO_ERROR;
    free(temporary_path);
    free(directory);
    return status;
}

static uint64_t milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static cdx_create_status checkpoint(cdx_cancel_handle cancellation, uint64_t deadline) {
    if (cancellation && atomic_load(cancellation)) return CDX_CREATE_CANCELLED;
    return milliseconds() >= deadline ? CDX_CREATE_TIMED_OUT : CDX_CREATE_OK;
}

static cdx_create_status error_status(int error) {
    switch (error) {
    case EEXIST: return CDX_CREATE_ALREADY_EXISTS;
    case ENOENT: case ENOTDIR: return CDX_CREATE_NOT_FOUND;
    case EACCES: case EPERM: return CDX_CREATE_PERMISSION_DENIED;
    case EINVAL: case ENAMETOOLONG: return CDX_CREATE_VALIDATION_ERROR;
    default: return CDX_CREATE_IO_ERROR;
    }
}

static cdx_create_status write_temporary(int file, const unsigned char *bytes, size_t length,
                                         cdx_cancel_handle cancellation, uint64_t deadline) {
    size_t offset = 0, step;
    cdx_create_status status;
    for (step = 0; offset < length && step < CDX_DOCUMENT_MAX_BYTES / 65536u + 1u; ++step) {
        size_t requested = length - offset < 65536u ? length - offset : 65536u;
        ssize_t count;
        status = checkpoint(cancellation, deadline);
        if (status != CDX_CREATE_OK) return status;
        count = write(file, bytes + offset, requested);
        if (count < 0 || (size_t)count != requested) return CDX_CREATE_IO_ERROR;
        offset += (size_t)count;
    }
    if (offset != length) return CDX_CREATE_IO_ERROR;
    status = checkpoint(cancellation, deadline);
    if (status != CDX_CREATE_OK) return status;
    return fsync(file) ? CDX_CREATE_IO_ERROR : CDX_CREATE_OK;
}
