#include "cdx/operations.h"
#include "cdx/presentation.h"
#include "cdx/version.h"
#include "request_queue.h"
#include "cdx/phone.h"
#include "cdx/screenshot.h"
#include "cdx/vnc.h"
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
typedef struct active_request { HANDLE thread, cancel; cdx_json json; cdx_json_output reply; int params, id; cdx_phone_session *phone; ULONGLONG started_ms; } active_request;
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
    cdx_mcp_write_id(output, json, id);
}
static void build_rpc_error(cdx_json_output *output, const cdx_json *json, int id, int code, const char *message);
static void rpc_error(const cdx_json *json, int id, int code, const char *message) {
    cdx_json_output output;
    build_rpc_error(&output, json, id, code, message);
    send_output(&output); cdx_json_output_free(&output);
}
static int string_node(const cdx_json *json, int node, const char *value) {
    return node >= 0 && json->nodes[node].type == CDX_JSON_STRING && !strcmp(json->nodes[node].string, value);
}
static int supported_tool(const char *name) {
    static const char *const names[] = {
        "create_directory", "ssh", "phone", "query_processes", "search_text", "git_status",
        "create_file", "tool_info", "execute_program", "workspace_open", "list_directory",
        "resolve_path", "verify_encoding", "read_file", "workspace_read", "workspace_edit",
        "workspace_commit", "workspace_convert_encoding", "workspace_status", "workspace_search",
        "workspace_close", "sftp", "http", "serial", "network", "read_files", "screenshot", "vnc_screenshot",
        "data_query", "find_files", "find_program", "hash_file", "copy_file", "move_file", "stat"
    };
    size_t i;
    if (!name) return 0;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) if (!strcmp(name, names[i])) return 1;
    return 0;
}
static int asynchronous_tool(const char *name) {
    return name && (!strcmp(name, "execute_program") || !strcmp(name, "search_text") ||
        !strcmp(name, "git_status") || !strcmp(name, "query_processes") || !strcmp(name, "ssh") ||
        !strcmp(name, "sftp") || !strcmp(name, "http") || !strcmp(name, "serial") ||
        !strcmp(name, "network") || !strcmp(name, "phone") || !strcmp(name, "screenshot") || !strcmp(name, "vnc_screenshot") ||
        !strcmp(name, "data_query") || !strcmp(name, "find_files") || !strcmp(name, "find_program") ||
        !strcmp(name, "stat") || !strcmp(name, "hash_file") ||
        !strcmp(name, "copy_file") || !strcmp(name, "move_file") ||
        !strcmp(name, "verify_encoding") || !strcmp(name, "create_file") ||
        !strcmp(name, "create_directory") || !strcmp(name, "read_files"));
}
static int tool_status_is_error(const char *status) {
    static const char *const failures[] = {
        "failed", "spawn_error", "execution_error", "validation_error", "not_found", "dirty_in_memory",
        "file_changed", "conflict", "ambiguous_match", "encoding_error", "io_error",
        "commit_error", "connection_error", "tls_error", "permission_denied",
        "access_denied", "access_denied_or_busy", "busy", "error"
    };
    size_t i;
    if (!status) return 0;
    for (i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) if (!strcmp(status, failures[i])) return 1;
    return 0;
}
static void build_rpc_error(cdx_json_output *output, const cdx_json *json, int id, int code, const char *message) {
    char number[24];
    cdx_json_output_init(output);
    response_start(output, json, id);
    cdx_json_raw(output, ",\"error\":{\"code\":");
    (void)snprintf(number, sizeof(number), "%d", code);
    cdx_json_raw(output, number);
    cdx_json_raw(output, ",\"message\":");
    cdx_json_quote(output, message, strlen(message));
    cdx_json_raw(output, "}}");
}
static void tool_response(const cdx_json *json, int params, int id, HANDLE cancellation, cdx_phone_session *phone, cdx_json_output *reply) {
    int name, arguments;
    cdx_json_output data, output;
    cdx_json parsed;
    const char *status = NULL;
    int status_node, is_error = 0;
    char summary[CDX_CALL_SUMMARY_BYTES];
    if (params < 0 || json->nodes[params].type != CDX_JSON_OBJECT) {
        build_rpc_error(&output, json, id, -32602, "Tool call params must be an object");
        goto done;
    }
    name = cdx_json_get(json, params, "name");
    arguments = cdx_json_get(json, params, "arguments");
    if (name < 0 || json->nodes[name].type != CDX_JSON_STRING) {
        build_rpc_error(&output, json, id, -32602, "Tool name must be a string");
        goto done;
    }
    if (!supported_tool(json->nodes[name].string)) {
        build_rpc_error(&output, json, id, -32602, "Unknown tool");
        goto done;
    }
    if (arguments >= 0 && json->nodes[arguments].type != CDX_JSON_OBJECT) {
        build_rpc_error(&output, json, id, -32602, "Tool arguments must be an object");
        goto done;
    }
    cdx_json_output_init(&data); cdx_json_output_init(&output);
    cdx_summarize_call(json->nodes[name].string, json, arguments, summary);
    if (cdx_summary_progress(json, params, summary, &output)) {
        send_output(&output); cdx_json_output_free(&output); cdx_json_output_init(&output);
    }
    if (!strcmp(json->nodes[name].string, "phone") && arguments >= 0 && json->nodes[arguments].type == CDX_JSON_OBJECT)
        cdx_phone_session_call(phone, json, arguments, cancellation, &data);
    else cdx_call(json->nodes[name].string, json, arguments, cancellation, &data);
    memset(&parsed, 0, sizeof(parsed));
    if (!data.failed && data.data && cdx_json_parse(&parsed, data.data, data.size)) {
        status_node = cdx_json_get(&parsed, 0, "status");
        if (status_node >= 0 && parsed.nodes[status_node].type == CDX_JSON_STRING) {
            status = parsed.nodes[status_node].string;
            is_error = tool_status_is_error(status);
            if (!strcmp(json->nodes[name].string, "vnc_screenshot")) is_error |= cdx_vnc_status_is_error(status);
        }
    }
    response_start(&output, json, id); cdx_json_raw(&output, ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":");
    if (data.failed || !data.data) cdx_json_quote(&output, "{\"status\":\"output_limit\"}", strlen("{\"status\":\"output_limit\"}"));
    else cdx_json_quote(&output, data.data, data.size);
    cdx_json_raw(&output, "}]");
    if (is_error) cdx_json_raw(&output, ",\"isError\":true");
    cdx_summary_metadata(summary, &output);
    cdx_json_raw(&output, "}}");
    cdx_json_free(&parsed);
    cdx_json_output_free(&data);
done:
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
static void reject_pending(const cdx_mcp_request *request, const active_request *active, unsigned queued,
                           int code, const char *reason, const char *message) {
    cdx_json_output output;
    cdx_mcp_queue_error(&output, request, code, reason, message, queued,
        active->thread ? &active->json : NULL, active->id, active->params, active->started_ms, GetTickCount64());
    send_output(&output); cdx_json_output_free(&output);
}
static void run_pending(cdx_mcp_queue *queue, active_request *active, cdx_phone_session *phone, ULONGLONG deadline) {
    unsigned iteration;
    for (iteration = 0; iteration < CDX_MCP_QUEUE_CAPACITY && queue->count && GetTickCount64() < deadline; ++iteration) {
        cdx_mcp_request request;
        if (cdx_mcp_queue_wait(queue, GetTickCount64()) == 0) {
            cdx_mcp_queue_remove(queue, 0, &request);
            reject_pending(&request, active, queue->count, -32001, "queue_timeout", "Queued request timed out before execution");
            cdx_json_free(&request.json); continue;
        }
        if (active->thread) return;
        cdx_mcp_queue_remove(queue, 0, &request);
        if (asynchronous_tool(request.json.nodes[cdx_json_get(&request.json, request.params, "name")].string)) {
            active->json = request.json; active->id = request.id; active->params = request.params;
            active->phone = phone;
            active->started_ms = GetTickCount64(); active->cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
            active->thread = active->cancel ? CreateThread(NULL, 0, execution_worker, active, 0, NULL) : NULL;
            if (active->thread) return;
            if (active->cancel) CloseHandle(active->cancel);
            memset(active, 0, sizeof(*active)); rpc_error(&request.json, request.id, -32603, "Cannot start execution worker");
        } else tool_response(&request.json, request.params, request.id, NULL, phone, NULL);
        cdx_json_free(&request.json);
    }
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
static int run_mcp(cdx_tool_profile profile) {
    input_reader reader;
    active_request active;
    cdx_mcp_queue queue;
    HANDLE input_thread;
    unsigned count = 0, iteration;
    ULONGLONG deadline = GetTickCount64() + SESSION_MAX_MS;
    int initialized = 0;
    cdx_phone_session *phone = cdx_phone_session_create();
    if (!phone) return 1;
    memset(&reader, 0, sizeof(reader)); memset(&active, 0, sizeof(active)); memset(&queue, 0, sizeof(queue));
    reader.available = CreateEventW(NULL, FALSE, FALSE, NULL);
    reader.consumed = CreateEventW(NULL, FALSE, FALSE, NULL);
    reader.stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!reader.available || !reader.consumed || !reader.stop) return 1;
    input_thread = CreateThread(NULL, 0, read_input, &reader, 0, NULL);
    if (!input_thread) return 1;
    for (iteration = 0; iteration < SESSION_MAX_REQUESTS * 3u && count < SESSION_MAX_REQUESTS && GetTickCount64() < deadline; ++iteration) {
        cdx_json json;
        cdx_json_output output;
        int method, id, params;
        DWORD remaining, wait;
        ULONGLONG now;
        size_t input_bytes;
        (void)finish_active(&active, 0);
        if (reader.eof) break;
        run_pending(&queue, &active, phone, deadline);
        now = GetTickCount64();
        if (now >= deadline) break;
        remaining = (DWORD)(deadline - now);
        if (queue.count && cdx_mcp_queue_wait(&queue, now) < remaining) remaining = (DWORD)cdx_mcp_queue_wait(&queue, now);
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
        ++count;
        if (!reader.line || !reader.line_length) { SetEvent(reader.consumed); continue; }
        input_bytes = reader.line_length;
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
            cdx_mcp_request request;
            int request_id = cdx_json_get(&json, params, "requestId");
            if (active.thread && cdx_mcp_ids_equal(&json, request_id, &active.json, active.id)) SetEvent(active.cancel);
            else if (cdx_mcp_queue_cancel(&queue, &json, request_id, &request)) {
                reject_pending(&request, &active, queue.count, -32800, "cancelled", "Queued request cancelled before execution");
                cdx_json_free(&request.json);
            }
        } else if (id < 0) {
            /* Notifications never receive JSON-RPC responses. */
        } else if (string_node(&json, method, "initialize")) {
            cdx_json_output_init(&output); response_start(&output, &json, id);
            cdx_json_raw(&output, ",\"result\":{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{\"tools\":{\"listChanged\":false}},\"serverInfo\":{\"name\":\"Cordonix\",\"version\":\"" CDX_VERSION "\"},\"instructions\":\"Use typed argv. workspace_edit changes only memory; commit explicitly before external builds. Close or commit dirty documents before session end. Overlapping tool calls wait in a bounded FIFO; cancel unwanted queued requests through notifications/cancelled.\"}}");
            send_output(&output); cdx_json_output_free(&output); initialized = 1;
        } else if (string_node(&json, method, "ping")) {
            cdx_json_output_init(&output); response_start(&output, &json, id); cdx_json_raw(&output, ",\"result\":{}}"); send_output(&output); cdx_json_output_free(&output);
        } else if (!initialized) rpc_error(&json, id, -32002, "Initialize the MCP session first");
        else if (string_node(&json, method, "tools/list")) {
            cdx_json_output_init(&output); response_start(&output, &json, id); cdx_json_raw(&output, ",\"result\":"); cdx_tool_list_profile(&output, profile); cdx_json_raw(&output, "}"); send_output(&output); cdx_json_output_free(&output);
        } else if (string_node(&json, method, "tools/call")) {
            int tool_name = -1;
            int tool_arguments = -1;
            if (params < 0 || json.nodes[params].type != CDX_JSON_OBJECT)
                rpc_error(&json, id, -32602, "Tool call params must be an object");
            else {
                tool_name = cdx_json_get(&json, params, "name");
                tool_arguments = cdx_json_get(&json, params, "arguments");
                if (tool_name < 0 || json.nodes[tool_name].type != CDX_JSON_STRING)
                    rpc_error(&json, id, -32602, "Tool name must be a string");
                else if (!supported_tool(json.nodes[tool_name].string))
                    rpc_error(&json, id, -32602, "Unknown tool");
                else if (tool_arguments >= 0 && json.nodes[tool_arguments].type != CDX_JSON_OBJECT)
                    rpc_error(&json, id, -32602, "Tool arguments must be an object");
                else {
                    cdx_mcp_request request = {json, params, id, input_bytes + 1u + json.capacity * sizeof(cdx_json_node), GetTickCount64()};
                    if (cdx_mcp_queue_push(&queue, &request)) continue;
                    reject_pending(&request, &active, queue.count, -32001, "queue_full", "MCP request queue is full; this request was not started");
                }
            }
        } else rpc_error(&json, id, -32601, "Unknown MCP method");
        cdx_json_free(&json);
    }
    SetEvent(reader.stop); CancelSynchronousIo(input_thread); SetEvent(reader.consumed);
    (void)WaitForSingleObject(input_thread, 2000); CloseHandle(input_thread);
    for (iteration = 0; iteration < CDX_MCP_QUEUE_CAPACITY && queue.count; ++iteration) {
        cdx_mcp_request request;
        cdx_mcp_queue_remove(&queue, 0, &request);
        reject_pending(&request, &active, queue.count, -32800, "session_closed", "MCP session closed before request execution");
        cdx_json_free(&request.json);
    }
    if (!finish_active(&active, 1)) {
        (void)fputs("Cordonix: operation cleanup did not finish before the shutdown deadline\n", stderr);
        ExitProcess(1);
    }
    cdx_workspace_clear();
    cdx_phone_session_destroy(phone);
    CloseHandle(reader.available); CloseHandle(reader.consumed); CloseHandle(reader.stop);
    return reader.oversized ? 2 : 0;
}
static int run_request(const wchar_t *path, int upgrade) {
    FILE *file = NULL;
    char *data;
    size_t length;
    cdx_json json;
    cdx_json_output output;
    int name, arguments, result = 0;
    if (_wfopen_s(&file, path, L"rb") || !file) return 2;
    data = (char *)malloc(INPUT_LIMIT + 1u);
    if (!data) { fclose(file); return 1; }
    length = fread(data, 1, INPUT_LIMIT + 1u, file); fclose(file);
    if (!cdx_json_parse(&json, data, length)) { (void)fprintf(stderr, "%s\n", json.error); free(data); cdx_json_free(&json); return 2; }
    free(data); name = cdx_json_get(&json, 0, "operation"); arguments = cdx_json_get(&json, 0, "arguments");
    if (name < 0 || json.nodes[name].type != CDX_JSON_STRING) { cdx_json_free(&json); return 2; }
    if (upgrade && strcmp(json.nodes[name].string, "execute_program")) { cdx_json_free(&json); return 2; }
    cdx_json_output_init(&output); cdx_call(json.nodes[name].string, &json, arguments, NULL, &output);
    if (upgrade) {
        cdx_json response;
        result = !cdx_json_parse(&response, output.data, output.size) ||
            !string_node(&response, cdx_json_get(&response, 0, "status"), "completed");
        cdx_json_free(&response);
    }
    send_output(&output); cdx_json_output_free(&output); cdx_json_free(&json); cdx_workspace_clear(); return result;
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

static int check_upgrade(const wchar_t *root) {
    wchar_t *path = (wchar_t *)malloc(32768u * sizeof(wchar_t)), *part;
    DWORD length;
    size_t used;
    int result = 2;
    if (!path) return 1;
    if (root) {
        used = wcslen(root);
        if (used > 32740u || !((used > 2u && root[1] == L':' && (root[2] == L'\\' || root[2] == L'/')) || (used > 2u && root[0] == L'\\' && root[1] == L'\\')))
            goto done;
        (void)wcscpy_s(path, 32768u, root);
    } else {
        length = GetModuleFileNameW(NULL, path, 32768u);
        if (!length || length >= 32768u) goto done;
        part = wcsrchr(path, L'\\'); if (!part) goto done; *part = L'\0';
        part = wcsrchr(path, L'\\'); if (!part) goto done; *part = L'\0';
        part = wcsrchr(path, L'\\');
        if (!part || _wcsicmp(part + 1, L"versions")) {
            (void)fputs("Cordonix updates require an installed runtime or --install-root.\n", stderr); goto done;
        }
        *part = L'\0';
    }
    if (wcscat_s(path, 32768u, L"\\update-request.json")) goto done;
    result = run_request(path, 1);
done:
    free(path);
    return result;
}

int wmain(int argc, wchar_t **argv) {
    int result;
    (void)_setmode(_fileno(stdin), _O_BINARY); (void)_setmode(_fileno(stdout), _O_BINARY);
    InitializeCriticalSection(&output_lock);
    if (argc == 2 && !wcscmp(argv[1], L"--mcp")) result = run_mcp(CDX_TOOL_PROFILE_FULL);
    else if (argc == 4 && !wcscmp(argv[1], L"--mcp") && !wcscmp(argv[2], L"--tool-profile")) {
        cdx_tool_profile profile;
        if (!wcscmp(argv[3], L"full")) profile = CDX_TOOL_PROFILE_FULL;
        else if (!wcscmp(argv[3], L"local-code")) profile = CDX_TOOL_PROFILE_LOCAL_CODE;
        else { (void)fputs("Unknown tool profile; expected full or local-code.\n", stderr); profile = (cdx_tool_profile)-1; }
        result = profile == (cdx_tool_profile)-1 ? 2 : run_mcp(profile);
    }
    else if (argc == 2 && !wcscmp(argv[1], L"--phone-helper")) result = run_phone_helper();
    else if (argc == 9 && !wcscmp(argv[1], L"--screenshot-worker")) result = cdx_screenshot_worker(argc, argv);
    else if (argc == 3 && !wcscmp(argv[1], L"--vnc-password-prompt")) result = cdx_vnc_password_prompt(argc, argv);
    else if ((argc == 2 || argc == 3) && !wcscmp(argv[1], L"--phone-ui")) { FreeConsole(); result = cdx_phone_ui(argc == 3 ? argv[2] : L"", 0); }
    else if ((argc == 2 || argc == 3) && !wcscmp(argv[1], L"--phone-ui-tray")) { FreeConsole(); result = cdx_phone_ui_tray(argc == 3 ? argv[2] : L""); }
    else if (argc == 2 && !wcscmp(argv[1], L"--phone-ui-smoke")) result = cdx_phone_ui(L"", 1);
    else if (argc == 3 && !wcscmp(argv[1], L"--request")) result = run_request(argv[2], 0);
    else if (argc == 2 && !wcscmp(argv[1], L"--checkupgrade")) result = check_upgrade(NULL);
    else if (argc == 4 && !wcscmp(argv[1], L"--checkupgrade") && !wcscmp(argv[2], L"--install-root")) result = check_upgrade(argv[3]);
    else if (argc == 2 && !wcscmp(argv[1], L"--help")) { (void)puts("Usage: cdx --mcp [--tool-profile full|local-code] | --request absolute-request.json | --checkupgrade [--install-root absolute-directory] | --version"); result = 0; }
    else if (argc == 2 && !wcscmp(argv[1], L"--version")) { (void)puts(CDX_VERSION_TEXT); result = 0; }
    else { (void)fputs("Usage: cdx --mcp [--tool-profile full|local-code] | --request absolute-request.json | --phone-ui [phone-name] | --phone-ui-tray [phone-name] | --version\n", stderr); result = 2; }
    DeleteCriticalSection(&output_lock); return result;
}
