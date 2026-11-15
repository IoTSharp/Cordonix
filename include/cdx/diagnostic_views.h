#ifndef CDX_DIAGNOSTIC_VIEWS_H
#define CDX_DIAGNOSTIC_VIEWS_H

#include <stddef.h>

#define CDX_DIAGNOSTIC_MAX_RECORDS 64u
#define CDX_DIAGNOSTIC_MAX_INPUT_BYTES (1024u * 1024u)
#define CDX_DIAGNOSTIC_PATH_BYTES 512u
#define CDX_DIAGNOSTIC_CODE_BYTES 64u
#define CDX_DIAGNOSTIC_MESSAGE_BYTES 256u

typedef enum cdx_diagnostic_status {
    CDX_DIAGNOSTIC_OK = 0,
    CDX_DIAGNOSTIC_PARTIAL,
    CDX_DIAGNOSTIC_OUTPUT_LIMIT,
    CDX_DIAGNOSTIC_INVALID_INPUT,
    CDX_DIAGNOSTIC_UNKNOWN
} cdx_diagnostic_status;

typedef enum cdx_diagnostic_kind {
    CDX_DIAGNOSTIC_SEARCH_MATCH = 0,
    CDX_DIAGNOSTIC_BUILD_ERROR,
    CDX_DIAGNOSTIC_TEST_FAILURE,
    CDX_DIAGNOSTIC_UNPARSED
} cdx_diagnostic_kind;

typedef struct cdx_diagnostic_record {
    cdx_diagnostic_kind kind;
    char path[CDX_DIAGNOSTIC_PATH_BYTES];
    unsigned line;
    unsigned column;
    char code[CDX_DIAGNOSTIC_CODE_BYTES];
    char message[CDX_DIAGNOSTIC_MESSAGE_BYTES];
    size_t source_offset;
    int parsed;
} cdx_diagnostic_record;

typedef struct cdx_diagnostic_result {
    cdx_diagnostic_status status;
    cdx_diagnostic_record records[CDX_DIAGNOSTIC_MAX_RECORDS];
    size_t count;
    size_t unparsed_lines;
    size_t source_bytes;
    const char *raw_log;
    int raw_log_available;
    int truncated;
} cdx_diagnostic_result;

/* Parses a bounded borrowed log; raw_log remains owned by the caller. */
int cdx_diagnostic_parse(const char *raw_log, size_t raw_length,
                         size_t max_records, cdx_diagnostic_result *result);
const char *cdx_diagnostic_status_name(cdx_diagnostic_status status);
const char *cdx_diagnostic_kind_name(cdx_diagnostic_kind kind);

#endif
