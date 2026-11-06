#define _POSIX_C_SOURCE 200809L
#include "cdx/operations.h"
#include "cdx/presentation.h"
#include "cdx/version.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define INPUT_LIMIT (1024u * 1024u)
#define SESSION_MAX_MS (24ull * 60ull * 60ull * 1000ull)
#define SESSION_MAX_REQUESTS 100000u

typedef struct active_request {
    pthread_t thread;
    atomic_int cancel, done;
    int running, params, id;
    cdx_json json;
    cdx_json_output reply;
} active_request;

static pthread_mutex_t output_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int output_failed;
static atomic_int request_cancel;
static volatile sig_atomic_t interrupted;
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Signal cancellation requires lock-free atomic int");

static unsigned long long monotonic_ms(void) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long long)now.tv_sec * 1000ull + (unsigned long long)now.tv_nsec / 1000000ull;
}

static void on_signal(int number) { (void)number; interrupted = 1; atomic_store(&request_cancel, 1); }

static int write_output(const char *data, size_t size) {
    size_t used = 0, iteration;
    unsigned long long deadline = monotonic_ms() + 2000u;
    for (iteration = 0; iteration < size + 200u && used < size && monotonic_ms() < deadline &&
         !interrupted && !atomic_load(&output_failed); ++iteration) {
        struct pollfd target = {STDOUT_FILENO, POLLOUT, 0};
        ssize_t count;
        int ready = poll(&target, 1, 20);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0 || (target.revents & (POLLERR | POLLHUP | POLLNVAL))) return 0;
        if (!(target.revents & POLLOUT)) continue;
        count = write(STDOUT_FILENO, data + used, size - used);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (count <= 0) return 0;
        used += (size_t)count;
    }
    return used == size;
}

static void send_output(cdx_json_output *output) {
    const char *error = "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Response exceeds output capacity\"}}";
    const char *data = output->failed || !output->data ? error : output->data;
    size_t size = data == error ? strlen(error) : output->size;
    (void)pthread_mutex_lock(&output_lock);
    if (!write_output(data, size) || !write_output("\n", 1)) atomic_store(&output_failed, 1);
    (void)pthread_mutex_unlock(&output_lock);
}

static void response_start(cdx_json_output *output, const cdx_json *json, int id) {
    cdx_json_raw(output, "{\"jsonrpc\":\"2.0\",\"id\":");
    if (id < 0) cdx_json_raw(output, "null");
    else if (json->nodes[id].type == CDX_JSON_STRING) cdx_json_quote(output, json->nodes[id].string, strlen(json->nodes[id].string));
    else cdx_json_rawn(output, json->nodes[id].number_text, json->nodes[id].number_length);
}

