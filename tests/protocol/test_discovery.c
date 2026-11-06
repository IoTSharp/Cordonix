#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "cdx/operations.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

void cdx_discovery_call(const cdx_json *, int, cdx_cancel_handle, cdx_json_output *);
const char *cdx_discovery_tool_schema(void);

static unsigned total, passed, not_run;

static void check(const char *name, int success) {
    ++total;
    if (success) ++passed;
    printf("%s: %s\n", name, success ? "PASS" : "FAIL");
}

static void check_result(const char *name, int success, const cdx_json *result) {
    int status, count, returned, complete, error;
    check(name, success);
    if (success) return;
    status = cdx_json_get(result, 0, "status"); count = cdx_json_get(result, 0, "total_files");
    returned = cdx_json_get(result, 0, "returned"); complete = cdx_json_get(result, 0, "scan_complete");
    error = cdx_json_get(result, 0, "stderr");
    printf("actual_discovery_result status=%s total_files=%.0f returned=%.0f scan_complete=%d stderr=%s\n",
        status >= 0 && result->nodes[status].type == CDX_JSON_STRING ? result->nodes[status].string : "<missing>",
        count >= 0 && result->nodes[count].type == CDX_JSON_NUMBER ? result->nodes[count].number : -1.0,
        returned >= 0 && result->nodes[returned].type == CDX_JSON_NUMBER ? result->nodes[returned].number : -1.0,
        complete >= 0 && result->nodes[complete].type == CDX_JSON_BOOL ? result->nodes[complete].boolean : -1,
        error >= 0 && result->nodes[error].type == CDX_JSON_STRING ? result->nodes[error].string : "<missing>");
}

#ifdef _WIN32
static wchar_t *native(const char *path) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *copy;
    if (!count) return NULL;
    copy = (wchar_t *)malloc((size_t)count * sizeof(*copy));
    if (copy && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, copy, count)) {
        free(copy); return NULL;
    }
    return copy;
}
#endif

static int make_directory(const char *path) {
#ifdef _WIN32
    wchar_t *wide = native(path);
    int result = wide ? _wmkdir(wide) : -1;
    free(wide); return result == 0;
#else
    return mkdir(path, 0700) == 0;
#endif
}

static int write_fixture(const char *root, const char *name) {
    char path[4096];
    FILE *file = NULL;
    int success;
    (void)snprintf(path, sizeof(path), "%s/%s", root, name);
#ifdef _WIN32
    {
        wchar_t *wide = native(path);
        if (wide) (void)_wfopen_s(&file, wide, L"wb");
        free(wide);
    }
#else
    file = fopen(path, "wb");
#endif
    if (!file) return 0;
    success = fwrite("fixture\n", 1, 8u, file) == 8u;
    return fclose(file) == 0 && success;
}

static void remove_fixture(const char *root, const char *name) {
    char path[4096];
    (void)snprintf(path, sizeof(path), "%s/%s", root, name);
#ifdef _WIN32
    {
        wchar_t *wide = native(path);
        if (wide) (void)_wremove(wide);
        free(wide);
    }
#else
    (void)unlink(path);
#endif
}

