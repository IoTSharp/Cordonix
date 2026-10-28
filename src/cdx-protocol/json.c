#include "cdx/json.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CDX_JSON_INPUT_LIMIT (1024u * 1024u)
#define CDX_JSON_NODE_LIMIT 65536u
#define CDX_JSON_OUTPUT_LIMIT (8u * 1024u * 1024u)

static void skip_space(char **cursor) {
    while (**cursor == ' ' || **cursor == '\t' || **cursor == '\r' || **cursor == '\n') ++*cursor;
}

static int hex4(char **cursor, unsigned *value) {
    unsigned result = 0;
    unsigned i;
    for (i = 0; i < 4; ++i) {
        unsigned char c = (unsigned char)**cursor;
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10u;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10u;
        else return 0;
        result = result * 16u + digit;
        ++*cursor;
    }
    *value = result;
    return 1;
}

static char *parse_string(char **cursor) {
    char *start, *write;
    if (**cursor != '"') return NULL;
    start = write = ++*cursor;
    while (**cursor && **cursor != '"') {
        unsigned char c = (unsigned char)*(*cursor)++;
        if (c < 0x20) return NULL;
        if (c != '\\') { *write++ = (char)c; continue; }
        c = (unsigned char)*(*cursor)++;
        switch (c) {
        case '"': case '\\': case '/': *write++ = (char)c; break;
        case 'b': *write++ = '\b'; break;
        case 'f': *write++ = '\f'; break;
        case 'n': *write++ = '\n'; break;
        case 'r': *write++ = '\r'; break;
        case 't': *write++ = '\t'; break;
        case 'u': {
            unsigned point, low;
            if (!hex4(cursor, &point) || point == 0) return NULL;
            if (point >= 0xd800 && point <= 0xdbff) {
                if ((*cursor)[0] != '\\' || (*cursor)[1] != 'u') return NULL;
                *cursor += 2;
                if (!hex4(cursor, &low) || low < 0xdc00 || low > 0xdfff) return NULL;
                point = 0x10000u + ((point - 0xd800u) << 10) + low - 0xdc00u;
            } else if (point >= 0xdc00 && point <= 0xdfff) return NULL;
            if (point < 0x80) *write++ = (char)point;
            else if (point < 0x800) {
                *write++ = (char)(0xc0u | (point >> 6)); *write++ = (char)(0x80u | (point & 63));
            } else if (point < 0x10000) {
                *write++ = (char)(0xe0u | (point >> 12)); *write++ = (char)(0x80u | ((point >> 6) & 63)); *write++ = (char)(0x80u | (point & 63));
            } else {
                *write++ = (char)(0xf0u | (point >> 18)); *write++ = (char)(0x80u | ((point >> 12) & 63));
                *write++ = (char)(0x80u | ((point >> 6) & 63)); *write++ = (char)(0x80u | (point & 63));
            }
            break;
        }
        default: return NULL;
        }
    }
    if (**cursor != '"') return NULL;
    ++*cursor;
    *write = '\0';
    return start;
}

static int new_node(cdx_json *json) {
    size_t next_capacity;
    cdx_json_node *nodes;
    int index;
    if (json->count == CDX_JSON_NODE_LIMIT) return -1;
    if (json->count == json->capacity) {
        next_capacity = json->capacity ? json->capacity * 2u : 128u;
        nodes = (cdx_json_node *)realloc(json->nodes, next_capacity * sizeof(*nodes));
        if (!nodes) return -1;
        json->nodes = nodes;
        json->capacity = next_capacity;
    }
    index = (int)json->count++;
    memset(&json->nodes[index], 0, sizeof(json->nodes[index]));
    json->nodes[index].first = json->nodes[index].next = -1;
    return index;
}

static int parse_value(cdx_json *json, char **cursor, unsigned depth) {
    int node, last = -1;
    char c;
    if (depth > 32) return -1;
    skip_space(cursor);
    node = new_node(json);
    if (node < 0) return -1;
    c = **cursor;
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        json->nodes[node].type = c == '{' ? CDX_JSON_OBJECT : CDX_JSON_ARRAY;
        ++*cursor; skip_space(cursor);
        if (**cursor == close) { ++*cursor; return node; }
        for (;;) {
            char *key = NULL;
            int child;
            if (c == '{') {
                int previous;
                key = parse_string(cursor);
                if (!key) return -1;
                for (previous = json->nodes[node].first; previous >= 0; previous = json->nodes[previous].next)
                    if (!strcmp(json->nodes[previous].key, key)) return -1;
                skip_space(cursor);
                if (*(*cursor)++ != ':') return -1;
            }
            child = parse_value(json, cursor, depth + 1);
            if (child < 0) return -1;
            json->nodes[child].key = key;
            if (last < 0) json->nodes[node].first = child;
            else json->nodes[last].next = child;
            last = child;
            skip_space(cursor);
            if (**cursor == close) { ++*cursor; return node; }
            if (*(*cursor)++ != ',') return -1;
            skip_space(cursor);
        }
    }
    if (c == '"') {
        json->nodes[node].type = CDX_JSON_STRING;
        json->nodes[node].string = parse_string(cursor);
        return json->nodes[node].string ? node : -1;
    }
    if (!strncmp(*cursor, "true", 4)) { json->nodes[node].type = CDX_JSON_BOOL; json->nodes[node].boolean = 1; *cursor += 4; return node; }
    if (!strncmp(*cursor, "false", 5)) { json->nodes[node].type = CDX_JSON_BOOL; *cursor += 5; return node; }
    if (!strncmp(*cursor, "null", 4)) { *cursor += 4; return node; }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char *start = *cursor, *end;
        if (**cursor == '-') ++*cursor;
        if (**cursor == '0') ++*cursor;
        else {
            if (**cursor < '1' || **cursor > '9') return -1;
            do { ++*cursor; } while (**cursor >= '0' && **cursor <= '9');
        }
        if (**cursor == '.') {
            ++*cursor;
            if (**cursor < '0' || **cursor > '9') return -1;
            do { ++*cursor; } while (**cursor >= '0' && **cursor <= '9');
        }
        if (**cursor == 'e' || **cursor == 'E') {
            ++*cursor;
            if (**cursor == '+' || **cursor == '-') ++*cursor;
            if (**cursor < '0' || **cursor > '9') return -1;
            do { ++*cursor; } while (**cursor >= '0' && **cursor <= '9');
        }
        json->nodes[node].type = CDX_JSON_NUMBER;
        json->nodes[node].number_text = start;
        json->nodes[node].number_length = (size_t)(*cursor - start);
        json->nodes[node].number = strtod(start, &end);
        return end == *cursor && isfinite(json->nodes[node].number) ? node : -1;
    }
    return -1;
}

