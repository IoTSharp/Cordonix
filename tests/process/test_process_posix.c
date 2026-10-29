#define _POSIX_C_SOURCE 200809L

#include "cdx/process.h"

#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static unsigned int passed, total;
static char fixture[4096];
static char cwd[4096];

static void *cancel_later(void *value) {
    const struct timespec pause_for = {0, 100000000L};
    (void)nanosleep(&pause_for, NULL);
    atomic_store_explicit((atomic_int *)value, 1, memory_order_release);
    return NULL;
}

static cdx_exec_request request_for(const char *const *args, size_t count) {
    cdx_exec_request request;
    memset(&request, 0, sizeof(request));
    request.program = fixture;
    request.cwd = cwd;
    request.args = args;
    request.arg_count = count;
    request.timeout_ms = 3000u;
    request.output_limit_bytes = 1024u * 1024u;
    return request;
}

static void record(const char *name, int success) {
    ++total;
    if (success) ++passed;
    printf("%s: %s\n", name, success ? "PASS" : "FAIL");
}

static void test_argv(void) {
    const char *args[] = {"echo", "one", "two words", "", "中文", "a\"b", "$(literal)", "line1\nline2"};
    cdx_exec_request request = request_for(args, sizeof(args) / sizeof(args[0]));
    cdx_exec_result result;
    const char expected[] = "onetwo words中文a\"b$(literal)line1\nline2";
    int ok = cdx_execute(&request, &result) == CDX_COMPLETED && result.cleanup_complete &&
        result.stdout_bytes.size == sizeof(expected) - 1u && memcmp(result.stdout_bytes.data, expected, sizeof(expected) - 1u) == 0;
    record("POSIX literal argv", ok);
    cdx_result_free(&result);
}

static void test_statuses(void) {
    const char *args[] = {"exit23"};
    int expected[] = {0, 23};
    cdx_exec_request request = request_for(args, 1);
    cdx_exec_result result;
    record("POSIX unexpected exit", cdx_execute(&request, &result) == CDX_FAILED && result.exit_code_known && result.exit_code == 23u);
    cdx_result_free(&result);
    request.expected_exit_codes = expected; request.expected_exit_count = 2;
    record("POSIX expected exit", cdx_execute(&request, &result) == CDX_COMPLETED && result.exit_code == 23u);
    cdx_result_free(&result);
}

static void test_stdin_and_limit(void) {
    const char *args[] = {"stdin"};
    unsigned char input[300000];
    cdx_exec_request request = request_for(args, 1);
    cdx_exec_result result;
    size_t index;
    for (index = 0; index < sizeof(input); ++index) input[index] = (unsigned char)('a' + index % 26u);
    request.stdin_utf8 = input; request.stdin_size = sizeof(input);
    record("POSIX bounded stdin", cdx_execute(&request, &result) == CDX_COMPLETED && result.stdout_bytes.size == sizeof(input) && memcmp(input, result.stdout_bytes.data, sizeof(input)) == 0);
    cdx_result_free(&result);
    args[0] = "output"; request.stdin_utf8 = NULL; request.stdin_size = 0; request.output_limit_bytes = 8192u;
    record("POSIX output limit", cdx_execute(&request, &result) == CDX_OUTPUT_LIMIT && result.output_truncated && result.stdout_bytes.size + result.stderr_bytes.size == 8192u && result.cleanup_complete);
    cdx_result_free(&result);
}

static void test_cancel_timeout(void) {
    const char *args[] = {"sleep"};
    cdx_exec_request request = request_for(args, 1);
    cdx_exec_result result;
    atomic_int cancelled = 1;
    request.cancel_event = &cancelled;
    record("POSIX pre-cancel", cdx_execute(&request, &result) == CDX_CANCELLED && result.pid == 0u);
    cdx_result_free(&result);
    request.cancel_event = NULL; request.timeout_ms = 100u;
    record("POSIX timeout", cdx_execute(&request, &result) == CDX_TIMED_OUT && result.cleanup_complete);
    cdx_result_free(&result);
    {
        pthread_t thread;
        atomic_store(&cancelled, 0); request.cancel_event = &cancelled; request.timeout_ms = 3000;
        if (pthread_create(&thread, NULL, cancel_later, &cancelled) != 0) { record("POSIX asynchronous cancel", 0); return; }
        record("POSIX asynchronous cancel", cdx_execute(&request, &result) == CDX_CANCELLED && result.pid != 0u && result.cleanup_complete);
        (void)pthread_join(thread, NULL); cdx_result_free(&result);
    }
}

