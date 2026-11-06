#define _POSIX_C_SOURCE 200809L
#include "cdx/operations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <wchar.h>
#else
#include <unistd.h>
#endif

static int checks;
static int failures;

static void expect(int condition, const char *label) {
    ++checks;
    if (!condition) { ++failures; (void)fprintf(stderr, "FAIL: %s\n", label); }
}

static int call_cancel(const char *text, const char *status, const char *field, cdx_cancel_handle cancellation) {
    cdx_json json;
    cdx_json_output output;
    int arguments;
    int ok = cdx_json_parse(&json, text, strlen(text));
    if (!ok) { cdx_json_free(&json); return 0; }
    arguments = cdx_json_get(&json, 0, "arguments");
    cdx_json_output_init(&output);
    cdx_program_discovery_call(&json, arguments, cancellation, &output);
    ok = output.data && strstr(output.data, status) != NULL && (!field || strstr(output.data, field) != NULL);
    cdx_json_output_free(&output);
    cdx_json_free(&json);
    return ok;
}

static int call(const char *text, const char *status, const char *field) {
    return call_cancel(text, status, field, NULL);
}

static void fixture_cases(void) {
    char path[4096];
    cdx_json_output request;
#ifdef _WIN32
    wchar_t temporary[2048], directory[2048], fixture[2048], self[2048];
    DWORD length = GetTempPathW(2048u, temporary);
    int ready = length && length < 2048u &&
        _snwprintf_s(directory, 2048u, _TRUNCATE, L"%lscdx-program-%lu", temporary, GetCurrentProcessId()) >= 0 &&
        CreateDirectoryW(directory, NULL) &&
        _snwprintf_s(fixture, 2048u, _TRUNCATE, L"%ls/含 空格.exe", directory) >= 0 &&
        GetModuleFileNameW(NULL, self, 2048u) && CopyFileW(self, fixture, TRUE) &&
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, fixture, -1, path, sizeof(path), NULL, NULL);
#else
    char directory[] = "/tmp/cdx-program-XXXXXX";
    int ready = mkdtemp(directory) && snprintf(path, sizeof(path), "%s/含 空格", directory) > 0 &&
        symlink("/bin/sh", path) == 0;
#endif
    expect(ready, "Unicode space executable fixture created");
    if (!ready) return;
    cdx_json_output_init(&request);
    cdx_json_raw(&request, "{\"arguments\":{\"name\":\"fixture\",\"search_path\":false,\"candidates\":[");
    cdx_json_quote(&request, path, strlen(path));
    cdx_json_raw(&request, ",");
    cdx_json_quote(&request, path, strlen(path));
    cdx_json_raw(&request, "]}}");
    expect(call(request.data, "\"status\":\"ok\"", "\"checked\":2"),
           "Unicode candidates resolve without execution");
#ifndef _WIN32
    {
        cdx_json_output quoted;
        cdx_json_output_init(&quoted);
        cdx_json_quote(&quoted, path, strlen(path));
        expect(call(request.data, "\"status\":\"ok\"", quoted.data),
               "symlink launch path retains interpreter identity");
        cdx_json_output_free(&quoted);
    }
#endif
    {
        cdx_json json, response;
        cdx_json_output output;
        int array, item, count = 0;
        int parsed = cdx_json_parse(&json, request.data, request.size);
        cdx_json_output_init(&output);
        if (parsed) cdx_program_discovery_call(&json, cdx_json_get(&json, 0, "arguments"), NULL, &output);
        parsed = output.data && cdx_json_parse(&response, output.data, output.size);
        if (parsed) {
            array = cdx_json_get(&response, 0, "candidates");
            if (array >= 0) for (item = response.nodes[array].first; item >= 0; item = response.nodes[item].next) ++count;
            cdx_json_free(&response);
        }
        expect(parsed && count == 1, "duplicate candidates return one executable");
        cdx_json_output_free(&output);
        cdx_json_free(&json);
    }
    cdx_json_output_free(&request);
