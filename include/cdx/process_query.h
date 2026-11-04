#ifndef CDX_PROCESS_QUERY_H
#define CDX_PROCESS_QUERY_H

#include <stddef.h>
#include <stdint.h>
#include "cdx/platform.h"

#define CDX_QUERY_MAX_TERMS 16u
#define CDX_QUERY_MAX_PIDS 32u
#define CDX_QUERY_MAX_RESULTS 100u

typedef struct cdx_process_query_request {
    const char *terms[CDX_QUERY_MAX_TERMS];
    size_t term_count;
    uint32_t pids[CDX_QUERY_MAX_PIDS];
    size_t pid_count;
    size_t limit, max_processes, output_limit_bytes;
    uint32_t timeout_ms;
    int include_self;
    cdx_cancel_handle cancellation;
} cdx_process_query_request;

typedef struct cdx_process_entry {
    uint32_t pid, parent_pid;
    uint64_t creation_filetime;
    char created_utc[40];
    char *command_line;
} cdx_process_entry;

typedef struct cdx_process_query_result {
    const char *status, *stop_reason;
    cdx_process_entry *entries;
    char *command_lines; /* Entries reference this single result-owned allocation. */
    size_t count, examined, access_denied, disappeared, unavailable, encoding_errors;
    uint32_t excluded_self_pid;
    uint64_t elapsed_ms;
    unsigned long native_error;
    int scan_complete;
} cdx_process_query_result;

void cdx_query_processes(const cdx_process_query_request *request, cdx_process_query_result *result);
void cdx_process_query_free(cdx_process_query_result *result);

#endif
