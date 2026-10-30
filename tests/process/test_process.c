#include "cdx/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static wchar_t fixture[32768];
static wchar_t cwd[32768];
static unsigned int passed, total;
static ULONGLONG suite_deadline;

static cdx_exec_request request_for(const wchar_t *const *args, size_t count) {
    cdx_exec_request request;
    ZeroMemory(&request, sizeof(request));
    request.program = fixture; request.cwd = cwd;
    request.args = args; request.arg_count = count;
    request.timeout_ms = 3000; request.output_limit_bytes = 1048576;
    return request;
}

static void record(const char *name, int success) {
    ++total;
    if (success) ++passed;
    printf("%s: %s\n", name, success ? "PASS" : "FAIL");
}

static void test_echo(int pilot) {
    const wchar_t *args[] = { L"echo", L"one", L"two words", L"", L"\x4e2d\x6587", L"a\"b", L"a'b", L"$(literal)", L"${value}", L"|", L">", L"&&", L"a;b", L"line1\nline2", L"C:\\Program Files\\x\\", L"\\\\server\\share\\", L"slash\\\"quote", L"double\\\\\"quote", L"backtick`", L"\xd83d\xde42" };
    cdx_exec_request request = request_for(args, pilot ? 3 : _countof(args));
    cdx_exec_result result;
    size_t offset = 0, index;
    int good = cdx_execute(&request, &result) == CDX_COMPLETED && result.cleanup_complete;
    for (index = 1; index < request.arg_count && good; ++index) {
        char expected[4096];
        int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, args[index], -1, expected, sizeof(expected), NULL, NULL);
        good = length > 0 && offset + (size_t)length <= result.stdout_bytes.size && memcmp(result.stdout_bytes.data + offset, expected, (size_t)length) == 0;
        offset += (size_t)length;
    }
    record(pilot ? "minimal argv" : "Unicode/empty/quotes/slashes/literal argv", good && offset == result.stdout_bytes.size);
    cdx_result_free(&result);
}

static void test_exit_codes(void) {
    const wchar_t *args[] = { L"exit23" };
    int expected[] = { 0, 23 };
    cdx_exec_request request = request_for(args, 1);
    cdx_exec_result result;
    record("unexpected exit code", cdx_execute(&request, &result) == CDX_FAILED && result.exit_code_known && result.exit_code == 23 && result.cleanup_complete);
    cdx_result_free(&result);
    request.expected_exit_codes = expected; request.expected_exit_count = 2;
    record("explicit expected exit code", cdx_execute(&request, &result) == CDX_COMPLETED && result.exit_code == 23 && result.cleanup_complete);
    cdx_result_free(&result);
}

static void test_stdin(void) {
    const wchar_t *args[] = { L"stdin" };
    cdx_exec_request request = request_for(args, 1);
    unsigned char *input = (unsigned char *)malloc(300000);
    size_t index;
    cdx_exec_result result;
    if (!input) { record("large stdin", 0); return; }
    for (index = 0; index < 300000; ++index) input[index] = (unsigned char)('a' + index % 26);
    request.stdin_utf8 = input; request.stdin_size = 300000;
    record("large stdin", cdx_execute(&request, &result) == CDX_COMPLETED && result.stdout_bytes.size == 300000 && memcmp(input, result.stdout_bytes.data, 300000) == 0 && result.cleanup_complete);
    free(input); cdx_result_free(&result);
}

static void test_streams(void) {
    const wchar_t *args[] = { L"streams" };
    cdx_exec_request request = request_for(args, 1);
    cdx_exec_result result;
    record("stdout/stderr tail preservation", cdx_execute(&request, &result) == CDX_COMPLETED && result.stdout_bytes.size == 9 && result.stderr_bytes.size == 7 && memcmp(result.stdout_bytes.data, "stdout\r\n\n", 9) == 0 && memcmp(result.stderr_bytes.data, "stderr\n", 7) == 0);
    cdx_result_free(&result);
}