static void test_validation_and_group(void) {
    const char *args[] = {"echo"};
    cdx_exec_request request = request_for(args, 1);
    cdx_exec_result result;
    request.cwd = "relative";
    record("POSIX invalid cwd", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0u);
    cdx_result_free(&result);
    request.cwd = cwd; request.program = "/bin/sh";
    record("POSIX shell rejection", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0u);
    cdx_result_free(&result);
    request.program = fixture; args[0] = "tree"; request.timeout_ms = 3000u;
    record("POSIX parent-exit group cleanup", cdx_execute(&request, &result) == CDX_COMPLETED && result.job_processes_total >= 2u && result.cleanup_complete && result.job_active_after_cleanup == 0u);
    if (!result.cleanup_complete) fprintf(stderr, "tree status=%s total=%u before=%u after=%u cleanup=%d error=%d\n", cdx_status_name(result.status), result.job_processes_total, result.job_active_before_cleanup, result.job_active_after_cleanup, result.cleanup_complete, result.posix_error);
    cdx_result_free(&result);
    args[0] = "tree-wait"; request.timeout_ms = 100;
    record("POSIX timeout group cleanup", cdx_execute(&request, &result) == CDX_TIMED_OUT && result.job_processes_total >= 2u && result.cleanup_complete && result.job_active_after_cleanup == 0u);
    cdx_result_free(&result);
}

static void test_review_regressions(void) {
    const char *args[] = {"cwd"};
    unsigned char input[300000];
    cdx_exec_request request = request_for(args, 1);
    cdx_exec_result result;
    clock_t cpu_started;
    request.cwd = "/";
    record("POSIX root cwd", cdx_execute(&request, &result) == CDX_COMPLETED && result.stdout_bytes.size == 1u && result.stdout_bytes.data[0] == '/');
    cdx_result_free(&result);
    request.cwd = cwd; request.timeout_ms = 200u; args[0] = "close-streams";
    cpu_started = clock();
    record("POSIX no descriptors still bounded", cdx_execute(&request, &result) == CDX_TIMED_OUT && result.cleanup_complete && result.elapsed_ms >= 150u && (double)(clock() - cpu_started) / CLOCKS_PER_SEC < 0.1);
    cdx_result_free(&result);
    memset(input, 'x', sizeof(input));
    request.timeout_ms = 3000u; args[0] = "close-stdin"; request.stdin_utf8 = input; request.stdin_size = sizeof(input);
    record("POSIX closed stdin preserves host", cdx_execute(&request, &result) == CDX_COMPLETED && result.stdout_bytes.size == 5u && memcmp(result.stdout_bytes.data, "alive", 5u) == 0 && result.cleanup_complete);
    cdx_result_free(&result);
    request.program = "/definitely-missing-cdx-program";
    record("POSIX invalid program zero spawn", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0u && result.posix_error == ENOENT);
    cdx_result_free(&result);
}

int main(int argc, char **argv) {
    int pilot = argc == 3 && strcmp(argv[2], "--pilot") == 0;
    alarm(30u);
    if (!getcwd(cwd, sizeof(cwd))) return 2;
    if (argc < 2 || strlen(argv[1]) >= sizeof(fixture)) return 2;
    memcpy(fixture, argv[1], strlen(argv[1]) + 1u);
    printf("owner_pid=%ld\n", (long)getpid());
    test_argv();
    if (!pilot) { test_statuses(); test_stdin_and_limit(); test_cancel_timeout(); test_validation_and_group(); test_review_regressions(); }
    printf("{\"status\":\"%s\",\"passed\":%u,\"total\":%u}\n", passed == total ? "PASS" : "FAIL", passed, total);
    return passed == total ? 0 : 1;
}
