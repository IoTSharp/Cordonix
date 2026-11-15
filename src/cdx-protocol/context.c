#include "cdx/context.h"
#include "cdx/project_facts.h"
#include "cdx/failure_loop.h"
#include "cdx/diagnostic_views.h"

#include <stdio.h>
#include <string.h>

static cdx_project_facts project_facts;
static int project_facts_ready;
static cdx_failure_loop_state failure_loop;
static int failure_loop_ready;

static void result_error(cdx_json_output *output, const char *status, const char *message) {
    cdx_json_raw(output, "{\"status\":");
    cdx_json_quote(output, status, strlen(status));
    cdx_json_raw(output, ",\"message\":");
    cdx_json_quote(output, message, strlen(message));
    cdx_json_raw(output, "}");
}

static const char *string_arg(const cdx_json *json, int arguments, const char *key, int required, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    if (node < 0) {
        if (required) *valid = 0;
        return NULL;
    }
    if (json->nodes[node].type != CDX_JSON_STRING) {
        *valid = 0;
        return NULL;
    }
    return json->nodes[node].string;
}

static unsigned integer_arg(const cdx_json *json, int arguments, const char *key,
                            unsigned fallback, unsigned maximum, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    double value;
    if (node < 0) return fallback;
    if (json->nodes[node].type != CDX_JSON_NUMBER || json->nodes[node].number < 0.0 ||
        json->nodes[node].number > (double)maximum ||
        json->nodes[node].number != (double)(unsigned)json->nodes[node].number) {
        *valid = 0;
        return fallback;
    }
    value = json->nodes[node].number;
    return (unsigned)value;
}

static int bool_arg(const cdx_json *json, int arguments, const char *key, int fallback, int *valid) {
    int node = cdx_json_get(json, arguments, key);
    if (node < 0) return fallback;
    if (json->nodes[node].type != CDX_JSON_BOOL) {
        *valid = 0;
        return fallback;
    }
    return json->nodes[node].boolean;
}

static void emit_bool(cdx_json_output *output, int value) {
    cdx_json_raw(output, value ? "true" : "false");
}

static void ensure_state(void) {
    if (!project_facts_ready) {
        cdx_project_facts_init(&project_facts);
        project_facts_ready = 1;
    }
    if (!failure_loop_ready) {
        cdx_failure_loop_reset(&failure_loop);
        failure_loop_ready = 1;
    }
}

