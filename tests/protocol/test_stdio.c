#include "cdx/json.h"
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned passed, total;
static ULONGLONG deadline;

static void record(const char *name, int success) {
    ++total;
    if (success) ++passed;
    printf("%s: %s\n", name, success ? "PASS" : "FAIL");
}

typedef struct wire_client {
    HANDLE job, process, input, output, error;
} wire_client;

static int wire_start(wire_client *client, const char *program) {
    wchar_t executable[32768], command[32768];
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    HANDLE input_read = NULL, output_write = NULL, error_write = NULL;
    int success = 0;
    memset(client, 0, sizeof(*client)); memset(&startup, 0, sizeof(startup)); memset(&process, 0, sizeof(process)); memset(&limits, 0, sizeof(limits));
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, program, -1, executable, _countof(executable))) return 0;
    if (swprintf_s(command, _countof(command), L"\"%ls\" --mcp", executable) < 0) return 0;
    client->job = CreateJobObjectW(NULL, NULL);
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!client->job || !SetInformationJobObject(client->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) goto done;
    if (!CreatePipe(&input_read, &client->input, &security, 0) || !CreatePipe(&client->output, &output_write, &security, 0) || !CreatePipe(&client->error, &error_write, &security, 0)) goto done;
    SetHandleInformation(client->input, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(client->output, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(client->error, HANDLE_FLAG_INHERIT, 0);
    startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_read; startup.hStdOutput = output_write; startup.hStdError = error_write;
    if (!CreateProcessW(executable, command, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, NULL, &startup, &process)) goto done;
    client->process = process.hProcess;
    if (!AssignProcessToJobObject(client->job, client->process)) { TerminateProcess(client->process, 3); goto done; }
    if (ResumeThread(process.hThread) == (DWORD)-1) goto done;
    printf("mcp_owner_pid=%lu parent_pid=%lu\n", (unsigned long)process.dwProcessId, (unsigned long)GetCurrentProcessId());
    success = 1;
done:
    if (process.hThread) CloseHandle(process.hThread);
    if (input_read) CloseHandle(input_read);
    if (output_write) CloseHandle(output_write);
    if (error_write) CloseHandle(error_write);
    return success;
}

static int wire_stop(wire_client *client) {
    int graceful = 0;
    DWORD code = 1;
    if (client->input) { CloseHandle(client->input); client->input = NULL; }
    if (client->process) {
        if (WaitForSingleObject(client->process, 3000) == WAIT_OBJECT_0) graceful = GetExitCodeProcess(client->process, &code) && code == 0;
        else TerminateJobObject(client->job, 4);
    }
    if (client->job) { TerminateJobObject(client->job, 4); CloseHandle(client->job); }
    if (client->process) { WaitForSingleObject(client->process, 1000); CloseHandle(client->process); }
    if (client->output) CloseHandle(client->output);
    if (client->error) CloseHandle(client->error);
    return graceful;
}

static int wire_send(wire_client *client, const char *text, size_t length) {
    DWORD written;
    return length <= 16384 && GetTickCount64() < deadline && WriteFile(client->input, text, (DWORD)length, &written, NULL) && written == length;
}

static int wire_read(wire_client *client, cdx_json *json) {
    char bytes[32768];
    size_t used = 0;
    unsigned iteration;
    ULONGLONG local_deadline = GetTickCount64() + 4000;
    memset(json, 0, sizeof(*json));
    for (iteration = 0; iteration < 40000 && GetTickCount64() < local_deadline && GetTickCount64() < deadline; ++iteration) {
        DWORD available = 0, got;
        if (!PeekNamedPipe(client->output, NULL, 0, NULL, &available, NULL)) return 0;
        if (!available) { Sleep(10); continue; }
        if (used + 1 >= sizeof(bytes) || !ReadFile(client->output, bytes + used, 1, &got, NULL) || got != 1) return 0;
        if (bytes[used++] == '\n') return cdx_json_parse(json, bytes, used);
    }
    return 0;
}

static int wire_tool_data(const cdx_json *response, cdx_json *data) {
    int result = cdx_json_get(response, 0, "result");
    int content = cdx_json_get(response, result, "content");
    int text;
    memset(data, 0, sizeof(*data));
    if (content < 0 || response->nodes[content].type != CDX_JSON_ARRAY) return 0;
    text = cdx_json_get(response, response->nodes[content].first, "text");
    if (text < 0 || response->nodes[text].type != CDX_JSON_STRING) return 0;
    return cdx_json_parse(data, response->nodes[text].string, strlen(response->nodes[text].string));
}

static int string_is(const cdx_json *json, int object, const char *key, const char *value) {
    int node = cdx_json_get(json, object, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_STRING && strcmp(json->nodes[node].string, value) == 0;
}

static int tool_call(wire_client *client, const char *name, const char *arguments, unsigned id, cdx_json *data) {
    cdx_json_output request;
    cdx_json response;
    int valid, response_id;
    memset(&response, 0, sizeof(response)); memset(data, 0, sizeof(*data));
    cdx_json_output_init(&request);
    cdx_json_raw(&request, "{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"id\":"); cdx_json_uint(&request, id);
    cdx_json_raw(&request, ",\"params\":{\"name\":"); cdx_json_quote(&request, name, strlen(name));
    cdx_json_raw(&request, ",\"arguments\":"); cdx_json_raw(&request, arguments); cdx_json_raw(&request, "}}\n");
    valid = !request.failed && wire_send(client, request.data, request.size) && wire_read(client, &response);
    response_id = valid ? cdx_json_get(&response, 0, "id") : -1;
    valid = valid && response_id >= 0 && response.nodes[response_id].type == CDX_JSON_NUMBER && response.nodes[response_id].number == id && wire_tool_data(&response, data);
    cdx_json_free(&response); cdx_json_output_free(&request);
    return valid;
}

static int file_equals(const wchar_t *path, const char *expected) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    char bytes[128]; DWORD got = 0;
    int same;
    if (file == INVALID_HANDLE_VALUE) return 0;
    same = ReadFile(file, bytes, sizeof(bytes), &got, NULL) && got == strlen(expected) && memcmp(bytes, expected, got) == 0;
    CloseHandle(file);
    return same;
}

static void workspace_cases(wire_client *client) {
    wchar_t path[32768], *tail;
    char utf8_path[65536], arguments[1024], hash[65];
    static const char original[] = "old \xe4\xb8\xad\r\n";
    static const char changed[] = "new \xe4\xb8\xad\r\n";
    cdx_json_output open_args;
    cdx_json data;
    HANDLE file;
    DWORD written;
    int valid, node;
    unsigned long long document_id = 0;
    if (!GetModuleFileNameW(NULL, path, _countof(path))) { record("workspace fixture create", 0); return; }
    tail = wcsrchr(path, L'\\');
    if (!tail || swprintf_s(tail + 1, _countof(path) - (size_t)(tail + 1 - path), L"cdx-stdio-%lu.txt", (unsigned long)GetCurrentProcessId()) < 0) { record("workspace fixture create", 0); return; }
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { record("workspace fixture create", 0); return; }
    valid = WriteFile(file, original, sizeof(original) - 1, &written, NULL) && written == sizeof(original) - 1;
    CloseHandle(file);
    if (!valid || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, utf8_path, sizeof(utf8_path), NULL, NULL)) { record("workspace fixture create", 0); DeleteFileW(path); return; }
    cdx_json_output_init(&open_args);
    cdx_json_raw(&open_args, "{\"path\":"); cdx_json_quote(&open_args, utf8_path, strlen(utf8_path)); cdx_json_raw(&open_args, ",\"encoding\":\"utf-8\"}");
    valid = tool_call(client, "workspace_open", open_args.data, 20, &data);
    node = valid ? cdx_json_get(&data, 0, "document_id") : -1;
    if (node >= 0) document_id = (unsigned long long)data.nodes[node].number;
    node = valid ? cdx_json_get(&data, 0, "memory_sha256") : -1;
    valid = valid && document_id && node >= 0 && data.nodes[node].type == CDX_JSON_STRING && strlen(data.nodes[node].string) == 64;
    if (valid) memcpy(hash, data.nodes[node].string, sizeof(hash));
    record("workspace opens with live revision", valid);
    cdx_json_free(&data); cdx_json_output_free(&open_args);
    if (!valid) { DeleteFileW(path); return; }
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"limit\":128}", document_id);
    valid = tool_call(client, "workspace_read", arguments, 21, &data);
    record("workspace read preserves UTF8 CRLF", valid && string_is(&data, 0, "text", original)); cdx_json_free(&data);
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"expected_sha256\":\"%s\",\"old\":\"old\",\"new\":\"new\",\"expected_count\":1}", document_id, hash);
    valid = tool_call(client, "workspace_edit", arguments, 22, &data);
    record("workspace dry-run leaves disk unchanged", valid && string_is(&data, 0, "status", "preview") && file_equals(path, original)); cdx_json_free(&data);
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"expected_sha256\":\"%s\",\"old\":\"old\",\"new\":\"new\",\"expected_count\":1,\"dry_run\":false}", document_id, hash);
    valid = tool_call(client, "workspace_edit", arguments, 23, &data);
    node = valid ? cdx_json_get(&data, 0, "dirty_in_memory") : -1;
    record("workspace edit changes memory only", valid && string_is(&data, 0, "status", "ok") && node >= 0 && data.nodes[node].boolean && file_equals(path, original)); cdx_json_free(&data);
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"offset\":4,\"limit\":1}", document_id);
    valid = tool_call(client, "workspace_read", arguments, 24, &data);
    record("UTF8 page never returns unchanged continuation", valid && string_is(&data, 0, "status", "validation_error")); cdx_json_free(&data);
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu}", document_id);
    valid = tool_call(client, "workspace_commit", arguments, 25, &data);
    node = valid ? cdx_json_get(&data, 0, "written_to_disk") : -1;
    record("workspace commit and byte readback", valid && string_is(&data, 0, "status", "ok") && node >= 0 && data.nodes[node].boolean && file_equals(path, changed)); cdx_json_free(&data);
    valid = tool_call(client, "workspace_close", arguments, 26, &data);
    record("workspace close releases document", valid && string_is(&data, 0, "status", "ok")); cdx_json_free(&data);
    (void)DeleteFileW(path);
}

