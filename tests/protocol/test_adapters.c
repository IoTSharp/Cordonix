#ifndef _WIN32
#include <pthread.h>
#define _POSIX_C_SOURCE 200809L
#endif
#include "cdx/operations.h"
#include "cdx/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

static unsigned total, passed;
static uint64_t deadline;

#ifdef _WIN32
static DWORD WINAPI cancel_after_start(void *event) {
    Sleep(100);
    SetEvent((HANDLE)event);
    return 0;
}
#else
static void *cancel_after_start(void *event) {
    struct timespec delay = {0, 100000000};
    (void)nanosleep(&delay, NULL);
    atomic_store((atomic_int *)event, 1);
    return NULL;
}
#endif

static uint64_t now_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static void record(const char *name, int success) {
    ++total; if (success) ++passed;
    printf("%s: %s\n", name, success ? "PASS" : "FAIL");
}

static cdx_path_char *native(const char *text) {
#ifdef _WIN32
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t *copy = (wchar_t *)calloc((size_t)count, sizeof(*copy));
    if (copy && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, copy, count)) { free(copy); return NULL; }
    return copy;
#else
    return strdup(text);
#endif
}

static int run(const char *program, const char *cwd, const char *const *args, size_t count) {
    cdx_exec_request request;
    cdx_exec_result result;
    cdx_path_char *argv[16] = {0};
    size_t i;
    int success;
    if (count > 16u || now_ms() >= deadline) return 0;
    memset(&request, 0, sizeof(request));
    request.program = native(program); request.cwd = native(cwd);
    for (i = 0; i < count; ++i) argv[i] = native(args[i]);
    request.args = (const cdx_path_char *const *)argv; request.arg_count = count;
    request.timeout_ms = 10000; request.output_limit_bytes = 32768;
    cdx_result_init(&result); (void)cdx_execute(&request, &result);
    printf("fixture_process pid=%lu parent=%lu created=%llu program=%s args=", (unsigned long)result.pid,
        (unsigned long)result.parent_pid, (unsigned long long)result.creation_filetime, program);
    for (i = 0; i < count; ++i) printf(" [%s]", args[i]);
    printf(" cleanup=%d\n", result.cleanup_complete);
    success = result.status == CDX_COMPLETED && result.cleanup_complete;
    if (!success) printf("fixture_process_error status=%s stderr=%.*s\n", cdx_status_name(result.status),
        (int)result.stderr_bytes.size, result.stderr_bytes.data ? (const char *)result.stderr_bytes.data : "");
    cdx_result_free(&result);
    for (i = 0; i < count; ++i) free(argv[i]);
    free((void *)request.program); free((void *)request.cwd);
    return success;
}

static int write_file(const char *root, const char *name, const unsigned char *bytes, size_t size) {
    char path[4096];
    FILE *file;
    int success;
    (void)snprintf(path, sizeof(path), "%s/%s", root, name);
#ifdef _WIN32
    {
        wchar_t *wide = native(path);
        file = NULL;
        if (wide) (void)_wfopen_s(&file, wide, L"wb");
        free(wide);
    }
#else
    file = fopen(path, "wb");
#endif
    if (!file) return 0;
    success = fwrite(bytes, 1, size, file) == size;
    return fclose(file) == 0 && success;
}

static int request_call(cdx_json *result, const char *operation, const char *program,
                         const char *cwd, const char *extra, cdx_cancel_handle cancellation) {
    cdx_json_output input, output;
    cdx_json request;
    int success = 0;
    memset(result, 0, sizeof(*result));
    if (now_ms() >= deadline) return 0;
    cdx_json_output_init(&input); cdx_json_output_init(&output);
    cdx_json_raw(&input, "{\"program\":"); cdx_json_quote(&input, program, strlen(program));
    cdx_json_raw(&input, ",\"cwd\":"); cdx_json_quote(&input, cwd, strlen(cwd));
    if (extra) cdx_json_raw(&input, extra);
    cdx_json_raw(&input, "}");
    if (cdx_json_parse(&request, input.data, input.size)) {
        cdx_adapter_call(operation, &request, 0, cancellation, &output);
        success = !output.failed && cdx_json_parse(result, output.data, output.size);
        if (!success) printf("invalid_adapter_result=%s\n", output.data ? output.data : "");
        else {
            int pid = cdx_json_get(result, 0, "pid"), parent = cdx_json_get(result, 0, "parent_pid");
            int created = cdx_json_get(result, 0, "creation_filetime"), clean = cdx_json_get(result, 0, "cleanup_complete");
            printf("adapter_process pid=%.0f parent=%.0f created=%.0f operation=%s program=%s cleanup=%d\n",
                pid >= 0 ? result->nodes[pid].number : 0, parent >= 0 ? result->nodes[parent].number : 0,
                created >= 0 ? result->nodes[created].number : 0, operation, program,
                clean >= 0 ? result->nodes[clean].boolean : 0);
        }
    }
    cdx_json_free(&request);
    cdx_json_output_free(&input); cdx_json_output_free(&output);
    return success;
}

