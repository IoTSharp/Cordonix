#define _POSIX_C_SOURCE 200809L
#include "cdx/operations.h"
#include "cdx/document.h"
#include "cdx/filesystem.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <time.h>
#endif

#define DATA_INPUT_MAX (1024u * 1024u)
#define DATA_POINTER_MAX 1024u
#define DATA_SELECT_MAX 32u
#define DATA_CSV_COLUMNS 128u
#define DATA_GROUP_MAX 1024u
#define DATA_RESULT_RESERVE 768u

typedef struct data_options {
    const char *path, *text, *format, *operation, *records_path, *group_by;
    const char *select[DATA_SELECT_MAX];
    size_t select_count, offset, limit, max_records, max_groups, max_input, max_output;
    unsigned timeout_ms;
    cdx_encoding encoding;
    const cdx_json *where_json;
    const char *where_field;
    int where_value;
} data_options;

typedef struct data_group {
    char *value;
    size_t length, count;
    int missing;
} data_group;

typedef struct data_query {
    data_options options;
    cdx_cancel_handle cancellation;
    uint64_t deadline;
    const char *stop;
    size_t examined, matched, returned, input_bytes, error_line;
    int scan_complete;
    cdx_json_output rows;
    data_group *groups;
    size_t group_count, group_bytes;
} data_query;

typedef struct csv_reader {
    char *source;
    size_t length, cursor, line;
    char *headers[DATA_CSV_COLUMNS];
    size_t columns;
} csv_reader;

static uint64_t data_milliseconds(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
#endif
}

static int stopped(data_query *query) {
    if (query->stop) return 1;
#ifdef _WIN32
    if (query->cancellation && WaitForSingleObject(query->cancellation, 0) == WAIT_OBJECT_0) query->stop = "cancelled";
#else
    if (query->cancellation && atomic_load(query->cancellation)) query->stop = "cancelled";
#endif
    if (!query->stop && data_milliseconds() >= query->deadline) query->stop = "timed_out";
    return query->stop != NULL;
}

static const char *text_option(const cdx_json *json, int arguments, const char *key, const char *fallback, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    if (node < 0) return fallback;
    if (json->nodes[node].type != CDX_JSON_STRING) { *valid = 0; return fallback; }
    return json->nodes[node].string;
}

static size_t size_option(const cdx_json *json, int arguments, const char *key, size_t fallback, size_t minimum, size_t maximum, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    double value;
    if (node < 0) return fallback;
    value = json->nodes[node].number;
    if (json->nodes[node].type != CDX_JSON_NUMBER || !isfinite(value) || value < (double)minimum || value > (double)maximum || floor(value) != value) {
        *valid = 0; return fallback;
    }
    return (size_t)value;
}

static int pointer_valid(const char *pointer) {
    size_t i, segments = 0, length = strlen(pointer);
    if (length > DATA_POINTER_MAX || (length && pointer[0] != '/')) return 0;
    for (i = 0; i < length; ++i) {
        if (pointer[i] == '/' && ++segments > 32u) return 0;
        if (pointer[i] == '~') {
            if (pointer[i + 1u] != '0' && pointer[i + 1u] != '1') return 0;
            ++i;
        }
    }
    return 1;
}

