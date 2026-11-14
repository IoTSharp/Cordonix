#define _POSIX_C_SOURCE 200809L
#define _CRT_SECURE_NO_WARNINGS
#include "cdx/document.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

static int checks, failures;

static void check(int condition, const char *name) {
    ++checks;
    if (!condition) ++failures;
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
}

static FILE *fixture_file(const cdx_path_char *path, int writing) {
#ifdef _WIN32
    return _wfopen(path, writing ? L"wb" : L"rb");
#else
    return fopen(path, writing ? "wb" : "rb");
#endif
}

static int write_bytes(const cdx_path_char *path, const void *bytes, size_t length) {
    FILE *file = fixture_file(path, 1);
    int ok;
    if (!file) return 0;
    ok = fwrite(bytes, 1, length, file) == length;
    return fclose(file) == 0 && ok;
}

static int disk_equals(const cdx_path_char *path, const void *bytes, size_t length) {
    unsigned char buffer[256];
    FILE *file = fixture_file(path, 0);
    size_t count;
    if (!file) return 0;
    count = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);
    return count == length && !memcmp(buffer, bytes, length);
}

static int open_fixture(cdx_document *document, const cdx_path_char *path,
                        const void *bytes, size_t length, cdx_encoding encoding) {
    cdx_document_status status;
    memset(document, 0, sizeof(*document));
    if (!write_bytes(path, bytes, length)) { check(0, "fixture write"); return 0; }
    status = cdx_document_open(document, path, encoding);
    check(status == CDX_STATUS_OK, "fixture opens with explicit encoding");
    return status == CDX_STATUS_OK;
}

static void ordered_case(const cdx_path_char *path, cdx_edit_batch_result *result) {
    cdx_document document;
    cdx_commit_result commit;
    char original_hash[65];
    const cdx_document_edit edits[] = {{"alpha", "beta", 1}, {"beta", "gamma", 2}};
    if (!open_fixture(&document, path, "alpha beta\n", 11, CDX_ENCODING_UTF8)) return;
    memcpy(original_hash, document.current_sha256, sizeof(original_hash));
    check(cdx_document_edit_batch_utf8(&document, original_hash, edits, 2, 1, NULL, 15000, result) == CDX_STATUS_OK &&
          result->status == CDX_STATUS_PREVIEW && result->completed_edits == 2 && result->failed_edit_index == SIZE_MAX,
          "ordered dry run sees earlier replacements");
    check(!document.dirty_in_memory && !strcmp(document.current_sha256, original_hash) && !strcmp(document.text_utf8, "alpha beta\n") &&
          result->bytes_before == 11 && result->bytes_after == 12 && !result->written_to_disk,
          "preview preserves memory SHA bytes and dirty flag");
    check(!strcmp(result->before, "alpha bet") && !strcmp(result->after, "gamma gamm") && result->prefix_bytes == 0 &&
          result->removed_bytes == 9 && result->added_bytes == 10 && !result->truncated,
          "preview reports exact common-prefix suffix changed span");
    check(cdx_document_edit_batch_utf8(&document, original_hash, edits, 2, 0, NULL, 15000, result) == CDX_STATUS_OK &&
          result->dirty_in_memory && !strcmp(document.text_utf8, "gamma gamma\n") && disk_equals(path, "alpha beta\n", 11),
          "applied batch changes only memory");
    check(!strcmp(document.current_sha256, result->preview_sha256) && result->edit_results[0].actual_count == 1 &&
          result->edit_results[1].actual_count == 2, "batch returns final hash and actual counts");
    check(cdx_document_commit(&document, &commit) == CDX_STATUS_OK && commit.written_to_disk &&
          disk_equals(path, "gamma gamma\n", 12), "explicit commit writes combined batch once");
    cdx_document_free(&document);
}

