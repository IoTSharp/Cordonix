#include "cdx/document.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

static void check(int condition, const char *name) {
    ++checks;
    if (condition) printf("PASS %s\n", name);
    else { ++failures; printf("FAIL %s\n", name); }
}

static int write_bytes(const wchar_t *path, const unsigned char *bytes, size_t length) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!WriteFile(file, bytes, (DWORD)length, &written, NULL) || written != length) { CloseHandle(file); return 0; }
    CloseHandle(file);
    return 1;
}

static int equal_file(const wchar_t *path, const unsigned char *expected, size_t expected_size) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    unsigned char bytes[2048];
    DWORD count = 0;
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(file, bytes, sizeof(bytes), &count, NULL)) { CloseHandle(file); return 0; }
    CloseHandle(file);
    return count == expected_size && memcmp(bytes, expected, expected_size) == 0;
}

static void case_utf8(const wchar_t *path) {
    const unsigned char original[] = "alpha\nold\n";
    const unsigned char changed[] = "alpha\nlonger\n";
    cdx_document document;
    cdx_edit_result edit;
    cdx_commit_result commit;
    cdx_document_status status;
    check(write_bytes(path, original, sizeof(original) - 1u), "create UTF8 fixture");
    status = cdx_document_open(&document, path, CDX_ENCODING_UTF8);
    check(status == CDX_STATUS_OK && document.eol == CDX_EOL_LF && !document.dirty_in_memory, "UTF8 open metadata");
    if (status != CDX_STATUS_OK) return;
    status = cdx_document_edit_utf8(&document, document.current_sha256, "old", "longer", 1u, 1, &edit);
    check(status == CDX_STATUS_OK && edit.status == CDX_STATUS_PREVIEW && !document.dirty_in_memory, "edit dry run remains memory clean");
    status = cdx_document_edit_utf8(&document, document.current_sha256, "old", "longer", 1u, 0, &edit);
    check(status == CDX_STATUS_OK && document.dirty_in_memory && equal_file(path, original, sizeof(original) - 1u), "memory edit avoids disk write");
    status = cdx_document_commit(&document, &commit);
    check(status == CDX_STATUS_OK && commit.written_to_disk && !document.dirty_in_memory && equal_file(path, changed, sizeof(changed) - 1u), "explicit UTF8 commit");
    cdx_document_free(&document);
}

static void case_utf8_bom_crlf(const wchar_t *path) {
    const unsigned char original[] = {0xef, 0xbb, 0xbf, 'a', '\r', '\n', 'b', '\r', '\n'};
    const unsigned char changed[] = {0xef, 0xbb, 0xbf, 'a', '\r', '\n', 'c', '\r', '\n'};
    cdx_document document; cdx_edit_result edit; cdx_commit_result commit; cdx_document_status status;
    check(write_bytes(path, original, sizeof(original)), "create BOM CRLF fixture");
    status = cdx_document_open(&document, path, CDX_ENCODING_AUTO);
    check(status == CDX_STATUS_OK && document.encoding == CDX_ENCODING_UTF8_BOM && document.eol == CDX_EOL_CRLF, "UTF8 BOM CRLF detected");
    if (status != CDX_STATUS_OK) return;
    status = cdx_document_edit_utf8(&document, document.current_sha256, "b", "c", 1u, 0, &edit);
    check(status == CDX_STATUS_OK, "BOM CRLF memory edit");
    status = cdx_document_commit(&document, &commit);
    check(status == CDX_STATUS_OK && equal_file(path, changed, sizeof(changed)), "BOM CRLF bytes retained");
    cdx_document_free(&document);
}

static void case_cp936(const wchar_t *path) {
    const unsigned char original[] = {0xd6, 0xd0, 0xce, 0xc4, '\r', '\n'};
    const unsigned char changed[] = {0xd6, 0xd0, 0xce, 0xc4, '!', '\r', '\n'};
    cdx_document document; cdx_edit_result edit; cdx_commit_result commit; cdx_document_status status;
    check(write_bytes(path, original, sizeof(original)), "create CP936 fixture");
    status = cdx_document_open(&document, path, CDX_ENCODING_CP936);
    check(status == CDX_STATUS_OK && document.encoding == CDX_ENCODING_CP936 && strcmp(document.text_utf8, "\xe4\xb8\xad\xe6\x96\x87\r\n") == 0, "CP936 strict decode");
    if (status != CDX_STATUS_OK) return;
    status = cdx_document_edit_utf8(&document, document.current_sha256, "\r\n", "!\r\n", 1u, 0, &edit);
    check(status == CDX_STATUS_OK, "CP936 memory edit");
    status = cdx_document_commit(&document, &commit);
    check(status == CDX_STATUS_OK && equal_file(path, changed, sizeof(changed)), "CP936 byte encoding retained");
    status = cdx_document_edit_utf8(&document, document.current_sha256, "!", "\xf0\x9f\x99\x82", 1u, 0, &edit);
    check(status == CDX_STATUS_ENCODING_ERROR && !document.dirty_in_memory && equal_file(path, changed, sizeof(changed)), "unrepresentable CP936 edit zero write");
    cdx_document_free(&document);
}

