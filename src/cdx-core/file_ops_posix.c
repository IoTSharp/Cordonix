#define _GNU_SOURCE
#include "cdx/file_ops.h"
#include "file_ops_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#include <time.h>
#include <unistd.h>

uint64_t cdx_file_clock_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

const char *cdx_file_checkpoint(cdx_cancel_handle cancellation, uint64_t deadline) {
    if (cancellation && atomic_load(cancellation)) { errno = ECANCELED; return "cancelled"; }
    if (cdx_file_clock_ms() >= deadline) { errno = ETIMEDOUT; return "timed_out"; }
    return NULL;
}

static const char *error_status(int error) {
    switch (error) {
    case ENOENT:
    case ENOTDIR: return "not_found";
    case EACCES:
    case EPERM: return "permission_denied";
    case EEXIST: return "already_exists";
    case EXDEV: return "cross_device";
    case ENOSYS:
    case EOPNOTSUPP: return "not_supported";
    default: return "io_error";
    }
}

static int rename_no_replace(const char *source, const char *destination) {
#if defined(__linux__) && defined(SYS_renameat2)
    return (int)syscall(SYS_renameat2, AT_FDCWD, source, AT_FDCWD, destination, 1u);
#else
    (void)source; (void)destination; errno = ENOSYS; return -1;
#endif
}

static const char *rename_status(int error) {
    /* Kernels/filesystems without RENAME_NOREPLACE must not fall back to replacing rename. */
    return error == EINVAL ? "not_supported" : error_status(error);
}

static int same_source(const struct stat *before, const struct stat *after) {
    return before->st_dev == after->st_dev && before->st_ino == after->st_ino && before->st_size == after->st_size &&
           before->st_mtim.tv_sec == after->st_mtim.tv_sec && before->st_mtim.tv_nsec == after->st_mtim.tv_nsec;
}

static const char *stream_file(const cdx_file_request *request, int source, int destination,
                               uint64_t deadline, cdx_file_result *result) {
    unsigned char buffer[65536];
    cdx_file_sha256 hash;
    unsigned step;
    const char *status;
    cdx_file_sha256_init(&hash);
    for (step = 0; step <= (unsigned)(CDX_FILE_MAX_BYTES / sizeof(buffer)); ++step) {
        ssize_t received;
        size_t offset = 0;
        status = cdx_file_checkpoint(request->cancellation, deadline);
        if (status) return status;
        received = read(source, buffer, sizeof(buffer));
        if (received < 0) return error_status(errno);
        if (!received) {
            if (result->bytes_processed != result->size_bytes) { result->file_changed = 1; errno = ESTALE; return "file_changed"; }
            cdx_file_sha256_finish(&hash, result->sha256); return "ok";
        }
        if (result->bytes_processed + (uint64_t)received > request->max_bytes ||
            result->bytes_processed + (uint64_t)received > result->size_bytes) {
            result->file_changed = 1; errno = ESTALE; return "file_changed";
        }
        cdx_file_sha256_update(&hash, buffer, (size_t)received);
        for (unsigned write_step = 0; destination >= 0 && offset < (size_t)received && write_step < sizeof(buffer); ++write_step) {
            ssize_t written;
            status = cdx_file_checkpoint(request->cancellation, deadline);
            if (status) return status;
            written = write(destination, buffer + offset, (size_t)received - offset);
            if (written <= 0) return "io_error";
            offset += (size_t)written;
        }
        if (destination >= 0 && offset != (size_t)received) return "io_error";
        result->bytes_processed += (uint64_t)received;
    }
    errno = EFBIG; return "too_large";
}

