#include "cdx/diagnostic_views.h"

#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
static void check(int valid, const char *name) {
    ++checks;
    if (!valid) ++failures;
    printf("%s %s\n", valid ? "PASS" : "FAIL", name);
}

int main(void) {
    static const char log[] =
        "src/main.c:12:4:needle\n"
        "src/main.c(13,7): error C2065: unknown name\n"
        "tests/unit.c:18: FAIL expected 1\n"
        "unparsed compiler output\n";
    cdx_diagnostic_result result;
    check(cdx_diagnostic_parse(log, sizeof(log) - 1u, 8u, &result), "diagnostic log parses");
    check(result.status == CDX_DIAGNOSTIC_PARTIAL && result.count == 4u && result.unparsed_lines == 1u,
          "partial status preserves unknown lines");
    check(result.records[0].kind == CDX_DIAGNOSTIC_SEARCH_MATCH && !strcmp(result.records[0].path, "src/main.c") &&
          result.records[0].line == 12u && result.records[0].column == 4u,
          "rg location is retained");
    check(result.records[1].kind == CDX_DIAGNOSTIC_BUILD_ERROR && result.records[1].line == 13u &&
          result.records[1].column == 7u && !strcmp(result.records[1].code, "C2065"),
          "MSVC error location and code are retained");
    check(result.records[2].kind == CDX_DIAGNOSTIC_TEST_FAILURE && result.records[3].kind == CDX_DIAGNOSTIC_UNPARSED,
          "test and unparsed kinds are distinct");
    check(result.raw_log_available && result.raw_log == log && result.source_bytes == sizeof(log) - 1u &&
          result.records[3].source_offset > result.records[2].source_offset,
          "raw log access and offsets are retained");
    check(cdx_diagnostic_parse(log, sizeof(log) - 1u, 2u, &result) &&
          result.status == CDX_DIAGNOSTIC_OUTPUT_LIMIT && result.truncated && result.count == 2u,
          "record bound exposes output limit");
    check(!cdx_diagnostic_parse("a\0b", 3u, 4u, &result) && result.status == CDX_DIAGNOSTIC_INVALID_INPUT,
          "embedded NUL is rejected");
    check(!cdx_diagnostic_parse(log, CDX_DIAGNOSTIC_MAX_INPUT_BYTES + 1u, 4u, &result) &&
          result.status == CDX_DIAGNOSTIC_INVALID_INPUT, "oversized input is rejected");
    check(cdx_diagnostic_parse("C:/src/main.c:21:2: error: bad\n", strlen("C:/src/main.c:21:2: error: bad\n"), 4u, &result) &&
          !strcmp(result.records[0].path, "C:/src/main.c") && result.records[0].line == 21u,
          "Windows drive paths retain the location");
    printf("%u/%u checks passed\n", checks - failures, checks);
    return failures == 0u ? 0 : 1;
}