static int parse_options(data_options *options, const cdx_json *json, int arguments) {
    const char *encoding;
    int valid = 1, node, child;
    size_t i;
    memset(options, 0, sizeof(*options));
    options->where_value = -1;
    options->path = text_option(json, arguments, "path", NULL, &valid);
    options->text = text_option(json, arguments, "text", NULL, &valid);
    options->format = text_option(json, arguments, "format", "", &valid);
    options->operation = text_option(json, arguments, "operation", "rows", &valid);
    options->records_path = text_option(json, arguments, "records_path", "", &valid);
    options->group_by = text_option(json, arguments, "group_by", NULL, &valid);
    encoding = text_option(json, arguments, "encoding", "auto", &valid);
    options->offset = size_option(json, arguments, "offset", 0, 0, 100000, &valid);
    options->limit = size_option(json, arguments, "limit", 100, 1, 1000, &valid);
    options->max_records = size_option(json, arguments, "max_records", 10000, 1, 100000, &valid);
    options->max_groups = size_option(json, arguments, "max_groups", 256, 1, DATA_GROUP_MAX, &valid);
    options->max_input = size_option(json, arguments, "max_input_bytes", DATA_INPUT_MAX, 1, DATA_INPUT_MAX, &valid);
    options->max_output = size_option(json, arguments, "max_output_bytes", 65536, 1024, DATA_INPUT_MAX, &valid);
    options->timeout_ms = (unsigned)size_option(json, arguments, "timeout_ms", 10000, 1, 60000, &valid);
    if ((options->path != NULL) == (options->text != NULL) || (options->path && !*options->path)) valid = 0;
    if (strcmp(options->format, "json") && strcmp(options->format, "jsonl") && strcmp(options->format, "csv")) valid = 0;
    if (strcmp(options->operation, "rows") && strcmp(options->operation, "count") && strcmp(options->operation, "group_count")) valid = 0;
    if (!pointer_valid(options->records_path) || (options->group_by && !pointer_valid(options->group_by))) valid = 0;
    if (strcmp(options->format, "json") && cdx_json_get(json, arguments, "records_path") >= 0) valid = 0;
    if ((!strcmp(options->operation, "group_count")) != (options->group_by != NULL)) valid = 0;
    if (!strcmp(encoding, "auto")) options->encoding = CDX_ENCODING_AUTO;
    else if (!strcmp(encoding, "utf-8")) options->encoding = CDX_ENCODING_UTF8;
    else if (!strcmp(encoding, "utf-8-bom")) options->encoding = CDX_ENCODING_UTF8_BOM;
    else if (!strcmp(encoding, "utf-16-le")) options->encoding = CDX_ENCODING_UTF16LE;
    else if (!strcmp(encoding, "utf-16-be")) options->encoding = CDX_ENCODING_UTF16BE;
    else if (!strcmp(encoding, "cp936") || !strcmp(encoding, "gbk")) options->encoding = CDX_ENCODING_CP936;
    else valid = 0;
    if (options->text && cdx_json_get(json, arguments, "encoding") >= 0) valid = 0;
    node = cdx_json_get(json, arguments, "select");
    if (node >= 0) {
        if (strcmp(options->operation, "rows") || json->nodes[node].type != CDX_JSON_ARRAY || json->nodes[node].first < 0) valid = 0;
        else for (child = json->nodes[node].first; child >= 0; child = json->nodes[child].next) {
            const char *pointer;
            if (options->select_count == DATA_SELECT_MAX || json->nodes[child].type != CDX_JSON_STRING) { valid = 0; break; }
            pointer = json->nodes[child].string;
            if (!pointer_valid(pointer)) valid = 0;
            for (i = 0; i < options->select_count; ++i) if (!strcmp(options->select[i], pointer)) valid = 0;
            options->select[options->select_count++] = pointer;
        }
    }
    node = cdx_json_get(json, arguments, "where");
    if (node >= 0) {
        int field = cdx_json_get(json, node, "field"), value = cdx_json_get(json, node, "equals");
        if (json->nodes[node].type != CDX_JSON_OBJECT || field < 0 || value < 0 || json->nodes[field].type != CDX_JSON_STRING ||
            json->nodes[value].type > CDX_JSON_STRING) valid = 0;
        else {
            options->where_json = json; options->where_value = value; options->where_field = json->nodes[field].string;
            if (!pointer_valid(options->where_field)) valid = 0;
        }
    }
    if (!strcmp(options->operation, "count") && (cdx_json_get(json, arguments, "offset") >= 0 || cdx_json_get(json, arguments, "limit") >= 0)) valid = 0;
    return valid;
}

