#include "cdx/phone.h"
#include "../../src/cdx-workspace/document_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    unsigned char input[65536], output[65536];
    size_t available, read, written;
    unsigned pauses;
    const char *stop;
    cdx_phone_stream stream;
} fixture;

static unsigned total, passed;
static const char inbox[] = "<MAP-msg-listing version=\"1.0\"><msg handle=\"0000000000000001\" datetime=\"20261031T120000\" sender_addressing=\"10000\" type=\"SMS_GSM\"/></MAP-msg-listing>";
static const char hfp_idle[] = "+BRSF: 0\r\nOK\r\n+CIND: (\"service\",(0,1)),(\"callsetup\",(0-3)),(\"call\",(0,1)),(\"battchg\",(0-5))\r\nOK\r\n+CIND: 1,0,0,4\r\nOK\r\nOK\r\n";
static const char hfp_incoming[] = "+BRSF: 0\r\nOK\r\n+CIND: (\"call\",(0,1)),(\"callsetup\",(0-3))\r\nOK\r\n+CIND: 0,1\r\nOK\r\nOK\r\n";

static void record(const char *name, int ok) {
    ++total; if (ok) ++passed;
    printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
}

static size_t read_bytes(void *context, void *bytes, size_t count) {
    fixture *f = (fixture *)context;
    if (count > 3) count = 3;
    if (count > f->available - f->read) count = f->available - f->read;
    if (!count) { f->stream.error = f->stop ? f->stop : "connection_closed"; return 0; }
    memcpy(bytes, f->input + f->read, count); f->read += count; return count;
}

static size_t write_bytes(void *context, const void *bytes, size_t count) {
    fixture *f = (fixture *)context;
    if (count > 3) count = 3;
    if (count > sizeof(f->output) - f->written) { f->stream.error = "output_limit"; return 0; }
    memcpy(f->output + f->written, bytes, count); f->written += count; return count;
}

static int pause_io(void *context, unsigned ms) {
    fixture *f = (fixture *)context;
    (void)ms; ++f->pauses;
    if (f->stop) { f->stream.error = f->stop; return 0; }
    return 1;
}

static void reset(fixture *f) {
    memset(f, 0, sizeof(*f));
    f->stream.context = f; f->stream.read = read_bytes; f->stream.write = write_bytes; f->stream.pause = pause_io;
}

static void append(fixture *f, const void *bytes, size_t size) {
    if (size > sizeof(f->input) - f->available) { fprintf(stderr, "fixture overflow\n"); exit(2); }
    memcpy(f->input + f->available, bytes, size); f->available += size;
}

static void map_start(fixture *f) {
    static const unsigned char bytes[] = {0xA0,0,12,0x10,0,0x10,0,0xCB,0,0,0,1,0xA0,0,3,0xA0,0,3};
    reset(f); append(f, bytes, sizeof(bytes));
}

static void body_packet(fixture *f, const char *body, unsigned code) {
    size_t size = strlen(body);
    unsigned char prefix[] = {(unsigned char)code, (unsigned char)((size + 6) >> 8), (unsigned char)(size + 6),
        0x49, (unsigned char)((size + 3) >> 8), (unsigned char)(size + 3)};
    append(f, prefix, sizeof(prefix)); append(f, body, size);
}

static int contains(const fixture *f, const void *bytes, size_t count) {
    size_t i;
    for (i = 0; i + count <= f->written; ++i) if (!memcmp(f->output + i, bytes, count)) return 1;
    return 0;
}

static int is_text(const cdx_json *json, const char *key, const char *text) {
    int n = cdx_json_get(json, 0, key);
    return n >= 0 && json->nodes[n].type == CDX_JSON_STRING && !strcmp(json->nodes[n].string, text);
}

