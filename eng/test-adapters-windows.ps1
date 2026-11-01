#requires -Version 7.0
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
. (Join-Path $PSScriptRoot 'package.ps1')
$root = Split-Path -Parent $PSScriptRoot
$deadline = [DateTime]::UtcNow.AddSeconds(90)
$git = (Get-Command git.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$rgCommand = Get-Command rg.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
$rg = if ($null -ne $rgCommand) { $rgCommand.Source } else { $null }
$binary = Join-Path $root 'build\protocol\cdx-adapter-tests.exe'
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$owned = Join-Path $temporaryRoot ('cordonix-adapters-' + [guid]::NewGuid().ToString('N'))
Assert-PackageContained $owned $temporaryRoot
[void][IO.Directory]::CreateDirectory($owned)
$audit = Join-Path $root ('build\protocol\adapter-processes-' + [guid]::NewGuid().ToString('N') + '.jsonl')
try {
    foreach ($mode in @('pilot','full')) {
        Assert-PackageBudget $deadline
        $fixture = Join-Path $owned $mode
        [void][IO.Directory]::CreateDirectory($fixture)
        $arguments = @($git, $(if ($rg) { $rg } else { $git }), $fixture)
        if ($mode -eq 'pilot') { $arguments += '--pilot' }
        elseif (-not $rg) { $arguments += '--git-only' }
        $result = Invoke-PackageProgram $binary $arguments $root $audit $deadline 35
        Write-Output $result.Stdout
        if ($result.ExitCode -ne 0) { throw ('Adapter gate failed: ' + $result.Stderr) }
    }
    if (-not $rg) { Write-Output 'Windows ripgrep gate: NOT_RUN (no configured native rg; Git scope passed)' }
}
finally {
    Assert-PackageContained $owned $temporaryRoot
    if (Test-Path -LiteralPath $owned) { Remove-Item -LiteralPath $owned -Recurse -Force }
}
