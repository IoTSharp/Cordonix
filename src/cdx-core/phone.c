#include "cdx/phone.h"
#include "../cdx-workspace/document_internal.h"
#include <math.h>
#include <string.h>

static const char *const actions[] = {
    "devices", "sms_list", "sms_read", "sms_send", "contacts", "call_status",
    "dial", "answer", "hangup", "wait_call", "wait_sms", "read_code",
    "adapters", "discover", "pair", "connect", "disconnect", "connection_status", "unpair", "call_history", "app_notifications"
};

void cdx_phone_error(cdx_json_output *out, const char *error, int submitted) {
    const char *status = submitted && strcmp(error, "rejected") && strcmp(error, "permission_denied") ? "unknown" : error;
    cdx_json_raw(out, "{\"status\":");
    cdx_json_quote(out, status, strlen(status));
    cdx_json_raw(out, ",\"reason\":"); cdx_json_quote(out, error, strlen(error));
    cdx_json_raw(out, submitted ? ",\"submitted\":true" : ",\"submitted\":false");
    cdx_json_raw(out, ",\"auto_retry\":false,\"audio_available\":false}");
}

static const char *text_arg(const cdx_json *json, int object, const char *key, int *ok) {
    int n = cdx_json_get(json, object, key);
    if (n < 0) return "";
    if (json->nodes[n].type != CDX_JSON_STRING) { *ok = 0; return ""; }
    return json->nodes[n].string;
}