static int is_bool(const cdx_json *json, const char *key, int v) {
    int n = cdx_json_get(json, 0, key);
    return n >= 0 && json->nodes[n].type == CDX_JSON_BOOL && json->nodes[n].boolean == v;
}
static int is_number(const cdx_json *json, const char *key, long long value) {
    int n = cdx_json_get(json, 0, key);
    return n >= 0 && json->nodes[n].type == CDX_JSON_NUMBER && json->nodes[n].number == value;
}

static int run(fixture *f, cdx_phone_request *r, cdx_json *json) {
    cdx_json_output out;
    int ok;
    memset(json, 0, sizeof(*json)); cdx_json_output_init(&out);
    cdx_phone_protocol(r, &f->stream, &out);
    ok = !out.failed && cdx_json_parse(json, out.data, out.size);
    cdx_json_output_free(&out); return ok;
}

static void api_case(const char *name, const char *input, const char *expected) {
    cdx_json request, result;
    cdx_json_output out;
    int ok;
    memset(&request, 0, sizeof(request)); memset(&result, 0, sizeof(result)); cdx_json_output_init(&out);
    ok = cdx_json_parse(&request, input, strlen(input));
    if (ok) {
        cdx_phone_call(&request, 0, NULL, &out);
        ok = cdx_json_parse(&result, out.data, out.size) && is_text(&result, "status", expected);
    }
    record(name, ok);
    cdx_json_free(&request); cdx_json_free(&result); cdx_json_output_free(&out);
}

static void message_body(fixture *f, const char *text) {
    char message[2048];
    int n = snprintf(message, sizeof(message), "BEGIN:BMSG\r\nVERSION:1.0\r\nSTATUS:UNREAD\r\nTYPE:SMS_GSM\r\n"
        "FOLDER:telecom/msg/inbox\r\nBEGIN:BENV\r\nBEGIN:BBODY\r\nCHARSET:UTF-8\r\nLENGTH:%u\r\nBEGIN:MSG\r\n%s\r\nEND:MSG\r\n"
        "END:BBODY\r\nEND:BENV\r\nEND:BMSG\r\n", (unsigned)strlen(text) + 22u, text);
    if (n < 0 || (size_t)n >= sizeof(message)) exit(2);
    body_packet(f, message, 0xA0);
}

