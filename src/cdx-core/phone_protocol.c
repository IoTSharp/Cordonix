#include "cdx/phone.h"
#include "../cdx-workspace/document_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACKET_MAX 4096u
#define BODY_MAX 32768u

void cdx_phone_ancs_command(uint32_t uid, unsigned char command[12]) {
    static const unsigned char attributes[] = {0, 1, 255, 0, 3, 255, 1};
    command[0] = 0;
    command[1] = (unsigned char)uid; command[2] = (unsigned char)(uid >> 8);
    command[3] = (unsigned char)(uid >> 16); command[4] = (unsigned char)(uid >> 24);
    memcpy(command + 5, attributes, sizeof(attributes));
}

int cdx_phone_ancs_parse(const unsigned char *bytes, size_t size, uint32_t uid, cdx_phone_ancs_attributes *result) {
    size_t at = 5;
    unsigned seen = 0;
    uint32_t received;
    cdx_phone_ancs_attributes parsed = {0};
    if (size < 5) return 0;
    received = (uint32_t)bytes[1] | ((uint32_t)bytes[2] << 8) | ((uint32_t)bytes[3] << 16) | ((uint32_t)bytes[4] << 24);
    if (bytes[0] != 0 || received != uid) return -1;
    while (seen != 7) {
        unsigned attribute, bit;
        size_t length, capacity;
        char *target;
        if (size - at < 3) return 0;
        attribute = bytes[at]; length = (size_t)bytes[at + 1] | ((size_t)bytes[at + 2] << 8); at += 3;
        if (attribute == 0) { target = parsed.app; capacity = sizeof(parsed.app); bit = 1; }
        else if (attribute == 1) { target = parsed.title; capacity = sizeof(parsed.title); bit = 2; }
        else if (attribute == 3) { target = parsed.message; capacity = sizeof(parsed.message); bit = 4; }
        else return -1;
        if ((seen & bit) || length >= capacity) return -1;
        if (length > size - at) return 0;
        if (memchr(bytes + at, 0, length) || !cdx_validate_utf8((const char *)bytes + at, length)) return -1;
        memcpy(target, bytes + at, length); target[length] = 0;
        at += length; seen |= bit;
    }
    if (at != size) return -1;
    *result = parsed;
    return 1;
}

typedef struct phone_protocol {
    cdx_phone_stream *io;
    unsigned char packet[PACKET_MAX], response[PACKET_MAX];
    size_t size, received;
    unsigned peer_max;
    unsigned long connection;
    int has_connection, submitted;
    char body[BODY_MAX + 1u];
    size_t body_size;
} phone_protocol;

static int fail(phone_protocol *p, const char *error) { p->io->error = error; return 0; }

static int transfer(phone_protocol *p, void *buffer, size_t size, int writing) {
    size_t done = 0, steps;
    for (steps = 0; done < size && steps < BODY_MAX; ++steps) {
        size_t n = writing ? p->io->write(p->io->context, (unsigned char *)buffer + done, size - done)
                           : p->io->read(p->io->context, (unsigned char *)buffer + done, size - done);
        if (!n) return 0;
        done += n;
    }
    return done == size ? 1 : fail(p, "output_limit");
}

static unsigned be16(const unsigned char *b) { return ((unsigned)b[0] << 8) | b[1]; }

static void begin_packet(phone_protocol *p, unsigned opcode, int connect) {
    p->packet[0] = (unsigned char)opcode; p->size = 3;
    if (connect) {
        p->packet[p->size++] = 0x10; p->packet[p->size++] = 0;
        p->packet[p->size++] = 0x10; p->packet[p->size++] = 0;
    } else if (p->has_connection) {
        p->packet[p->size++] = 0xCB;
        p->packet[p->size++] = (unsigned char)(p->connection >> 24);
        p->packet[p->size++] = (unsigned char)(p->connection >> 16);
        p->packet[p->size++] = (unsigned char)(p->connection >> 8);
        p->packet[p->size++] = (unsigned char)p->connection;
    }
}

static int header(phone_protocol *p, unsigned id, const void *data, size_t size) {
    if (size + 3u > PACKET_MAX - p->size) return fail(p, "output_limit");
    p->packet[p->size++] = (unsigned char)id;
    p->packet[p->size++] = (unsigned char)((size + 3u) >> 8);
    p->packet[p->size++] = (unsigned char)(size + 3u);
    memcpy(p->packet + p->size, data, size); p->size += size;
    return 1;
}

static int name_header(phone_protocol *p, const char *name) {
    unsigned char utf16[128];
    size_t i, n = strlen(name);
    if (n > 63) return fail(p, "output_limit");
    for (i = 0; i <= n; ++i) { utf16[i * 2u] = 0; utf16[i * 2u + 1u] = (unsigned char)name[i]; }
    return header(p, 0x01, utf16, (n + 1u) * 2u);
}

