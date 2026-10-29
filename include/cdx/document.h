#ifndef CDX_DOCUMENT_H
#define CDX_DOCUMENT_H

#include "cdx/platform.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CDX_DOCUMENT_MAX_BYTES (8u * 1024u * 1024u)
#define CDX_DOCUMENT_MAX_UTF8_BYTES (3u * CDX_DOCUMENT_MAX_BYTES)
#define CDX_SHA256_HEX_LENGTH 64

typedef enum cdx_encoding {
    CDX_ENCODING_AUTO = 0,
    CDX_ENCODING_UTF8,
    CDX_ENCODING_UTF8_BOM,
    CDX_ENCODING_UTF16LE,
    CDX_ENCODING_UTF16BE,
    CDX_ENCODING_CP936
} cdx_encoding;

typedef enum cdx_eol {
    CDX_EOL_NONE = 0,
    CDX_EOL_LF,
    CDX_EOL_CRLF,
    CDX_EOL_CR,
    CDX_EOL_MIXED
} cdx_eol;

typedef enum cdx_document_status {
    CDX_STATUS_OK = 0,
    CDX_STATUS_PREVIEW,
    CDX_STATUS_VALIDATION_ERROR,
    CDX_STATUS_NOT_FOUND,
    CDX_STATUS_ENCODING_ERROR,
    CDX_STATUS_FILE_CHANGED,
    CDX_STATUS_ANCHOR_MISMATCH,
    CDX_STATUS_AMBIGUOUS_MATCH,
    CDX_STATUS_MIXED_EOL,
    CDX_STATUS_TOO_LARGE,
    CDX_STATUS_IO_ERROR,
    CDX_STATUS_COMMIT_ERROR
} cdx_document_status;

typedef struct cdx_file_version {
#ifdef _WIN32
    DWORD volume_serial;
    DWORD file_index_high;
    DWORD file_index_low;
    LARGE_INTEGER size;
    FILETIME last_write_time;
#else
    uint64_t device;
    uint64_t inode;
    uint64_t size;
    int64_t modified_seconds;
    long modified_nanoseconds;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
#endif
} cdx_file_version;

typedef struct cdx_document {
    cdx_path_char *path;
    unsigned char *original_bytes;
    size_t original_size;
    unsigned char *current_bytes;
    size_t current_size;
    char *text_utf8;
    size_t text_length;
    cdx_encoding encoding;
    cdx_eol eol;
    cdx_file_version original_version;
    char original_sha256[CDX_SHA256_HEX_LENGTH + 1];
    char current_sha256[CDX_SHA256_HEX_LENGTH + 1];
    int has_bom;
    int dirty_in_memory;
} cdx_document;

typedef struct cdx_edit_result {
    cdx_document_status status;
    size_t expected_count;
    size_t actual_count;
    size_t bytes_before;
    size_t bytes_after;
    int written_to_disk;
    int dirty_in_memory;
} cdx_edit_result;

typedef struct cdx_commit_result {
    cdx_document_status status;
    int written_to_disk;
    int cleanup_complete;
    char disk_sha256[CDX_SHA256_HEX_LENGTH + 1];
} cdx_commit_result;

const char *cdx_document_status_name(cdx_document_status status);
const char *cdx_encoding_name(cdx_encoding encoding);
const char *cdx_eol_name(cdx_eol eol);

cdx_document_status cdx_document_open(cdx_document *document, const cdx_path_char *path, cdx_encoding requested_encoding);
cdx_document_status cdx_document_edit_utf8(cdx_document *document, const char *expected_sha256,
                                  const char *old_text, const char *new_text,
                                  size_t expected_count, int dry_run, cdx_edit_result *result);
cdx_document_status cdx_document_convert_encoding(cdx_document *document, const char *expected_sha256,
                                                  cdx_encoding target_encoding, int dry_run,
                                                  cdx_edit_result *result);
cdx_document_status cdx_document_commit(cdx_document *document, cdx_commit_result *result);
void cdx_document_free(cdx_document *document);

#ifdef __cplusplus
}
#endif

#endif
