#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$SourceRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$InputExecutable,
    [string]$OutputDirectory,
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z.+_-]{0,63}$')][string]$Version = '0.1.0-dev',
    [ValidateRange(0, 253402300799)][long]$SourceDateEpoch = 0,
    [datetime]$DeadlineUtc = [datetime]::UtcNow.AddSeconds(120)
)

function Assert-PackageBudget {
    param([datetime]$Deadline)
    if ([datetime]::UtcNow -ge $Deadline) { throw 'Package operation exceeded its 120-second wall-clock budget.' }
}

function Get-PackagePath {
    param([string]$Path, [switch]$MustExist)
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path.IndexOfAny([char[]]@([char]0, [char]10, [char]13)) -ge 0 -or
        $Path -match '(\$\(|\$\{|;|`|\|)') { throw 'Expected a literal filesystem path.' }
    if ($MustExist) { return (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path }
    return [IO.Path]::GetFullPath($Path)
}

function Assert-PackageContained {
    param([string]$Path, [string]$Root)
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if (-not $fullPath.StartsWith($fullRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Package path '$fullPath' is outside '$fullRoot'."
    }
}

function Write-PackageText {
    param([string]$Path, [string]$Text)
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Get-PackageTextHash {
    param([string]$Text, [ValidateSet('SHA1', 'SHA256')][string]$Algorithm)
    $hash = if ($Algorithm -eq 'SHA1') { [Security.Cryptography.SHA1]::Create() } else { [Security.Cryptography.SHA256]::Create() }
    try { return [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text))).Replace('-', '').ToLowerInvariant() }
    finally { $hash.Dispose() }
}

function Invoke-PackageProgram {
    param([string]$Executable, [string[]]$Arguments, [string]$WorkingDirectory,
        [string]$AuditPath, [datetime]$Deadline, [int]$TimeoutSeconds = 15)
    Assert-PackageBudget $Deadline
    if ($Arguments.Count -gt 32) { throw 'Package subprocess argument limit exceeded.' }
    $remaining = [Math]::Min($TimeoutSeconds * 1000, [Math]::Floor(($Deadline - [datetime]::UtcNow).TotalMilliseconds) - 5000)
    if ($remaining -le 0) { throw 'Insufficient package time budget for a subprocess.' }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = $Executable
    $process.StartInfo.WorkingDirectory = $WorkingDirectory
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void]$process.StartInfo.ArgumentList.Add($argument) }
    $started = $false
    $record = $null
    try {
        $requestedUtc = [DateTime]::UtcNow
        $started = $process.Start()
        if (-not $started) { throw "Could not start '$Executable'." }
        $childStarted = $process.StartTime
        $parentStarted = (Get-Process -Id $PID).StartTime
        $record = [ordered]@{
            pid = $process.Id; requested_utc = $requestedUtc.ToString('o')
            started_utc = if ($null -ne $childStarted) { $childStarted.ToUniversalTime().ToString('o') } else { $null }
            creation_time_observed = ($null -ne $childStarted)
            program = $Executable; args = $Arguments; cwd = $WorkingDirectory
            parent_pid = $PID; parent_started_utc = if ($null -ne $parentStarted) { $parentStarted.ToUniversalTime().ToString('o') } else { $null }
            state = 'running'; cleanup_complete = $false
        }
        [IO.File]::AppendAllText($AuditPath, (($record | ConvertTo-Json -Depth 5 -Compress) + "`n"))
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit([int]$remaining)) { $record.state = 'timed_out'; throw "Package subprocess timed out (PID $($process.Id))." }
        if (-not $stdout.Wait(2000) -or -not $stderr.Wait(2000)) { throw 'Package subprocess output did not close.' }
        $record.state = 'exited'; $record.exit_code = $process.ExitCode
        $result = [pscustomobject]@{ ExitCode = $process.ExitCode; Stdout = $stdout.Result; Stderr = $stderr.Result }
        if ($result.Stdout.Length + $result.Stderr.Length -gt 1048576) { throw 'Package subprocess output exceeds 1 MiB.' }
        return $result
    }
    finally {
        if ($started -and -not $process.HasExited) { $process.Kill($true); [void]$process.WaitForExit(3000) }
        if ($record) {
            $record.cleanup_complete = $process.HasExited
            [IO.File]::AppendAllText($AuditPath, (($record | ConvertTo-Json -Depth 5 -Compress) + "`n"))
        }
        $process.Dispose()
    }
}

