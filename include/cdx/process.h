#ifndef CDX_PROCESS_H
#define CDX_PROCESS_H

#include <stddef.h>
#include <stdint.h>
#include <windows.h>

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
    const wchar_t *program;
    const wchar_t *cwd;
    const wchar_t *const *args;
    size_t arg_count;
    uint32_t timeout_ms;
    const int *expected_exit_codes;
    size_t expected_exit_count;
    const unsigned char *stdin_utf8;
    size_t stdin_size;
    size_t output_limit_bytes;
    HANDLE cancel_event;
} cdx_exec_request;

typedef struct cdx_byte_buffer {
    unsigned char *data;
    size_t size;
    size_t observed;
} cdx_byte_buffer;

typedef struct cdx_exec_result {
    cdx_status status;
    DWORD exit_code;
    int exit_code_known;
    DWORD pid;
    DWORD parent_pid;
    uint64_t creation_filetime;
    uint64_t elapsed_ms;
    DWORD job_processes_total;
    DWORD job_active_before_cleanup;
    DWORD job_active_after_cleanup;
    int cleanup_complete;
    DWORD win32_error;
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

#endif
