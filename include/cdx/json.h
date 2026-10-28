#ifndef CDX_JSON_H
#define CDX_JSON_H
#include <stddef.h>

typedef enum cdx_json_type { CDX_JSON_NULL, CDX_JSON_BOOL, CDX_JSON_NUMBER, CDX_JSON_STRING, CDX_JSON_ARRAY, CDX_JSON_OBJECT } cdx_json_type;
typedef struct cdx_json_node {
    cdx_json_type type;
    char *key;
    char *string;
    double number;
    const char *number_text;
    size_t number_length;
    int boolean;
    int first;
    int next;
} cdx_json_node;
typedef struct cdx_json {
    char *source;
    cdx_json_node *nodes;
    size_t count;
    size_t capacity;
    const char *error;
} cdx_json;
typedef struct cdx_json_output { char *data; size_t size; size_t capacity; int failed; } cdx_json_output;

int cdx_json_parse(cdx_json *json, const char *source, size_t length);
void cdx_json_free(cdx_json *json);
int cdx_json_get(const cdx_json *json, int object, const char *key);
void cdx_json_output_init(cdx_json_output *output);
void cdx_json_output_free(cdx_json_output *output);
void cdx_json_raw(cdx_json_output *output, const char *text);
void cdx_json_rawn(cdx_json_output *output, const char *text, size_t length);
void cdx_json_quote(cdx_json_output *output, const char *text, size_t length);
void cdx_json_uint(cdx_json_output *output, unsigned long long number);
#endif