static void rollback_case(const cdx_path_char *path, cdx_edit_batch_result *result) {
    cdx_document document;
    cdx_edit_result single;
    char hash[65];
    cdx_document_edit edits[] = {{"old", "new", 1}, {"missing", "x", 1}};
    if (!open_fixture(&document, path, "old\n", 4, CDX_ENCODING_UTF8)) return;
    memcpy(hash, document.current_sha256, sizeof(hash));
    check(cdx_document_edit_batch_utf8(&document, hash, edits, 2, 0, NULL, 15000, result) == CDX_STATUS_ANCHOR_MISMATCH &&
          result->completed_edits == 1 && result->failed_edit_index == 1, "late missing anchor reports failing zero-based index");
    check(!strcmp(document.current_sha256, hash) && !strcmp(document.text_utf8, "old\n") && !document.dirty_in_memory &&
          !result->dirty_in_memory && result->bytes_after == 4 && disk_equals(path, "old\n", 4), "late failure rolls back entire batch");
    edits[0].new_text = "new new"; edits[1].old_text = "new";
    check(cdx_document_edit_batch_utf8(&document, hash, edits, 2, 0, NULL, 15000, result) == CDX_STATUS_AMBIGUOUS_MATCH &&
          result->edit_results[1].actual_count == 2 && !document.dirty_in_memory, "ambiguous later edit rolls back earlier edit");
    check(cdx_document_edit_utf8(&document, hash, "old", "dirty", 1, 0, &single) == CDX_STATUS_OK, "prepare existing dirty document");
    memcpy(hash, document.current_sha256, sizeof(hash));
    edits[0].old_text = "dirty"; edits[0].new_text = "new"; edits[1].old_text = "absent";
    check(cdx_document_edit_batch_utf8(&document, hash, edits, 2, 0, NULL, 15000, result) == CDX_STATUS_ANCHOR_MISMATCH &&
          document.dirty_in_memory && result->dirty_in_memory && !strcmp(document.current_sha256, hash) &&
          !strcmp(document.text_utf8, "dirty\n"), "failure retains preexisting dirty memory and SHA");
    check(cdx_document_edit_batch_utf8(&document, "stale", edits, 2, 0, NULL, 15000, result) == CDX_STATUS_FILE_CHANGED &&
          !result->completed_edits && result->failed_edit_index == SIZE_MAX, "initial stale SHA rejects entire batch");
    edits[1].old_text = "";
    check(cdx_document_edit_batch_utf8(&document, hash, edits, 2, 0, NULL, 15000, result) == CDX_STATUS_VALIDATION_ERROR &&
          !result->completed_edits && result->failed_edit_index == 1 && !strcmp(document.text_utf8, "dirty\n"),
          "all edit arguments validated before first replacement");
    check(cdx_document_edit_batch_utf8(&document, hash, edits, 0, 0, NULL, 15000, result) == CDX_STATUS_VALIDATION_ERROR &&
          cdx_document_edit_batch_utf8(&document, hash, edits, 33, 0, NULL, 15000, result) == CDX_STATUS_VALIDATION_ERROR,
          "zero and oversized edit batches rejected");
    cdx_document_free(&document);
}

static void encoding_case(const cdx_path_char *path, cdx_edit_batch_result *result) {
    static const unsigned char bom[] = {0xef,0xbb,0xbf,'a','\r','\n'};
    static const unsigned char bom_changed[] = {0xef,0xbb,0xbf,'c','\r','\n'};
    static const unsigned char cp936[] = {0xd6,0xd0,'\r','\n'};
    static const unsigned char cp936_changed[] = {0xd6,0xd0,'!','\r','\n'};
    cdx_document document;
    cdx_commit_result commit;
    cdx_document_edit edits[] = {{"a", "b", 1}, {"b", "c", 1}};
    char hash[65];
    if (open_fixture(&document, path, bom, sizeof(bom), CDX_ENCODING_AUTO)) {
        check(cdx_document_edit_batch_utf8(&document, document.current_sha256, edits, 2, 0, NULL, 15000, result) == CDX_STATUS_OK &&
              document.encoding == CDX_ENCODING_UTF8_BOM && document.has_bom && document.eol == CDX_EOL_CRLF &&
              cdx_document_commit(&document, &commit) == CDX_STATUS_OK && disk_equals(path, bom_changed, sizeof(bom_changed)),
              "BOM CRLF preserved through batch and commit");
        cdx_document_free(&document);
    }
    if (open_fixture(&document, path, cp936, sizeof(cp936), CDX_ENCODING_CP936)) {
        memcpy(hash, document.current_sha256, sizeof(hash));
        edits[0].old_text = "\r\n"; edits[0].new_text = "!\r\n";
        edits[1].old_text = "!"; edits[1].new_text = "\xf0\x9f\x99\x82";
        check(cdx_document_edit_batch_utf8(&document, hash, edits, 2, 0, NULL, 15000, result) == CDX_STATUS_ENCODING_ERROR &&
              result->failed_edit_index == 1 && !document.dirty_in_memory && !strcmp(hash, document.current_sha256) &&
              !memcmp(document.current_bytes, cp936, sizeof(cp936)), "unrepresentable later CP936 edit rolls back encoded bytes");
        check(cdx_document_edit_batch_utf8(&document, hash, edits, 1, 0, NULL, 15000, result) == CDX_STATUS_OK &&
              cdx_document_commit(&document, &commit) == CDX_STATUS_OK && disk_equals(path, cp936_changed, sizeof(cp936_changed)),
              "representable CP936 batch commits exact legacy bytes");
        cdx_document_free(&document);
    }
    if (open_fixture(&document, path, "a\r\nb\n", 5, CDX_ENCODING_UTF8)) {
        edits[0].old_text = "a"; edits[0].new_text = "x";
        check(cdx_document_edit_batch_utf8(&document, document.current_sha256, edits, 1, 0, NULL, 15000, result) == CDX_STATUS_MIXED_EOL &&
              !document.dirty_in_memory && disk_equals(path, "a\r\nb\n", 5), "inherited mixed EOL boundary rejects batch");
        cdx_document_free(&document);
    }
    if (open_fixture(&document, path, "a\r\nb\r\n", 6, CDX_ENCODING_UTF8)) {
        edits[1].old_text = "b\r\n"; edits[1].new_text = "b\n";
        check(cdx_document_edit_batch_utf8(&document, document.current_sha256, edits, 2, 0, NULL, 15000, result) == CDX_STATUS_MIXED_EOL &&
              !strcmp(document.text_utf8, "a\r\nb\r\n") && !document.dirty_in_memory,
              "later EOL inversion rolls back earlier replacement");
        cdx_document_free(&document);
    }
    if (open_fixture(&document, path, "old\n", 4, CDX_ENCODING_UTF8)) {
        edits[0].old_text = "\n"; edits[0].new_text = "\r";
        check(cdx_document_edit_batch_utf8(&document, document.current_sha256, edits, 1, 0, NULL, 15000, result) == CDX_STATUS_MIXED_EOL &&
              result->edit_results[0].status == CDX_STATUS_MIXED_EOL && !result->completed_edits &&
              document.eol == CDX_EOL_LF && !document.dirty_in_memory && !strcmp(document.text_utf8, "old\n"),
              "resulting lone CR rejected with batch rollback");
        cdx_document_free(&document);
    }
}