# The package gate dot-sources these shared helpers; normal invocation runs packaging.
if ($MyInvocation.InvocationName -eq '.') { return }
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 or later is required.' }
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$maximumDeadline = [datetime]::UtcNow.AddSeconds(120)
if ($DeadlineUtc -gt $maximumDeadline) { $DeadlineUtc = $maximumDeadline }
$SourceRoot = Get-PackagePath $SourceRoot -MustExist
if (-not $InputExecutable) { $InputExecutable = Join-Path $SourceRoot 'build\bin\cdx.exe' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $SourceRoot 'build\packaging' }
$InputExecutable = Get-PackagePath $InputExecutable -MustExist
$OutputDirectory = Get-PackagePath $OutputDirectory
Assert-PackageContained $OutputDirectory (Join-Path $SourceRoot 'build')
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$taskRoot = Join-Path $OutputDirectory ('package-' + [guid]::NewGuid().ToString('N'))
Assert-PackageContained $taskRoot $OutputDirectory
[void](New-Item -ItemType Directory -Path $taskRoot -ErrorAction Stop)
$staging = Join-Path $taskRoot 'staging'
[void][IO.Directory]::CreateDirectory($staging)
$auditPath = Join-Path $taskRoot 'processes.jsonl'
$success = $false

try {
    $versionResult = Invoke-PackageProgram $InputExecutable @('--version') $SourceRoot $auditPath $DeadlineUtc
    if ($versionResult.ExitCode -ne 0 -or $versionResult.Stdout.Trim() -cne "Cordonix cdx $Version") {
        throw "Input executable version does not equal requested package version '$Version'."
    }
    $infoResult = Invoke-PackageProgram $InputExecutable @('--request', (Join-Path $SourceRoot 'examples\request.json')) $SourceRoot $auditPath $DeadlineUtc
    if ($infoResult.ExitCode -ne 0) { throw 'Executable build metadata query failed.' }
    $buildInfo = $infoResult.Stdout | ConvertFrom-Json
    if ($buildInfo.status -cne 'ok' -or $buildInfo.version -cne $Version -or
        $buildInfo.build_revision -notmatch '^(UNKNOWN|[0-9a-f]{40})$') { throw 'Executable build metadata is invalid.' }
    $requiredFiles = [ordered]@{
        'cdx.exe' = $InputExecutable; 'LICENSE' = Join-Path $SourceRoot 'LICENSE'
        'README.md' = Join-Path $SourceRoot 'README.md'; 'docs/packaging.md' = Join-Path $SourceRoot 'docs\packaging.md'
        'docs/execution-boundaries.md' = Join-Path $SourceRoot 'docs\execution-boundaries.md'
        'docs/install-update.md' = Join-Path $SourceRoot 'docs\install-update.md'
        'install/install.ps1' = Join-Path $SourceRoot 'install\install.ps1'
        'install/install.sh' = Join-Path $SourceRoot 'install\install.sh'
        'examples/codex-config.toml' = Join-Path $SourceRoot 'examples\codex-config.toml'
        'examples/request.json' = Join-Path $SourceRoot 'examples\request.json'
        'examples/cordonix-install-update-prompt.md' = Join-Path $SourceRoot 'examples\cordonix-install-update-prompt.md'
        'examples/read-lines.json' = Join-Path $SourceRoot 'examples\read-lines.json'
        'examples/read-first.json' = Join-Path $SourceRoot 'examples\read-first.json'
        'examples/read-tail.json' = Join-Path $SourceRoot 'examples\read-tail.json'
        'examples/read-raw.json' = Join-Path $SourceRoot 'examples\read-raw.json'
        'examples/list-directory.json' = Join-Path $SourceRoot 'examples\list-directory.json'
        'examples/resolve-path.json' = Join-Path $SourceRoot 'examples\resolve-path.json'
        'examples/verify-encoding.json' = Join-Path $SourceRoot 'examples\verify-encoding.json'
    }
    foreach ($entry in $requiredFiles.GetEnumerator()) {
        Assert-PackageBudget $DeadlineUtc
        $input = Get-Item -LiteralPath $entry.Value -ErrorAction Stop
        if ($input.PSIsContainer -or $input.Length -gt 33554432) { throw "Package input is not a file of at most 32 MiB: $($entry.Value)" }
        $target = Join-Path $staging $entry.Key
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $target))
        Copy-Item -LiteralPath $entry.Value -Destination $target
    }
    $git = (Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $revision = Invoke-PackageProgram $git @('-C', $SourceRoot, 'rev-parse', 'HEAD') $SourceRoot $auditPath $DeadlineUtc
    $status = Invoke-PackageProgram $git @('-C', $SourceRoot, 'status', '--porcelain=v1', '--untracked-files=normal') $SourceRoot $auditPath $DeadlineUtc
    if ($revision.ExitCode -ne 0 -or $status.ExitCode -ne 0) { throw 'Could not read packaging checkout provenance.' }
    $gitStatus = @($status.Stdout -split "`r?`n" | Where-Object { $_ })
    if ($gitStatus.Count -gt 100) { throw 'Packaging checkout has more than 100 changed paths.' }
    if ($env:CDX_REQUIRE_CLEAN_BUILD -eq '1' -and ($gitStatus.Count -gt 0 -or $buildInfo.build_dirty -ne $false -or
        $buildInfo.build_revision -cne $revision.Stdout.Trim())) { throw 'Release requires a clean build of the packaging revision.' }
    $fileRecords = @(
        foreach ($name in @($requiredFiles.Keys | Sort-Object -CaseSensitive)) {
            Assert-PackageBudget $DeadlineUtc
            $file = Get-Item -LiteralPath (Join-Path $staging $name)
            [ordered]@{ path = $name; size_bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
        }
    )
    $epochText = [DateTimeOffset]::FromUnixTimeSeconds($SourceDateEpoch).UtcDateTime.ToString('yyyy-MM-ddTHH:mm:ssZ', [Globalization.CultureInfo]::InvariantCulture)
    $manifest = [ordered]@{
        format_version = 1; name = 'Cordonix'; version = $Version; target = 'windows-x64'
        scope = 'payload files; generated metadata is described by SPDX and the archive hash'
        source_date_epoch = $SourceDateEpoch; files = $fileRecords
    }
    Write-PackageText (Join-Path $staging 'manifest.json') ($manifest | ConvertTo-Json -Depth 8 -Compress)
    $provenance = [ordered]@{
        format_version = 1; name = 'Cordonix'; version = $Version; target = 'windows-x64'
        checkout_revision_at_packaging = $revision.Stdout.Trim(); checkout_dirty_at_packaging = ($gitStatus.Count -gt 0)
        checkout_status_at_packaging = $gitStatus; binary_build_revision = $buildInfo.build_revision
        binary_build_dirty = $buildInfo.build_dirty
        provenance_boundary = 'Build-script revision and dirty flag embedded in the executable and independently read back. This is not a signed builder attestation.'
        input_executable_sha256 = ($fileRecords | Where-Object path -eq 'cdx.exe').sha256
        verified_executable_version = $versionResult.Stdout.Trim(); source_date_epoch = $SourceDateEpoch
        builder = 'eng/package.ps1'; signature = 'NOT_RUN'
    }
    Write-PackageText (Join-Path $staging 'provenance.json') ($provenance | ConvertTo-Json -Depth 8 -Compress)
    $analyzedNames = @(@($requiredFiles.Keys) + @('manifest.json', 'provenance.json') | Sort-Object -CaseSensitive)
    $spdxFiles = @(
        foreach ($name in $analyzedNames) {
            Assert-PackageBudget $DeadlineUtc
            $path = Join-Path $staging $name
            [ordered]@{
                fileName = './' + $name; SPDXID = 'SPDXRef-File-' + ($name -replace '[^A-Za-z0-9.-]', '-')
                checksums = @(
                    [ordered]@{ algorithm = 'SHA1'; checksumValue = (Get-FileHash -LiteralPath $path -Algorithm SHA1).Hash.ToLowerInvariant() }
                    [ordered]@{ algorithm = 'SHA256'; checksumValue = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
                )
                licenseConcluded = 'NOASSERTION'; licenseInfoInFiles = @('NOASSERTION'); copyrightText = 'NOASSERTION'
            }
        }
    )
    $sha1Values = @($spdxFiles | ForEach-Object { $_.checksums[0].checksumValue } | Sort-Object -CaseSensitive)
    $verificationCode = Get-PackageTextHash ($sha1Values -join '') 'SHA1'
    $contentHash = Get-PackageTextHash ($spdxFiles | ConvertTo-Json -Depth 8 -Compress) 'SHA256'
    $relationships = @([ordered]@{ spdxElementId = 'SPDXRef-DOCUMENT'; relationshipType = 'DESCRIBES'; relatedSpdxElement = 'SPDXRef-Package-Cordonix' })
    $relationships += @($spdxFiles | ForEach-Object { [ordered]@{ spdxElementId = 'SPDXRef-Package-Cordonix'; relationshipType = 'CONTAINS'; relatedSpdxElement = $_.SPDXID } })
    $sbom = [ordered]@{
        spdxVersion = 'SPDX-2.3'; dataLicense = 'CC0-1.0'; SPDXID = 'SPDXRef-DOCUMENT'; name = "Cordonix-$Version-win-x64"
        documentNamespace = "https://github.com/IoTSharp/Cordonix/spdx/$contentHash"
        creationInfo = [ordered]@{ created = $epochText; creators = @('Tool: Cordonix eng/package.ps1') }
        packages = @([ordered]@{
            SPDXID = 'SPDXRef-Package-Cordonix'; name = 'Cordonix'; versionInfo = $Version; downloadLocation = 'NOASSERTION'
            filesAnalyzed = $true; packageVerificationCode = [ordered]@{ packageVerificationCodeValue = $verificationCode; packageVerificationCodeExcludedFiles = @('./sbom.spdx.json') }
            licenseConcluded = 'NOASSERTION'; licenseDeclared = 'Apache-2.0'; licenseInfoFromFiles = @('NOASSERTION'); copyrightText = 'NOASSERTION'
        })
        files = $spdxFiles; relationships = $relationships
    }
    Write-PackageText (Join-Path $staging 'sbom.spdx.json') ($sbom | ConvertTo-Json -Depth 12 -Compress)
    $archiveName = "cordonix-$Version-win-x64.zip"
    $temporaryArchive = Join-Path $taskRoot $archiveName
    $stream = [IO.File]::Open($temporaryArchive, [IO.FileMode]::CreateNew)
    try {
        $zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            foreach ($name in @($analyzedNames + @('sbom.spdx.json') | Sort-Object -CaseSensitive)) {
                Assert-PackageBudget $DeadlineUtc
                $entry = $zip.CreateEntry($name, [IO.Compression.CompressionLevel]::NoCompression)
                $entry.LastWriteTime = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
                $entry.ExternalAttributes = 0
                $input = [IO.File]::OpenRead((Join-Path $staging $name)); $output = $entry.Open()
                try { $input.CopyTo($output, 65536) } finally { $output.Dispose(); $input.Dispose() }
            }
        } finally { $zip.Dispose() }
    } finally { $stream.Dispose() }
    $archivePath = Join-Path $OutputDirectory $archiveName
    [IO.File]::Move($temporaryArchive, $archivePath, $true)
    foreach ($pair in @(@('manifest.json', 'manifest.json'), @('provenance.json', 'provenance.json'), @('sbom.spdx.json', 'spdx.json'))) {
        Assert-PackageBudget $DeadlineUtc
        Copy-Item -LiteralPath (Join-Path $staging $pair[0]) -Destination (Join-Path $OutputDirectory "cordonix-$Version-win-x64.$($pair[1])") -Force
    }
    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-PackageText ($archivePath + '.sha256') "$archiveHash  $archiveName`n"
    Assert-PackageBudget $DeadlineUtc
    $success = $true
    [ordered]@{ status = 'PASS'; archive = $archivePath; archive_sha256 = $archiveHash; version = $Version
        checkout_revision_at_packaging = $revision.Stdout.Trim(); checkout_dirty_at_packaging = ($gitStatus.Count -gt 0)
        binary_build_revision = $buildInfo.build_revision; binary_build_dirty = $buildInfo.build_dirty; signature = 'NOT_RUN' } | ConvertTo-Json -Depth 5
}
finally {
    Assert-PackageContained $taskRoot $OutputDirectory
    if ($success) { Remove-Item -LiteralPath $taskRoot -Recurse -Force }
    else { Write-Warning "Package failed; task-owned diagnostics retained at $taskRoot" }
}