static void build_rpc_error(cdx_json_output *output, const cdx_json *json, int id, int code, const char *message);
static void rpc_error(const cdx_json *json, int id, int code, const char *message) {
    cdx_json_output output;
    build_rpc_error(&output, json, id, code, message);
    send_output(&output);
    cdx_json_output_free(&output);
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
        "workspace_close", "sftp", "http", "serial", "network", "read_files", "screenshot",
        "data_query", "find_files", "hash_file", "copy_file", "move_file"
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
        !strcmp(name, "network") || !strcmp(name, "phone") || !strcmp(name, "screenshot") ||
        !strcmp(name, "data_query") || !strcmp(name, "find_files") || !strcmp(name, "hash_file") ||
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

static int ids_equal(const cdx_json *a, int ai, const cdx_json *b, int bi) {
    if (ai < 0 || bi < 0 || a->nodes[ai].type != b->nodes[bi].type) return 0;
    if (a->nodes[ai].type == CDX_JSON_STRING) return !strcmp(a->nodes[ai].string, b->nodes[bi].string);
    return a->nodes[ai].type == CDX_JSON_NUMBER && a->nodes[ai].number_length == b->nodes[bi].number_length &&
        !memcmp(a->nodes[ai].number_text, b->nodes[bi].number_text, a->nodes[ai].number_length);
}

static void tool_response(const cdx_json *json, int params, int id, cdx_cancel_handle cancellation, cdx_json_output *reply) {
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
    cdx_json_output_init(&data);
    cdx_json_output_init(&output);
    cdx_summarize_call(json->nodes[name].string, json, arguments, summary);
    if (cdx_summary_progress(json, params, summary, &output)) {
        send_output(&output); cdx_json_output_free(&output); cdx_json_output_init(&output);
    }
    cdx_call(json->nodes[name].string, json, arguments, cancellation, &data);
    memset(&parsed, 0, sizeof(parsed));
    if (!data.failed && data.data && cdx_json_parse(&parsed, data.data, data.size)) {
        status_node = cdx_json_get(&parsed, 0, "status");
        if (status_node >= 0 && parsed.nodes[status_node].type == CDX_JSON_STRING) {
            status = parsed.nodes[status_node].string;
            is_error = tool_status_is_error(status);
        }
    }
    response_start(&output, json, id);
    cdx_json_raw(&output, ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":");
    if (data.failed || !data.data) {
        const char *error = "{\"status\":\"output_limit\"}";
        cdx_json_quote(&output, error, strlen(error));
    } else cdx_json_quote(&output, data.data, data.size);
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

static void *execution_worker(void *context) {
    active_request *active = (active_request *)context;
    tool_response(&active->json, active->params, active->id, &active->cancel, &active->reply);
    atomic_store(&active->done, 1);
    return NULL;
}

static int finish_active(active_request *active, int cancel) {
    unsigned count;
    unsigned long long deadline = monotonic_ms() + (cancel ? 10000u : 0u);
    const struct timespec delay = {0, 10000000};
    if (!active->running) return 1;
    if (cancel) atomic_store(&active->cancel, 1);
    for (count = 0; cancel && count < 1000 && !atomic_load(&active->done) && monotonic_ms() < deadline; ++count)
        (void)nanosleep(&delay, NULL);
    if (!atomic_load(&active->done)) return 0;
    (void)pthread_join(active->thread, NULL);
    send_output(&active->reply);
    cdx_json_output_free(&active->reply);
    cdx_json_free(&active->json);
    active->running = 0;
    return 1;
}

static void dispatch(const char *line, size_t length, int *initialized, active_request *active) {
    cdx_json json;
    cdx_json_output output;
    int method, id, params;
    if (!length || interrupted || atomic_load(&output_failed)) return;
    if (!cdx_json_parse(&json, line, length)) {
        rpc_error(NULL, -1, -32700, "Invalid UTF-8 JSON request");
        cdx_json_free(&json);
        return;
    }
    method = cdx_json_get(&json, 0, "method");
    id = cdx_json_get(&json, 0, "id");
    params = cdx_json_get(&json, 0, "params");
    (void)finish_active(active, 0);
    if (!string_node(&json, cdx_json_get(&json, 0, "jsonrpc"), "2.0") || method < 0 || json.nodes[method].type != CDX_JSON_STRING ||
        (id >= 0 && json.nodes[id].type != CDX_JSON_STRING && json.nodes[id].type != CDX_JSON_NUMBER)) {
        rpc_error(NULL, -1, -32600, "Invalid JSON-RPC request");
    } else if (string_node(&json, method, "notifications/cancelled")) {
        if (active->running && ids_equal(&json, cdx_json_get(&json, params, "requestId"), &active->json, active->id)) atomic_store(&active->cancel, 1);
    } else if (id < 0) {
        /* Notifications never receive JSON-RPC responses. */
    } else if (string_node(&json, method, "initialize")) {
        cdx_json_output_init(&output);
        response_start(&output, &json, id);
        cdx_json_raw(&output, ",\"result\":{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{\"tools\":{\"listChanged\":false}},\"serverInfo\":{\"name\":\"Cordonix\",\"version\":\"" CDX_VERSION "\"},\"instructions\":\"Use typed argv. workspace_edit changes only memory; commit explicitly before external builds. Close or commit dirty documents before session end.\"}}");
        send_output(&output);
        cdx_json_output_free(&output);
        *initialized = 1;
    } else if (string_node(&json, method, "ping")) {
        cdx_json_output_init(&output);
        response_start(&output, &json, id);
        cdx_json_raw(&output, ",\"result\":{}}");
        send_output(&output);
        cdx_json_output_free(&output);
    } else if (!*initialized) rpc_error(&json, id, -32002, "Initialize the MCP session first");
    else if (string_node(&json, method, "tools/list")) {
        cdx_json_output_init(&output);
        response_start(&output, &json, id);
        cdx_json_raw(&output, ",\"result\":");
        cdx_tool_list(&output);
        cdx_json_raw(&output, "}");
        send_output(&output);
        cdx_json_output_free(&output);
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
            else if (active->running)
                rpc_error(&json, id, -32001, "An operation is running; wait or cancel that request");
            else if (asynchronous_tool(json.nodes[tool_name].string)) {
                active->json = json;
                active->id = id;
                active->params = params;
                atomic_store(&active->cancel, 0);
                atomic_store(&active->done, 0);
                if (!pthread_create(&active->thread, NULL, execution_worker, active)) { active->running = 1; return; }
                rpc_error(&json, id, -32603, "Cannot start execution worker");
            } else tool_response(&json, params, id, NULL, NULL);
        }
    } else rpc_error(&json, id, -32601, "Unknown MCP method");
    cdx_json_free(&json);
}

static int run_mcp(void) {
    active_request active;
    unsigned long long deadline = monotonic_ms() + SESSION_MAX_MS;
    unsigned long long iteration;
    unsigned messages = 0;
    size_t length = 0;
    char *line = (char *)malloc(INPUT_LIMIT + 1u);
    int initialized = 0, result = 0;
    memset(&active, 0, sizeof(active));
    atomic_init(&active.cancel, 0);
    atomic_init(&active.done, 0);
    if (!line) return 1;
    for (iteration = 0; iteration < SESSION_MAX_MS / 20u + (unsigned long long)SESSION_MAX_REQUESTS * (INPUT_LIMIT + 1u) &&
        messages < SESSION_MAX_REQUESTS && monotonic_ms() < deadline && !interrupted && !atomic_load(&output_failed); ++iteration) {
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        char chunk[4096];
        ssize_t received, i;
        int ready = poll(&input, 1, 20);
        (void)finish_active(&active, 0);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0 || (input.revents & (POLLERR | POLLNVAL))) { result = 1; break; }
        if (!(input.revents & (POLLIN | POLLHUP))) continue;
        received = read(STDIN_FILENO, chunk, sizeof(chunk));
        if (received < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (received <= 0) {
            if (length) dispatch(line, length, &initialized, &active);
            break;
        }
        for (i = 0; i < received && messages < SESSION_MAX_REQUESTS && monotonic_ms() < deadline &&
             !interrupted && !atomic_load(&output_failed); ++i) {
            if (chunk[i] == '\n') { dispatch(line, length, &initialized, &active); length = 0; ++messages; }
            else if (length == INPUT_LIMIT) { result = 2; goto done; }
            else line[length++] = chunk[i];
        }
    }
done:
    free(line);
    if (!finish_active(&active, 1)) {
        (void)fputs("Cordonix: execution cleanup did not finish before the shutdown deadline\n", stderr);
        _Exit(1);
    }
    cdx_workspace_clear();
    return atomic_load(&output_failed) ? 1 : result;
}

static int run_request(const char *path) {
    FILE *file = fopen(path, "rb");
    char *data;
    size_t length;
    cdx_json json;
    cdx_json_output output;
    int name, arguments;
    if (!file) return 2;
    data = (char *)malloc(INPUT_LIMIT + 1u);
    if (!data) { (void)fclose(file); return 1; }
    length = fread(data, 1, INPUT_LIMIT + 1u, file);
    (void)fclose(file);
    if (!cdx_json_parse(&json, data, length)) { free(data); cdx_json_free(&json); return 2; }
    free(data);
    name = cdx_json_get(&json, 0, "operation");
    arguments = cdx_json_get(&json, 0, "arguments");
    if (name < 0 || json.nodes[name].type != CDX_JSON_STRING) { cdx_json_free(&json); return 2; }
    cdx_json_output_init(&output);
    cdx_call(json.nodes[name].string, &json, arguments, &request_cancel, &output);
    send_output(&output);
    cdx_json_output_free(&output);
    cdx_json_free(&json);
    cdx_workspace_clear();
    return atomic_load(&output_failed) ? 1 : 0;
}

int main(int argc, char **argv) {
    struct sigaction action;
    int flags = fcntl(STDOUT_FILENO, F_GETFL);
    int result;
    atomic_init(&output_failed, 0);
    atomic_init(&request_cancel, 0);
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);
    action.sa_handler = SIG_IGN;
    (void)sigaction(SIGPIPE, &action, NULL);
    if (flags < 0 || fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) return 1;
    if (argc == 2 && !strcmp(argv[1], "--mcp")) result = run_mcp();
    else if (argc == 3 && !strcmp(argv[1], "--request")) result = run_request(argv[2]);
    else if (argc == 2 && !strcmp(argv[1], "--version")) {
        static const char version[] = CDX_VERSION_TEXT "\n";
        result = write_output(version, sizeof(version) - 1u) ? 0 : 1;
    } else {
        (void)fputs("Usage: cdx --mcp | --request absolute-request.json | --version\n", stderr);
        result = 2;
    }
    /* stdout may share its open-file description with the invoking process. */
    if (fflush(stdout)) result = 1;
    if (fcntl(STDOUT_FILENO, F_SETFL, flags) < 0) result = 1;
    return result;
}
