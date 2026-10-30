#requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Pilot','Start','Status','Cleanup')][string]$Phase = 'Pilot',
    [string]$ReceiptRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build/scheduler-windows-natural'),
    [uri]$Proxy
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$sourceRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot 'build'))
$ReceiptRoot = [IO.Path]::GetFullPath($ReceiptRoot)
if (-not $IsWindows -or -not $ReceiptRoot.StartsWith($buildRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use Windows PowerShell 7 with a receipt directory below this checkout build directory.'
}
[void][IO.Directory]::CreateDirectory($ReceiptRoot)
$installer = Join-Path $sourceRoot 'install/install.ps1'
$installRoot = Join-Path $ReceiptRoot 'isolated-installation'
$statePath = Join-Path $ReceiptRoot 'state.json'
$parameters = @{ InstallRoot=$installRoot; Source='gitee' }
if ($Proxy) { $parameters.Proxy = $Proxy }
$phaseSeconds = if ($Phase -eq 'Start') {180} elseif ($Phase -eq 'Pilot') {15} else {30}
$phaseDeadline = [datetime]::UtcNow.AddSeconds($phaseSeconds)

function Assert-PhaseBudget {
    if ([datetime]::UtcNow -ge $phaseDeadline) { throw "The $Phase phase exceeded its $phaseSeconds-second budget." }
}

function Save-Receipt([string]$Name, $Value) {
    [IO.File]::WriteAllText((Join-Path $ReceiptRoot $Name), ($Value | ConvertTo-Json -Depth 12), $utf8)
}

function Open-Scheduler {
    Assert-PhaseBudget
    $service = New-Object -ComObject 'Schedule.Service'
    $service.Connect()
    return $service.GetFolder('\')
}

function Find-Task($Folder, [string]$Name) {
    Assert-PhaseBudget
    try { return $Folder.GetTask($Name) }
    catch {
        $exception = if ($_.Exception.InnerException) { $_.Exception.InnerException } else { $_.Exception }
        if ($exception.HResult -ne -2147024894) { throw }
        return $null
    }
}

function Assert-OwnedTask($Task, [string]$Name) {
    if (-not $Task -or $Task.Name -cne $Name) { throw 'The isolated scheduled task is missing.' }
    $action = $Task.Definition.Actions.Item(1)
    $updater = Join-Path $installRoot 'update.ps1'
    if ($Task.Definition.Actions.Count -ne 1 -or $action.Path -cne (Join-Path $PSHOME 'pwsh.exe') -or
        $action.Arguments -notlike ('*"' + $updater + '"*') -or $action.Arguments -notlike ('*"' + $installRoot + '"*')) {
        throw 'The scheduled action does not belong to this acceptance installation.'
    }
    if ($Task.Definition.Triggers.Count -ne 1 -or $Task.Definition.Triggers.Item(1).Repetition.Interval -cne 'PT1H' -or
        $Task.Definition.Settings.ExecutionTimeLimit -cne 'PT3M' -or $Task.Definition.Principal.RunLevel -ne 0) {
        throw 'The product hourly trigger, execution limit or privilege level changed.'
    }
}

function Get-OwnedProcesses {
    Assert-PhaseBudget
    $escaped = $installRoot.Replace('\','\\').Replace("'", "\'")
    return @(Get-CimInstance Win32_Process -OperationTimeoutSec 10 -Filter ("CommandLine LIKE '%" + $escaped + "%'") |
        Where-Object { $_.ProcessId -ne $PID } |
        Select-Object ProcessId,ParentProcessId,CreationDate,ExecutablePath,CommandLine)
}

function Remove-OwnedInstallation {
    Assert-PhaseBudget
    $absolute = [IO.Path]::GetFullPath($installRoot)
    if ($absolute -cne [IO.Path]::Combine($ReceiptRoot,'isolated-installation') -or
        -not $absolute.StartsWith($buildRoot + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Cleanup path is outside the owned fixture.' }
    if ([IO.Directory]::Exists($absolute)) { Remove-Item -LiteralPath $absolute -Recurse -Force }
}

$self = Get-CimInstance Win32_Process -OperationTimeoutSec 10 -Filter "ProcessId=$PID"
Save-Receipt ('owner-' + $Phase.ToLowerInvariant() + '.json') ([ordered]@{
    pid=$PID; created=$self.CreationDate; parent_pid=$self.ParentProcessId; argv=$self.CommandLine; phase=$Phase
})

if ($Phase -eq 'Pilot') {
    Assert-PhaseBudget
    $definition = & $installer @parameters -TaskDefinitionOnly | ConvertFrom-Json
    [xml]$xml = $definition.task_xml
    if ($definition.registered -or $xml.Task.Triggers.TimeTrigger.Repetition.Interval -cne 'PT1H' -or
        $xml.Task.Settings.ExecutionTimeLimit -cne 'PT3M') { throw 'Product task-definition pilot failed.' }
    Save-Receipt 'pilot.json' $definition
    Assert-PhaseBudget
    [ordered]@{status='PASS'; phase='Pilot'; task_registered=$false; interval='PT1H'} | ConvertTo-Json
    return
}

if ($Phase -eq 'Start') {
    if ([IO.File]::Exists($statePath) -or [IO.Directory]::Exists($installRoot)) { throw 'Existing acceptance state requires inspection or cleanup before a new start.' }
    $ready = $false
    try {
        Assert-PhaseBudget
        $installed = & $installer @parameters -EnableAutoUpdate | ConvertFrom-Json
        Save-Receipt 'installation.json' $installed
        $folder = Open-Scheduler
        $task = Find-Task $folder $installed.task_name
        Assert-OwnedTask $task $installed.task_name
        $trigger = $task.Definition.Triggers.Item(1)
        $firstDue = ([datetime]$trigger.StartBoundary).ToUniversalTime()
        [IO.File]::WriteAllText((Join-Path $ReceiptRoot 'registered-task.xml'), $task.Xml, $utf8)
        $state = [ordered]@{
            status='WAITING'; task_name=$task.Name; install_root=$installRoot
            registered_utc=[datetime]::UtcNow.ToString('o'); first_due_utc=$firstDue.ToString('o')
            repeat_due_utc=$firstDue.AddHours(1).ToString('o'); deadline_utc=$firstDue.AddMinutes(75).ToString('o')
            max_polls=90; polls=0; first_success_utc=$null; repeat_success_utc=$null
            interval='PT1H'; manually_started=$false; interval_modified=$false
            installer_sha256=(Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
            updater_sha256=(Get-FileHash -LiteralPath (Join-Path $installRoot 'update.ps1') -Algorithm SHA256).Hash.ToLowerInvariant()
            initial_last_run=$task.LastRunTime.ToUniversalTime().ToString('o'); initial_result=$task.LastTaskResult
            proxy=if ($Proxy) {$Proxy.AbsoluteUri} else {$null}
        }
        Save-Receipt 'state.json' $state
        Assert-PhaseBudget
        $ready = $true
        $state | ConvertTo-Json -Depth 5
    } finally {
        if (-not $ready) {
            $phaseDeadline = [datetime]::UtcNow.AddSeconds(30)
            $phaseSeconds = 30
            $failureCleanup = [ordered]@{status='FAIL'; installation_preserved=$true; task_absent=$false; processes=@()}
            try {
                $disabled = & $installer @parameters -DisableAutoUpdate | ConvertFrom-Json
                Save-Receipt 'failed-start-disable.json' $disabled
                $cleanupFolder = Open-Scheduler
                $remainingTask = Find-Task $cleanupFolder $disabled.task_name
                $failureCleanup.task_absent = $null -eq $remainingTask
                $failureCleanup.processes = @(Get-OwnedProcesses)
                if (-not $failureCleanup.task_absent -or $failureCleanup.processes.Count -ne 0) {
                    throw 'The failed start still owns a task or process; preserve its installation for inspection.'
                }
                Remove-OwnedInstallation
                $failureCleanup.installation_preserved = [IO.Directory]::Exists($installRoot)
                $failureCleanup.status = 'PASS'
            } catch {
                $failureCleanup.error = $_.Exception.Message
            } finally {
                Save-Receipt 'failed-start-cleanup.json' $failureCleanup
            }
        }
    }
    return
}

if (-not [IO.File]::Exists($statePath)) { throw 'Start the acceptance fixture before status or cleanup.' }
$state = [IO.File]::ReadAllText($statePath,$utf8) | ConvertFrom-Json
if ($state.install_root -cne $installRoot) { throw 'The saved installation ownership differs.' }
$folder = Open-Scheduler
$task = Find-Task $folder $state.task_name

if ($Phase -eq 'Status') {
    Assert-OwnedTask $task $state.task_name
    if ($state.polls -ge $state.max_polls) { throw 'Maximum status observations reached; cleanup is still available.' }
    $state.polls++
    $lastRun = $task.LastRunTime.ToUniversalTime()
    $result = $task.LastTaskResult
    $active = @(Get-OwnedProcesses)
    $intervalSeconds = $null
    if ($task.State -ne 4 -and $result -eq 0 -and $lastRun -ge [datetime]$state.first_due_utc) {
        if (-not $state.first_success_utc) { $state.first_success_utc = $lastRun.ToString('o') }
        $intervalSeconds = ($lastRun - [datetime]$state.first_success_utc).TotalSeconds
        if ($lastRun -ge [datetime]$state.repeat_due_utc -and $intervalSeconds -ge 3600) {
            $state.repeat_success_utc = $lastRun.ToString('o')
        }
    }
    $observedFailure = $task.State -ne 4 -and $lastRun -ge [datetime]$state.first_due_utc -and $result -ne 0
    $state.status = if ($state.status -eq 'FAIL' -or $observedFailure) {'FAIL'}
        elseif ($state.first_success_utc -and $state.repeat_success_utc) {'PASS'}
        elseif ([datetime]::UtcNow -ge [datetime]$state.deadline_utc) {'FAIL'} else {'WAITING'}
    $observation = [ordered]@{
        observed_utc=[datetime]::UtcNow.ToString('o'); status=$state.status; poll=$state.polls
        task_state=$task.State; last_run_utc=$lastRun.ToString('o'); last_task_result=$result
        next_run_utc=$task.NextRunTime.ToUniversalTime().ToString('o'); missed_runs=$task.NumberOfMissedRuns
        first_success_utc=$state.first_success_utc; repeat_success_utc=$state.repeat_success_utc
        actual_interval_seconds=$intervalSeconds
        completed_action_failed=$observedFailure
        deadline_utc=$state.deadline_utc; processes=$active
    }
    Save-Receipt ('status-{0:d3}.json' -f $state.polls) $observation
    Save-Receipt 'state.json' $state
    $observation | ConvertTo-Json -Depth 8
    Assert-PhaseBudget
    if ($state.status -eq 'FAIL') { exit 1 }
    return
}

if ($task) {
    Assert-OwnedTask $task $state.task_name
    if ($task.State -eq 4) {
        Save-Receipt 'cleanup-running.json' @(Get-OwnedProcesses)
        $task.Stop(0)
        $stopDeadline = [datetime]::UtcNow.AddSeconds(10)
        for ($attempt = 0; $attempt -lt 20 -and [datetime]::UtcNow -lt $stopDeadline; $attempt++) {
            $task = Find-Task $folder $state.task_name
            if (-not $task -or $task.State -ne 4) { break }
            Start-Sleep -Milliseconds 500
        }
        if ($task -and $task.State -eq 4) { throw 'The owned scheduler action did not stop within its cleanup deadline.' }
    }
}
$before = @(Get-OwnedProcesses)
if ($before.Count -ne 0) { Save-Receipt 'cleanup-active.json' $before; throw 'Owned updater processes remain; installation has been preserved.' }
$disabled = & $installer @parameters -DisableAutoUpdate | ConvertFrom-Json
if (Find-Task $folder $state.task_name) { throw 'The product disable operation did not remove the isolated task.' }
Remove-OwnedInstallation
$after = @(Get-OwnedProcesses)
$cleanup = [ordered]@{
    status=if ($after.Count -eq 0 -and -not [IO.Directory]::Exists($installRoot)) {'PASS'} else {'FAIL'}
    natural_hourly_status=$state.status; task_name=$state.task_name; task_absent=$true
    installation_absent=(-not [IO.Directory]::Exists($installRoot)); processes=$after
    disabled=$disabled; completed_utc=[datetime]::UtcNow.ToString('o')
}
Save-Receipt 'cleanup.json' $cleanup
$cleanup | ConvertTo-Json -Depth 8
Assert-PhaseBudget
if ($cleanup.status -eq 'FAIL') { exit 1 }