static int valid_utf8(const unsigned char *source, size_t length) {
    size_t i = 0;
    while (i < length) {
        unsigned c = source[i++], count, point, minimum;
        if (c < 0x80) continue;
        if (c >= 0xc2 && c <= 0xdf) { count = 1; point = c & 0x1fu; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { count = 2; point = c & 0x0fu; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { count = 3; point = c & 7u; minimum = 0x10000; }
        else return 0;
        if (length - i < count) return 0;
        for (; count; --count) {
            c = source[i++];
            if ((c & 0xc0u) != 0x80u) return 0;
            point = (point << 6) | (c & 0x3fu);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return 0;
    }
    return 1;
}
int cdx_json_parse(cdx_json *json, const char *source, size_t length) {
    char *cursor;
    memset(json, 0, sizeof(*json));
    if (!source || !length || length > CDX_JSON_INPUT_LIMIT || memchr(source, 0, length)) { json->error = "invalid JSON input size or NUL"; return 0; }
    if (!valid_utf8((const unsigned char *)source, length)) { json->error = "invalid UTF-8"; return 0; }
    json->source = (char *)calloc(length + 8u, 1);
    if (!json->source) { json->error = "out of memory"; return 0; }
    memcpy(json->source, source, length);
    cursor = json->source;
    if (parse_value(json, &cursor, 0) < 0) { json->error = "invalid JSON, duplicate key, NUL string, or depth limit"; return 0; }
    skip_space(&cursor);
    if (*cursor) { json->error = "trailing JSON data"; return 0; }
    return 1;
}

void cdx_json_free(cdx_json *json) { free(json->source); free(json->nodes); memset(json, 0, sizeof(*json)); }
int cdx_json_get(const cdx_json *json, int object, const char *key) {
    int child;
    if (object < 0 || (size_t)object >= json->count || json->nodes[object].type != CDX_JSON_OBJECT) return -1;
    for (child = json->nodes[object].first; child >= 0; child = json->nodes[child].next)
        if (!strcmp(json->nodes[child].key, key)) return child;
    return -1;
}
void cdx_json_output_init(cdx_json_output *output) { memset(output, 0, sizeof(*output)); }
void cdx_json_output_free(cdx_json_output *output) { free(output->data); memset(output, 0, sizeof(*output)); }
static void write_bytes(cdx_json_output *output, const char *text, size_t size) {
    size_t needed = output->size + size + 1, capacity;
    char *data;
    if (output->failed) return;
    if (needed > CDX_JSON_OUTPUT_LIMIT) { output->failed = 1; return; }
    if (needed > output->capacity) {
        capacity = output->capacity ? output->capacity : 512;
        while (capacity < needed) capacity *= 2;
        data = (char *)realloc(output->data, capacity);
        if (!data) { output->failed = 1; return; }
        output->data = data; output->capacity = capacity;
    }
    memcpy(output->data + output->size, text, size); output->size += size; output->data[output->size] = 0;
}
void cdx_json_raw(cdx_json_output *output, const char *text) { write_bytes(output, text, strlen(text)); }
void cdx_json_rawn(cdx_json_output *output, const char *text, size_t length) { write_bytes(output, text, length); }
void cdx_json_quote(cdx_json_output *output, const char *text, size_t length) {
    size_t i;
    write_bytes(output, "\"", 1);
    for (i = 0; i < length && !output->failed; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') { char pair[2] = {'\\', (char)c}; write_bytes(output, pair, 2); }
        else if (c < 32) { char escape[7]; (void)snprintf(escape, sizeof(escape), "\\u%04x", (unsigned)c); write_bytes(output, escape, 6); }
        else write_bytes(output, text + i, 1);
    }
    write_bytes(output, "\"", 1);
}
void cdx_json_uint(cdx_json_output *output, unsigned long long number) { char buffer[32]; (void)snprintf(buffer, sizeof(buffer), "%llu", number); cdx_json_raw(output, buffer); }
