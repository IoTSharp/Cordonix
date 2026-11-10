#ifndef CDX_MCP_REQUEST_QUEUE_H
#define CDX_MCP_REQUEST_QUEUE_H
#include "cdx/json.h"
#include <stdio.h>
#include <string.h>

#define CDX_MCP_QUEUE_CAPACITY 16u
#define CDX_MCP_QUEUE_BYTES (8u * 1024u * 1024u)
#define CDX_MCP_QUEUE_WAIT_MS 1800000ull

typedef struct cdx_mcp_request {
    cdx_json json;
    int params, id;
    size_t memory_bytes;
    unsigned long long enqueued_ms;
} cdx_mcp_request;

typedef struct cdx_mcp_queue {
    cdx_mcp_request requests[CDX_MCP_QUEUE_CAPACITY];
    unsigned count;
    size_t memory_bytes;
} cdx_mcp_queue;

static inline void cdx_mcp_write_id(cdx_json_output *output, const cdx_json *json, int id) {
    if (id < 0) cdx_json_raw(output, "null");
    else if (json->nodes[id].type == CDX_JSON_STRING) cdx_json_quote(output, json->nodes[id].string, strlen(json->nodes[id].string));
    else cdx_json_rawn(output, json->nodes[id].number_text, json->nodes[id].number_length);
}

static inline int cdx_mcp_ids_equal(const cdx_json *a, int ai, const cdx_json *b, int bi) {
    if (ai < 0 || bi < 0 || a->nodes[ai].type != b->nodes[bi].type) return 0;
    if (a->nodes[ai].type == CDX_JSON_STRING) return !strcmp(a->nodes[ai].string, b->nodes[bi].string);
    return a->nodes[ai].type == CDX_JSON_NUMBER && a->nodes[ai].number_length == b->nodes[bi].number_length &&
        !memcmp(a->nodes[ai].number_text, b->nodes[bi].number_text, a->nodes[ai].number_length);
}

static inline int cdx_mcp_queue_push(cdx_mcp_queue *queue, const cdx_mcp_request *request) {
    if (queue->count == CDX_MCP_QUEUE_CAPACITY || request->memory_bytes > CDX_MCP_QUEUE_BYTES - queue->memory_bytes) return 0;
    queue->requests[queue->count++] = *request;
    queue->memory_bytes += request->memory_bytes;
    return 1;
}

static inline void cdx_mcp_queue_remove(cdx_mcp_queue *queue, unsigned index, cdx_mcp_request *request) {
    *request = queue->requests[index];
    queue->memory_bytes -= request->memory_bytes;
    --queue->count;
    memmove(queue->requests + index, queue->requests + index + 1u, (queue->count - index) * sizeof(*request));
}

static inline int cdx_mcp_queue_cancel(cdx_mcp_queue *queue, const cdx_json *json, int id, cdx_mcp_request *request) {
    unsigned index;
    for (index = 0; index < queue->count && index < CDX_MCP_QUEUE_CAPACITY; ++index) {
        if (cdx_mcp_ids_equal(json, id, &queue->requests[index].json, queue->requests[index].id)) {
            cdx_mcp_queue_remove(queue, index, request);
            return 1;
        }
    }
    return 0;
}

static inline unsigned long long cdx_mcp_queue_wait(const cdx_mcp_queue *queue, unsigned long long now) {
    unsigned long long expires;
    if (!queue->count) return CDX_MCP_QUEUE_WAIT_MS;
    expires = queue->requests[0].enqueued_ms + CDX_MCP_QUEUE_WAIT_MS;
    return expires > now ? expires - now : 0;
}

static inline void cdx_mcp_queue_error(cdx_json_output *output, const cdx_mcp_request *request,
                                     int code, const char *reason, const char *message, unsigned queued,
                                     const cdx_json *active, int active_id, int active_params,
                                     unsigned long long active_started, unsigned long long now) {
    char number[24];
    cdx_json_output_init(output);
    cdx_json_raw(output, "{\"jsonrpc\":\"2.0\",\"id\":"); cdx_mcp_write_id(output, &request->json, request->id);
    cdx_json_raw(output, ",\"error\":{\"code\":");
    (void)snprintf(number, sizeof(number), "%d", code); cdx_json_raw(output, number);
    cdx_json_raw(output, ",\"message\":"); cdx_json_quote(output, message, strlen(message));
    cdx_json_raw(output, ",\"data\":{\"reason\":"); cdx_json_quote(output, reason, strlen(reason));
    cdx_json_raw(output, ",\"request_started\":false,\"queued_requests\":"); cdx_json_uint(output, queued);
    cdx_json_raw(output, ",\"waited_ms\":"); cdx_json_uint(output, now - request->enqueued_ms);
    if (active) {
        int name = cdx_json_get(active, active_params, "name");
        cdx_json_raw(output, ",\"active_request_id\":"); cdx_mcp_write_id(output, active, active_id);
        cdx_json_raw(output, ",\"active_tool\":"); cdx_json_quote(output, active->nodes[name].string, strlen(active->nodes[name].string));
        cdx_json_raw(output, ",\"active_elapsed_ms\":"); cdx_json_uint(output, now - active_started);
    }
    cdx_json_raw(output, "}}}");
}
#endif
