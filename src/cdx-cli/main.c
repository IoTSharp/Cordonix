#include "cdx/operations.h"
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define INPUT_LIMIT (1024u * 1024u)
#define SESSION_MAX_MS (24ull * 60ull * 60ull * 1000ull)
#define SESSION_MAX_REQUESTS 100000u
typedef struct input_reader { HANDLE available, consumed, stop; char *line; size_t line_length; int eof, oversized; } input_reader;
typedef struct active_request { HANDLE thread, cancel; cdx_json json; int params, id; } active_request;
static CRITICAL_SECTION output_lock;

static void send_output(cdx_json_output *output) {
    EnterCriticalSection(&output_lock);
    if (output->failed) (void)fputs("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Response exceeds output capacity\"}}\n", stdout);
    else if (output->data) { (void)fwrite(output->data, 1, output->size, stdout); (void)fputc('\n', stdout); }
    (void)fflush(stdout);
    LeaveCriticalSection(&output_lock);
}
static void response_start(cdx_json_output *output, const cdx_json *json, int id) {
    cdx_json_raw(output, "{\"jsonrpc\":\"2.0\",\"id\":");
    if (id < 0) cdx_json_raw(output, "null");
    else if (json->nodes[id].type == CDX_JSON_STRING) cdx_json_quote(output, json->nodes[id].string, strlen(json->nodes[id].string));
    else cdx_json_rawn(output, json->nodes[id].number_text, json->nodes[id].number_length);
}
static void rpc_error(const cdx_json *json, int id, int code, const char *message) {
    char number[24];
    cdx_json_output output; cdx_json_output_init(&output);
    response_start(&output, json, id); cdx_json_raw(&output, ",\"error\":{\"code\":");
    (void)snprintf(number, sizeof(number), "%d", code); cdx_json_raw(&output, number);
    cdx_json_raw(&output, ",\"message\":"); cdx_json_quote(&output, message, strlen(message)); cdx_json_raw(&output, "}}");
    send_output(&output); cdx_json_output_free(&output);
}
static int string_node(const cdx_json *json, int node, const char *value) {
    return node >= 0 && json->nodes[node].type == CDX_JSON_STRING && !strcmp(json->nodes[node].string, value);
}
static int ids_equal(const cdx_json *a, int ai, const cdx_json *b, int bi) {
    if (ai < 0 || bi < 0 || a->nodes[ai].type != b->nodes[bi].type) return 0;
    if (a->nodes[ai].type == CDX_JSON_STRING) return !strcmp(a->nodes[ai].string, b->nodes[bi].string);
    return a->nodes[ai].type == CDX_JSON_NUMBER && a->nodes[ai].number_length == b->nodes[bi].number_length &&
        !memcmp(a->nodes[ai].number_text, b->nodes[bi].number_text, a->nodes[ai].number_length);
}
static void tool_response(const cdx_json *json, int params, int id, HANDLE cancellation) {
    int name = cdx_json_get(json, params, "name"), arguments = cdx_json_get(json, params, "arguments");
    cdx_json_output data, output;
    if (name < 0 || json->nodes[name].type != CDX_JSON_STRING) { rpc_error(json, id, -32602, "Tool name must be a string"); return; }
    cdx_json_output_init(&data); cdx_json_output_init(&output);
    cdx_call(json->nodes[name].string, json, arguments, cancellation, &data);
    response_start(&output, json, id); cdx_json_raw(&output, ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":");
    if (data.failed || !data.data) cdx_json_quote(&output, "{\"status\":\"output_limit\"}", strlen("{\"status\":\"output_limit\"}"));
    else cdx_json_quote(&output, data.data, data.size);
    cdx_json_raw(&output, "}]}}"); send_output(&output);
    cdx_json_output_free(&data); cdx_json_output_free(&output);
}
static DWORD WINAPI execution_worker(void *context) {
    active_request *active = (active_request *)context;
    tool_response(&active->json, active->params, active->id, active->cancel);
    return 0;
}
static int finish_active(active_request *active, int cancel) {
    if (!active->thread) return 1;
    if (cancel) (void)SetEvent(active->cancel);
    if (WaitForSingleObject(active->thread, cancel ? 10000u : 0u) != WAIT_OBJECT_0) return 0;
    CloseHandle(active->thread); CloseHandle(active->cancel); cdx_json_free(&active->json); memset(active, 0, sizeof(*active));
    return 1;
}
static DWORD WINAPI read_input(void *context) {
    input_reader *reader = (input_reader *)context;
    char chunk[4096];
    DWORD received, i;
    size_t length = 0;
    unsigned messages = 0;
    HANDLE waits[2] = {reader->stop, reader->consumed};
    char *line = (char *)malloc(INPUT_LIMIT + 1u);
    if (!line) { reader->eof = 1; SetEvent(reader->available); return 1; }
    while (messages < SESSION_MAX_REQUESTS && WaitForSingleObject(reader->stop, 0) != WAIT_OBJECT_0) {
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), chunk, sizeof(chunk), &received, NULL) || !received) break;
        for (i = 0; i < received; ++i) {
            if (chunk[i] != '\n') {
                if (length == INPUT_LIMIT) { reader->oversized = 1; goto end; }
                line[length++] = chunk[i]; continue;
            }
            line[length] = 0; reader->line = line; reader->line_length = length; SetEvent(reader->available);
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) goto end;
            length = 0; ++messages;
        }
    }
    if (length && !reader->oversized) {
        line[length] = 0; reader->line = line; reader->line_length = length; SetEvent(reader->available);
        (void)WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    }
