#ifndef CDX_TOOL_PROFILES_H
#define CDX_TOOL_PROFILES_H

typedef enum cdx_tool_profile {
    CDX_TOOL_PROFILE_FULL = 0,
    CDX_TOOL_PROFILE_LOCAL_CODE
} cdx_tool_profile;

int cdx_tool_profile_parse(const char *name, cdx_tool_profile *profile);
const char *cdx_tool_profile_name(cdx_tool_profile profile);
/* Advertisement selection does not authorize or reject tools/call. */
int cdx_tool_profile_advertises(cdx_tool_profile profile, const char *tool_name);

#endif