void cdx_file_platform_execute(const cdx_file_request *request, uint64_t deadline, cdx_file_result *result) {
    int source = -1, destination = -1, temporary_created = 0, error = 0;
    struct stat before, after, copied_metadata;
    struct timespec times[2];
    char *temporary = NULL;
    size_t capacity;
    result->status = cdx_file_checkpoint(request->cancellation, deadline);
    if (result->status) goto done;
    source = open(request->source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (source < 0) { result->status = errno == ELOOP ? "not_regular_file" : error_status(errno); goto done; }
    if (fstat(source, &before)) { result->status = "io_error"; goto done; }
    if (!S_ISREG(before.st_mode) || before.st_size < 0) { result->status = "not_regular_file"; errno = EINVAL; goto done; }
    result->size_bytes = (uint64_t)before.st_size;
    if (request->operation != CDX_FILE_HASH) {
        struct stat existing;
        if (!lstat(request->destination, &existing)) { errno = EEXIST; result->status = "already_exists"; goto done; }
        if (errno != ENOENT) { result->status = error_status(errno); goto done; }
    }
    if (request->operation == CDX_FILE_MOVE) {
        result->status = cdx_file_checkpoint(request->cancellation, deadline);
        if (result->status) goto done;
        if (rename_no_replace(request->source, request->destination)) { result->status = rename_status(errno); goto done; }
        result->written_to_disk = 1; result->source_removed = 1; result->status = "ok"; goto done;
    }
    if (result->size_bytes > request->max_bytes) { result->status = "too_large"; errno = EFBIG; goto done; }
    if (request->operation == CDX_FILE_COPY) {
        capacity = strlen(request->destination) + 24u;
        if (capacity > PATH_MAX) { result->status = "validation_error"; errno = ENAMETOOLONG; goto done; }
        temporary = (char *)malloc(capacity);
        if (!temporary) { result->status = "io_error"; goto done; }
        if (snprintf(temporary, capacity, "%s.cdx-copy-XXXXXX", request->destination) < 0) { result->status = "io_error"; goto done; }
        result->status = cdx_file_checkpoint(request->cancellation, deadline);
        if (result->status) goto done;
        destination = mkstemp(temporary);
        if (destination < 0) { result->status = error_status(errno); goto done; }
        temporary_created = 1;
    }
    result->status = stream_file(request, source, destination, deadline, result);
    if (strcmp(result->status, "ok")) goto done;
    if (fstat(source, &after)) { result->status = "io_error"; goto done; }
    if (!same_source(&before, &after)) { result->status = "file_changed"; result->file_changed = 1; errno = ESTALE; goto done; }
    if (request->operation == CDX_FILE_HASH) goto done;
    times[0] = before.st_atim; times[1] = before.st_mtim;
    if (fchmod(destination, before.st_mode & 0777u) || futimens(destination, times) || fsync(destination) || fstat(destination, &copied_metadata)) {
        result->status = "io_error"; goto done;
    }
    result->metadata_checked = 1;
    result->metadata_preserved = (before.st_mode & 0777u) == (copied_metadata.st_mode & 0777u) &&
        before.st_atim.tv_sec == copied_metadata.st_atim.tv_sec && before.st_atim.tv_nsec == copied_metadata.st_atim.tv_nsec &&
        before.st_mtim.tv_sec == copied_metadata.st_mtim.tv_sec && before.st_mtim.tv_nsec == copied_metadata.st_mtim.tv_nsec;
    if (close(destination)) { destination = -1; result->status = "io_error"; goto done; }
    destination = -1;
    result->status = cdx_file_checkpoint(request->cancellation, deadline);
    if (result->status) goto done;
    if (link(temporary, request->destination)) { result->status = error_status(errno); goto done; }
    result->written_to_disk = 1; result->status = "ok";
done:
    if (!result->status) result->status = "io_error";
    if (strcmp(result->status, "ok")) error = errno;
    if (destination >= 0 && close(destination) && !error) error = errno;
    if (source >= 0 && close(source) && !error) error = errno;
    if (temporary_created && unlink(temporary)) {
        result->cleanup_complete = 0;
        if (!error) error = errno;
    }
    if (error && !strcmp(result->status, "ok")) result->status = "io_error";
    if (!result->written_to_disk) { result->metadata_checked = 0; result->metadata_preserved = 0; }
    if (strcmp(result->status, "ok")) result->sha256[0] = 0;
    result->native_error = (unsigned long)error;
    free(temporary);
}