end:
    free(line); reader->line = NULL; reader->eof = 1; SetEvent(reader->available); return 0;
}
static int run_mcp(void) {
    input_reader reader;
    active_request active;
    HANDLE input_thread;
    unsigned count;
    ULONGLONG deadline = GetTickCount64() + SESSION_MAX_MS;
    int initialized = 0;
    memset(&reader, 0, sizeof(reader)); memset(&active, 0, sizeof(active));
    reader.available = CreateEventW(NULL, FALSE, FALSE, NULL);
    reader.consumed = CreateEventW(NULL, FALSE, FALSE, NULL);
    reader.stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!reader.available || !reader.consumed || !reader.stop) return 1;
    input_thread = CreateThread(NULL, 0, read_input, &reader, 0, NULL);
    if (!input_thread) return 1;
    for (count = 0; count < SESSION_MAX_REQUESTS && GetTickCount64() < deadline; ++count) {
        cdx_json json;
        cdx_json_output output;
        int method, id, params;
        DWORD remaining = (DWORD)(deadline - GetTickCount64());
        if (WaitForSingleObject(reader.available, remaining) != WAIT_OBJECT_0 || reader.eof) break;
        if (!reader.line || !reader.line_length) { SetEvent(reader.consumed); continue; }
        if (!cdx_json_parse(&json, reader.line, reader.line_length)) {
            rpc_error(NULL, -1, -32700, "Invalid UTF-8 JSON request");
            cdx_json_free(&json);
            SetEvent(reader.consumed); continue;
        }
        SetEvent(reader.consumed);
        method = cdx_json_get(&json, 0, "method"); id = cdx_json_get(&json, 0, "id"); params = cdx_json_get(&json, 0, "params");
        (void)finish_active(&active, 0);
        if (!string_node(&json, cdx_json_get(&json, 0, "jsonrpc"), "2.0") || method < 0 || json.nodes[method].type != CDX_JSON_STRING ||
            (id >= 0 && json.nodes[id].type != CDX_JSON_STRING && json.nodes[id].type != CDX_JSON_NUMBER)) {
            rpc_error(NULL, -1, -32600, "Invalid JSON-RPC request"); cdx_json_free(&json); continue;
        }
        if (string_node(&json, method, "notifications/cancelled")) {
            if (active.thread && ids_equal(&json, cdx_json_get(&json, params, "requestId"), &active.json, active.id)) SetEvent(active.cancel);
        } else if (id < 0) {
            /* Notifications never receive JSON-RPC responses. */
        } else if (string_node(&json, method, "initialize")) {
            cdx_json_output_init(&output); response_start(&output, &json, id);
            cdx_json_raw(&output, ",\"result\":{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{\"tools\":{\"listChanged\":false}},\"serverInfo\":{\"name\":\"Cordonix\",\"version\":\"0.1.0-dev\"},\"instructions\":\"Use typed argv. workspace_edit changes only memory; commit explicitly before external builds. Close or commit dirty documents before session end.\"}}");
            send_output(&output); cdx_json_output_free(&output); initialized = 1;
        } else if (string_node(&json, method, "ping")) {
            cdx_json_output_init(&output); response_start(&output, &json, id); cdx_json_raw(&output, ",\"result\":{}}"); send_output(&output); cdx_json_output_free(&output);
        } else if (!initialized) rpc_error(&json, id, -32002, "Initialize the MCP session first");
        else if (string_node(&json, method, "tools/list")) {
            cdx_json_output_init(&output); response_start(&output, &json, id); cdx_json_raw(&output, ",\"result\":"); cdx_json_raw(&output, cdx_tool_list()); cdx_json_raw(&output, "}"); send_output(&output); cdx_json_output_free(&output);
        } else if (string_node(&json, method, "tools/call")) {
            if (active.thread) rpc_error(&json, id, -32001, "A program is running; wait or cancel that request");
            else if (string_node(&json, cdx_json_get(&json, params, "name"), "execute_program")) {
                active.json = json; active.id = id; active.params = params; active.cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
                active.thread = active.cancel ? CreateThread(NULL, 0, execution_worker, &active, 0, NULL) : NULL;
                if (active.thread) continue;
                if (active.cancel) CloseHandle(active.cancel);
                memset(&active, 0, sizeof(active)); rpc_error(&json, id, -32603, "Cannot start execution worker");
            } else tool_response(&json, params, id, NULL);
        } else rpc_error(&json, id, -32601, "Unknown MCP method");
        cdx_json_free(&json);
    }
    SetEvent(reader.stop); CancelSynchronousIo(input_thread); SetEvent(reader.consumed);
    (void)WaitForSingleObject(input_thread, 2000); CloseHandle(input_thread);
    (void)finish_active(&active, 1); cdx_workspace_clear();
    CloseHandle(reader.available); CloseHandle(reader.consumed); CloseHandle(reader.stop);
    return reader.oversized ? 2 : 0;
}
static int run_request(const wchar_t *path) {
    FILE *file = NULL;
    char *data;
    size_t length;
    cdx_json json;
    cdx_json_output output;
    int name, arguments;
    if (_wfopen_s(&file, path, L"rb") || !file) return 2;
    data = (char *)malloc(INPUT_LIMIT + 1u);
    if (!data) { fclose(file); return 1; }
    length = fread(data, 1, INPUT_LIMIT + 1u, file); fclose(file);
    if (!cdx_json_parse(&json, data, length)) { (void)fprintf(stderr, "%s\n", json.error); free(data); cdx_json_free(&json); return 2; }
    free(data); name = cdx_json_get(&json, 0, "operation"); arguments = cdx_json_get(&json, 0, "arguments");
    if (name < 0 || json.nodes[name].type != CDX_JSON_STRING) { cdx_json_free(&json); return 2; }
    cdx_json_output_init(&output); cdx_call(json.nodes[name].string, &json, arguments, NULL, &output);
    send_output(&output); cdx_json_output_free(&output); cdx_json_free(&json); cdx_workspace_clear(); return 0;
}
int wmain(int argc, wchar_t **argv) {
    int result;
    (void)_setmode(_fileno(stdin), _O_BINARY); (void)_setmode(_fileno(stdout), _O_BINARY);
    InitializeCriticalSection(&output_lock);
    if (argc == 2 && !wcscmp(argv[1], L"--mcp")) result = run_mcp();
    else if (argc == 3 && !wcscmp(argv[1], L"--request")) result = run_request(argv[2]);
    else if (argc == 2 && !wcscmp(argv[1], L"--version")) { (void)puts("Cordonix cdx 0.1.0-dev"); result = 0; }
    else { (void)fputs("Usage: cdx --mcp | --request absolute-request.json | --version\n", stderr); result = 2; }
    DeleteCriticalSection(&output_lock); return result;
}
