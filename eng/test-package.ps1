#requires -Version 7.0
[CmdletBinding()]
param([string]$SourceRoot = (Split-Path -Parent $PSScriptRoot), [string]$Version = '0.1.0-dev')

$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 or later is required.' }
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$deadline = [datetime]::UtcNow.AddSeconds(120)
$packageScript = Join-Path $PSScriptRoot 'package.ps1'
. $packageScript -SourceRoot $SourceRoot -Version $Version
$SourceRoot = Get-PackagePath $SourceRoot -MustExist
$verificationRoot = Join-Path $SourceRoot 'build\package-tests'
$taskRoot = Join-Path $verificationRoot ('run-' + [guid]::NewGuid().ToString('N'))
Assert-PackageContained $taskRoot $verificationRoot
[void](New-Item -ItemType Directory -Path $taskRoot -ErrorAction Stop)
$extractRoot = Join-Path $taskRoot '含 空格 的验证目录'
$outputDirectory = Join-Path $SourceRoot 'build\packaging'
$auditPath = Join-Path $taskRoot 'processes.jsonl'
$success = $false

try {
    $first = & $packageScript -SourceRoot $SourceRoot -Version $Version -DeadlineUtc $deadline | ConvertFrom-Json
    Assert-PackageBudget $deadline
    $second = & $packageScript -SourceRoot $SourceRoot -Version $Version -DeadlineUtc $deadline | ConvertFrom-Json
    if ($first.status -cne 'PASS' -or $second.status -cne 'PASS' -or $first.archive_sha256 -cne $second.archive_sha256) { throw 'Packages from the same inputs are not byte-identical.' }
    $archivePath = $second.archive
    $archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        if ($archive.Entries.Count -ne 17) { throw 'Package must contain exactly 17 files.' }
        foreach ($entry in $archive.Entries) {
            Assert-PackageBudget $deadline
            if ($entry.Length -gt 33554432) { throw 'Package entry exceeds 32 MiB.' }
            Assert-PackageContained (Join-Path $extractRoot $entry.FullName) $extractRoot
        }
    } finally { $archive.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $extractRoot)
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $extractRoot 'manifest.json') | ConvertFrom-Json
    $provenance = Get-Content -Raw -LiteralPath (Join-Path $extractRoot 'provenance.json') | ConvertFrom-Json
    $sbom = Get-Content -Raw -LiteralPath (Join-Path $extractRoot 'sbom.spdx.json') | ConvertFrom-Json
    if ($manifest.format_version -ne 1 -or $manifest.version -cne $Version -or $manifest.target -cne 'windows-x64' -or @($manifest.files).Count -ne 14) { throw 'Invalid package manifest.' }
    foreach ($file in $manifest.files) {
        Assert-PackageBudget $deadline
        $path = Join-Path $extractRoot $file.path
        Assert-PackageContained $path $extractRoot
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $file.sha256 -or (Get-Item -LiteralPath $path).Length -ne $file.size_bytes) { throw "Manifest mismatch: $($file.path)" }
    }
    if ((Get-FileHash -LiteralPath (Join-Path $extractRoot 'LICENSE') -Algorithm SHA256).Hash -cne (Get-FileHash -LiteralPath (Join-Path $SourceRoot 'LICENSE') -Algorithm SHA256).Hash) { throw 'Package license differs from source.' }
    if ($provenance.format_version -ne 1 -or $provenance.binary_build_revision -cne 'UNKNOWN' -or
        $provenance.verified_executable_version -cne "Cordonix cdx $Version" -or $provenance.checkout_revision_at_packaging -notmatch '^[0-9a-f]{40}$' -or
        $provenance.input_executable_sha256 -cne ($manifest.files | Where-Object path -eq 'cdx.exe').sha256) { throw 'Invalid packaging provenance.' }
    if ($sbom.spdxVersion -cne 'SPDX-2.3' -or $sbom.dataLicense -cne 'CC0-1.0' -or $sbom.SPDXID -cne 'SPDXRef-DOCUMENT' -or
        @($sbom.packages).Count -ne 1 -or @($sbom.files).Count -ne 16 -or @($sbom.relationships).Count -ne 17 -or
        $sbom.documentNamespace -notmatch '^https://github.com/IoTSharp/Cordonix/spdx/[0-9a-f]{64}$') { throw 'Invalid SPDX document shape.' }
    $package = $sbom.packages[0]
    if (-not $package.filesAnalyzed -or $package.versionInfo -cne $Version -or $package.licenseDeclared -cne 'Apache-2.0' -or
        @($package.packageVerificationCode.packageVerificationCodeExcludedFiles).Count -ne 1 -or $package.packageVerificationCode.packageVerificationCodeExcludedFiles[0] -cne './sbom.spdx.json') { throw 'Invalid SPDX package metadata.' }
    $describes = @($sbom.relationships | Where-Object { $_.spdxElementId -ceq $sbom.SPDXID -and $_.relationshipType -ceq 'DESCRIBES' -and $_.relatedSpdxElement -ceq $package.SPDXID })
    if ($describes.Count -ne 1) { throw 'SPDX must describe its package exactly once.' }
    $namespaceHash = Get-PackageTextHash ($sbom.files | ConvertTo-Json -Depth 8 -Compress) 'SHA256'
    if ($sbom.documentNamespace -cne "https://github.com/IoTSharp/Cordonix/spdx/$namespaceHash") { throw 'SPDX namespace is not bound to its file content.' }
    $sha1Values = @(
        foreach ($file in $sbom.files) {
            Assert-PackageBudget $deadline
            $path = Join-Path $extractRoot $file.fileName
            Assert-PackageContained $path $extractRoot
            $sha1 = (Get-FileHash -LiteralPath $path -Algorithm SHA1).Hash.ToLowerInvariant()
            $sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($sha1 -cne ($file.checksums | Where-Object algorithm -eq 'SHA1').checksumValue -or $sha256 -cne ($file.checksums | Where-Object algorithm -eq 'SHA256').checksumValue) { throw 'SPDX file checksum mismatch.' }
            if (@($sbom.relationships | Where-Object { $_.spdxElementId -ceq $package.SPDXID -and $_.relationshipType -ceq 'CONTAINS' -and $_.relatedSpdxElement -ceq $file.SPDXID }).Count -ne 1) { throw 'SPDX file is missing its package relationship.' }
            $sha1
        }
    )
    if ((Get-PackageTextHash (($sha1Values | Sort-Object -CaseSensitive) -join '') 'SHA1') -cne $package.packageVerificationCode.packageVerificationCodeValue) { throw 'SPDX package verification code mismatch.' }
    foreach ($pair in @(@('manifest.json', 'manifest.json'), @('provenance.json', 'provenance.json'), @('sbom.spdx.json', 'spdx.json'))) {
        Assert-PackageBudget $deadline
        if ((Get-FileHash -LiteralPath (Join-Path $extractRoot $pair[0]) -Algorithm SHA256).Hash -cne (Get-FileHash -LiteralPath (Join-Path $outputDirectory "cordonix-$Version-win-x64.$($pair[1])") -Algorithm SHA256).Hash) { throw 'External package metadata differs from archive.' }
    }
    $expectedChecksum = "$($second.archive_sha256)  $([IO.Path]::GetFileName($archivePath))"
    if ((Get-Content -LiteralPath ($archivePath + '.sha256') -Raw).Trim() -cne $expectedChecksum) { throw 'Archive checksum sidecar mismatch.' }
    $executable = Join-Path $extractRoot 'cdx.exe'
    $versionResult = Invoke-PackageProgram $executable @('--version') $extractRoot $auditPath $deadline
    if ($versionResult.ExitCode -ne 0 -or $versionResult.Stdout.Trim() -cne "Cordonix cdx $Version") { throw 'Packaged executable version mismatch.' }
    $requestPath = Join-Path $taskRoot 'tool-info.json'
    Write-PackageText $requestPath '{"operation":"tool_info","arguments":{}}'
    $requestResult = Invoke-PackageProgram $executable @('--request', $requestPath) $extractRoot $auditPath $deadline
    if ($requestResult.ExitCode -ne 0) { throw 'Packaged tool_info failed.' }
    $info = $requestResult.Stdout | ConvertFrom-Json
    if ($info.status -cne 'ok' -or $info.name -cne 'Cordonix' -or $info.version -cne $Version -or $info.platform -cne 'windows' -or $info.runtime -cne 'C11') { throw 'Packaged tool_info contract mismatch.' }
    Assert-PackageBudget $deadline
    $success = $true
    [ordered]@{ status = 'PASS'; reproducible_sha256 = $second.archive_sha256; archive = $archivePath; version = $Version
        manifest_files = 14; spdx_files = 16; spdx_verification = 'PASS'; unicode_path_cli = 'PASS'; signature = 'NOT_RUN' } | ConvertTo-Json
}
finally {
    Assert-PackageContained $taskRoot $verificationRoot
    if ($success) { Remove-Item -LiteralPath $taskRoot -Recurse -Force }
    else { Write-Warning "Package test failed; task-owned diagnostics retained at $taskRoot" }
}
