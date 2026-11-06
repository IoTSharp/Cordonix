#include "cdx/operations.h"
#include "cdx/presentation.h"
#include "cdx/version.h"
#include "cdx/phone.h"
#include "cdx/screenshot.h"
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
typedef struct active_request { HANDLE thread, cancel; cdx_json json; cdx_json_output reply; int params, id; cdx_phone_session *phone; } active_request;
static CRITICAL_SECTION output_lock;
int cdx_phone_ui(const wchar_t *target, int smoke);
int cdx_phone_ui_tray(const wchar_t *target);

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
static void tool_response(const cdx_json *json, int params, int id, HANDLE cancellation, cdx_phone_session *phone, cdx_json_output *reply) {
    int name = cdx_json_get(json, params, "name"), arguments = cdx_json_get(json, params, "arguments");
    cdx_json_output data, output;
    char summary[CDX_CALL_SUMMARY_BYTES];
    if (name < 0 || json->nodes[name].type != CDX_JSON_STRING) { rpc_error(json, id, -32602, "Tool name must be a string"); return; }
    cdx_json_output_init(&data); cdx_json_output_init(&output);
    cdx_summarize_call(json->nodes[name].string, json, arguments, summary);
    if (cdx_summary_progress(json, params, summary, &output)) {
        send_output(&output); cdx_json_output_free(&output); cdx_json_output_init(&output);
    }
    if (!strcmp(json->nodes[name].string, "phone") && arguments >= 0 && json->nodes[arguments].type == CDX_JSON_OBJECT)
        cdx_phone_session_call(phone, json, arguments, cancellation, &data);
    else cdx_call(json->nodes[name].string, json, arguments, cancellation, &data);
    response_start(&output, json, id); cdx_json_raw(&output, ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":");
    if (data.failed || !data.data) cdx_json_quote(&output, "{\"status\":\"output_limit\"}", strlen("{\"status\":\"output_limit\"}"));
    else cdx_json_quote(&output, data.data, data.size);
    cdx_json_raw(&output, "}]"); cdx_summary_metadata(summary, &output);
    cdx_json_raw(&output, "}}");
    cdx_json_output_free(&data);
    if (reply) *reply = output;
    else { send_output(&output); cdx_json_output_free(&output); }
}
static DWORD WINAPI execution_worker(void *context) {
    active_request *active = (active_request *)context;
    tool_response(&active->json, active->params, active->id, active->cancel, active->phone, &active->reply);
    return 0;
}
static int finish_active(active_request *active, int cancel) {
    if (!active->thread) return 1;
    if (cancel) (void)SetEvent(active->cancel);
    if (WaitForSingleObject(active->thread, cancel ? 10000u : 0u) != WAIT_OBJECT_0) return 0;
    send_output(&active->reply); cdx_json_output_free(&active->reply);
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
    cdx_phone_session *phone = cdx_phone_session_create();
    if (!phone) return 1;
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
        DWORD wait;
        if (active.thread) {
            HANDLE waits[2] = {active.thread, reader.available};
            wait = WaitForMultipleObjects(2, waits, FALSE, remaining > 1000 ? 1000 : remaining);
            if (wait == WAIT_OBJECT_0) { (void)finish_active(&active, 0); cdx_phone_session_tick(phone); continue; }
            if (wait == WAIT_OBJECT_0 + 1u) wait = WAIT_OBJECT_0;
        } else wait = WaitForSingleObject(reader.available, remaining > 1000 ? 1000 : remaining);
        if (wait == WAIT_TIMEOUT) {
            if (!active.thread) cdx_phone_session_tick(phone);
            continue;
        }
        if (wait != WAIT_OBJECT_0 || reader.eof) break;
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
            cdx_json_raw(&output, ",\"result\":{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{\"tools\":{\"listChanged\":false}},\"serverInfo\":{\"name\":\"Cordonix\",\"version\":\"" CDX_VERSION "\"},\"instructions\":\"Use typed argv. workspace_edit changes only memory; commit explicitly before external builds. Close or commit dirty documents before session end.\"}}");
            send_output(&output); cdx_json_output_free(&output); initialized = 1;
        } else if (string_node(&json, method, "ping")) {
            cdx_json_output_init(&output); response_start(&output, &json, id); cdx_json_raw(&output, ",\"result\":{}}"); send_output(&output); cdx_json_output_free(&output);
        } else if (!initialized) rpc_error(&json, id, -32002, "Initialize the MCP session first");
        else if (string_node(&json, method, "tools/list")) {
            cdx_json_output_init(&output); response_start(&output, &json, id); cdx_json_raw(&output, ",\"result\":"); cdx_tool_list(&output); cdx_json_raw(&output, "}"); send_output(&output); cdx_json_output_free(&output);
        } else if (string_node(&json, method, "tools/call")) {
            if (active.thread) rpc_error(&json, id, -32001, "An operation is running; wait or cancel that request");
            else if (string_node(&json, cdx_json_get(&json, params, "name"), "execute_program") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "search_text") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "git_status") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "query_processes") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "ssh") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "sftp") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "http") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "serial") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "network") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "phone") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "screenshot") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "data_query") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "find_files") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "hash_file") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "copy_file") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "move_file") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "verify_encoding") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "create_file") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "create_directory") ||
                     string_node(&json, cdx_json_get(&json, params, "name"), "read_files")) {
                active.json = json; active.id = id; active.params = params; active.cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
                active.phone = phone;
                active.thread = active.cancel ? CreateThread(NULL, 0, execution_worker, &active, 0, NULL) : NULL;
                if (active.thread) continue;
                if (active.cancel) CloseHandle(active.cancel);
                memset(&active, 0, sizeof(active)); rpc_error(&json, id, -32603, "Cannot start execution worker");
            } else tool_response(&json, params, id, NULL, phone, NULL);
        } else rpc_error(&json, id, -32601, "Unknown MCP method");
        cdx_json_free(&json);
    }
    SetEvent(reader.stop); CancelSynchronousIo(input_thread); SetEvent(reader.consumed);
    (void)WaitForSingleObject(input_thread, 2000); CloseHandle(input_thread);
    if (!finish_active(&active, 1)) {
        (void)fputs("Cordonix: operation cleanup did not finish before the shutdown deadline\n", stderr);
        ExitProcess(1);
    }
    cdx_workspace_clear();
    cdx_phone_session_destroy(phone);
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
static int run_phone_helper(void) {
    char input[2048];
    size_t size = fread(input, 1, sizeof(input), stdin);
    cdx_json json;
    cdx_json_output out;
    if (!cdx_json_parse(&json, input, size)) { cdx_json_free(&json); return 2; }
    cdx_json_output_init(&out);
    if (json.nodes[0].type == CDX_JSON_OBJECT) cdx_phone_helper(&json, 0, &out);
    else cdx_phone_error(&out, "validation_error", 0);
    send_output(&out); cdx_json_output_free(&out); cdx_json_free(&json); return 0;
}