static int equal(const cdx_json *json, int object, const char *key, const char *value) {
    int node = cdx_json_get(json, object, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_STRING && !strcmp(json->nodes[node].string, value);
}

static int field_true(const cdx_json *json, const char *key) {
    int node = cdx_json_get(json, 0, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_BOOL && json->nodes[node].boolean;
}

static size_t array_count(const cdx_json *json, const char *key) {
    int array = cdx_json_get(json, 0, key), node;
    size_t count = 0;
    if (array < 0 || json->nodes[array].type != CDX_JSON_ARRAY) return (size_t)-1;
    for (node = json->nodes[array].first; node >= 0 && count < 1001u; node = json->nodes[node].next) ++count;
    return count;
}

static int find_entry(const cdx_json *json, const char *path) {
    int array = cdx_json_get(json, 0, "entries"), node;
    size_t count = 0;
    if (array < 0) return -1;
    for (node = json->nodes[array].first; node >= 0 && count++ < 1000u; node = json->nodes[node].next)
        if (equal(json, node, "path", path)) return node;
    return -1;
}

static int search(cdx_json *result, const char *program, const char *cwd, const char *extra) {
    cdx_json_output fields;
    int success;
    cdx_json_output_init(&fields);
    cdx_json_raw(&fields, ",\"roots\":["); cdx_json_quote(&fields, cwd, strlen(cwd));
    cdx_json_raw(&fields, "]"); cdx_json_raw(&fields, extra);
    success = request_call(result, "search_text", program, cwd, fields.data, 0);
    cdx_json_output_free(&fields);
    return success;
}

int main(int argc, char **argv) {
    const char *git, *rg, *root;
    const char *init[] = {"init", "-b", "main"};
    const char *add[] = {"add", "--", "tracked old.txt", "change.txt"};
    const char *commit[] = {"-c", "user.name=Cordonix Fixture", "-c", "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false", "commit", "-m", "fixture"};
    const char *rename[] = {"mv", "--", "tracked old.txt", "renamed 中文.txt"};
    cdx_json result;
    int success, entry, pilot, git_only;
    if (argc > 1 && !strcmp(argv[1], "--no-optional-locks")) {
#ifdef _WIN32
        Sleep(2000);
#else
        struct timespec pause = {2, 0};
        (void)nanosleep(&pause, NULL);
#endif
        return 0;
    }
    if (argc < 4 || argc > 5) return 2;
    git = argv[1]; rg = argv[2]; root = argv[3];
    pilot = argc == 5 && !strcmp(argv[4], "--pilot");
    git_only = argc == 5 && !strcmp(argv[4], "--git-only");
    deadline = now_ms() + 90000u;
    success = run(git, root, init, sizeof(init) / sizeof(init[0]));
    record("git init real repository", success);
    if (!success) goto done;
    success = request_call(&result, "git_status", git, root, NULL, 0);
    record("git empty unborn branch", success && equal(&result, 0, "status", "completed") &&
        equal(&result, cdx_json_get(&result, 0, "branch"), "head", "main") && array_count(&result, "entries") == 0u);
    cdx_json_free(&result);
    if (pilot) goto done;
    success = write_file(root, "tracked old.txt", (const unsigned char *)"tracked\n", 8u) &&
              write_file(root, "change.txt", (const unsigned char *)"old\n", 4u) &&
              run(git, root, add, sizeof(add) / sizeof(add[0])) &&
              run(git, root, commit, sizeof(commit) / sizeof(commit[0])) &&
              run(git, root, rename, sizeof(rename) / sizeof(rename[0]));
    record("git tracked and renamed fixture", success);
    if (!success) goto done;
    success = write_file(root, "change.txt", (const unsigned char *)"new\n", 4u) &&
              write_file(root, "untracked 中文 space.txt", (const unsigned char *)"needle $() -needle\nneedle second\n", 32u) &&
              write_file(root, "excluded.log", (const unsigned char *)"needle\n", 7u);
    record("real text fixtures", success);
    if (!success) goto done;
    success = request_call(&result, "git_status", git, root, NULL, 0);
    entry = find_entry(&result, "renamed 中文.txt");
    record("git rename preserves both NUL paths", success && equal(&result, 0, "status", "completed") &&
        entry >= 0 && equal(&result, entry, "original_path", "tracked old.txt") && equal(&result, entry, "index_status", "R"));
    entry = find_entry(&result, "change.txt");
    record("git worktree modification", entry >= 0 && equal(&result, entry, "worktree_status", "M"));
    entry = find_entry(&result, "untracked 中文 space.txt");
    record("git untracked Unicode and spaces", entry >= 0 && equal(&result, entry, "kind", "untracked"));
    cdx_json_free(&result);
    success = request_call(&result, "git_status", git, root, ",\"untracked\":\"no\"", 0);
    record("git untracked mode", success && equal(&result, 0, "status", "completed") && array_count(&result, "entries") == 2u);
    cdx_json_free(&result);
    success = request_call(&result, "git_status", git, root, ",\"max_entries\":1", 0);
    record("git entry limit explicit", success && equal(&result, 0, "status", "output_limit") && field_true(&result, "truncated") && array_count(&result, "entries") == 1u);
    cdx_json_free(&result);
    success = request_call(&result, "git_status", git, root, ",\"output_limit_bytes\":8", 0);
    record("git byte limit explicit", success && equal(&result, 0, "status", "output_limit") && field_true(&result, "truncated") && array_count(&result, "entries") == 0u);
    cdx_json_free(&result);
    success = request_call(&result, "git_status", git, root, ",\"untracked\":\"invalid\"", 0);
    record("git invalid mode rejected", success && equal(&result, 0, "status", "validation_error"));
    cdx_json_free(&result);
    {
        char outside[4096];
        (void)snprintf(outside, sizeof(outside), "%s/..", root);
        success = request_call(&result, "git_status", git, outside, NULL, 0);
        /* The runner supplies a parent outside every Git worktree. */
        record("git non repository preserves failure", success && equal(&result, 0, "status", "failed") && array_count(&result, "entries") == 0u);
        cdx_json_free(&result);
    }
#ifndef _WIN32
    {
        const char *weird = "quote\"and\nnewline.txt";
        success = write_file(root, weird, (const unsigned char *)"needle\n", 7u) && request_call(&result, "git_status", git, root, NULL, 0);
        record("git quotes and newline remain one path", success && find_entry(&result, weird) >= 0);
        cdx_json_free(&result);
        success = write_file(root, "raw-\xff.txt", (const unsigned char *)"needle\n", 7u) && request_call(&result, "git_status", git, root, NULL, 0);
        record("git invalid UTF-8 preserves raw bytes", success && equal(&result, 0, "status", "encoding_error") &&
            cdx_json_get(&result, 0, "stdout_base64") >= 0 && array_count(&result, "entries") == 0u);
        cdx_json_free(&result);
    }
#endif
    success = request_call(&result, "git_status", argv[0], root, ",\"timeout_ms\":100", 0);
    record("adapter real subprocess timeout cleanup", success && equal(&result, 0, "status", "timed_out") && field_true(&result, "cleanup_complete"));
    cdx_json_free(&result);
    {
#ifdef _WIN32
        HANDLE cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
        HANDLE thread = cancel ? CreateThread(NULL, 0, cancel_after_start, cancel, 0, NULL) : NULL;
        success = thread != NULL;
#else
        atomic_int cancelled = ATOMIC_VAR_INIT(0);
        cdx_cancel_handle cancel = &cancelled;
        pthread_t thread;
        success = pthread_create(&thread, NULL, cancel_after_start, cancel) == 0;
#endif
        if (success) {
            success = request_call(&result, "git_status", argv[0], root, NULL, cancel);
            record("adapter active cancellation cleanup", success && equal(&result, 0, "status", "cancelled") && field_true(&result, "cleanup_complete"));
            cdx_json_free(&result);
#ifdef _WIN32
            (void)WaitForSingleObject(thread, 1000);
            CloseHandle(thread);
#else
            (void)pthread_join(thread, NULL);
#endif
            success = request_call(&result, "git_status", git, root, NULL, cancel);
            record("adapter pre cancellation", success && equal(&result, 0, "status", "cancelled"));
            cdx_json_free(&result);
        } else record("adapter cancellation thread", 0);
#ifdef _WIN32
        if (cancel) CloseHandle(cancel);
#endif
    }
    if (git_only) { puts("Linux ripgrep gate: NOT_RUN (no configured native rg)"); goto done; }
    success = search(&result, rg, root, ",\"patterns\":[\"$()\",\"-needle\"]");
    record("rg literal patterns remain arguments", success && equal(&result, 0, "status", "completed") && array_count(&result, "matches") == 1u);
    cdx_json_free(&result);
    success = search(&result, rg, root, ",\"patterns\":[\"needle\"],\"globs\":[\"*.txt\"]");
    record("rg parsed Unicode path and glob", success && equal(&result, 0, "status", "completed") && array_count(&result, "matches") >= 2u);
    cdx_json_free(&result);
    success = search(&result, rg, root, ",\"patterns\":[\"absent-exact-token\"]");
    record("rg exit one is no_match", success && equal(&result, 0, "status", "no_match") && array_count(&result, "matches") == 0u);
    cdx_json_free(&result);
    success = search(&result, rg, root, ",\"patterns\":[\"[\"],\"literal\":false");
    record("rg invalid regex is failed", success && equal(&result, 0, "status", "failed") && array_count(&result, "matches") == 0u);
    cdx_json_free(&result);
    success = search(&result, rg, root, ",\"patterns\":[\"needle\"],\"max_entries\":1");
    record("rg entry limit explicit", success && equal(&result, 0, "status", "output_limit") && field_true(&result, "truncated") && array_count(&result, "matches") == 1u);
    cdx_json_free(&result);
    success = search(&result, rg, root, ",\"patterns\":[\"needle\"],\"output_limit_bytes\":8");
    record("rg byte limit explicit", success && equal(&result, 0, "status", "output_limit") && field_true(&result, "truncated") && array_count(&result, "matches") == 0u);
    cdx_json_free(&result);
    success = search(&result, rg, root, ",\"patterns\":[\"needle\"],\"max_matches_per_file\":1");
    record("rg per file cap disclosed", success && equal(&result, 0, "status", "completed") && field_true(&result, "per_file_limit_reached"));
    cdx_json_free(&result);
    success = search(&result, rg, root, ",\"patterns\":[\"needle\"],\"encoding\":\"guess\"");
    record("rg undeclared encoding rejected", success && equal(&result, 0, "status", "validation_error"));
    cdx_json_free(&result);
    success = request_call(&result, "search_text", rg, root, ",\"patterns\":[\"needle\"],\"roots\":[\"relative\"]", 0);
    record("rg relative root rejected", success && equal(&result, 0, "status", "validation_error"));
    cdx_json_free(&result);
    {
        const unsigned char raw[] = {0xff, 'n', 'e', 'e', 'd', 'l', 'e', '\n'};
        success = write_file(root, "raw-content.txt", raw, sizeof(raw)) && search(&result, rg, root,
            ",\"patterns\":[\"needle\"],\"globs\":[\"raw-content.txt\"]");
        entry = cdx_json_get(&result, 0, "matches");
        entry = entry >= 0 ? result.nodes[entry].first : -1;
        entry = cdx_json_get(&result, entry, "lines");
        record("rg invalid text bytes stay Base64", success && equal(&result, 0, "status", "completed") &&
            equal(&result, entry, "bytes", "/25lZWRsZQo="));
        cdx_json_free(&result);
    }
    {
        const unsigned char gbk[] = {0xd6, 0xd0, 0xce, 0xc4, '\n'};
        success = write_file(root, "gbk.txt", gbk, sizeof(gbk)) && search(&result, rg, root,
            ",\"patterns\":[\"中文\"],\"globs\":[\"gbk.txt\"],\"encoding\":\"cp936\"");
        entry = cdx_json_get(&result, 0, "matches");
        entry = entry >= 0 ? result.nodes[entry].first : -1;
        entry = cdx_json_get(&result, entry, "lines");
        record("rg explicit GBK delegated conversion", success && equal(&result, 0, "status", "completed") && equal(&result, entry, "text", "中文\n"));
        cdx_json_free(&result);
    }
#ifndef _WIN32
    success = search(&result, rg, root, ",\"patterns\":[\"needle\"],\"globs\":[\"raw-*.txt\"]");
    {
        int matches = cdx_json_get(&result, 0, "matches"), found = 0;
        size_t checked = 0;
        for (entry = matches >= 0 ? result.nodes[matches].first : -1; entry >= 0 && checked++ < 10u; entry = result.nodes[entry].next) {
            int path = cdx_json_get(&result, entry, "path");
            if (cdx_json_get(&result, path, "bytes") >= 0) found = 1;
        }
        record("rg raw filename remains Base64", success && equal(&result, 0, "status", "completed") && found);
    }
    cdx_json_free(&result);
#endif
done:
    printf("%u/%u adapter checks passed%s\n", passed, total, git_only ? " (Git scope only)" : "");
    return total && passed == total ? 0 : 1;
}
