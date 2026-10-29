#ifndef _WIN32
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "cdx/json.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
typedef unsigned long long ULONGLONG;
static ULONGLONG GetTickCount64(void) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (ULONGLONG)now.tv_sec * 1000u + (ULONGLONG)now.tv_nsec / 1000000u;
}
static void Sleep(unsigned ms) {
    struct timespec duration = {(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
    (void)nanosleep(&duration, NULL);
}
#define ExitProcess _Exit
#define DeleteFileW unlink
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cdx/version.h"

static unsigned passed, total;
static ULONGLONG deadline;

static void record(const char *name, int success) {
    ++total;
    if (success) ++passed;
    printf("%s: %s\n", name, success ? "PASS" : "FAIL");
}

#ifdef _WIN32
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
#else
typedef struct process_identity {
    pid_t pid, parent, group;
    unsigned long long started;
    char state;
} process_identity;

typedef struct wire_client {
    pid_t process;
    int input, output, error;
    const char *fixture;
    unsigned cleaned_fixtures;
} wire_client;

static int read_identity(pid_t pid, process_identity *identity) {
    char path[80], bytes[1024], *cursor, *token, *state;
    FILE *file;
    unsigned field;
    (void)snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    file = fopen(path, "r");
    if (!file) return 0;
    cursor = fgets(bytes, sizeof(bytes), file);
    fclose(file);
    if (!cursor || !(cursor = strrchr(bytes, ')'))) return 0;
    memset(identity, 0, sizeof(*identity)); identity->pid = pid;
    cursor += 2;
    for (field = 3; field <= 22; ++field) {
        token = strtok_r(field == 3 ? cursor : NULL, " \n", &state);
        if (!token) return 0;
        if (field == 3) identity->state = *token;
        else if (field == 4) identity->parent = (pid_t)strtol(token, NULL, 10);
        else if (field == 5) identity->group = (pid_t)strtol(token, NULL, 10);
        else if (field == 22) identity->started = strtoull(token, NULL, 10);
    }
    return identity->started != 0;
}

static int matching_identity(const process_identity *before, process_identity *after) {
    return read_identity(before->pid, after) && before->parent == after->parent &&
        before->group == after->group && before->started == after->started;
}

static unsigned fixture_children(wire_client *client, process_identity children[16]) {
    char path[128], expected[4096], actual[4096];
    process_identity server, current;
    DIR *tasks;
    struct dirent *task;
    unsigned count = 0, scanned = 0, thread_count;
    long pid;
    ULONGLONG end = GetTickCount64() + 500;
    if (!client->fixture || !realpath(client->fixture, expected) ||
        !read_identity(client->process, &server) || server.parent != getpid()) return 0;
    (void)snprintf(path, sizeof(path), "/proc/%ld/task", (long)client->process);
    tasks = opendir(path);
    if (!tasks) return 0;
    /* execute_program forks in its worker thread, whose children list is separate. */
    for (thread_count = 0; thread_count < 18 && scanned < 16 && GetTickCount64() < end && (task = readdir(tasks)); ++thread_count) {
        FILE *file;
        long thread_id;
        char *tail;
        if (*task->d_name < '0' || *task->d_name > '9') continue;
        thread_id = strtol(task->d_name, &tail, 10);
        if (*tail) continue;
        (void)snprintf(path, sizeof(path), "/proc/%ld/task/%ld/children", (long)client->process, thread_id);
        file = fopen(path, "r");
        if (!file) continue;
        for (; scanned < 16 && GetTickCount64() < end && fscanf(file, "%ld", &pid) == 1; ++scanned) {
            process_identity child;
            ssize_t length;
            if (!read_identity((pid_t)pid, &child) || child.parent != client->process || child.group != child.pid) continue;
            (void)snprintf(path, sizeof(path), "/proc/%ld/exe", pid);
            length = readlink(path, actual, sizeof(actual) - 1u);
            if (length < 0) continue;
            actual[length] = 0;
            if (strcmp(actual, expected) || !matching_identity(&child, &current) || !matching_identity(&server, &current)) continue;
            children[count++] = child;
        }
        fclose(file);
    }
    closedir(tasks);
    return count;
}

static void stop_fixture_children(wire_client *client) {
    process_identity children[16], current;
    unsigned count = fixture_children(client, children), index;
    ULONGLONG end = GetTickCount64() + 500;
    for (index = 0; index < count && GetTickCount64() < end; ++index) {
        if (matching_identity(&children[index], &current) && kill(-current.group, SIGKILL) == 0) ++client->cleaned_fixtures;
    }
}

static int wait_child(pid_t pid, int *status, unsigned timeout_ms) {
    unsigned iteration;
    ULONGLONG end = GetTickCount64() + timeout_ms;
    for (iteration = 0; iteration <= timeout_ms / 10u && GetTickCount64() < end; ++iteration) {
        pid_t got = waitpid(pid, status, WNOHANG);
        if (got == pid) return 1;
        if (got < 0 && errno != EINTR) return 0;
        Sleep(10);
    }
    return 0;
}

static int wire_start(wire_client *client, const char *program) {
    int in[2] = {-1, -1}, out[2] = {-1, -1}, error[2] = {-1, -1};
    int i;
    memset(client, 0, sizeof(*client));
    client->input = client->output = client->error = -1;
    if (pipe(in) || pipe(out) || pipe(error)) goto fail;
    client->process = fork();
    if (client->process < 0) goto fail;
    if (!client->process) {
        if (setpgid(0, 0) || dup2(in[0], 0) < 0 || dup2(out[1], 1) < 0 || dup2(error[1], 2) < 0) _exit(126);
        for (i = 0; i < 2; ++i) { close(in[i]); close(out[i]); close(error[i]); }
        execl(program, program, "--mcp", (char *)NULL);
        _exit(127);
    }
    close(in[0]); close(out[1]); close(error[1]);
    client->input = in[1]; client->output = out[0]; client->error = error[0];
    (void)fcntl(client->input, F_SETFL, O_NONBLOCK);
    (void)fcntl(client->output, F_SETFL, O_NONBLOCK);
    printf("mcp_owner_pid=%ld parent_pid=%ld\n", (long)client->process, (long)getpid());
    return 1;
fail:
    for (i = 0; i < 2; ++i) {
        if (in[i] >= 0) close(in[i]);
        if (out[i] >= 0) close(out[i]);
        if (error[i] >= 0) close(error[i]);
    }
    return 0;
}

static int wire_stop(wire_client *client) {
    int status = 0, exited = 0, graceful = 0;
    if (client->input >= 0) { close(client->input); client->input = -1; }
    if (client->process > 0) {
        exited = wait_child(client->process, &status, 4000);
        graceful = exited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    if (client->process > 0 && !exited) {
        (void)kill(client->process, SIGTERM);
        exited = wait_child(client->process, &status, 4000);
        if (!exited) {
            stop_fixture_children(client);
            (void)kill(client->process, SIGKILL);
            exited = wait_child(client->process, &status, 1000);
        }
    }
    if (exited) client->process = 0;
    if (client->output >= 0) close(client->output);
    if (client->error >= 0) close(client->error);
    return graceful;
}

static int wire_send(wire_client *client, const char *text, size_t length) {
    unsigned i;
    size_t used = 0;
    ULONGLONG end = GetTickCount64() + 2000;
    if (length > 16384) return 0;
    for (i = 0; i < 16484 && used < length && GetTickCount64() < end && GetTickCount64() < deadline; ++i) {
        struct pollfd target = {client->input, POLLOUT, 0};
        ssize_t written;
        if (poll(&target, 1, 20) <= 0) continue;
        written = write(client->input, text + used, length - used);
        if (written < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (written <= 0) return 0;
        used += (size_t)written;
    }
    return used == length;
}

static int wire_read(wire_client *client, cdx_json *json) {
    char bytes[32768];
    size_t used = 0;
    unsigned i;
    ULONGLONG end = GetTickCount64() + 4000;
    memset(json, 0, sizeof(*json));
    for (i = 0; i < 40000 && used < sizeof(bytes) && GetTickCount64() < end && GetTickCount64() < deadline; ++i) {
        struct pollfd source = {client->output, POLLIN, 0};
        ssize_t got;
        if (poll(&source, 1, 10) <= 0) continue;
        got = read(client->output, bytes + used, 1);
        if (got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (got != 1) return 0;
        if (bytes[used++] == '\n') return cdx_json_parse(json, bytes, used);
    }
    return 0;
}
#endif

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

#ifdef _WIN32
static int file_equals(const wchar_t *path, const char *expected) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    char bytes[128]; DWORD got = 0;
    int same;
    if (file == INVALID_HANDLE_VALUE) return 0;
    same = ReadFile(file, bytes, sizeof(bytes), &got, NULL) && got == strlen(expected) && memcmp(bytes, expected, got) == 0;
    CloseHandle(file);
    return same;
}
#else
static int file_equals(const char *path, const char *expected) {
    char bytes[128];
    FILE *file = fopen(path, "rb");
    size_t got;
    if (!file) return 0;
    got = fread(bytes, 1, sizeof(bytes), file);
    fclose(file);
    return got == strlen(expected) && !memcmp(bytes, expected, got);
}
#endif

static void workspace_cases(wire_client *client) {
#ifdef _WIN32
    wchar_t path[32768], *tail;
#else
    char path[4096], cwd[4000];
#endif
    char utf8_path[65536], arguments[1024], hash[65];
    static const char original[] = "old \xe4\xb8\xad\r\n";
    static const char changed[] = "new \xe4\xb8\xad\r\n";
    cdx_json_output open_args;
    cdx_json data;
    int valid, node;
    unsigned long long document_id = 0;
#ifdef _WIN32
    HANDLE file;
    DWORD written;
    if (!GetModuleFileNameW(NULL, path, _countof(path))) { record("workspace fixture create", 0); return; }
    tail = wcsrchr(path, L'\\');
    if (!tail || swprintf_s(tail + 1, _countof(path) - (size_t)(tail + 1 - path), L"cdx-stdio-%lu.txt", (unsigned long)GetCurrentProcessId()) < 0) { record("workspace fixture create", 0); return; }
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { record("workspace fixture create", 0); return; }
    valid = WriteFile(file, original, sizeof(original) - 1, &written, NULL) && written == sizeof(original) - 1;
    CloseHandle(file);
    if (!valid || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, utf8_path, sizeof(utf8_path), NULL, NULL)) { record("workspace fixture create", 0); DeleteFileW(path); return; }
#else
    int file;
    if (!getcwd(cwd, sizeof(cwd))) { record("workspace fixture create", 0); return; }
    (void)snprintf(path, sizeof(path), "%s/cdx-stdio-%ld.txt", cwd, (long)getpid());
    file = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) { record("workspace fixture create", 0); return; }
    valid = write(file, original, sizeof(original) - 1) == (ssize_t)(sizeof(original) - 1);
    close(file);
    if (!valid) { record("workspace fixture create", 0); unlink(path); return; }
    memcpy(utf8_path, path, strlen(path) + 1u);
#endif
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

typedef struct line_fixture {
#ifdef _WIN32
    wchar_t path[32768];
#else
    char path[4096];
#endif
    char utf8_path[65536];
    int owned;
} line_fixture;

static int write_line_fixture(line_fixture *fixture, const void *bytes, size_t length, int create) {
    int valid;
#ifdef _WIN32
    DWORD written = 0;
    HANDLE file = CreateFileW(fixture->path, GENERIC_WRITE, 0, NULL, create ? CREATE_NEW : TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    fixture->owned = 1;
    valid = length <= 8192 && WriteFile(file, bytes, (DWORD)length, &written, NULL) && written == length;
    CloseHandle(file);
#else
    int file = open(fixture->path, O_WRONLY | (create ? O_CREAT | O_EXCL : O_TRUNC), 0600);
    if (file < 0) return 0;
    fixture->owned = 1;
    valid = length <= 8192 && write(file, bytes, length) == (ssize_t)length;
    close(file);
#endif
    return valid;
}

static int create_line_fixture(line_fixture *fixture, unsigned id, const void *bytes, size_t length) {
#ifdef _WIN32
    wchar_t *tail;
    if (!GetModuleFileNameW(NULL, fixture->path, _countof(fixture->path))) return 0;
    tail = wcsrchr(fixture->path, L'\\');
    if (!tail || swprintf_s(tail + 1, _countof(fixture->path) - (size_t)(tail + 1 - fixture->path),
                           L"cdx-lines-%lu-%u.txt", (unsigned long)GetCurrentProcessId(), id) < 0) return 0;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, fixture->path, -1, fixture->utf8_path,
                             sizeof(fixture->utf8_path), NULL, NULL)) return 0;
#else
    char *tail;
    ssize_t size = readlink("/proc/self/exe", fixture->path, sizeof(fixture->path) - 1u);
    if (size < 0) return 0;
    fixture->path[size] = 0;
    tail = strrchr(fixture->path, '/');
    if (!tail || snprintf(tail + 1, sizeof(fixture->path) - (size_t)(tail + 1 - fixture->path),
                          "cdx-lines-%ld-%u.txt", (long)getpid(), id) < 0) return 0;
    memcpy(fixture->utf8_path, fixture->path, strlen(fixture->path) + 1u);
#endif
    return write_line_fixture(fixture, bytes, length, 1);
}

static void remove_line_fixture(line_fixture *fixture) {
    if (fixture->owned) { (void)DeleteFileW(fixture->path); fixture->owned = 0; }
}

static int call_file_tool(wire_client *client, const char *tool, const line_fixture *fixture,
                          const char *options, unsigned id, cdx_json *data) {
    cdx_json_output arguments;
    int valid;
    memset(data, 0, sizeof(*data));
    cdx_json_output_init(&arguments);
    cdx_json_raw(&arguments, "{\"path\":");
    cdx_json_quote(&arguments, fixture->utf8_path, strlen(fixture->utf8_path));
    cdx_json_raw(&arguments, options); cdx_json_raw(&arguments, "}");
    valid = !arguments.failed && tool_call(client, tool, arguments.data, id, data);
    cdx_json_output_free(&arguments);
    return valid;
}

static int number_is(const cdx_json *json, const char *key, unsigned long long expected) {
    int node = cdx_json_get(json, 0, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_NUMBER && json->nodes[node].number == (double)expected;
}

static int bool_is(const cdx_json *json, const char *key, int expected) {
    int node = cdx_json_get(json, 0, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_BOOL && json->nodes[node].boolean == expected;
}

static int page_is(const cdx_json *json, const char *text, unsigned start, unsigned lines,
                   unsigned next, int eof, int truncated, const char *source) {
    int node = cdx_json_get(json, 0, "next_line");
    int hash = cdx_json_get(json, 0, "memory_sha256");
    return string_is(json, 0, "status", "ok") && string_is(json, 0, "text", text) &&
        number_is(json, "start_line", start) && number_is(json, "lines_returned", lines) &&
        (next ? number_is(json, "next_line", next) : node >= 0 && json->nodes[node].type == CDX_JSON_NULL) &&
        bool_is(json, "eof", eof) && bool_is(json, "truncated", truncated) &&
        string_is(json, 0, "source", source) && hash >= 0 && json->nodes[hash].type == CDX_JSON_STRING &&
        strlen(json->nodes[hash].string) == 64;
}

static void line_read_cases(wire_client *client) {
    static const char mixed[] = "first\r\n\r\n\xe4\xb8\xad\nthird\rfourth";
    static const unsigned char cp936[] = {0xd6, 0xd0, 0xce, 0xc4, 13, 10, 'T'};
    static const unsigned char utf16be[] = {0xfe, 0xff, 0x4e, 0x2d, 0, 13, 0, 10, 0, 'T'};
    static const unsigned char utf8bom[] = {0xef, 0xbb, 0xbf, 'A', 10, 'B', 10};
    line_fixture *fixtures = (line_fixture *)calloc(8, sizeof(*fixtures));
    char large[8192], expected[2048], arguments[256], original_hash[65];
    size_t used = 0, wanted = 0;
    unsigned i, id = 100;
    unsigned long long document_id = 0;
    cdx_json data;
    int valid, node, all;
    if (!fixtures) { record("line read fixtures allocated", 0); return; }
    for (i = 0; i < 950 && GetTickCount64() < deadline; ++i) { memcpy(large + used, "skip\n", 5); used += 5; }
    if (i != 950) goto fixture_failure;
    memcpy(large + used, "begin\n", 6); used += 6;
    memcpy(expected + wanted, "begin\n", 6); wanted += 6;
    for (i = 0; i < 243 && GetTickCount64() < deadline; ++i) {
        memcpy(large + used, "take\n", 5); used += 5;
        memcpy(expected + wanted, "take\n", 5); wanted += 5;
    }
    if (i != 243) goto fixture_failure;
    memcpy(large + used, "last\nend\n", 9); used += 9;
    memcpy(expected + wanted, "last\n", 5); wanted += 5; expected[wanted] = 0;
    if (!create_line_fixture(&fixtures[0], 0, large, used) ||
        !create_line_fixture(&fixtures[1], 1, mixed, sizeof(mixed) - 1u) ||
        !create_line_fixture(&fixtures[2], 2, "", 0) ||
        !create_line_fixture(&fixtures[3], 3, cp936, sizeof(cp936)) ||
        !create_line_fixture(&fixtures[4], 4, utf16be, sizeof(utf16be)) ||
        !create_line_fixture(&fixtures[5], 5, utf8bom, sizeof(utf8bom)) ||
        !create_line_fixture(&fixtures[6], 6, "one\n", 4) ||
        !create_line_fixture(&fixtures[7], 7, "old\nkeep\n", 9)) goto fixture_failure;

    valid = call_file_tool(client, "read_file", &fixtures[0], ",\"start_line\":951,\"line_count\":245", id++, &data);
    record("read_file line 951 returns 245 exact lines", valid && page_is(&data, expected, 951, 245, 1196, 0, 0, "disk") &&
           bool_is(&data, "dirty_in_memory", 0) && string_is(&data, 0, "encoding", "utf-8") && string_is(&data, 0, "eol", "lf"));
    cdx_json_free(&data);

    valid = call_file_tool(client, "read_file", &fixtures[1], ",\"start_line\":2,\"line_count\":3", id++, &data);
    record("read_file preserves mixed EOL and empty lines", valid && page_is(&data, "\r\n\xe4\xb8\xad\nthird\r", 2, 3, 5, 0, 0, "disk") && string_is(&data, 0, "eol", "mixed"));
    cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[1], ",\"start_line\":5", id++, &data);
    record("read_file includes unterminated final line", valid && page_is(&data, "fourth", 5, 1, 0, 1, 0, "disk"));
    cdx_json_free(&data);

    valid = call_file_tool(client, "read_file", &fixtures[6], "", id++, &data);
    all = valid && page_is(&data, "one\n", 1, 1, 0, 1, 0, "disk"); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[6], ",\"start_line\":2", id++, &data);
    all = all && valid && page_is(&data, "", 2, 0, 0, 1, 0, "disk"); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[6], ",\"start_line\":99", id++, &data);
    record("read_file trailing newline and beyond EOF", all && valid && page_is(&data, "", 99, 0, 0, 1, 0, "disk"));
    cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[2], "", id++, &data);
    record("read_file empty file has zero lines", valid && page_is(&data, "", 1, 0, 0, 1, 0, "disk")); cdx_json_free(&data);

    valid = call_file_tool(client, "read_file", &fixtures[3], ",\"encoding\":\"cp936\",\"line_count\":1", id++, &data);
    record("read_file explicit CP936 line decoding", valid && page_is(&data, "\xe4\xb8\xad\xe6\x96\x87\r\n", 1, 1, 2, 0, 0, "disk") &&
           string_is(&data, 0, "encoding", "cp936") && string_is(&data, 0, "eol", "crlf")); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[4], ",\"start_line\":2", id++, &data);
    record("read_file recognizes UTF16BE BOM", valid && page_is(&data, "T", 2, 1, 0, 1, 0, "disk") && string_is(&data, 0, "encoding", "utf-16be")); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[5], "", id++, &data);
    record("read_file removes UTF8 BOM from text", valid && page_is(&data, "A\nB\n", 1, 2, 0, 1, 0, "disk") && string_is(&data, 0, "encoding", "utf-8-bom")); cdx_json_free(&data);

    valid = call_file_tool(client, "read_file", &fixtures[3], "", id++, &data);
    all = valid && string_is(&data, 0, "status", "encoding_error"); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[1], ",\"encoding\":\"unsupported\"", id++, &data);
    record("read_file rejects guessed and unknown encoding", all && valid && string_is(&data, 0, "status", "validation_error")); cdx_json_free(&data);
    {
        static const char *const invalid[] = {",\"start_line\":0", ",\"line_count\":0", ",\"line_count\":1001", ",\"max_bytes\":0", ",\"max_bytes\":262145"};
        all = 1;
        for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]) && GetTickCount64() < deadline; ++i) {
            valid = call_file_tool(client, "read_file", &fixtures[1], invalid[i], id++, &data);
            all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
        }
        record("read_file validates line and byte bounds", all && i == sizeof(invalid) / sizeof(invalid[0]));
    }

    valid = call_file_tool(client, "read_file", &fixtures[1], ",\"line_count\":5,\"max_bytes\":7", id++, &data);
    all = valid && page_is(&data, "first\r\n", 1, 1, 2, 0, 1, "disk"); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[1], ",\"start_line\":2,\"max_bytes\":2", id++, &data);
    record("read_file byte budget keeps whole line pages", all && valid && page_is(&data, "\r\n", 2, 1, 3, 0, 1, "disk")); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[1], ",\"start_line\":3,\"max_bytes\":3", id++, &data);
    record("read_file oversized first line is not split", valid && string_is(&data, 0, "status", "line_too_long")); cdx_json_free(&data);

    valid = call_file_tool(client, "workspace_open", &fixtures[7], "", id++, &data);
    node = valid ? cdx_json_get(&data, 0, "document_id") : -1;
    if (node >= 0 && data.nodes[node].type == CDX_JSON_NUMBER) document_id = (unsigned long long)data.nodes[node].number;
    node = valid ? cdx_json_get(&data, 0, "memory_sha256") : -1;
    all = valid && document_id && node >= 0 && data.nodes[node].type == CDX_JSON_STRING && strlen(data.nodes[node].string) == 64;
    if (all) memcpy(original_hash, data.nodes[node].string, sizeof(original_hash));
    cdx_json_free(&data);
    if (!all) { record("line workspace fixture opens", 0); goto cleanup; }
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"start_line\":1,\"offset\":0}", document_id);
    valid = tool_call(client, "workspace_read", arguments, id++, &data);
    all = valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"max_bytes\":10,\"limit\":10}", document_id);
    valid = tool_call(client, "workspace_read", arguments, id++, &data);
    record("workspace line mode rejects byte mode mixing", all && valid && string_is(&data, 0, "status", "validation_error")); cdx_json_free(&data);

    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"expected_sha256\":\"%s\",\"old\":\"old\",\"new\":\"new\",\"dry_run\":false}", document_id, original_hash);
    valid = tool_call(client, "workspace_edit", arguments, id++, &data);
    all = valid && string_is(&data, 0, "status", "ok"); cdx_json_free(&data);
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"line_count\":1}", document_id);
    valid = tool_call(client, "workspace_read", arguments, id++, &data);
    all = all && valid && page_is(&data, "new\n", 1, 1, 2, 0, 0, "memory") && bool_is(&data, "dirty_in_memory", 1) && !string_is(&data, 0, "memory_sha256", original_hash); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[7], ",\"line_count\":1", id++, &data);
    record("memory line reads and disk snapshots stay separate", all && valid && page_is(&data, "old\n", 1, 1, 2, 0, 0, "disk") && bool_is(&data, "dirty_in_memory", 0) && string_is(&data, 0, "memory_sha256", original_hash)); cdx_json_free(&data);
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"discard\":true}", document_id);
    valid = tool_call(client, "workspace_close", arguments, id++, &data); cdx_json_free(&data);
    if (valid) document_id = 0;

    all = write_line_fixture(&fixtures[7], "disk\nkeep\n", 10, 0);
    valid = call_file_tool(client, "read_file", &fixtures[7], ",\"line_count\":1", id++, &data);
    record("read_file refreshes disk without a memory cache", all && valid && page_is(&data, "disk\n", 1, 1, 2, 0, 0, "disk") && !string_is(&data, 0, "memory_sha256", original_hash)); cdx_json_free(&data);
    all = 1;
    for (i = 0; i < 17 && GetTickCount64() < deadline; ++i) {
        line_fixture one = {0};
        if (!create_line_fixture(&one, 100 + i, "x\n", 2)) { remove_line_fixture(&one); all = 0; break; }
        valid = call_file_tool(client, "read_file", &one, "", id++, &data);
        all = all && valid && page_is(&data, "x\n", 1, 1, 0, 1, 0, "disk"); cdx_json_free(&data);
        remove_line_fixture(&one);
    }
    record("read_file does not consume workspace document slots", all && i == 17);
    goto cleanup;