static int pointer_node(const cdx_json *json, int node, const char *pointer) {
    size_t cursor = 0;
    while (pointer[cursor] && node >= 0) {
        char token[DATA_POINTER_MAX + 1u];
        size_t length = 0, index = 0, i;
        int child;
        ++cursor;
        while (pointer[cursor] && pointer[cursor] != '/') {
            char c = pointer[cursor++];
            if (c == '~') c = pointer[cursor++] == '0' ? '~' : '/';
            token[length++] = c;
        }
        token[length] = 0;
        if (json->nodes[node].type == CDX_JSON_OBJECT) node = cdx_json_get(json, node, token);
        else if (json->nodes[node].type == CDX_JSON_ARRAY) {
            if (!length || (length > 1u && token[0] == '0')) return -1;
            for (i = 0; i < length; ++i) {
                if (token[i] < '0' || token[i] > '9' || index > 65536u) return -1;
                index = index * 10u + (size_t)(token[i] - '0');
            }
            child = json->nodes[node].first;
            for (i = 0; i < index && child >= 0; ++i) child = json->nodes[child].next;
            node = child;
        } else return -1;
    }
    return node;
}

static int scalar_equal(const cdx_json *left, int a, const cdx_json *right, int b) {
    const cdx_json_node *x, *y;
    if (a < 0) return 0;
    x = &left->nodes[a]; y = &right->nodes[b];
    if (x->type != y->type) return 0;
    switch (x->type) {
    case CDX_JSON_NULL: return 1;
    case CDX_JSON_BOOL: return x->boolean == y->boolean;
    case CDX_JSON_STRING: return !strcmp(x->string, y->string);
    case CDX_JSON_NUMBER: return x->number_length == y->number_length && !memcmp(x->number_text, y->number_text, x->number_length);
    default: return 0;
    }
}

static int emit_node(data_query *query, cdx_json_output *output, const cdx_json *json, int node) {
    const cdx_json_node *value;
    int child, first = 1;
    if (stopped(query)) return 0;
    if (node < 0) { cdx_json_raw(output, "null"); return 1; }
    value = &json->nodes[node];
    switch (value->type) {
    case CDX_JSON_NULL: cdx_json_raw(output, "null"); break;
    case CDX_JSON_BOOL: cdx_json_raw(output, value->boolean ? "true" : "false"); break;
    case CDX_JSON_NUMBER: cdx_json_rawn(output, value->number_text, value->number_length); break;
    case CDX_JSON_STRING: cdx_json_quote(output, value->string, strlen(value->string)); break;
    case CDX_JSON_ARRAY: case CDX_JSON_OBJECT:
        cdx_json_raw(output, value->type == CDX_JSON_ARRAY ? "[" : "{");
        for (child = value->first; child >= 0; child = json->nodes[child].next) {
            if (!first) cdx_json_raw(output, ",");
            first = 0;
            if (value->type == CDX_JSON_OBJECT) { cdx_json_quote(output, json->nodes[child].key, strlen(json->nodes[child].key)); cdx_json_raw(output, ":"); }
            if (!emit_node(query, output, json, child)) return 0;
        }
        cdx_json_raw(output, value->type == CDX_JSON_ARRAY ? "]" : "}"); break;
    }
    if (output->failed) { query->stop = "io_error"; return 0; }
    if (output->size > query->options.max_output - DATA_RESULT_RESERVE) { query->stop = "output_limit"; return 0; }
    return !stopped(query);
}

static int append_row(data_query *query, const cdx_json *json, int node) {
    cdx_json_output row;
    size_t i, size;
    int good = 1;
    if (query->matched <= query->options.offset || query->returned >= query->options.limit) return 1;
    cdx_json_output_init(&row);
    if (!query->options.select_count) good = emit_node(query, &row, json, node);
    else {
        cdx_json_raw(&row, "{");
        for (i = 0; i < query->options.select_count && good; ++i) {
            const char *pointer = query->options.select[i];
            if (i) cdx_json_raw(&row, ",");
            cdx_json_quote(&row, pointer, strlen(pointer)); cdx_json_raw(&row, ":");
            good = emit_node(query, &row, json, pointer_node(json, node, pointer));
        }
        cdx_json_raw(&row, "}");
    }
    size = query->rows.size + (query->returned ? 1u : 0u) + row.size;
    if (good && (row.failed || size > query->options.max_output - DATA_RESULT_RESERVE)) { query->stop = row.failed ? "io_error" : "output_limit"; good = 0; }
    if (good) {
        if (query->returned) cdx_json_raw(&query->rows, ",");
        cdx_json_rawn(&query->rows, row.data, row.size); ++query->returned;
        if (query->rows.failed) { query->stop = "io_error"; good = 0; }
    }
    cdx_json_output_free(&row);
    return good;
}

