#define _POSIX_C_SOURCE 200809L
#include "cdx/operations.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

void cdx_data_query_call(const cdx_json *, int, cdx_cancel_handle, cdx_json_output *);
const char *cdx_data_query_tool_schema(void);

static unsigned total, passed;
static uint64_t deadline;

static uint64_t milliseconds(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static void record(const char *name, int success, const char *actual) {
    if (++total > 80u || milliseconds() >= deadline) exit(3);
    if (success) ++passed;
    printf("%s: %s\n", name, success ? "PASS" : "FAIL");
    if (!success && actual) printf("actual=%s\n", actual);
}

static cdx_json_output execute(const char *format, const char *text, const char *extra, cdx_cancel_handle cancel) {
    cdx_json_output request, result;
    cdx_json json = {0};
    cdx_json_output_init(&request); cdx_json_output_init(&result);
    cdx_json_raw(&request, "{\"format\":"); cdx_json_quote(&request, format, strlen(format));
    cdx_json_raw(&request, ",\"text\":"); cdx_json_quote(&request, text, strlen(text));
    if (extra) cdx_json_raw(&request, extra);
    cdx_json_raw(&request, "}");
    if (!request.failed && cdx_json_parse(&json, request.data, request.size)) cdx_data_query_call(&json, 0, cancel, &result);
    cdx_json_free(&json); cdx_json_output_free(&request);
    return result;
}

static int string_field(const cdx_json *json, int object, const char *field, const char *expected) {
    int node = cdx_json_get(json, object, field);
    return node >= 0 && json->nodes[node].type == CDX_JSON_STRING && !strcmp(json->nodes[node].string, expected);
}

static int uint_field(const cdx_json *json, int object, const char *field, size_t expected) {
    int node = cdx_json_get(json, object, field);
    return node >= 0 && json->nodes[node].type == CDX_JSON_NUMBER && json->nodes[node].number == (double)expected;
}

static int bool_field(const cdx_json *json, int object, const char *field, int expected) {
    int node = cdx_json_get(json, object, field);
    return node >= 0 && json->nodes[node].type == CDX_JSON_BOOL && json->nodes[node].boolean == expected;
}

static void basic_case(const char *name, const char *format, const char *text, const char *extra,
                       const char *status, size_t examined, size_t matched, size_t returned, int complete) {
    cdx_json_output result = execute(format, text, extra, NULL);
    cdx_json json = {0};
    int good = result.data && !result.failed && cdx_json_parse(&json, result.data, result.size);
    if (good) good = string_field(&json, 0, "status", status) && uint_field(&json, 0, "examined", examined) &&
                     uint_field(&json, 0, "matched", matched) && uint_field(&json, 0, "returned", returned) &&
                     bool_field(&json, 0, "scan_complete", complete) && bool_field(&json, 0, "read_only", 1);
    record(name, good, result.data); cdx_json_free(&json); cdx_json_output_free(&result);
}

static void validation_case(const char *name, const char *format, const char *text, const char *extra) {
    cdx_json_output result = execute(format, text, extra, NULL);
    cdx_json json = {0};
    int good = result.data && !result.failed && cdx_json_parse(&json, result.data, result.size);
    record(name, good && string_field(&json, 0, "status", "validation_error"), result.data);
    cdx_json_free(&json); cdx_json_output_free(&result);
}

static void projections(void) {
    static const char source[] = "{\"items\":[{\"id\":9007199254740993,\"a/b\":{\"~x\":\"中文\"},\"n\":1.2300e+2}]}";
    cdx_json_output result = execute("json", source, ",\"records_path\":\"/items\",\"select\":[\"/id\",\"/a~1b/~0x\",\"/missing\",\"/n\"]", NULL);
    cdx_json json = {0};
    int good = result.data && cdx_json_parse(&json, result.data, result.size), rows, row, missing;
    rows = good ? cdx_json_get(&json, 0, "rows") : -1;
    row = rows >= 0 ? json.nodes[rows].first : -1;
    missing = row >= 0 ? cdx_json_get(&json, row, "/missing") : -1;
    good = good && row >= 0 && string_field(&json, row, "/a~1b/~0x", "中文") && missing >= 0 &&
           json.nodes[missing].type == CDX_JSON_NULL && strstr(result.data, "9007199254740993") && strstr(result.data, "1.2300e+2");
    record("projection preserves number tokens, Unicode and escaped pointers", good, result.data);
    cdx_json_free(&json); cdx_json_output_free(&result);
    result = execute("json", "[{\"a\":[\"first\",\"second\"]}]", ",\"select\":[\"/a/1\",\"/a/01\"]", NULL);
    good = result.data && strstr(result.data, "\"/a/1\":\"second\"") && strstr(result.data, "\"/a/01\":null");
    record("array pointer indices are canonical", good, result.data); cdx_json_output_free(&result);
}

static void pagination_and_groups(void) {
    cdx_json_output result = execute("json", "[1,2,3]", ",\"offset\":1,\"limit\":1", NULL);
    cdx_json json = {0};
    int good = result.data && cdx_json_parse(&json, result.data, result.size);
    record("rows pagination keeps exact full-scan totals", good && uint_field(&json, 0, "matched", 3) &&
        uint_field(&json, 0, "next_offset", 2) && bool_field(&json, 0, "truncated", 1) && strstr(result.data, "\"rows\":[2]"), result.data);
    cdx_json_free(&json); cdx_json_output_free(&result);
    result = execute("json", "[{\"x\":null},{},{\"x\":null},{\"x\":\"1\"},{\"x\":1},{\"x\":true}]", ",\"operation\":\"group_count\",\"group_by\":\"/x\"", NULL);
    good = result.data && cdx_json_parse(&json, result.data, result.size);
    record("groups preserve types and distinguish missing from null", good && uint_field(&json, 0, "group_count", 5) &&
        uint_field(&json, 0, "matched", 6) && strstr(result.data, "{\"value\":null,\"missing\":false,\"count\":2}") &&
        strstr(result.data, "{\"value\":null,\"missing\":true,\"count\":1}"), result.data);
    cdx_json_free(&json); cdx_json_output_free(&result);
    result = execute("json", "[2,1,2]", ",\"operation\":\"group_count\",\"group_by\":\"\",\"offset\":1,\"limit\":1", NULL);
    good = result.data && cdx_json_parse(&json, result.data, result.size);
    record("group pagination follows first-seen order", good && uint_field(&json, 0, "group_count", 2) &&
        bool_field(&json, 0, "scan_complete", 1) && bool_field(&json, 0, "truncated", 0) && strstr(result.data, "\"value\":1"), result.data);
    cdx_json_free(&json); cdx_json_output_free(&result);
}

static void csv_values(void) {
    cdx_json_output result = execute("csv", "name,note,value\r\n中文,\"hello,\"\"world\"\"\r\nnext\",001\r\n", NULL, NULL);
    cdx_json json = {0};
    int good = result.data && cdx_json_parse(&json, result.data, result.size), rows, row;
    rows = good ? cdx_json_get(&json, 0, "rows") : -1; row = rows >= 0 ? json.nodes[rows].first : -1;
    record("CSV quoted commas, doubled quotes, CRLF and Unicode", good && row >= 0 && string_field(&json, row, "name", "中文") &&
        string_field(&json, row, "note", "hello,\"world\"\r\nnext") && string_field(&json, row, "value", "001"), result.data);
    cdx_json_free(&json); cdx_json_output_free(&result);
    result = execute("csv", "a,b\nx,\n", NULL, NULL);
    good = result.data && strstr(result.data, "\"rows\":[{\"a\":\"x\",\"b\":\"\"}]");
    record("CSV trailing empty cell", good, result.data); cdx_json_output_free(&result);
    result = execute("csv", "x\n\"one\r\"\"\ntwo\"\n\"bad\"x", NULL, NULL);
    good = result.data && cdx_json_parse(&json, result.data, result.size);
    record("CSV errors report physical line after mixed quoted newlines", good && string_field(&json, 0, "status", "parse_error") &&
        uint_field(&json, 0, "examined", 1) && uint_field(&json, 0, "error_line", 5), result.data);
    cdx_json_free(&json); cdx_json_output_free(&result);
}

static void limits_and_cancellation(void) {
    char source[512];
    cdx_json_output result;
    cdx_json json = {0};
    int good;
#ifdef _WIN32
    HANDLE cancellation = CreateEventW(NULL, TRUE, TRUE, NULL);
#else
    atomic_int flag = 1;
    cdx_cancel_handle cancellation = &flag;
#endif
    result = execute("json", "[1]", NULL, cancellation);
    good = result.data && cdx_json_parse(&json, result.data, result.size);
    record("pre-signaled cancellation", good && string_field(&json, 0, "status", "cancelled") &&
        uint_field(&json, 0, "examined", 0) && bool_field(&json, 0, "scan_complete", 0), result.data);
    cdx_json_free(&json); cdx_json_output_free(&result);
#ifdef _WIN32
    CloseHandle(cancellation);
#endif
    source[0] = '['; source[1] = '"'; memset(source + 2, 'x', 400); source[402] = '"'; source[403] = ']'; source[404] = 0;
    result = execute("json", source, ",\"max_output_bytes\":1024", NULL);
    good = result.data && result.size <= 1024 && cdx_json_parse(&json, result.data, result.size);
    record("oversized row returns bounded explicit output_limit", good && string_field(&json, 0, "status", "output_limit") &&
        uint_field(&json, 0, "returned", 0) && bool_field(&json, 0, "scan_complete", 0), result.data);
    cdx_json_free(&json); cdx_json_output_free(&result);
    basic_case("input budget enforced before parsing", "json", "[1,2]", ",\"max_input_bytes\":4", "too_large", 0, 0, 0, 0);
    basic_case("record limit preserves explicit partial rows", "json", "[1,2,3]", ",\"max_records\":2", "record_limit", 2, 2, 2, 0);
    basic_case("group limit preserves bounded partial groups", "json", "[1,2]", ",\"operation\":\"group_count\",\"group_by\":\"\",\"max_groups\":1", "group_limit", 2, 2, 1, 0);
}

static int write_fixture(const char *path, const unsigned char *bytes, size_t length) {
    FILE *file;
    size_t written;
#ifdef _WIN32
    wchar_t native[32768];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, native, 32768)) return 0;
    if (_wfopen_s(&file, native, L"wb")) return 0;