#ifdef _WIN32
    {
        unsigned char dos[128] = {0};
        FILE *file = NULL;
        dos[0] = 'M'; dos[1] = 'Z'; dos[2] = 128; dos[4] = 1; dos[8] = 4; dos[24] = 64;
        ready = !_wfopen_s(&file, fixture, L"wb") && file != NULL;
        if (ready) { ready = fwrite(dos, 1u, sizeof(dos), file) == sizeof(dos); ready = !fclose(file) && ready; }
        cdx_json_output_init(&request);
        cdx_json_raw(&request, "{\"arguments\":{\"name\":\"fixture\",\"search_path\":false,\"candidates\":[");
        cdx_json_quote(&request, path, strlen(path));
        cdx_json_raw(&request, "]}}");
        expect(ready && call(request.data, "\"status\":\"not_found\"", "\"scan_complete\":true"),
               "legacy DOS executable is excluded from native candidates");
        cdx_json_output_free(&request);
    }
    expect(DeleteFileW(fixture) && RemoveDirectoryW(directory), "program fixtures cleaned");
#else
    expect(unlink(path) == 0 && rmdir(directory) == 0, "program fixtures cleaned");
#endif
}

int main(void) {
    {
        cdx_json schema;
        const char *text = cdx_program_discovery_tool_schema();
        int valid = cdx_json_parse(&schema, text, strlen(text));
        int input = valid ? cdx_json_get(&schema, 0, "inputSchema") : -1;
        int properties = input >= 0 ? cdx_json_get(&schema, input, "properties") : -1;
        int item = properties >= 0 ? schema.nodes[properties].first : -1;
        unsigned count = 0;
        while (item >= 0 && count < 8u) {
            int description = cdx_json_get(&schema, item, "description");
            if (description < 0 || schema.nodes[description].type != CDX_JSON_STRING ||
                !schema.nodes[description].string[0]) valid = 0;
            ++count;
            item = schema.nodes[item].next;
        }
        expect(valid && count == 7u && item < 0, "all published discovery input properties are documented");
        cdx_json_free(&schema);
    }
    {
        char name[301];
        cdx_json json;
        cdx_json_output request, output;
        memset(name, 'a', sizeof(name) - 1u); name[sizeof(name) - 1u] = 0;
        cdx_json_output_init(&request);
        cdx_json_raw(&request, "{\"arguments\":{\"name\":");
        cdx_json_quote(&request, name, strlen(name));
        cdx_json_raw(&request, ",\"path\":\"\",\"max_output_bytes\":1024}}");
        cdx_json_output_init(&output);
        if (cdx_json_parse(&json, request.data, request.size))
            cdx_program_discovery_call(&json, cdx_json_get(&json, 0, "arguments"), NULL, &output);
        expect(output.data && output.size <= 1024u && strstr(output.data, "\"status\":\"output_limit\"") &&
               strstr(output.data, "\"stop_reason\":\"max_output_bytes\""), "serialized result respects its output budget");
        cdx_json_free(&json);
        cdx_json_output_free(&output);
        cdx_json_output_free(&request);
    }
#ifdef _WIN32
    expect(call("{\"arguments\":{\"name\":\"where\",\"path\":\"C:\\\\Windows\\\\System32\",\"search_path\":true}}",
                "\"status\":\"ok\"", "where.exe"), "Windows PATH candidate resolves native exe");
    expect(call("{\"arguments\":{\"name\":\"where\",\"search_path\":false,\"candidates\":[\"C:\\\\Windows\\\\System32\\\\where.exe\"]}}",
                "\"status\":\"ok\"", "selected"), "Windows explicit candidate resolves native exe");
    expect(call("{\"arguments\":{\"name\":\"where\",\"search_path\":false,\"max_results\":1,\"candidates\":[\"C:\\\\Windows\\\\System32\\\\where.exe\",\"C:\\\\Windows\\\\System32\\\\cmd.exe\"]}}",
                "\"status\":\"output_limit\"", "\"truncated\":true"), "Windows result bound remains explicit");
#else
    expect(call("{\"arguments\":{\"name\":\"sh\",\"search_path\":false,\"candidates\":[\"/bin/sh\"]}}",
                "\"status\":\"ok\"", "sh"), "POSIX explicit executable resolves");
    expect(call("{\"arguments\":{\"name\":\"sh\",\"search_path\":false,\"max_results\":1,\"candidates\":[\"/bin/sh\",\"/bin/ls\"]}}",
                "\"status\":\"output_limit\"", "\"truncated\":true"), "POSIX result bound remains explicit");
    expect(call("{\"arguments\":{\"name\":\"sh\",\"search_path\":false,\"candidates\":[\"/bin/cdx-missing-directory/../sh\"]}}",
                "\"status\":\"not_found\"", "\"scan_complete\":true"), "missing/../ candidate preserves filesystem traversal");
    expect(call("{\"arguments\":{\"name\":\"sh\",\"path\":\"/bin:/usr/bin\"}}",
                "\"status\":\"ok\"", "\"scan_complete\":true"), "POSIX PATH lookup resolves executable");
#endif
    expect(call(
#ifdef _WIN32
                "{\"arguments\":{\"name\":\"cdx-program-does-not-exist\",\"search_path\":false,\"candidates\":[\"C:\\\\definitely\\\\missing\\\\cdx-program-does-not-exist.exe\"]}}",
#else
                "{\"arguments\":{\"name\":\"cdx-program-does-not-exist\",\"search_path\":false,\"candidates\":[\"/definitely/missing/cdx-program-does-not-exist\"]}}",
#endif
                "\"status\":\"not_found\"", "\"scan_complete\":true"), "missing candidate remains distinct");
    expect(call("{\"arguments\":{\"name\":\"bad/name\"}}", "\"status\":\"validation_error\"", NULL),
           "path separators are rejected from program name");
    expect(call("{\"arguments\":{\"name\":\"cdx-program-does-not-exist\",\"path\":\"relative\"}}",
                "\"status\":\"validation_error\"", NULL), "relative PATH entries are rejected");
    expect(call("{\"arguments\":{\"name\":\"$(bad)\"}}", "\"status\":\"validation_error\"", NULL),
           "command substitution is rejected");
    expect(call("{\"arguments\":{\"name\":\"empty-path\",\"path\":\"\"}}", "\"status\":\"not_found\"", "\"checked\":0"),
           "empty PATH does not search the working directory");
    expect(call("{\"arguments\":{\"name\":\"fixture\",\"search_path\":false}}", "\"status\":\"validation_error\"", NULL),
           "explicit-only search requires candidates");
#ifdef _WIN32
    {
        HANDLE cancellation = CreateEventW(NULL, TRUE, TRUE, NULL);
        expect(cancellation && call_cancel("{\"arguments\":{\"name\":\"where\",\"path\":\"C:\\\\Windows\\\\System32\"}}",
               "\"status\":\"cancelled\"", "\"scan_complete\":false", cancellation), "cancellation stops discovery");
        if (cancellation) CloseHandle(cancellation);
    }
#else
    {
        atomic_int cancellation = ATOMIC_VAR_INIT(1);
        expect(call_cancel("{\"arguments\":{\"name\":\"sh\",\"path\":\"/bin\"}}",
               "\"status\":\"cancelled\"", "\"scan_complete\":false", &cancellation), "cancellation stops discovery");
    }
#endif
    fixture_cases();
    {
#ifdef _WIN32
        DWORD size = GetEnvironmentVariableW(L"PATH", NULL, 0);
        wchar_t *saved = size ? (wchar_t *)malloc((size_t)size * sizeof(*saved)) : NULL;
        int ready = !size || (saved && GetEnvironmentVariableW(L"PATH", saved, size) < size);
        expect(ready && SetEnvironmentVariableW(L"PATH", NULL), "inherited PATH removed for regression");
        expect(ready && call("{\"arguments\":{\"name\":\"where\",\"candidates\":[\"C:\\\\Windows\\\\System32\\\\where.exe\"]}}",
                            "\"status\":\"ok\"", "\"checked\":1"), "unset PATH preserves explicit candidate lookup");
        expect(ready && SetEnvironmentVariableW(L"PATH", saved), "inherited PATH restored");
#else
        const char *inherited = getenv("PATH");
        char *saved = inherited ? strdup(inherited) : NULL;
        int ready = !inherited || saved != NULL;
        expect(ready && unsetenv("PATH") == 0, "inherited PATH removed for regression");
        expect(ready && call("{\"arguments\":{\"name\":\"sh\",\"candidates\":[\"/bin/sh\"]}}",
                            "\"status\":\"ok\"", "\"checked\":1"), "unset PATH preserves explicit candidate lookup");
        expect(ready && (saved ? setenv("PATH", saved, 1) : unsetenv("PATH")) == 0, "inherited PATH restored");
#endif
        free(saved);
    }
    (void)printf("program discovery checks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