static void preview_case(const cdx_path_char *path, cdx_edit_batch_result *result) {
    cdx_document document;
    cdx_document_edit edit = {"\xe4\xb8\xad", "\xe4\xb8\xb0", 1};
    char *large = NULL;
    size_t i;
    if (open_fixture(&document, path, "\xe4\xb8\xad" "A\xe6\x96\x87", 7, CDX_ENCODING_UTF8)) {
        check(cdx_document_edit_batch_utf8(&document, document.current_sha256, &edit, 1, 1, NULL, 15000, result) == CDX_STATUS_OK &&
              result->prefix_bytes == 0 && result->removed_bytes == 3 && result->added_bytes == 3 &&
              !strcmp(result->before, "\xe4\xb8\xad") && !strcmp(result->after, "\xe4\xb8\xb0"),
              "preview prefix and suffix preserve UTF8 character boundaries");
        edit.new_text = "\xe5\xb8\xad";
        check(cdx_document_edit_batch_utf8(&document, document.current_sha256, &edit, 1, 1, NULL, 15000, result) == CDX_STATUS_OK &&
              result->removed_bytes == 3 && result->added_bytes == 3 && !strcmp(result->after, "\xe5\xb8\xad"),
              "common suffix inside multibyte characters expands to complete code points");
        edit.new_text = edit.old_text;
        check(cdx_document_edit_batch_utf8(&document, document.current_sha256, &edit, 1, 1, NULL, 15000, result) == CDX_STATUS_OK &&
              !result->removed_bytes && !result->added_bytes && !result->before_length && !result->after_length && !result->truncated,
              "no-op preview has empty changed span");
        cdx_document_free(&document);
    }
    large = (char *)malloc(20001);
    check(large != NULL, "allocate bounded preview fixture");
    if (!large) return;
    for (i = 0; i < 5000; ++i) memcpy(large + i * 4, "\xf0\x9f\x99\x82", 4);
    large[20000] = '\0';
    if (open_fixture(&document, path, "old", 3, CDX_ENCODING_UTF8)) {
        edit.old_text = "old"; edit.new_text = large;
        check(cdx_document_edit_batch_utf8(&document, document.current_sha256, &edit, 1, 1, NULL, 15000, result) == CDX_STATUS_OK &&
              result->truncated && result->after_length == 8192 && result->added_bytes == 20000 && result->before_length == 3 &&
              strlen(result->after) == result->after_length && result->after[8191] == '\x82', "large preview bounded at complete UTF8 code point");
        cdx_document_free(&document);
    }
    free(large);
}

