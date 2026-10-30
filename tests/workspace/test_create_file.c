#define _POSIX_C_SOURCE 200809L
#include "cdx/create_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <wchar.h>
#define PATH_TEXT(value) L##value
#else
#include <sys/stat.h>
#include <unistd.h>
#define PATH_TEXT(value) value
#endif

static int checks, failures;

static void check(int condition, const char *name) {
    ++checks;
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}

static int file_absent(const cdx_path_char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesW(path), error = GetLastError();
    return attributes == INVALID_FILE_ATTRIBUTES && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
#else
    struct stat metadata;
    return lstat(path, &metadata) != 0;
#endif
}

static void remove_file(const cdx_path_char *path) {
#ifdef _WIN32
    (void)DeleteFileW(path);
#else
    (void)unlink(path);
#endif
}

static int file_equals(const cdx_path_char *path, const unsigned char *expected, size_t length) {
    unsigned char data[256];
    size_t count;
    int failed;
    FILE *file = NULL;
#ifdef _WIN32
    if (_wfopen_s(&file, path, L"rb") != 0) return 0;
#else
    file = fopen(path, "rb");
    if (!file) return 0;
#endif
    count = fread(data, 1, sizeof(data), file);
    failed = ferror(file);
    if (fclose(file) || failed) return 0;
    return count == length && memcmp(data, expected, length) == 0;
}

static void create_bytes(const cdx_path_char *path, const char *text, cdx_encoding encoding,
                         const unsigned char *expected, size_t length, const char *name) {
    cdx_create_result result;
    cdx_create_status status = cdx_create_file(path, text, strlen(text), encoding, NULL, &result);
    check(status == CDX_CREATE_OK && result.status == status && result.written_to_disk &&
          result.cleanup_complete && result.encoding == encoding && result.bytes == length &&
          strlen(result.disk_sha256) == 64 && file_equals(path, expected, length), name);
    remove_file(path);
}

static void reject_create(const cdx_path_char *path, const char *text, size_t length,
                          cdx_encoding encoding, cdx_create_status expected, const char *name) {
    cdx_create_result result;
    cdx_create_status status = cdx_create_file(path, text, length, encoding, NULL, &result);
    check(status == expected && result.status == status && !result.written_to_disk &&
          result.cleanup_complete && result.encoding == encoding && result.bytes == 0 &&
          result.disk_sha256[0] == 0 && file_absent(path), name);
    remove_file(path);
}

