#requires -Version 7.0
[CmdletBinding()]
param([string]$Executable = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\bin\cdx.exe'))

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
. (Join-Path $PSScriptRoot 'package.ps1')
$root = Split-Path -Parent $PSScriptRoot
$deadline = [datetime]::UtcNow.AddSeconds(90)
$owned = Join-Path $root ('build\pwsh-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($owned)
$audit = Join-Path $owned 'processes.jsonl'
$scriptPath = Join-Path $owned '中文 saved script.ps1'
Write-PackageText $scriptPath @'
param([string]$Value, [int]$ExitCode = 0, [int]$DelayMs = 0)
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::Write(($PSVersionTable.PSVersion.Major.ToString() + '|' + $Value))
if ($DelayMs -gt 0) { Start-Sleep -Milliseconds $DelayMs }
exit $ExitCode
'@
$literal = '中文 $(literal) a"b'
$requestPath = Join-Path $owned 'request.json'
$hosts = @(@((Join-Path $PSHOME 'pwsh.exe'); (Get-Command pwsh.exe -All -CommandType Application -ErrorAction Stop).Source) | Select-Object -Unique)
if ($hosts.Count -gt 8) { throw 'PowerShell host discovery exceeds the eight-host test budget.' }
$passed = 0

function Assert-Execution($Result, [string]$Label) {
    if ($Result.status -cne 'completed' -or $Result.exit_code -ne 0 -or -not $Result.cleanup_complete -or $Result.stdout -cne ('7|' + $literal)) {
        throw ($Label + ': ' + ($Result | ConvertTo-Json -Compress -Depth 8))
    }
    $script:passed++
    Write-Output ($Label + ': PASS')
}

function Invoke-Rpc([int]$Id, [string]$Method, $Params) {
    Assert-PackageBudget $deadline
    $mcp.StandardInput.WriteLine((@{jsonrpc='2.0'; id=$Id; method=$Method; params=$Params} | ConvertTo-Json -Compress -Depth 12))
    $mcp.StandardInput.Flush()
    $read = $mcp.StandardOutput.ReadLineAsync()
    if (-not $read.Wait(15000) -or $null -eq $read.Result) { throw ('MCP response missing: ' + $Method) }
    $reply = $read.Result | ConvertFrom-Json -Depth 30
    if ($reply.id -ne $Id -or $reply.error) { throw ('Unexpected MCP response: ' + $read.Result) }
    return $reply.result
}

foreach ($pwsh in $hosts) {
    $arguments = @{program=$pwsh; args=@('-NoLogo','-NoProfile','-File',$scriptPath,$literal); cwd=$owned; timeout_ms=10000; output_limit_bytes=4096}
    Write-PackageText $requestPath (@{operation='execute_program'; arguments=$arguments} | ConvertTo-Json -Depth 8 -Compress)
    $cli = Invoke-PackageProgram $Executable @('--request',$requestPath) $root $audit $deadline 15
    if ($cli.ExitCode -ne 0) { throw ('CLI PowerShell request failed: ' + $cli.Stderr + $cli.Stdout) }
    Assert-Execution ($cli.Stdout | ConvertFrom-Json) ('CLI saved script: ' + $pwsh)
}

$mcp = [Diagnostics.Process]::new()
$mcp.StartInfo.FileName = $Executable
$mcp.StartInfo.WorkingDirectory = $root
$mcp.StartInfo.ArgumentList.Add('--mcp')
$mcp.StartInfo.UseShellExecute = $false
$mcp.StartInfo.CreateNoWindow = $true
$mcp.StartInfo.RedirectStandardInput = $true
$mcp.StartInfo.RedirectStandardOutput = $true
$mcp.StartInfo.RedirectStandardError = $true
$mcp.StartInfo.StandardInputEncoding = [Text.UTF8Encoding]::new($false)
$mcp.StartInfo.StandardOutputEncoding = [Text.Encoding]::UTF8
$started = $false
try {
    $started = $mcp.Start()
    if (-not $started) { throw 'MCP server did not start.' }
    $stderr = $mcp.StandardError.ReadToEndAsync()
    $initialize = Invoke-Rpc 1 'initialize' @{protocolVersion='2025-06-18';capabilities=@{};clientInfo=@{name='pwsh-regression';version='1.0'}}
    if ($initialize.serverInfo.name -cne 'Cordonix') { throw 'Unexpected MCP server.' }
    $mcp.StandardInput.WriteLine('{"jsonrpc":"2.0","method":"notifications/initialized"}')
    $mcp.StandardInput.Flush()
    $id = 2
    foreach ($pwsh in $hosts) {
        $arguments = @{program=$pwsh; args=@('-NoLogo','-NoProfile','-File',$scriptPath,$literal); cwd=$owned; timeout_ms=10000; output_limit_bytes=4096}
        $reply = Invoke-Rpc $id 'tools/call' @{name='execute_program';arguments=$arguments}
        if ($reply.isError) { throw ('MCP tool returned an error: ' + ($reply | ConvertTo-Json -Compress -Depth 8)) }
        Assert-Execution ($reply.content[0].text | ConvertFrom-Json) ('MCP saved script: ' + $pwsh)
        $id++

        $arguments.args = @('-NoLogo','-NoProfile','-File',$scriptPath,$literal,'-ExitCode','7')
        $reply = Invoke-Rpc $id 'tools/call' @{name='execute_program';arguments=$arguments}
        $result = $reply.content[0].text | ConvertFrom-Json
        if ($result.status -cne 'failed' -or $result.process_status -cne 'failed' -or $result.exit_code -ne 7 -or $result.pid -le 0 -or -not $result.cleanup_complete -or $result.stdout -cne ('7|' + $literal)) {
            throw ('PowerShell script failure was not preserved: ' + ($result | ConvertTo-Json -Compress))
        }
        $passed++
        Write-Output ('MCP script exit code: PASS: ' + $pwsh)
        $id++

        $arguments.args = @('-NoLogo','-NoProfile','-File',$scriptPath,$literal,'-DelayMs','30000')
        $arguments.timeout_ms = 2000
        $reply = Invoke-Rpc $id 'tools/call' @{name='execute_program';arguments=$arguments}
        $result = $reply.content[0].text | ConvertFrom-Json
        if ($result.status -cne 'timed_out' -or $result.process_status -cne 'timed_out' -or $result.pid -le 0 -or -not $result.cleanup_complete -or $result.active_processes_after_cleanup -ne 0) {
            throw ('PowerShell timeout cleanup failed: ' + ($result | ConvertTo-Json -Compress))
        }
        $passed++
        Write-Output ('MCP timeout cleanup: PASS: ' + $pwsh)
        $id++
    }
    foreach ($forbidden in @(@('-NoProfile','-Command','Write-Output forbidden'), @('-NoProfile','-EncodedCommand','QQA='), @('-NoProfile'), @('-NoProfile','-File','relative.ps1'))) {
        $reply = Invoke-Rpc $id 'tools/call' @{name='execute_program';arguments=@{program=$hosts[0];args=@($forbidden);cwd=$owned;timeout_ms=1000;output_limit_bytes=4096}}
        $result = $reply.content[0].text | ConvertFrom-Json
        if ($result.status -cne 'validation_error' -or $result.pid -ne 0) { throw 'PowerShell invalid invocation was not rejected before spawn.' }
        $passed++
        Write-Output 'MCP command/missing-file/relative-file rejection: PASS'
        $id++
    }
    $mcp.StandardInput.Close()
    if (-not $mcp.WaitForExit(3000) -or $mcp.ExitCode -ne 0) { throw 'MCP server did not exit cleanly.' }
    [ordered]@{status='PASS';passed=$passed;hosts=@($hosts);executable=$Executable;receipts=$owned} | ConvertTo-Json -Depth 5
}
finally {
    if ($started -and -not $mcp.HasExited) { $mcp.Kill($true); [void]$mcp.WaitForExit(3000) }
    $mcp.Dispose()
}
