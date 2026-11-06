#define _POSIX_C_SOURCE 200809L
#include "cdx/file_ops.h"
#include "cdx/filesystem.h"
#include "cdx/operations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <wchar.h>
#define PATH_TEXT(value) L##value
#else
#include <sys/stat.h>
#ifdef __linux__
#include <sys/vfs.h>
#endif
#include <unistd.h>
#define PATH_TEXT(value) value
#endif

static unsigned checks, failures;

static void check(int valid, const char *name) {
    ++checks; if (!valid) ++failures;
    printf("%s %s\n", valid ? "PASS" : "FAIL", name);
}

static int join_path(cdx_path_char path[2048], const cdx_path_char *root, const cdx_path_char *name) {
#ifdef _WIN32
    return _snwprintf_s(path, 2048u, _TRUNCATE, L"%ls/%ls", root, name) >= 0;
#else
    int length = snprintf(path, 2048u, "%s/%s", root, name);
    return length >= 0 && length < 2048;
#endif
}

static FILE *open_file(const cdx_path_char *path, int writing) {
#ifdef _WIN32
    FILE *file = NULL;
    (void)_wfopen_s(&file, path, writing ? L"wb" : L"rb"); return file;
#else
    return fopen(path, writing ? "wb" : "rb");
#endif
}

static int write_bytes(const cdx_path_char *path, const unsigned char *bytes, size_t length) {
    FILE *file = open_file(path, 1);
    int valid;
    if (!file) return 0;
    valid = fwrite(bytes, 1u, length, file) == length;
    return fclose(file) == 0 && valid;
}

static int exact_bytes(const cdx_path_char *path, const unsigned char *bytes, size_t length) {
    unsigned char *actual = (unsigned char *)malloc(length + 1u);
    FILE *file = open_file(path, 0);
    int valid = actual && file && fread(actual, 1u, length + 1u, file) == length && !memcmp(actual, bytes, length);
    if (file) fclose(file);
    free(actual); return valid;
}

static int exists(const cdx_path_char *path) {
#ifdef _WIN32
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat value; return !lstat(path, &value);
#endif
}

static void remove_file(const cdx_path_char *path) {
#ifdef _WIN32
    (void)DeleteFileW(path);
#else
    (void)unlink(path);
#endif
}

static int remove_directory(const cdx_path_char *path) {
#ifdef _WIN32
    return RemoveDirectoryW(path) != 0;
#else
    return rmdir(path) == 0;
#endif
}

#ifdef _WIN32
typedef WIN32_FILE_ATTRIBUTE_DATA fixture_metadata;
#else
typedef struct stat fixture_metadata;
#endif

static int read_metadata(const cdx_path_char *path, fixture_metadata *metadata) {
#ifdef _WIN32
    return GetFileAttributesExW(path, GetFileExInfoStandard, metadata) != 0;
#else
    return stat(path, metadata) == 0;
#endif
}

static int exact_metadata(const fixture_metadata *before, const fixture_metadata *after) {
#ifdef _WIN32
    return CompareFileTime(&before->ftCreationTime, &after->ftCreationTime) == 0 &&
           CompareFileTime(&before->ftLastAccessTime, &after->ftLastAccessTime) == 0 &&
           CompareFileTime(&before->ftLastWriteTime, &after->ftLastWriteTime) == 0;
#else
    return (before->st_mode & 0777u) == (after->st_mode & 0777u) &&
           before->st_atim.tv_sec == after->st_atim.tv_sec && before->st_atim.tv_nsec == after->st_atim.tv_nsec &&
           before->st_mtim.tv_sec == after->st_mtim.tv_sec && before->st_mtim.tv_nsec == after->st_mtim.tv_nsec;
#endif
}

static int require_exact_metadata(const cdx_path_char *path) {
#ifdef __linux__
    struct statfs filesystem;
    /* ext4 is the positive Linux fixture; drvfs exposes timestamp rounding separately. */
    return statfs(path, &filesystem) == 0 && filesystem.f_type == 0xef53;
#else
    (void)path; return 1;
#endif
}