static int group_row(data_query *query, const cdx_json *json, int row) {
    int node = pointer_node(json, row, query->options.group_by), missing = node < 0;
    cdx_json_output value;
    size_t i;
    if (node >= 0 && json->nodes[node].type > CDX_JSON_STRING) { query->stop = "invalid_group_value"; return 0; }
    cdx_json_output_init(&value);
    if (!emit_node(query, &value, json, node)) { cdx_json_output_free(&value); return 0; }
    for (i = 0; i < query->group_count; ++i) {
        data_group *group = &query->groups[i];
        if (!(i % 64u) && stopped(query)) { cdx_json_output_free(&value); return 0; }
        if (missing == group->missing && value.size == group->length && !memcmp(value.data, group->value, value.size)) {
            ++group->count; cdx_json_output_free(&value); return 1;
        }
    }
    if (query->group_count == query->options.max_groups || query->group_bytes + value.size > DATA_INPUT_MAX) {
        query->stop = "group_limit"; cdx_json_output_free(&value); return 0;
    }
    query->groups[query->group_count].value = value.data;
    query->groups[query->group_count].length = value.size;
    query->groups[query->group_count].missing = missing;
    query->groups[query->group_count].count = 1;
    ++query->group_count; query->group_bytes += value.size;
    return 1;
}

static int process_row(data_query *query, const cdx_json *json, int node) {
    if (stopped(query)) return 0;
    if (query->examined == query->options.max_records) { query->stop = "record_limit"; return 0; }
    ++query->examined;
    if (query->options.where_json && !scalar_equal(json, pointer_node(json, node, query->options.where_field),
                                                   query->options.where_json, query->options.where_value)) return 1;
    ++query->matched;
    if (!strcmp(query->options.operation, "rows")) return append_row(query, json, node);
    if (!strcmp(query->options.operation, "group_count")) return group_row(query, json, node);
    return 1;
}

static void query_json(data_query *query, const char *source, size_t length) {
    cdx_json json;
    int root, child;
    if (!cdx_json_parse(&json, source, length)) { query->stop = "parse_error"; cdx_json_free(&json); return; }
    if (stopped(query)) { cdx_json_free(&json); return; }
    root = pointer_node(&json, 0, query->options.records_path);
    if (root < 0 || (json.nodes[root].type != CDX_JSON_ARRAY && json.nodes[root].type != CDX_JSON_OBJECT)) query->stop = "invalid_records_path";
    else if (json.nodes[root].type == CDX_JSON_OBJECT) query->scan_complete = process_row(query, &json, root);
    else {
        for (child = json.nodes[root].first; child >= 0; child = json.nodes[child].next) if (!process_row(query, &json, child)) break;
        query->scan_complete = child < 0;
    }
    cdx_json_free(&json);
}

static void query_jsonl(data_query *query, const char *source, size_t length) {
    size_t cursor = 0, line = 0;
    while (cursor < length && !stopped(query)) {
        size_t start = cursor, end;
        cdx_json json;
        ++line;
        while (cursor < length && source[cursor] != '\n' && source[cursor] != '\r') ++cursor;
        end = cursor;
        if (cursor < length && source[cursor++] == '\r' && cursor < length && source[cursor] == '\n') ++cursor;
        while (start < end && (source[start] == ' ' || source[start] == '\t')) ++start;
        while (end > start && (source[end - 1u] == ' ' || source[end - 1u] == '\t')) --end;
        if (start == end) continue;
        if (!cdx_json_parse(&json, source + start, end - start)) { query->stop = "parse_error"; query->error_line = line; cdx_json_free(&json); break; }
        if (!process_row(query, &json, 0)) { cdx_json_free(&json); break; }
        cdx_json_free(&json);
    }
    query->scan_complete = cursor == length && !query->stop;
}

