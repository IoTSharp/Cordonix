# Release artifact signing

Starting with v0.0.4, the Windows ZIP and Linux tar.gz have detached Sigstore provenance bundles. The release jobs use the existing `IoTSharp/Cordonix` GitHub Actions OIDC identity through the official attestation action pinned to commit `977bb373ede98d70efdf65b84cb5f73e068dcc2a` (v3). Each bundle covers one exact archive SHA-256 and accompanies the same archive on GitHub and Gitee.

Verify a downloaded archive with GitHub CLI:

```text
gh attestation verify cordonix-0.0.4-win-x64.zip --bundle cordonix-0.0.4-win-x64.zip.sigstore.json --repo IoTSharp/Cordonix --signer-workflow IoTSharp/Cordonix/.github/workflows/release.yml --source-ref refs/tags/v0.0.4 --source-digest <release-commit-sha> --deny-self-hosted-runners
```

Use the Linux archive and its `.sigstore.json` file for Linux. `tests/acceptance/verify_attestation.py` verifies both public hosts, checks certificate identity/source, archive subject and witnessed timestamps, and proves that modifying one byte fails digest verification. A network error does not pass the tampering check.

The signed archive contains the manifest, SPDX inventory and packaging provenance, so its signature binds those internal bytes. Separate metadata sidecars are compared with the archive by package gates; they are not individually signed subjects. The packaging provenance's `signature: NOT_RUN` describes the earlier packaging step, before the separate CI attestation is generated. The bundle is the signature evidence.

This is signed artifact provenance, not Windows Authenticode. It does not establish a Windows trusted-publisher certificate or SmartScreen reputation. It also does not establish reproducibility across different compilers or independent builders. Previously published versions and their assets remain immutable.
