#include "cdx/process_query.h"
#include "cdx/operations.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static unsigned total, passed;
static ULONGLONG deadline;

static void record(const char *name, int success) {
    ++total; if (success) ++passed;
    printf("%s: %s\n", name, success ? "PASS" : "FAIL");
}

static cdx_process_query_request self_request(void) {
    cdx_process_query_request request;
    memset(&request, 0, sizeof(request));
    request.terms[0] = "cdx-process-query-tests"; request.term_count = 1;
    request.pids[0] = GetCurrentProcessId(); request.pid_count = 1;
    request.include_self = 1; request.limit = 10; request.max_processes = 4096;
    request.output_limit_bytes = 65536; request.timeout_ms = 1000;
    return request;
}

static void protocol_case(const char *arguments, const char *status, const char *name) {
    cdx_json input = {0}, data = {0};
    cdx_json_output output;
    int node, valid = cdx_json_parse(&input, arguments, strlen(arguments));
    cdx_json_output_init(&output);
    if (valid) cdx_process_query_call(&input, 0, NULL, &output);
    valid = valid && !output.failed && cdx_json_parse(&data, output.data, output.size);
    node = valid ? cdx_json_get(&data, 0, "status") : -1;
    record(name, data.nodes && node >= 0 && data.nodes[node].type == CDX_JSON_STRING && !strcmp(data.nodes[node].string, status));
    cdx_json_free(&data);
    cdx_json_output_free(&output); cdx_json_free(&input);
}

int main(int argc, char **argv) {
    cdx_process_query_request request = self_request();
    cdx_process_query_result result;
    FILETIME created, exited, kernel, user;
    ULARGE_INTEGER ticks;
    wchar_t *command = (wchar_t *)malloc(32768u * sizeof(wchar_t));
    int pilot = argc > 1 && !strcmp(argv[1], "--pilot");
    HANDLE cancellation;
    deadline = GetTickCount64() + 15000u;
    cdx_query_processes(&request, &result);
    record("native query finds only the requested live PID", !strcmp(result.status, "completed") &&
        result.scan_complete && result.count == 1 && result.entries[0].pid == GetCurrentProcessId());
    record("native query retains original Win32 command line", command && result.count == 1 &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, result.entries[0].command_line, -1, command, 32768) &&
        !wcscmp(command, GetCommandLineW()));
    ticks.QuadPart = 0;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        ticks.LowPart = created.dwLowDateTime; ticks.HighPart = created.dwHighDateTime;
    }
    record("native query retains exact OS creation time", result.count == 1 &&
        result.entries[0].creation_filetime == ticks.QuadPart && strstr(result.entries[0].created_utc, "T") != NULL &&
        strchr(result.entries[0].created_utc, 'Z') != NULL);
    cdx_process_query_free(&result);
    free(command);
    if (pilot) return passed == total ? 0 : 1;

    request.terms[0] = "never-match-4c813fd2"; request.terms[1] = "CDX-PROCESS-QUERY-TESTS"; request.term_count = 2;
    cdx_query_processes(&request, &result);
    record("native query uses case-insensitive literal OR", !strcmp(result.status, "completed") && result.count == 1);
    cdx_process_query_free(&result);
    request = self_request(); request.terms[0] = "$(literal)";
    cdx_query_processes(&request, &result);
    record("native query treats shell-shaped text literally", !strcmp(result.status, "completed") && result.count == 1);
    cdx_process_query_free(&result);
    request = self_request(); request.terms[0] = "query-no-match-d64fd38a";
    cdx_query_processes(&request, &result);
    record("native query reports a complete known-PID miss", !strcmp(result.status, "no_match") && result.scan_complete && !result.count);
    cdx_process_query_free(&result);
    request = self_request(); request.include_self = 0;
    cdx_query_processes(&request, &result);
    record("native query excludes its own runtime by default", !strcmp(result.status, "no_match") &&
        !result.count && result.excluded_self_pid == GetCurrentProcessId());
    cdx_process_query_free(&result);
    request = self_request(); request.limit = 1;
    cdx_query_processes(&request, &result);
    record("native query marks result cap as incomplete", !strcmp(result.status, "partial") &&
        !result.scan_complete && result.count == 1 && !strcmp(result.stop_reason, "result_limit"));
    cdx_process_query_free(&result);
    request = self_request(); request.max_processes = 1;
    cdx_query_processes(&request, &result);
    record("native query marks scan cap as incomplete", !strcmp(result.status, "partial") &&
        !result.scan_complete && !strcmp(result.stop_reason, "process_limit"));
    cdx_process_query_free(&result);
    request = self_request(); request.output_limit_bytes = 1;
    cdx_query_processes(&request, &result);
    record("native query preserves output-limit status", !strcmp(result.status, "output_limit") && !result.scan_complete && !result.count);
    cdx_process_query_free(&result);
    cancellation = CreateEventW(NULL, TRUE, TRUE, NULL);
    request = self_request(); request.cancellation = cancellation;
    cdx_query_processes(&request, &result);
    record("native query cancellation observes no process", cancellation && !strcmp(result.status, "cancelled") && !result.examined);
    cdx_process_query_free(&result);
    if (cancellation) CloseHandle(cancellation);
    request = self_request(); request.limit = 0;
    cdx_query_processes(&request, &result);
    record("native query validates bounds before inspection", !strcmp(result.status, "validation_error") && !result.examined);
    cdx_process_query_free(&result);
    request = self_request(); request.terms[0] = "\xff";
    cdx_query_processes(&request, &result);
    record("native query rejects invalid UTF8 without replacement", !strcmp(result.status, "encoding_error") && !result.examined);
    cdx_process_query_free(&result);
    protocol_case("{\"contains\":[]}", "validation_error", "query protocol rejects empty terms");
    protocol_case("{\"contains\":[\"\"]}", "validation_error", "query protocol rejects empty strings");
    protocol_case("{\"contains\":[\"x\"],\"pids\":[true]}", "validation_error", "query protocol rejects bool PIDs");
    protocol_case("{\"contains\":[\"x\"],\"limit\":1.5}", "validation_error", "query protocol rejects fractional limits");
    protocol_case("{\"contains\":[\"x\"],\"timeout_ms\":10001}", "validation_error", "query protocol rejects excessive timeout");
    record("native query test suite stays bounded", GetTickCount64() < deadline);
    printf("{\"status\":\"%s\",\"passed\":%u,\"total\":%u}\n", passed == total ? "PASS" : "FAIL", passed, total);
    return passed == total ? 0 : 1;
}
