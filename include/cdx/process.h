#ifndef CDX_PROCESS_H
#define CDX_PROCESS_H

#include <stddef.h>
#include <stdint.h>
#include "cdx/platform.h"

#ifdef _WIN32
#define CDX_NATIVE_ERROR_FIELD DWORD win32_error
#define CDX_PROCESS_CODE_TYPE DWORD
#define CDX_PROCESS_ID_TYPE DWORD
#define CDX_PROCESS_COUNT_TYPE DWORD
#else
#define CDX_NATIVE_ERROR_FIELD int posix_error
#define CDX_PROCESS_CODE_TYPE uint32_t
#define CDX_PROCESS_ID_TYPE uint32_t
#define CDX_PROCESS_COUNT_TYPE uint32_t
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CDX_MAX_ARGS 512u
#define CDX_MAX_OUTPUT_BYTES (16u * 1024u * 1024u)
#define CDX_MAX_TIMEOUT_MS (30u * 60u * 1000u)

typedef enum cdx_status {
    CDX_COMPLETED = 0,
    CDX_FAILED,
    CDX_TIMED_OUT,
    CDX_CANCELLED,
    CDX_OUTPUT_LIMIT,
    CDX_VALIDATION_ERROR,
    CDX_SPAWN_ERROR,
    CDX_EXECUTION_ERROR
} cdx_status;

typedef struct cdx_exec_request {
    const cdx_path_char *program;
    const cdx_path_char *cwd;
    const cdx_path_char *const *args;
    size_t arg_count;
    uint32_t timeout_ms;
    const int *expected_exit_codes;
    size_t expected_exit_count;
    const unsigned char *stdin_utf8;
    size_t stdin_size;
    size_t output_limit_bytes;
    cdx_cancel_handle cancel_event;
} cdx_exec_request;

typedef struct cdx_byte_buffer {
    unsigned char *data;
    size_t size;
    size_t observed;
} cdx_byte_buffer;

typedef struct cdx_exec_result {
    cdx_status status;
    CDX_PROCESS_CODE_TYPE exit_code;
    int exit_code_known;
    CDX_PROCESS_ID_TYPE pid;
    CDX_PROCESS_ID_TYPE parent_pid;
    uint64_t creation_filetime;
    uint64_t elapsed_ms;
    CDX_PROCESS_COUNT_TYPE job_processes_total;
    CDX_PROCESS_COUNT_TYPE job_active_before_cleanup;
    CDX_PROCESS_COUNT_TYPE job_active_after_cleanup;
    int cleanup_complete;
    CDX_NATIVE_ERROR_FIELD;
    int output_truncated;
    cdx_byte_buffer stdout_bytes;
    cdx_byte_buffer stderr_bytes;
} cdx_exec_result;

void cdx_result_init(cdx_exec_result *result);
void cdx_result_free(cdx_exec_result *result);
const char *cdx_status_name(cdx_status status);
cdx_status cdx_execute(const cdx_exec_request *request, cdx_exec_result *result);

#ifdef __cplusplus
}
#endif

#undef CDX_NATIVE_ERROR_FIELD
#undef CDX_PROCESS_CODE_TYPE
#undef CDX_PROCESS_ID_TYPE
#undef CDX_PROCESS_COUNT_TYPE

#endif