static void wire_execute(wire_client *client, const char *fixture, const char *mode, int id) {
    cdx_json_output request;
    char number[32];
    cdx_json_output_init(&request);
    cdx_json_raw(&request, "{\"jsonrpc\":\"2.0\",\"id\":");
    (void)snprintf(number, sizeof(number), "%d", id); cdx_json_raw(&request, number);
    cdx_json_raw(&request, ",\"method\":\"tools/call\",\"params\":{\"name\":\"execute_program\",\"arguments\":{\"program\":");
    cdx_json_quote(&request, fixture, strlen(fixture));
    cdx_json_raw(&request, ",\"args\":["); cdx_json_quote(&request, mode, strlen(mode));
    cdx_json_raw(&request, "],\"cwd\":\"C:\\\\Windows\",\"timeout_ms\":3000}}}\n");
    if (request.failed || !wire_send(client, request.data, request.size)) ExitProcess(3);
    cdx_json_output_free(&request);
}

static int wire_suite(const char *program, const char *fixture, int pilot) {
    wire_client client;
    cdx_json response, data;
    int valid, node;
    static const char initialize[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"cdx-c-test\",\"version\":\"1\"}}}\n";
    static const char large_id[] = "{\"jsonrpc\":\"2.0\",\"id\":9007199254740993,\"method\":\"ping\"}\n";
    static const char omitted[] = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"tool_info\"}}\n";
    static const char list[] = "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/list\"}\n";
    static const char malformed[] = "{\"jsonrpc\":\"2.0\",\"id\":\n";
    static const char nul_frame[] = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}\0ignored\n";
    static const char cancel[] = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":11}}\n";
    deadline = GetTickCount64() + 60000;
    memset(&response, 0, sizeof(response)); memset(&data, 0, sizeof(data));
    if (!wire_start(&client, program)) { record("MCP server starts", 0); wire_stop(&client); return 1; }
    valid = wire_send(&client, initialize, sizeof(initialize) - 1) && wire_read(&client, &response);
    record("MCP initialize", valid && cdx_json_get(&response, 0, "result") >= 0); cdx_json_free(&response);
    if (pilot) { record("MCP EOF exits cleanly", wire_stop(&client)); return passed == total ? 0 : 1; }
    valid = wire_send(&client, list, sizeof(list) - 1) && wire_read(&client, &response);
    node = valid ? cdx_json_get(&response, cdx_json_get(&response, 0, "result"), "tools") : -1;
    record("MCP tools list", node >= 0 && response.nodes[node].type == CDX_JSON_ARRAY); cdx_json_free(&response);
    valid = wire_send(&client, malformed, sizeof(malformed) - 1) && wire_read(&client, &response);
    node = valid ? cdx_json_get(&response, cdx_json_get(&response, 0, "error"), "code") : -1;
    record("MCP malformed JSON reports parse error", node >= 0 && response.nodes[node].number == -32700); cdx_json_free(&response);
    valid = wire_send(&client, large_id, sizeof(large_id) - 1) && wire_read(&client, &response);
    node = valid ? cdx_json_get(&response, 0, "id") : -1;
    record("MCP large request id unchanged", node >= 0 && response.nodes[node].number_length == 16 && memcmp(response.nodes[node].number_text, "9007199254740993", 16) == 0); cdx_json_free(&response);
    valid = wire_send(&client, omitted, sizeof(omitted) - 1) && wire_read(&client, &response);
    memset(&data, 0, sizeof(data));
    valid = valid && wire_tool_data(&response, &data);
    record("MCP omitted empty arguments", valid && string_is(&data, 0, "status", "ok")); cdx_json_free(&response); cdx_json_free(&data);
    valid = wire_send(&client, nul_frame, sizeof(nul_frame) - 1) && wire_read(&client, &response);
    node = valid ? cdx_json_get(&response, cdx_json_get(&response, 0, "error"), "code") : -1;
    record("MCP rejects raw NUL without truncation", node >= 0 && response.nodes[node].number == -32700); cdx_json_free(&response);
    workspace_cases(&client);
    wire_execute(&client, fixture, "streams", 10);
    valid = wire_read(&client, &response); memset(&data, 0, sizeof(data));
    valid = valid && wire_tool_data(&response, &data);
    record("MCP process stdout/stderr preserved", valid && string_is(&data, 0, "status", "completed") && string_is(&data, 0, "stdout", "stdout\r\n\n") && string_is(&data, 0, "stderr", "stderr\n"));
    cdx_json_free(&response); cdx_json_free(&data);
    wire_execute(&client, fixture, "invalid-utf8", 12);
    valid = wire_read(&client, &response); memset(&data, 0, sizeof(data));
    valid = valid && wire_tool_data(&response, &data);
    record("MCP invalid UTF8 preserves original base64", valid && string_is(&data, 0, "status", "encoding_error") && string_is(&data, 0, "stdout_base64", "dmFsaWT/Cg=="));
    cdx_json_free(&response); cdx_json_free(&data);
    wire_execute(&client, fixture, "sleep", 11);
    Sleep(150);
    valid = wire_send(&client, cancel, sizeof(cancel) - 1) && wire_read(&client, &response); memset(&data, 0, sizeof(data));
    valid = valid && wire_tool_data(&response, &data);
    node = valid ? cdx_json_get(&data, 0, "cleanup_complete") : -1;
    record("MCP asynchronous cancellation cleanup", valid && string_is(&data, 0, "status", "cancelled") && node >= 0 && data.nodes[node].boolean);
    cdx_json_free(&response); cdx_json_free(&data);
    record("MCP EOF exits cleanly", wire_stop(&client));
    printf("{\"status\":\"%s\",\"passed\":%u,\"total\":%u}\n", passed == total ? "PASS" : "FAIL", passed, total);
    return passed == total ? 0 : 1;
}


int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) { fprintf(stderr, "Usage: cdx-stdio-tests cdx.exe fixture.exe [--pilot]\n"); return 2; }
    return wire_suite(argv[1], argv[2], argc == 4 && strcmp(argv[3], "--pilot") == 0);
}
