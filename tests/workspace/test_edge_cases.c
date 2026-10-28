#include "cdx/document.h"
#include "cdx/operations.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;
static ULONGLONG deadline;
static volatile LONG cancelled;

static BOOL WINAPI on_interrupt(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
        InterlockedExchange(&cancelled, 1);
        return TRUE;
    }
    return FALSE;
}

static void expect(int passed, const char *name) {
    ++checks;
    if (!passed) ++failures;
    (void)printf("%s %s\n", passed ? "PASS" : "FAIL", name);
}

static int available(void) {
    return checks < 25 && !cancelled && GetTickCount64() < deadline;
}

static int put_file(const wchar_t *path, const unsigned char *bytes, size_t size) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    int ok;
    if (file == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(file, bytes, (DWORD)size, &written, NULL) && written == size;
    CloseHandle(file);
    return ok;
}

static int disk_equals(const wchar_t *path, const unsigned char *expected, size_t size) {
    unsigned char bytes[256];
    DWORD count = 0;
    int ok;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    ok = ReadFile(file, bytes, sizeof(bytes), &count, NULL) && count == size && !memcmp(bytes, expected, size);
    CloseHandle(file);
    return ok;
}

static int call(const char *operation, const char *arguments, cdx_json *response) {
    cdx_json request;
    cdx_json_output output;
    int parsed;
    if (!available() || !cdx_json_parse(&request, arguments, strlen(arguments))) return 0;
    cdx_json_output_init(&output);
    cdx_call(operation, &request, 0, NULL, &output);
    parsed = !output.failed && cdx_json_parse(response, output.data, output.size);
    cdx_json_output_free(&output);
    cdx_json_free(&request);
    return parsed;
}

static int status_is(const cdx_json *json, const char *expected) {
    int node = cdx_json_get(json, 0, "status");
    return node >= 0 && json->nodes[node].type == CDX_JSON_STRING && !strcmp(json->nodes[node].string, expected);
}

static unsigned long long number(const cdx_json *json, const char *name) {
    int node = cdx_json_get(json, 0, name);
    return node >= 0 && json->nodes[node].type == CDX_JSON_NUMBER ? (unsigned long long)json->nodes[node].number : 0;
}

static unsigned long long open_protocol(const wchar_t *path, char hash[65]) {
    char utf8_path[1024];
    cdx_json response;
    cdx_json_output arguments;
    unsigned long long id = 0;
    int node;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, utf8_path, sizeof(utf8_path), NULL, NULL)) return 0;
    cdx_json_output_init(&arguments);
    cdx_json_raw(&arguments, "{\"path\":");
    cdx_json_quote(&arguments, utf8_path, strlen(utf8_path));
    cdx_json_raw(&arguments, ",\"encoding\":\"utf-8\"}");
    if (call("workspace_open", arguments.data, &response)) {
        id = number(&response, "document_id");
        node = cdx_json_get(&response, 0, "memory_sha256");
        if (node >= 0 && response.nodes[node].type == CDX_JSON_STRING) {
            (void)strcpy_s(hash, 65, response.nodes[node].string);
        }
        cdx_json_free(&response);
    }
    cdx_json_output_free(&arguments);
    return id;
}

