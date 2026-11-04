param([Parameter(Mandatory)][string]$ArchivePath)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'PowerShell 7 is required' }
$repoRoot = Split-Path -Parent $PSScriptRoot
$archive = (Resolve-Path -LiteralPath $ArchivePath).Path
$expected = 'd9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7'
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) { throw 'libssh2 archive SHA-256 mismatch' }
$scratch = Join-Path $repoRoot ('build\vendor-libssh2-' + [guid]::NewGuid().ToString('N'))
$destination = Join-Path $repoRoot 'third_party\libssh2'
$timer = [Diagnostics.Stopwatch]::StartNew()
try {
    New-Item -ItemType Directory -Path $scratch -ErrorAction Stop | Out-Null
    & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $scratch
    if ($LASTEXITCODE) { throw 'libssh2 extraction failed' }
    $sourceRoot = Join-Path $scratch 'libssh2-1.11.1'
    $files = @('COPYING', 'src\Makefile.inc')
    foreach ($directory in @('src', 'include')) {
        $entries = @(Get-ChildItem -LiteralPath (Join-Path $sourceRoot $directory) -File | Where-Object { $_.Extension -in '.c', '.h' })
        if ($entries.Count -gt 128) { throw 'Unexpected upstream file count' }
        $files += @($entries | ForEach-Object { Join-Path $directory $_.Name })
    }
    if ($files.Count -gt 160) { throw 'Unexpected vendor file count' }
    foreach ($relative in $files) {
        if ($timer.Elapsed.TotalSeconds -gt 30) { throw 'Vendor copy deadline exceeded' }
        $target = Join-Path $destination $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $sourceRoot $relative) -Destination $target
    }
    Write-Output "Vendored $($files.Count) unchanged libssh2 1.11.1 files; SHA-256 $expected"
}
finally {
    $resolvedScratch = [IO.Path]::GetFullPath($scratch)
    $allowed = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build')) + [IO.Path]::DirectorySeparatorChar
    if (!$resolvedScratch.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Scratch cleanup is outside build/' }
    if (Test-Path -LiteralPath $resolvedScratch) { Remove-Item -LiteralPath $resolvedScratch -Recurse -Force }
}