int wmain(int argc, wchar_t **argv) {
    int result;
    (void)_setmode(_fileno(stdin), _O_BINARY); (void)_setmode(_fileno(stdout), _O_BINARY);
    InitializeCriticalSection(&output_lock);
    if (argc == 2 && !wcscmp(argv[1], L"--mcp")) result = run_mcp();
    else if (argc == 2 && !wcscmp(argv[1], L"--phone-helper")) result = run_phone_helper();
    else if (argc == 9 && !wcscmp(argv[1], L"--screenshot-worker")) result = cdx_screenshot_worker(argc, argv);
    else if ((argc == 2 || argc == 3) && !wcscmp(argv[1], L"--phone-ui")) { FreeConsole(); result = cdx_phone_ui(argc == 3 ? argv[2] : L"", 0); }
    else if ((argc == 2 || argc == 3) && !wcscmp(argv[1], L"--phone-ui-tray")) { FreeConsole(); result = cdx_phone_ui_tray(argc == 3 ? argv[2] : L""); }
    else if (argc == 2 && !wcscmp(argv[1], L"--phone-ui-smoke")) result = cdx_phone_ui(L"", 1);
    else if (argc == 3 && !wcscmp(argv[1], L"--request")) result = run_request(argv[2]);
    else if (argc == 2 && !wcscmp(argv[1], L"--version")) { (void)puts(CDX_VERSION_TEXT); result = 0; }
    else { (void)fputs("Usage: cdx --mcp | --request absolute-request.json | --phone-ui [phone-name] | --phone-ui-tray [phone-name] | --version\n", stderr); result = 2; }
    DeleteCriticalSection(&output_lock); return result;
}