static int csv_record(data_query *query, csv_reader *csv, char **fields, size_t *count) {
    int finished = 0;
    *count = 0;
    while (!finished && !stopped(query)) {
        size_t write = csv->cursor;
        char *field = csv->source + write;
        int quoted = csv->cursor < csv->length && csv->source[csv->cursor] == '"', closed = !quoted, previous_cr = 0;
        char delimiter = 0;
        if (*count == DATA_CSV_COLUMNS) { query->stop = "column_limit"; return 0; }
        if (quoted) ++csv->cursor;
        while (csv->cursor < csv->length) {
            char c = csv->source[csv->cursor++];
            if (!(csv->cursor % 4096u) && stopped(query)) return 0;
            if (quoted) {
                if (c == '"') {
                    if (csv->cursor < csv->length && csv->source[csv->cursor] == '"') {
                        ++csv->cursor; csv->source[write++] = '"'; previous_cr = 0;
                    }
                    else { closed = 1; break; }
                } else {
                    if (c == '\r' || (c == '\n' && !previous_cr)) ++csv->line;
                    previous_cr = c == '\r';
                    csv->source[write++] = c;
                }
            } else {
                if (c == ',' || c == '\n' || c == '\r') { delimiter = c; break; }
                if (c == '"') { query->stop = "parse_error"; return 0; }
                csv->source[write++] = c;
            }
        }
        if (!closed) { query->stop = "parse_error"; return 0; }
        if (quoted && csv->cursor < csv->length) {
            delimiter = csv->source[csv->cursor++];
            if (delimiter != ',' && delimiter != '\n' && delimiter != '\r') { query->stop = "parse_error"; return 0; }
        }
        if (delimiter == '\r' && csv->cursor < csv->length && csv->source[csv->cursor] == '\n') ++csv->cursor;
        if (delimiter == '\r' || delimiter == '\n') ++csv->line;
        csv->source[write] = 0;
        fields[(*count)++] = field;
        finished = delimiter != ',';
    }
    return !stopped(query);
}

static void query_csv(data_query *query, const char *source, size_t length) {
    csv_reader csv;
    char *fields[DATA_CSV_COLUMNS];
    cdx_json_node nodes[DATA_CSV_COLUMNS + 1u];
    cdx_json row;
    size_t i, j, count;
    memset(&csv, 0, sizeof(csv)); memset(&row, 0, sizeof(row));
    csv.source = (char *)malloc(length + 1u); csv.length = length; csv.line = 1;
    if (!csv.source) { query->stop = "io_error"; return; }
    memcpy(csv.source, source, length); csv.source[length] = 0;
    if (!length) query->stop = "parse_error";
    if (!query->stop && csv_record(query, &csv, csv.headers, &csv.columns)) {
        for (i = 0; i < csv.columns && !stopped(query); ++i) {
            if (!*csv.headers[i]) query->stop = "invalid_csv_header";
            for (j = 0; j < i; ++j) if (!strcmp(csv.headers[i], csv.headers[j])) query->stop = "invalid_csv_header";
        }
    }
    if (query->stop) query->error_line = 1;
    while (csv.cursor < length && !stopped(query)) {
        size_t line = csv.line;
        if (!csv_record(query, &csv, fields, &count)) { query->error_line = line; break; }
        if (count != csv.columns) { query->stop = "parse_error"; query->error_line = line; break; }
        memset(nodes, 0, sizeof(nodes));
        nodes[0].type = CDX_JSON_OBJECT; nodes[0].first = 1; nodes[0].next = -1;
        for (i = 0; i < count; ++i) {
            nodes[i + 1u].type = CDX_JSON_STRING; nodes[i + 1u].key = csv.headers[i]; nodes[i + 1u].string = fields[i];
            nodes[i + 1u].first = -1; nodes[i + 1u].next = i + 1u == count ? -1 : (int)i + 2;
        }
        row.nodes = nodes; row.count = count + 1u;
        if (!process_row(query, &row, 0)) break;
    }
    query->scan_complete = csv.cursor == length && !query->stop;
    if (query->stop && !query->error_line) query->error_line = csv.line;
    free(csv.source);
}