fixture_failure:
    record("line read fixtures created", 0);
cleanup:
    if (document_id) {
        (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"discard\":true}", document_id);
        (void)tool_call(client, "workspace_close", arguments, id++, &data); cdx_json_free(&data);
    }
    for (i = 0; i < 8; ++i) remove_line_fixture(&fixtures[i]);
    free(fixtures);
}

static int whole_text_is(const cdx_json *json, const char *text, unsigned start, unsigned lines,
                         const char *source, const char *mode, int dirty) {
    return page_is(json, text, start, lines, 0, 1, 0, source) &&
        string_is(json, 0, "mode", mode) && bool_is(json, "dirty_in_memory", dirty);
}

static int whole_text_limit_is(const cdx_json *json, unsigned required, unsigned limit) {
    return string_is(json, 0, "status", "output_limit") && number_is(json, "bytes_required", required) &&
        number_is(json, "max_bytes", limit) && cdx_json_get(json, 0, "text") < 0;
}

static void tail_raw_cases(wire_client *client) {
    static const char mixed[] = "head\r\n\r\n\xe4\xb8\xad\nthird\rfourth";
    static const unsigned char cp936[] = {0xd6, 0xd0, 0xce, 0xc4, 13, 10, 'T'};
    static const unsigned char utf16be[] = {0xfe, 0xff, 0x4e, 0x2d, 0, 13, 0, 10, 0, 'T'};
    static const unsigned char utf8bom[] = {0xef, 0xbb, 0xbf, 'A', 10, 'B', 10};
    static const char *const invalid_modes[] = {
        ",\"tail_lines\":1,\"start_line\":1", ",\"tail_lines\":1,\"line_count\":1",
        ",\"tail_lines\":1,\"offset\":0", ",\"tail_lines\":1,\"limit\":10",
        ",\"raw\":true,\"tail_lines\":1", ",\"raw\":true,\"start_line\":1",
        ",\"raw\":true,\"line_count\":1", ",\"raw\":true,\"offset\":0", ",\"raw\":true,\"limit\":10"
    };
    line_fixture *fixtures = (line_fixture *)calloc(7, sizeof(*fixtures));
    char large[8192], tail[512], arguments[256], original_hash[65];
    size_t used = 0, tail_size = 0;
    unsigned i, id = 300;
    unsigned long long document_id = 0;
    cdx_json data;
    int valid, node, all;
    if (!fixtures) { record("tail/raw fixtures allocated", 0); return; }
    for (i = 0; i < 1001 && GetTickCount64() < deadline; ++i) { memcpy(large + used, "skip\n", 5); used += 5; }
    if (i != 1001) goto fixture_failure;
    memcpy(tail + tail_size, "begin\n", 6); tail_size += 6;
    for (i = 0; i < 53 && GetTickCount64() < deadline; ++i) { memcpy(tail + tail_size, "take\n", 5); tail_size += 5; }
    if (i != 53) goto fixture_failure;
    memcpy(tail + tail_size, "last\n", 5); tail_size += 5; tail[tail_size] = 0;
    memcpy(large + used, tail, tail_size); used += tail_size; large[used] = 0;
    if (!create_line_fixture(&fixtures[0], 200, large, used) ||
        !create_line_fixture(&fixtures[1], 201, mixed, sizeof(mixed) - 1u) ||
        !create_line_fixture(&fixtures[2], 202, "", 0) ||
        !create_line_fixture(&fixtures[3], 203, cp936, sizeof(cp936)) ||
        !create_line_fixture(&fixtures[4], 204, utf16be, sizeof(utf16be)) ||
        !create_line_fixture(&fixtures[5], 205, utf8bom, sizeof(utf8bom)) ||
        !create_line_fixture(&fixtures[6], 206, "head\nold\n", 9)) goto fixture_failure;

    valid = call_file_tool(client, "read_file", &fixtures[0], ",\"tail_lines\":55", id++, &data);
    record("read_file tail55 returns exact last 55 lines", valid && whole_text_is(&data, tail, 1002, 55, "disk", "tail", 0) &&
           string_is(&data, 0, "eol", "lf") && string_is(&data, 0, "encoding", "utf-8")); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[1], ",\"tail_lines\":55", id++, &data);
    record("read_file tail above count returns whole document", valid && whole_text_is(&data, mixed, 1, 5, "disk", "tail", 0)); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[1], ",\"tail_lines\":4", id++, &data);
    record("read_file tail preserves empty mixed EOL final line", valid && whole_text_is(&data, "\r\n\xe4\xb8\xad\nthird\rfourth", 2, 4, "disk", "tail", 0) && string_is(&data, 0, "eol", "mixed")); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[2], ",\"tail_lines\":55", id++, &data);
    all = valid && whole_text_is(&data, "", 1, 0, "disk", "tail", 0); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[5], ",\"tail_lines\":1", id++, &data);
    record("read_file tail handles empty and trailing newline", all && valid && whole_text_is(&data, "B\n", 2, 1, "disk", "tail", 0) && string_is(&data, 0, "encoding", "utf-8-bom")); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[0], ",\"tail_lines\":55,\"max_bytes\":275", id++, &data);
    all = valid && whole_text_limit_is(&data, 276, 275); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[0], ",\"tail_lines\":55,\"max_bytes\":276", id++, &data);
    record("read_file tail budget fails whole then accepts increase", all && valid && whole_text_is(&data, tail, 1002, 55, "disk", "tail", 0)); cdx_json_free(&data);

    valid = call_file_tool(client, "read_file", &fixtures[0], ",\"raw\":true", id++, &data);
    record("read_file raw returns all 1056 lines", valid && whole_text_is(&data, large, 1, 1056, "disk", "raw", 0)); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[2], ",\"raw\":true", id++, &data);
    record("read_file raw empty document has zero lines", valid && whole_text_is(&data, "", 1, 0, "disk", "raw", 0)); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[3], ",\"raw\":true,\"encoding\":\"cp936\"", id++, &data);
    all = valid && whole_text_is(&data, "\xe4\xb8\xad\xe6\x96\x87\r\nT", 1, 2, "disk", "raw", 0) && string_is(&data, 0, "encoding", "cp936"); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[4], ",\"raw\":true", id++, &data);
    all = all && valid && whole_text_is(&data, "\xe4\xb8\xad\r\nT", 1, 2, "disk", "raw", 0) && string_is(&data, 0, "encoding", "utf-16be"); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[5], ",\"raw\":true", id++, &data);
    record("read_file raw preserves decoded encodings without BOM", all && valid && whole_text_is(&data, "A\nB\n", 1, 2, "disk", "raw", 0) && string_is(&data, 0, "encoding", "utf-8-bom")); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[3], ",\"raw\":true,\"encoding\":\"cp936\",\"max_bytes\":8", id++, &data);
    all = valid && whole_text_limit_is(&data, 9, 8); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[3], ",\"raw\":true,\"encoding\":\"cp936\",\"max_bytes\":9", id++, &data);
    record("read_file raw budget uses decoded UTF8 bytes", all && valid && whole_text_is(&data, "\xe4\xb8\xad\xe6\x96\x87\r\nT", 1, 2, "disk", "raw", 0)); cdx_json_free(&data);
    all = 1;
    for (i = 0; i < sizeof(invalid_modes) / sizeof(invalid_modes[0]) && GetTickCount64() < deadline; ++i) {
        valid = call_file_tool(client, "read_file", &fixtures[1], invalid_modes[i], id++, &data);
        all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
    }
    record("read_file rejects conflicting raw tail line byte modes", all && i == sizeof(invalid_modes) / sizeof(invalid_modes[0]));
    {
        static const char *const invalid[] = {
            ",\"tail_lines\":0", ",\"tail_lines\":1001", ",\"tail_lines\":-1", ",\"tail_lines\":1.5",
            ",\"tail_lines\":\"2\"", ",\"tail_lines\":true", ",\"raw\":1", ",\"raw\":\"true\"", ",\"raw\":null"
        };
        all = 1;
        for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]) && GetTickCount64() < deadline; ++i) {
            valid = call_file_tool(client, "read_file", &fixtures[1], invalid[i], id++, &data);
            all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
        }
        record("read_file validates tail bounds and raw boolean", all && i == sizeof(invalid) / sizeof(invalid[0]));
    }

    valid = call_file_tool(client, "workspace_open", &fixtures[6], "", id++, &data);
    node = valid ? cdx_json_get(&data, 0, "document_id") : -1;
    if (node >= 0 && data.nodes[node].type == CDX_JSON_NUMBER) document_id = (unsigned long long)data.nodes[node].number;
    node = valid ? cdx_json_get(&data, 0, "memory_sha256") : -1;
    all = valid && document_id && node >= 0 && data.nodes[node].type == CDX_JSON_STRING && strlen(data.nodes[node].string) == 64;
    if (all) memcpy(original_hash, data.nodes[node].string, sizeof(original_hash));
    cdx_json_free(&data);
    if (!all) { record("tail/raw workspace fixture opens", 0); goto cleanup; }
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"expected_sha256\":\"%s\",\"old\":\"old\",\"new\":\"new\",\"dry_run\":false}", document_id, original_hash);
    valid = tool_call(client, "workspace_edit", arguments, id++, &data);
    all = valid && string_is(&data, 0, "status", "ok"); cdx_json_free(&data);
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"raw\":true,\"max_bytes\":8}", document_id);
    valid = tool_call(client, "workspace_read", arguments, id++, &data);
    all = all && valid && whole_text_limit_is(&data, 9, 8); cdx_json_free(&data);
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"raw\":true}", document_id);
    valid = tool_call(client, "workspace_read", arguments, id++, &data);
    all = all && valid && whole_text_is(&data, "head\nnew\n", 1, 2, "memory", "raw", 1) && !string_is(&data, 0, "memory_sha256", original_hash); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[6], ",\"raw\":true", id++, &data);
    record("workspace raw returns dirty memory distinct from disk", all && valid && whole_text_is(&data, "head\nold\n", 1, 2, "disk", "raw", 0) && string_is(&data, 0, "memory_sha256", original_hash)); cdx_json_free(&data);
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"tail_lines\":1,\"max_bytes\":3}", document_id);
    valid = tool_call(client, "workspace_read", arguments, id++, &data);
    all = valid && whole_text_limit_is(&data, 4, 3); cdx_json_free(&data);
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"tail_lines\":1}", document_id);
    valid = tool_call(client, "workspace_read", arguments, id++, &data);
    all = all && valid && whole_text_is(&data, "new\n", 2, 1, "memory", "tail", 1) && !string_is(&data, 0, "memory_sha256", original_hash); cdx_json_free(&data);
    valid = call_file_tool(client, "read_file", &fixtures[6], ",\"tail_lines\":1", id++, &data);
    record("workspace tail returns dirty memory distinct from disk", all && valid && whole_text_is(&data, "old\n", 2, 1, "disk", "tail", 0) && string_is(&data, 0, "memory_sha256", original_hash)); cdx_json_free(&data);
    all = 1;
    for (i = 0; i < sizeof(invalid_modes) / sizeof(invalid_modes[0]) && GetTickCount64() < deadline; ++i) {
        (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu%s}", document_id, invalid_modes[i]);
        valid = tool_call(client, "workspace_read", arguments, id++, &data);
        all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
    }
    (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"raw\":false,\"line_count\":1}", document_id);
    valid = tool_call(client, "workspace_read", arguments, id++, &data);
    record("workspace validates modes and raw false selects lines", all && i == sizeof(invalid_modes) / sizeof(invalid_modes[0]) && valid &&
           page_is(&data, "head\n", 1, 1, 2, 0, 0, "memory") && string_is(&data, 0, "mode", "lines") && bool_is(&data, "dirty_in_memory", 1)); cdx_json_free(&data);
    goto cleanup;