static void case_utf16(const wchar_t *path) {
    const unsigned char original[] = {0xff, 0xfe, 'a', 0, '\r', 0, '\n', 0};
    const unsigned char changed[] = {0xff, 0xfe, 'b', 0, '\r', 0, '\n', 0};
    cdx_document document; cdx_edit_result edit; cdx_commit_result commit; cdx_document_status status;
    check(write_bytes(path, original, sizeof(original)), "create UTF16LE fixture");
    status = cdx_document_open(&document, path, CDX_ENCODING_AUTO);
    check(status == CDX_STATUS_OK && document.encoding == CDX_ENCODING_UTF16LE && document.has_bom, "UTF16LE BOM decode");
    if (status != CDX_STATUS_OK) return;
    status = cdx_document_edit_utf8(&document, document.current_sha256, "a", "b", 1u, 0, &edit);
    check(status == CDX_STATUS_OK, "UTF16LE memory edit");
    status = cdx_document_commit(&document, &commit);
    check(status == CDX_STATUS_OK && equal_file(path, changed, sizeof(changed)), "UTF16LE BOM bytes retained");
    cdx_document_free(&document);
}

static void case_conflict(const wchar_t *path) {
    const unsigned char original[] = "old\n";
    const unsigned char external[] = "external\n";
    cdx_document document; cdx_edit_result edit; cdx_commit_result commit; cdx_document_status status;
    write_bytes(path, original, sizeof(original) - 1u);
    status = cdx_document_open(&document, path, CDX_ENCODING_UTF8);
    if (status != CDX_STATUS_OK) { check(0, "conflict setup"); return; }
    status = cdx_document_edit_utf8(&document, document.current_sha256, "old", "new", 1u, 0, &edit);
    check(status == CDX_STATUS_OK, "conflict memory edit");
    write_bytes(path, external, sizeof(external) - 1u);
    status = cdx_document_commit(&document, &commit);
    check(status == CDX_STATUS_FILE_CHANGED && !commit.written_to_disk && document.dirty_in_memory && equal_file(path, external, sizeof(external) - 1u), "external version conflict zero write");
    cdx_document_free(&document);
}

