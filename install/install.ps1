#requires -Version 7.0
# Cordonix updater generation: 3
[CmdletBinding()]
param(
    [string]$InstallRoot = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Cordonix'),
    [ValidateSet('auto', 'github', 'gitee')][string]$Source = 'auto',
    [switch]$CheckOnly,
    [switch]$EnableAutoUpdate,
    [switch]$DisableAutoUpdate,
    [switch]$TaskDefinitionOnly,
    [switch]$ScheduleDryRun,
    [switch]$Automatic,
    [switch]$StartupCheck,
    [ValidateRange(1, 150)][int]$TimeBudgetSeconds = 150,
    [uri]$Proxy,
    [uri]$GitHubApi = 'https://api.github.com/repos/IoTSharp/Cordonix/releases/latest',
    [uri]$GiteeApi = 'https://gitee.com/api/v5/repos/IoTSharp/Cordonix/releases?page=1&per_page=20'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$deadline = [datetime]::UtcNow.AddSeconds($TimeBudgetSeconds)
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$stablePattern = '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'
$taskRoot = $null
$http = $null
$updateLock = $null
$recordAttempt = $false
$sourceDeadline = $deadline

function Assert-Budget {
    if ([datetime]::UtcNow -ge $deadline) { throw 'Installation exceeded its time budget.' }
}

function Read-Download {
    param([uri]$Uri, [long]$MaximumBytes, [string]$Path)
    Assert-Budget
    if ($Uri.Scheme -ne 'https' -and -not ($Uri.Scheme -eq 'http' -and $Uri.IsLoopback)) { throw 'Downloads require HTTPS; HTTP is allowed only for loopback tests.' }
    $remaining = [Math]::Min(20000, [Math]::Max(1, ($sourceDeadline - [datetime]::UtcNow).TotalMilliseconds))
    $cancel = [Threading.CancellationTokenSource]::new([int]$remaining)
    $response = $null; $inputStream = $null; $outputStream = $null
    try {
        $response = $http.GetAsync($Uri, [Net.Http.HttpCompletionOption]::ResponseHeadersRead, $cancel.Token).GetAwaiter().GetResult()
        [void]$response.EnsureSuccessStatusCode()
        if ($response.Content.Headers.ContentLength -gt $MaximumBytes) { throw 'Download exceeds its byte limit.' }
        $inputStream = $response.Content.ReadAsStreamAsync($cancel.Token).GetAwaiter().GetResult()
        $outputStream = if ($Path) { [IO.File]::Open($Path, [IO.FileMode]::CreateNew) } else { [IO.MemoryStream]::new() }
        $buffer = [byte[]]::new(65536)
        $total = 0L; $complete = $false
        $maxReads = [int]([Math]::Ceiling($MaximumBytes / 65536.0) + 2)
        for ($index = 0; $index -lt $maxReads; $index++) {
            Assert-Budget
            $read = $inputStream.ReadAsync($buffer, 0, $buffer.Length, $cancel.Token).GetAwaiter().GetResult()
            if ($read -eq 0) { $complete = $true; break }
            $total += $read
            if ($total -gt $MaximumBytes) { throw 'Download exceeds its byte limit.' }
            $outputStream.Write($buffer, 0, $read)
        }
        if (-not $complete -or ($null -ne $response.Content.Headers.ContentLength -and $total -ne $response.Content.Headers.ContentLength)) { throw 'Download is incomplete.' }
        if (-not $Path) { return $utf8.GetString($outputStream.ToArray()) }
    } finally {
        if ($outputStream) { $outputStream.Dispose() }
        if ($inputStream) { $inputStream.Dispose() }
        if ($response) { $response.Dispose() }
        $cancel.Dispose()
    }
}

function Invoke-InstalledProgram {
    param([string]$Program, [string[]]$Arguments)
    Assert-Budget
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = $Program
    $process.StartInfo.WorkingDirectory = Split-Path -Parent $Program
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) { [void]$process.StartInfo.ArgumentList.Add($argument) }
    $started = $false; $audit = $null
    try {
        $started = $process.Start()
        if (-not $started) { throw 'Could not start the downloaded runtime.' }
        $audit = [ordered]@{ pid = $process.Id; started_utc = $process.StartTime.ToUniversalTime().ToString('o'); parent_pid = $PID; program = $Program; args = $Arguments; cleanup_complete = $false }
        [IO.File]::AppendAllText((Join-Path $taskRoot 'processes.jsonl'), (($audit | ConvertTo-Json -Compress) + "`n"), $utf8)
        $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync()
        $remaining = [int][Math]::Min(10000, [Math]::Max(1, ($deadline - [datetime]::UtcNow).TotalMilliseconds))
        if (-not $process.WaitForExit($remaining)) { throw 'Downloaded runtime validation timed out.' }
        if (-not $stdout.Wait(1000) -or -not $stderr.Wait(1000)) { throw 'Downloaded runtime output did not close.' }
        if ($process.ExitCode -ne 0 -or $stdout.Result.Length + $stderr.Result.Length -gt 1048576) { throw 'Downloaded runtime validation failed.' }
        return $stdout.Result.Trim()
    } finally {
        if ($started -and -not $process.HasExited) { $process.Kill($true); [void]$process.WaitForExit(3000) }
        if ($audit) {
            $audit.cleanup_complete = $process.HasExited
            [IO.File]::AppendAllText((Join-Path $taskRoot 'processes.jsonl'), (($audit | ConvertTo-Json -Compress) + "`n"), $utf8)
        }
        $process.Dispose()
    }
}

