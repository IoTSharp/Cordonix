# WinGet distribution

The package identifier is `IoTSharp.Cordonix`. Its Windows x64 ZIP is installed as
a portable application with the `cdx` command alias. The initial submission and
each subsequent version must pass the Microsoft community repository's checks
and review before appearing in the public WinGet source.

Initial 0.0.5 submission (2026-11-02):
[microsoft/winget-pkgs#468155](https://github.com/microsoft/winget-pkgs/pull/468155).
The published archive's SHA-256 and the three generated manifests passed the
local WinGet validator. Catalog availability is pending upstream completion.
The repository's automatic submission is enabled; workflow `41340981155`
successfully verified 0.0.5 and reused that PR without creating a duplicate.

As of 2026-11-02 04:16 Asia/Shanghai, all ten upstream validation checks
(`01. Pull Request Validation` through `10. Validation Completed`) and
`license/cla` report success, including installation and installer metadata
validation. PR #468155 remains open with `REVIEW_REQUIRED` and
`mergeStateStatus=BLOCKED` while review is pending; it has not merged,
so official WinGet catalog lookup remains pending.

Once the initial submission is merged and indexed:

```powershell
winget install --id IoTSharp.Cordonix --exact --source winget
winget upgrade --id IoTSharp.Cordonix --exact --source winget
```

WinGet manages its own installation and upgrades. Configure an MCP client to use
that installation separately. The bootstrap installation under
`%LOCALAPPDATA%\Cordonix`, its stable launcher and its hourly update task are a
separate installation; publishing a WinGet manifest does not switch that client
configuration or add another updater to it.

## Release automation

`.github/workflows/winget.yml` runs after a successful `Cordonix tagged release`
workflow, so Windows/Linux gates and GitHub/Gitee publication finish first. It
can also be dispatched with a published version such as `0.0.5`.

`eng/publish-winget.ps1` reads that stable release, downloads its actual Windows
archive and checksum, checks SHA-256 and the root `cdx.exe`, then renders the
three source templates under `install/winget/`. Generated manifests and receipts
belong under ignored `build/winget/<version>/`; edit the templates and regenerate
instead of modifying generated YAML.

Without `WINGET_TOKEN`, the workflow uploads the generated manifests for review.
With the repository Actions secret `WINGET_TOKEN`, it also creates a version
branch in `maikebing/winget-pkgs` and submits a PR to `microsoft/winget-pkgs`.
The token must allow writing that fork and opening a PR in the public upstream;
it is supplied through `GH_TOKEN` and never written into a manifest or receipt.
The fork must already exist. `-ForkOwner` supports another configured owner.
Existing submitted PRs are reported without opening another one. A timed-out
mutation requires remote-state inspection before retrying.

To prepare and validate a published version locally:

```powershell
pwsh -NoProfile -File ./eng/publish-winget.ps1 -Version 0.0.5
winget validate --manifest ./build/winget/0.0.5/manifests
```

Add `-Submit` to submit the prepared release using the authenticated GitHub
account. Automatic PR creation does not establish that the community PR has
merged, that source indexing has completed, or that every client has refreshed
its package metadata. Those are separate observable states.
