#define _POSIX_C_SOURCE 200809L
#include "cdx/document.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failures;
static void check(int condition, const char *name) {
    ++checks;
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}
static int write_bytes(const char *path, const unsigned char *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    int ok;
    if (!file) return 0;
    ok = fwrite(bytes, 1, size, file) == size;
    return fclose(file) == 0 && ok;
}
static int equal_file(const char *path, const unsigned char *bytes, size_t size) {
    unsigned char buffer[2048];
    FILE *file = fopen(path, "rb");
    size_t count;
    if (!file) return 0;
    count = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);
    return count == size && !memcmp(buffer, bytes, size);
}
static void text_case(const char *path) {
    cdx_document document;
    cdx_edit_result edit;
    cdx_commit_result commit;
    struct stat metadata;
    mode_t original_mode = 0;
    char hash[65];
    check(write_bytes(path, (const unsigned char *)"old old\r\n", 9), "write real UTF8 CRLF fixture");
    check(chmod(path, 0640) == 0, "set fixture mode");
    if (!stat(path, &metadata)) original_mode = metadata.st_mode & 07777;
    check(cdx_document_open(&document, path, CDX_ENCODING_AUTO) == CDX_STATUS_OK, "open real UTF8 file");
    if (!document.text_utf8) return;
    check(document.encoding == CDX_ENCODING_UTF8 && document.eol == CDX_EOL_CRLF, "UTF8 CRLF metadata");
    memcpy(hash, document.current_sha256, sizeof(hash));
    check(cdx_document_edit_utf8(&document, hash, "old", "new", 1, 0, &edit) == CDX_STATUS_AMBIGUOUS_MATCH && !document.dirty_in_memory, "ambiguous count is zero change");
    check(cdx_document_edit_utf8(&document, hash, "missing", "new", 1, 0, &edit) == CDX_STATUS_ANCHOR_MISMATCH, "missing anchor is zero change");
    check(cdx_document_edit_utf8(&document, hash, "old", "longer", 2, 1, &edit) == CDX_STATUS_OK && edit.status == CDX_STATUS_PREVIEW && !document.dirty_in_memory, "preview leaves memory clean");
    check(cdx_document_edit_utf8(&document, hash, "old", "longer", 2, 0, &edit) == CDX_STATUS_OK && document.dirty_in_memory && equal_file(path, (const unsigned char *)"old old\r\n", 9), "memory change does not touch disk");
    check(cdx_document_edit_utf8(&document, hash, "longer", "new", 2, 0, &edit) == CDX_STATUS_FILE_CHANGED, "stale memory SHA rejected");
    check(cdx_document_commit(&document, &commit) == CDX_STATUS_OK && commit.written_to_disk && commit.cleanup_complete && !document.dirty_in_memory && equal_file(path, (const unsigned char *)"longer longer\r\n", 15), "commit readback and cleanup");
    check(stat(path, &metadata) == 0 && (metadata.st_mode & 07777) == original_mode, "commit preserves filesystem-reported permissions");
    check(cdx_document_commit(&document, &commit) == CDX_STATUS_OK && !commit.written_to_disk, "clean commit performs no write");
    check(cdx_document_edit_utf8(&document, document.current_sha256, "longer", "x", 2, 0, &edit) == CDX_STATUS_OK, "prepare conflict");
    check(write_bytes(path, (const unsigned char *)"external\n", 9), "external writer changes file");
    check(cdx_document_commit(&document, &commit) == CDX_STATUS_FILE_CHANGED && !commit.written_to_disk && commit.cleanup_complete && document.dirty_in_memory && equal_file(path, (const unsigned char *)"external\n", 9), "external conflict preserves external bytes");
    cdx_document_free(&document);
}
static void encodings_case(const char *path) {
    static const unsigned char utf8bom[] = {0xef, 0xbb, 0xbf, 'a', '\r', '\n'};
    static const unsigned char utf16le[] = {0xff, 0xfe, 0x3d, 0xd8, 0x42, 0xde, '\n', 0};
    static const unsigned char utf16be[] = {0xfe, 0xff, 0xd8, 0x3d, 0xde, 0x42, 0, '\n'};
    static const unsigned char cp936[] = {0xd6, 0xd0, 0xce, 0xc4, '\r', '\n'};
    static const unsigned char invalid[] = {0xc0, 0xaf};
    static const unsigned char invalid16[] = {0xff, 0xfe, 0x00, 0xd8};
    static const unsigned char above_unicode[] = {0xf4, 0x90, 0x80, 0x80};
    cdx_document document;
    cdx_edit_result edit;
    cdx_commit_result commit;
    cdx_document_status status;
    write_bytes(path, utf8bom, sizeof(utf8bom));
    status = cdx_document_open(&document, path, CDX_ENCODING_AUTO);
    check(status == CDX_STATUS_OK && document.encoding == CDX_ENCODING_UTF8_BOM && document.has_bom, "UTF8 BOM detection");
    if (status == CDX_STATUS_OK) {
        check(cdx_document_edit_utf8(&document, document.current_sha256, "a", "b", 1, 0, &edit) == CDX_STATUS_OK && document.current_bytes[0] == 0xef, "BOM retained through memory edit");
        cdx_document_free(&document);
    }
    write_bytes(path, utf16le, sizeof(utf16le));
    status = cdx_document_open(&document, path, CDX_ENCODING_AUTO);
    check(status == CDX_STATUS_OK && document.encoding == CDX_ENCODING_UTF16LE && !strcmp(document.text_utf8, "\xf0\x9f\x99\x82\n"), "UTF16LE non-BMP roundtrip");
    if (status == CDX_STATUS_OK) {
        check(cdx_document_convert_encoding(&document, document.current_sha256, CDX_ENCODING_CP936, 0, &edit) == CDX_STATUS_ENCODING_ERROR && !document.dirty_in_memory, "CP936 rejects unrepresentable emoji");
        check(cdx_document_convert_encoding(&document, document.current_sha256, CDX_ENCODING_UTF16BE, 1, &edit) == CDX_STATUS_OK && edit.status == CDX_STATUS_PREVIEW && document.encoding == CDX_ENCODING_UTF16LE, "conversion preview retains encoding");
        check(cdx_document_convert_encoding(&document, document.current_sha256, CDX_ENCODING_UTF16BE, 0, &edit) == CDX_STATUS_OK && equal_file(path, utf16le, sizeof(utf16le)), "encoding conversion only changes memory");
        check(cdx_document_commit(&document, &commit) == CDX_STATUS_OK && equal_file(path, utf16be, sizeof(utf16be)), "UTF16BE explicit commit exact bytes");
        cdx_document_free(&document);
    }
    status = cdx_document_open(&document, path, CDX_ENCODING_AUTO);
    check(status == CDX_STATUS_OK && document.encoding == CDX_ENCODING_UTF16BE, "UTF16BE reopens");
    if (status == CDX_STATUS_OK) cdx_document_free(&document);
    write_bytes(path, cp936, sizeof(cp936));
    check(cdx_document_open(&document, path, CDX_ENCODING_AUTO) == CDX_STATUS_ENCODING_ERROR, "AUTO never guesses GBK");
    status = cdx_document_open(&document, path, CDX_ENCODING_CP936);
    check(status == CDX_STATUS_OK && !strcmp(document.text_utf8, "\xe4\xb8\xad\xe6\x96\x87\r\n"), "explicit CP936 strict roundtrip");
    if (status == CDX_STATUS_OK) {
        check(cdx_document_edit_utf8(&document, document.current_sha256, "\r\n", "!\r\n", 1, 0, &edit) == CDX_STATUS_OK && cdx_document_commit(&document, &commit) == CDX_STATUS_OK, "CP936 edit and commit");
        cdx_document_free(&document);
    }
    write_bytes(path, invalid, sizeof(invalid));
    check(cdx_document_open(&document, path, CDX_ENCODING_UTF8) == CDX_STATUS_ENCODING_ERROR, "invalid UTF8 rejected");
    write_bytes(path, invalid16, sizeof(invalid16));
    check(cdx_document_open(&document, path, CDX_ENCODING_AUTO) == CDX_STATUS_ENCODING_ERROR, "unpaired UTF16 surrogate rejected");
    write_bytes(path, above_unicode, sizeof(above_unicode));
    check(cdx_document_open(&document, path, CDX_ENCODING_AUTO) == CDX_STATUS_ENCODING_ERROR, "UTF8 above U+10FFFF rejected");
    cdx_document_free(&document);
}
static void edge_case(const char *path) {
    cdx_document document;
    cdx_edit_result edit;
    cdx_commit_result commit;
    write_bytes(path, (const unsigned char *)"abc", 3);
    check(cdx_document_open(&document, path, CDX_ENCODING_AUTO) == CDX_STATUS_OK, "known hash opens");
    if (!document.text_utf8) return;
    check(!strcmp(document.current_sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA256 abc known vector");
    check(cdx_document_edit_utf8(&document, document.current_sha256, "abc", "", 1, 0, &edit) == CDX_STATUS_OK && cdx_document_commit(&document, &commit) == CDX_STATUS_OK, "empty result commit");
    check(!strcmp(document.current_sha256, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "SHA256 empty known vector");
    cdx_document_free(&document);
    write_bytes(path, (const unsigned char *)"a\r\nb\n", 5);
    check(cdx_document_open(&document, path, CDX_ENCODING_AUTO) == CDX_STATUS_OK, "mixed EOL opens");
    if (!document.text_utf8) return;
    check(document.eol == CDX_EOL_MIXED && cdx_document_edit_utf8(&document, document.current_sha256, "a", "c", 1, 0, &edit) == CDX_STATUS_MIXED_EOL, "mixed EOL edit rejected");
    cdx_document_free(&document);
    write_bytes(path, (const unsigned char *)"aaa", 3);
    check(cdx_document_open(&document, path, CDX_ENCODING_AUTO) == CDX_STATUS_OK, "overlap fixture opens");
    if (!document.text_utf8) return;
    check(cdx_document_edit_utf8(&document, document.current_sha256, "aa", "b", 2, 0, &edit) == CDX_STATUS_AMBIGUOUS_MATCH, "overlapping matches rejected");
    cdx_document_free(&document);
    check(cdx_document_open(&document, "relative.txt", CDX_ENCODING_AUTO) == CDX_STATUS_VALIDATION_ERROR, "relative path rejected");
    check(cdx_document_open(&document, "/tmp/$(code)", CDX_ENCODING_AUTO) == CDX_STATUS_VALIDATION_ERROR, "code-shaped path rejected");
}
static void hash_case(const char *path) {
    cdx_document document;
    unsigned char *million = (unsigned char *)malloc(1000000u);
    const unsigned char two_blocks[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    if (!million) { check(0, "allocate bounded hash fixture"); return; }
    write_bytes(path, two_blocks, sizeof(two_blocks) - 1u);
    check(cdx_document_open(&document, path, CDX_ENCODING_AUTO) == CDX_STATUS_OK &&
        !strcmp(document.current_sha256, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "SHA256 two-block padding known vector");
    cdx_document_free(&document);
    memset(million, 'a', 1000000u);
    write_bytes(path, million, 1000000u);
    free(million);
    check(cdx_document_open(&document, path, CDX_ENCODING_AUTO) == CDX_STATUS_OK &&
        !strcmp(document.current_sha256, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"), "SHA256 million-byte known vector");
    cdx_document_free(&document);
}
int main(int argc, char **argv) {
    char directory[4096], path[4096];
    int length;
    if (argc != 2) return 2;
    length = snprintf(directory, sizeof(directory), "%s/document-test-XXXXXX", argv[1]);
    if (length < 0 || (size_t)length >= sizeof(directory) || !mkdtemp(directory)) return 2;
    length = snprintf(path, sizeof(path), "%s/中文 document.txt", directory);
    if (length < 0 || (size_t)length >= sizeof(path)) { rmdir(directory); return 2; }
    text_case(path);
    if (!getenv("CDX_PILOT")) { encodings_case(path); edge_case(path); hash_case(path); }
    check(unlink(path) == 0 && rmdir(directory) == 0, "all owned fixtures and temporary files cleaned");
    printf("checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