static void test_limits(void) {
    const wchar_t *args[] = { L"sleep" };
    cdx_exec_request request = request_for(args, 1);
    cdx_exec_result result;
    request.timeout_ms = 100;
    record("timeout cleanup", cdx_execute(&request, &result) == CDX_TIMED_OUT && result.cleanup_complete && result.elapsed_ms < 3000);
    cdx_result_free(&result);
    args[0] = L"output"; request.timeout_ms = 3000; request.output_limit_bytes = 8192;
    record("output cap cleanup", cdx_execute(&request, &result) == CDX_OUTPUT_LIMIT && result.cleanup_complete && result.output_truncated && result.stdout_bytes.size + result.stderr_bytes.size == 8192);
    cdx_result_free(&result);
}

static DWORD WINAPI cancel_later(void *event) { Sleep(100); SetEvent((HANDLE)event); return 0; }

static void test_cancel(void) {
    const wchar_t *args[] = { L"sleep" };
    cdx_exec_request request = request_for(args, 1);
    cdx_exec_result result;
    HANDLE thread;
    request.cancel_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    thread = CreateThread(NULL, 0, cancel_later, request.cancel_event, 0, NULL);
    record("cancel cleanup", cdx_execute(&request, &result) == CDX_CANCELLED && result.cleanup_complete && result.pid != 0);
    cdx_result_free(&result);
    WaitForSingleObject(thread, 1000); CloseHandle(thread);
    record("cancel before spawn", cdx_execute(&request, &result) == CDX_CANCELLED && result.pid == 0);
    cdx_result_free(&result); CloseHandle(request.cancel_event);
}