static int exchange(phone_protocol *p, int connection_response) {
    size_t at;
    unsigned code;
    if (p->size > p->peer_max) return fail(p, "output_limit");
    p->packet[1] = (unsigned char)(p->size >> 8); p->packet[2] = (unsigned char)p->size;
    if (!transfer(p, p->packet, p->size, 1) || !transfer(p, p->response, 3, 0)) return 0;
    p->received = be16(p->response + 1);
    if (p->received < 3 || p->received > PACKET_MAX) return fail(p, "protocol_error");
    if (!transfer(p, p->response + 3, p->received - 3u, 0)) return 0;
    code = p->response[0];
    if (code != 0xA0 && code != 0x90) return fail(p, code == 0xC1 || code == 0xC3 ? "permission_denied" : "rejected");
    at = connection_response ? 7u : 3u;
    if (p->received < at) return fail(p, "protocol_error");
    if (connection_response) {
        p->peer_max = be16(p->response + 5);
        if (p->peer_max < 255 || code != 0xA0) return fail(p, "protocol_error");
    }
    while (at < p->received) {
        unsigned id = p->response[at], kind = id >> 6;
        size_t length = kind < 2 ? 0 : kind == 2 ? 2u : 5u;
        if (kind < 2) {
            if (p->received - at < 3) return fail(p, "protocol_error");
            length = be16(p->response + at + 1);
            if (length < 3) return fail(p, "protocol_error");
        }
        if (length > p->received - at) return fail(p, "protocol_error");
        if (id == 0xCB) {
            p->connection = ((unsigned long)p->response[at + 1] << 24) |
                ((unsigned long)p->response[at + 2] << 16) | ((unsigned long)p->response[at + 3] << 8) | p->response[at + 4];
            p->has_connection = 1;
        }
        if (id == 0x48 || id == 0x49) {
            if (length - 3u > BODY_MAX - p->body_size) return fail(p, "output_limit");
            memcpy(p->body + p->body_size, p->response + at + 3u, length - 3u);
            p->body_size += length - 3u;
        }
        at += length;
    }
    p->body[p->body_size] = 0;
    return 1;
}

static int obex_connect(phone_protocol *p, int contacts) {
    static const unsigned char map_target[] = {0xBB,0x58,0x2B,0x40,0x42,0x0C,0x11,0xDB,0xB0,0xDE,0x08,0x00,0x20,0x0C,0x9A,0x66};
    static const unsigned char pbap_target[] = {0x79,0x61,0x35,0xF0,0xF0,0xC5,0x11,0xD8,0x09,0x66,0x08,0x00,0x20,0x0C,0x9A,0x66};
    begin_packet(p, 0x80, 1);
    return header(p, 0x46, contacts ? pbap_target : map_target, 16) && exchange(p, 1);
}

static int set_path(phone_protocol *p, const char *name) {
    begin_packet(p, 0x85, 0);
    /* SETPATH's flags/constants precede headers. Bit 1 forbids folder creation. */
    memmove(p->packet + 5, p->packet + 3, p->size - 3u);
    p->packet[3] = 2; p->packet[4] = 0; p->size += 2;
    return name_header(p, name) && exchange(p, 0) && p->response[0] == 0xA0;
}

static int get_body(phone_protocol *p, const char *type, const char *name,
                    const unsigned char *params, size_t count) {
    unsigned pages;
    p->body_size = 0;
    begin_packet(p, 0x83, 0);
    if (!name_header(p, name) || !header(p, 0x42, type, strlen(type) + 1u) || !header(p, 0x4C, params, count)) return 0;
    for (pages = 0; pages < 128; ++pages) {
        if (!exchange(p, 0)) return 0;
        if (p->response[0] == 0xA0) {
            if (memchr(p->body, 0, p->body_size) || !cdx_validate_utf8(p->body, p->body_size)) return fail(p, "encoding_error");
            return 1;
        }
        begin_packet(p, 0x83, 0);
    }
    return fail(p, "output_limit");
}

static int listing(phone_protocol *p, const cdx_phone_request *r, int contacts) {
    unsigned char params[] = {1,2,0,0,2,2,0,0,3,1,0x1C,0x10,4,0,0,0,0x4E};
    if (contacts) {
        unsigned char book[] = {4,2,0,0,5,2,0,0,7,1,1};
        book[2] = (unsigned char)(r->max_entries >> 8); book[3] = (unsigned char)r->max_entries;
        book[6] = (unsigned char)(r->offset >> 8); book[7] = (unsigned char)r->offset;
        return get_body(p, "x-bt/phonebook", r->action == CDX_PHONE_CALL_HISTORY ? "telecom/cch.vcf" : "telecom/pb.vcf", book, sizeof(book));
    }
    params[2] = (unsigned char)(r->max_entries >> 8); params[3] = (unsigned char)r->max_entries;
    params[6] = (unsigned char)(r->offset >> 8); params[7] = (unsigned char)r->offset;
    return get_body(p, "x-bt/MAP-msg-listing", "inbox", params, sizeof(params));
}

