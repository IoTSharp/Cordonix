#define _POSIX_C_SOURCE 200809L
#include "cdx/document.h"
#include "document_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static uint64_t milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

int cdx_read_file(const char *path, unsigned char **bytes, size_t *length, cdx_file_version *version) {
    int file = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    struct stat metadata;
    unsigned char *buffer = NULL;
    size_t size = 0, capacity, iteration;
    uint64_t deadline = milliseconds() + 15000u;
    int error = 0;
    if (file < 0) return 0;
    if (fstat(file, &metadata)) goto failed;
    if (!S_ISREG(metadata.st_mode) || metadata.st_nlink != 1) { errno = EINVAL; goto failed; }
    if (metadata.st_size < 0 || (uint64_t)metadata.st_size > CDX_DOCUMENT_MAX_BYTES) { errno = EFBIG; goto failed; }
    capacity = (size_t)metadata.st_size + 1u;
    buffer = (unsigned char *)malloc(capacity);
    if (!buffer) goto failed;
    for (iteration = 0; iteration <= CDX_DOCUMENT_MAX_BYTES / 65536u + 1u && milliseconds() < deadline; ++iteration) {
        size_t requested = capacity - size < 65536u ? capacity - size : 65536u;
        ssize_t received = read(file, buffer + size, requested);
        if (received < 0) goto failed;
        if (!received) break;
        size += (size_t)received;
        if (size >= capacity) { errno = EFBIG; goto failed; }
    }
    if (size != (size_t)metadata.st_size || milliseconds() >= deadline) { errno = EIO; goto failed; }
    if (close(file)) { free(buffer); return 0; }
    version->device = (uint64_t)metadata.st_dev;
    version->inode = (uint64_t)metadata.st_ino;
    version->size = (uint64_t)metadata.st_size;
    version->modified_seconds = metadata.st_mtim.tv_sec;
    version->modified_nanoseconds = metadata.st_mtim.tv_nsec;
    version->mode = (uint32_t)metadata.st_mode;
    version->uid = (uint32_t)metadata.st_uid;
    version->gid = (uint32_t)metadata.st_gid;
    *bytes = buffer; *length = size;
    return 1;
failed:
    error = errno;
    close(file); free(buffer); errno = error;
    return 0;
}

int cdx_same_version(const cdx_file_version *left, const cdx_file_version *right) {
    return left->device == right->device && left->inode == right->inode && left->size == right->size &&
        left->modified_seconds == right->modified_seconds && left->modified_nanoseconds == right->modified_nanoseconds &&
        left->mode == right->mode && left->uid == right->uid && left->gid == right->gid;
}

cdx_document_status cdx_document_open(cdx_document *document, const char *path, cdx_encoding requested_encoding) {
    size_t path_length, offset = 0;
    cdx_encoding encoding;
    unsigned char *roundtrip = NULL;
    size_t roundtrip_size = 0;
    if (!document) return CDX_STATUS_VALIDATION_ERROR;
    memset(document, 0, sizeof(*document));
    if (!path || path[0] != '/' || requested_encoding < CDX_ENCODING_AUTO || requested_encoding > CDX_ENCODING_CP936)
        return CDX_STATUS_VALIDATION_ERROR;
    path_length = strnlen(path, PATH_MAX);
    if (path_length >= PATH_MAX - 40u || strchr(path, '\n') || strchr(path, '\r') ||
        strstr(path, "$(") || strchr(path, '`') || !cdx_validate_utf8(path, path_length)) return CDX_STATUS_VALIDATION_ERROR;
    document->path = strdup(path);
    if (!document->path) return CDX_STATUS_IO_ERROR;
    if (!cdx_read_file(path, &document->original_bytes, &document->original_size, &document->original_version)) {
        int error = errno;
        cdx_document_free(document);
        return error == EFBIG ? CDX_STATUS_TOO_LARGE :
            (error == ENOENT || error == ENOTDIR) ? CDX_STATUS_NOT_FOUND : CDX_STATUS_IO_ERROR;
    }
    if (!cdx_sha256(document->original_bytes, document->original_size, document->original_sha256)) {
        cdx_document_free(document); return CDX_STATUS_IO_ERROR;
    }
    encoding = cdx_detect_encoding(document->original_bytes, document->original_size, requested_encoding, &offset);
    if (encoding == CDX_ENCODING_AUTO) encoding = CDX_ENCODING_UTF8;
    if (!cdx_decode_to_utf8(document->original_bytes + offset, document->original_size - offset,
                            encoding, &document->text_utf8, &document->text_length) ||
        memchr(document->text_utf8, 0, document->text_length)) {
        cdx_document_free(document); return CDX_STATUS_ENCODING_ERROR;
    }
    if (!cdx_encode_text(document->text_utf8, document->text_length, encoding, &roundtrip, &roundtrip_size) ||
        roundtrip_size != document->original_size || memcmp(roundtrip, document->original_bytes, roundtrip_size)) {
        free(roundtrip); cdx_document_free(document); return CDX_STATUS_ENCODING_ERROR;
    }
    free(roundtrip);
    document->encoding = encoding; document->has_bom = offset != 0;
    document->eol = cdx_detect_eol(document->text_utf8, document->text_length);
    document->current_bytes = (unsigned char *)malloc(document->original_size + 1u);
    if (!document->current_bytes) { cdx_document_free(document); return CDX_STATUS_IO_ERROR; }
    memcpy(document->current_bytes, document->original_bytes, document->original_size);
    document->current_size = document->original_size;
    memcpy(document->current_sha256, document->original_sha256, sizeof(document->current_sha256));
    return CDX_STATUS_OK;
}