static void interruption_conflict_case(const cdx_path_char *path, cdx_edit_batch_result *result) {
    cdx_document document;
    cdx_commit_result commit;
    const cdx_document_edit edit = {"old", "new", 1};
    cdx_cancel_handle cancel_event;
#ifdef _WIN32
    cancel_event = CreateEventW(NULL, TRUE, TRUE, NULL);
#else
    atomic_int cancelled = 1;
    cancel_event = &cancelled;
#endif
    check(cancel_event != NULL, "create native cancellation fixture");
    if (open_fixture(&document, path, "old\n", 4, CDX_ENCODING_UTF8)) {
        check(cdx_document_edit_batch_utf8(&document, document.current_sha256, &edit, 1, 0, cancel_event, 15000, result) == CDX_STATUS_CANCELLED &&
              !document.dirty_in_memory && !result->completed_edits && disk_equals(path, "old\n", 4), "pre-cancelled batch leaves memory and disk intact");
        check(cdx_document_edit_batch_utf8(&document, document.current_sha256, &edit, 1, 0, NULL, 0, result) == CDX_STATUS_VALIDATION_ERROR,
              "zero deadline rejected");
        check(cdx_document_edit_batch_utf8(&document, document.current_sha256, &edit, 1, 0, NULL, 15000, result) == CDX_STATUS_OK &&
              write_bytes(path, "external\n", 9) && cdx_document_commit(&document, &commit) == CDX_STATUS_FILE_CHANGED &&
              !commit.written_to_disk && document.dirty_in_memory && disk_equals(path, "external\n", 9),
              "external disk writer still blocks explicit batch commit");
        cdx_document_free(&document);
    }
#ifdef _WIN32
    if (cancel_event) CloseHandle(cancel_event);
#endif
}

static void timeout_case(const cdx_path_char *path, cdx_edit_batch_result *result) {
    const size_t length = 2u * 1024u * 1024u;
    char *large = (char *)malloc(length);
    char *anchor = (char *)malloc(length / 2u + 1u);
    cdx_document document;
    cdx_document_edit edit = {NULL, "new", 1};
    char hash[65];
    check(large != NULL && anchor != NULL, "allocate bounded timeout fixture");
    if (!large || !anchor) { free(large); free(anchor); return; }
    memset(large, 'x', length);
    memset(anchor, 'x', length / 2u);
    anchor[length / 2u - 1u] = 'z';
    anchor[length / 2u] = '\0';
    edit.old_text = anchor;
    if (open_fixture(&document, path, large, length, CDX_ENCODING_UTF8)) {
        memcpy(hash, document.current_sha256, sizeof(hash));
        check(cdx_document_edit_batch_utf8(&document, hash, &edit, 1, 0, NULL, 1, result) == CDX_STATUS_TIMED_OUT &&
              !document.dirty_in_memory && !strcmp(document.current_sha256, hash) && document.current_size == length &&
              !result->completed_edits, "shared one-millisecond deadline interrupts scan without memory change");
        cdx_document_free(&document);
    }
    free(large); free(anchor);
}

int main(int argc, char **argv) {
    cdx_path_char path[1024];
    cdx_edit_batch_result *result = (cdx_edit_batch_result *)malloc(sizeof(*result));
    int pilot = argc > 2 && !strcmp(argv[2], "--pilot");
    if (!result) { fprintf(stderr, "Batch result allocation failed\n"); return 1; }
#ifdef _WIN32
    {
        wchar_t root[512], input[512];
        DWORD length;
        int converted = argc > 1 ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, input, 512) : 0;
        if (argc > 1 && !converted) { free(result); return 1; }
        if (argc <= 1) {
            CreateDirectoryW(L"build", NULL);
            wcscpy_s(input, 512, L"build/workspace");
        }
        length = GetFullPathNameW(input, 512, root, NULL);
        if (!length || length >= 512) { free(result); return 1; }
        CreateDirectoryW(root, NULL);
        _snwprintf_s(path, sizeof(path) / sizeof(path[0]), _TRUNCATE, L"%ls/edit-batch-%lu.tmp", root, GetCurrentProcessId());
    }
#else
    if (argc > 1) {
        mkdir(argv[1], 0700);
        snprintf(path, sizeof(path), "%s/edit-batch-%ld.tmp", argv[1], (long)getpid());
    } else {
        char root[512];
        mkdir("build", 0700);
        mkdir("build/workspace", 0700);
        if (!getcwd(root, sizeof(root))) { free(result); return 1; }
        snprintf(path, sizeof(path), "%s/build/workspace/edit-batch-%ld.tmp", root, (long)getpid());
    }
#endif
    ordered_case(path, result);
    if (!pilot) {
        rollback_case(path, result);
        encoding_case(path, result);
        preview_case(path, result);
        interruption_conflict_case(path, result);
        timeout_case(path, result);
    }
#ifdef _WIN32
    check(DeleteFileW(path) != 0, "remove owned batch fixture");
#else
    check(unlink(path) == 0, "remove owned batch fixture");
#endif
    free(result);
    printf("%d/%d edit batch checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