fixture_failure:
    record("tail/raw fixtures created", 0);
cleanup:
    if (document_id) {
        (void)snprintf(arguments, sizeof(arguments), "{\"document_id\":%llu,\"discard\":true}", document_id);
        (void)tool_call(client, "workspace_close", arguments, id++, &data); cdx_json_free(&data);
    }
    for (i = 0; i < 7; ++i) remove_line_fixture(&fixtures[i]);
    free(fixtures);
}

static int filesystem_root(line_fixture *fixture) {
#ifdef _WIN32
    wchar_t *tail;
    if (!GetModuleFileNameW(NULL, fixture->path, _countof(fixture->path))) return 0;
    tail = wcsrchr(fixture->path, L'\\');
    if (!tail || swprintf_s(tail + 1, _countof(fixture->path) - (size_t)(tail + 1 - fixture->path),
                           L"cdx-fs-%lu \x4e2d\x6587 \x7a7a\x683c", (unsigned long)GetCurrentProcessId()) < 0) return 0;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, fixture->path, -1, fixture->utf8_path,
                             sizeof(fixture->utf8_path), NULL, NULL)) return 0;
    fixture->owned = CreateDirectoryW(fixture->path, NULL) != 0;
#else
    char *tail;
    ssize_t size = readlink("/proc/self/exe", fixture->path, sizeof(fixture->path) - 1u);
    int written;
    size_t capacity;
    if (size < 0) return 0;
    fixture->path[size] = 0;
    tail = strrchr(fixture->path, '/');
    if (!tail) return 0;
    capacity = sizeof(fixture->path) - (size_t)(tail + 1 - fixture->path);
    written = snprintf(tail + 1, capacity, "cdx-fs-%ld \xe4\xb8\xad\xe6\x96\x87 \xe7\xa9\xba\xe6\xa0\xbc", (long)getpid());
    if (written < 0 || (size_t)written >= capacity) return 0;
    memcpy(fixture->utf8_path, fixture->path, strlen(fixture->path) + 1u);
    fixture->owned = mkdir(fixture->path, 0700) == 0;