static int write_temporary(int file, const cdx_document *document) {
    size_t offset = 0, iteration;
    struct stat metadata;
    uint64_t deadline = milliseconds() + 15000u;
    for (iteration = 0; offset < document->current_size && iteration <= CDX_DOCUMENT_MAX_BYTES && milliseconds() < deadline; ++iteration) {
        size_t remaining = document->current_size - offset;
        ssize_t written = write(file, document->current_bytes + offset, remaining < 65536u ? remaining : 65536u);
        if (written <= 0) return 0;
        offset += (size_t)written;
    }
    if (offset != document->current_size || milliseconds() >= deadline || fstat(file, &metadata)) return 0;
    if (((uint32_t)metadata.st_uid != document->original_version.uid || (uint32_t)metadata.st_gid != document->original_version.gid) &&
        fchown(file, (uid_t)document->original_version.uid, (gid_t)document->original_version.gid)) return 0;
    return fchmod(file, (mode_t)(document->original_version.mode & 07777u)) == 0 && fsync(file) == 0;
}

cdx_document_status cdx_document_commit(cdx_document *document, cdx_commit_result *result) {
    unsigned char *disk_bytes = NULL, *committed_bytes = NULL;
    size_t disk_size = 0;
    cdx_file_version disk_version;
    char disk_hash[CDX_SHA256_HEX_LENGTH + 1], temp_path[PATH_MAX], directory[PATH_MAX];
    char *separator;
    int temporary = -1, dir = -1, created = 0;
    cdx_document_status status = CDX_STATUS_IO_ERROR;
    if (result) memset(result, 0, sizeof(*result));
    if (!document || !document->path) {
        if (result) result->status = CDX_STATUS_VALIDATION_ERROR;
        return CDX_STATUS_VALIDATION_ERROR;
    }
    if (!document->dirty_in_memory) {
        if (result) { result->status = CDX_STATUS_OK; result->cleanup_complete = 1; memcpy(result->disk_sha256, document->original_sha256, sizeof(result->disk_sha256)); }
        return CDX_STATUS_OK;
    }
    if (!cdx_read_file(document->path, &disk_bytes, &disk_size, &disk_version) || !cdx_sha256(disk_bytes, disk_size, disk_hash)) goto done;
    if (!cdx_same_version(&document->original_version, &disk_version) || strcmp(document->original_sha256, disk_hash)) { status = CDX_STATUS_FILE_CHANGED; goto done; }
    cdx_free_bytes(&disk_bytes);
    committed_bytes = (unsigned char *)malloc(document->current_size + 1u);
    if (!committed_bytes) goto done;
    memcpy(committed_bytes, document->current_bytes, document->current_size);
    memcpy(directory, document->path, strlen(document->path) + 1u);
    separator = strrchr(directory, '/');
    if (separator == directory) separator[1] = 0; else *separator = 0;
    dir = open(directory, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if (dir < 0) goto done;
    if (snprintf(temp_path, sizeof(temp_path), "%s.cdx.tmp.XXXXXX", document->path) >= (int)sizeof(temp_path)) goto done;
    status = CDX_STATUS_COMMIT_ERROR;
    temporary = mkstemp(temp_path);
    if (temporary < 0) goto done;
    created = 1;
    if (!write_temporary(temporary, document)) goto done;
    if (close(temporary)) { temporary = -1; goto done; }
    temporary = -1;
    if (!cdx_read_file(document->path, &disk_bytes, &disk_size, &disk_version) || !cdx_sha256(disk_bytes, disk_size, disk_hash)) goto done;
    if (!cdx_same_version(&document->original_version, &disk_version) || strcmp(document->original_sha256, disk_hash)) { status = CDX_STATUS_FILE_CHANGED; goto done; }
    cdx_free_bytes(&disk_bytes);
    /* The final compare/rename interval is not a filesystem compare-and-swap. */
    if (rename(temp_path, document->path)) goto done;
    created = 0;
    if (result) result->written_to_disk = 1;
    if (fsync(dir) || !cdx_read_file(document->path, &disk_bytes, &disk_size, &disk_version) ||
        !cdx_sha256(disk_bytes, disk_size, disk_hash) || strcmp(disk_hash, document->current_sha256)) goto done;
    cdx_free_bytes(&document->original_bytes);
    document->original_bytes = committed_bytes; committed_bytes = NULL;
    document->original_size = document->current_size;
    document->original_version = disk_version;
    memcpy(document->original_sha256, document->current_sha256, sizeof(document->original_sha256));
    document->dirty_in_memory = 0;
    status = CDX_STATUS_OK;
done:
    if (temporary >= 0) close(temporary);
    if (dir >= 0) close(dir);
    if (result) result->cleanup_complete = !created || unlink(temp_path) == 0;
    else if (created) (void)unlink(temp_path);
    cdx_free_bytes(&disk_bytes); free(committed_bytes);
    if (result) {
        result->status = status;
        if (status == CDX_STATUS_OK) memcpy(result->disk_sha256, document->current_sha256, sizeof(result->disk_sha256));
    }
    return status;
}
