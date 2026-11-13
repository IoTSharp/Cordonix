#include "cdx/tool_profiles.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static unsigned passed, total;

static void check(const char *name, int valid) {
    ++total;
    if (valid) ++passed;
    (void)printf("%s: %s\n", name, valid ? "PASS" : "FAIL");
}

int main(int argc, char **argv) {
    static const char *const local_tools[] = {
        "create_directory", "create_file", "tool_info", "execute_program",
        "query_processes", "search_text", "find_files", "find_program",
        "git_status", "data_query", "hash_file", "copy_file", "move_file",
        "list_directory", "resolve_path", "stat", "verify_encoding",
        "read_file", "read_files", "workspace_open", "workspace_read",
        "workspace_edit", "workspace_convert_encoding", "workspace_commit",
        "workspace_status", "workspace_search", "workspace_close"
    };
    static const char *const other_tools[] = {
        "ssh", "sftp", "http", "network", "serial", "phone", "screenshot",
        "vnc_screenshot"
    };
    static const char *const invalid_names[] = {
        "", "Full", "LOCAL-CODE", "local_code", "local-code ", "unknown"
    };
    cdx_tool_profile profile = CDX_TOOL_PROFILE_LOCAL_CODE;
    time_t deadline = time(NULL) + 5;
    size_t i;
    int valid = 1, pilot = argc > 1 && !strcmp(argv[1], "--pilot");
    check("omitted profile selects full", cdx_tool_profile_parse(NULL, &profile) &&
        profile == CDX_TOOL_PROFILE_FULL);
    if (pilot) {
        (void)printf("%u/%u tool-profile checks passed\n", passed, total);
        return passed == total ? 0 : 1;
    }
    check("explicit full selects full", cdx_tool_profile_parse("full", &profile) &&
        profile == CDX_TOOL_PROFILE_FULL && !strcmp(cdx_tool_profile_name(profile), "full"));
    check("explicit local-code selects local-code", cdx_tool_profile_parse("local-code", &profile) &&
        profile == CDX_TOOL_PROFILE_LOCAL_CODE && !strcmp(cdx_tool_profile_name(profile), "local-code"));
    for (i = 0; i < sizeof(invalid_names) / sizeof(invalid_names[0]) && time(NULL) < deadline; ++i)
        valid &= !cdx_tool_profile_parse(invalid_names[i], &profile) && profile == CDX_TOOL_PROFILE_LOCAL_CODE;
    check("unknown profiles fail without selecting another profile", valid && i == sizeof(invalid_names) / sizeof(invalid_names[0]));
    check("invalid enum has no profile name", cdx_tool_profile_name((cdx_tool_profile)99) == NULL);
    for (i = 0; i < sizeof(local_tools) / sizeof(local_tools[0]) && time(NULL) < deadline; ++i) {
        char label[120];
        (void)snprintf(label, sizeof(label), "local-code advertises %s", local_tools[i]);
        check(label, cdx_tool_profile_advertises(CDX_TOOL_PROFILE_LOCAL_CODE, local_tools[i]) &&
            cdx_tool_profile_advertises(CDX_TOOL_PROFILE_FULL, local_tools[i]));
    }
    check("all 27 local-code members checked", i == sizeof(local_tools) / sizeof(local_tools[0]));
    for (i = 0; i < sizeof(other_tools) / sizeof(other_tools[0]) && time(NULL) < deadline; ++i) {
        char label[120];
        (void)snprintf(label, sizeof(label), "local-code omits %s advertisement", other_tools[i]);
        check(label, !cdx_tool_profile_advertises(CDX_TOOL_PROFILE_LOCAL_CODE, other_tools[i]) &&
            cdx_tool_profile_advertises(CDX_TOOL_PROFILE_FULL, other_tools[i]));
    }
    check("all eight omitted tools checked", i == sizeof(other_tools) / sizeof(other_tools[0]));
    check("local-code does not advertise an unlisted tool", !cdx_tool_profile_advertises(CDX_TOOL_PROFILE_LOCAL_CODE, "future_tool"));
    check("full keeps future advertisements", cdx_tool_profile_advertises(CDX_TOOL_PROFILE_FULL, "future_tool"));
    check("invalid enum advertises no tools", !cdx_tool_profile_advertises((cdx_tool_profile)99, "read_file"));
    check("checks finish before fixed deadline", time(NULL) < deadline);
    (void)printf("%u/%u tool-profile checks passed\n", passed, total);
    return passed == total ? 0 : 1;
}