#endif
    return fixture->owned;
}

static int filesystem_child(line_fixture *fixture, const line_fixture *root, const char *name) {
    int written = snprintf(fixture->utf8_path, sizeof(fixture->utf8_path), "%s%s%s", root->utf8_path,
#ifdef _WIN32
                           "\\",
#else
                           "/",
#endif
                           name);
    if (written < 0 || (size_t)written >= sizeof(fixture->utf8_path)) return 0;
#ifdef _WIN32
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fixture->utf8_path, -1,
                                fixture->path, _countof(fixture->path)) != 0;
#else
    if ((size_t)written >= sizeof(fixture->path)) return 0;
    memcpy(fixture->path, fixture->utf8_path, (size_t)written + 1u);
    return 1;
#endif
}

static int make_fixture_directory(line_fixture *fixture) {
#ifdef _WIN32
    fixture->owned = CreateDirectoryW(fixture->path, NULL) != 0;
#else
    fixture->owned = mkdir(fixture->path, 0700) == 0;
#endif
    return fixture->owned;
}

static void remove_fixture_directory(line_fixture *fixture) {
    if (!fixture->owned) return;
#ifdef _WIN32
    (void)RemoveDirectoryW(fixture->path);
#else
    (void)rmdir(fixture->path);
#endif
    fixture->owned = 0;
}

