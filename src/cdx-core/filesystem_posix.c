#define _POSIX_C_SOURCE 200809L
#include "cdx/filesystem.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

int cdx_validate_utf8(const char *text, size_t length);

#define CDX_FS_MAX_STEPS 100000u
#define CDX_FS_DEADLINE_MS 5000u

static uint64_t milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static cdx_fs_status error_status(int error) {
    switch (error) {
    case ENOENT: return CDX_FS_NOT_FOUND;
    case ENOTDIR: return CDX_FS_NOT_DIRECTORY;
    case EACCES:
    case EPERM: return CDX_FS_PERMISSION_DENIED;
    default: return CDX_FS_IO_ERROR;
    }
}

static cdx_fs_kind entry_kind(mode_t mode) {
    if (S_ISREG(mode)) return CDX_FS_FILE;
    if (S_ISDIR(mode)) return CDX_FS_DIRECTORY;
    if (S_ISLNK(mode)) return CDX_FS_LINK;
    return CDX_FS_OTHER;
}

static cdx_fs_status interrupted(cdx_cancel_handle cancellation, uint64_t deadline) {
    if (cancellation && atomic_load(cancellation)) return CDX_FS_CANCELLED;
    if (milliseconds() >= deadline) return CDX_FS_TIMED_OUT;
    return CDX_FS_OK;
}

static cdx_fs_status validate_request(const cdx_fs_request *request) {
    if (!request ||
        (request->list_directory != 0 && request->list_directory != 1) ||
        (request->include_hidden != 0 && request->include_hidden != 1) ||
        (request->list_directory && (!request->max_entries || request->max_entries > CDX_FS_MAX_ENTRIES))) return CDX_FS_VALIDATION_ERROR;
    return CDX_FS_OK;
}

static cdx_fs_status validate_path(const char *path) {
    size_t length;
    if (!path || path[0] != '/') return CDX_FS_VALIDATION_ERROR;
    length = strnlen(path, PATH_MAX);
    if (length == PATH_MAX || strchr(path, '\r') || strchr(path, '\n') ||
        strstr(path, "$(") || strstr(path, "${") || strchr(path, '`'))
        return CDX_FS_VALIDATION_ERROR;
    return cdx_validate_utf8(path, length) ? CDX_FS_OK : CDX_FS_ENCODING_ERROR;
}

cdx_fs_status cdx_fs_normalize_path(const char *path, cdx_cancel_handle cancellation,
                                    char **result, unsigned long *native_error) {
    size_t input = 1, output = 1, length, step;
    uint64_t deadline = milliseconds() + CDX_FS_DEADLINE_MS;
    char *normalized = NULL;
    cdx_fs_status status = validate_path(path);
    if (result) *result = NULL;
    if (native_error) *native_error = 0;
    if (!result) status = CDX_FS_VALIDATION_ERROR;
    if (status != CDX_FS_OK) { errno = status == CDX_FS_ENCODING_ERROR ? EILSEQ : EINVAL; goto done; }
    status = interrupted(cancellation, deadline);
    if (status != CDX_FS_OK) goto done;
    length = strlen(path);
    normalized = (char *)malloc(length + 1u);
    if (!normalized) { errno = ENOMEM; status = CDX_FS_IO_ERROR; goto done; }
    normalized[0] = '/';
    for (step = 0; input < length && step < PATH_MAX; ++step) {
        size_t start, part_length;
        status = interrupted(cancellation, deadline);
        if (status != CDX_FS_OK) break;
        if (path[input] == '/') { ++input; continue; }
        start = input;
        while (input < length && path[input] != '/') ++input;
        part_length = input - start;
        if (part_length == 1 && path[start] == '.') continue;
        if (part_length == 2 && path[start] == '.' && path[start + 1] == '.') {
            if (output > 1) {
                while (output > 1 && normalized[output - 1] != '/') --output;
                if (output > 1) --output;
            }
            continue;
        }
        if (output > 1) normalized[output++] = '/';
        memcpy(normalized + output, path + start, part_length);
        output += part_length;
    }
    if (status != CDX_FS_OK) goto done;
    status = interrupted(cancellation, deadline);
    if (status != CDX_FS_OK) goto done;
    normalized[output] = 0;
    *result = normalized;
    normalized = NULL;
done:
    if (status == CDX_FS_CANCELLED) errno = ECANCELED;
    else if (status == CDX_FS_TIMED_OUT) errno = ETIMEDOUT;
    if (status != CDX_FS_OK && native_error) *native_error = (unsigned long)errno;
    free(normalized);
    return status;
}