static cdx_path_char *native_path(const char *path) {
#ifdef _WIN32
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *native = length > 0 && length <= 32768 ? (wchar_t *)malloc((size_t)length * sizeof(*native)) : NULL;
    if (native && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, native, length)) { free(native); native = NULL; }
    return native;
#else
    size_t length = strlen(path);
    char *native = (char *)malloc(length + 1u);
    if (native) memcpy(native, path, length + 1u);
    return native;
#endif
}

static const char *load_source(data_query *query, cdx_document *document, size_t *length) {
    const char *source;
    if (query->options.text) { source = query->options.text; *length = strlen(source); }
    else {
        cdx_path_char *path = native_path(query->options.path), *normalized = NULL;
        unsigned long native_error = 0;
        cdx_fs_status fs_status;
        cdx_document_status status;
        if (!path) { query->stop = "encoding_error"; return NULL; }
        fs_status = cdx_fs_normalize_path(path, query->cancellation, &normalized, &native_error);
        free(normalized);
        if (fs_status != CDX_FS_OK) { free(path); query->stop = cdx_fs_status_name(fs_status); return NULL; }
        /* Validate lexically, but preserve actual traversal through links and missing parents. */
        status = cdx_document_open(document, path, query->options.encoding); free(path);
        if (status != CDX_STATUS_OK) { query->stop = cdx_document_status_name(status); return NULL; }
        source = document->text_utf8; *length = document->text_length;
    }
    query->input_bytes = *length;
    if (*length > query->options.max_input) { query->stop = "too_large"; return NULL; }
    if (memchr(source, 0, *length)) { query->stop = "encoding_error"; return NULL; }
    if (*length >= 3u && !memcmp(source, "\xef\xbb\xbf", 3u)) { source += 3; *length -= 3u; }
    return stopped(query) ? NULL : source;
}

static void emit_groups(data_query *query) {
    size_t i;
    /* A completed scan can still return only one page of complete group counts. */
    for (i = query->options.offset; i < query->group_count && query->returned < query->options.limit; ++i) {
        cdx_json_output item;
        const data_group *group = &query->groups[i];
        if (!query->stop && stopped(query)) break;
        cdx_json_output_init(&item);
        cdx_json_raw(&item, "{\"value\":"); cdx_json_rawn(&item, group->value, group->length);
        cdx_json_raw(&item, ",\"missing\":"); cdx_json_raw(&item, group->missing ? "true" : "false");
        cdx_json_raw(&item, ",\"count\":"); cdx_json_uint(&item, group->count); cdx_json_raw(&item, "}");
        if (item.failed || query->rows.size + item.size + 1u > query->options.max_output - DATA_RESULT_RESERVE) {
            if (!query->stop) query->stop = item.failed ? "io_error" : "output_limit";
            cdx_json_output_free(&item); break;
        }
        if (query->returned) cdx_json_raw(&query->rows, ",");
        cdx_json_rawn(&query->rows, item.data, item.size); ++query->returned;
        cdx_json_output_free(&item);
    }
}