static int directory_entry(const cdx_json *json, const char *name) {
    int entries = cdx_json_get(json, 0, "entries"), entry;
    unsigned count;
    if (entries < 0 || json->nodes[entries].type != CDX_JSON_ARRAY) return -1;
    for (entry = json->nodes[entries].first, count = 0; entry >= 0 && count < 1000 && GetTickCount64() < deadline;
         entry = json->nodes[entry].next, ++count)
        if (string_is(json, entry, "name", name)) return entry;
    return -1;
}

static int entry_metadata(const cdx_json *json, const char *name, const char *type, int size, int hidden, int system) {
    int entry = directory_entry(json, name);
    int size_node = cdx_json_get(json, entry, "size_bytes");
    int hidden_node = cdx_json_get(json, entry, "hidden");
    int system_node = cdx_json_get(json, entry, "system");
    return entry >= 0 && string_is(json, entry, "type", type) && size_node >= 0 &&
        (size < 0 ? json->nodes[size_node].type == CDX_JSON_NULL :
         json->nodes[size_node].type == CDX_JSON_NUMBER && json->nodes[size_node].number == size) &&
        hidden_node >= 0 && json->nodes[hidden_node].type == CDX_JSON_BOOL && json->nodes[hidden_node].boolean == hidden &&
        system_node >= 0 && json->nodes[system_node].type == CDX_JSON_BOOL && json->nodes[system_node].boolean == system;
}

static int directory_result(const cdx_json *json, const char *path, unsigned returned, int truncated, int hidden) {
    int entries = cdx_json_get(json, 0, "entries"), entry;
    unsigned count = 0;
    if (entries < 0 || json->nodes[entries].type != CDX_JSON_ARRAY) return 0;
    for (entry = json->nodes[entries].first; entry >= 0 && count < 1001 && GetTickCount64() < deadline;
         entry = json->nodes[entry].next) ++count;
    return string_is(json, 0, "status", "ok") && string_is(json, 0, "path", path) &&
        string_is(json, 0, "type", "directory") && number_is(json, "returned", returned) && count == returned &&
        bool_is(json, "truncated", truncated) && bool_is(json, "include_hidden", hidden) &&
        directory_entry(json, ".") < 0 && directory_entry(json, "..") < 0;
}

static void filesystem_cases(wire_client *client) {
    static const char name[] = "data \xe6\x96\x87\xe4\xbb\xb6.txt";
#ifdef _WIN32
    static const char hidden_name[] = "hidden.txt", system_name[] = "system.txt";
#else
    static const char hidden_name[] = ".hidden.txt", system_name[] = ".system.txt";
#endif
    line_fixture *paths = (line_fixture *)calloc(10, sizeof(*paths));
    cdx_json data;
    unsigned i, id = 500;
    int valid, all;
    if (!paths) { record("filesystem fixtures allocated", 0); return; }
    if (!filesystem_root(&paths[0]) || !filesystem_child(&paths[1], &paths[0], name) ||
        !filesystem_child(&paths[2], &paths[0], "nested") || !filesystem_child(&paths[3], &paths[2], "sentinel.txt") ||
        !filesystem_child(&paths[4], &paths[0], "empty") || !filesystem_child(&paths[5], &paths[0], hidden_name) ||
        !filesystem_child(&paths[6], &paths[0], system_name) ||
        !filesystem_child(&paths[7], &paths[0], "nested/.././data \xe6\x96\x87\xe4\xbb\xb6.txt") ||
        !filesystem_child(&paths[8], &paths[0], "missing.txt") || !filesystem_child(&paths[9], &paths[0], "nested/../.") ||
        !make_fixture_directory(&paths[2]) || !make_fixture_directory(&paths[4]) ||
        !write_line_fixture(&paths[1], "abc\n", 4, 1) || !write_line_fixture(&paths[3], "secret", 6, 1) ||
        !write_line_fixture(&paths[5], "h", 1, 1) || !write_line_fixture(&paths[6], "s", 1, 1)) goto fixture_failure;
#ifdef _WIN32
    if (!SetFileAttributesW(paths[5].path, FILE_ATTRIBUTE_HIDDEN) ||
        !SetFileAttributesW(paths[6].path, FILE_ATTRIBUTE_SYSTEM)) goto fixture_failure;
#endif

    valid = call_file_tool(client, "resolve_path", &paths[7], "", id++, &data);
    record("resolve_path normalizes Unicode spaced file path", valid && string_is(&data, 0, "status", "ok") &&
           string_is(&data, 0, "path", paths[1].utf8_path) && string_is(&data, 0, "type", "file")); cdx_json_free(&data);
    valid = call_file_tool(client, "resolve_path", &paths[9], "", id++, &data);
    record("resolve_path normalizes directory dot segments", valid && string_is(&data, 0, "status", "ok") &&
           string_is(&data, 0, "path", paths[0].utf8_path) && string_is(&data, 0, "type", "directory")); cdx_json_free(&data);
    valid = call_file_tool(client, "list_directory", &paths[0], "", id++, &data);
    record("list_directory reports visible names types sizes", valid && directory_result(&data, paths[0].utf8_path, 3, 0, 0) &&
           entry_metadata(&data, name, "file", 4, 0, 0) && entry_metadata(&data, "nested", "directory", -1, 0, 0) &&
           entry_metadata(&data, "empty", "directory", -1, 0, 0) && directory_entry(&data, hidden_name) < 0 && directory_entry(&data, system_name) < 0);
    all = valid && directory_entry(&data, "sentinel.txt") < 0; cdx_json_free(&data);
    valid = call_file_tool(client, "list_directory", &paths[2], "", id++, &data);
    record("list_directory does not recurse into nested directories", all && valid && directory_result(&data, paths[2].utf8_path, 1, 0, 0) &&
           entry_metadata(&data, "sentinel.txt", "file", 6, 0, 0)); cdx_json_free(&data);
    valid = call_file_tool(client, "list_directory", &paths[0], ",\"include_hidden\":true", id++, &data);
    all = valid && directory_result(&data, paths[0].utf8_path, 5, 0, 1) && entry_metadata(&data, hidden_name, "file", 1, 1, 0);
#ifdef _WIN32
    all = all && entry_metadata(&data, system_name, "file", 1, 0, 1);
#else
    all = all && entry_metadata(&data, system_name, "file", 1, 1, 0);
#endif
    record("list_directory includes platform hidden system flags", all); cdx_json_free(&data);
    valid = call_file_tool(client, "list_directory", &paths[4], "", id++, &data);
    record("list_directory empty directory succeeds", valid && directory_result(&data, paths[4].utf8_path, 0, 0, 0)); cdx_json_free(&data);
    valid = call_file_tool(client, "list_directory", &paths[1], "", id++, &data);
    record("list_directory rejects ordinary file root", valid && string_is(&data, 0, "status", "not_directory")); cdx_json_free(&data);
    valid = call_file_tool(client, "list_directory", &paths[0], ",\"max_entries\":1", id++, &data);
    record("list_directory signals remaining visible entries", valid && directory_result(&data, paths[0].utf8_path, 1, 1, 0) &&
           (directory_entry(&data, name) >= 0 || directory_entry(&data, "nested") >= 0 || directory_entry(&data, "empty") >= 0)); cdx_json_free(&data);
    valid = call_file_tool(client, "list_directory", &paths[0], ",\"max_entries\":3", id++, &data);
    all = valid && directory_result(&data, paths[0].utf8_path, 3, 0, 0); cdx_json_free(&data);
    valid = call_file_tool(client, "list_directory", &paths[0], ",\"include_hidden\":true,\"max_entries\":5", id++, &data);
    record("list_directory exact visible bound is not truncated", all && valid && directory_result(&data, paths[0].utf8_path, 5, 0, 1)); cdx_json_free(&data);
    valid = call_file_tool(client, "resolve_path", &paths[8], "", id++, &data);
    all = valid && string_is(&data, 0, "status", "not_found"); cdx_json_free(&data);
    valid = call_file_tool(client, "list_directory", &paths[8], "", id++, &data);
    record("filesystem missing paths stay not_found", all && valid && string_is(&data, 0, "status", "not_found")); cdx_json_free(&data);
    {
        static const char *const tools[] = {"resolve_path", "list_directory"};
        line_fixture malformed = {0};
        all = 1;
        for (i = 0; i < 2 && GetTickCount64() < deadline; ++i) {
            valid = tool_call(client, tools[i], "{\"path\":\"relative\"}", id++, &data);
            all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
            valid = filesystem_child(&malformed, &paths[0], "$(Get-Content x)") && call_file_tool(client, tools[i], &malformed, "", id++, &data);
            all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
            valid = filesystem_child(&malformed, &paths[0], "bad\nname") && call_file_tool(client, tools[i], &malformed, "", id++, &data);
            all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
        }
        record("filesystem rejects relative and code-shaped paths", all && i == 2);
    }
    {
        static const char *const invalid[] = {",\"include_hidden\":\"false\"", ",\"max_entries\":0", ",\"max_entries\":1001", ",\"max_entries\":true", ",\"max_entries\":1.5"};
        all = 1;
        for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]) && GetTickCount64() < deadline; ++i) {
            valid = call_file_tool(client, "list_directory", &paths[0], invalid[i], id++, &data);
            all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
        }
        all = all && i == sizeof(invalid) / sizeof(invalid[0]);
        valid = tool_call(client, "resolve_path", "{\"path\":7}", id++, &data);
        all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
        valid = tool_call(client, "list_directory", "{\"path\":7}", id++, &data);
        record("filesystem validates typed path boolean and bounds", all && valid && string_is(&data, 0, "status", "validation_error")); cdx_json_free(&data);
    }
    goto cleanup;

fixture_failure:
    record("filesystem fixtures created", 0);
cleanup:
#ifdef _WIN32
    if (paths[5].owned) (void)SetFileAttributesW(paths[5].path, FILE_ATTRIBUTE_NORMAL);
    if (paths[6].owned) (void)SetFileAttributesW(paths[6].path, FILE_ATTRIBUTE_NORMAL);
#endif
    remove_line_fixture(&paths[1]); remove_line_fixture(&paths[3]);
    remove_line_fixture(&paths[5]); remove_line_fixture(&paths[6]);
    remove_fixture_directory(&paths[2]); remove_fixture_directory(&paths[4]); remove_fixture_directory(&paths[0]);
    free(paths);
}