static unsigned number_arg(const cdx_json *json, int object, const char *key,
                           unsigned fallback, unsigned min, unsigned max, int *ok) {
    int n = cdx_json_get(json, object, key);
    double v;
    if (n < 0) return fallback;
    v = json->nodes[n].number;
    if (json->nodes[n].type != CDX_JSON_NUMBER || v < min || v > max || floor(v) != v) {
        *ok = 0; return fallback;
    }
    return (unsigned)v;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_string(const char *s, size_t size) {
    size_t i;
    if (strlen(s) != size) return 0;
    for (i = 0; i < size; ++i) if (hex_value(s[i]) < 0) return 0;
    return 1;
}

static int recipient_valid(const char *s) {
    size_t i = s[0] == '+' ? 1u : 0u, digits = strlen(s) - i;
    if (digits < 3 || digits > 20) return 0;
    for (; s[i]; ++i) if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

static int decode(const cdx_json *json, int object, cdx_phone_request *r, int helper) {
    static const char *const keys[] = {"action", "device_address", "recipient", "body", "message_handle",
        "expected_sha256", "timeout_ms", "max_entries", "offset", "dry_run", "radio_address", "connection_ttl_ms", "parent_window"};
    int ok = 1, n;
    size_t i;
    const char *action = text_arg(json, object, "action", &ok);
    const char *address = text_arg(json, object, "device_address", &ok);
    const char *radio = text_arg(json, object, "radio_address", &ok);
    memset(r, 0, sizeof(*r));
    for (n = json->nodes[object].first; n >= 0; n = json->nodes[n].next) {
        for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) if (!strcmp(json->nodes[n].key, keys[i])) break;
        if (i == sizeof(keys) / sizeof(keys[0])) return 0;
        if (!helper && !strcmp(json->nodes[n].key, "parent_window")) return 0;
    }
    for (i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) if (!strcmp(action, actions[i])) break;
    if (i == sizeof(actions) / sizeof(actions[0])) return 0;
    r->action = (cdx_phone_action)i;
    r->recipient = text_arg(json, object, "recipient", &ok);
    r->body = text_arg(json, object, "body", &ok);
    r->handle = text_arg(json, object, "message_handle", &ok);
    r->expected_sha256 = text_arg(json, object, "expected_sha256", &ok);
    r->timeout_ms = number_arg(json, object, "timeout_ms", 15000, 1, 60000, &ok);
    r->max_entries = number_arg(json, object, "max_entries", 20, 1, 50, &ok);
    r->offset = number_arg(json, object, "offset", 0, 0, 65535, &ok);
    r->connection_ttl_ms = number_arg(json, object, "connection_ttl_ms", 3600000, 1000, 28800000, &ok);
    if (helper) r->parent_window = number_arg(json, object, "parent_window", 0, 0, 0xffffffffu, &ok);
    r->dry_run = 1;
    n = cdx_json_get(json, object, "dry_run");
    if (n >= 0) {
        if (json->nodes[n].type != CDX_JSON_BOOL) ok = 0;
        else r->dry_run = json->nodes[n].boolean;
    }
    if (r->action != CDX_PHONE_DEVICES && r->action != CDX_PHONE_ADAPTERS &&
        r->action != CDX_PHONE_DISCOVER && r->action != CDX_PHONE_CONNECTION_STATUS) {
        if (!hex_string(address, 12)) return 0;
        for (i = 0; i < 12; ++i) r->address = (r->address << 4) | (unsigned)hex_value(address[i]);
        if (!r->address) return 0;
    }
    if (*radio) {
        if (!hex_string(radio, 12)) return 0;
        for (i = 0; i < 12; ++i) r->radio_address = (r->radio_address << 4) | (unsigned)hex_value(radio[i]);
        if (!r->radio_address) return 0;
    }
    if (r->action == CDX_PHONE_DISCOVER && r->timeout_ms < 2000) ok = 0;
    if ((r->action == CDX_PHONE_DIAL || r->action == CDX_PHONE_SMS_SEND) && !recipient_valid(r->recipient)) ok = 0;
    if (r->action == CDX_PHONE_SMS_SEND && (!*r->body || strlen(r->body) > 1920 ||
        !cdx_validate_utf8(r->body, strlen(r->body)))) ok = 0;
    if (r->action == CDX_PHONE_SMS_READ || r->action == CDX_PHONE_READ_CODE) {
        if (!hex_string(r->handle, 16) || !hex_string(r->expected_sha256, 64)) ok = 0;
    }
    if (r->action == CDX_PHONE_WAIT_SMS && !hex_string(r->expected_sha256, 64)) ok = 0;
    return ok;
}

void cdx_phone_session_call(cdx_phone_session *session, const cdx_json *json, int object,
                          cdx_cancel_handle cancel, cdx_json_output *out) {
    cdx_phone_request r;
    if (!decode(json, object, &r, 0)) { cdx_phone_error(out, "validation_error", 0); return; }
    if (r.dry_run && (r.action == CDX_PHONE_SMS_SEND || r.action == CDX_PHONE_DIAL ||
                     r.action == CDX_PHONE_ANSWER || r.action == CDX_PHONE_HANGUP ||
                     r.action == CDX_PHONE_PAIR || r.action == CDX_PHONE_CONNECT || r.action == CDX_PHONE_DISCONNECT || r.action == CDX_PHONE_UNPAIR)) {
        cdx_json_raw(out, "{\"status\":\"preview\",\"submitted\":false,\"action\":");
        cdx_json_quote(out, actions[r.action], strlen(actions[r.action]));
        cdx_json_raw(out, ",\"recipient\":"); cdx_json_quote(out, r.recipient, strlen(r.recipient));
        cdx_json_raw(out, ",\"body_bytes\":"); cdx_json_uint(out, strlen(r.body));
        cdx_json_raw(out, ",\"audio_available\":false,\"auto_retry\":false}");
        return;
    }
    cdx_phone_session_platform(session, &r, cancel, out);
}

void cdx_phone_call(const cdx_json *json, int object, cdx_cancel_handle cancel, cdx_json_output *out) {
    cdx_phone_session_call(NULL, json, object, cancel, out);
}

void cdx_phone_helper(const cdx_json *json, int object, cdx_json_output *out) {
    cdx_phone_request r;
    if (!decode(json, object, &r, 1) || (r.action != CDX_PHONE_DISCOVER && r.action != CDX_PHONE_PAIR && r.action != CDX_PHONE_UNPAIR) || r.dry_run) {
        cdx_phone_error(out, "validation_error", 0); return;
    }
    cdx_phone_platform_helper(&r, out);
}