static void case_edge_cases(const wchar_t *path) {
    const unsigned char utf16be[] = {0xfe, 0xff, 0, 'a', 0, '\n'};
    const unsigned char utf16be_changed[] = {0xfe, 0xff, 0, 'z', 0, '\n'};
    const unsigned char invalid[] = {0xc0, 0xaf};
    const unsigned char cp936[] = {0xd6, 0xd0};
    const unsigned char mixed[] = "a\r\nb\n";
    const unsigned char repeated[] = "old old\n";
    const unsigned char abc[] = "abc";
    cdx_document document;
    cdx_edit_result edit;
    cdx_commit_result commit;
    cdx_document_status status;
    char baseline_hash[65];
    write_bytes(path, utf16be, sizeof(utf16be));
    status = cdx_document_open(&document, path, CDX_ENCODING_AUTO);
    check(status == CDX_STATUS_OK && document.encoding == CDX_ENCODING_UTF16BE, "UTF16BE BOM decode");
    if (status == CDX_STATUS_OK) {
        status = cdx_document_edit_utf8(&document, document.current_sha256, "a", "z", 1, 0, &edit);
        check(status == CDX_STATUS_OK && cdx_document_commit(&document, &commit) == CDX_STATUS_OK && equal_file(path, utf16be_changed, sizeof(utf16be_changed)), "UTF16BE edit commit preserves BOM");
        cdx_document_free(&document);
    }
    write_bytes(path, invalid, sizeof(invalid));
    check(cdx_document_open(&document, path, CDX_ENCODING_UTF8) == CDX_STATUS_ENCODING_ERROR && equal_file(path, invalid, sizeof(invalid)), "invalid UTF8 zero write");
    write_bytes(path, cp936, sizeof(cp936));
    check(cdx_document_open(&document, path, CDX_ENCODING_AUTO) == CDX_STATUS_ENCODING_ERROR, "AUTO does not silently guess CP936");
    write_bytes(path, mixed, sizeof(mixed) - 1u);
    status = cdx_document_open(&document, path, CDX_ENCODING_UTF8);
    if (status == CDX_STATUS_OK) {
        check(document.eol == CDX_EOL_MIXED && cdx_document_edit_utf8(&document, document.current_sha256, "a", "c", 1, 0, &edit) == CDX_STATUS_MIXED_EOL && equal_file(path, mixed, sizeof(mixed) - 1u), "mixed EOL edit rejected");
        cdx_document_free(&document);
    } else check(0, "mixed EOL setup");
    write_bytes(path, repeated, sizeof(repeated) - 1u);
    status = cdx_document_open(&document, path, CDX_ENCODING_UTF8);
    if (status == CDX_STATUS_OK) {
        check(cdx_document_edit_utf8(&document, document.current_sha256, "old", "new", 1, 0, &edit) == CDX_STATUS_AMBIGUOUS_MATCH && !document.dirty_in_memory, "ambiguous count rejects memory change");
        check(cdx_document_edit_utf8(&document, document.current_sha256, "absent", "new", 1, 0, &edit) == CDX_STATUS_ANCHOR_MISMATCH, "missing anchor rejects memory change");
        memcpy(baseline_hash, document.current_sha256, sizeof(baseline_hash));
        status = cdx_document_edit_utf8(&document, baseline_hash, "old", "longer", 2, 0, &edit);
        check(status == CDX_STATUS_OK && strcmp(document.text_utf8, "longer longer\n") == 0 && equal_file(path, repeated, sizeof(repeated) - 1u), "two replacements expand in memory");
        check(cdx_document_edit_utf8(&document, baseline_hash, "longer", "new", 2, 0, &edit) == CDX_STATUS_FILE_CHANGED, "stale memory SHA rejected");
        status = cdx_document_edit_utf8(&document, document.current_sha256, "longer", "x", 2, 0, &edit);
        check(status == CDX_STATUS_OK && strcmp(document.text_utf8, "x x\n") == 0 && equal_file(path, repeated, sizeof(repeated) - 1u), "second edit remains in memory");
        check(cdx_document_commit(&document, &commit) == CDX_STATUS_OK && equal_file(path, (const unsigned char *)"x x\n", 4u), "combined memory edits commit once");
        cdx_document_free(&document);
    } else check(0, "repeated setup");
    write_bytes(path, abc, sizeof(abc) - 1u);
    status = cdx_document_open(&document, path, CDX_ENCODING_UTF8);
    if (status == CDX_STATUS_OK) {
        check(strcmp(document.current_sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0, "SHA256 matches known fixture");
        check(cdx_document_edit_utf8(&document, document.current_sha256, "a", "\xff", 1, 0, &edit) == CDX_STATUS_ENCODING_ERROR && !document.dirty_in_memory, "invalid UTF8 replacement rejected");
        check(cdx_document_edit_utf8(&document, document.current_sha256, "abc", "", 1, 0, &edit) == CDX_STATUS_OK && cdx_document_commit(&document, &commit) == CDX_STATUS_OK && equal_file(path, (const unsigned char *)"", 0), "empty result commits correctly");
        cdx_document_free(&document);
    } else check(0, "abc setup");
    status = cdx_document_open(&document, path, CDX_ENCODING_UTF8);
    check(status == CDX_STATUS_OK && document.current_size == 0u && document.text_length == 0u, "empty UTF8 file opens");
    if (status == CDX_STATUS_OK) cdx_document_free(&document);
    write_bytes(path, (const unsigned char *)"aaa", 3u);
    status = cdx_document_open(&document, path, CDX_ENCODING_UTF8);
    if (status == CDX_STATUS_OK) {
        check(cdx_document_edit_utf8(&document, document.current_sha256, "aa", "b", 2, 0, &edit) == CDX_STATUS_AMBIGUOUS_MATCH && !document.dirty_in_memory && equal_file(path, (const unsigned char *)"aaa", 3u), "overlapping anchor rejected before allocation");
        cdx_document_free(&document);
    } else check(0, "overlap setup");
}

int wmain(int argc, wchar_t **argv) {
    wchar_t path[MAX_PATH];
    if (argc != 2) return 2;
    _snwprintf_s(path, _countof(path), _TRUNCATE, L"%ls\\workspace-test.txt", argv[1]);
    case_utf8(path);
    if (GetEnvironmentVariableW(L"CDX_PILOT", NULL, 0) == 0) { case_utf8_bom_crlf(path); case_cp936(path); case_utf16(path); case_conflict(path); case_edge_cases(path); }
    DeleteFileW(path);
    printf("checks=%d failures=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