static void hash_vectors(const cdx_path_char *root, cdx_file_request *request) {
    static const struct { size_t length; const char *sha256; } boundaries[] = {
        {55u, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56u, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63u, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64u, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65u, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {65537u, "008ffc88d3c96a9f307524eb361e47c5222a887fc45fa0c1fb8d429c5c23b430"}
    };
    cdx_path_char path[2048];
    cdx_file_result result;
    unsigned char *million = (unsigned char *)malloc(1000000u);
    if (!join_path(path, root, PATH_TEXT("hash.bin")) || !million) { check(0, "hash fixture allocation"); free(million); return; }
    request->source = path; request->destination = NULL; request->operation = CDX_FILE_HASH;
    check(write_bytes(path, (const unsigned char *)"abc", 3u), "known-vector file created");
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "ok") && !strcmp(result.sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") &&
          result.bytes_processed == 3u && !result.written_to_disk && !result.source_removed,
          "SHA256 abc exact known vector");
    check(write_bytes(path, (const unsigned char *)"", 0), "empty binary file created");
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "ok") && !strcmp(result.sha256, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") &&
          !result.bytes_processed, "SHA256 empty known vector");
    memset(million, 'a', 1000000u);
    for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i) {
        int written = write_bytes(path, million, boundaries[i].length);
        cdx_file_execute(request, &result);
        if (!written || strcmp(result.status, "ok") || strcmp(result.sha256, boundaries[i].sha256))
            printf("SHA256 boundary length=%zu status=%s digest=%s\n", boundaries[i].length, result.status, result.sha256);
        check(written && !strcmp(result.status, "ok") && result.bytes_processed == boundaries[i].length &&
              !strcmp(result.sha256, boundaries[i].sha256), "SHA256 padding/chunk boundary matches independent crypto vector");
    }
    check(write_bytes(path, million, 1000000u), "million-byte file created");
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "ok") && result.bytes_processed == 1000000u &&
          !strcmp(result.sha256, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
          "SHA256 million a crosses streaming chunk boundaries");
    request->max_bytes = 999999u;
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "too_large") && !result.bytes_processed && !*result.sha256,
          "hash input limit rejects before streaming");
    request->max_bytes = CDX_FILE_MAX_BYTES;
    remove_file(path); free(million);
}

static void protocol_cases(const cdx_path_char *path) {
    cdx_json_output input, output;
    cdx_json json, parsed;
    char *utf8 = NULL;
    int valid = 0, status, digest;
#ifdef _WIN32
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, NULL, 0, NULL, NULL);
    if (size > 0) utf8 = (char *)malloc((size_t)size);
    if (utf8) (void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, utf8, size, NULL, NULL);
#else
    utf8 = strdup(path);
#endif
    cdx_json_output_init(&input); cdx_json_output_init(&output);
    cdx_json_raw(&input, "{\"path\":");
    if (utf8) cdx_json_quote(&input, utf8, strlen(utf8)); else cdx_json_raw(&input, "null");
    cdx_json_raw(&input, "}");
    if (cdx_json_parse(&json, input.data, input.size)) {
        cdx_file_ops_call("hash_file", &json, 0, NULL, &output);
        if (cdx_json_parse(&parsed, output.data, output.size)) {
            status = cdx_json_get(&parsed, 0, "status"); digest = cdx_json_get(&parsed, 0, "sha256");
            valid = status >= 0 && digest >= 0 && parsed.nodes[status].type == CDX_JSON_STRING &&
                    !strcmp(parsed.nodes[status].string, "ok") && parsed.nodes[digest].type == CDX_JSON_STRING &&
                    strlen(parsed.nodes[digest].string) == 64u;
            cdx_json_free(&parsed);
        }
        cdx_json_free(&json);
    }
    check(valid, "protocol hash result is structured JSON with a digest");
    cdx_json_output_free(&input); cdx_json_output_free(&output); free(utf8);
    cdx_json_output_init(&output);
    if (cdx_json_parse(&json, "{\"path\":\"relative\",\"max_bytes\":0}", strlen("{\"path\":\"relative\",\"max_bytes\":0}"))) {
        cdx_file_ops_call("hash_file", &json, 0, NULL, &output);
        check(strstr(output.data, "validation_error") != NULL, "protocol rejects zero byte budget");
        cdx_json_free(&json);
    } else check(0, "protocol validation request parsed");
    cdx_json_output_free(&output);
    cdx_json_output_init(&input);
    cdx_json_raw(&input, "["); cdx_json_raw(&input, cdx_file_ops_tool_schema()); cdx_json_raw(&input, "]");
    valid = cdx_json_parse(&json, input.data, input.size);
    if (valid) {
        unsigned count = 0;
        for (int node = json.nodes[0].first; node >= 0 && count < 4u; node = json.nodes[node].next) ++count;
        check(count == 3u, "all three native file tool schemas parse"); cdx_json_free(&json);
    } else check(0, "file tool schemas JSON valid");
    cdx_json_output_free(&input);
}