static int equal(const cdx_json *json, const char *key, const char *value) {
    int node = cdx_json_get(json, 0, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_STRING && !strcmp(json->nodes[node].string, value);
}

static int bool_field_is(const cdx_json *json, const char *key) {
    int node = cdx_json_get(json, 0, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_BOOL && json->nodes[node].boolean;
}

static int number(const cdx_json *json, const char *key, size_t value) {
    int node = cdx_json_get(json, 0, key);
    return node >= 0 && json->nodes[node].type == CDX_JSON_NUMBER && json->nodes[node].number == (double)value;
}

static const char *path_at(const cdx_json *json, size_t index) {
    int array = cdx_json_get(json, 0, "paths"), node;
    size_t i = 0;
    if (array < 0 || json->nodes[array].type != CDX_JSON_ARRAY) return NULL;
    for (node = json->nodes[array].first; node >= 0 && i < 1000u; node = json->nodes[node].next, ++i)
        if (i == index && json->nodes[node].type == CDX_JSON_STRING) return json->nodes[node].string;
    return NULL;
}

static int call(cdx_json *result, const char *program, const char *cwd, const char *root,
                 const char *extra, cdx_cancel_handle cancellation, size_t *output_size) {
    cdx_json_output input, output;
    cdx_json request;
    int success;
    memset(result, 0, sizeof(*result));
    cdx_json_output_init(&input); cdx_json_output_init(&output);
    cdx_json_raw(&input, "{\"program\":"); cdx_json_quote(&input, program, strlen(program));
    cdx_json_raw(&input, ",\"cwd\":"); cdx_json_quote(&input, cwd, strlen(cwd));
    cdx_json_raw(&input, ",\"roots\":["); cdx_json_quote(&input, root, strlen(root)); cdx_json_raw(&input, "]");
    if (extra) cdx_json_raw(&input, extra);
    cdx_json_raw(&input, "}");
    success = cdx_json_parse(&request, input.data, input.size);
    if (success) {
        cdx_discovery_call(&request, 0, cancellation, &output);
        if (output_size) *output_size = output.size;
        success = !output.failed && cdx_json_parse(result, output.data, output.size);
        if (!success) printf("invalid_result=%s\n", output.data ? output.data : "");
    }
    cdx_json_free(&request);
    cdx_json_output_free(&input); cdx_json_output_free(&output);
    return success;
}

static int documented_schema(const cdx_json *json, int tool) {
    int description = cdx_json_get(json, tool, "description"), node;
    size_t i;
    if (description < 0 || json->nodes[description].type != CDX_JSON_STRING ||
        !*json->nodes[description].string) return 0;
    for (i = 0; i < json->count; ++i) {
        const cdx_json_node *properties = &json->nodes[i];
        if (properties->type != CDX_JSON_OBJECT || !properties->key || strcmp(properties->key, "properties")) continue;
        for (node = properties->first; node >= 0; node = json->nodes[node].next) {
            description = cdx_json_get(json, node, "description");
            if (description < 0 || json->nodes[description].type != CDX_JSON_STRING ||
                !*json->nodes[description].string) return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    static const char *const names[] = {"a space.txt", "z.txt", "中文-$(literal).txt", "skip.bin", ".hidden.txt"};
    char root[4096], missing[8192], long_name[160];
    cdx_json result, full, schema;
    size_t i, size = 0;
    int success;
    unsigned long pid;
#ifdef _WIN32
    HANDLE cancellation;
    pid = GetCurrentProcessId();
#else
    atomic_int cancelled;
    pid = (unsigned long)getpid();
#endif
    if (argc != 3) { fprintf(stderr, "usage: test-discovery ABSOLUTE_RG ABSOLUTE_FIXTURE_PARENT\n"); return 2; }
    (void)snprintf(root, sizeof(root), "%s/find-files-%lu", argv[2], pid);
    (void)make_directory(argv[2]);
    success = make_directory(root);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) success = write_fixture(root, names[i]) && success;
    check("create native Unicode, space and literal fixtures", success);
    if (!success) return 1;
    success = cdx_json_parse(&schema, cdx_discovery_tool_schema(), strlen(cdx_discovery_tool_schema()));
    check("published tool schema parses", success && equal(&schema, "name", "find_files"));
    check("published discovery schema documents the tool and every input", success && documented_schema(&schema, 0));
    cdx_json_free(&schema);
    success = call(&full, argv[1], root, root, NULL, 0, NULL);
    check("real rg complete sorted scan", success && equal(&full, "status", "completed") &&
        bool_field_is(&full, "scan_complete") && bool_field_is(&full, "cleanup_complete") && number(&full, "total_files", 4u) &&
        number(&full, "returned", 4u) && path_at(&full, 0) && path_at(&full, 3) &&
        strcmp(path_at(&full, 0), path_at(&full, 1)) < 0 && strcmp(path_at(&full, 1), path_at(&full, 2)) < 0 &&
        strcmp(path_at(&full, 2), path_at(&full, 3)) < 0 && strstr(path_at(&full, 3), "$(literal)") != NULL);
    success = call(&result, argv[1], root, root, ",\"offset\":1,\"limit\":1", 0, NULL);
    check("offset page uses deterministic full-scan order", success && number(&result, "returned", 1u) &&
        number(&result, "next_offset", 2u) && bool_field_is(&result, "truncated") && path_at(&full, 1) &&
        path_at(&result, 0) && !strcmp(path_at(&full, 1), path_at(&result, 0)));
    cdx_json_free(&result); cdx_json_free(&full);
    {
        cdx_json_output input, output;
        cdx_json request;
        cdx_json_output_init(&input); cdx_json_output_init(&output);
        cdx_json_raw(&input, "{\"program\":"); cdx_json_quote(&input, argv[1], strlen(argv[1]));
        cdx_json_raw(&input, ",\"cwd\":"); cdx_json_quote(&input, root, strlen(root));
        cdx_json_raw(&input, ",\"roots\":["); cdx_json_quote(&input, root, strlen(root));
        cdx_json_raw(&input, ","); cdx_json_quote(&input, root, strlen(root)); cdx_json_raw(&input, "]}");
        success = cdx_json_parse(&request, input.data, input.size);
        memset(&result, 0, sizeof(result));
        if (success) {
            cdx_discovery_call(&request, 0, 0, &output);
            success = cdx_json_parse(&result, output.data, output.size);
        }
        check("duplicate roots do not duplicate returned names", success && number(&result, "total_files", 4u) &&
            number(&result, "returned", 4u));
        cdx_json_free(&request); cdx_json_free(&result);
        cdx_json_output_free(&input); cdx_json_output_free(&output);
    }
    success = call(&result, argv[1], root, root, ",\"globs\":[\"*.txt\"]", 0, NULL);
    check_result("rg glob excludes binary file and includes explicitly matched hidden file (expected 4)",
        success && number(&result, "total_files", 4u) && path_at(&result, 0) &&
        strstr(path_at(&result, 0), ".hidden.txt") != NULL, &result);
    cdx_json_free(&result);
    success = call(&result, argv[1], root, root, ",\"include_hidden\":true", 0, NULL);
    check("hidden opt-in", success && number(&result, "total_files", 5u));
    cdx_json_free(&result);
    success = call(&result, argv[1], root, root, ",\"globs\":[\"*.absent\"]", 0, NULL);
    check("empty scan distinct from process failure", success && equal(&result, "status", "no_match") &&
        bool_field_is(&result, "scan_complete") && number(&result, "returned", 0u));
    cdx_json_free(&result);
    success = call(&result, argv[1], root, root, ",\"max_files\":1", 0, NULL);
    check("scan count cap suppresses partial page", success && equal(&result, "status", "output_limit") &&
        equal(&result, "stop_reason", "max_files") && !bool_field_is(&result, "scan_complete") && number(&result, "returned", 0u));
    cdx_json_free(&result);
    success = call(&result, argv[1], root, root, ",\"offset\":100000", 0, NULL);
    check("offset after end is a complete empty page", success && equal(&result, "status", "completed") &&
        bool_field_is(&result, "scan_complete") && number(&result, "returned", 0u) && !bool_field_is(&result, "truncated"));
    cdx_json_free(&result);
    (void)snprintf(missing, sizeof(missing), "%s/missing/..", root);
    success = call(&result, argv[1], root, missing, NULL, 0, NULL);
#ifdef _WIN32
    check_result("Windows native lookup resolves missing/.. (expected completed, 4 files)",
        success && equal(&result, "status", "completed") && bool_field_is(&result, "scan_complete") &&
        number(&result, "total_files", 4u), &result);
#else
    check_result("missing ancestor traversal is not lexically rewritten (expected failed)", success && equal(&result, "status", "failed") &&
        !bool_field_is(&result, "scan_complete") && number(&result, "returned", 0u), &result);
#endif
    cdx_json_free(&result);
    success = call(&result, argv[1], root, root, ",\"include_hidden\":1", 0, NULL);
    check("invalid boolean rejected at boundary", success && equal(&result, "status", "validation_error"));
    cdx_json_free(&result);
    success = call(&result, argv[1], root, root, ",\"limit\":0", 0, NULL);
    check("invalid page bound rejected", success && equal(&result, "status", "validation_error"));
    cdx_json_free(&result);
#ifdef _WIN32
    cancellation = CreateEventW(NULL, TRUE, TRUE, NULL);
    success = cancellation && call(&result, argv[1], root, root, NULL, cancellation, NULL);
    if (cancellation) CloseHandle(cancellation);
#else
    atomic_init(&cancelled, 1);
    success = call(&result, argv[1], root, root, NULL, &cancelled, NULL);
#endif
    check("pre-cancellation produces no page", success && equal(&result, "status", "cancelled") &&
        !bool_field_is(&result, "scan_complete") && number(&result, "returned", 0u));
    cdx_json_free(&result);
    success = 1;
    for (i = 0; i < 30u; ++i) {
        (void)snprintf(long_name, sizeof(long_name), "bulk-%02u-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz.txt", (unsigned)i);
        success = write_fixture(root, long_name) && success;
    }
    check("create bounded bulk fixture", success);
    success = call(&result, argv[1], root, root, ",\"capture_limit_bytes\":1024", 0, NULL);
    check("capture limit is distinct and suppresses partial page", success && equal(&result, "status", "output_limit") &&
        equal(&result, "stop_reason", "capture_limit_bytes") && !bool_field_is(&result, "scan_complete") && number(&result, "returned", 0u));
    cdx_json_free(&result);
    success = call(&result, argv[1], root, root, ",\"max_output_bytes\":1024", 0, &size);
    check("public JSON budget preserves complete scan and next offset", success && equal(&result, "status", "output_limit") &&
        equal(&result, "stop_reason", "max_output_bytes") && bool_field_is(&result, "scan_complete") &&
        size <= 1024u && number(&result, "total_files", 34u) && path_at(&result, 0) && bool_field_is(&result, "truncated"));
    cdx_json_free(&result);
    for (i = 0; i < 30u; ++i) {
        (void)snprintf(long_name, sizeof(long_name), "bulk-%02u-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz.txt", (unsigned)i);
        remove_fixture(root, long_name);
    }
#ifndef _WIN32
    {
        char target[8192], sub[8192], link[8192], traversal[8192];
        (void)snprintf(target, sizeof(target), "%s/target", root);
        (void)snprintf(sub, sizeof(sub), "%s/target/sub", root);
        (void)snprintf(link, sizeof(link), "%s/link", root);
        (void)snprintf(traversal, sizeof(traversal), "%s/link/..", root);
        success = make_directory(target) && make_directory(sub) && write_fixture(target, "only.txt") &&
            symlink("target/sub", link) == 0;
        if (success) success = call(&result, argv[1], root, traversal, NULL, 0, NULL);
        check("symlink parent traversal keeps native filesystem meaning", success &&
            equal(&result, "status", "completed") && number(&result, "total_files", 1u));
        if (success) cdx_json_free(&result);
        (void)unlink(link); remove_fixture(target, "only.txt"); (void)rmdir(sub); (void)rmdir(target);
        {
            int written = write_fixture(root, "invalid-\xff.txt"), raw_name = 0, readable = 0;
            DIR *directory = written ? opendir(root) : NULL;
            if (directory) {
                struct dirent *entry;
                size_t steps = 0;
                readable = 1;
                while (steps++ < 128u && (entry = readdir(directory)) != NULL) {
                    if (!strcmp(entry->d_name, "invalid-\xff.txt")) { raw_name = 1; break; }
                }
                (void)closedir(directory);
            }
            if (!written || (readable && !raw_name)) {
                ++not_run;
                puts("invalid UTF-8 filename test: NOT_RUN (filesystem cannot represent the raw byte fixture)");
            } else {
                success = readable && call(&result, argv[1], root, root, NULL, 0, NULL);
                check("invalid UTF-8 filenames return explicit encoding error", success &&
                    equal(&result, "status", "encoding_error") && !bool_field_is(&result, "scan_complete") && number(&result, "returned", 0u));
                if (success) cdx_json_free(&result);
            }
            if (written) remove_fixture(root, "invalid-\xff.txt");
        }
        success = write_fixture(root, "line\nbreak.txt");
        if (success) success = call(&result, argv[1], root, root, NULL, 0, NULL);
        check("NUL records retain newlines in discovered names", success && equal(&result, "status", "completed") &&
            number(&result, "total_files", 5u) && path_at(&result, 1) && strchr(path_at(&result, 1), '\n') != NULL);
        if (success) cdx_json_free(&result);
        remove_fixture(root, "line\nbreak.txt");
    }
#endif
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) remove_fixture(root, names[i]);
#ifdef _WIN32
    {
        wchar_t *wide = native(root);
        if (wide) (void)_wrmdir(wide);
        free(wide);
    }
#else
    (void)rmdir(root);
#endif
    printf("discovery: %u/%u passed (NOT_RUN=%u)\n", passed, total, not_run);
    return passed == total ? 0 : 1;
}