#else
    file = fopen(path, "wb");
#endif
    if (!file) return 0;
    written = fwrite(bytes, 1, length, file);
    return fclose(file) == 0 && written == length;
}

static cdx_json_output execute_path(const char *path, const char *encoding) {
    cdx_json_output request, result;
    cdx_json json = {0};
    cdx_json_output_init(&request); cdx_json_output_init(&result);
    cdx_json_raw(&request, "{\"format\":\"json\",\"operation\":\"count\",\"path\":"); cdx_json_quote(&request, path, strlen(path));
    if (encoding) { cdx_json_raw(&request, ",\"encoding\":"); cdx_json_quote(&request, encoding, strlen(encoding)); }
    cdx_json_raw(&request, "}");
    if (cdx_json_parse(&json, request.data, request.size)) cdx_data_query_call(&json, 0, NULL, &result);
    cdx_json_free(&json); cdx_json_output_free(&request);
    return result;
}

static void file_case(const char *directory, const char *name, const unsigned char *bytes, size_t length,
                      const char *encoding, const char *status) {
    char path[32768];
    cdx_json_output result;
    cdx_json parsed = {0};
    int good;
    int written = snprintf(path, sizeof(path), "%s/data-query-中文-%s.json", directory, name);
    if (written < 0 || (size_t)written >= sizeof(path) || !write_fixture(path, bytes, length)) { record(name, 0, "fixture creation failed"); return; }
    result = execute_path(path, encoding);
    good = result.data && cdx_json_parse(&parsed, result.data, result.size);
    record(name, good && string_field(&parsed, 0, "status", status), result.data);
    cdx_json_free(&parsed); cdx_json_output_free(&result);
}

