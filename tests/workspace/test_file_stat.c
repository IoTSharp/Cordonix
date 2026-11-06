#define _POSIX_C_SOURCE 200809L
#include "cdx/file_stat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <wchar.h>
#define PATH_TEXT(value) L##value
#else
#include <fcntl.h>
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
    return _snwprintf_s(path, 2048u, _TRUNCATE, L"%ls/%ls", root, name) >= 0;
#else
    int length = snprintf(path, 2048u, "%s/%s", root, name);
    return length >= 0 && length < 2048;
#endif
}

static int write_fixture(const cdx_path_char *path) {
#ifdef _WIN32
    FILE *file = NULL;
    if (_wfopen_s(&file, path, L"wb") || !file) return 0;
#else
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
#endif
    if (fwrite("stat fixture\n", 1u, 13u, file) != 13u) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static int remove_file(const cdx_path_char *path) {
#ifdef _WIN32
    return DeleteFileW(path) != 0;
#else
    return unlink(path) == 0;
#endif
}

static int remove_directory(const cdx_path_char *path) {
#ifdef _WIN32
    return RemoveDirectoryW(path) != 0;
#else
    return rmdir(path) == 0;
#endif
}

static void stat_cases(const cdx_path_char *root) {
    cdx_path_char file_path[2048], dir_path[2048], missing_path[2048];
    cdx_stat_request request = {0};
    cdx_stat_result result;
    cdx_path_char link_path[2048];
    if (!join_path(file_path, root, PATH_TEXT(".含 空格 fixture.txt")) ||
        !join_path(dir_path, root, PATH_TEXT("directory")) ||
        !join_path(missing_path, root, PATH_TEXT("missing.txt"))) {
        check(0, "stat fixture paths fit the bounded path buffer");
        return;
    }
    check(write_fixture(file_path), "regular stat fixture created");
#ifdef _WIN32
    check(CreateDirectoryW(dir_path, NULL) != 0, "directory stat fixture created");
#else
    check(mkdir(dir_path, 0700) == 0, "directory stat fixture created");
#endif
    request.path = file_path;
    request.timeout_ms = 5000u;
    check(cdx_stat_query(&request, &result) == CDX_FS_OK && result.status == CDX_FS_OK &&
          result.kind == CDX_FS_FILE && result.size_known && result.size_bytes == 13u &&
          result.path != NULL && result.time_precision_known && result.modification_time_known,
          "regular file reports kind size path and modification time");
    cdx_stat_result_free(&result);
#ifdef _WIN32
    check(SetFileAttributesW(file_path, FILE_ATTRIBUTE_HIDDEN) != 0, "hidden attribute fixture set");
#endif
    check(cdx_stat_query(&request, &result) == CDX_FS_OK && result.hidden_known && result.hidden,
          "Unicode space path preserves hidden metadata");
    cdx_stat_result_free(&result);
#ifndef _WIN32
    {
        const struct timespec times[2] = {{10000000000LL, 123}, {10000000000LL, 456}};
        struct stat observed;
        check(utimensat(AT_FDCWD, file_path, times, 0) == 0 && lstat(file_path, &observed) == 0,
              "real file exposes an extended-range timestamp");
        check(cdx_stat_query(&request, &result) == CDX_FS_OK &&
              (observed.st_mtim.tv_sec > INT64_MAX / 1000000000LL
               ? !result.modification_time_known
               : result.modification_time_known && result.modification_time_ns ==
                 (int64_t)observed.st_mtim.tv_sec * 1000000000LL + observed.st_mtim.tv_nsec),
              "timestamp outside signed nanosecond range remains unavailable");
        cdx_stat_result_free(&result);
    }
#endif
    request.path = dir_path;
    check(cdx_stat_query(&request, &result) == CDX_FS_OK && result.status == CDX_FS_OK &&
          result.kind == CDX_FS_DIRECTORY && !result.size_known,
          "directory reports kind with size marked unavailable");
    cdx_stat_result_free(&result);
#ifndef _WIN32
    if (join_path(link_path, root, PATH_TEXT("fixture-link")) && !symlink(file_path, link_path)) {
        request.path = link_path;
        check(cdx_stat_query(&request, &result) == CDX_FS_OK && result.status == CDX_FS_OK &&
              result.kind == CDX_FS_LINK && !result.size_known && result.modification_time_known,
              "POSIX symlink is reported lexically with target-independent metadata");
        cdx_stat_result_free(&result);
        check(remove_file(link_path), "symlink fixture removed");
    } else check(0, "POSIX symlink fixture created");
#else
    if (join_path(link_path, root, PATH_TEXT("fixture-link")) &&
        CreateSymbolicLinkW(link_path, file_path, 2u)) {
        request.path = link_path;
        check(cdx_stat_query(&request, &result) == CDX_FS_OK && result.kind == CDX_FS_LINK &&
              !result.size_known && !result.modification_time_known,
              "Windows link reports its entry and omits target timestamps");
        cdx_stat_result_free(&result);
        check(remove_file(link_path), "Windows symlink fixture removed");
    } else if (GetLastError() == ERROR_PRIVILEGE_NOT_HELD || GetLastError() == ERROR_INVALID_PARAMETER) {
        printf("Windows stat link NOT_RUN: symlink privilege or developer mode unavailable\n");
    } else check(0, "Windows symlink fixture created");
#endif
    request.path = missing_path;
    check(cdx_stat_query(&request, &result) == CDX_FS_NOT_FOUND && result.status == CDX_FS_NOT_FOUND &&
          result.path != NULL,
          "missing path reports not_found with normalized path context");
    cdx_stat_result_free(&result);
    request.path = PATH_TEXT("relative");
    check(cdx_stat_query(&request, &result) == CDX_FS_VALIDATION_ERROR && result.status == CDX_FS_VALIDATION_ERROR,
          "relative path is rejected before filesystem access");
    request.path = file_path;
    request.timeout_ms = 0;
    check(cdx_stat_query(&request, &result) == CDX_FS_VALIDATION_ERROR,
          "zero timeout is rejected at the public boundary");
    request.timeout_ms = 5000u;
#ifdef _WIN32
    request.cancellation = CreateEventW(NULL, TRUE, TRUE, NULL);
    check(request.cancellation != NULL && cdx_stat_query(&request, &result) == CDX_FS_CANCELLED,
          "signalled cancellation stops metadata access");
    if (request.cancellation) CloseHandle(request.cancellation);
#else
    {
        atomic_int cancellation = ATOMIC_VAR_INIT(1);
        request.cancellation = &cancellation;
        check(cdx_stat_query(&request, &result) == CDX_FS_CANCELLED,
              "signalled cancellation stops metadata access");
    }
#endif
    cdx_stat_result_free(&result);
    check(remove_file(file_path), "regular stat fixture removed");
    check(remove_directory(dir_path), "directory stat fixture removed");
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    cdx_path_char root[2048];
    if (argc != 2 || _snwprintf_s(root, 2048u, _TRUNCATE, L"%ls/file-stat-%lu", argv[1], GetCurrentProcessId()) < 0 ||
        !CreateDirectoryW(root, NULL)) return 2;
#else
int main(int argc, char **argv) {
    cdx_path_char root[2048];
    if (argc != 2 || snprintf(root, sizeof(root), "%s/file-stat-%ld", argv[1], (long)getpid()) >= (int)sizeof(root) ||
        mkdir(root, 0700)) return 2;
#endif
    stat_cases(root);
    check(remove_directory(root), "all owned stat fixtures cleaned");
    printf("checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