static void item_field(cdx_json_output *out, const char *key, const char *value) {
    cdx_json_raw(out, ","); cdx_json_quote(out, key, strlen(key)); cdx_json_raw(out, ":");
    cdx_json_quote(out, value, strlen(value));
}

/* Previews are deliberately bounded. Raw, hashed protocol data remains available
   when a phone returns a format that this display parser cannot represent. */
static int vcard_items(const char *data, cdx_json_output *items, unsigned *count) {
    const char *at = data;
    char line[4096], name[1024] = "", number[1024] = "", date[128] = "", direction[32] = "";
    unsigned lines, numbers = 0;
    int card = 0, version = 0;
    for (lines = 0; *at && lines < BODY_MAX; ++lines) {
        size_t size = 0, i, write;
        char *colon, *value, *parameter;
        do {
            const char *end = strchr(at, '\n');
            size_t length = end ? (size_t)(end - at) : strlen(at);
            if (length && at[length - 1] == '\r') --length;
            if (size + length >= sizeof(line)) return 0;
            memcpy(line + size, at, length); size += length;
            at = end ? end + 1 : at + strlen(at);
            if (*at != ' ' && *at != '\t') break;
            ++at;
        } while (*at);
        line[size] = 0;
        if (!strcmp(line, "BEGIN:VCARD")) {
            if (card || *count >= 50) return 0;
            card = 1; version = 0; numbers = 0;
            name[0] = number[0] = date[0] = direction[0] = 0; continue;
        }
        if (!strcmp(line, "END:VCARD")) {
            if (!card || !version) return 0;
            if ((*count)++) cdx_json_raw(items, ",");
            cdx_json_raw(items, "{\"name\":"); cdx_json_quote(items, name, strlen(name));
            item_field(items, "number", number); item_field(items, "datetime", date); item_field(items, "direction", direction);
            cdx_json_raw(items, ",\"number_count\":"); cdx_json_uint(items, numbers); cdx_json_raw(items, "}");
            card = 0; continue;
        }
        if (!size) continue;
        if (!card || !(colon = strchr(line, ':'))) return 0;
        *colon = 0; value = colon + 1;
        parameter = strchr(line, ';');
        if (parameter) *parameter++ = 0;
        /* Grouped vCard properties (item1.TEL) use the same field semantics. */
        { char *group = strchr(line, '.'); if (group) memmove(line, group + 1, strlen(group + 1) + 1); }
        for (i = 0, write = 0; value[i]; ++i) {
            char c = value[i];
            if (c == '\\') {
                c = value[++i];
                if (!c) return 0;
                if (c == 'n' || c == 'N') c = '\n';
                else if (c != '\\' && c != ',' && c != ';') return 0;
            }
            value[write++] = c;
        }
        value[write] = 0;
        if (!strcmp(line, "VERSION")) { if (strcmp(value, "3.0")) return 0; version = 1; }
        else if (!strcmp(line, "FN")) {
            if (write >= sizeof(name)) return 0;
            memcpy(name, value, write + 1);
        } else if (!strcmp(line, "TEL")) {
            if (++numbers == 1) {
                if (write >= sizeof(number)) return 0;
                memcpy(number, value, write + 1);
            }
        } else if (!strcmp(line, "X-IRMC-CALL-DATETIME")) {
            if (write >= sizeof(date)) return 0;
            memcpy(date, value, write + 1);
            if (parameter) {
                const char *kind = strstr(parameter, "MISSED") ? "missed" : strstr(parameter, "RECEIVED") ? "incoming" :
                    strstr(parameter, "DIALED") ? "outgoing" : "";
                memcpy(direction, kind, strlen(kind) + 1);
            }
        }
    }
    return !*at && !card;
}