void cdx_project_facts_call(const cdx_json *request, int arguments,
                            cdx_cancel_handle cancellation, cdx_json_output *output) {
    int valid = arguments >= 0;
    const char *action;
    (void)cancellation;
    ensure_state();
    action = valid ? string_arg(request, arguments, "action", 1, &valid) : NULL;
    if (!valid || !action) { result_error(output, "validation_error", "action is required"); return; }
    if (!strcmp(action, "reset")) {
        cdx_project_facts_init(&project_facts);
        cdx_json_raw(output, "{\"status\":\"reset\"}");
        return;
    }
    if (!strcmp(action, "render")) {
        char context[8192];
        size_t written = 0u;
        cdx_fact_result result = cdx_project_facts_render_context(&project_facts, context, sizeof(context), &written);
        cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, result == CDX_FACT_OK ? "ok" : "too_large", 9u);
        cdx_json_raw(output, ",\"context\":"); cdx_json_quote(output, context, written);
        cdx_json_raw(output, ",\"bytes\":"); cdx_json_uint(output, (unsigned long long)written); cdx_json_raw(output, "}");
        return;
    }
    if (!strcmp(action, "fingerprint")) {
        cdx_failure_fingerprint fingerprint;
        const char *raw = string_arg(request, arguments, "raw", 1, &valid);
        cdx_fact_result result;
        if (!valid || !raw) { result_error(output, "validation_error", "raw is required"); return; }
        result = cdx_failure_fingerprint_make(raw, &fingerprint);
        if (result != CDX_FACT_OK) { result_error(output, result == CDX_FACT_TOO_LARGE ? "too_large" : "validation_error", "fingerprint input is invalid"); return; }
        cdx_json_raw(output, "{\"status\":\"ok\",\"classification\":");
        cdx_json_quote(output, cdx_fingerprint_class_name(fingerprint.classification), strlen(cdx_fingerprint_class_name(fingerprint.classification)));
        cdx_json_raw(output, ",\"normalized\":"); cdx_json_quote(output, fingerprint.normalized, strlen(fingerprint.normalized));
        cdx_json_raw(output, ",\"hash\":"); cdx_json_quote(output, fingerprint.hash_hex, strlen(fingerprint.hash_hex)); cdx_json_raw(output, "}");
        return;
    }
    if (!strcmp(action, "upsert") || !strcmp(action, "revoke")) {
        const char *key = string_arg(request, arguments, "key", 1, &valid);
        const char *evidence = string_arg(request, arguments, "evidence", 1, &valid);
        cdx_fact_result result;
        if (!valid || !key || !evidence) { result_error(output, "validation_error", "key and evidence are required"); return; }
        if (!strcmp(action, "revoke")) result = cdx_project_fact_revoke(&project_facts, key, evidence);
        else {
            const char *value = string_arg(request, arguments, "value", 1, &valid);
            const char *kind = string_arg(request, arguments, "kind", 1, &valid);
            cdx_fact_evidence_kind evidence_kind = kind && !strcmp(kind, "confirmation") ? CDX_FACT_CONFIRMATION :
                kind && !strcmp(kind, "contradiction") ? CDX_FACT_CONTRADICTION : 0;
            if (!valid || !value || !kind || !evidence_kind) { result_error(output, "validation_error", "value and kind are required"); return; }
            result = cdx_project_fact_upsert(&project_facts, key, value, evidence, evidence_kind);
        }
        cdx_json_raw(output, "{\"status\":");
        cdx_json_quote(output, result == CDX_FACT_OK ? "ok" : result == CDX_FACT_AMBIGUOUS ? "ambiguous" : result == CDX_FACT_FULL ? "full" : result == CDX_FACT_NOT_FOUND ? "not_found" : "validation_error", strlen(result == CDX_FACT_OK ? "ok" : result == CDX_FACT_AMBIGUOUS ? "ambiguous" : result == CDX_FACT_FULL ? "full" : result == CDX_FACT_NOT_FOUND ? "not_found" : "validation_error"));
        cdx_json_raw(output, "}");
        return;
    }
    result_error(output, "validation_error", "unsupported project_facts action");
}

