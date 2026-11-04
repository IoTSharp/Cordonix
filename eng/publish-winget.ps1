#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$')][string]$Version,
    [switch]$Submit,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9-]{0,38}$')][string]$ForkOwner = 'maikebing'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
. (Join-Path $PSScriptRoot 'package.ps1') -Version $Version
$root = Split-Path -Parent $PSScriptRoot
$deadline = [datetime]::UtcNow.AddSeconds(240)
$gh = (Get-Command gh -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$output = Join-Path $root "build/winget/$Version"
[void][IO.Directory]::CreateDirectory($output)
$audit = Join-Path $output ('processes-' + [guid]::NewGuid().ToString('N') + '.jsonl')
$apiSequence = 0

function Invoke-WingetApi {
    param([string]$Method, [string]$Endpoint, $Body)
    $arguments = @('api', '--method', $Method, $Endpoint)
    if ($null -ne $Body) {
        $script:apiSequence++
        $inputPath = Join-Path $output ("api-input-$script:apiSequence.json")
        Write-PackageText $inputPath ($Body | ConvertTo-Json -Depth 12)
        $arguments += @('--input', $inputPath)
    }
    $result = Invoke-PackageProgram $gh $arguments $root $audit $deadline 25
    if ($result.ExitCode -ne 0) { throw ('GitHub API operation failed; inspect remote state before retrying a mutation: ' + $result.Stderr) }
    return $result.Stdout | ConvertFrom-Json
}

$releaseResult = Invoke-PackageProgram $gh @('release','view',"v$Version",'--repo','IoTSharp/Cordonix','--json','tagName,isDraft,isPrerelease,publishedAt,assets') $root $audit $deadline 25
if ($releaseResult.ExitCode -ne 0) { throw ('Cannot read the published release: ' + $releaseResult.Stderr) }
$release = $releaseResult.Stdout | ConvertFrom-Json
$archiveName = "cordonix-$Version-win-x64.zip"
if ($release.tagName -cne "v$Version" -or $release.isDraft -or $release.isPrerelease -or
    @($release.assets | Where-Object name -CEQ $archiveName).Count -ne 1 -or
    @($release.assets | Where-Object name -CEQ ($archiveName + '.sha256')).Count -ne 1) {
    throw 'Expected one published stable Windows archive and checksum.'
}
$download = Invoke-PackageProgram $gh @('release','download',"v$Version",'--repo','IoTSharp/Cordonix','--pattern',$archiveName,'--pattern',($archiveName + '.sha256'),'--dir',$output,'--clobber') $root $audit $deadline 45
if ($download.ExitCode -ne 0) { throw ('Release download failed: ' + $download.Stderr) }
$archivePath = Join-Path $output $archiveName
$checksum = [IO.File]::ReadAllText(($archivePath + '.sha256'), [Text.UTF8Encoding]::new($false, $true)).Trim()
$checksumPattern = '^([0-9a-fA-F]{64})  ' + [regex]::Escape($archiveName) + '$'
if ($checksum -cnotmatch $checksumPattern) { throw 'Invalid archive checksum sidecar.' }
$expectedHash = $Matches[1]
$sha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
if ($sha256 -ine $expectedHash) { throw 'Published archive does not match its checksum.' }
$zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
try { if (@($zip.Entries | Where-Object FullName -CEQ 'cdx.exe').Count -ne 1) { throw 'The portable archive must contain one root cdx.exe.' } }
finally { $zip.Dispose() }
$manifestDirectory = Join-Path $output 'manifests'
[void][IO.Directory]::CreateDirectory($manifestDirectory)
$releaseDate = ([datetimeoffset]$release.publishedAt).UtcDateTime.ToString('yyyy-MM-dd')
$templates = @('IoTSharp.Cordonix.yaml.in','IoTSharp.Cordonix.installer.yaml.in','IoTSharp.Cordonix.locale.en-US.yaml.in')
$files = @()
foreach ($template in $templates) {
    Assert-PackageBudget $deadline
    $content = [IO.File]::ReadAllText((Join-Path $root "install/winget/$template"), [Text.UTF8Encoding]::new($false, $true))
    $content = $content.Replace('@@VERSION@@', $Version).Replace('@@SHA256@@', $sha256).Replace('@@RELEASE_DATE@@', $releaseDate)
    $name = $template.Substring(0, $template.Length - 3)
    $path = Join-Path $manifestDirectory $name
    Write-PackageText $path $content
    $files += [ordered]@{ name = $name; path = $path; content = $content }
}
$receipt = [ordered]@{ status = 'PASS'; version = $Version; archive_sha256 = $sha256; manifest_directory = $manifestDirectory; submitted = $false; pull_request = $null }

if ($Submit) {
    $branch = "codex/cordonix-$Version"
    $existing = @(Invoke-WingetApi GET "repos/microsoft/winget-pkgs/pulls?head=${ForkOwner}:$branch&state=all&per_page=1" $null)
    if ($existing.Count -gt 0) {
        $receipt.submitted = $true
        $receipt.pull_request = $existing[0].html_url
    } else {
        $refs = @(Invoke-WingetApi GET "repos/$ForkOwner/winget-pkgs/git/matching-refs/heads/$branch" $null | Where-Object ref -CEQ "refs/heads/$branch")
        if ($refs.Count -eq 0) {
            $upstreamRef = Invoke-WingetApi GET 'repos/microsoft/winget-pkgs/git/ref/heads/master' $null
            $upstreamCommit = Invoke-WingetApi GET ("repos/microsoft/winget-pkgs/git/commits/" + $upstreamRef.object.sha) $null
            $treeEntries = @($files | ForEach-Object { [ordered]@{ path = "manifests/i/IoTSharp/Cordonix/$Version/" + $_.name; mode = '100644'; type = 'blob'; content = $_.content } })
            $tree = Invoke-WingetApi POST "repos/$ForkOwner/winget-pkgs/git/trees" @{ base_tree = $upstreamCommit.tree.sha; tree = $treeEntries }
            $commit = Invoke-WingetApi POST "repos/$ForkOwner/winget-pkgs/git/commits" @{ message = "Add IoTSharp.Cordonix version $Version"; tree = $tree.sha; parents = @($upstreamRef.object.sha) }
            $null = Invoke-WingetApi POST "repos/$ForkOwner/winget-pkgs/git/refs" @{ ref = "refs/heads/$branch"; sha = $commit.sha }
        }
        $body = "Adds the published Cordonix Windows x64 portable package. The archive SHA-256 was calculated from the actual release download and checked against its published checksum.`n`nRelease: https://github.com/IoTSharp/Cordonix/releases/tag/v$Version`n`nThe cdx command is installed as a portable application. MCP client configuration remains a separate user action."
        $pull = Invoke-WingetApi POST 'repos/microsoft/winget-pkgs/pulls' @{ title = "Update: IoTSharp.Cordonix to $Version"; head = "${ForkOwner}:$branch"; base = 'master'; body = $body }
        $receipt.submitted = $true
        $receipt.pull_request = $pull.html_url
    }
}
Write-PackageText (Join-Path $output 'receipt.json') ($receipt | ConvertTo-Json -Depth 5)
$receipt | ConvertTo-Json -Depth 5