static void remaining_cases(const cdx_path_char *path, const cdx_path_char *directory,
                             const cdx_path_char *missing_parent, const cdx_path_char *code_path) {
    const unsigned char utf8_bom[] = {0xef, 0xbb, 0xbf, 'a', '\r', '\n'};
    const unsigned char utf16le[] = {0xff, 0xfe, 'a', 0, '\r', 0, '\n', 0};
    const unsigned char utf16be[] = {0xfe, 0xff, 0, 'a', 0, '\r', 0, '\n'};
    const unsigned char cp936[] = {0xd6, 0xd0, 0xce, 0xc4, '\r', '\n'};
    const char literal[] = "\xe4\xb8\xad\xe6\x96\x87 \"quoted\" $(literal)\r\nsecond\nlast\r";
    cdx_create_result result;
    cdx_create_status status;
    char *large;
#ifdef _WIN32
    HANDLE cancellation = CreateEventW(NULL, TRUE, TRUE, NULL);
#else
    atomic_int cancelled;
    cdx_cancel_handle cancellation = &cancelled;
    atomic_init(&cancelled, 1);
#endif
    create_bytes(path, literal, CDX_ENCODING_UTF8, (const unsigned char *)literal,
                 sizeof(literal) - 1u, "Unicode and literal shell text preserve mixed EOL bytes");
    create_bytes(path, "a\r\n", CDX_ENCODING_UTF8_BOM, utf8_bom, sizeof(utf8_bom), "UTF8 BOM bytes");
    create_bytes(path, "a\r\n", CDX_ENCODING_UTF16LE, utf16le, sizeof(utf16le), "UTF16LE BOM bytes");
    create_bytes(path, "a\r\n", CDX_ENCODING_UTF16BE, utf16be, sizeof(utf16be), "UTF16BE BOM bytes");
    create_bytes(path, "\xe4\xb8\xad\xe6\x96\x87\r\n", CDX_ENCODING_CP936, cp936, sizeof(cp936), "CP936 exact bytes");
    reject_create(path, "\xf0\x9f\x99\x82", 4, CDX_ENCODING_CP936, CDX_CREATE_ENCODING_ERROR, "unrepresentable CP936 leaves no file");
    reject_create(path, "\xc0\xaf", 2, CDX_ENCODING_UTF8, CDX_CREATE_ENCODING_ERROR, "invalid UTF8 leaves no file");
    reject_create(path, "a\0b", 3, CDX_ENCODING_UTF8, CDX_CREATE_ENCODING_ERROR, "text NUL leaves no file");
    reject_create(path, "x", 1, CDX_ENCODING_AUTO, CDX_CREATE_VALIDATION_ERROR, "AUTO is invalid for creation");
    reject_create(path, "x", CDX_DOCUMENT_MAX_BYTES + 1u, CDX_ENCODING_UTF8, CDX_CREATE_TOO_LARGE, "input size checked before reading content");
    reject_create(missing_parent, "x", 1, CDX_ENCODING_UTF8, CDX_CREATE_NOT_FOUND, "missing parent is not created");
    status = cdx_create_file(directory, "x", 1, CDX_ENCODING_UTF8, NULL, &result);
    check(status == CDX_CREATE_ALREADY_EXISTS && !result.written_to_disk && result.cleanup_complete,
          "existing directory is not replaced");
    status = cdx_create_file(path, "", 0, CDX_ENCODING_UTF8, NULL, &result);
    check(status == CDX_CREATE_OK && result.written_to_disk && result.bytes == 0 && result.cleanup_complete &&
          strcmp(result.disk_sha256, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0 &&
          file_equals(path, (const unsigned char *)"", 0), "empty file and known SHA256");
    remove_file(path);
    status = cdx_create_file(path, "x", 1, CDX_ENCODING_UTF8, cancellation, &result);
    check(cancellation && status == CDX_CREATE_CANCELLED && !result.written_to_disk &&
          result.cleanup_complete && file_absent(path), "cancel before creation leaves no file");
    remove_file(path);
#ifdef _WIN32
    if (cancellation) CloseHandle(cancellation);
#endif
    status = cdx_create_file(PATH_TEXT("relative.txt"), "x", 1, CDX_ENCODING_UTF8, NULL, &result);
    check(status == CDX_CREATE_VALIDATION_ERROR && !result.written_to_disk && result.cleanup_complete,
          "relative path rejected");
    reject_create(code_path, "x", 1, CDX_ENCODING_UTF8, CDX_CREATE_VALIDATION_ERROR, "code-shaped path rejected");
    large = (char *)malloc(CDX_DOCUMENT_MAX_BYTES / 2u + 1u);
    if (!large) check(0, "encoded size fixture allocation");
    else {
        memset(large, 'a', CDX_DOCUMENT_MAX_BYTES / 2u + 1u);
        reject_create(path, large, CDX_DOCUMENT_MAX_BYTES / 2u + 1u, CDX_ENCODING_UTF16LE,
                      CDX_CREATE_TOO_LARGE, "encoded size limit leaves no file");
        free(large);
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
    cdx_path_char directory[1024], path[1100], missing_parent[1200], code_path[1200];
    cdx_create_result result;
    cdx_create_status status;
    int pilot;
    if (argc != 2 && argc != 3) return 2;
#ifdef _WIN32
    pilot = argc == 3 && wcscmp(argv[2], L"--pilot") == 0;
    if (argc == 3 && !pilot) return 2;
    if (_snwprintf_s(directory, _countof(directory), _TRUNCATE, L"%ls\\create-file-%lu", argv[1], (unsigned long)GetCurrentProcessId()) < 0 ||
        !CreateDirectoryW(directory, NULL)) return 2;
    _snwprintf_s(path, _countof(path), _TRUNCATE, L"%ls\\\x4e2d\x6587 file.txt", directory);
    _snwprintf_s(missing_parent, _countof(missing_parent), _TRUNCATE, L"%ls\\absent\\file.txt", directory);
    _snwprintf_s(code_path, _countof(code_path), _TRUNCATE, L"%ls\\$(literal).txt", directory);
#else
    pilot = argc == 3 && strcmp(argv[2], "--pilot") == 0;
    if (argc == 3 && !pilot) return 2;
    if (snprintf(directory, sizeof(directory), "%s/create-file-%ld", argv[1], (long)getpid()) >= (int)sizeof(directory) ||
        mkdir(directory, 0700)) return 2;
    (void)snprintf(path, sizeof(path), "%s/\xe4\xb8\xad\xe6\x96\x87 file.txt", directory);
    (void)snprintf(missing_parent, sizeof(missing_parent), "%s/absent/file.txt", directory);
    (void)snprintf(code_path, sizeof(code_path), "%s/$(literal).txt", directory);
#endif
    status = cdx_create_file(path, "abc", 3, CDX_ENCODING_UTF8, NULL, &result);
    check(status == CDX_CREATE_OK && result.written_to_disk && result.cleanup_complete && result.bytes == 3 &&
          strcmp(result.disk_sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0 &&
          file_equals(path, (const unsigned char *)"abc", 3), "create with known SHA256 and native readback");
    status = cdx_create_file(path, "replacement", 11, CDX_ENCODING_UTF8, NULL, &result);
    check(status == CDX_CREATE_ALREADY_EXISTS && !result.written_to_disk && result.cleanup_complete &&
          result.disk_sha256[0] == 0 && file_equals(path, (const unsigned char *)"abc", 3), "existing file is unchanged");
    remove_file(path);
    if (!pilot) remaining_cases(path, directory, missing_parent, code_path);
#ifdef _WIN32
    check(RemoveDirectoryW(directory) != 0, "all owned files and temporary names cleaned");
#else
    check(rmdir(directory) == 0, "all owned files and temporary names cleaned");
#endif
    printf("checks=%d failures=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