function Write-AtomicText {
    param([string]$Path, [string]$Text)
    $temporary = Join-Path (Split-Path -Parent $Path) ('.cdx-' + [guid]::NewGuid().ToString('N'))
    try {
        [IO.File]::WriteAllText($temporary, $Text, $utf8)
        [IO.File]::Move($temporary, $Path, $true)
    } finally { if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
}

function Get-UpdateTaskXml {
    param([string]$TaskProgram, [string]$UpdaterPath, [string]$Root)
    $escape = { param($Text) [Security.SecurityElement]::Escape($Text) }
    $arguments = '-NoLogo -NoProfile -NonInteractive -File "' + $UpdaterPath + '" -InstallRoot "' + $Root + '" -Automatic'
    $user = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $boundary = [datetime]::Now.AddMinutes(1).ToString('yyyy-MM-ddTHH:mm:ss')
    return @"
<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo><Description>Cordonix user update check; keeps previous versions.</Description></RegistrationInfo>
  <Triggers><LogonTrigger><UserId>$user</UserId><Delay>PT1M</Delay><Enabled>true</Enabled></LogonTrigger><TimeTrigger><Repetition><Interval>P1D</Interval><StopAtDurationEnd>false</StopAtDurationEnd></Repetition><StartBoundary>$boundary</StartBoundary><Enabled>true</Enabled></TimeTrigger></Triggers>
  <Principals><Principal id="Author"><UserId>$user</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>
  <Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><StartWhenAvailable>true</StartWhenAvailable><RunOnlyIfNetworkAvailable>true</RunOnlyIfNetworkAvailable><ExecutionTimeLimit>PT3M</ExecutionTimeLimit><Enabled>true</Enabled></Settings>
  <Actions Context="Author"><Exec><Command>$(& $escape $TaskProgram)</Command><Arguments>$(& $escape $arguments)</Arguments><WorkingDirectory>$(& $escape $Root)</WorkingDirectory></Exec></Actions>
</Task>
"@
}

function Get-ExistingUpdateTask {
    param($Folder, [string]$Name)
    try { return $Folder.GetTask($Name) }
    catch {
        $exception = if ($_.Exception.InnerException) { $_.Exception.InnerException } else { $_.Exception }
        if ($exception.HResult -ne -2147024894) { throw }
    }
}

try {
    if (-not $IsWindows -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne [Runtime.InteropServices.Architecture]::X64) { throw 'This installer supports Windows x64 only.' }
    if (-not [IO.Path]::IsPathFullyQualified($InstallRoot) -or $InstallRoot -match '[\x00\r\n"`]|\$\(') { throw 'InstallRoot must be an absolute literal directory.' }
    $InstallRoot = [IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
    if (($EnableAutoUpdate -and $DisableAutoUpdate) -or ($CheckOnly -and ($EnableAutoUpdate -or $DisableAutoUpdate))) { throw 'CheckOnly, EnableAutoUpdate and DisableAutoUpdate cannot conflict.' }
    foreach ($endpoint in @($GitHubApi, $GiteeApi)) {
        if ($endpoint.Scheme -ne 'https' -and -not ($endpoint.Scheme -eq 'http' -and $endpoint.IsLoopback)) { throw 'Release endpoints require HTTPS or loopback HTTP for tests.' }
    }
    $taskHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($utf8.GetBytes($InstallRoot.ToLowerInvariant()))).Substring(0, 12)
    $taskName = 'CordonixUpdate-' + $taskHash
    $taskProgram = Join-Path $PSHOME 'pwsh.exe'
    $updaterPath = Join-Path $InstallRoot 'update.ps1'
    $taskXml = Get-UpdateTaskXml $taskProgram $updaterPath $InstallRoot
    if ($TaskDefinitionOnly) {
        [ordered]@{ status = 'PASS'; operation = 'task_definition'; task_name = $taskName; task_xml = $taskXml; registered = $false } | ConvertTo-Json -Depth 5
        return
    }
    $settingsPath = Join-Path $InstallRoot 'update-settings.json'
    $attemptPath = Join-Path $InstallRoot 'last-update-check.json'
    $settings = if ([IO.File]::Exists($settingsPath)) { [IO.File]::ReadAllText($settingsPath, $utf8) | ConvertFrom-Json -AsHashtable } else { @{ enabled = $true; source = $Source; proxy = if ($Proxy) { $Proxy.AbsoluteUri } else { $null }; github_api = $GitHubApi.AbsoluteUri; gitee_api = $GiteeApi.AbsoluteUri } }
    if ($Automatic) {
        if (-not $settings.enabled) { return }
        $Source = $settings.source
        $Proxy = if ($settings.proxy) { [uri]$settings.proxy } else { $null }
        $GitHubApi = [uri]$settings.github_api
        $GiteeApi = [uri]$settings.gitee_api
    }
    if (-not $CheckOnly) {
        [void][IO.Directory]::CreateDirectory($InstallRoot)
        try { $updateLock = [IO.File]::Open((Join-Path $InstallRoot '.update.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
        catch [IO.IOException] { if ($Automatic) { return }; throw }
        if ($Automatic -and $StartupCheck -and [IO.File]::Exists($attemptPath)) {
            $last = [IO.File]::ReadAllText($attemptPath, $utf8) | ConvertFrom-Json
            if (([datetime]$last.checked_utc).ToLocalTime().Date -eq [datetime]::Now.Date) { return }
        }
    }
    if ($DisableAutoUpdate) {
        $settings.enabled = $false
        Write-AtomicText $settingsPath ($settings | ConvertTo-Json -Compress)
        if ($ScheduleDryRun) {
            [ordered]@{ status = 'PASS'; operation = 'disable_auto_update'; registered = $false; schedule = 'NOT_RUN' } | ConvertTo-Json -Compress
            return
        }
        $scheduler = New-Object -ComObject 'Schedule.Service'
        $scheduler.Connect(); $folder = $scheduler.GetFolder('\')
        $task = Get-ExistingUpdateTask $folder $taskName
        if ($task) {
            $action = $task.Definition.Actions.Item(1)
            if ($action.Path -cne $taskProgram -or $action.Arguments -notlike ('*"' + $updaterPath + '"*')) { throw 'The existing task does not belong to this installation.' }
            $folder.DeleteTask($taskName, 0)
        }
        [ordered]@{ status = 'PASS'; operation = 'disable_auto_update'; task_name = $taskName; registered = $false } | ConvertTo-Json -Compress
        return
    }
    if ($EnableAutoUpdate) { $settings.enabled = $true }
    if (-not $Automatic -and -not $CheckOnly) {
        $settings.source = $Source
        $settings.proxy = if ($Proxy) { $Proxy.AbsoluteUri } else { $null }
        $settings.github_api = $GitHubApi.AbsoluteUri
        $settings.gitee_api = $GiteeApi.AbsoluteUri
    }
    if (-not $CheckOnly) {
        $recordAttempt = $true
        Write-AtomicText $attemptPath (@{ status = 'checking'; checked_utc = [datetime]::UtcNow.ToString('o') } | ConvertTo-Json -Compress)
    }
    $previous = $null
    $currentPath = Join-Path $InstallRoot 'current.json'
    if ([IO.File]::Exists($currentPath)) {
        $previous = [IO.File]::ReadAllText($currentPath, $utf8) | ConvertFrom-Json
        if ('v' + $previous.version -notmatch $stablePattern) { throw 'The installed version pointer is invalid.' }
    }
    $handler = [Net.Http.HttpClientHandler]::new()
    if ($Proxy) { $handler.Proxy = [Net.WebProxy]::new($Proxy); $handler.UseProxy = $true }
    $http = [Net.Http.HttpClient]::new($handler)
    $http.DefaultRequestHeaders.UserAgent.ParseAdd('Cordonix-Installer/1.0')
    $http.Timeout = [Threading.Timeout]::InfiniteTimeSpan
    $sources = if ($Source -eq 'auto') { @('github', 'gitee') } else { @($Source) }
    $release = $null; $selected = $null; $archive = $null; $checksum = $null
    $sourceErrors = [Collections.Generic.List[string]]::new()
    foreach ($candidate in $sources) {
        try {
            $sourceDeadline = if ($Automatic -and $Source -eq 'auto' -and $candidate -eq 'github') { [datetime]::UtcNow.AddMilliseconds([Math]::Max(1, ($deadline - [datetime]::UtcNow).TotalMilliseconds / 2)) } else { $deadline }
            $uri = if ($candidate -eq 'github') { $GitHubApi } else { $GiteeApi }
            $releases = @(Read-Download $uri 1048576 | ConvertFrom-Json)
            if ($releases.Count -gt 100) { throw 'Release metadata contains more than 100 releases.' }
            $eligible = @($releases | Where-Object { $_.tag_name -cmatch $stablePattern -and -not $_.prerelease -and -not $_.draft } | Sort-Object { [version]$_.tag_name.Substring(1) } -Descending)
            if ($eligible.Count -eq 0) { throw 'No stable release is available.' }
            $release = $eligible[0]
            $version = $release.tag_name.Substring(1)
            $assetName = "cordonix-$version-win-x64.zip"
            $archive = @($release.assets | Where-Object name -CEQ $assetName)
            $checksum = @($release.assets | Where-Object name -CEQ ($assetName + '.sha256'))
            if ($archive.Count -ne 1 -or $checksum.Count -ne 1) { throw 'The exact archive and checksum assets are required.' }
            $operation = if (-not $previous) { 'install' } elseif ([version]$previous.version -eq [version]$version) { 'up_to_date' } elseif ([version]$previous.version -gt [version]$version) { 'older_release' } else { 'update' }
            if (-not $CheckOnly -and $operation -in @('install', 'update')) {
                if (-not $taskRoot) {
                    [void][IO.Directory]::CreateDirectory($InstallRoot)
                    $taskRoot = Join-Path $InstallRoot ('.install-' + [guid]::NewGuid().ToString('N'))
                    [void][IO.Directory]::CreateDirectory($taskRoot)
                }
                $archivePath = Join-Path $taskRoot ($candidate + '-' + $assetName)
                $checksumText = Read-Download ([uri]$checksum[0].browser_download_url) 4096
                $checksumPattern = '^([0-9a-fA-F]{64})  ' + [regex]::Escape($assetName) + '\r?\n?$'
                if ($checksumText -cnotmatch $checksumPattern) { throw [Security.SecurityException]::new('The checksum file has an invalid hash or filename.') }
                $expectedHash = $Matches[1].ToLowerInvariant()
                Read-Download ([uri]$archive[0].browser_download_url) 268435456 $archivePath
                if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $expectedHash) { throw [Security.SecurityException]::new('Archive SHA-256 mismatch; the previous installation is intact.') }
            }
            $selected = $candidate
            break
        } catch [Security.SecurityException] { throw }
        catch { $sourceErrors.Add($candidate + ': ' + $_.Exception.Message) }
    }
    if (-not $selected) { throw ($sourceErrors -join '; ') }
    if ($CheckOnly) {
        [ordered]@{ status = 'PASS'; operation = $operation; source = $selected; tag = $release.tag_name; previous_version = $previous.version; available_version = $version; changed = $false } | ConvertTo-Json -Compress
        return
    }
    if ($operation -in @('install', 'update')) {
        $staging = Join-Path $taskRoot 'version'
        [void][IO.Directory]::CreateDirectory($staging)
        $zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
        try {
            if ($zip.Entries.Count -gt 100) { throw 'Archive has too many entries.' }
            $totalBytes = 0L
            foreach ($entry in $zip.Entries) {
                Assert-Budget
                $destination = [IO.Path]::GetFullPath((Join-Path $staging $entry.FullName))
                if (-not $destination.StartsWith($staging + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Archive entry escapes the version directory.' }
                $totalBytes += $entry.Length
                if ($totalBytes -gt 268435456) { throw 'Expanded archive exceeds 256 MiB.' }
            }
        } finally { $zip.Dispose() }
        [IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $staging)
        $binary = Join-Path $staging 'cdx.exe'
        if ((Invoke-InstalledProgram $binary @('--version')) -cne "Cordonix cdx $version") { throw 'Downloaded executable version differs from the release tag.' }
        $requestPath = Join-Path $taskRoot 'tool-info.json'
        [IO.File]::WriteAllText($requestPath, '{"operation":"tool_info","arguments":{}}', $utf8)
        $info = Invoke-InstalledProgram $binary @('--request', $requestPath) | ConvertFrom-Json
        if ($info.status -cne 'ok' -or $info.name -cne 'Cordonix' -or $info.version -cne $version -or $info.platform -cne 'windows' -or $info.runtime -cne 'C11') { throw 'Downloaded executable tool_info validation failed.' }
        $versions = Join-Path $InstallRoot 'versions'
        [void][IO.Directory]::CreateDirectory($versions)
        $versionPath = Join-Path $versions $version
        if ([IO.Directory]::Exists($versionPath)) { throw 'The candidate version directory already exists; inspect it before retrying.' }
        for ($publishAttempt = 0; $publishAttempt -lt 6; $publishAttempt++) {
            Assert-Budget
            try { [IO.Directory]::Move($staging, $versionPath); break }
            catch {
                $cause = $_.Exception.InnerException
                if ($publishAttempt -eq 5 -or -not $cause -or ($cause.HResult -band 0xffff) -notin @(5, 32) -or -not [IO.Directory]::Exists($staging) -or [IO.Directory]::Exists($versionPath)) { throw }
                Start-Sleep -Milliseconds 200
            }
        }
        $state = [ordered]@{ format_version = 1; version = $version; tag = $release.tag_name; source = $selected; asset = $assetName; sha256 = $expectedHash; installed_utc = [datetime]::UtcNow.ToString('o'); supports_checkupgrade = $info.update_command -ceq '--checkupgrade' }
        Write-AtomicText $currentPath ($state | ConvertTo-Json -Compress)
    }
    [void][IO.Directory]::CreateDirectory($InstallRoot)
    $launcher = @'
#requires -Version 7.0
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$CdxArguments)
$ErrorActionPreference = 'Stop'
function Start-DailyUpgrade {
    $lease = $null
    try {
        $settings = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'update-settings.json')) | ConvertFrom-Json
        if (-not $settings.enabled) { return }
        try { $lease = [IO.File]::Open((Join-Path $PSScriptRoot '.update-launch.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
        catch [IO.IOException] { return }
        foreach ($name in @('last-update-check.json', 'last-update-launch.json')) {
            $path = Join-Path $PSScriptRoot $name
            if ([IO.File]::Exists($path)) {
                $record = [IO.File]::ReadAllText($path) | ConvertFrom-Json
                $when = if ($name -eq 'last-update-check.json') { $record.checked_utc } else { $record.started_utc }
                if (([datetime]$when).ToLocalTime().Date -eq [datetime]::Now.Date) { return }
            }
        }
        $pointer = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'current.json')) | ConvertFrom-Json
        if ($pointer.version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw 'Invalid Cordonix current version.' }
        $program = if ($pointer.supports_checkupgrade) { Join-Path $PSScriptRoot ('versions/' + $pointer.version + '/cdx.exe') } else { Join-Path $PSHOME 'pwsh.exe' }
        $arguments = if ($pointer.supports_checkupgrade) { '--checkupgrade' } else { '-NoLogo -NoProfile -NonInteractive -File "' + (Join-Path $PSScriptRoot 'update.ps1') + '" -InstallRoot "' + $PSScriptRoot + '" -Automatic -StartupCheck' }
        $worker = Start-Process -FilePath $program -ArgumentList $arguments -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru
        try {
            $audit = @{ pid = $worker.Id; parent_pid = $PID; started_utc = $worker.StartTime.ToUniversalTime().ToString('o'); program = $program; arguments = $arguments }
            $temporary = Join-Path $PSScriptRoot 'last-update-launch.new'
            [IO.File]::WriteAllText($temporary, ($audit | ConvertTo-Json -Compress), [Text.UTF8Encoding]::new($false))
            [IO.File]::Move($temporary, (Join-Path $PSScriptRoot 'last-update-launch.json'), $true)
        } finally { $worker.Dispose() }
    } catch { [Console]::Error.WriteLine('Cordonix could not start its background update check; using the installed version.') }
    finally { if ($lease) { $lease.Dispose() } }
}
Start-DailyUpgrade
$current = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'current.json')) | ConvertFrom-Json
if ($current.version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw 'Invalid Cordonix current version.' }
$binary = Join-Path $PSScriptRoot ('versions/' + $current.version + '/cdx.exe')
if (-not $CdxArguments -or $CdxArguments.Count -eq 0) { $CdxArguments = @('--mcp') }
& $binary @CdxArguments
exit $LASTEXITCODE
'@
    Write-AtomicText (Join-Path $InstallRoot 'launch.ps1') $launcher
    $updaterText = [IO.File]::ReadAllText($PSCommandPath, $utf8)
    $installedVersion = if ($operation -eq 'older_release') { $previous.version } else { $version }
    $packagedUpdater = Join-Path $InstallRoot ('versions/' + $installedVersion + '/install/install.ps1')
    foreach ($candidateUpdater in @($packagedUpdater, $updaterPath)) {
      if ([IO.File]::Exists($candidateUpdater)) {
        $packagedText = [IO.File]::ReadAllText($candidateUpdater, $utf8)
        $pattern = '(?m)^# Cordonix updater generation: ([0-9]+)\r?$'
        $generation = [regex]::Match($updaterText, $pattern)
        $packagedGeneration = [regex]::Match($packagedText, $pattern)
        if ($packagedGeneration.Success -and [int]$packagedGeneration.Groups[1].Value -gt [int]$generation.Groups[1].Value) { $updaterText = $packagedText }
      }
    }
    Write-AtomicText $updaterPath $updaterText
    Write-AtomicText $settingsPath ($settings | ConvertTo-Json -Compress)
    $updateRequest = @{ operation = 'execute_program'; arguments = @{ program = $taskProgram; args = @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', $updaterPath, '-InstallRoot', $InstallRoot, '-Automatic', '-StartupCheck'); cwd = $InstallRoot; timeout_ms = 170000; output_limit_bytes = 262144; expected_exit_codes = @(0) } }
    Write-AtomicText (Join-Path $InstallRoot 'update-request.json') ($updateRequest | ConvertTo-Json -Depth 5 -Compress)
    $registered = $false
    $scheduleError = $null
    if ($EnableAutoUpdate -and $settings.enabled -and -not $Automatic -and -not $ScheduleDryRun) {
      try {
        if ($GitHubApi.IsLoopback -or $GiteeApi.IsLoopback) { throw 'Loopback test sources cannot be registered for unattended updates.' }
        $scheduler = New-Object -ComObject 'Schedule.Service'
        $scheduler.Connect(); $folder = $scheduler.GetFolder('\')
        $existingTask = Get-ExistingUpdateTask $folder $taskName
        if ($existingTask) {
            $action = $existingTask.Definition.Actions.Item(1)
            if ($action.Path -cne $taskProgram -or $action.Arguments -notlike ('*"' + $updaterPath + '"*')) { throw 'The existing task belongs to another installation.' }
        }
        [void]$folder.RegisterTask($taskName, $taskXml, 6, $null, $null, 3)
        $registered = $true
      } catch { $scheduleError = $_.Exception.Message }
    }
    $receipt = [ordered]@{ status = 'PASS'; operation = $operation; source = $selected; tag = $release.tag_name; previous_version = $previous.version; installed_version = $installedVersion; install_root = $InstallRoot; launcher = Join-Path $InstallRoot 'launch.ps1'; task_name = $taskName; auto_update_enabled = [bool]$settings.enabled; auto_update_registered = $registered; schedule_error = $scheduleError; schedule_registration = if ($Automatic -or $ScheduleDryRun) { 'NOT_RUN' } elseif ($registered) { 'PASS' } elseif ($scheduleError) { 'FAIL' } else { 'not_requested' }; config_updated = $false; source_errors = @($sourceErrors); checked_utc = [datetime]::UtcNow.ToString('o') }
    Write-AtomicText $attemptPath ($receipt | ConvertTo-Json -Depth 5 -Compress)
    $receipt | ConvertTo-Json -Depth 5
} catch {
    if ($recordAttempt) { Write-AtomicText $attemptPath (@{ status = 'FAIL'; checked_utc = [datetime]::UtcNow.ToString('o'); error = $_.Exception.Message } | ConvertTo-Json -Compress) }
    throw
} finally {
    if ($updateLock) { $updateLock.Dispose() }
    if ($http) { $http.Dispose() }
    if ($taskRoot -and [IO.Path]::GetFullPath($taskRoot).StartsWith($InstallRoot + '\.install-', [StringComparison]::OrdinalIgnoreCase) -and [IO.Directory]::Exists($taskRoot)) { Remove-Item -LiteralPath $taskRoot -Recurse -Force }
}
