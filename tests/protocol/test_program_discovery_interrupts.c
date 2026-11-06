#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "cdx/operations.h"
#include "cdx/filesystem.h"
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <time.h>
#endif

static int probe_mode;
static cdx_fs_status probe_normalize(const cdx_path_char *path, cdx_cancel_handle cancellation,
                                     cdx_path_char **normalized, unsigned long *error);

/* Inject only the boundary outcome; all parsing, scan and filesystem code stays real. */
#define cdx_fs_normalize_path probe_normalize
#ifndef CDX_DISCOVERY_IMPLEMENTATION
#define CDX_DISCOVERY_IMPLEMENTATION "../../src/cdx-protocol/program_discovery.c"
#endif
#include CDX_DISCOVERY_IMPLEMENTATION
#undef cdx_fs_normalize_path

static cdx_fs_status probe_normalize(const cdx_path_char *path, cdx_cancel_handle cancellation,
                                     cdx_path_char **normalized, unsigned long *error) {
    cdx_fs_status status;
    *normalized = NULL;
    *error = 0;
    if (probe_mode == 1) return CDX_FS_IO_ERROR;
    if (probe_mode == 2) return CDX_FS_TIMED_OUT;
    status = cdx_fs_normalize_path(path, cancellation, normalized, error);
    if (status == CDX_FS_OK && probe_mode == 3) {
#ifdef _WIN32
        SetEvent(cancellation);
#else
        atomic_store(cancellation, 1);
#endif
    }
    if (status == CDX_FS_OK && probe_mode == 4) {
#ifdef _WIN32
        Sleep(150u);
#else
        const struct timespec delay = {0, 150000000};
        (void)nanosleep(&delay, NULL);
#endif
    }
    return status;
}

int main(void) {
    const char *statuses[] = {"io_error", "timed_out", "cancelled", "timed_out"};
    const char *request =
#ifdef _WIN32
        "{\"name\":\"missing\",\"candidates\":[\"C:\\\\cdx-missing-final-probe\\\\missing.exe\"],\"search_path\":false,\"timeout_ms\":100}";
#else
        "{\"name\":\"missing\",\"candidates\":[\"/cdx-missing-final-probe/missing\"],\"search_path\":false,\"timeout_ms\":100}";
#endif
    unsigned failed = 0;
    for (probe_mode = 1; probe_mode <= 4; ++probe_mode) {
        cdx_json input;
        cdx_json_output output;
        char expected[64];
#ifdef _WIN32
        cdx_cancel_handle cancellation = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!cancellation) return 2;
#else
        atomic_int flag = ATOMIC_VAR_INIT(0);
        cdx_cancel_handle cancellation = &flag;
#endif
        if (!cdx_json_parse(&input, request, strlen(request))) return 2;
        cdx_json_output_init(&output);
        cdx_program_discovery_call(&input, 0, cancellation, &output);
        (void)snprintf(expected, sizeof(expected), "\"status\":\"%s\"", statuses[probe_mode - 1]);
        if (!output.data || !strstr(output.data, expected) ||
            !strstr(output.data, "\"scan_complete\":false") || !strstr(output.data, "\"checked\":1")) {
            ++failed;
            (void)fprintf(stderr, "FAIL probe %d: %s\n", probe_mode, output.data ? output.data : "no output");
        }
        cdx_json_output_free(&output);
        cdx_json_free(&input);
#ifdef _WIN32
        CloseHandle(cancellation);
#endif
    }
    (void)printf("program discovery interruption checks: 4, failures: %u\n", failed);
    return failed ? 1 : 0;
}
