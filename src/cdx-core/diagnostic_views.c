#include "cdx/diagnostic_views.h"

#include <ctype.h>
#include <string.h>

static void copy_field(char *destination, size_t capacity, const char *source, size_t length) {
    if (capacity == 0u) return;
    if (length >= capacity) length = capacity - 1u;
    if (length != 0u) memcpy(destination, source, length);
    destination[length] = 0;
}

static int digits(const char *value, size_t length, unsigned *number) {
    size_t i;
    unsigned n = 0u;
    if (length == 0u || !number) return 0;
    for (i = 0u; i < length; ++i) {
        if (!isdigit((unsigned char)value[i]) || n > 100000000u) return 0;
        n = n * 10u + (unsigned)(value[i] - '0');
    }
    *number = n;
    return 1;
}

static const char *next_colon(const char *start, const char *end) {
    const char *p;
    for (p = start; p < end; ++p) if (*p == ':') return p;
    return NULL;
}

static int parse_colon_location(const char *line, size_t length, cdx_diagnostic_record *record) {
    const char *end = line + length;
    const char *first;
    const char *second;
    const char *third;
    unsigned line_number, column_number;
    first = line;
    while (first && first < end) {
        second = next_colon(first + 1, end);
        if (second && digits(first + 1, (size_t)(second - first - 1), &line_number)) {
            third = next_colon(second + 1, end);
            if (third && digits(second + 1, (size_t)(third - second - 1), &column_number)) {
                copy_field(record->path, sizeof(record->path), line, (size_t)(first - line));
                record->line = line_number;
                record->column = column_number;
                copy_field(record->message, sizeof(record->message), third + 1, (size_t)(end - third - 1));
                return 1;
            }
        }
        first = next_colon(first + 1, end);
    }
    return 0;
}

static int parse_line_location(const char *line, size_t length, cdx_diagnostic_record *record) {
    const char *end = line + length;
    const char *separator = next_colon(line, end);
    const char *end_number;
    unsigned line_number;
    if (!separator) return 0;
    end_number = next_colon(separator + 1, end);
    if (!end_number || !digits(separator + 1, (size_t)(end_number - separator - 1), &line_number)) return 0;
    copy_field(record->path, sizeof(record->path), line, (size_t)(separator - line));
    record->line = line_number;
    record->column = 0u;
    copy_field(record->message, sizeof(record->message), end_number + 1, (size_t)(end - end_number - 1));
    return 1;
}

static int parse_paren_location(const char *line, size_t length, cdx_diagnostic_record *record) {
    const char *end = line + length;
    const char *open = strchr(line, '(');
    const char *comma;
    const char *close;
    unsigned line_number, column_number;
    if (!open || open == line) return 0;
    comma = strchr(open + 1, ',');
    close = comma ? strchr(comma + 1, ')') : NULL;
    if (!comma || !close || !digits(open + 1, (size_t)(comma - open - 1), &line_number) ||
        !digits(comma + 1, (size_t)(close - comma - 1), &column_number)) return 0;
    copy_field(record->path, sizeof(record->path), line, (size_t)(open - line));
    record->line = line_number;
    record->column = column_number;
    copy_field(record->message, sizeof(record->message), close + 1, (size_t)(end - close - 1));
    return 1;
}

static int contains_word(const char *line, size_t length, const char *word) {
    size_t word_length = strlen(word), i;
    if (word_length == 0u || length < word_length) return 0;
    for (i = 0u; i + word_length <= length; ++i) {
        if (!memcmp(line + i, word, word_length)) return 1;
    }
    return 0;
}

static void extract_error_code(char *destination, size_t capacity, const char *line, size_t length) {
    size_t i = 0u, start;
    while (i + 5u <= length) {
        if (!memcmp(line + i, "error", 5u) || !memcmp(line + i, "ERROR", 5u)) break;
        ++i;
    }
    if (i + 5u > length) return;
    i += 5u;
    while (i < length && isspace((unsigned char)line[i])) ++i;
    start = i;
    while (i < length && !isspace((unsigned char)line[i]) && line[i] != ':') ++i;
    if (i > start) copy_field(destination, capacity, line + start, i - start);
}