static void test_protocol_memory(const wchar_t *path) {
    cdx_json response;
    cdx_json info;
    HANDLE exclusive;
    unsigned long long id, reopened, before = 0;
    char hash[65] = {0}, again_hash[65] = {0}, arguments[1024];
    const unsigned char content[] = "old\n";
    int ok;
    cdx_workspace_clear();
    if (!put_file(path, content, sizeof(content) - 1u)) { expect(0, "protocol fixture creation"); return; }
    id = open_protocol(path, hash);
    expect(id != 0, "workspace initial open");
    if (!id) return;
    exclusive = CreateFileW(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    (void)_snprintf_s(arguments, sizeof(arguments), _TRUNCATE, "{\"document_id\":%llu}", id);
    ok = exclusive != INVALID_HANDLE_VALUE && call("workspace_read", arguments, &response);
    expect(ok && status_is(&response, "ok"), "read cached document while disk is exclusively locked");
    if (ok) cdx_json_free(&response);
    reopened = open_protocol(path, again_hash);
    expect(exclusive != INVALID_HANDLE_VALUE && reopened == id, "reopen same path reuses cached id without disk IO");
    if (exclusive != INVALID_HANDLE_VALUE) CloseHandle(exclusive);
    (void)_snprintf_s(arguments, sizeof(arguments), _TRUNCATE,
        "{\"document_id\":%llu,\"expected_sha256\":\"%s\",\"old\":\"old\",\"new\":\"new\",\"expected_count\":1,\"dry_run\":true}", id, hash);
    ok = call("workspace_edit", arguments, &response);
    expect(ok && status_is(&response, "preview") && disk_equals(path, content, sizeof(content) - 1u), "protocol dry_run returns preview and preserves disk");
    if (ok) cdx_json_free(&response);
    if (call("tool_info", "{}", &info)) { before = number(&info, "document_loads"); cdx_json_free(&info); }
    (void)_snprintf_s(arguments, sizeof(arguments), _TRUNCATE, "{\"document_id\":%llu}", id);
    if (call("workspace_commit", arguments, &response)) cdx_json_free(&response);
    ok = call("tool_info", "{}", &info);
    expect(ok && number(&info, "document_loads") == before, "clean commit does not report a disk load");
    if (ok) cdx_json_free(&info);
    cdx_workspace_clear();
}

static void test_protocol_utf8_page(const wchar_t *path) {
    const unsigned char content[] = {0xf0, 0x9f, 0x99, 0x82, '\n'};
    cdx_json response;
    char hash[65], arguments[256];
    unsigned long long id;
    int ok;
    if (!put_file(path, content, sizeof(content))) { expect(0, "UTF8 paging fixture creation"); return; }
    id = open_protocol(path, hash);
    (void)_snprintf_s(arguments, sizeof(arguments), _TRUNCATE, "{\"document_id\":%llu,\"offset\":0,\"limit\":1}", id);
    ok = id != 0 && call("workspace_read", arguments, &response);
    expect(ok && (status_is(&response, "validation_error") || number(&response, "next_offset") > 0), "UTF8 tiny page either advances or rejects instead of looping");
    if (ok) cdx_json_free(&response);
    cdx_workspace_clear();
}

static void test_strict_encodings(const wchar_t *path) {
    const unsigned char orphan_high[] = {0xff, 0xfe, 0x3d, 0xd8};
    const unsigned char orphan_low[] = {0xfe, 0xff, 0xde, 0x42};
    const unsigned char pair[] = {0xff, 0xfe, 0x3d, 0xd8, 0x42, 0xde, '\r', 0, '\n', 0};
    const unsigned char bad_cp936[] = {0x81};
    const unsigned char chinese[] = {0xd6, 0xd0, 0xce, 0xc4, '\r', '\n'};
    cdx_document document;
    cdx_edit_result edit;
    cdx_document_status status;
    (void)put_file(path, orphan_high, sizeof(orphan_high));
    status = cdx_document_open(&document, path, CDX_ENCODING_AUTO);
    expect(status == CDX_STATUS_ENCODING_ERROR && disk_equals(path, orphan_high, sizeof(orphan_high)), "UTF16 orphan high surrogate rejected without write");
    if (status == CDX_STATUS_OK) cdx_document_free(&document);
    (void)put_file(path, orphan_low, sizeof(orphan_low));
    status = cdx_document_open(&document, path, CDX_ENCODING_AUTO);
    expect(status == CDX_STATUS_ENCODING_ERROR, "UTF16 orphan low surrogate rejected");
    if (status == CDX_STATUS_OK) cdx_document_free(&document);
    (void)put_file(path, pair, sizeof(pair));
    status = cdx_document_open(&document, path, CDX_ENCODING_AUTO);
    expect(status == CDX_STATUS_OK && !strcmp(document.text_utf8, "\xf0\x9f\x99\x82\r\n") && document.encoding == CDX_ENCODING_UTF16LE && document.eol == CDX_EOL_CRLF, "UTF16 surrogate pair decodes with CRLF metadata");
    if (status == CDX_STATUS_OK) cdx_document_free(&document);
    (void)put_file(path, bad_cp936, sizeof(bad_cp936));
    status = cdx_document_open(&document, path, CDX_ENCODING_CP936);
    expect(status == CDX_STATUS_ENCODING_ERROR, "CP936 dangling lead byte rejected");
    if (status == CDX_STATUS_OK) cdx_document_free(&document);
    (void)put_file(path, chinese, sizeof(chinese));
    status = cdx_document_open(&document, path, CDX_ENCODING_CP936);
    if (status != CDX_STATUS_OK) { expect(0, "CP936 best-fit setup"); return; }
    status = cdx_document_edit_utf8(&document, document.current_sha256, "\r\n", "\xc2\xa5\r\n", 1, 0, &edit);
    expect(status == CDX_STATUS_ENCODING_ERROR && !document.dirty_in_memory && disk_equals(path, chinese, sizeof(chinese)), "CP936 yen best-fit substitution is rejected");
    cdx_document_free(&document);
}

static void test_commit_and_dirty(const wchar_t *path) {
    const unsigned char original[] = "old\n";
    const unsigned char external[] = "zzz\n";
    cdx_document document;
    cdx_edit_result edit;
    cdx_commit_result commit;
    cdx_document_status status;
    HANDLE file;
    FILETIME original_write;
    int ok;
    (void)put_file(path, original, sizeof(original) - 1u);
    status = cdx_document_open(&document, path, CDX_ENCODING_UTF8);
    if (status != CDX_STATUS_OK) { expect(0, "commit fixture setup"); return; }
    original_write = document.original_version.last_write_time;
    status = cdx_document_edit_utf8(&document, document.current_sha256, "old", "new", 1, 0, &edit);
    ok = status == CDX_STATUS_OK && document.dirty_in_memory;
    status = cdx_document_edit_utf8(&document, document.current_sha256, "new", "next", 1, 1, &edit);
    expect(ok && status == CDX_STATUS_OK && document.dirty_in_memory && edit.dirty_in_memory, "preview on already-dirty document retains dirty result");
    (void)put_file(path, external, sizeof(external) - 1u);
    file = CreateFileW(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (file != INVALID_HANDLE_VALUE) { (void)SetFileTime(file, NULL, NULL, &original_write); CloseHandle(file); }
    status = cdx_document_commit(&document, &commit);
    expect(status == CDX_STATUS_FILE_CHANGED && !commit.written_to_disk && document.dirty_in_memory && disk_equals(path, external, sizeof(external) - 1u), "same-size same-mtime external change still conflicts by hash");
    cdx_document_free(&document);
    (void)put_file(path, original, sizeof(original) - 1u);
    status = cdx_document_open(&document, path, CDX_ENCODING_UTF8);
    if (status != CDX_STATUS_OK) { expect(0, "revert fixture setup"); return; }
    (void)cdx_document_edit_utf8(&document, document.current_sha256, "old", "new", 1, 0, &edit);
    status = cdx_document_edit_utf8(&document, document.current_sha256, "new", "old", 1, 0, &edit);
    expect(status == CDX_STATUS_OK && !document.dirty_in_memory && !strcmp(document.current_sha256, document.original_sha256), "reverting all memory changes restores clean state");
    (void)cdx_document_edit_utf8(&document, document.current_sha256, "old", "longer", 1, 0, &edit);
    status = cdx_document_commit(&document, &commit);
    expect(status == CDX_STATUS_OK && commit.written_to_disk && !document.dirty_in_memory && !strcmp(document.current_sha256, document.original_sha256) && document.original_size == document.current_size && !memcmp(document.original_bytes, document.current_bytes, document.current_size), "commit refreshes original bytes hash metadata and dirty state");
    cdx_document_free(&document);
}

static void test_encoded_size_limit(const wchar_t *path) {
    const size_t source_size = 6u * 1024u * 1024u + 3u;
    unsigned char *bytes = (unsigned char *)malloc(source_size);
    size_t offset;
    cdx_document document;
    cdx_edit_result edit;
    cdx_document_status status;
    if (!bytes) { expect(0, "encoded size fixture allocation"); return; }
    for (offset = 0; offset < source_size - 3u && available(); offset += 2u) {
        bytes[offset] = 0xd6;
        bytes[offset + 1u] = 0xd0;
    }
    if (offset != source_size - 3u) { free(bytes); expect(0, "encoded size fixture budget"); return; }
    memcpy(bytes + source_size - 3u, "END", 3u);
    if (!put_file(path, bytes, source_size)) { free(bytes); expect(0, "encoded size fixture write"); return; }
    free(bytes);
    status = cdx_document_open(&document, path, CDX_ENCODING_CP936);
    expect(status == CDX_STATUS_OK && document.current_size < CDX_DOCUMENT_MAX_BYTES && document.text_length > CDX_DOCUMENT_MAX_BYTES, "CP936 document source fits 8MiB while UTF8 view grows");
    if (status != CDX_STATUS_OK) return;
    status = cdx_document_edit_utf8(&document, document.current_sha256, "END", "XYZ", 1, 0, &edit);
    expect(status == CDX_STATUS_OK && document.current_size == source_size && document.dirty_in_memory, "encoding expansion does not reject same-size source edit");
    cdx_document_free(&document);
}

static void test_encoding_conversion(const wchar_t *path) {
    const unsigned char cp936[] = {0xd6, 0xd0, 0xce, 0xc4, '\r', '\n'};
    const unsigned char utf8[] = {0xe4, 0xb8, 0xad, 0xe6, 0x96, 0x87, '\r', '\n'};
    const unsigned char emoji[] = {0xf0, 0x9f, 0x99, 0x82, '\n'};
    const unsigned char wen[] = {0xe6, 0x96, 0x87, '\r', '\n'};
    const unsigned char wen_bom[] = {0xef, 0xbb, 0xbf, 0xe6, 0x96, 0x87, '\r', '\n'};
    const unsigned char wen_be[] = {0xfe, 0xff, 0x65, 0x87, 0, '\r', 0, '\n'};
    cdx_document document;
    cdx_edit_result edit;
    cdx_commit_result commit;
    cdx_document_status status;
    char initial_hash[65];
    (void)put_file(path, cp936, sizeof(cp936));
    status = cdx_document_open(&document, path, CDX_ENCODING_CP936);
    if (status != CDX_STATUS_OK) { expect(0, "conversion CP936 setup"); return; }
    memcpy(initial_hash, document.current_sha256, sizeof(initial_hash));
    status = cdx_document_convert_encoding(&document, initial_hash, CDX_ENCODING_AUTO, 0, &edit);
    expect(status == CDX_STATUS_VALIDATION_ERROR && !document.dirty_in_memory, "conversion requires explicit target encoding");
    status = cdx_document_convert_encoding(&document, "0000000000000000000000000000000000000000000000000000000000000000", CDX_ENCODING_UTF8, 0, &edit);
    expect(status == CDX_STATUS_FILE_CHANGED && !strcmp(initial_hash, document.current_sha256), "conversion rejects stale memory hash");
    status = cdx_document_convert_encoding(&document, initial_hash, CDX_ENCODING_UTF8, 1, &edit);
    expect(status == CDX_STATUS_OK && edit.status == CDX_STATUS_PREVIEW && document.encoding == CDX_ENCODING_CP936 && !document.dirty_in_memory && !strcmp(initial_hash, document.current_sha256) && disk_equals(path, cp936, sizeof(cp936)), "conversion dry run preserves bytes encoding hash and disk");
    status = cdx_document_convert_encoding(&document, initial_hash, CDX_ENCODING_UTF8, 0, &edit);
    expect(status == CDX_STATUS_OK && document.encoding == CDX_ENCODING_UTF8 && document.dirty_in_memory && disk_equals(path, cp936, sizeof(cp936)) && cdx_document_commit(&document, &commit) == CDX_STATUS_OK && commit.written_to_disk && !document.dirty_in_memory && disk_equals(path, utf8, sizeof(utf8)), "CP936 to UTF8 stays in memory until explicit commit");
    cdx_document_free(&document);
    (void)put_file(path, emoji, sizeof(emoji));
    status = cdx_document_open(&document, path, CDX_ENCODING_UTF8);
    if (status != CDX_STATUS_OK) { expect(0, "conversion emoji setup"); return; }
    memcpy(initial_hash, document.current_sha256, sizeof(initial_hash));
    status = cdx_document_convert_encoding(&document, initial_hash, CDX_ENCODING_CP936, 0, &edit);
    expect(status == CDX_STATUS_ENCODING_ERROR && document.encoding == CDX_ENCODING_UTF8 && !document.dirty_in_memory && !strcmp(initial_hash, document.current_sha256) && document.current_size == sizeof(emoji) && !memcmp(document.current_bytes, emoji, sizeof(emoji)) && disk_equals(path, emoji, sizeof(emoji)), "UTF8 emoji to CP936 fails without memory or disk mutation");
    cdx_document_free(&document);
    (void)put_file(path, wen, sizeof(wen));
    status = cdx_document_open(&document, path, CDX_ENCODING_UTF8);
    if (status != CDX_STATUS_OK) { expect(0, "conversion BOM setup"); return; }
    status = cdx_document_convert_encoding(&document, document.current_sha256, CDX_ENCODING_UTF8_BOM, 0, &edit);
    expect(status == CDX_STATUS_OK && document.has_bom && document.eol == CDX_EOL_CRLF && disk_equals(path, wen, sizeof(wen)) && cdx_document_commit(&document, &commit) == CDX_STATUS_OK && disk_equals(path, wen_bom, sizeof(wen_bom)), "UTF8 BOM conversion preserves CRLF and commits exact bytes");
    status = cdx_document_convert_encoding(&document, document.current_sha256, CDX_ENCODING_UTF16BE, 0, &edit);
    expect(status == CDX_STATUS_OK && document.has_bom && document.encoding == CDX_ENCODING_UTF16BE && document.eol == CDX_EOL_CRLF && !strcmp(document.text_utf8, "\xe6\x96\x87\r\n") && disk_equals(path, wen_bom, sizeof(wen_bom)) && cdx_document_commit(&document, &commit) == CDX_STATUS_OK && disk_equals(path, wen_be, sizeof(wen_be)), "UTF16BE conversion preserves Unicode text and commits BOM");
    cdx_document_free(&document);
    status = cdx_document_open(&document, path, CDX_ENCODING_AUTO);
    expect(status == CDX_STATUS_OK && document.encoding == CDX_ENCODING_UTF16BE && document.eol == CDX_EOL_CRLF && !document.dirty_in_memory && !strcmp(document.text_utf8, "\xe6\x96\x87\r\n"), "converted UTF16BE file independently opens with preserved text");
    if (status == CDX_STATUS_OK) cdx_document_free(&document);
}

int wmain(int argc, wchar_t **argv) {
    wchar_t owned[MAX_PATH], path[MAX_PATH];
    FILETIME created, exited, kernel, user;
    if (argc != 2) return 2;
    deadline = GetTickCount64() + 120000u;
    (void)SetConsoleCtrlHandler(on_interrupt, TRUE);
    (void)GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    (void)printf("owner_pid=%lu created_filetime=%lu:%lu max_checks=25 timeout_seconds=120\n", GetCurrentProcessId(), created.dwHighDateTime, created.dwLowDateTime);
    (void)_snwprintf_s(owned, _countof(owned), _TRUNCATE, L"%ls\\edge-%lu-%llu", argv[1], GetCurrentProcessId(), GetTickCount64());
    if (!CreateDirectoryW(owned, NULL)) return 2;
    (void)_snwprintf_s(path, _countof(path), _TRUNCATE, L"%ls\\fixture.txt", owned);
    if (available()) test_strict_encodings(path);
    if (GetEnvironmentVariableW(L"CDX_PILOT", NULL, 0) == 0) {
        if (available()) test_protocol_memory(path);
        if (available()) test_protocol_utf8_page(path);
        if (available()) test_commit_and_dirty(path);
        if (available()) test_encoded_size_limit(path);
        if (available()) test_encoding_conversion(path);
    }
    cdx_workspace_clear();
    (void)DeleteFileW(path);
    if (!RemoveDirectoryW(owned)) { (void)fprintf(stderr, "owned test directory cleanup failed\n"); ++failures; }
    if (cancelled || GetTickCount64() >= deadline) ++failures;
    (void)printf("checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
