#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Destination, [uri]$Proxy, [string]$ArchivePath)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
. (Join-Path $PSScriptRoot 'package.ps1')
$deadline = [DateTime]::UtcNow.AddSeconds(120)
$Destination = Get-PackagePath $Destination
if (-not [IO.Path]::IsPathFullyQualified($Destination) -or (Test-Path -LiteralPath $Destination)) { throw 'Use a new, absolute task-owned tool directory.' }
$name = if ($IsWindows) { 'ripgrep-15.2.0-x86_64-pc-windows-msvc' } else { 'ripgrep-15.2.0-x86_64-unknown-linux-musl' }
$suffix = if ($IsWindows) { '.zip' } else { '.tar.gz' }
$expected = if ($IsWindows) { '71b2fef860abe467217a538ff31de02f5258807c0129f771846f87bd029aafc5' } else { '33e15bcf1624b25cdd2a55813a47a2f95dbe126268203e76aa6a585d1e7b149c' }
$uri = 'https://github.com/BurntSushi/ripgrep/releases/download/15.2.0/' + $name + $suffix
[void][IO.Directory]::CreateDirectory($Destination)
$archive = Join-Path $Destination ($name + $suffix)
if ($ArchivePath) { Copy-Item -LiteralPath (Get-PackagePath $ArchivePath -MustExist) -Destination $archive }
else {
    $curl = (Get-Command curl -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $arguments = @('--fail','--location','--silent','--show-error','--connect-timeout','8','--max-time','20','--retry','2','--retry-delay','2','--retry-max-time','60','--output',$archive,$uri)
    if ($Proxy) { $arguments = @('--proxy',$Proxy.AbsoluteUri) + $arguments }
    $download = Invoke-PackageProgram $curl $arguments $Destination (Join-Path $Destination 'processes.jsonl') $deadline 70
    if ($download.ExitCode -ne 0) { throw ('ripgrep download failed: ' + $download.Stderr) }
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -cne $expected) { throw 'ripgrep archive does not match the reviewed upstream SHA-256.' }
Assert-PackageBudget $deadline
if ($IsWindows) { [IO.Compression.ZipFile]::ExtractToDirectory($archive, $Destination) }
else {
    $extracted = Invoke-PackageProgram '/usr/bin/tar' @('-xzf',$archive,'-C',$Destination) $Destination (Join-Path $Destination 'processes.jsonl') $deadline 20
    if ($extracted.ExitCode -ne 0) { throw $extracted.Stderr }
}
$directory = Join-Path $Destination $name
$binary = Join-Path $directory $(if ($IsWindows) {'rg.exe'} else {'rg'})
$versionReadback = Invoke-PackageProgram $binary @('--version') $Destination (Join-Path $Destination 'processes.jsonl') $deadline 10
if ($versionReadback.ExitCode -ne 0 -or -not $versionReadback.Stdout.StartsWith('ripgrep 15.2.0 ')) { throw 'Downloaded rg version differs.' }
if ($env:GITHUB_PATH) { [IO.File]::AppendAllText($env:GITHUB_PATH, $directory + [Environment]::NewLine, [Text.UTF8Encoding]::new($false)) }
$receipt = [ordered]@{status='PASS';version='15.2.0';source=$uri;archive_sha256=$expected;program=$binary}
[IO.File]::WriteAllText((Join-Path $Destination 'installation.json'),($receipt | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
$receipt | ConvertTo-Json