int cdx_fs_path_within(const char *path, const char *root) {
    size_t path_length = strlen(path), root_length = strlen(root);
    while (root_length > 1 && root[root_length - 1] == '/') --root_length;
    if (path_length < root_length || memcmp(path, root, root_length)) return 0;
    return root_length == 1 || path_length == root_length || path[root_length] == '/';
}

static cdx_fs_status list_directory(const cdx_fs_request *request, cdx_fs_result *result, uint64_t deadline) {
    DIR *directory = opendir(result->path);
    cdx_fs_status status = CDX_FS_OK;
    size_t step;
    int ended = 0;
    if (!directory) { result->native_error = (unsigned long)errno; return error_status(errno); }
    result->entries = (cdx_fs_entry *)calloc(request->max_entries, sizeof(*result->entries));
    if (!result->entries) { result->native_error = ENOMEM; closedir(directory); return CDX_FS_IO_ERROR; }
    for (step = 0; step < CDX_FS_MAX_STEPS; ++step) {
        struct dirent *item;
        struct stat metadata;
        size_t name_length;
        int hidden;
        cdx_fs_entry *entry;
        status = interrupted(request->cancellation, deadline);
        if (status != CDX_FS_OK) break;
        errno = 0;
        item = readdir(directory);
        if (!item) {
            if (errno) { result->native_error = (unsigned long)errno; status = error_status(errno); }
            else ended = 1;
            break;
        }
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
        hidden = item->d_name[0] == '.';
        if (hidden && !request->include_hidden) continue;
        if (result->entry_count == request->max_entries) { result->truncated = 1; ended = 1; break; }
        name_length = strlen(item->d_name);
        if (!cdx_validate_utf8(item->d_name, name_length)) { status = CDX_FS_ENCODING_ERROR; break; }
        if (fstatat(dirfd(directory), item->d_name, &metadata, AT_SYMLINK_NOFOLLOW)) {
            result->native_error = (unsigned long)errno; status = error_status(errno); break;
        }
        entry = &result->entries[result->entry_count];
        entry->name = (char *)malloc(name_length + 1u);
        if (!entry->name) { result->native_error = ENOMEM; status = CDX_FS_IO_ERROR; break; }
        memcpy(entry->name, item->d_name, name_length + 1u);
        entry->kind = entry_kind(metadata.st_mode);
        entry->hidden = hidden;
        if (S_ISREG(metadata.st_mode) && metadata.st_size >= 0) {
            entry->size_known = 1; entry->size_bytes = (uint64_t)metadata.st_size;
        }
        ++result->entry_count;
    }
    if (status == CDX_FS_OK && !ended) status = CDX_FS_TIMED_OUT;
    if (status == CDX_FS_OK) status = interrupted(request->cancellation, deadline);
    if (closedir(directory) && status == CDX_FS_OK) {
        result->native_error = (unsigned long)errno; status = error_status(errno);
    }
    return status;
}

cdx_fs_status cdx_fs_query(const cdx_fs_request *request, cdx_fs_result *result) {
    struct stat metadata;
    uint64_t deadline;
    cdx_fs_status status;
    if (!result) return CDX_FS_VALIDATION_ERROR;
    memset(result, 0, sizeof(*result));
    status = validate_request(request);
    if (status != CDX_FS_OK) return result->status = status;
    deadline = milliseconds() + CDX_FS_DEADLINE_MS;
    status = cdx_fs_normalize_path(request->path, request->cancellation, &result->path, &result->native_error);
    if (status != CDX_FS_OK) return result->status = status;
    if (lstat(result->path, &metadata)) {
        result->native_error = (unsigned long)errno;
        return result->status = error_status(errno);
    }
    result->kind = entry_kind(metadata.st_mode);
    status = interrupted(request->cancellation, deadline);
    if (status == CDX_FS_OK && request->list_directory) status = list_directory(request, result, deadline);
    return result->status = status;
}
