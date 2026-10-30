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

static void check_environment(const char *label, cdx_exec_request *request, const char *expected) {
    cdx_exec_result result;
    size_t length = strlen(expected);
    int ok = cdx_execute(request, &result) == CDX_COMPLETED && result.cleanup_complete &&
        result.stdout_bytes.size == length && (!length || memcmp(result.stdout_bytes.data, expected, length) == 0);
    record(label, ok);
    cdx_result_free(&result);
}

static void test_environment(int pilot) {
    const char *args[] = {"env", "CDX_ENV_TEST_OWNED"};
    cdx_env_entry entries[] = {{"CDX_ENV_TEST_OWNED", "中文 $(literal)=\"quote\"\n"}, {"CDX_ENV_TEST_NEW", "new"}};
    cdx_exec_request request = request_for(args, 2);
    if (setenv(args[1], "parent", 1) != 0) { record("environment fixture setup", 0); return; }
    request.env = entries; request.env_count = sizeof(entries) / sizeof(entries[0]);
    check_environment("child Unicode environment override", &request, "中文 $(literal)=\"quote\"\n");
    if (!pilot) {
        args[1] = "CDX_ENV_TEST_NEW";
        check_environment("new child environment variable", &request, "new");
        args[1] = "CDX_ENV_TEST_OWNED"; entries[0].value = "";
        check_environment("empty child environment value", &request, "");
        entries[0].value = NULL;
        check_environment("deleted child environment value", &request, "<absent>");
        request.env = NULL; request.env_count = 0;
        check_environment("inherit environment without overrides", &request, "parent");
        record("parent environment unchanged", getenv(args[1]) && strcmp(getenv(args[1]), "parent") == 0);
        args[1] = "CDX_ENV_TEST_NEW";
        check_environment("new child variable absent from parent", &request, "<absent>");
        entries[0].name = "CDX_ENV_中文"; entries[0].value = "value";
        request.env = entries; request.env_count = 1; args[1] = entries[0].name;
        check_environment("Unicode environment name", &request, "value");
        entries[0].name = "cdx_env_test_owned"; entries[0].value = "lower";
        args[1] = "CDX_ENV_TEST_OWNED";
        check_environment("POSIX case-sensitive inherited name", &request, "parent");
        args[1] = "cdx_env_test_owned";
        check_environment("POSIX separate lowercase name", &request, "lower");
    }
    (void)unsetenv("CDX_ENV_TEST_OWNED");
}

static void test_environment_validation(void) {
    const char *args[] = {"env", "CDX_ENV_TEST"};
    const char invalid_utf8[] = {(char)0xff, 0};
    const char *invalid_names[] = {NULL, "", "bad=name", invalid_utf8};
    cdx_env_entry entries[CDX_MAX_ENV + 1u];
    char names[CDX_MAX_ENV][32], long_name[CDX_MAX_ENV_NAME + 2u], long_value[CDX_MAX_ENV_VALUE + 2u];
    cdx_exec_request request = request_for(args, 2);
    cdx_exec_result result;
    size_t index;
    request.env = entries; request.env_count = 1; entries[0].value = "value";
    for (index = 0; index < sizeof(invalid_names) / sizeof(invalid_names[0]); ++index) {
        entries[0].name = invalid_names[index];
        record("invalid environment name rejects before spawn", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
        cdx_result_free(&result);
    }
    entries[0].name = "CDX_ENV_TEST"; entries[0].value = invalid_utf8;
    record("invalid environment value UTF-8 rejects", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    memset(long_name, 'x', CDX_MAX_ENV_NAME + 1u); long_name[CDX_MAX_ENV_NAME + 1u] = 0;
    entries[0].name = long_name; entries[0].value = "value";
    record("oversize environment name rejects", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    memset(long_value, 'x', CDX_MAX_ENV_VALUE + 1u); long_value[CDX_MAX_ENV_VALUE + 1u] = 0;
    entries[0].name = "CDX_ENV_TEST"; entries[0].value = long_value;
    record("oversize environment value rejects", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    entries[0].value = "one"; entries[1].name = "CDX_ENV_TEST"; entries[1].value = "two"; request.env_count = 2;
    record("duplicate environment names reject", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    entries[1].name = "cdx_env_test"; args[1] = "cdx_env_test";
    check_environment("POSIX differently cased overrides coexist", &request, "two");
    request.env_count = CDX_MAX_ENV + 1u;
    record("environment count bound rejects", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    request.env_count = 1; request.env = NULL;
    record("missing environment array rejects", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    for (index = 0; index < CDX_MAX_ENV; ++index) {
        (void)snprintf(names[index], sizeof(names[index]), "CDX_ENV_BOUND_%zu", index);
        entries[index].name = names[index]; entries[index].value = "value";
    }
    request.env = entries; request.env_count = CDX_MAX_ENV; args[1] = names[CDX_MAX_ENV - 1u];
    check_environment("maximum environment override count", &request, "value");
}

typedef struct environment_case {
    cdx_exec_request request;
    cdx_exec_result result;
} environment_case;

static void *run_environment(void *raw) {
    environment_case *test = (environment_case *)raw;
    cdx_execute(&test->request, &test->result);
    return NULL;
}

static void test_parallel_environment(void) {
    const char *args[] = {"env", "CDX_ENV_PARALLEL"};
    cdx_env_entry first_env = {"CDX_ENV_PARALLEL", "first"}, second_env = {"CDX_ENV_PARALLEL", "second"};
    environment_case first, second;
    pthread_t threads[2];
    first.request = request_for(args, 2); first.request.env = &first_env; first.request.env_count = 1;
    second.request = request_for(args, 2); second.request.env = &second_env; second.request.env_count = 1;
    if (pthread_create(&threads[0], NULL, run_environment, &first) != 0) { record("parallel environment setup", 0); return; }
    if (pthread_create(&threads[1], NULL, run_environment, &second) != 0) {
        (void)pthread_join(threads[0], NULL); cdx_result_free(&first.result); record("parallel environment setup", 0); return;
    }
    (void)pthread_join(threads[0], NULL); (void)pthread_join(threads[1], NULL);
    record("parallel child environment isolation", first.result.status == CDX_COMPLETED && second.result.status == CDX_COMPLETED &&
        first.result.cleanup_complete && second.result.cleanup_complete && first.result.stdout_bytes.size == 5u && second.result.stdout_bytes.size == 6u &&
        memcmp(first.result.stdout_bytes.data, "first", 5u) == 0 && memcmp(second.result.stdout_bytes.data, "second", 6u) == 0);
    cdx_result_free(&first.result); cdx_result_free(&second.result);
}

int main(int argc, char **argv) {
    int pilot = argc == 3 && strcmp(argv[2], "--pilot") == 0;
    alarm(30u);
    if (!getcwd(cwd, sizeof(cwd))) return 2;
    if (argc < 2 || strlen(argv[1]) >= sizeof(fixture)) return 2;
    memcpy(fixture, argv[1], strlen(argv[1]) + 1u);
    printf("owner_pid=%ld\n", (long)getpid());
    test_argv();
    test_environment(pilot);
    if (!pilot) { test_statuses(); test_stdin_and_limit(); test_cancel_timeout(); test_validation_and_group(); test_review_regressions(); test_environment_validation(); test_parallel_environment(); }
    printf("{\"status\":\"%s\",\"passed\":%u,\"total\":%u}\n", passed == total ? "PASS" : "FAIL", passed, total);
    return passed == total ? 0 : 1;
}
