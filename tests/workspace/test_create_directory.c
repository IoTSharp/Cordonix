#define _POSIX_C_SOURCE 200809L
#include "cdx/filesystem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <wchar.h>
#define PATH_TEXT(value) L##value
#else
#include <sys/stat.h>
#include <unistd.h>
#define PATH_TEXT(value) value
#endif

static unsigned checks, failures;

static void check(int valid, const char *name) {
    ++checks;
    if (!valid) ++failures;
    printf("%s %s\n", valid ? "PASS" : "FAIL", name);
}

static int join_path(cdx_path_char path[2048], const cdx_path_char *root, const cdx_path_char *name) {
#ifdef _WIN32
    return _snwprintf_s(path, 2048, _TRUNCATE, L"%ls/%ls", root, name) >= 0;
#else
    int length = snprintf(path, 2048, "%s/%s", root, name);
    return length >= 0 && length < 2048;
#endif
}

static int directory_exists(const cdx_path_char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat metadata;
    return !stat(path, &metadata) && S_ISDIR(metadata.st_mode);
#endif
}

static int remove_directory(const cdx_path_char *path) {
#ifdef _WIN32
    return RemoveDirectoryW(path) != 0;
#else
    return rmdir(path) == 0;
#endif
}

static void remaining_cases(const cdx_path_char *root, cdx_fs_create_request *request) {
    cdx_path_char parent[2048], middle[2048], leaf[2048], other[2048], file_path[2048], deep[2048];
    cdx_fs_create_result result;
    FILE *file = NULL;
    unsigned i;
    size_t length, bytes_read = 0;
    char bytes[8] = {0};
    time_t deadline = time(NULL) + 15;
#ifdef _WIN32
    HANDLE cancellation = CreateEventW(NULL, TRUE, TRUE, NULL);
#else
    atomic_int cancelled;
    cdx_cancel_handle cancellation = &cancelled;
    atomic_init(&cancelled, 1);
#endif
    if (!join_path(parent, root, PATH_TEXT("parent")) || !join_path(middle, parent, PATH_TEXT("middle")) ||
        !join_path(leaf, middle, PATH_TEXT("leaf")) || !join_path(file_path, root, PATH_TEXT("file.txt"))) {
        check(0, "bounded directory fixture paths"); goto done;
    }
    request->path = leaf;
    request->exist_ok = 0;
    cdx_fs_create_directory(request, &result);
    check(result.status == CDX_FS_NOT_FOUND && !result.created && !result.directories_created && !directory_exists(parent),
          "missing parents are not created by default");
    cdx_fs_create_result_free(&result);
    request->parents = 1;
    cdx_fs_create_directory(request, &result);
    check(result.status == CDX_FS_OK && result.created && result.directories_created == 3 && directory_exists(leaf),
          "parents creates the real three-level hierarchy");
    cdx_fs_create_result_free(&result);
    cdx_fs_create_directory(request, &result);
    check(result.status == CDX_FS_ALREADY_EXISTS && !result.created && !result.directories_created,
          "recursive creation retains strict existing-leaf semantics");
    cdx_fs_create_result_free(&result);
    request->exist_ok = 1;
    cdx_fs_create_directory(request, &result);
    check(result.status == CDX_FS_OK && !result.created && !result.directories_created && directory_exists(leaf),
          "recursive exist_ok reports an unchanged existing directory");
    cdx_fs_create_result_free(&result);
    check(remove_directory(leaf) && remove_directory(middle) && remove_directory(parent), "owned nested directories cleaned");

#ifdef _WIN32
    (void)_wfopen_s(&file, file_path, L"wb");
#else
    file = fopen(file_path, "wb");
#endif
    if (!file) { check(0, "ordinary file fixture created"); goto done; }
    (void)fwrite("keep", 1, 4, file); fclose(file);
    request->path = file_path;
    cdx_fs_create_directory(request, &result);
    check(result.status == CDX_FS_NOT_DIRECTORY && !result.created && !result.directories_created,
          "exist_ok never accepts a regular file");
    cdx_fs_create_result_free(&result);
    (void)join_path(other, file_path, PATH_TEXT("child")); request->path = other;
    cdx_fs_create_directory(request, &result);
    if (result.status != CDX_FS_NOT_DIRECTORY || result.directories_created)
        printf("file ancestor result: status=%s native_error=%lu directories_created=%zu\n",
               cdx_fs_status_name(result.status), result.native_error, result.directories_created);
    check(result.status == CDX_FS_NOT_DIRECTORY && !result.directories_created, "file ancestor is not a directory");
    cdx_fs_create_result_free(&result);
#ifdef _WIN32
    (void)_wfopen_s(&file, file_path, L"rb");
#else
    file = fopen(file_path, "rb");
#endif
    if (file) { bytes_read = fread(bytes, 1, sizeof(bytes), file); fclose(file); }
    check(bytes_read == 4 && !strcmp(bytes, "keep"), "file collision preserves exact original content");
#ifdef _WIN32
    (void)DeleteFileW(file_path);
#else
    (void)unlink(file_path);
#endif

    request->path = parent; request->cancellation = cancellation;
    cdx_fs_create_directory(request, &result);
    check(cancellation && result.status == CDX_FS_CANCELLED && !result.created && !result.directories_created && !directory_exists(parent),
          "pre-cancellation leaves the hierarchy absent");
    cdx_fs_create_result_free(&result); request->cancellation = NULL;
    request->path = PATH_TEXT("relative");
    cdx_fs_create_directory(request, &result);
    check(result.status == CDX_FS_VALIDATION_ERROR && !result.directories_created, "relative paths rejected before creation");
    cdx_fs_create_result_free(&result);
    (void)join_path(other, root, PATH_TEXT("$(literal)")); request->path = other;
    cdx_fs_create_directory(request, &result);
    check(result.status == CDX_FS_VALIDATION_ERROR && !result.directories_created && !directory_exists(other), "code-shaped path rejected");
    cdx_fs_create_result_free(&result);
    (void)join_path(other, root, PATH_TEXT("bad\nname"));
    cdx_fs_create_directory(request, &result);
    check(result.status == CDX_FS_VALIDATION_ERROR && !result.directories_created && !directory_exists(other), "newline path rejected");
    cdx_fs_create_result_free(&result);
    request->path = parent; request->timeout_ms = 0;
    cdx_fs_create_directory(request, &result);
    check(result.status == CDX_FS_VALIDATION_ERROR && !result.directories_created && !directory_exists(parent), "zero deadline rejected");
    cdx_fs_create_result_free(&result); request->timeout_ms = 5000; request->parents = 2;
    cdx_fs_create_directory(request, &result);
    check(result.status == CDX_FS_VALIDATION_ERROR && !result.directories_created && !directory_exists(parent), "non-boolean native option rejected");
    cdx_fs_create_result_free(&result); request->parents = 1;

    (void)join_path(deep, root, PATH_TEXT("depth-limit"));
#ifdef _WIN32
    length = wcslen(deep);
#else
    length = strlen(deep);
#endif
    for (i = 0; i < CDX_FS_MAX_CREATED_DIRECTORIES && length + 2u < 2048 && time(NULL) < deadline; ++i) {
        deep[length++] = '/'; deep[length++] = 'd';
    }
    deep[length] = 0; request->path = deep;
    cdx_fs_create_directory(request, &result);
    (void)join_path(other, root, PATH_TEXT("depth-limit"));
    check(i == CDX_FS_MAX_CREATED_DIRECTORIES && result.status == CDX_FS_VALIDATION_ERROR &&
          !result.created && !result.directories_created && !directory_exists(other), "missing-directory bound checked before writes");
    cdx_fs_create_result_free(&result);
    (void)join_path(other, root, PATH_TEXT("absent/../normalized")); request->path = other;
    cdx_fs_create_directory(request, &result);
    (void)join_path(leaf, root, PATH_TEXT("normalized")); (void)join_path(parent, root, PATH_TEXT("absent"));
    check(result.status == CDX_FS_OK && result.created && result.directories_created == 1 &&
          directory_exists(leaf) && !directory_exists(parent), "dot segments normalized before directory creation");
    cdx_fs_create_result_free(&result); check(remove_directory(leaf), "normalized directory cleaned");
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
    cdx_path_char root[2048], path[2048];
    cdx_fs_create_request request = {0};
    cdx_fs_create_result result;
    int pilot;
    if (argc != 2 && argc != 3) return 2;
#ifdef _WIN32
    pilot = argc == 3 && !wcscmp(argv[2], L"--pilot");
    if (_snwprintf_s(root, 2048, _TRUNCATE, L"%ls/create-directory-%lu", argv[1], (unsigned long)GetCurrentProcessId()) < 0 ||
        !CreateDirectoryW(root, NULL)) return 2;
#else
    pilot = argc == 3 && !strcmp(argv[2], "--pilot");
    if (snprintf(root, sizeof(root), "%s/create-directory-%ld", argv[1], (long)getpid()) >= (int)sizeof(root) || mkdir(root, 0700)) return 2;
#endif
    if (!join_path(path, root, PATH_TEXT("\u4e2d\u6587 space"))) return 2;
    request.path = path; request.timeout_ms = 5000;
    cdx_fs_create_directory(&request, &result);
    check(result.status == CDX_FS_OK && result.created && result.directories_created == 1 && result.path && directory_exists(path),
          "native Unicode spaced directory creation and disk readback");
    cdx_fs_create_result_free(&result);
    cdx_fs_create_directory(&request, &result);
    check(result.status == CDX_FS_ALREADY_EXISTS && !result.created && !result.directories_created && directory_exists(path),
          "existing directory reports already_exists by default");
    cdx_fs_create_result_free(&result); request.exist_ok = 1;
    cdx_fs_create_directory(&request, &result);
    check(result.status == CDX_FS_OK && !result.created && !result.directories_created, "exist_ok accepts a directory without mutation");
    cdx_fs_create_result_free(&result); check(remove_directory(path), "owned single directory cleaned");
    if (!pilot) remaining_cases(root, &request);
    check(remove_directory(root), "all owned directory fixtures cleaned");
    printf("checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
