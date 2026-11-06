#requires -Version 7.0
[CmdletBinding()]
param([string]$SourceRoot = (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$utf8 = [Text.UTF8Encoding]::new($false)
$deadline = [datetime]::UtcNow.AddSeconds(120)
$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$testBase = Join-Path $SourceRoot 'build/install-windows-tests'
[void][IO.Directory]::CreateDirectory($testBase)
$taskRoot = Join-Path $testBase ([guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($taskRoot)
$server = $null
$results = [Collections.Generic.List[object]]::new()

function Assert-Case {
    param([string]$Name, [bool]$Passed)
    $results.Add([ordered]@{ name = $Name; passed = $Passed })
    if (-not $Passed) { throw "FAIL: $Name" }
}

function Invoke-Installer {
    param([string]$Root, [string]$GitHubRoute = '/github', [string]$GiteeRoute = '/gitee', [switch]$Check)
    if ([datetime]::UtcNow -ge $deadline) { throw 'Installer fixture exceeded its deadline.' }
    $parameters = @{ InstallRoot = $Root; GitHubApi = $serverUrl + $GitHubRoute; GiteeApi = $serverUrl + $GiteeRoute; ScheduleDryRun = $true }
    if ($Check) { $parameters.CheckOnly = $true }
    return (& (Join-Path $SourceRoot 'install/install.ps1') @parameters | ConvertFrom-Json)
}

function Invoke-Launcher {
    param([string]$Root, [switch]$NoWait)
    if ([datetime]::UtcNow -ge $deadline) { throw 'Launcher fixture exceeded its deadline.' }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = Join-Path $PSHOME 'pwsh.exe'
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    foreach ($arg in @('-NoLogo','-NoProfile','-File',(Join-Path $Root 'launch.ps1'),'--version')) { [void]$process.StartInfo.ArgumentList.Add($arg) }
    try {
        $watch = [Diagnostics.Stopwatch]::StartNew()
        if (-not $process.Start()) { throw 'Could not start stable launcher.' }
        [IO.File]::AppendAllText((Join-Path $taskRoot 'launchers.jsonl'), ((@{pid=$process.Id; started_utc=$process.StartTime.ToUniversalTime().ToString('o'); parent_pid=$PID; program=$process.StartInfo.FileName; args=@($process.StartInfo.ArgumentList)} | ConvertTo-Json -Compress) + "`n"), $utf8)
        $output = $process.StandardOutput.ReadToEndAsync(); $errors = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(13000)) { throw 'Stable launcher timed out.' }
        if ($process.ExitCode -ne 0 -or $output.Result.Trim() -cne ('Cordonix cdx ' + $binaryVersion)) { throw 'Stable launcher did not forward the installed runtime output.' }
        $script:launchMilliseconds = $watch.ElapsedMilliseconds
        if (-not $NoWait) { Wait-UpgradeWorker $Root }
        return $errors.Result
    } finally {
        if (-not $process.HasExited) { $process.Kill($true); [void]$process.WaitForExit(3000) }
        $process.Dispose()
    }
}

function Wait-UpgradeWorker {
    param([string]$Root)
    $auditPath = Join-Path $Root 'last-update-launch.json'
    if (-not [IO.File]::Exists($auditPath)) { return }
    $audit = [IO.File]::ReadAllText($auditPath) | ConvertFrom-Json
    for ($index = 0; $index -lt 200 -and [datetime]::UtcNow -lt $deadline; $index++) {
        $worker = $null
        try { $worker = [Diagnostics.Process]::GetProcessById($audit.pid) } catch [ArgumentException] { return }
        try {
            if ([Math]::Abs(($worker.StartTime.ToUniversalTime() - [datetime]$audit.started_utc).TotalMilliseconds) -gt 1) { return }
            if ($worker.WaitForExit(50)) { return }
        } finally { $worker.Dispose() }
    }
    throw 'Background upgrade worker did not finish within its fixture deadline.'
}

function Clear-UpgradeAttempt {
    param([string]$Root)
    foreach ($name in @('last-update-check.json','last-update-launch.json')) { [IO.File]::Delete((Join-Path $Root $name)) }
}

try {
    $binary = Join-Path $SourceRoot 'build/bin/cdx.exe'
    if (-not [IO.File]::Exists($binary)) { throw 'Build a Windows executable before the installer gate.' }
    $binaryVersion = (& $binary --version).Substring('Cordonix cdx '.Length).Trim()
    if ($binaryVersion -cnotmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw 'Build with a stable CDX_VERSION, e.g. CDX_VERSION=0.0.1.' }
    $payload = Join-Path $taskRoot 'payload'
    [void][IO.Directory]::CreateDirectory($payload)
    Copy-Item -LiteralPath $binary -Destination (Join-Path $payload 'cdx.exe')
    [void][IO.Directory]::CreateDirectory((Join-Path $payload 'install'))
    $newerUpdater = [IO.File]::ReadAllText((Join-Path $SourceRoot 'install/install.ps1')).Replace('# Cordonix updater generation: 3', '# Cordonix updater generation: 4')
    [IO.File]::WriteAllText((Join-Path $payload 'install/install.ps1'), $newerUpdater, $utf8)
    $assetName = "cordonix-$binaryVersion-win-x64.zip"
    $archive = Join-Path $taskRoot 'archive.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory($payload, $archive)
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    $fixtureData = [ordered]@{ version = $binaryVersion; asset = $assetName; sha256 = $hash }
    [IO.File]::WriteAllText((Join-Path $taskRoot 'fixture.json'), ($fixtureData | ConvertTo-Json -Compress), $utf8)
    $serverScript = Join-Path $taskRoot 'server.ps1'
    $serverText = @'
#requires -Version 7.0
param([string]$Root, [int]$Port)
$ErrorActionPreference = 'Stop'
$data = Get-Content -LiteralPath (Join-Path $Root 'fixture.json') -Raw | ConvertFrom-Json
$listener = [Net.HttpListener]::new()
$base = 'http://127.0.0.1:' + $Port
$listener.Prefixes.Add($base + '/')
$listener.Start()
[IO.File]::WriteAllText((Join-Path $Root 'ready'), 'ready')
$deadline = [datetime]::UtcNow.AddSeconds(110)
try {
    for ($index = 0; $index -lt 200 -and [datetime]::UtcNow -lt $deadline -and -not [IO.File]::Exists((Join-Path $Root 'stop')); $index++) {
        $pending = $listener.GetContextAsync()
        $received = $false
        for ($poll = 0; $poll -lt 220 -and [datetime]::UtcNow -lt $deadline -and -not [IO.File]::Exists((Join-Path $Root 'stop')); $poll++) {
            if ($pending.Wait(500)) { $received = $true; break }
        }
        if (-not $received) { break }
        $context = $pending.Result
        $response = $context.Response
        try {
            $route = $context.Request.Url.AbsolutePath
            if ($route -eq '/slow') { Start-Sleep -Seconds 3; $response.StatusCode = 503; $response.Close(); continue }
            if ($route -eq '/fail') { $response.StatusCode = 503; $response.Close(); continue }
            $version = if ($route -eq '/mismatch') { '999.0.0' } else { $data.version }
            $asset = 'cordonix-' + $version + '-win-x64.zip'
            $archiveRoute = if ($route -eq '/truncated') { '/truncated.zip' } else { '/archive.zip' }
            $checksumRoute = if ($route -eq '/badhash') { '/bad.sha256' } elseif ($route -eq '/mismatch') { '/mismatch.sha256' } else { '/archive.sha256' }
            if ($route -in @('/github','/gitee','/missing','/mismatch','/badhash','/truncated')) {
                $release = [ordered]@{ tag_name = 'v' + $version; draft = $false; prerelease = $false; assets = @(@{name=$asset; browser_download_url=$base + $archiveRoute}, @{name=$asset + '.sha256'; browser_download_url=$base + $checksumRoute}) }
                if ($route -eq '/missing') { $release.assets = @() }
                $text = $release | ConvertTo-Json -Depth 5 -Compress
                if ($route -eq '/gitee') { $text = '[' + $text + ']' }
                $bytes = [Text.Encoding]::UTF8.GetBytes($text)
            } elseif ($route -eq '/archive.zip' -or $route -eq '/truncated.zip') {
                $bytes = [IO.File]::ReadAllBytes((Join-Path $Root 'archive.zip'))
            } elseif ($route -in @('/archive.sha256','/bad.sha256','/mismatch.sha256')) {
                $sha = if ($route -eq '/bad.sha256') { '0' * 64 } else { $data.sha256 }
                $name = if ($route -eq '/mismatch.sha256') { 'cordonix-999.0.0-win-x64.zip' } else { $data.asset }
                $bytes = [Text.Encoding]::UTF8.GetBytes($sha + '  ' + $name + "`n")
            } else { $response.StatusCode = 404; $response.Close(); continue }
            $response.ContentLength64 = $bytes.Length
            if ($route -eq '/truncated.zip') { $response.OutputStream.Write($bytes, 0, 8) }
            else { $response.OutputStream.Write($bytes, 0, $bytes.Length) }
        } finally { try { $response.Close() } catch { $response.Abort() } }
    }
} finally { $listener.Stop(); $listener.Close() }
'@
    [IO.File]::WriteAllText($serverScript, $serverText, $utf8)
    $portProbe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $portProbe.Start(); $port = $portProbe.LocalEndpoint.Port; $portProbe.Stop()
    $serverUrl = 'http://127.0.0.1:' + $port
    $server = [Diagnostics.Process]::new()
    $server.StartInfo.FileName = Join-Path $PSHOME 'pwsh.exe'
    $server.StartInfo.UseShellExecute = $false
    $server.StartInfo.CreateNoWindow = $true
    $server.StartInfo.RedirectStandardOutput = $true
    $server.StartInfo.RedirectStandardError = $true
    foreach ($arg in @('-NoLogo','-NoProfile','-File',$serverScript,'-Root',$taskRoot,'-Port',[string]$port)) { [void]$server.StartInfo.ArgumentList.Add($arg) }
    if (-not $server.Start()) { throw 'Could not start the bounded HTTP fixture.' }
    $serverOut = $server.StandardOutput.ReadToEndAsync(); $serverErr = $server.StandardError.ReadToEndAsync()
    [IO.File]::WriteAllText((Join-Path $taskRoot 'server-process.json'), (@{pid=$server.Id; started_utc=$server.StartTime.ToUniversalTime().ToString('o'); parent_pid=$PID; program=$server.StartInfo.FileName; args=@('-NoLogo','-NoProfile','-File',$serverScript,'-Root',$taskRoot,'-Port',[string]$port)} | ConvertTo-Json -Compress), $utf8)
    for ($pilot = 0; $pilot -lt 20 -and -not [IO.File]::Exists((Join-Path $taskRoot 'ready')); $pilot++) { Start-Sleep -Milliseconds 100 }
    if (-not [IO.File]::Exists((Join-Path $taskRoot 'ready'))) { throw 'HTTP fixture did not become ready.' }
    $root = Join-Path $taskRoot '安装 含空格'
    $result = Invoke-Installer $root '/fail'
    Assert-Case 'GitHub 503 falls back to Gitee and installs actual cdx' ($result.status -ceq 'PASS' -and $result.source -ceq 'gitee' -and $result.operation -ceq 'install' -and $result.installed_version -ceq $binaryVersion)
    Assert-Case 'Automatic updates are enabled by default without registering a test task' ($result.auto_update_enabled -and $result.schedule_registration -ceq 'NOT_RUN' -and -not $result.auto_update_registered)
    Assert-Case 'The verified package refreshes the updater generation' ([IO.File]::ReadAllText((Join-Path $root 'update.ps1')).Contains('# Cordonix updater generation: 4'))
    $pointer = [IO.File]::ReadAllText((Join-Path $root 'current.json'))
    $launcherProcess = [Diagnostics.Process]::new()
    $launcherProcess.StartInfo.FileName = Join-Path $PSHOME 'pwsh.exe'
    $launcherProcess.StartInfo.UseShellExecute = $false
    $launcherProcess.StartInfo.CreateNoWindow = $true
    $launcherProcess.StartInfo.RedirectStandardOutput = $true
    $launcherProcess.StartInfo.RedirectStandardError = $true
    foreach ($arg in @('-NoLogo','-NoProfile','-File',(Join-Path $root 'launch.ps1'),'--version')) { [void]$launcherProcess.StartInfo.ArgumentList.Add($arg) }
    try {
        if (-not $launcherProcess.Start()) { throw 'Could not start stable launcher.' }
        $launcherAudit = @{pid=$launcherProcess.Id; started_utc=$launcherProcess.StartTime.ToUniversalTime().ToString('o'); parent_pid=$PID; program=$launcherProcess.StartInfo.FileName; args=@('-NoLogo','-NoProfile','-File',(Join-Path $root 'launch.ps1'),'--version')}
        [IO.File]::WriteAllText((Join-Path $taskRoot 'launcher-process.json'), ($launcherAudit | ConvertTo-Json -Compress), $utf8)
        $launcherOut = $launcherProcess.StandardOutput.ReadToEndAsync(); $launcherErr = $launcherProcess.StandardError.ReadToEndAsync()
        if (-not $launcherProcess.WaitForExit(10000)) { throw 'Stable launcher timed out.' }
        Assert-Case 'Stable launcher forwards only the runtime output' ($launcherProcess.ExitCode -eq 0 -and $launcherOut.Result.Trim() -ceq ('Cordonix cdx ' + $binaryVersion) -and $launcherErr.Result.Length -eq 0)
    } finally {
        if (-not $launcherProcess.HasExited) { $launcherProcess.Kill($true); [void]$launcherProcess.WaitForExit(3000) }
        $launcherProcess.Dispose()
    }
    $result = Invoke-Installer $root
    Assert-Case 'Same version performs no replacement' ($result.operation -ceq 'up_to_date' -and $pointer -ceq [IO.File]::ReadAllText((Join-Path $root 'current.json')))
    Assert-Case 'An older installer cannot downgrade the installed updater generation' ([IO.File]::ReadAllText((Join-Path $root 'update.ps1')).Contains('# Cordonix updater generation: 4'))
    $result = Invoke-Installer (Join-Path $taskRoot 'metadata-fallback') '/missing'
    Assert-Case 'Missing exact assets fall back to Gitee' ($result.source -ceq 'gitee')
    $result = Invoke-Installer (Join-Path $taskRoot 'truncated-fallback') '/truncated'
    Assert-Case 'Truncated GitHub download falls back to Gitee' ($result.source -ceq 'gitee')
    $updateRoot = Join-Path $taskRoot 'update'
    $oldVersionPath = Join-Path $updateRoot 'versions/0.0.0'
    [void][IO.Directory]::CreateDirectory($oldVersionPath)
    Copy-Item -LiteralPath $binary -Destination (Join-Path $oldVersionPath 'cdx.exe')
    [IO.File]::WriteAllText((Join-Path $updateRoot 'current.json'), '{"version":"0.0.0","format_version":1}', $utf8)
    $result = Invoke-Installer $updateRoot
    Assert-Case 'An update switches the pointer and preserves the previous version directory' ($result.operation -ceq 'update' -and $result.previous_version -ceq '0.0.0' -and $result.installed_version -ceq $binaryVersion -and [IO.File]::Exists((Join-Path $oldVersionPath 'cdx.exe')))
    $checkRoot = Join-Path $taskRoot 'check-only'
    $result = Invoke-Installer $checkRoot -Check
    Assert-Case 'CheckOnly does not create an installation directory' ($result.operation -ceq 'install' -and -not [IO.Directory]::Exists($checkRoot))
    $older = @{version='999.0.0'; format_version=1} | ConvertTo-Json -Compress
    [IO.File]::WriteAllText((Join-Path $root 'current.json'), $older, $utf8)
    $result = Invoke-Installer $root
    Assert-Case 'A mirror with an older version cannot downgrade' ($result.operation -ceq 'older_release' -and $older -ceq [IO.File]::ReadAllText((Join-Path $root 'current.json')))
    [IO.File]::WriteAllText((Join-Path $root 'current.json'), '{"version":"0.0.0","format_version":1}', $utf8)
    $before = [IO.File]::ReadAllText((Join-Path $root 'current.json'))
    $failed = $false
    try { [void](Invoke-Installer $root '/badhash') } catch { $failed = $_.Exception.Message -like '*SHA-256 mismatch*' }
    Assert-Case 'SHA mismatch preserves the previous pointer' ($failed -and $before -ceq [IO.File]::ReadAllText((Join-Path $root 'current.json')))
    $failed = $false
    try { [void](Invoke-Installer $root '/mismatch') } catch { $failed = $_.Exception.Message -like '*executable version differs*' }
    Assert-Case 'Downloaded runtime must match the release tag before switching' ($failed -and $before -ceq [IO.File]::ReadAllText((Join-Path $root 'current.json')))
    [IO.File]::WriteAllText((Join-Path $root 'current.json'), $pointer, $utf8)
    $autoRoot = Join-Path $taskRoot 'startup-update'
    [void](Invoke-Installer $autoRoot '/fail')
    [IO.Directory]::Move((Join-Path $autoRoot ('versions/' + $binaryVersion)), (Join-Path $autoRoot 'versions/0.0.0'))
    [IO.File]::WriteAllText((Join-Path $autoRoot 'current.json'), '{"version":"0.0.0","format_version":1,"supports_checkupgrade":true}', $utf8)
    Clear-UpgradeAttempt $autoRoot
    $errors = Invoke-Launcher $autoRoot
    $attempt = [IO.File]::ReadAllText((Join-Path $autoRoot 'last-update-check.json')) | ConvertFrom-Json
    Assert-Case 'Launching performs an actual verified update through Gitee without updater output on stdout' ($errors.Length -eq 0 -and $attempt.operation -ceq 'update' -and $attempt.source -ceq 'gitee' -and $attempt.installed_version -ceq $binaryVersion)
    $workerAudit = [IO.File]::ReadAllText((Join-Path $autoRoot 'last-update-launch.json')) | ConvertFrom-Json
    Assert-Case 'The separate instance runs the native checkupgrade command' ($workerAudit.program -like '*cdx.exe' -and $workerAudit.arguments -ceq '--checkupgrade')
    $before = [IO.File]::ReadAllText((Join-Path $autoRoot 'last-update-check.json'))
    [void](Invoke-Launcher $autoRoot)
    Assert-Case 'Repeated launches on the same day skip the update check' ($before -ceq [IO.File]::ReadAllText((Join-Path $autoRoot 'last-update-check.json')))
    $yesterday = @{ checked_utc = [datetime]::UtcNow.AddDays(-1).ToString('o'); status = 'PASS' } | ConvertTo-Json -Compress
    [IO.File]::WriteAllText((Join-Path $autoRoot 'last-update-check.json'), $yesterday, $utf8)
    [IO.File]::Delete((Join-Path $autoRoot 'last-update-launch.json'))
    [void](Invoke-Launcher $autoRoot)
    $today = [IO.File]::ReadAllText((Join-Path $autoRoot 'last-update-check.json')) | ConvertFrom-Json
    Assert-Case 'The first launch on a later calendar day starts another check' (([datetime]$today.checked_utc).ToLocalTime().Date -eq [datetime]::Now.Date)
    $held = [IO.File]::Open((Join-Path $autoRoot '.update.lock'), [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try { Clear-UpgradeAttempt $autoRoot; $errors = Invoke-Launcher $autoRoot }
    finally { $held.Dispose() }
    Assert-Case 'Concurrent updater ownership skips the startup check and still launches' ($errors.Length -eq 0 -and -not [IO.File]::Exists((Join-Path $autoRoot 'last-update-check.json')))
    $settingsPath = Join-Path $autoRoot 'update-settings.json'
    $settings = [IO.File]::ReadAllText($settingsPath) | ConvertFrom-Json
    $settings.github_api = $serverUrl + '/slow'; $settings.gitee_api = $serverUrl + '/gitee'
    [IO.File]::WriteAllText($settingsPath, ($settings | ConvertTo-Json -Compress), $utf8)
    Clear-UpgradeAttempt $autoRoot
    $errors = Invoke-Launcher $autoRoot -NoWait
    Wait-UpgradeWorker $autoRoot
    Assert-Case ('A slow update does not block the main launch (' + $script:launchMilliseconds + 'ms)') ($script:launchMilliseconds -lt 2500)
    $attempt = [IO.File]::ReadAllText((Join-Path $autoRoot 'last-update-check.json')) | ConvertFrom-Json
    Assert-Case 'The background instance falls back to Gitee after slow GitHub' ($errors.Length -eq 0 -and $attempt.status -ceq 'PASS' -and $attempt.source -ceq 'gitee')
    Clear-UpgradeAttempt $autoRoot
    $settings.github_api = $serverUrl + '/fail'; $settings.gitee_api = $serverUrl + '/fail'
    [IO.File]::WriteAllText($settingsPath, ($settings | ConvertTo-Json -Compress), $utf8)
    $errors = Invoke-Launcher $autoRoot
    $attempt = [IO.File]::ReadAllText((Join-Path $autoRoot 'last-update-check.json')) | ConvertFrom-Json
    Assert-Case 'Offline startup keeps the working runtime and records the failure' ($errors.Length -eq 0 -and $attempt.status -ceq 'FAIL')
    $settings.gitee_api = $serverUrl + '/gitee'
    [IO.File]::WriteAllText($settingsPath, ($settings | ConvertTo-Json -Compress), $utf8)
    $scheduled = & (Join-Path $autoRoot 'update.ps1') -InstallRoot $autoRoot -Automatic | ConvertFrom-Json
    Assert-Case 'A recent failed startup does not suppress the full scheduled update' ($scheduled.status -ceq 'PASS' -and $scheduled.source -ceq 'gitee')
    $registrationFailure = & (Join-Path $SourceRoot 'install/install.ps1') -InstallRoot $autoRoot -EnableAutoUpdate -GitHubApi ($serverUrl + '/github') -GiteeApi ($serverUrl + '/gitee') | ConvertFrom-Json
    Assert-Case 'A registration failure reports separately from a verified installation' ($registrationFailure.status -ceq 'PASS' -and $registrationFailure.schedule_registration -ceq 'FAIL' -and $registrationFailure.auto_update_enabled -and $registrationFailure.schedule_error)
    [void](& (Join-Path $SourceRoot 'install/install.ps1') -InstallRoot $autoRoot -DisableAutoUpdate -ScheduleDryRun)
    [IO.File]::Delete((Join-Path $autoRoot 'last-update-check.json'))
    $errors = Invoke-Launcher $autoRoot
    Assert-Case 'Disabling automatic updates persists across launches' ($errors.Length -eq 0 -and -not [IO.File]::Exists((Join-Path $autoRoot 'last-update-check.json')))
    $result = Invoke-Installer $autoRoot
    Assert-Case 'An ordinary reinstall preserves the explicit disabled preference' (-not $result.auto_update_enabled)
    $definition = & (Join-Path $SourceRoot 'install/install.ps1') -InstallRoot $root -TaskDefinitionOnly | ConvertFrom-Json
    [xml]$taskXml = $definition.task_xml
    Assert-Case 'Daily and logon current-user task has a three-minute execution limit without registration' ($definition.registered -eq $false -and $taskXml.Task.Triggers.TimeTrigger.Repetition.Interval -ceq 'P1D' -and $taskXml.Task.Triggers.LogonTrigger.Delay -ceq 'PT1M' -and $taskXml.Task.Settings.ExecutionTimeLimit -ceq 'PT3M' -and $taskXml.Task.Actions.Exec.Command -ceq (Join-Path $PSHOME 'pwsh.exe') -and $taskXml.Task.Actions.Exec.Arguments -like '*-Automatic')
    Assert-Case 'Temporary installer directories were cleaned' (@([IO.Directory]::EnumerateDirectories($root, '.install-*')).Count -eq 0)
    $summary = [ordered]@{status='PASS'; checks=$results.Count; cases=@($results); actual_task_registration='NOT_RUN'; actual_user_installation='NOT_RUN'}
    [IO.File]::WriteAllText((Join-Path $testBase 'results.json'), ($summary | ConvertTo-Json -Depth 6), $utf8)
    $summary | ConvertTo-Json -Depth 6
} finally {
    if ($autoRoot -and [IO.Directory]::Exists($autoRoot)) { Wait-UpgradeWorker $autoRoot }
    if ($server) {
        [IO.File]::WriteAllText((Join-Path $taskRoot 'stop'), 'stop')
        if (-not $server.HasExited -and -not $server.WaitForExit(2000)) { $server.Kill($true); [void]$server.WaitForExit(3000) }
        if ($serverErr -and $serverErr.IsCompleted -and $serverErr.Result) { Write-Warning $serverErr.Result }
        $server.Dispose()
    }
    $absolute = [IO.Path]::GetFullPath($taskRoot)
    if ($absolute.StartsWith($testBase + '\', [StringComparison]::OrdinalIgnoreCase) -and [IO.Directory]::Exists($absolute)) { Remove-Item -LiteralPath $absolute -Recurse -Force }
}