#ifndef _WIN32
static void posix_traversal_cases(const char *directory) {
    char base[4000], target[4096], inner[4096], link[4096], present[4096], requested[4096];
    cdx_json_output result;
    cdx_json parsed = {0};
    int good;
    int length = snprintf(base, sizeof(base), "%s/data-query-traversal-%ld", directory, (long)getpid());
    if (length < 0 || (size_t)length + 64u >= sizeof(base) || mkdir(base, 0700)) { record("POSIX traversal fixture", 0, "fixture directory creation failed"); return; }
    (void)snprintf(target, sizeof(target), "%s/target", base);
    (void)snprintf(inner, sizeof(inner), "%s/target/inner", base);
    (void)snprintf(link, sizeof(link), "%s/link", base);
    (void)snprintf(present, sizeof(present), "%s/present.json", base);
    good = !mkdir(target, 0700) && !mkdir(inner, 0700) && !symlink(inner, link) &&
           write_fixture(present, (const unsigned char *)"[1]", 3);
    (void)snprintf(present, sizeof(present), "%s/target/present.json", base);
    good = good && write_fixture(present, (const unsigned char *)"[1,2]", 5);
    if (!good) { record("POSIX traversal fixture", 0, "fixture creation failed"); return; }
    (void)snprintf(requested, sizeof(requested), "%s/missing/../present.json", base);
    result = execute_path(requested, NULL);
    good = result.data && cdx_json_parse(&parsed, result.data, result.size);
    record("POSIX missing/../ path preserves lookup failure", good && string_field(&parsed, 0, "status", "not_found"), result.data);
    cdx_json_free(&parsed); cdx_json_output_free(&result);
    (void)snprintf(requested, sizeof(requested), "%s/link/../present.json", base);
    result = execute_path(requested, NULL);
    good = result.data && cdx_json_parse(&parsed, result.data, result.size);
    record("POSIX symlink/../ follows original filesystem traversal", good && string_field(&parsed, 0, "status", "ok") &&
           uint_field(&parsed, 0, "count", 2), result.data);
    cdx_json_free(&parsed); cdx_json_output_free(&result);
}
#endif