void cdx_failure_loop_call(const cdx_json *request, int arguments,
                           cdx_cancel_handle cancellation, cdx_json_output *output) {
    int valid = arguments >= 0;
    const char *fingerprint;
    const char *evidence;
    const char *outcome;
    cdx_failure_observation observation;
    cdx_failure_decision decision;
    (void)cancellation;
    ensure_state();
    fingerprint = valid ? string_arg(request, arguments, "fingerprint", 1, &valid) : NULL;
    evidence = valid ? string_arg(request, arguments, "evidence", 1, &valid) : NULL;
    outcome = valid ? string_arg(request, arguments, "outcome", 1, &valid) : NULL;
    observation.turn_id = valid ? integer_arg(request, arguments, "turn_id", 0u, 1000000u, &valid) : 0u;
    observation.progress = valid ? (unsigned)bool_arg(request, arguments, "progress", 0, &valid) : 0u;
    if (!valid || !fingerprint || !evidence || !outcome || !observation.turn_id) { result_error(output, "validation_error", "fingerprint, evidence, outcome and turn_id are required"); return; }
    if (!strcmp(outcome, "failed")) observation.outcome = CDX_FAILURE_OUTCOME_FAILED;
    else if (!strcmp(outcome, "unknown")) observation.outcome = CDX_FAILURE_OUTCOME_UNKNOWN;
    else if (!strcmp(outcome, "timed_out")) observation.outcome = CDX_FAILURE_OUTCOME_TIMED_OUT;
    else if (!strcmp(outcome, "completed")) observation.outcome = CDX_FAILURE_OUTCOME_COMPLETED;
    else if (!strcmp(outcome, "cancelled")) observation.outcome = CDX_FAILURE_OUTCOME_CANCELLED;
    else { result_error(output, "validation_error", "unsupported outcome"); return; }
    observation.fingerprint = fingerprint; observation.evidence = evidence;
    if (!cdx_failure_loop_observe(&failure_loop, &observation, &decision)) { result_error(output, "validation_error", "invalid failure observation"); return; }
    cdx_json_raw(output, "{\"status\":\"ok\",\"action\":"); cdx_json_quote(output, cdx_failure_action_name(decision.action), strlen(cdx_failure_action_name(decision.action)));
    cdx_json_raw(output, ",\"repeated_count\":"); cdx_json_uint(output, decision.repeated_count);
    cdx_json_raw(output, ",\"replay_allowed\":"); emit_bool(output, decision.replay_allowed);
    cdx_json_raw(output, ",\"human_stop_required\":"); emit_bool(output, decision.human_stop_required);
    cdx_json_raw(output, ",\"reminder\":"); cdx_json_quote(output, decision.reminder, strlen(decision.reminder)); cdx_json_raw(output, "}");
}

void cdx_diagnostic_view_call(const cdx_json *request, int arguments,
                              cdx_cancel_handle cancellation, cdx_json_output *output) {
    int valid = arguments >= 0;
    const char *raw = valid ? string_arg(request, arguments, "raw_log", 1, &valid) : NULL;
    unsigned max_records = valid ? integer_arg(request, arguments, "max_records", CDX_DIAGNOSTIC_MAX_RECORDS, CDX_DIAGNOSTIC_MAX_RECORDS, &valid) : 0u;
    cdx_diagnostic_result result;
    size_t i;
    (void)cancellation;
    if (!valid || !raw || !max_records) { result_error(output, "validation_error", "raw_log and max_records are required"); return; }
    if (!cdx_diagnostic_parse(raw, strlen(raw), max_records, &result)) { result_error(output, "invalid_input", "diagnostic log is invalid"); return; }
    cdx_json_raw(output, "{\"status\":"); cdx_json_quote(output, cdx_diagnostic_status_name(result.status), strlen(cdx_diagnostic_status_name(result.status)));
    cdx_json_raw(output, ",\"count\":"); cdx_json_uint(output, (unsigned long long)result.count);
    cdx_json_raw(output, ",\"unparsed_lines\":"); cdx_json_uint(output, (unsigned long long)result.unparsed_lines);
    cdx_json_raw(output, ",\"source_bytes\":"); cdx_json_uint(output, (unsigned long long)result.source_bytes);
    cdx_json_raw(output, ",\"raw_log_available\":"); emit_bool(output, result.raw_log_available);
    cdx_json_raw(output, ",\"truncated\":"); emit_bool(output, result.truncated);
    cdx_json_raw(output, ",\"records\":[");
    for (i = 0u; i < result.count; ++i) {
        const cdx_diagnostic_record *record = &result.records[i];
        if (i) cdx_json_raw(output, ",");
        cdx_json_raw(output, "{\"kind\":"); cdx_json_quote(output, cdx_diagnostic_kind_name(record->kind), strlen(cdx_diagnostic_kind_name(record->kind)));
        cdx_json_raw(output, ",\"path\":"); cdx_json_quote(output, record->path, strlen(record->path));
        cdx_json_raw(output, ",\"line\":"); cdx_json_uint(output, record->line);
        cdx_json_raw(output, ",\"column\":"); cdx_json_uint(output, record->column);
        cdx_json_raw(output, ",\"code\":"); cdx_json_quote(output, record->code, strlen(record->code));
        cdx_json_raw(output, ",\"message\":"); cdx_json_quote(output, record->message, strlen(record->message));
        cdx_json_raw(output, ",\"source_offset\":"); cdx_json_uint(output, (unsigned long long)record->source_offset);
        cdx_json_raw(output, "}");
    }
    cdx_json_raw(output, "]}");
}

