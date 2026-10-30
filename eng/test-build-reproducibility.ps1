#requires -Version 7.0
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
. (Join-Path $PSScriptRoot 'package.ps1')
$root = Split-Path -Parent $PSScriptRoot
$deadline = [DateTime]::UtcNow.AddSeconds(240)
$target = if ($IsWindows) { 'windows' } else { 'linux' }
$out = Join-Path $root 'build/reproducibility'
[void][IO.Directory]::CreateDirectory($out)
$audit = Join-Path $out ($target + '-' + [guid]::NewGuid().ToString('N') + '-processes.jsonl')
$program = if ($IsWindows) { 'C:\Windows\System32\cmd.exe' } else { '/bin/sh' }
$arguments = if ($IsWindows) { @('/d','/c',(Join-Path $root 'eng/build.cmd')) } else { @((Join-Path $root 'eng/build-linux.sh')) }
$binary = Join-Path $root $(if ($IsWindows) { 'build/bin/cdx.exe' } else { 'build/linux/bin/cdx' })
$results = @()
$git = (Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$revision = Invoke-PackageProgram $git @('rev-parse','HEAD') $root $audit $deadline 10
$checkout = Invoke-PackageProgram $git @('status','--porcelain=v1','--untracked-files=normal') $root $audit $deadline 10
if ($revision.ExitCode -ne 0 -or $checkout.ExitCode -ne 0) { throw 'Cannot establish the build source state.' }
$expectedDirty = -not [string]::IsNullOrEmpty($checkout.Stdout.Trim())
for ($iteration = 0; $iteration -lt 2; $iteration++) {
    Assert-PackageBudget $deadline
    $built = Invoke-PackageProgram $program $arguments $root $audit $deadline 110
    if ($built.ExitCode -ne 0) { throw ('Build failed: ' + $built.Stderr + $built.Stdout) }
    $info = Invoke-PackageProgram $binary @('--request',(Join-Path $root 'examples/request.json')) $root $audit $deadline 10
    if ($info.ExitCode -ne 0) { throw 'Build metadata readback failed.' }
    $metadata = $info.Stdout | ConvertFrom-Json
    if ($metadata.build_revision -cne $revision.Stdout.Trim() -or $metadata.build_dirty -ne $expectedDirty) { throw 'Embedded source state differs from the independently observed checkout.' }
    $results += [ordered]@{iteration=$iteration+1;sha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant();build=$metadata}
}
$passed = $results[0].sha256 -ceq $results[1].sha256
$receipt = [ordered]@{status=if($passed){'PASS'}else{'FAIL'};target=$target;boundary='Two builds in the same checkout, path, compiler and environment. Cross-toolchain and independent-builder reproducibility are not claimed.';builds=$results}
[IO.File]::WriteAllText((Join-Path $out ($target + '.json')),($receipt | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$receipt | ConvertTo-Json -Depth 8
if (-not $passed) { throw 'Executable hashes differ between the two fixed-input builds.' }
