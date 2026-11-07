# Cordonix 0.4.1

Cordonix 0.4.1 adds daily background update checks and makes file-creation errors identify the actual invalid argument. It retains all 34 MCP tools and their existing valid argument contracts.

## Daily background updates

- On the first startup of each local calendar day, the stable launcher starts a separate update worker and continues the main runtime. Later launches that day skip the check.
- Windows and Linux provide `cdx --checkupgrade [--install-root absolute-directory]`; `tool_info.update_command` advertises the capability.
- Update settings retain explicit opt-out, source/proxy choices and the last attempt. A shared installation lock coordinates update workers. GitHub remains the preferred source, with Gitee fallback.
- Default startup checks need no scheduler. The installer retains the optional legacy scheduler path and migrates older launcher generations.

## Accurate file-creation errors

`create_file` requires `path` and `content`. A request with `text` instead of `content` now returns:

```json
{"status":"validation_error","message":"Missing required string parameter 'content'; rename 'text' to 'content'"}
```

Missing or mistyped path/content, invalid encoding types, unsupported encoding names and actual `encoding: "auto"` requests receive distinct diagnostics before writing. Tool/property descriptions state the correct body field. UTF-8 remains the default; empty files, strict GBK/CP936 conversion and no-overwrite behavior remain supported.

## Upgrade and verification

Use the stable installer described in [Install and update](https://github.com/IoTSharp/Cordonix/blob/v0.4.1/docs/install-update.md). Existing installations need the updated launcher to adopt daily startup checks; the launcher selects the native command when the installed binary exposes it. An existing MCP connection retains its running executable until its client establishes a new connection.

Windows x64 ZIP and Linux x64 tar.gz packages include release notes, checksums, manifests, SPDX inventories and packaging provenance. Detached `.sigstore.json` bundles identify the exact source tag and archive digest; verification instructions are in [Release signing](https://github.com/IoTSharp/Cordonix/blob/v0.4.1/docs/release-signing.md). GitHub and Gitee carry the same package bytes.

The release workflow requires native/MCP tests, package checks, SPDX validation, reproducibility and installation/update fixtures on both platforms before publication. Microsoft WinGet catalog acceptance remains a separate upstream process.

The v0.4.0 tag was retained after package validation caught a missing release-notes file; no release assets were published for that tag. Version 0.4.1 includes the corrected package lists.

Full changes: [v0.3.1...v0.4.1](https://github.com/IoTSharp/Cordonix/compare/v0.3.1...v0.4.1).
