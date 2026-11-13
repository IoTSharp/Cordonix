#include "cdx/tool_profiles.h"
#include <stddef.h>
#include <string.h>

int cdx_tool_profile_parse(const char *name, cdx_tool_profile *profile) {
    if (!name || !strcmp(name, "full")) {
        *profile = CDX_TOOL_PROFILE_FULL;
        return 1;
    }
    if (!strcmp(name, "local-code")) {
        *profile = CDX_TOOL_PROFILE_LOCAL_CODE;
        return 1;
    }
    return 0;
}

const char *cdx_tool_profile_name(cdx_tool_profile profile) {
    switch (profile) {
    case CDX_TOOL_PROFILE_FULL: return "full";
    case CDX_TOOL_PROFILE_LOCAL_CODE: return "local-code";
    default: return NULL;
    }
}

int cdx_tool_profile_advertises(cdx_tool_profile profile, const char *tool_name) {
    static const char *const local_code[] = {
        "create_directory", "query_processes", "search_text", "git_status",
        "create_file", "tool_info", "execute_program", "workspace_open",
        "list_directory", "resolve_path", "verify_encoding", "read_file",
        "workspace_read", "workspace_edit", "workspace_commit",
        "workspace_convert_encoding", "workspace_status", "workspace_search",
        "workspace_close", "read_files", "data_query", "find_files",
        "find_program", "hash_file", "copy_file", "move_file", "stat"
    };
    size_t i;
    if (profile == CDX_TOOL_PROFILE_FULL) return 1;
    if (profile != CDX_TOOL_PROFILE_LOCAL_CODE) return 0;
    for (i = 0; i < sizeof(local_code) / sizeof(local_code[0]); ++i)
        if (!strcmp(tool_name, local_code[i])) return 1;
    return 0;
}