static void emit_result(data_query *query, cdx_json_output *output) {
    size_t available = !strcmp(query->options.operation, "group_count") ? query->group_count : query->matched;
    int paged = strcmp(query->options.operation, "count") != 0;
    int truncated = paged && (available > query->options.offset + query->returned || !query->scan_complete);
    const char *status = query->stop ? query->stop : query->matched ? "ok" : "no_match";
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, status, strlen(status));
    cdx_json_raw(output, ",\"operation\":"); cdx_json_quote(output, query->options.operation, strlen(query->options.operation));
    cdx_json_raw(output, ",\"read_only\":true,\"scan_complete\":"); cdx_json_raw(output, query->scan_complete ? "true" : "false");
    cdx_json_raw(output, ",\"stop_reason\":"); if (query->stop) cdx_json_quote(output, query->stop, strlen(query->stop)); else cdx_json_raw(output, "null");
    cdx_json_raw(output, ",\"input_bytes\":"); cdx_json_uint(output, query->input_bytes);
    cdx_json_raw(output, ",\"examined\":"); cdx_json_uint(output, query->examined);
    cdx_json_raw(output, ",\"matched\":"); cdx_json_uint(output, query->matched);
    cdx_json_raw(output, ",\"returned\":"); cdx_json_uint(output, query->returned);
    cdx_json_raw(output, ",\"truncated\":"); cdx_json_raw(output, truncated ? "true" : "false");
    cdx_json_raw(output, ",\"next_offset\":");
    if (truncated && query->returned) cdx_json_uint(output, query->options.offset + query->returned); else cdx_json_raw(output, "null");
    if (query->error_line) { cdx_json_raw(output, ",\"error_line\":"); cdx_json_uint(output, query->error_line); }
    if (!strcmp(query->options.operation, "count")) { cdx_json_raw(output, ",\"count\":"); cdx_json_uint(output, query->matched); }
    else {
        if (!strcmp(query->options.operation, "group_count")) { cdx_json_raw(output, ",\"group_count\":"); cdx_json_uint(output, query->group_count); }
        cdx_json_raw(output, !strcmp(query->options.operation, "rows") ? ",\"rows\":[" : ",\"groups\":[");
        if (query->rows.size) cdx_json_rawn(output, query->rows.data, query->rows.size);
        cdx_json_raw(output, "]");
    }
    cdx_json_raw(output, "}");
}

void cdx_data_query_call(const cdx_json *json, int arguments, cdx_cancel_handle cancellation, cdx_json_output *output) {
    data_query query;
    cdx_document document;
    const char *source;
    size_t length = 0, i;
    memset(&query, 0, sizeof(query)); memset(&document, 0, sizeof(document));
    if (!parse_options(&query.options, json, arguments)) {
        cdx_json_raw(output, "{\"status\":\"validation_error\",\"message\":\"Supply exactly one path/text, json/jsonl/csv format, valid RFC6901 pointers and bounded query options. group_count requires group_by; count has no pagination; select belongs to rows.\"}"); return;
    }
    query.cancellation = cancellation; query.deadline = data_milliseconds() + query.options.timeout_ms;
    cdx_json_output_init(&query.rows);
    if (!strcmp(query.options.operation, "group_count")) {
        query.groups = (data_group *)calloc(query.options.max_groups, sizeof(*query.groups));
        if (!query.groups) query.stop = "io_error";
    }
    source = stopped(&query) ? NULL : load_source(&query, &document, &length);
    if (source) {
        if (!strcmp(query.options.format, "json")) query_json(&query, source, length);
        else if (!strcmp(query.options.format, "jsonl")) query_jsonl(&query, source, length);
        else query_csv(&query, source, length);
    }
    if (!strcmp(query.options.operation, "group_count")) emit_groups(&query);
    emit_result(&query, output);
    for (i = 0; i < query.group_count; ++i) free(query.groups[i].value);
    free(query.groups); cdx_json_output_free(&query.rows); cdx_document_free(&document);
}