static const char project_facts_schema[] = "{\"name\":\"project_facts\",\"title\":\"维护项目事实与失败指纹\",\"description\":\"Deterministic model-free project facts and normalized failure fingerprints. Facts retain confirmation/contradiction evidence and candidate/active/revoked state. Actions do not execute commands or call a model.\",\"inputSchema\":{\"type\":\"object\",\"required\":[\"action\"],\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"upsert\",\"revoke\",\"render\",\"fingerprint\",\"reset\"],\"description\":\"Operation to perform on the bounded fact store.\"},\"key\":{\"type\":\"string\",\"description\":\"Stable fact key; literal text without NUL or line breaks.\"},\"value\":{\"type\":\"string\",\"description\":\"Observed fact value retained with its evidence.\"},\"evidence\":{\"type\":\"string\",\"description\":\"Bounded human or tool evidence supporting this observation.\"},\"kind\":{\"type\":\"string\",\"enum\":[\"confirmation\",\"contradiction\"],\"description\":\"Evidence relation for an upsert.\"},\"raw\":{\"type\":\"string\",\"description\":\"Raw failure text to normalize into a stable fingerprint.\"}}},\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":false}}";
static const char failure_loop_schema[] = "{\"name\":\"failure_loop\",\"title\":\"检查失败循环提醒\",\"description\":\"Deterministic host-side reminder for repeated failures. It emits at most one reminder per turn, requires human review for unknown or timed-out outcomes, and never replays work automatically.\",\"inputSchema\":{\"type\":\"object\",\"required\":[\"fingerprint\",\"evidence\",\"turn_id\",\"outcome\"],\"properties\":{\"fingerprint\":{\"type\":\"string\",\"description\":\"Normalized failure identity used for bounded repetition tracking.\"},\"evidence\":{\"type\":\"string\",\"description\":\"Bounded evidence text retained for human review.\"},\"turn_id\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000000,\"description\":\"Monotonically increasing host turn identifier.\"},\"progress\":{\"type\":\"boolean\",\"default\":false,\"description\":\"True records progress and resets the repeated-failure counter.\"},\"outcome\":{\"type\":\"string\",\"enum\":[\"failed\",\"unknown\",\"timed_out\",\"completed\",\"cancelled\"],\"description\":\"Observed outcome; unknown and timed_out block automatic replay.\"}}},\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":false}}";
static const char diagnostic_view_schema[] = "{\"name\":\"diagnostic_view\",\"title\":\"解析搜索与构建诊断\",\"description\":\"Parse bounded ripgrep/build/test log lines into deterministic records while retaining paths, locations, partial or unknown status, and raw-log offsets. Unparsed lines are counted and never silently discarded.\",\"inputSchema\":{\"type\":\"object\",\"required\":[\"raw_log\"],\"properties\":{\"raw_log\":{\"type\":\"string\",\"maxLength\":1048576,\"description\":\"Complete borrowed search, compiler or test log text to parse once.\"},\"max_records\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":64,\"default\":64,\"description\":\"Maximum diagnostic records; excess lines remain represented by output_limit.\"}}},\"annotations\":{\"readOnlyHint\":true}}";

const char *cdx_project_facts_tool_schema(void) { return project_facts_schema; }
const char *cdx_failure_loop_tool_schema(void) { return failure_loop_schema; }
const char *cdx_diagnostic_view_tool_schema(void) { return diagnostic_view_schema; }