static int xml_value(const char *at, size_t size, char value[1024]) {
    size_t i, write = 0;
    for (i = 0; i < size; ++i) {
        unsigned code = (unsigned char)at[i];
        if (code == '&') {
            size_t begin = ++i, end = begin;
            while (end < size && end - begin < 12 && at[end] != ';') ++end;
            if (end == size || at[end] != ';') return 0;
            if (end - begin == 3 && !memcmp(at + begin, "amp", 3)) code = '&';
            else if (end - begin == 2 && !memcmp(at + begin, "lt", 2)) code = '<';
            else if (end - begin == 2 && !memcmp(at + begin, "gt", 2)) code = '>';
            else if (end - begin == 4 && !memcmp(at + begin, "quot", 4)) code = '"';
            else if (end - begin == 4 && !memcmp(at + begin, "apos", 4)) code = '\'';
            else if (at[begin] == '#') {
                unsigned base = 10;
                size_t digit = begin + 1;
                if (digit < end && at[digit] == 'x') { base = 16; ++digit; }
                if (digit == end) return 0;
                code = 0;
                for (; digit < end; ++digit) {
                    unsigned v = at[digit] >= '0' && at[digit] <= '9' ? (unsigned)(at[digit] - '0') :
                        at[digit] >= 'a' && at[digit] <= 'f' ? (unsigned)(at[digit] - 'a' + 10) :
                        at[digit] >= 'A' && at[digit] <= 'F' ? (unsigned)(at[digit] - 'A' + 10) : 16;
                    if (v >= base || code > 0x10ffffu / base) return 0;
                    code = code * base + v;
                }
                if (!code || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
            } else return 0;
            i = end;
            if (write + 4 >= 1024) return 0;
            if (code >= 0x10000) { value[write++] = (char)(0xf0 | (code >> 18)); value[write++] = (char)(0x80 | ((code >> 12) & 63)); }
            else if (code >= 0x800) value[write++] = (char)(0xe0 | (code >> 12));
            if (code >= 0x80) {
                if (code < 0x800) value[write++] = (char)(0xc0 | (code >> 6));
                else value[write++] = (char)(0x80 | ((code >> 6) & 63));
                value[write++] = (char)(0x80 | (code & 63)); continue;
            }
        }
        if (write + 1 >= 1024) return 0;
        value[write++] = (char)code;
    }
    value[write] = 0; return 1;
}

static int map_items(const char *data, cdx_json_output *items, unsigned *count) {
    const char *at = data;
    unsigned tags;
    int root = 0, closed = 0;
    for (tags = 0; *at && tags < 128; ++tags) {
        const char *tag, *name;
        size_t length;
        while (*at == ' ' || *at == '\r' || *at == '\n' || *at == '\t') ++at;
        if (!*at) break;
        if (!strncmp(at, "<?xml ", 6) && !root) {
            at = strstr(at + 6, "?>"); if (!at) return 0; at += 2; continue;
        }
        if (!strncmp(at, "</MAP-msg-listing>", 18)) {
            if (!root || closed) return 0;
            closed = 1; at += 18; continue;
        }
        if (*at++ != '<' || closed) return 0;
        tag = at;
        while (*at && *at != ' ' && *at != '\t' && *at != '\r' && *at != '\n' && *at != '/' && *at != '>') ++at;
        length = (size_t)(at - tag);
        if (!root) { if (length != 15 || memcmp(tag, "MAP-msg-listing", 15)) return 0; root = 1; }
        else {
            if (length != 3 || memcmp(tag, "msg", 3) || *count >= 50) return 0;
            if ((*count)++) cdx_json_raw(items, ",");
            cdx_json_raw(items, "{\"entry\":true");
        }
        for (;;) {
            char value[1024], quote;
            const char *start;
            while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n') ++at;
            if (*at == '/' || *at == '>') break;
            name = at;
            while (*at && *at != '=' && *at != ' ' && *at != '\t') ++at;
            length = (size_t)(at - name);
            while (*at == ' ' || *at == '\t') ++at;
            if (*at++ != '=') return 0;
            while (*at == ' ' || *at == '\t') ++at;
            quote = *at++; if (quote != '\'' && quote != '"') return 0;
            start = at;
            while (*at && *at != quote) ++at;
            if (!*at || !xml_value(start, (size_t)(at - start), value)) return 0;
            ++at;
            if (tag[0] == 'm') {
                static const char *const keys[] = {"handle", "subject", "datetime", "sender_name", "sender_addressing", "type"};
                size_t key;
                for (key = 0; key < sizeof(keys) / sizeof(keys[0]); ++key)
                    if (length == strlen(keys[key]) && !memcmp(name, keys[key], length)) { item_field(items, keys[key], value); break; }
            }
        }
        if (tag[0] == 'm') {
            if (*at++ != '/' || *at++ != '>') return 0;
            cdx_json_raw(items, "}");
        } else {
            if (*at == '/') { closed = 1; ++at; }
            if (*at++ != '>') return 0;
        }
    }
    return !*at && root && closed;
}

static void payload_result(phone_protocol *p, const cdx_phone_request *r, cdx_json_output *out, const char *format, const char *hash) {
    cdx_json_output items;
    unsigned count = 0;
    int parsed;
    cdx_json_output_init(&items);
    parsed = r->action == CDX_PHONE_CONTACTS || r->action == CDX_PHONE_CALL_HISTORY
        ? vcard_items(p->body, &items, &count) : map_items(p->body, &items, &count);
    cdx_json_raw(out, "{\"status\":\"ok\",\"format\":"); cdx_json_quote(out, format, strlen(format));
    cdx_json_raw(out, ",\"data\":"); cdx_json_quote(out, p->body, p->body_size);
    cdx_json_raw(out, ",\"sha256\":"); cdx_json_quote(out, hash, 64);
    cdx_json_raw(out, ",\"offset\":"); cdx_json_uint(out, r->offset);
    cdx_json_raw(out, ",\"max_entries\":"); cdx_json_uint(out, r->max_entries);
    cdx_json_raw(out, ",\"complete_collection\":false,\"read_state_modified\":false");
    cdx_json_raw(out, parsed && !items.failed ? ",\"preview_status\":\"ok\",\"items\":[" : ",\"preview_status\":\"unsupported_format\",\"items\":[");
    if (parsed && !items.failed && items.size) cdx_json_rawn(out, items.data, items.size);
    cdx_json_raw(out, "]");
    if (r->action == CDX_PHONE_WAIT_SMS) cdx_json_raw(out, ",\"event\":\"inbox_changed\"");
    cdx_json_raw(out, "}");
    cdx_json_output_free(&items);
}

static int handle_present(phone_protocol *p, const char *handle) {
    const char *cursor = p->body;
    unsigned count = 0, matches = 0;
    while (count++ < 50 && (cursor = strstr(cursor, "<msg ")) != NULL) {
        const char *end = strchr(cursor, '>'), *h = strstr(cursor, "handle=");
        if (!end) return 0;
        if (h && h < end && (h[7] == '"' || h[7] == '\'') && end - h >= 25 && h[24] == h[7]) {
            size_t i;
            for (i = 0; i < 16; ++i) {
                char a = h[8 + i], b = handle[i];
                if (a >= 'a' && a <= 'f') a -= 'a' - 'A';
                if (b >= 'a' && b <= 'f') b -= 'a' - 'A';
                if (a != b) break;
            }
            if (i == 16) ++matches;
        }
        cursor = end + 1;
    }
    return matches == 1;
}

static char *message_line(char **cursor) {
    char *start = *cursor, *end = strstr(start, "\r\n");
    if (!end || end - start > 4096) return NULL;
    *end = 0; *cursor = end + 2; return start;
}

static int message_expect(char **cursor, const char *expected) {
    char *line = message_line(cursor);
    return line && !strcmp(line, expected);
}

static int skip_cards(char **cursor) {
    unsigned cards, lines;
    for (cards = 0; cards < 16 && !strncmp(*cursor, "BEGIN:VCARD\r\n", 13); ++cards) {
        char *line;
        *cursor += 13;
        line = message_line(cursor);
        if (!line || (strcmp(line, "VERSION:2.1") && strcmp(line, "VERSION:3.0"))) return 0;
        for (lines = 0; lines < 64; ++lines) {
            line = message_line(cursor);
            if (!line) return 0;
            if (!strcmp(line, "END:VCARD")) break;
            if (!strchr(line, ':') || !strncmp(line, "BEGIN:", 6) || !strncmp(line, "END:", 4)) return 0;
        }
        if (lines == 64) return 0;
    }
    return strncmp(*cursor, "BEGIN:VCARD\r\n", 13) != 0;
}

static int sms_text(phone_protocol *p) {
    char *cursor = p->body, *line, *start, *end;
    unsigned long bytes;
    size_t size, i, write = 0;
    if (!message_expect(&cursor, "BEGIN:BMSG") || !message_expect(&cursor, "VERSION:1.0")) return fail(p, "protocol_error");
    line = message_line(&cursor);
    if (!line || (strcmp(line, "STATUS:READ") && strcmp(line, "STATUS:UNREAD"))) return fail(p, "protocol_error");
    line = message_line(&cursor);
    if (!line || (strcmp(line, "TYPE:SMS_GSM") && strcmp(line, "TYPE:SMS_CDMA"))) return fail(p, "unsupported_message");
    if (!message_expect(&cursor, "FOLDER:telecom/msg/inbox") || !skip_cards(&cursor) ||
        !message_expect(&cursor, "BEGIN:BENV") || !skip_cards(&cursor) ||
        !message_expect(&cursor, "BEGIN:BBODY") || !message_expect(&cursor, "CHARSET:UTF-8")) return fail(p, "unsupported_message");
    line = message_line(&cursor);
    if (line && !strncmp(line, "LANGUAGE:", 9)) line = message_line(&cursor);
    if (!line || strncmp(line, "LENGTH:", 7) || line[7] < '0' || line[7] > '9') return fail(p, "protocol_error");
    bytes = strtoul(line + 7, &end, 10);
    if (*end || bytes < 22 || bytes > BODY_MAX || (size_t)(p->body + p->body_size - cursor) < bytes) return fail(p, "protocol_error");
    start = cursor;
    if (memcmp(start, "BEGIN:MSG\r\n", 11) || memcmp(start + bytes - 11u, "\r\nEND:MSG\r\n", 11)) return fail(p, "protocol_error");
    cursor += bytes;
    if (!strncmp(cursor, "\r\n", 2)) cursor += 2;
    if (!message_expect(&cursor, "END:BBODY") || !message_expect(&cursor, "END:BENV") ||
        !message_expect(&cursor, "END:BMSG") || cursor != p->body + p->body_size) return fail(p, "protocol_error");
    start += 11; size = bytes - 22u;
    for (i = 0; i < size; ++i) {
        if (i == 0 || start[i - 1] == '\n') {
            size_t j = i;
            if (size - i >= 7 && !memcmp(start + i, "END:MSG", 7)) return fail(p, "protocol_error");
            while (j < size && start[j] == '/') ++j;
            if (j > i && size - j >= 7 && !memcmp(start + j, "END:MSG", 7)) ++i;
        }
        p->body[write++] = start[i];
    }
    p->body[write] = 0; p->body_size = write;
    return 1;
}

static void code_result(phone_protocol *p, cdx_json_output *out) {
    static const char *const labels[] = {"验证码", "校验码", "驗證碼", "verification code", "Verification code", "OTP", "one-time password"};
    char code[9] = {0};
    size_t label, i;
    int ambiguous = 0;
    for (label = 0; label < sizeof(labels) / sizeof(labels[0]); ++label) {
        const char *at = p->body;
        unsigned occurrences;
        for (occurrences = 0; occurrences < 128 && (at = strstr(at, labels[label])) != NULL; ++occurrences) {
            size_t begin = (size_t)(at - p->body) + strlen(labels[label]);
            size_t limit = p->body_size - begin > 64 ? begin + 64 : p->body_size;
            for (i = begin; i < limit; ++i) {
                size_t end, digits;
                if (p->body[i] == '\r' || p->body[i] == '\n') break;
                if (p->body[i] < '0' || p->body[i] > '9') continue;
                end = i;
                while (end < p->body_size && p->body[end] >= '0' && p->body[end] <= '9') ++end;
                digits = end - i;
                if (digits >= 4 && digits <= 8) {
                    if (*code && (strlen(code) != digits || memcmp(code, p->body + i, digits))) ambiguous = 1;
                    memcpy(code, p->body + i, digits); code[digits] = 0;
                }
                i = end - 1u;
            }
            at += strlen(labels[label]);
        }
        if (occurrences == 128 && strstr(at, labels[label])) { cdx_phone_error(out, "output_limit", 0); return; }
    }
    if (ambiguous || !*code) { cdx_phone_error(out, ambiguous ? "ambiguous" : "no_match", 0); return; }
    cdx_json_raw(out, "{\"status\":\"ok\",\"code\":"); cdx_json_quote(out, code, strlen(code));
    cdx_json_raw(out, ",\"read_state_modified\":false,\"auto_forward\":false}");
}

static int send_sms(phone_protocol *p, const cdx_phone_request *r) {
    char text[PACKET_MAX], message[PACKET_MAX];
    size_t i, n = 0;
    int length;
    static const unsigned char params[] = {0x0B,1,0,0x0C,1,0,0x14,1,1};
    for (i = 0; r->body[i]; ++i) {
        if (i == 0 || r->body[i - 1] == '\r' || r->body[i - 1] == '\n') {
            size_t j = i;
            while (r->body[j] == '/') ++j;
            if (!strncmp(r->body + j, "END:MSG", 7)) text[n++] = '/';
        }
        if (r->body[i] == '\r' || r->body[i] == '\n') {
            if (r->body[i] == '\r' && r->body[i + 1] == '\n') ++i;
            text[n++] = '\r'; text[n++] = '\n';
        } else text[n++] = r->body[i];
    }
    text[n] = 0;
    length = snprintf(message, sizeof(message),
        "BEGIN:BMSG\r\nVERSION:1.0\r\nSTATUS:UNREAD\r\nTYPE:SMS_GSM\r\nFOLDER:\r\n"
        "BEGIN:BENV\r\nBEGIN:VCARD\r\nVERSION:2.1\r\nN:\r\nTEL:%s\r\nEND:VCARD\r\n"
        "BEGIN:BBODY\r\nCHARSET:UTF-8\r\nLENGTH:%u\r\nBEGIN:MSG\r\n%s\r\nEND:MSG\r\n"
        "END:BBODY\r\nEND:BENV\r\nEND:BMSG\r\n", r->recipient, (unsigned)n + 22u, text);
    if (length < 0 || (size_t)length >= sizeof(message)) return fail(p, "output_limit");
    begin_packet(p, 0x82, 0);
    if (!name_header(p, "outbox") || !header(p, 0x42, "x-bt/message", 13) ||
        !header(p, 0x4C, params, sizeof(params)) || !header(p, 0x49, message, (size_t)length)) return 0;
    if (p->size > p->peer_max) return fail(p, "output_limit");
    p->submitted = 1;
    if (!exchange(p, 0)) return 0;
    if (p->response[0] != 0xA0) return fail(p, "unexpected_continue");
    return 1;
}

static int obex_operation(phone_protocol *p, const cdx_phone_request *r, cdx_json_output *out) {
    char hash[65];
    char expected[65];
    unsigned attempt;
    size_t i;
    int contacts = r->action == CDX_PHONE_CONTACTS || r->action == CDX_PHONE_CALL_HISTORY;
    for (i = 0; i < 64 && r->expected_sha256[i]; ++i) {
        char c = r->expected_sha256[i]; expected[i] = c >= 'A' && c <= 'F' ? (char)(c + ('a' - 'A')) : c;
    }
    expected[i] = 0;
    if (!obex_connect(p, contacts)) return 0;
    if (!contacts && (!set_path(p, "telecom") || !set_path(p, "msg"))) return 0;
    if (r->action == CDX_PHONE_SMS_SEND) {
        if (!send_sms(p, r)) return 0;
        cdx_json_raw(out, "{\"status\":\"accepted\",\"submitted\":true,\"delivery_confirmed\":false,\"auto_retry\":false}");
        return 1;
    }
    for (attempt = 0; attempt < 61; ++attempt) {
        if (!listing(p, r, contacts) || !cdx_sha256((const unsigned char *)p->body, p->body_size, hash)) return 0;
        if (r->action != CDX_PHONE_WAIT_SMS || strcmp(hash, expected)) break;
        if (!p->io->pause(p->io->context, 1000)) return 0;
    }
    if (attempt == 61) return fail(p, "timed_out");
    if (r->action == CDX_PHONE_SMS_READ || r->action == CDX_PHONE_READ_CODE) {
        static const unsigned char params[] = {0x0A,1,0,0x14,1,1};
        if (strcmp(hash, expected)) return fail(p, "snapshot_changed");
        if (!handle_present(p, r->handle)) return fail(p, "no_match");
        if (!get_body(p, "x-bt/message", r->handle, params, sizeof(params)) || !sms_text(p)) return 0;
        if (r->action == CDX_PHONE_READ_CODE) code_result(p, out);
        else {
            cdx_json_raw(out, "{\"status\":\"ok\",\"body\":"); cdx_json_quote(out, p->body, p->body_size);
            cdx_json_raw(out, ",\"read_state_modified\":false}");
        }
    } else payload_result(p, r, out, contacts ? "vcard-3.0" : "map-message-listing-xml", hash);
    return 1;
}

typedef cdx_phone_hfp hfp_state;

static int hfp_line(phone_protocol *p, char *line, size_t capacity) {
    size_t n = 0, count;
    for (count = 0; count < 2048; ++count) {
        unsigned char byte;
        if (!transfer(p, &byte, 1, 0)) return 0;
        if (byte == '\r' || byte == '\n') {
            if (n) { line[n] = 0; return 1; }
        } else {
            if (byte < 32 || byte > 126 || n + 1 >= capacity) return fail(p, "protocol_error");
            line[n++] = (char)byte;
        }
    }
    return fail(p, "output_limit");
}

static int hfp_command(phone_protocol *p, const char *command, char *reply, size_t capacity) {
    char line[1024];
    unsigned count;
    size_t used = 0;
    reply[0] = 0;
    if (!transfer(p, (void *)command, strlen(command), 1)) return 0;
    for (count = 0; count < 256; ++count) {
        if (!hfp_line(p, line, sizeof(line))) return 0;
        if (!strcmp(line, "OK")) return 1;
        if (!strcmp(line, "ERROR") || !strncmp(line, "+CME ERROR", 10)) return fail(p, "rejected");
        if (used + strlen(line) + 2u > capacity) return fail(p, "output_limit");
        memcpy(reply + used, line, strlen(line)); used += strlen(line); reply[used++] = '\n'; reply[used] = 0;
    }
    return fail(p, "output_limit");
}

static int hfp_definitions(phone_protocol *p, hfp_state *state, const char *reply) {
    const char *at = strstr(reply, "+CIND:");
    unsigned i;
    if (!at) return fail(p, "protocol_error");
    state->call_index = state->setup_index = state->battery_index = 0;
    for (i = 1; i <= 32; ++i) {
        const char *end;
        at = strchr(at, '"'); if (!at) break;
        ++at; end = strchr(at, '"'); if (!end) return fail(p, "protocol_error");
        if (end - at == 4 && !memcmp(at, "call", 4)) state->call_index = (int)i;
        if (end - at == 9 && !memcmp(at, "callsetup", 9)) state->setup_index = (int)i;
        if (end - at == 7 && !memcmp(at, "battchg", 7)) state->battery_index = (int)i;
        at = end + 1;
    }
    return state->call_index && state->setup_index ? 1 : fail(p, "protocol_error");
}

static int hfp_values(phone_protocol *p, hfp_state *state, const char *reply) {
    const char *at = strstr(reply, "+CIND:");
    int i;
    state->call = state->setup = state->battery_level = -1;
    if (!at) return fail(p, "protocol_error");
    at += 6;
    for (i = 1; i <= 32; ++i) {
        char *end;
        long v;
        while (*at == ' ') ++at;
        if (*at < '0' || *at > '9') return fail(p, "protocol_error");
        v = strtol(at, &end, 10);
        if (v < 0 || v > 9) return fail(p, "protocol_error");
        if (i == state->call_index) state->call = (int)v;
        if (i == state->setup_index) state->setup = (int)v;
        if (i == state->battery_index) state->battery_level = (int)v;
        if (*end != ',') break;
        at = end + 1;
    }
    return state->call >= 0 && state->call <= 1 && state->setup >= 0 && state->setup <= 3 ? 1 : fail(p, "protocol_error");
}

static const char *call_state(const hfp_state *s) {
    return s->setup == 1 ? "incoming" : s->setup == 2 ? "dialing" : s->setup == 3 ? "alerting" : s->call ? "active" : "idle";
}

static void hfp_battery_json(const hfp_state *state, cdx_json_output *out) {
    if (state->battery_index > 0 && state->battery_level >= 0) {
        unsigned percent = state->battery_level <= 5 ? (unsigned)state->battery_level * 20u : (unsigned)state->battery_level;
        cdx_json_raw(out, ",\"battery_supported\":true,\"battery_level\":");
        cdx_json_uint(out, (unsigned)state->battery_level);
        cdx_json_raw(out, ",\"battery_scale\":5,\"battery_percent\":");
        cdx_json_uint(out, percent > 100u ? 100u : percent);
    } else {
        cdx_json_raw(out, ",\"battery_supported\":false,\"battery_level\":null,\"battery_scale\":null,\"battery_percent\":null");
    }
}

static int hfp_operation(phone_protocol *p, const cdx_phone_request *r, hfp_state *saved, cdx_json_output *out) {
    hfp_state state = {0};
    char reply[4096], command[64];
    unsigned polls;
    if (saved) state = *saved;
    if (!state.initialized) {
        if (!hfp_command(p, "AT+BRSF=0\r", reply, sizeof(reply)) || !strstr(reply, "+BRSF:")) return 0;
        if (!hfp_command(p, "AT+CIND=?\r", reply, sizeof(reply)) || !hfp_definitions(p, &state, reply)) return 0;
    }
    if (!hfp_command(p, "AT+CIND?\r", reply, sizeof(reply)) || !hfp_values(p, &state, reply)) return 0;
    if (!state.initialized && !hfp_command(p, "AT+CMER=3,0,0,1\r", reply, sizeof(reply))) return 0;
    state.initialized = 1;
    if (r->action == CDX_PHONE_WAIT_CALL) {
        for (polls = 0; polls < 60 && state.setup != 1; ++polls) {
            if (!p->io->pause(p->io->context, 1000) || !hfp_command(p, "AT+CIND?\r", reply, sizeof(reply)) || !hfp_values(p, &state, reply)) return 0;
        }
        if (state.setup != 1) return fail(p, "timed_out");
    }
    if (r->action == CDX_PHONE_DIAL || r->action == CDX_PHONE_ANSWER || r->action == CDX_PHONE_HANGUP) {
        if ((r->action == CDX_PHONE_DIAL && (state.call || state.setup)) ||
            (r->action == CDX_PHONE_ANSWER && state.setup != 1) ||
            (r->action == CDX_PHONE_HANGUP && !state.call && !state.setup)) return fail(p, "call_state_changed");
        if (r->action == CDX_PHONE_DIAL) (void)snprintf(command, sizeof(command), "ATD%s;\r", r->recipient);
        else (void)snprintf(command, sizeof(command), "%s", r->action == CDX_PHONE_ANSWER ? "ATA\r" : "AT+CHUP\r");
        p->submitted = 1;
        if (!hfp_command(p, command, reply, sizeof(reply))) return 0;
        cdx_json_raw(out, "{\"status\":\"accepted\",\"submitted\":true,\"state_verified\":false,\"audio_available\":false,\"auto_retry\":false}");
    } else {
        cdx_json_raw(out, "{\"status\":\"ok\",\"call_state\":");
        cdx_json_quote(out, call_state(&state), strlen(call_state(&state)));
        cdx_json_raw(out, p->io->retain_connection ? ",\"audio_available\":false,\"connection_retained\":true" :
            ",\"audio_available\":false,\"connection_retained\":false");
        hfp_battery_json(&state, out);
        if (r->action == CDX_PHONE_WAIT_CALL) cdx_json_raw(out, ",\"event\":\"incoming_call\"");
        cdx_json_raw(out, "}");
    }
    if (saved) *saved = state;
    return 1;
}

void cdx_phone_protocol_connected(const cdx_phone_request *r, cdx_phone_stream *io, cdx_phone_hfp *state, cdx_json_output *out) {
    phone_protocol *p = (phone_protocol *)calloc(1, sizeof(*p));
    int success;
    if (!p) { cdx_phone_error(out, "out_of_memory", 0); return; }
    p->io = io; p->peer_max = PACKET_MAX;
    success = r->action >= CDX_PHONE_CALL_STATUS && r->action <= CDX_PHONE_WAIT_CALL
        ? hfp_operation(p, r, state, out) : obex_operation(p, r, out);
    if (!success) cdx_phone_error(out, io->error ? io->error : "protocol_error", p->submitted);
    free(p);
}

void cdx_phone_protocol(const cdx_phone_request *r, cdx_phone_stream *io, cdx_json_output *out) {
    cdx_phone_protocol_connected(r, io, NULL, out);
}