static void file_cases(const cdx_path_char *root, cdx_file_request *request) {
    cdx_path_char source[2048], destination[2048], moved[2048], invalid[2048];
    unsigned char bytes[70001];
    cdx_file_result result;
    cdx_fs_request listing = {0};
    cdx_fs_result entries;
    fixture_metadata expected_metadata, actual_metadata;
    int clean = 1, metadata_captured, metadata_exact;
#ifdef _WIN32
    HANDLE cancellation = CreateEventW(NULL, TRUE, TRUE, NULL);
#else
    atomic_int signal; cdx_cancel_handle cancellation = &signal;
    atomic_init(&signal, 1);
#endif
    if (!join_path(source, root, PATH_TEXT("\u6e90's binary.bin")) || !join_path(destination, root, PATH_TEXT("copy.bin")) ||
        !join_path(moved, root, PATH_TEXT("moved.bin"))) { check(0, "file fixture paths"); goto done; }
    for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = (unsigned char)(i % 256u);
    check(write_bytes(source, bytes, sizeof(bytes)), "binary Unicode apostrophe source fixture created");
    metadata_captured = read_metadata(source, &expected_metadata);
    request->operation = CDX_FILE_COPY; request->source = source; request->destination = destination;
    cdx_file_execute(request, &result);
    metadata_exact = metadata_captured && read_metadata(destination, &actual_metadata) &&
                     exact_metadata(&expected_metadata, &actual_metadata);
    if (strcmp(result.status, "ok")) printf("copy status=%s native_error=%lu\n", result.status, result.native_error);
    check(!strcmp(result.status, "ok") && result.written_to_disk && !result.source_removed && result.cleanup_complete &&
          result.bytes_processed == sizeof(bytes) && exact_bytes(destination, bytes, sizeof(bytes)) && exact_bytes(source, bytes, sizeof(bytes)),
          "copy publishes exact binary bytes and retains Unicode source");
    check(result.metadata_checked && result.metadata_preserved == metadata_exact,
          "copy metadata flags match native exact readback before content reads");
    if (require_exact_metadata(root)) check(metadata_exact && result.metadata_preserved,
          "native positive fixture preserves all documented metadata exactly");
    else if (!metadata_exact) {
        printf("CAPABILITY copy_file metadata limited: exact timestamps/permissions were not preserved\n");
        check(result.metadata_checked && !result.metadata_preserved,
              "filesystem metadata limitation is explicit despite successful byte copy");
    }
    protocol_cases(destination);
    request->destination = source;
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "already_exists") && !result.written_to_disk && exact_bytes(source, bytes, sizeof(bytes)),
          "copy to its own source is a collision without mutation");
    request->operation = CDX_FILE_MOVE;
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "already_exists") && !result.source_removed && exact_bytes(source, bytes, sizeof(bytes)),
          "move to its own source is a collision without removal");
    request->operation = CDX_FILE_COPY; request->destination = destination;
    check(write_bytes(destination, (const unsigned char *)"keep", 4u), "copy collision sentinel created");
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "already_exists") && !result.written_to_disk && result.cleanup_complete &&
          !result.metadata_checked && !result.metadata_preserved &&
          exact_bytes(destination, (const unsigned char *)"keep", 4u), "copy collision never replaces destination or claims metadata readback");
    request->operation = CDX_FILE_MOVE;
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "already_exists") && !result.written_to_disk && !result.source_removed &&
          exact_bytes(source, bytes, sizeof(bytes)), "move collision retains source and destination");
    request->destination = moved;
    cdx_file_execute(request, &result);
    if (!strcmp(result.status, "not_supported")) {
        printf("CAPABILITY move_file not_supported: filesystem lacks atomic no-replace rename\n");
        check(!result.written_to_disk && !result.source_removed && exact_bytes(source, bytes, sizeof(bytes)) && !exists(moved),
              "unsupported atomic move retains source and absent destination");
        check(write_bytes(moved, bytes, sizeof(bytes)), "independent post-boundary fixture prepared");
        remove_file(source);
    } else check(!strcmp(result.status, "ok") && result.written_to_disk && result.source_removed && !result.metadata_checked && !exists(source) &&
                exact_bytes(moved, bytes, sizeof(bytes)), "move atomically renames the regular file");
    request->source = moved; request->operation = CDX_FILE_COPY; request->destination = source;
    request->max_bytes = sizeof(bytes) - 1u;
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "too_large") && !result.written_to_disk && !exists(source) && result.cleanup_complete,
          "copy input budget leaves destination absent");
    request->max_bytes = CDX_FILE_MAX_BYTES; request->cancellation = cancellation;
    cdx_file_execute(request, &result);
    check(cancellation && !strcmp(result.status, "cancelled") && !result.written_to_disk && !exists(source),
          "pre-cancelled copy creates no destination");
    request->operation = CDX_FILE_MOVE;
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "cancelled") && !result.source_removed && exists(moved) && !exists(source),
          "pre-cancelled move preserves source");
    request->cancellation = NULL; request->operation = CDX_FILE_HASH; request->destination = NULL;
    request->source = root;
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "not_regular_file") || !strcmp(result.status, "permission_denied"),
          "hash rejects directories");
    request->source = PATH_TEXT("relative");
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "validation_error"), "relative path rejected at native boundary");
    (void)join_path(invalid, root, PATH_TEXT("$(literal)")); request->source = invalid;
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "validation_error"), "code-shaped literal path rejected without execution");
    (void)join_path(invalid, root, PATH_TEXT("bad\nname")); request->source = invalid;
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "validation_error"), "newline path rejected");
#ifndef _WIN32
    (void)join_path(invalid, root, PATH_TEXT("missing/../moved.bin")); request->source = invalid;
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "not_found"), "native missing-parent traversal retained after validation");
    (void)join_path(invalid, root, PATH_TEXT("source-link"));
    check(!symlink(moved, invalid), "real symlink fixture created"); request->source = invalid;
    cdx_file_execute(request, &result);
    check(!strcmp(result.status, "not_regular_file") && !*result.sha256, "hash rejects symlink source");
    remove_file(invalid);
