#include "cdx/json.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
typedef unsigned long long ULONGLONG;
static ULONGLONG GetTickCount64(void) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (ULONGLONG)now.tv_sec * 1000u + (ULONGLONG)now.tv_nsec / 1000000u;
}
#define GetCurrentProcessId getpid
#define ExitProcess _Exit
#endif
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

static void parse_case(const char *name, const char *source, size_t length, int expected) {
    cdx_json json;
    int actual;
    if (total >= 50 || GetTickCount64() >= deadline) ExitProcess(3);
    actual = cdx_json_parse(&json, source, length);
    record(name, actual == expected);
    cdx_json_free(&json);
}

static void fields_and_escapes(void) {
    static const char source[] = "{\"text\":\"a\\\"b\\\\c\\n\\r\\t\\b\\f\\/\",\"number\":-12.5e2,\"bool\":true,\"nil\":null,\"array\":[1,2]}";
    cdx_json json;
    int text, number, boolean, nil, array;
    int good = cdx_json_parse(&json, source, sizeof(source) - 1);
    if (!good) { record("fields and escape contents", 0); cdx_json_free(&json); return; }
    text = cdx_json_get(&json, 0, "text"); number = cdx_json_get(&json, 0, "number");
    boolean = cdx_json_get(&json, 0, "bool"); nil = cdx_json_get(&json, 0, "nil"); array = cdx_json_get(&json, 0, "array");
    good = text >= 0 && number >= 0 && boolean >= 0 && nil >= 0 && array >= 0;
    if (good) good = json.nodes[text].type == CDX_JSON_STRING && strcmp(json.nodes[text].string, "a\"b\\c\n\r\t\b\f/") == 0 && json.nodes[number].number == -1250.0 && json.nodes[boolean].boolean == 1 && json.nodes[nil].type == CDX_JSON_NULL && json.nodes[array].type == CDX_JSON_ARRAY;
    record("fields and escape contents", good);
    cdx_json_free(&json);
}

static void unicode_and_quotes(void) {
    static const char source[] = "{\"text\":\"\\u4e2d\\u6587\\ud83d\\ude42\"}";
    static const char expected[] = "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x99\x82";
    cdx_json json;
    cdx_json_output output;
    int node, good = cdx_json_parse(&json, source, sizeof(source) - 1);
    node = good ? cdx_json_get(&json, 0, "text") : -1;
    record("Unicode escapes and surrogate pair", node >= 0 && strcmp(json.nodes[node].string, expected) == 0);
    cdx_json_free(&json);
    cdx_json_output_init(&output);
    cdx_json_quote(&output, "\"\\\n\r\t\b\f", 7);
    good = !output.failed && cdx_json_parse(&json, output.data, output.size);
    record("writer escaped text round trip", good && json.nodes[0].type == CDX_JSON_STRING && strcmp(json.nodes[0].string, "\"\\\n\r\t\b\f") == 0);
    if (good) cdx_json_free(&json);
    cdx_json_output_free(&output);
    cdx_json_output_init(&output);
    cdx_json_quote(&output, expected, sizeof(expected) - 1);
    good = !output.failed && cdx_json_parse(&json, output.data, output.size);
    record("writer raw UTF8 round trip", good && strcmp(json.nodes[0].string, expected) == 0);
    if (good) cdx_json_free(&json);
    cdx_json_output_free(&output);
}

static void bounded_depth(void) {
    char source[128];
    unsigned index;
    for (index = 0; index < 34; ++index) source[index] = '[';
    source[34] = '0';
    for (index = 0; index < 34; ++index) source[35 + index] = ']';
    source[69] = 0;
    parse_case("depth beyond 32", source, 69, 0);
}


int main(int argc, char **argv) {
    static const struct {
        const char *name;
        const char *source;
        int expected;
    } cases[] = {
        {"empty object", "{}", 1},
        {"empty array", "[]", 1},
        {"whitespace around object", " \n\t{\"a\":1}\r ", 1},
        {"array values", "[0,-0,true,false,null,\"\"]", 1},
        {"fraction and exponent", "1.25e-2", 1},
        {"empty input", "", 0},
        {"object truncated", "{", 0},
        {"array truncated", "[", 0},
        {"key truncated", "{\"a\"", 0},
        {"missing value", "{\"a\":", 0},
        {"missing object terminator", "{\"a\":1", 0},
        {"missing array terminator", "[1", 0},
        {"trailing array comma", "[1,]", 0},
        {"trailing object comma", "{\"a\":1,}", 0},
        {"double array comma", "[1,,2]", 0},
        {"double object colon", "{\"a\"::1}", 0},
        {"missing comma", "{\"a\":1 \"b\":2}", 0},
        {"trailing root data", "{} {}", 0},
        {"duplicate key", "{\"x\":1,\"x\":2}", 0},
        {"escaped duplicate key", "{\"x\":1,\"\\u0078\":2}", 0},
        {"unterminated string", "\"abc", 0},
        {"trailing string escape", "\"abc\\", 0},
        {"unknown string escape", "\"\\q\"", 0},
        {"raw control in string", "\"a\nb\"", 0},
        {"truncated unicode escape", "\"\\u12\"", 0},
        {"unpaired high surrogate", "\"\\ud800\"", 0},
        {"unpaired low surrogate", "\"\\udc00\"", 0},
        {"wrong surrogate pair", "\"\\ud800\\u0041\"", 0},
        {"NUL string unsupported", "\"\\u0000\"", 0},
        {"leading zero", "01", 0},
        {"missing fraction", "1.", 0},
        {"missing exponent", "1e+", 0},
        {"leading plus", "+1", 0},
        {"NaN forbidden", "NaN", 0},
        {"numeric overflow", "1e309", 0},
        {"keyword suffix", "truex", 0},
        {"raw overlong UTF8", "\"\xc0\xaf\"", 0},
        {"raw isolated UTF8 continuation", "\"\x80\"", 0},
        {"raw surrogate UTF8", "\"\xed\xa0\x80\"", 0},
        {"raw out-of-range UTF8", "\"\xf4\x90\x80\x80\"", 0}
    };
    size_t index, count = sizeof(cases) / sizeof(cases[0]);
    int pilot = argc > 1 && strcmp(argv[1], "--pilot") == 0;
    deadline = GetTickCount64() + 10000;
    printf("owner_pid=%lu max_cases=50 wallclock_ms=10000\n", (unsigned long)GetCurrentProcessId());
    if (pilot) count = 1;
    for (index = 0; index < count; ++index) parse_case(cases[index].name, cases[index].source, strlen(cases[index].source), cases[index].expected);
    if (!pilot) {
        static const char embedded_nul[] = {'{','}',0,'x'};
        parse_case("input embedded NUL", embedded_nul, sizeof(embedded_nul), 0);
        fields_and_escapes(); unicode_and_quotes(); bounded_depth();
    }
    printf("{\"status\":\"%s\",\"passed\":%u,\"total\":%u}\n", passed == total ? "PASS" : "FAIL", passed, total);
    return passed == total ? 0 : 1;
}