static void test_validation(void) {
    const wchar_t *args[] = { L"echo" };
    cdx_exec_request request = request_for(args, 1);
    cdx_exec_result result;
    request.cwd = L"relative";
    record("invalid cwd zero spawn", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    request.cwd = cwd; request.program = L"C:\\Windows\\System32\\cmd.exe";
    record("shell rejection", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
}

static void test_descendant(int timeout) {
    const wchar_t *args[] = { timeout ? L"tree-wait" : L"tree" };
    cdx_exec_request request = request_for(args, 1);
    cdx_exec_result result;
    HANDLE descendant;
    DWORD descendant_id = 0;
    int live = 0, success;
    if (timeout) request.timeout_ms = 250;
    success = cdx_execute(&request, &result) == (timeout ? CDX_TIMED_OUT : CDX_COMPLETED) && result.job_processes_total >= 2 && result.cleanup_complete && result.job_active_after_cleanup == 0;
    if (result.stdout_bytes.size) descendant_id = (DWORD)strtoul((const char *)result.stdout_bytes.data, NULL, 10);
    descendant = descendant_id ? OpenProcess(SYNCHRONIZE, FALSE, descendant_id) : NULL;
    if (descendant) { live = WaitForSingleObject(descendant, 1000) == WAIT_TIMEOUT; CloseHandle(descendant); }
    if (!success || !descendant_id || live) fprintf(stderr, "tree detail status=%s total=%lu before=%lu after=%lu cleanup=%d descendant=%lu live=%d stdout=%zu error=%lu\n", cdx_status_name(result.status), (unsigned long)result.job_processes_total, (unsigned long)result.job_active_before_cleanup, (unsigned long)result.job_active_after_cleanup, result.cleanup_complete, (unsigned long)descendant_id, live, result.stdout_bytes.size, (unsigned long)result.win32_error);
    record(timeout ? "timeout descendant cleanup" : "parent-exit descendant cleanup", success && descendant_id != 0 && !live);
    cdx_result_free(&result);
}

typedef struct parallel_case {
    cdx_exec_request request;
    cdx_exec_result result;
} parallel_case;

static DWORD WINAPI run_parallel(void *raw) {
    parallel_case *test = (parallel_case *)raw;
    cdx_execute(&test->request, &test->result);
    return 0;
}

static void test_parallel_pipes(void) {
    const wchar_t *first_args[] = { L"echo", L"first-only" };
    const wchar_t *second_args[] = { L"echo", L"second-only" };
    parallel_case first, second;
    HANDLE handles[2];
    DWORD waited;
    first.request = request_for(first_args, 2);
    second.request = request_for(second_args, 2);
    handles[0] = CreateThread(NULL, 0, run_parallel, &first, 0, NULL);
    handles[1] = CreateThread(NULL, 0, run_parallel, &second, 0, NULL);
    waited = WaitForMultipleObjects(2, handles, TRUE, 10000);
    record("parallel pipe handle isolation", waited == WAIT_OBJECT_0 && first.result.status == CDX_COMPLETED && second.result.status == CDX_COMPLETED && first.result.cleanup_complete && second.result.cleanup_complete && first.result.stdout_bytes.size == 11 && second.result.stdout_bytes.size == 12 && memcmp(first.result.stdout_bytes.data, "first-only", 11) == 0 && memcmp(second.result.stdout_bytes.data, "second-only", 12) == 0);
    if (waited != WAIT_OBJECT_0) ExitProcess(3);
    cdx_result_free(&first.result); cdx_result_free(&second.result);
    CloseHandle(handles[0]); CloseHandle(handles[1]);
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
    const wchar_t *args[] = {L"env", L"CDX_ENV_TEST_OWNED"};
    cdx_env_entry entries[] = {{L"cdx_env_test_owned", L"中文 $(literal)=\"quote\"\n"}, {L"CDX_ENV_TEST_NEW", L"new"}};
    cdx_exec_request request = request_for(args, 2);
    wchar_t parent[32];
    if (!SetEnvironmentVariableW(args[1], L"parent")) { record("environment fixture setup", 0); return; }
    request.env = entries; request.env_count = _countof(entries);
    check_environment("child Unicode environment and Windows case-insensitive override", &request, "中文 $(literal)=\"quote\"\n");
    if (!pilot) {
        args[1] = L"CDX_ENV_TEST_NEW";
        check_environment("new child environment variable", &request, "new");
        args[1] = L"CDX_ENV_TEST_OWNED"; entries[0].value = L"";
        check_environment("empty child environment value", &request, "");
        entries[0].value = NULL;
        check_environment("deleted child environment value", &request, "<absent>");
        request.env = NULL; request.env_count = 0;
        check_environment("inherit environment without overrides", &request, "parent");
        record("parent environment unchanged", GetEnvironmentVariableW(args[1], parent, _countof(parent)) == 6 && wcscmp(parent, L"parent") == 0);
        args[1] = L"CDX_ENV_TEST_NEW";
        check_environment("new child variable absent from parent", &request, "<absent>");
        entries[0].name = L"CDX_ENV_中文"; entries[0].value = L"value";
        request.env = entries; request.env_count = 1; args[1] = entries[0].name;
        check_environment("Unicode environment name", &request, "value");
    }
    (void)SetEnvironmentVariableW(L"CDX_ENV_TEST_OWNED", NULL);
}

static void test_environment_validation(void) {
    const wchar_t *args[] = {L"env", L"CDX_ENV_TEST"};
    const wchar_t invalid_utf16[] = {(wchar_t)0xd800, 0};
    const wchar_t *invalid_names[] = {NULL, L"", L"bad=name", invalid_utf16};
    cdx_env_entry entries[CDX_MAX_ENV + 1u];
    wchar_t names[CDX_MAX_ENV][32], long_name[CDX_MAX_ENV_NAME + 2u], long_value[CDX_MAX_ENV_VALUE + 2u];
    cdx_exec_request request = request_for(args, 2);
    cdx_exec_result result;
    size_t index;
    request.env = entries; request.env_count = 1;
    entries[0].value = L"value";
    for (index = 0; index < _countof(invalid_names) && GetTickCount64() < suite_deadline; ++index) {
        entries[0].name = invalid_names[index];
        record("invalid environment name rejects before spawn", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
        cdx_result_free(&result);
    }
    entries[0].name = L"CDX_ENV_TEST"; entries[0].value = invalid_utf16;
    record("invalid environment value UTF-16 rejects", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    wmemset(long_name, L'x', CDX_MAX_ENV_NAME + 1u); long_name[CDX_MAX_ENV_NAME + 1u] = 0;
    entries[0].name = long_name; entries[0].value = L"value";
    record("oversize environment name rejects", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    wmemset(long_value, L'x', CDX_MAX_ENV_VALUE + 1u); long_value[CDX_MAX_ENV_VALUE + 1u] = 0;
    entries[0].name = L"CDX_ENV_TEST"; entries[0].value = long_value;
    record("oversize environment value rejects", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    entries[0].value = L"one"; entries[1].name = L"cdx_env_test"; entries[1].value = L"two"; request.env_count = 2;
    record("Windows case-insensitive duplicate names reject", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    request.env_count = CDX_MAX_ENV + 1u;
    record("environment count bound rejects", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    request.env_count = 1; request.env = NULL;
    record("missing environment array rejects", cdx_execute(&request, &result) == CDX_VALIDATION_ERROR && result.pid == 0);
    cdx_result_free(&result);
    for (index = 0; index < CDX_MAX_ENV && GetTickCount64() < suite_deadline; ++index) {
        (void)swprintf_s(names[index], _countof(names[index]), L"CDX_ENV_BOUND_%zu", index);
        entries[index].name = names[index]; entries[index].value = L"value";
    }
    request.env = entries; request.env_count = CDX_MAX_ENV; args[1] = names[CDX_MAX_ENV - 1u];
    check_environment("maximum environment override count", &request, "value");
}

static void test_parallel_environment(void) {
    const wchar_t *args[] = {L"env", L"CDX_ENV_PARALLEL"};
    cdx_env_entry first_env = {L"CDX_ENV_PARALLEL", L"first"}, second_env = {L"CDX_ENV_PARALLEL", L"second"};
    parallel_case first, second;
    HANDLE handles[2];
    DWORD waited;
    first.request = request_for(args, 2); first.request.env = &first_env; first.request.env_count = 1;
    second.request = request_for(args, 2); second.request.env = &second_env; second.request.env_count = 1;
    handles[0] = CreateThread(NULL, 0, run_parallel, &first, 0, NULL);
    handles[1] = CreateThread(NULL, 0, run_parallel, &second, 0, NULL);
    waited = WaitForMultipleObjects(2, handles, TRUE, 10000);
    record("parallel child environment isolation", waited == WAIT_OBJECT_0 && first.result.status == CDX_COMPLETED && second.result.status == CDX_COMPLETED &&
        first.result.cleanup_complete && second.result.cleanup_complete && first.result.stdout_bytes.size == 5 && second.result.stdout_bytes.size == 6 &&
        memcmp(first.result.stdout_bytes.data, "first", 5) == 0 && memcmp(second.result.stdout_bytes.data, "second", 6) == 0);
    if (waited != WAIT_OBJECT_0) ExitProcess(3);
    cdx_result_free(&first.result); cdx_result_free(&second.result);
    CloseHandle(handles[0]); CloseHandle(handles[1]);
}

int wmain(int argc, wchar_t **argv) {
    wchar_t *last;
    int pilot = argc > 1 && wcscmp(argv[1], L"--pilot") == 0;
    suite_deadline = GetTickCount64() + 60000;
    if (!GetModuleFileNameW(NULL, fixture, _countof(fixture)) || !GetCurrentDirectoryW(_countof(cwd), cwd)) return 2;
    last = wcsrchr(fixture, L'\\');
    if (!last || wcscpy_s(last + 1, _countof(fixture) - (size_t)(last + 1 - fixture), L"cdx-process-fixture.exe")) return 2;
    printf("owner_pid=%lu tests_deadline_ms=60000\n", (unsigned long)GetCurrentProcessId());
    test_echo(1);
    test_environment(pilot);
    if (!pilot && GetTickCount64() < suite_deadline) {
        test_echo(0); test_exit_codes(); test_stdin(); test_streams(); test_limits(); test_cancel(); test_validation(); test_descendant(0); test_descendant(1); test_parallel_pipes(); test_environment_validation(); test_parallel_environment();
    }
    printf("{\"status\":\"%s\",\"passed\":%u,\"total\":%u}\n", passed == total ? "PASS" : "FAIL", passed, total);
    return passed == total ? 0 : 1;
}