#endif
    listing.path = root; listing.list_directory = 1; listing.include_hidden = 1; listing.max_entries = 100u;
    cdx_fs_query(&listing, &entries);
    if (entries.status != CDX_FS_OK) clean = 0;
    for (size_t i = 0; i < entries.entry_count; ++i) if (strstr(entries.entries[i].name, ".cdx-copy-")) clean = 0;
    check(clean, "success failure collision and cancellation leave no temporary artifacts");
    cdx_fs_result_free(&entries);
    remove_file(source); remove_file(destination); remove_file(moved);
done: ;
#ifdef _WIN32
    if (cancellation) CloseHandle(cancellation);
#endif
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
    cdx_path_char root[2048];
    cdx_file_request request = {0};
    if (argc != 2) return 2;
#ifdef _WIN32
    if (_snwprintf_s(root, 2048u, _TRUNCATE, L"%ls/file-ops-%lu", argv[1], GetCurrentProcessId()) < 0 || !CreateDirectoryW(root, NULL)) return 2;
#else
    if (snprintf(root, sizeof(root), "%s/file-ops-%ld", argv[1], (long)getpid()) >= (int)sizeof(root) || mkdir(root, 0700)) return 2;
#endif
    request.timeout_ms = 10000u; request.max_bytes = CDX_FILE_MAX_BYTES;
    hash_vectors(root, &request); file_cases(root, &request);
    check(remove_directory(root), "all owned file fixtures cleaned");
    printf("checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
