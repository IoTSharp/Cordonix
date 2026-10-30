#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string[]]$Path, [string]$SchemaPath, [uri]$Proxy)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$deadline = [DateTime]::UtcNow.AddSeconds(60)
if ($Path.Count -lt 1 -or $Path.Count -gt 4) { throw 'Supply one to four SPDX files.' }
$root = Split-Path -Parent $PSScriptRoot
$owned = $null
try {
    if (-not $SchemaPath) {
        $owned = Join-Path $root ('build/spdx-' + [guid]::NewGuid().ToString('N'))
        [void][IO.Directory]::CreateDirectory($owned)
        $SchemaPath = Join-Path $owned 'spdx-schema.json'
        $request = @{ Uri='https://raw.githubusercontent.com/spdx/spdx-spec/v2.3/schemas/spdx-schema.json'; OutFile=$SchemaPath; TimeoutSec=20; MaximumRetryCount=0 }
        if ($Proxy) { $request.Proxy=$Proxy }
        Invoke-WebRequest @request
    }
    $schemaHash = (Get-FileHash -LiteralPath $SchemaPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($schemaHash -cne '239208b7ac287b3cf5d9a9af23f9d69863971102a5e1587a27a398b43490b89b') { throw 'The official SPDX 2.3 schema hash differs from the reviewed schema.' }
    $results = foreach ($document in $Path) {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'SPDX gate deadline exceeded.' }
        $file = Get-Item -LiteralPath $document
        if ($file.PSIsContainer -or $file.Length -gt 4194304) { throw 'SPDX document must be at most 4 MiB.' }
        if (-not (Test-Json -Json ([IO.File]::ReadAllText($file.FullName)) -SchemaFile $SchemaPath -ErrorAction Stop)) { throw 'SPDX schema validation failed.' }
        [ordered]@{path=$file.FullName;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant();status='PASS'}
    }
    [ordered]@{status='PASS';schema_sha256=$schemaHash;validator=('PowerShell Test-Json ' + $PSVersionTable.PSVersion);results=@($results)} | ConvertTo-Json -Depth 6
}
finally {
    if ($owned) {
        $absolute = [IO.Path]::GetFullPath($owned)
        $expected = [IO.Path]::GetFullPath((Join-Path $root 'build')) + [IO.Path]::DirectorySeparatorChar
        if (-not $absolute.StartsWith($expected, [StringComparison]::OrdinalIgnoreCase)) { throw 'Schema cleanup escaped build.' }
        Remove-Item -LiteralPath $absolute -Recurse -Force
    }
}