static void classify(cdx_diagnostic_record *record, const char *line, size_t length, int located) {
    int error = contains_word(line, length, "error") || contains_word(line, length, "Error") ||
        contains_word(line, length, "ERROR");
    int failure = contains_word(line, length, "FAIL") || contains_word(line, length, "failed") ||
        contains_word(line, length, "FAILED");
    if (located && error) record->kind = CDX_DIAGNOSTIC_BUILD_ERROR;
    else if (located && failure) record->kind = CDX_DIAGNOSTIC_TEST_FAILURE;
    else if (located) record->kind = CDX_DIAGNOSTIC_SEARCH_MATCH;
    else record->kind = CDX_DIAGNOSTIC_UNPARSED;
    record->parsed = located;
    if (record->kind == CDX_DIAGNOSTIC_BUILD_ERROR) extract_error_code(record->code, sizeof(record->code), line, length);
    if (!located) copy_field(record->message, sizeof(record->message), line, length);
}

int cdx_diagnostic_parse(const char *raw_log, size_t raw_length,
                         size_t max_records, cdx_diagnostic_result *result) {
    size_t offset = 0u;
    if (!result) return 0;
    memset(result, 0, sizeof(*result));
    result->status = CDX_DIAGNOSTIC_INVALID_INPUT;
    if (!raw_log || raw_length > CDX_DIAGNOSTIC_MAX_INPUT_BYTES || max_records == 0u) return 0;
    if (max_records > CDX_DIAGNOSTIC_MAX_RECORDS) max_records = CDX_DIAGNOSTIC_MAX_RECORDS;
    if (memchr(raw_log, 0, raw_length) != NULL) return 0;
    result->raw_log = raw_log;
    result->raw_log_available = 1;
    result->source_bytes = raw_length;
    while (offset < raw_length) {
        size_t start = offset, length;
        cdx_diagnostic_record *record;
        while (offset < raw_length && raw_log[offset] != '\n') ++offset;
        length = offset - start;
        if (length && raw_log[start + length - 1u] == '\r') --length;
        if (result->count < max_records) {
            record = &result->records[result->count++];
            memset(record, 0, sizeof(*record));
            record->source_offset = start;
            if (!parse_colon_location(raw_log + start, length, record) &&
                !parse_paren_location(raw_log + start, length, record) &&
                !parse_line_location(raw_log + start, length, record)) {
                classify(record, raw_log + start, length, 0);
                ++result->unparsed_lines;
            } else {
                classify(record, raw_log + start, length, 1);
            }
        } else {
            result->truncated = 1;
        }
        if (offset < raw_length) ++offset;
    }
    if (result->truncated) result->status = CDX_DIAGNOSTIC_OUTPUT_LIMIT;
    else if (result->unparsed_lines != 0u) result->status = result->count == result->unparsed_lines ?
        CDX_DIAGNOSTIC_UNKNOWN : CDX_DIAGNOSTIC_PARTIAL;
    else result->status = CDX_DIAGNOSTIC_OK;
    return 1;
}

const char *cdx_diagnostic_status_name(cdx_diagnostic_status status) {
    switch (status) {
    case CDX_DIAGNOSTIC_OK: return "ok";
    case CDX_DIAGNOSTIC_PARTIAL: return "partial";
    case CDX_DIAGNOSTIC_OUTPUT_LIMIT: return "output_limit";
    case CDX_DIAGNOSTIC_INVALID_INPUT: return "invalid_input";
    case CDX_DIAGNOSTIC_UNKNOWN: return "unknown";
    default: return "unknown";
    }
}

const char *cdx_diagnostic_kind_name(cdx_diagnostic_kind kind) {
    switch (kind) {
    case CDX_DIAGNOSTIC_SEARCH_MATCH: return "search_match";
    case CDX_DIAGNOSTIC_BUILD_ERROR: return "build_error";
    case CDX_DIAGNOSTIC_TEST_FAILURE: return "test_failure";
    case CDX_DIAGNOSTIC_UNPARSED: return "unparsed";
    default: return "unparsed";
    }
}