int main(void) {
    fixture *f = (fixture *)calloc(1, sizeof(*f));
    cdx_phone_request r;
    cdx_json result;
    char hash[65];
    int ok;
    static const unsigned char success[] = {0xA0,0,3}, reject[] = {0xC3,0,3}, no_create[] = {0x85,0,0x1D,2,0};
    if (!f) return 2;
    memset(&r, 0, sizeof(r)); r.max_entries = 20; r.timeout_ms = 1000;
    r.recipient = "+15555550123"; r.body = "中文 $(literal)\nEND:MSG"; r.handle = "0000000000000001";
    (void)cdx_sha256((const unsigned char *)inbox, strlen(inbox), hash); r.expected_sha256 = hash;

    api_case("phone mutation defaults to a no-device preview", "{\"action\":\"sms_send\",\"device_address\":\"001122334455\",\"recipient\":\"+15555550123\",\"body\":\"中文 $(literal)\\nhello\"}", "preview");
    api_case("phone rejects AT command injection", "{\"action\":\"dial\",\"device_address\":\"001122334455\",\"recipient\":\"123;ATA\"}", "validation_error");
    api_case("phone rejects zero wait", "{\"action\":\"devices\",\"timeout_ms\":0}", "validation_error");
    api_case("phone rejects unbounded entries", "{\"action\":\"devices\",\"max_entries\":51}", "validation_error");
    api_case("phone rejects unknown fields", "{\"action\":\"devices\",\"typo\":true}", "validation_error");
    api_case("sensitive read requires a snapshot", "{\"action\":\"read_code\",\"device_address\":\"001122334455\",\"message_handle\":\"0000000000000001\"}", "validation_error");
    api_case("phone requires one explicit device", "{\"action\":\"call_status\"}", "validation_error");
    api_case("call history requires one explicit device", "{\"action\":\"call_history\"}", "validation_error");
    api_case("application notifications require one explicit device", "{\"action\":\"app_notifications\"}", "validation_error");
    api_case("application notification timeout is validated before BLE", "{\"action\":\"app_notifications\",\"device_address\":\"001122334455\",\"timeout_ms\":0}", "validation_error");
    api_case("pairing previews without opening UI", "{\"action\":\"pair\",\"device_address\":\"001122334455\"}", "preview");
    api_case("connection previews without opening a socket", "{\"action\":\"connect\",\"device_address\":\"001122334455\"}", "preview");
    api_case("disconnect previews without changing a session", "{\"action\":\"disconnect\",\"device_address\":\"001122334455\"}", "preview");
    api_case("unpair previews without removing a bond", "{\"action\":\"unpair\",\"device_address\":\"001122334455\"}", "preview");
    api_case("public requests cannot supply an internal window owner", "{\"action\":\"pair\",\"device_address\":\"001122334455\",\"parent_window\":1}", "validation_error");
    api_case("discovery requires a bounded usable inquiry", "{\"action\":\"discover\",\"timeout_ms\":1000}", "validation_error");
    api_case("adapter address is validated before IO", "{\"action\":\"discover\",\"radio_address\":\"not-an-address\"}", "validation_error");
    api_case("connection lease has an upper bound", "{\"action\":\"connect\",\"device_address\":\"001122334455\",\"connection_ttl_ms\":28800001}", "validation_error");
#ifdef _WIN32
    api_case("one-shot connect requires a lifecycle owner", "{\"action\":\"connect\",\"device_address\":\"001122334455\",\"dry_run\":false}", "session_required");
    api_case("fresh connection status performs no Bluetooth IO", "{\"action\":\"connection_status\"}", "ok");
#endif
#ifndef _WIN32
    api_case("Linux reports unsupported device access", "{\"action\":\"devices\"}", "not_supported");
#endif

    r.action = CDX_PHONE_SMS_LIST; map_start(f); body_packet(f, inbox, 0xA0);
    ok = run(f, &r, &result);
    record("fragmented OBEX returns exact SMS metadata and hash", ok && is_text(&result, "data", inbox) && is_text(&result, "sha256", hash));
    record("SETPATH forbids creating phone folders", contains(f, no_create, sizeof(no_create)));
    cdx_json_free(&result);

    map_start(f); body_packet(f, "<MAP-msg-listing>", 0x90); body_packet(f, "</MAP-msg-listing>", 0xA0);
    ok = run(f, &r, &result);
    record("OBEX continuation assembles the complete payload", ok && is_text(&result, "data", "<MAP-msg-listing></MAP-msg-listing>")); cdx_json_free(&result);
    map_start(f); body_packet(f, "\xC0\xAF", 0xA0);
    ok = run(f, &r, &result);
    record("phone refuses invalid UTF8", ok && is_text(&result, "status", "encoding_error")); cdx_json_free(&result);
    reset(f); append(f, "\xA0\x00\x02", 3);
    ok = run(f, &r, &result);
    record("phone rejects undersized OBEX frame", ok && is_text(&result, "status", "protocol_error")); cdx_json_free(&result);

    r.action = CDX_PHONE_CONTACTS; map_start(f); f->available = 12;
    body_packet(f, "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:测试联系人\r\nTEL:+15555550123\r\nEND:VCARD\r\n", 0xA0);
    ok = run(f, &r, &result);
    record("PBAP returns UTF8 vCard with phone numbers", ok && is_text(&result, "status", "ok") && is_text(&result, "format", "vcard-3.0") && contains(f, "x-bt/phonebook", 13)); cdx_json_free(&result);

    r.action = CDX_PHONE_CALL_HISTORY; r.offset = 40; map_start(f); f->available = 12;
    body_packet(f, "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:测试\r\n 联系人\r\nTEL;TYPE=CELL:+15555550123\r\n"
        "TEL;TYPE=HOME:+15555550124\r\nX-IRMC-CALL-DATETIME;MISSED:20261031T120000\r\nEND:VCARD\r\n", 0xA0);
    ok = run(f, &r, &result);
    {
        static const unsigned char history_name[] = {0,'t',0,'e',0,'l',0,'e',0,'c',0,'o',0,'m',0,'/',0,'c',0,'c',0,'h',0,'.',0,'v',0,'c',0,'f',0,0};
        static const unsigned char page_params[] = {4,2,0,20,5,2,0,40,7,1,1};
        int items = cdx_json_get(&result, 0, "items"), item = items >= 0 ? result.nodes[items].first : -1;
        int name = item >= 0 ? cdx_json_get(&result, item, "name") : -1;
        int direction = item >= 0 ? cdx_json_get(&result, item, "direction") : -1;
        int numbers = item >= 0 ? cdx_json_get(&result, item, "number_count") : -1;
        record("call history requests combined phonebook with explicit offset", ok && is_text(&result, "status", "ok") &&
            contains(f, history_name, sizeof(history_name)) && contains(f, page_params, sizeof(page_params)) &&
            !contains(f, "x-bt/MAP-msg-listing", 19));
        record("history preview unfolds names and preserves missed-call and multi-number facts",
            ok && is_text(&result, "preview_status", "ok") && name >= 0 && !strcmp(result.nodes[name].string, "测试联系人") &&
            direction >= 0 && !strcmp(result.nodes[direction].string, "missed") && numbers >= 0 && result.nodes[numbers].number == 2);
        record("call history is a page without changing read state", ok && is_bool(&result, "complete_collection", 0) &&
            is_bool(&result, "read_state_modified", 0));
    }
    cdx_json_free(&result); r.offset = 0;
    map_start(f); f->available = 12; append(f, reject, sizeof(reject));
    ok = run(f, &r, &result);
    record("PBAP denial stays distinct from an empty history", ok && is_text(&result, "status", "permission_denied") &&
        cdx_json_get(&result, 0, "items") < 0); cdx_json_free(&result);
    map_start(f); f->available = 12; body_packet(f, "", 0xA0);
    ok = run(f, &r, &result);
    record("empty phonebook remains readable without claiming completeness", ok && is_text(&result, "preview_status", "ok") &&
        is_bool(&result, "complete_collection", 0)); cdx_json_free(&result);
    map_start(f); f->available = 12; body_packet(f, "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:unfinished\r\n", 0xA0);
    ok = run(f, &r, &result);
    record("unsupported vCard preview preserves raw successful transfer", ok && is_text(&result, "status", "ok") &&
        is_text(&result, "preview_status", "unsupported_format") && cdx_json_get(&result, 0, "data") >= 0); cdx_json_free(&result);
    r.action = CDX_PHONE_SMS_LIST; map_start(f);
    body_packet(f, "<?xml version=\"1.0\"?><MAP-msg-listing version=\"1.0\"><msg handle=\"0000000000000001\" "
        "sender_name = 'A &amp; B' subject=\"&#x4F60;&#22909; &quot;test&quot; &#x1F600;\"/></MAP-msg-listing>", 0xA0);
    ok = run(f, &r, &result);
    {
        int items = cdx_json_get(&result, 0, "items"), item = items >= 0 ? result.nodes[items].first : -1;
        int subject = item >= 0 ? cdx_json_get(&result, item, "subject") : -1;
        int sender = item >= 0 ? cdx_json_get(&result, item, "sender_name") : -1;
        record("SMS display decodes quoted attributes and Unicode entities", ok && is_text(&result, "preview_status", "ok") &&
            sender >= 0 && !strcmp(result.nodes[sender].string, "A & B") &&
            subject >= 0 && !strcmp(result.nodes[subject].string, "你好 \"test\" 😀"));
    }
    cdx_json_free(&result);
    map_start(f); body_packet(f, "<MAP-msg-listing><msg subject=\"&custom;\"/></MAP-msg-listing>", 0xA0);
    ok = run(f, &r, &result);
    record("unrecognized XML entities never produce misleading display text", ok && is_text(&result, "preview_status", "unsupported_format"));
    cdx_json_free(&result);

    r.action = CDX_PHONE_SMS_READ; map_start(f); body_packet(f, inbox, 0xA0); message_body(f, "中文 $(literal)\r\n/END:MSG");
    ok = run(f, &r, &result);
    record("SMS read extracts only text and unescapes delimiter", ok && is_text(&result, "body", "中文 $(literal)\r\nEND:MSG") && is_bool(&result, "read_state_modified", 0)); cdx_json_free(&result);
    map_start(f); body_packet(f, "<MAP-msg-listing/>", 0xA0);
    ok = run(f, &r, &result);
    record("changed snapshot prevents fetching sensitive body", ok && is_text(&result, "status", "snapshot_changed") && !contains(f, "x-bt/message", 12)); cdx_json_free(&result);
    r.action = CDX_PHONE_READ_CODE; map_start(f); body_packet(f, inbox, 0xA0); message_body(f, "验证码：123456，请勿泄露。");
    ok = run(f, &r, &result);
    record("OTP returns code without the SMS body", ok && is_text(&result, "code", "123456") && cdx_json_get(&result, 0, "body") < 0); cdx_json_free(&result);
    map_start(f); body_packet(f, inbox, 0xA0); message_body(f, "验证码 123456 或 654321");
    ok = run(f, &r, &result);
    record("multiple codes remain ambiguous", ok && is_text(&result, "status", "ambiguous") && cdx_json_get(&result, 0, "code") < 0); cdx_json_free(&result);
    map_start(f); body_packet(f, inbox, 0xA0); message_body(f, "订单 123456");
    ok = run(f, &r, &result);
    record("unlabelled digits are not a verification code", ok && is_text(&result, "status", "no_match")); cdx_json_free(&result);

    r.action = CDX_PHONE_SMS_READ; map_start(f); body_packet(f, inbox, 0xA0);
    body_packet(f, "BEGIN:BMSG\r\nVERSION:1.0\r\nSTATUS:UNREAD\r\nTYPE:MMS\r\nFOLDER:telecom/msg/inbox\r\nBEGIN:BENV\r\nBEGIN:BBODY\r\nCHARSET:UTF-8\r\nLENGTH:48\r\nBEGIN:MSG\r\nTYPE:SMS_GSM\r\n验证码 123456\r\nEND:MSG\r\nEND:BBODY\r\nEND:BENV\r\nEND:BMSG\r\n", 0xA0);
    ok = run(f, &r, &result);
    record("SMS type is parsed from the envelope, never the body", ok && is_text(&result, "status", "unsupported_message")); cdx_json_free(&result);
    map_start(f); body_packet(f, inbox, 0xA0); message_body(f, "END:MSG\r\ninjected text");
    ok = run(f, &r, &result);
    record("unescaped message delimiters are rejected", ok && is_text(&result, "status", "protocol_error")); cdx_json_free(&result);
    map_start(f); body_packet(f, inbox, 0xA0); r.handle = "0000000000000002";
    ok = run(f, &r, &result);
    record("missing message handle does not fetch a body", ok && is_text(&result, "status", "no_match") && !contains(f, "x-bt/message", 12)); cdx_json_free(&result);
    r.handle = "0000000000000001";

    r.action = CDX_PHONE_SMS_LIST; map_start(f);
    { unsigned page; char payload[1001]; memset(payload, 'x', 1000); payload[1000] = 0;
      for (page = 0; page < 33; ++page) body_packet(f, payload, 0x90); }
    ok = run(f, &r, &result);
    record("phone body limit remains explicit", ok && is_text(&result, "status", "output_limit")); cdx_json_free(&result);

    r.action = CDX_PHONE_SMS_SEND; map_start(f); append(f, success, sizeof(success));
    ok = run(f, &r, &result);
    record("SMS acceptance does not claim delivery", ok && is_text(&result, "status", "accepted") && is_bool(&result, "delivery_confirmed", 0));
    record("SMS uses literal Unicode text and escapes END MSG", contains(f, "中文 $(literal)\r\n/END:MSG", strlen("中文 $(literal)\r\n/END:MSG"))); cdx_json_free(&result);
    map_start(f); f->stop = "timed_out";
    ok = run(f, &r, &result);
    record("timeout after SMS submission is unknown without retry", ok && is_text(&result, "status", "unknown") && is_bool(&result, "auto_retry", 0) && f->read == f->available); cdx_json_free(&result);
    map_start(f); append(f, reject, sizeof(reject));
    ok = run(f, &r, &result);
    record("explicit phone rejection remains distinct from unknown", ok && is_text(&result, "status", "permission_denied")); cdx_json_free(&result);
    map_start(f); f->input[5] = 0; f->input[6] = 255;
    ok = run(f, &r, &result);
    record("SMS packet size is checked before submission", ok && is_text(&result, "status", "output_limit") && is_bool(&result, "submitted", 0)); cdx_json_free(&result);

    r.action = CDX_PHONE_WAIT_SMS; map_start(f); body_packet(f, inbox, 0xA0); f->stop = "cancelled";
    ok = run(f, &r, &result);
    record("SMS waiting has a cancellation path", ok && is_text(&result, "status", "cancelled") && f->pauses == 1); cdx_json_free(&result);
    map_start(f); body_packet(f, "<MAP-msg-listing/>", 0xA0);
    ok = run(f, &r, &result);
    record("SMS waiting returns a changed bounded snapshot", ok && is_text(&result, "status", "ok") && f->pauses == 0); cdx_json_free(&result);

    r.action = CDX_PHONE_CALL_STATUS; reset(f); append(f, hfp_idle, strlen(hfp_idle));
    ok = run(f, &r, &result);
    record("HFP resolves indicator order from the phone", ok && is_text(&result, "call_state", "idle") && is_bool(&result, "audio_available", 0));
    record("HFP exposes the optional phone battery indicator", ok && is_bool(&result, "battery_supported", 1) &&
        is_number(&result, "battery_level", 4) && is_number(&result, "battery_percent", 80)); cdx_json_free(&result);
    r.action = CDX_PHONE_DIAL; reset(f); append(f, hfp_idle, strlen(hfp_idle)); append(f, "OK\r\n", 4);
    ok = run(f, &r, &result);
    record("dial submits one literal AT command", ok && is_text(&result, "status", "accepted") && contains(f, "ATD+15555550123;\r", 16) && is_bool(&result, "state_verified", 0)); cdx_json_free(&result);
    r.action = CDX_PHONE_ANSWER; reset(f); append(f, hfp_idle, strlen(hfp_idle));
    ok = run(f, &r, &result);
    record("answer requires an observed incoming call", ok && is_text(&result, "status", "call_state_changed") && !contains(f, "ATA\r", 4)); cdx_json_free(&result);
    reset(f); append(f, hfp_incoming, strlen(hfp_incoming)); append(f, "OK\r\n", 4);
    ok = run(f, &r, &result);
    record("answer uses the incoming call control channel", ok && is_text(&result, "status", "accepted") && contains(f, "ATA\r", 4)); cdx_json_free(&result);
    r.action = CDX_PHONE_HANGUP; reset(f); append(f, hfp_incoming, strlen(hfp_incoming)); f->stop = "cancelled";
    ok = run(f, &r, &result);
    record("cancel after call mutation preserves unknown", ok && is_text(&result, "status", "unknown") && contains(f, "AT+CHUP\r", 8)); cdx_json_free(&result);
    r.action = CDX_PHONE_WAIT_CALL; reset(f); append(f, hfp_idle, strlen(hfp_idle)); append(f, "+CIND: 1,1,0\r\nOK\r\n", 18);
    ok = run(f, &r, &result);
    record("bounded call wait observes incoming state", ok && is_text(&result, "call_state", "incoming") && f->pauses == 1); cdx_json_free(&result);
    reset(f); append(f, hfp_idle, strlen(hfp_idle)); f->stop = "timed_out";
    ok = run(f, &r, &result);
    record("call wait timeout is not idle or success", ok && is_text(&result, "status", "timed_out")); cdx_json_free(&result);

    {
        cdx_phone_hfp state = {0};
        cdx_json_output output;
        reset(f); append(f, hfp_idle, strlen(hfp_idle));
        r.action = CDX_PHONE_CALL_STATUS; f->stream.retain_connection = 1;
        cdx_json_output_init(&output);
        cdx_phone_protocol_connected(&r, &f->stream, &state, &output);
        ok = cdx_json_parse(&result, output.data, output.size);
        record("retained HFP completes one service handshake", ok && state.initialized && is_bool(&result, "connection_retained", 1));
        cdx_json_free(&result); cdx_json_output_free(&output);
        reset(f); f->stream.retain_connection = 1;
        append(f, "+CIND: 1,1,0\r\nOK\r\n", strlen("+CIND: 1,1,0\r\nOK\r\n"));
        cdx_json_output_init(&output);
        cdx_phone_protocol_connected(&r, &f->stream, &state, &output);
        ok = cdx_json_parse(&result, output.data, output.size);
        record("retained HFP reuses negotiated mapping and observes incoming", ok && is_text(&result, "call_state", "incoming") &&
            contains(f, "AT+CIND?\r", 9) && !contains(f, "AT+BRSF", 7));
        cdx_json_free(&result); cdx_json_output_free(&output);
    }
    {
        static const unsigned char expected[] = {0,0x78,0x56,0x34,0x12,0,1,255,0,3,255,1};
        static const unsigned char response[] = {0,0x78,0x56,0x34,0x12,0,3,0,'a','p','p',1,3,0,0xE4,0xBD,0xA0,3,2,0,'O','K'};
        unsigned char command[12], bad[sizeof(response)];
        cdx_phone_ancs_attributes attributes = {0};
        size_t n;
        cdx_phone_ancs_command(0x12345678, command);
        record("ANCS command includes title and message maximum lengths", !memcmp(command, expected, sizeof(expected)));
        ok = 1;
        for (n = 0; n < sizeof(response); ++n)
            if (cdx_phone_ancs_parse(response, n, 0x12345678, &attributes) != 0) ok = 0;
        record("ANCS preserves header and UTF-8 across every fragment boundary", ok &&
            cdx_phone_ancs_parse(response, sizeof(response), 0x12345678, &attributes) == 1 &&
            !strcmp(attributes.app, "app") && !strcmp(attributes.title, "你") && !strcmp(attributes.message, "OK"));
        record("ANCS rejects a response for another notification", cdx_phone_ancs_parse(response, sizeof(response), 1, &attributes) == -1);
        memcpy(bad, response, sizeof(bad)); bad[17] = 1;
        record("ANCS rejects duplicate attributes", cdx_phone_ancs_parse(bad, sizeof(bad), 0x12345678, &attributes) == -1);
        memcpy(bad, response, sizeof(bad)); bad[12] = 0xff; bad[13] = 0xff;
        record("ANCS rejects oversized attributes before reading", cdx_phone_ancs_parse(bad, sizeof(bad), 0x12345678, &attributes) == -1);
        memcpy(bad, response, sizeof(bad)); bad[14] = 0xff;
        record("ANCS rejects invalid UTF-8", cdx_phone_ancs_parse(bad, sizeof(bad), 0x12345678, &attributes) == -1);
    }
    free(f); printf("%u/%u phone checks passed\n", passed, total);
    return passed == total ? 0 : 1;
}