const char *cdx_data_query_tool_schema(void) {
    return
    "{\"name\":\"data_query\",\"title\":\"查询 JSON、JSONL 与 CSV\",\"description\":\"Use when filtering, projecting, counting or grouping JSON/JSONL/CSV records. Use read_file for text and search_text for content matches. Require format and exactly one absolute path or inline text. rows is default; count forbids offset/limit/select; group_count needs group_by. Fields use RFC6901 pointers. Numeric token spelling is exact (1 differs from 1.0); CSV cells are strings. Check scan_complete/stop_reason: incomplete counts cover a prefix. next_offset pages unchanged input, not a snapshot. Read-only native parsing; no scripts. Cooperative timeouts cannot interrupt synchronous loading/parsing.\",\"annotations\":{\"readOnlyHint\":true},\"inputSchema\":{\"type\":\"object\",\"required\":[\"format\"],\"oneOf\":[{\"required\":[\"path\"],\"not\":{\"required\":[\"text\"]}},{\"required\":[\"text\"],\"not\":{\"required\":[\"path\"]}}],\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Absolute existing data-file path; mutually exclusive with text.\"},"
    "\"text\":{\"type\":\"string\",\"description\":\"Inline data instead of path. Omit encoding with text.\"},"
    "\"format\":{\"type\":\"string\",\"enum\":[\"json\",\"jsonl\",\"csv\"],\"description\":\"Required syntax. JSONL skips blank physical lines; CSV requires unique nonempty headers.\"},"
    "\"encoding\":{\"type\":\"string\",\"enum\":[\"auto\",\"utf-8\",\"utf-8-bom\",\"utf-16-le\",\"utf-16-be\",\"cp936\",\"gbk\"],\"description\":\"Strict file decoding only; omit with text. auto accepts BOM/strict UTF-8.\"},"
    "\"operation\":{\"type\":\"string\",\"enum\":[\"rows\",\"count\",\"group_count\"],\"default\":\"rows\",\"description\":\"rows projects records; count has no pagination/select; group_count requires group_by.\"},"
    "\"records_path\":{\"type\":\"string\",\"maxLength\":1024,\"description\":\"JSON-only RFC6901 pointer to an array or one object. Default root; escape '/' as '~1', '~' as '~0'.\"},"
    "\"select\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":32,\"uniqueItems\":true,\"items\":{\"type\":\"string\",\"maxLength\":1024},\"description\":\"rows-only unique field pointers. Output keys are these pointers; missing values become null. Omit for whole records.\"},"
    "\"where\":{\"type\":\"object\",\"required\":[\"field\",\"equals\"],\"properties\":{\"field\":{\"type\":\"string\",\"maxLength\":1024,\"description\":\"RFC6901 field pointer; empty selects record. CSV Name uses /Name.\"},\"equals\":{\"type\":[\"null\",\"boolean\",\"number\",\"string\"],\"description\":\"Exact scalar type/value; numeric tokens differ lexically. CSV values are strings.\"}},\"description\":\"One scalar equality filter. Both fields required; missing values never match, even null.\"},"
    "\"group_by\":{\"type\":\"string\",\"maxLength\":1024,\"description\":\"Required only for group_count. Scalar pointer; empty means record. Missing differs from null; containers fail.\"},"
    "\"offset\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100000,\"default\":0,\"description\":\"Zero-based row/group offset; omit for count. Every call rescans unchanged input.\"},"
    "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000,\"default\":100,\"description\":\"Returned rows/groups per page, not scan limit. Omit for count.\"},"
    "\"max_records\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100000,\"default\":10000,\"description\":\"Examined-record cap including nonmatches; record_limit means partial results.\"},"
    "\"max_groups\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1024,\"default\":256,\"description\":\"Distinct group cap; group_limit means partial counts, not page size.\"},"
    "\"max_input_bytes\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1048576,\"default\":1048576,\"description\":\"Decoded UTF-8 input cap. File loader may first read up to its separate 8 MiB disk cap.\"},"
    "\"max_output_bytes\":{\"type\":\"integer\",\"minimum\":1024,\"maximum\":1048576,\"default\":65536,\"description\":\"JSON result budget excluding MCP envelope. Raise and repeat if a row/group cannot fit.\"},"
    "\"timeout_ms\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":60000,\"default\":10000,\"description\":\"Cooperative milliseconds; synchronous loading and one JSON parse may exceed it.\"}"
    "}}}";
}
