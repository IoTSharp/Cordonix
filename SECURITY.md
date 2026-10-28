# Security

Cordonix is a development MVP. There is no stable supported-version policy yet; report findings against the exact commit or development binary used.

## Trust boundary

Cordonix executes programs and writes explicitly committed source files with the permissions of its current OS user. Typed argv and Windows Job ownership reduce command interpretation and process-lifecycle mistakes; they do not provide OS isolation or make an arbitrary program safe. A program can still perform the actions allowed by its arguments, configuration and permissions.

Use a trusted local MCP client and a normal least-privileged account. Keep network-facing exposure outside the current stdio MVP. Do not supply credentials in arguments or source fixtures when a safer existing mechanism is available.

## Reporting a vulnerability

Use GitHub's **Report a vulnerability** action on [maikebing/Cordonix](https://github.com/maikebing/Cordonix/security) if the private reporting option is available. If it is not available, open a minimal issue asking for a private reporting channel without posting an exploit, credentials, private source or sensitive paths.

Include the affected commit, Windows version, compiler/build details, entry point, expected and actual behavior, and a minimal sanitized reproducer. Indicate whether the issue causes unintended execution, data loss, encoding corruption, stale-file overwrite or unowned-process termination. No response-time or remediation deadline is promised until the maintainers establish a support policy.

For a possible write failure or timeout, preserve evidence and inspect the target state before repeating the operation. A timeout does not establish that an external side effect did not happen.