static int fixture_bytes_equal(const line_fixture *fixture, const unsigned char *expected, size_t length) {
    unsigned char bytes[64];
#ifdef _WIN32
    DWORD got = 0;
    HANDLE file = CreateFileW(fixture->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    int valid;
    if (file == INVALID_HANDLE_VALUE) return 0;
    valid = ReadFile(file, bytes, sizeof(bytes), &got, NULL) && got == length && !memcmp(bytes, expected, length);
    CloseHandle(file);
    return valid;
#else
    FILE *file = fopen(fixture->path, "rb");
    size_t got;
    if (!file) return 0;
    got = fread(bytes, 1, sizeof(bytes), file); fclose(file);
    return got == length && !memcmp(bytes, expected, length);
#endif
}

static int create_large_verify_fixture(line_fixture *fixture) {
    unsigned char block[8192];
    unsigned i;
    int valid = 1;
    ULONGLONG end = GetTickCount64() + 10000;
#ifdef _WIN32
    HANDLE file = CreateFileW(fixture->path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
#else
    int file = open(fixture->path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) return 0;
#endif
    fixture->owned = 1;
    memset(block, 'x', sizeof(block));
    for (i = 0; i < 1024 && GetTickCount64() < end && GetTickCount64() < deadline; ++i) {
#ifdef _WIN32
        DWORD written = 0;
        valid = WriteFile(file, block, sizeof(block), &written, NULL) && written == sizeof(block);
#else
        valid = write(file, block, sizeof(block)) == (ssize_t)sizeof(block);
#endif
        if (!valid) break;
    }
#ifdef _WIN32
    CloseHandle(file);
#else
    close(file);
#endif
    return valid && i == 1024;
}

static int verify_arguments(cdx_json_output *arguments, const char *const *paths, unsigned count,
                             const char *encoding, const char *root, const char *options) {
    unsigned i;
    cdx_json_output_init(arguments); cdx_json_raw(arguments, "{\"paths\":[");
    for (i = 0; i < count && i < 33 && GetTickCount64() < deadline; ++i) {
        if (i) cdx_json_raw(arguments, ",");
        cdx_json_quote(arguments, paths[i], strlen(paths[i]));
    }
    cdx_json_raw(arguments, "]");
    if (encoding) { cdx_json_raw(arguments, ",\"encoding\":"); cdx_json_quote(arguments, encoding, strlen(encoding)); }
    if (root) { cdx_json_raw(arguments, ",\"allowed_root\":"); cdx_json_quote(arguments, root, strlen(root)); }
    cdx_json_raw(arguments, options); cdx_json_raw(arguments, "}");
    return i == count && !arguments->failed;
}

static int call_verify(wire_client *client, const char *const *paths, unsigned count, const char *encoding,
                        const char *root, const char *options, unsigned id, cdx_json *data) {
    cdx_json_output arguments;
    int valid;
    memset(data, 0, sizeof(*data));
    valid = verify_arguments(&arguments, paths, count, encoding, root, options) &&
        tool_call(client, "verify_encoding", arguments.data, id, data);
    cdx_json_output_free(&arguments);
    return valid;
}

static int verification_entry(const cdx_json *json, unsigned index) {
    int array = cdx_json_get(json, 0, "results"), entry;
    unsigned i;
    if (array < 0 || json->nodes[array].type != CDX_JSON_ARRAY) return -1;
    for (entry = json->nodes[array].first, i = 0; entry >= 0 && i < index && i < 32 && GetTickCount64() < deadline; ++i) entry = json->nodes[entry].next;
    return entry;
}

static int verification_summary(const cdx_json *json, const char *status, unsigned files, unsigned checked,
                                 unsigned passed_count, unsigned failed_count, unsigned not_run, const char *aggregate) {
    int entry;
    unsigned count = 0;
    for (entry = verification_entry(json, 0); entry >= 0 && count < 33 && GetTickCount64() < deadline; entry = json->nodes[entry].next) ++count;
    return string_is(json, 0, "status", status) && number_is(json, "files", files) && number_is(json, "checked", checked) &&
        number_is(json, "passed", passed_count) && number_is(json, "failed", failed_count) && number_is(json, "not_run", not_run) &&
        bool_is(json, "written_to_disk", 0) && string_is(json, 0, "strict_decode", aggregate) &&
        string_is(json, 0, "no_nul", aggregate) && string_is(json, 0, "roundtrip", aggregate) && count == files;
}

static int stopped_verification_is(const cdx_json *json, const char *status) {
    const char *keys[] = {"checked", "passed", "failed"};
    unsigned values[3], i;
    int valid;
    for (i = 0; i < 3 && GetTickCount64() < deadline; ++i) {
        int node = cdx_json_get(json, 0, keys[i]);
        if (node < 0 || json->nodes[node].type != CDX_JSON_NUMBER || json->nodes[node].number < 0 ||
            json->nodes[node].number > 32 || floor(json->nodes[node].number) != json->nodes[node].number) return 0;
        values[i] = (unsigned)json->nodes[node].number;
    }
    if (i != 3 || values[0] >= 32 || values[1] + values[2] != values[0]) return 0;
    valid = verification_summary(json, status, 32, values[0], values[1], values[2], 32 - values[0], "UNKNOWN");
    for (i = values[0]; i < 32 && GetTickCount64() < deadline; ++i) {
        int entry = verification_entry(json, i);
        valid = valid && string_is(json, entry, "status", "not_run") && string_is(json, entry, "verification", "NOT_RUN");
    }
    return valid && i == 32;
}

static int initialize_client(wire_client *client, const char *program) {
    static const char request[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}\n";
    cdx_json response;
    int valid;
    memset(&response, 0, sizeof(response));
    if (!wire_start(client, program)) return 0;
    valid = wire_send(client, request, sizeof(request) - 1u) && wire_read(client, &response);
    cdx_json_free(&response);
    return valid;
}

static void verify_cancellation_case(const char *program, const char *const *paths, int eof) {
    wire_client client;
    cdx_json_output arguments, request;
    cdx_json response, data;
    int valid = initialize_client(&client, program), graceful;
    cdx_json_output_init(&request); cdx_json_output_init(&arguments);
    memset(&response, 0, sizeof(response)); memset(&data, 0, sizeof(data));
    if (valid) valid = verify_arguments(&arguments, paths, 32, "utf-8", NULL, ",\"timeout_ms\":60000");
    if (valid) {
        cdx_json_raw(&request, "{\"jsonrpc\":\"2.0\",\"id\":900,\"method\":\"tools/call\",\"params\":{\"name\":\"verify_encoding\",\"arguments\":");
        cdx_json_raw(&request, arguments.data); cdx_json_raw(&request, "}}\n");
        if (!eof) cdx_json_raw(&request, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":900}}\n");
        valid = !request.failed && wire_send(&client, request.data, request.size);
    }
    if (valid && eof) {
#ifdef _WIN32
        CloseHandle(client.input); client.input = NULL;
#else
        close(client.input); client.input = -1;
#endif
    }
    valid = valid && wire_read(&client, &response) && number_is(&response, "id", 900) && wire_tool_data(&response, &data) &&
        stopped_verification_is(&data, "cancelled");
    graceful = wire_stop(&client);
    record(eof ? "MCP EOF cancels verification with NOT_RUN entries" : "MCP notification cancels verification with NOT_RUN entries", valid && graceful);
    cdx_json_free(&response); cdx_json_free(&data);
    cdx_json_output_free(&arguments); cdx_json_output_free(&request);
}

static int document_loads(wire_client *client, unsigned id, unsigned long long *loads) {
    cdx_json data;
    int valid = tool_call(client, "tool_info", "{}", id, &data);
    int node = valid ? cdx_json_get(&data, 0, "document_loads") : -1;
    valid = node >= 0 && data.nodes[node].type == CDX_JSON_NUMBER;
    if (valid) *loads = (unsigned long long)data.nodes[node].number;
    cdx_json_free(&data);
    return valid;
}

static void verify_encoding_cases(wire_client *client, const char *program) {
    static const unsigned char cp936[] = {0xd6, 0xd0, 0xce, 0xc4, 13, 10, 'A', 13, 10};
    static const unsigned char invalid_cp936[] = {0x81}, nul_text[] = {'A', 0, 'B'};
    static const unsigned char utf8bom[] = {0xef, 0xbb, 0xbf, 'A', 10};
    static const unsigned char utf16le[] = {0xff, 0xfe, 'A', 0, 13, 0, 10, 0};
    static const unsigned char utf16be[] = {0xfe, 0xff, 0, 'A', 0, 13, 0, 10};
    static const char cp936_hash[] = "6985fdf80aa87af783f20ab353fbebbd3f7348e7e78bfc8960daaafc5adf3f9a";
    line_fixture *fixtures = (line_fixture *)calloc(17, sizeof(*fixtures));
    const char *batch[33];
    char name[32];
    unsigned id = 700, i;
    unsigned long long before = 0, after = 0;
    cdx_json data;
    int valid, all, entry;
    if (!fixtures) { record("verify_encoding fixtures allocated", 0); return; }
    if (!filesystem_root(&fixtures[0])) goto fixture_failure;
    for (i = 1; i <= 7 && GetTickCount64() < deadline; ++i) {
        (void)snprintf(name, sizeof(name), "cp936-%u.txt", i);
        if (!filesystem_child(&fixtures[i], &fixtures[0], name) || !write_line_fixture(&fixtures[i], cp936, sizeof(cp936), 1)) goto fixture_failure;
        batch[i - 1u] = fixtures[i].utf8_path;
    }
    if (i != 8 || !filesystem_child(&fixtures[8], &fixtures[0], "invalid.txt") ||
        !filesystem_child(&fixtures[9], &fixtures[0], "nul.txt") || !filesystem_child(&fixtures[10], &fixtures[0], "missing.txt") ||
        !filesystem_child(&fixtures[11], &fixtures[0], "utf8-bom.txt") || !filesystem_child(&fixtures[12], &fixtures[0], "utf16le.txt") ||
        !filesystem_child(&fixtures[13], &fixtures[0], "utf16be.txt") || !filesystem_child(&fixtures[14], &fixtures[0], "nested") ||
        !filesystem_child(&fixtures[15], &fixtures[0], "nested/../cp936-1.txt") || !filesystem_child(&fixtures[16], &fixtures[0], "large.txt") ||
        !write_line_fixture(&fixtures[8], invalid_cp936, sizeof(invalid_cp936), 1) ||
        !write_line_fixture(&fixtures[9], nul_text, sizeof(nul_text), 1) || !write_line_fixture(&fixtures[11], utf8bom, sizeof(utf8bom), 1) ||
        !write_line_fixture(&fixtures[12], utf16le, sizeof(utf16le), 1) || !write_line_fixture(&fixtures[13], utf16be, sizeof(utf16be), 1) ||
        !make_fixture_directory(&fixtures[14]) || !create_large_verify_fixture(&fixtures[16])) goto fixture_failure;

    valid = call_verify(client, batch, 7, "cp936", fixtures[0].utf8_path, ",\"expected_count\":7", id++, &data);
    all = valid && verification_summary(&data, "ok", 7, 7, 7, 0, 0, "PASS") && string_is(&data, 0, "encoding", "cp936");
    for (i = 0; i < 7 && GetTickCount64() < deadline; ++i) {
        int bytes;
        entry = verification_entry(&data, i); bytes = cdx_json_get(&data, entry, "bytes");
        all = all && entry >= 0 && string_is(&data, entry, "path", batch[i]) && string_is(&data, entry, "status", "ok") &&
            string_is(&data, entry, "verification", "PASS") && string_is(&data, entry, "encoding", "cp936") &&
            bytes >= 0 && data.nodes[bytes].type == CDX_JSON_NUMBER && data.nodes[bytes].number == sizeof(cp936) &&
            string_is(&data, entry, "sha256", cp936_hash) && fixture_bytes_equal(&fixtures[i + 1u], cp936, sizeof(cp936));
    }
    record("verify_encoding seven CP936 files preserve bytes and hash", all && i == 7); cdx_json_free(&data);
    batch[0] = fixtures[1].utf8_path; batch[1] = fixtures[1].utf8_path;
    valid = call_verify(client, batch, 2, "gbk", NULL, ",\"expected_count\":2", id++, &data);
    record("verify_encoding GBK alias retains duplicate path entries", valid && verification_summary(&data, "ok", 2, 2, 2, 0, 0, "PASS") &&
           string_is(&data, 0, "encoding", "cp936") && string_is(&data, verification_entry(&data, 1), "path", batch[1])); cdx_json_free(&data);
    batch[0] = fixtures[1].utf8_path; batch[1] = fixtures[8].utf8_path; batch[2] = fixtures[9].utf8_path; batch[3] = fixtures[10].utf8_path;
    valid = call_verify(client, batch, 4, "cp936", NULL, "", id++, &data);
    all = valid && verification_summary(&data, "failed", 4, 4, 1, 3, 0, "UNKNOWN") &&
        string_is(&data, verification_entry(&data, 0), "verification", "PASS") &&
        string_is(&data, verification_entry(&data, 1), "status", "encoding_error") &&
        string_is(&data, verification_entry(&data, 2), "status", "encoding_error") &&
        string_is(&data, verification_entry(&data, 3), "status", "not_found");
    for (i = 1; i < 4 && GetTickCount64() < deadline; ++i) {
        entry = verification_entry(&data, i);
        all = all && string_is(&data, entry, "verification", "FAIL") && cdx_json_get(&data, entry, "sha256") < 0 && cdx_json_get(&data, entry, "bytes") < 0;
    }
    if (!all) {
        int summary_status = cdx_json_get(&data, 0, "status");
        int files_node = cdx_json_get(&data, 0, "files");
        int checked_node = cdx_json_get(&data, 0, "checked");
        int passed_node = cdx_json_get(&data, 0, "passed");
        int failed_node = cdx_json_get(&data, 0, "failed");
        int not_run_node = cdx_json_get(&data, 0, "not_run");
        (void)fprintf(stderr, "verify_encoding mixed summary: status=%s files=%.0f checked=%.0f passed=%.0f failed=%.0f not_run=%.0f\n",
            summary_status >= 0 ? data.nodes[summary_status].string : "<missing>",
            files_node >= 0 ? data.nodes[files_node].number : -1.0,
            checked_node >= 0 ? data.nodes[checked_node].number : -1.0,
            passed_node >= 0 ? data.nodes[passed_node].number : -1.0,
            failed_node >= 0 ? data.nodes[failed_node].number : -1.0,
            not_run_node >= 0 ? data.nodes[not_run_node].number : -1.0);
        for (i = 0; i < 4; ++i) {
            int status_node, verification_node;
            entry = verification_entry(&data, i);
            status_node = entry >= 0 ? cdx_json_get(&data, entry, "status") : -1;
            verification_node = entry >= 0 ? cdx_json_get(&data, entry, "verification") : -1;
            (void)fprintf(stderr, "verify_encoding mixed entry%u: status=%s verification=%s\n", i,
                status_node >= 0 ? data.nodes[status_node].string : "<missing>",
                verification_node >= 0 ? data.nodes[verification_node].string : "<missing>");
        }
    }
    record("verify_encoding mixed failures keep aggregate UNKNOWN", all && i == 4 && fixture_bytes_equal(&fixtures[8], invalid_cp936, sizeof(invalid_cp936)) && fixture_bytes_equal(&fixtures[9], nul_text, sizeof(nul_text))); cdx_json_free(&data);
    batch[0] = fixtures[11].utf8_path;
    valid = call_verify(client, batch, 1, "cp936", NULL, "", id++, &data);
    entry = verification_entry(&data, 0);
    record("verify_encoding rejects BOM overriding declared CP936", valid && verification_summary(&data, "failed", 1, 1, 0, 1, 0, "UNKNOWN") &&
           string_is(&data, entry, "status", "encoding_mismatch") && string_is(&data, entry, "verification", "FAIL") &&
           string_is(&data, entry, "encoding", "utf-8-bom") && cdx_json_get(&data, entry, "sha256") >= 0 && cdx_json_get(&data, entry, "bytes") >= 0); cdx_json_free(&data);
    valid = call_verify(client, batch, 1, "utf-8", NULL, "", id++, &data);
    all = valid && verification_summary(&data, "ok", 1, 1, 1, 0, 0, "PASS") && string_is(&data, verification_entry(&data, 0), "encoding", "utf-8-bom"); cdx_json_free(&data);
    valid = call_verify(client, batch, 1, "utf-8-bom", NULL, "", id++, &data);
    record("verify_encoding accepts declared UTF8 BOM compatibility", all && valid && verification_summary(&data, "ok", 1, 1, 1, 0, 0, "PASS") && fixture_bytes_equal(&fixtures[11], utf8bom, sizeof(utf8bom))); cdx_json_free(&data);
    batch[0] = fixtures[12].utf8_path;
    valid = call_verify(client, batch, 1, "utf-16-le", NULL, "", id++, &data);
    all = valid && verification_summary(&data, "ok", 1, 1, 1, 0, 0, "PASS") && string_is(&data, 0, "encoding", "utf-16le"); cdx_json_free(&data);
    batch[0] = fixtures[13].utf8_path;
    valid = call_verify(client, batch, 1, "utf-16-be", NULL, "", id++, &data);
    record("verify_encoding strict UTF16 keeps BOM and EOL", all && valid && verification_summary(&data, "ok", 1, 1, 1, 0, 0, "PASS") &&
           string_is(&data, 0, "encoding", "utf-16be") && fixture_bytes_equal(&fixtures[12], utf16le, sizeof(utf16le)) && fixture_bytes_equal(&fixtures[13], utf16be, sizeof(utf16be))); cdx_json_free(&data);

    all = document_loads(client, id++, &before);
    batch[0] = fixtures[1].utf8_path; batch[1] = fixtures[2].utf8_path;
    valid = call_verify(client, batch, 2, "cp936", NULL, ",\"expected_count\":1", id++, &data);
    all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
    valid = call_verify(client, batch, 0, "cp936", NULL, "", id++, &data);
    all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
    for (i = 0; i < 33 && GetTickCount64() < deadline; ++i) batch[i] = fixtures[1].utf8_path;
    if (i != 33) goto budget_failure;
    valid = call_verify(client, batch, 33, "cp936", NULL, "", id++, &data);
    all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
    record("verify_encoding count preflight opens zero documents", all && document_loads(client, id++, &after) && before == after);
    {
        line_fixture outside = {0};
        static const char sibling_suffix[] = "-sibling/file.txt";
        size_t root_length = strlen(fixtures[0].utf8_path);
        all = document_loads(client, id++, &before);
        if (root_length + sizeof(sibling_suffix) > sizeof(outside.utf8_path)) goto fixture_failure;
        memcpy(outside.utf8_path, fixtures[0].utf8_path, root_length);
        memcpy(outside.utf8_path + root_length, sibling_suffix, sizeof(sibling_suffix));
        batch[0] = fixtures[1].utf8_path; batch[1] = outside.utf8_path;
        valid = call_verify(client, batch, 2, "cp936", fixtures[0].utf8_path, "", id++, &data);
        all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
        valid = filesystem_child(&outside, &fixtures[0], "../outside.txt");
        valid = valid && call_verify(client, batch, 2, "cp936", fixtures[0].utf8_path, "", id++, &data);
        all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
        record("verify_encoding root boundary preflights whole batch", all && document_loads(client, id++, &after) && before == after);
    }
    {
        cdx_json_output arguments;
        all = document_loads(client, id++, &before);
        cdx_json_output_init(&arguments); cdx_json_raw(&arguments, "{\"encoding\":\"cp936\",\"paths\":[");
        cdx_json_quote(&arguments, fixtures[1].utf8_path, strlen(fixtures[1].utf8_path)); cdx_json_raw(&arguments, ",7]}");
        valid = !arguments.failed && tool_call(client, "verify_encoding", arguments.data, id++, &data);
        all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data); cdx_json_output_free(&arguments);
        batch[0] = fixtures[1].utf8_path; batch[1] = "relative.txt";
        valid = call_verify(client, batch, 2, "cp936", NULL, "", id++, &data);
        all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
        valid = call_verify(client, batch, 1, "auto", NULL, "", id++, &data);
        all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
        valid = call_verify(client, batch, 1, "invalid-codepage", NULL, "", id++, &data);
        all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
        valid = call_verify(client, batch, 1, NULL, NULL, "", id++, &data);
        all = all && valid && string_is(&data, 0, "status", "validation_error"); cdx_json_free(&data);
        record("verify_encoding typed path and explicit encoding preflight", all && document_loads(client, id++, &after) && before == after);
    }
    batch[0] = fixtures[15].utf8_path;
    valid = filesystem_child(&fixtures[10], &fixtures[0], "nested/..") &&
        call_verify(client, batch, 1, "cp936", fixtures[10].utf8_path, "", id++, &data);
    record("verify_encoding accepts normalized path within root", valid && verification_summary(&data, "ok", 1, 1, 1, 0, 0, "PASS") &&
           string_is(&data, verification_entry(&data, 0), "path", batch[0]) && string_is(&data, verification_entry(&data, 0), "sha256", cp936_hash)); cdx_json_free(&data);
    for (i = 0; i < 32 && GetTickCount64() < deadline; ++i) batch[i] = fixtures[1].utf8_path;
    if (i != 32) goto budget_failure;
    valid = call_verify(client, batch, 32, "cp936", NULL, ",\"expected_count\":32", id++, &data);
    record("verify_encoding full batch releases temporary documents", valid && verification_summary(&data, "ok", 32, 32, 32, 0, 0, "PASS")); cdx_json_free(&data);
    for (i = 0; i < 32 && GetTickCount64() < deadline; ++i) batch[i] = fixtures[16].utf8_path;
    if (i != 32) goto budget_failure;
    valid = call_verify(client, batch, 32, "utf-8", NULL, ",\"timeout_ms\":1", id++, &data);
    record("verify_encoding timeout keeps remaining entries NOT_RUN", valid && stopped_verification_is(&data, "timed_out"));
    cdx_json_free(&data);
    verify_cancellation_case(program, batch, 0);
    verify_cancellation_case(program, batch, 1);
    goto cleanup;

fixture_failure:
    record("verify_encoding fixtures created", 0);
    goto cleanup;
budget_failure:
    record("verify_encoding suite remains within time budget", 0);
cleanup:
    for (i = 1; i < 17; ++i) if (i != 14) remove_line_fixture(&fixtures[i]);
    remove_fixture_directory(&fixtures[14]); remove_fixture_directory(&fixtures[0]);
    free(fixtures);
}

static void wire_execute(wire_client *client, const char *fixture, const char *mode, int id) {
    cdx_json_output request;
    char number[32];
#ifndef _WIN32
    client->fixture = fixture;
#endif
    cdx_json_output_init(&request);
    cdx_json_raw(&request, "{\"jsonrpc\":\"2.0\",\"id\":");
    (void)snprintf(number, sizeof(number), "%d", id); cdx_json_raw(&request, number);
    cdx_json_raw(&request, ",\"method\":\"tools/call\",\"params\":{\"name\":\"execute_program\",\"arguments\":{\"program\":");
    cdx_json_quote(&request, fixture, strlen(fixture));
    cdx_json_raw(&request, ",\"args\":["); cdx_json_quote(&request, mode, strlen(mode));
#ifdef _WIN32
    cdx_json_raw(&request, "],\"cwd\":\"C:\\\\Windows\",\"timeout_ms\":3000}}}\n");
#else
    cdx_json_raw(&request, "],\"cwd\":\"/\",\"timeout_ms\":3000}}}\n");
#endif
    if (request.failed || !wire_send(client, request.data, request.size)) ExitProcess(3);
    cdx_json_output_free(&request);
}

#ifndef _WIN32
static int await_fixture(wire_client *client, process_identity *identity) {
    unsigned iteration;
    ULONGLONG end = GetTickCount64() + 1000;
    for (iteration = 0; iteration < 100 && GetTickCount64() < end; ++iteration) {
        process_identity children[16];
        if (fixture_children(client, children)) { *identity = children[0]; return 1; }
        Sleep(10);
    }
    return 0;
}

static int fixture_inactive(const process_identity *identity) {
    process_identity current;
    return !read_identity(identity->pid, &current) || current.started != identity->started ||
        current.state == 'Z' || current.state == 'X';
}

static void blocked_output_case(const char *program, const char *fixture, int terminate) {
    static const char list[] = "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/list\"}\n";
    wire_client client;
    process_identity child;
    char requests[sizeof(list) * 8u];
    size_t used = 0;
    unsigned index;
    int valid, status = 0, exited;
    ULONGLONG started;
    valid = initialize_client(&client, program);
    if (valid) valid = fcntl(client.output, F_SETPIPE_SZ, 4096) == 4096;
    if (valid) { wire_execute(&client, fixture, "sleep", 50); valid = await_fixture(&client, &child); }
    for (index = 0; index < 8; ++index) { memcpy(requests + used, list, sizeof(list) - 1u); used += sizeof(list) - 1u; }
    started = GetTickCount64();
    if (valid) valid = wire_send(&client, requests, used);
    if (valid && terminate) { Sleep(100); valid = kill(client.process, SIGTERM) == 0; }
    exited = valid && wait_child(client.process, &status, 4500);
    if (exited) client.process = 0;
    record(terminate ? "MCP blocked stdout SIGTERM stops dispatch" : "MCP blocked stdout stops buffered dispatch",
           exited && WIFEXITED(status) && WEXITSTATUS(status) != 0 && GetTickCount64() - started < 4500 && fixture_inactive(&child));
    (void)wire_stop(&client);
}

static void stopped_server_case(const char *program, const char *fixture) {
    wire_client client;
    process_identity child, current;
    int valid = initialize_client(&client, program), graceful;
    ULONGLONG started;
    if (valid) { wire_execute(&client, fixture, "sleep", 51); valid = await_fixture(&client, &child); }
    if (valid) valid = matching_identity(&child, &current) && kill(child.pid, SIGSTOP) == 0 && kill(client.process, SIGSTOP) == 0;
    started = GetTickCount64();
    graceful = wire_stop(&client);
    record("MCP forced test cleanup owns fixture group", valid && !graceful && client.process == 0 &&
           client.cleaned_fixtures == 1 && fixture_inactive(&child) && GetTickCount64() - started < 10000);
}

static void stdout_flags_case(const char *program) {
    int pipes[2], status = 0, flags, valid;
    pid_t child;
    char bytes[128];
    static const char expected[] = CDX_VERSION_TEXT "\n";
    ssize_t length;
    if (pipe(pipes)) { record("CLI restores shared stdout flags", 0); return; }
    flags = fcntl(pipes[1], F_GETFL);
    child = fork();
    if (!child) {
        if (dup2(pipes[1], STDOUT_FILENO) < 0) _exit(126);
        close(pipes[0]); close(pipes[1]);
        execl(program, program, "--version", (char *)NULL); _exit(127);
    }
    valid = child > 0 && wait_child(child, &status, 2000);
    if (!valid && child > 0) { (void)kill(child, SIGKILL); (void)wait_child(child, &status, 1000); }
    valid = valid && WIFEXITED(status) && WEXITSTATUS(status) == 0 && fcntl(pipes[1], F_GETFL) == flags;
    close(pipes[1]);
    length = fcntl(pipes[0], F_SETFL, O_NONBLOCK) == 0 ? read(pipes[0], bytes, sizeof(bytes)) : -1;
    close(pipes[0]);
    record("CLI restores shared stdout flags", valid && length == (ssize_t)(sizeof(expected) - 1u) &&
           !memcmp(bytes, expected, sizeof(expected) - 1u));
}
#endif

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
    record("MCP tools list", node >= 0 && response.nodes[node].type == CDX_JSON_ARRAY);
    {
        int tool = node >= 0 ? response.nodes[node].first : -1;
        unsigned count = 0;
        int titles_valid = 1, execution_title = 0;
        for (; tool >= 0 && count < 15; tool = response.nodes[tool].next, ++count) {
            int title = cdx_json_get(&response, tool, "title");
            if (title < 0 || response.nodes[title].type != CDX_JSON_STRING || !*response.nodes[title].string) titles_valid = 0;
            if (string_is(&response, tool, "name", "execute_program")) execution_title = string_is(&response, tool, "title", "执行本地程序");
        }
        record("MCP fourteen tools expose human-readable titles", count == 14 && titles_valid && execution_title);
    }
    cdx_json_free(&response);
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
    line_read_cases(&client);
    tail_raw_cases(&client);
    filesystem_cases(&client);
    verify_encoding_cases(&client, program);
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
    wire_execute(&client, fixture, "sleep", 13);
    Sleep(150);
#ifdef _WIN32
    CloseHandle(client.input); client.input = NULL;
#else
    close(client.input); client.input = -1;
#endif
    valid = wire_read(&client, &response);
    memset(&data, 0, sizeof(data));
    valid = valid && wire_tool_data(&response, &data);
    node = valid ? cdx_json_get(&data, 0, "cleanup_complete") : -1;
    record("MCP EOF cancels the active process", valid && string_is(&data, 0, "status", "cancelled") && node >= 0 && data.nodes[node].boolean);
    cdx_json_free(&response); cdx_json_free(&data);
    record("MCP EOF exits cleanly", wire_stop(&client));
#ifndef _WIN32
    blocked_output_case(program, fixture, 0);
    blocked_output_case(program, fixture, 1);
    stopped_server_case(program, fixture);
    stdout_flags_case(program);
#endif
    printf("{\"status\":\"%s\",\"passed\":%u,\"total\":%u}\n", passed == total ? "PASS" : "FAIL", passed, total);
    return passed == total ? 0 : 1;
}


int main(int argc, char **argv) {
#ifndef _WIN32
    (void)signal(SIGPIPE, SIG_IGN);
#endif
    if (argc != 3 && argc != 4) { fprintf(stderr, "Usage: cdx-stdio-tests cdx.exe fixture.exe [--pilot]\n"); return 2; }
    return wire_suite(argv[1], argv[2], argc == 4 && strcmp(argv[3], "--pilot") == 0);
}