int main(int argc, char **argv) {
    int pilot = argc > 2 && !strcmp(argv[2], "--pilot");
    cdx_json schema;
    deadline = milliseconds() + 15000u;
    if (argc < 2) { fprintf(stderr, "usage: test_data_query ABSOLUTE_FIXTURE_DIRECTORY [--pilot]\n"); return 2; }
    record("tool schema is valid JSON", cdx_json_parse(&schema, cdx_data_query_tool_schema(), strlen(cdx_data_query_tool_schema())), NULL);
    cdx_json_free(&schema);
    basic_case("JSON array rows", "json", "[{\"x\":1},{\"x\":2}]", NULL, "ok", 2, 2, 2, 1);
    if (!pilot) {
        projections(); pagination_and_groups(); csv_values(); limits_and_cancellation();
        basic_case("empty JSON array", "json", "[]", NULL, "no_match", 0, 0, 0, 1);
        basic_case("JSON object supplies one record", "json", "{\"x\":1}", NULL, "ok", 1, 1, 1, 1);
        basic_case("large adjacent integers compare exactly", "json", "[{\"id\":9007199254740992},{\"id\":9007199254740993}]", ",\"operation\":\"count\",\"where\":{\"field\":\"/id\",\"equals\":9007199254740993}", "ok", 2, 1, 0, 1);
        basic_case("numeric comparison uses original spelling", "json", "[1,1.0,1e0]", ",\"operation\":\"count\",\"where\":{\"field\":\"\",\"equals\":1}", "ok", 3, 1, 0, 1);
        basic_case("where null does not match missing", "json", "[{\"x\":null},{}]", ",\"operation\":\"count\",\"where\":{\"field\":\"/x\",\"equals\":null}", "ok", 2, 1, 0, 1);
        basic_case("empty group key supports scalar records", "json", "[\"x\",\"x\",true]", ",\"operation\":\"group_count\",\"group_by\":\"\"", "ok", 3, 3, 2, 1);
        basic_case("group containers are explicit errors", "json", "[{\"x\":{}}]", ",\"operation\":\"group_count\",\"group_by\":\"/x\"", "invalid_group_value", 1, 1, 0, 0);
        basic_case("records pointer must locate array/object", "json", "{\"x\":1}", ",\"records_path\":\"/x\"", "invalid_records_path", 0, 0, 0, 0);
        basic_case("malformed JSON rejected", "json", "[1,]", NULL, "parse_error", 0, 0, 0, 0);
        basic_case("duplicate JSON keys rejected", "json", "{\"x\":1,\"x\":2}", NULL, "parse_error", 0, 0, 0, 0);
        basic_case("JSON escaped NUL rejected", "json", "[{\"x\":\"\\u0000\"}]", NULL, "parse_error", 0, 0, 0, 0);
        basic_case("JSONL blank lines and CRLF", "jsonl", "\r\n1\r\n \t\r\n{\"x\":2}\n", NULL, "ok", 2, 2, 2, 1);
        basic_case("malformed JSONL preserves completed records", "jsonl", "\n{\"x\":1}\n[invalid]\n", NULL, "parse_error", 1, 1, 1, 0);
        basic_case("CSV string comparison avoids type coercion", "csv", "x\n1\n2\n", ",\"operation\":\"count\",\"where\":{\"field\":\"/x\",\"equals\":1}", "no_match", 2, 0, 0, 1);
        basic_case("CSV string predicate", "csv", "x\n1\n2\n", ",\"operation\":\"count\",\"where\":{\"field\":\"/x\",\"equals\":\"1\"}", "ok", 2, 1, 0, 1);
        basic_case("CSV duplicate headers rejected", "csv", "x,x\n1,2", NULL, "invalid_csv_header", 0, 0, 0, 0);
        basic_case("CSV empty headers rejected", "csv", "x,\n1,2", NULL, "invalid_csv_header", 0, 0, 0, 0);
        basic_case("CSV unmatched quote rejected", "csv", "x\n\"bad", NULL, "parse_error", 0, 0, 0, 0);
        basic_case("CSV quote in plain field rejected", "csv", "x\nb\"ad", NULL, "parse_error", 0, 0, 0, 0);
        basic_case("CSV data after closing quote rejected", "csv", "x\n\"bad\"x", NULL, "parse_error", 0, 0, 0, 0);
        basic_case("CSV ragged row preserves earlier rows", "csv", "x,y\n1,2\n3\n", NULL, "parse_error", 1, 1, 1, 0);
        basic_case("CSV header only supplies zero records", "csv", "x,y\r\n", NULL, "no_match", 0, 0, 0, 1);
        basic_case("CSV CR line endings", "csv", "x\ra\rb\r", NULL, "ok", 2, 2, 2, 1);
        validation_case("exclusive source options", "json", "[]", ",\"path\":\"C:/unused.json\"");
        validation_case("invalid pointer escapes", "json", "[]", ",\"select\":[\"/bad~2\"]");
        validation_case("duplicate projection fields", "json", "[]", ",\"select\":[\"/x\",\"/x\"]");
        validation_case("group count requires group_by", "json", "[]", ",\"operation\":\"group_count\"");
        validation_case("rows reject group_by", "json", "[]", ",\"group_by\":\"/x\"");
        validation_case("count rejects pagination", "json", "[]", ",\"operation\":\"count\",\"limit\":1");
        validation_case("records_path applies only to JSON", "jsonl", "1\n", ",\"records_path\":\"\"");
        validation_case("where equals must be scalar", "json", "[]", ",\"where\":{\"field\":\"/x\",\"equals\":[]}");
        validation_case("inline text rejects encoding override", "json", "[]", ",\"encoding\":\"cp936\"");
        {
            static const unsigned char utf8_bom[] = {0xef,0xbb,0xbf,'[','{','"','x','"',':','1','}',']','\r','\n'};
            static const unsigned char utf16[] = {0xff,0xfe,'[',0,'{',0,'"',0,'x',0,'"',0,':',0,'1',0,'}',0,']',0};
            static const unsigned char cp936[] = {'[','{','"','x','"',':','"',0xd6,0xd0,0xce,0xc4,'"','}',']'};
            static const unsigned char invalid[] = {'[','"',0xff,'"',']'};
            static const unsigned char nul[] = {'[',']',0,'x'};
            file_case(argv[1], "utf8-bom", utf8_bom, sizeof(utf8_bom), NULL, "ok");
            file_case(argv[1], "utf16", utf16, sizeof(utf16), NULL, "ok");
            file_case(argv[1], "cp936", cp936, sizeof(cp936), "cp936", "ok");
            file_case(argv[1], "invalid-utf8", invalid, sizeof(invalid), NULL, "encoding_error");
            file_case(argv[1], "embedded-nul", nul, sizeof(nul), NULL, "encoding_error");
        }
#ifndef _WIN32
        posix_traversal_cases(argv[1]);
#endif
    }
    printf("{\"status\":\"%s\",\"passed\":%u,\"total\":%u}\n", passed == total ? "PASS" : "FAIL", passed, total);
    return passed == total ? 0 : 1;
}
