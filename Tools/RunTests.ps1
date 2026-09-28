# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Runs the UE Automation tests headless and prints a pass/fail summary.
#   .\Tools\RunTests.ps1                      run everything under "SOLTest"
#   .\Tools\RunTests.ps1 -Filter SOLTest.Kepler
#   .\Tools\RunTests.ps1 -Build               build SOLTestEditor first
#   .\Tools\RunTests.ps1 -List                list the tests matching -Filter
# Exit codes: 0 all passed, 1 test failures, 2 could not run / no results.
# Results: Saved\AutomationReports\index.json (also the per-run log in Saved\Logs).
[CmdletBinding()]
param(
    # automation test filter (name prefix, or a group such as "Group:Project")
    [string]$Filter = "SOLTest",
    # build the SOLTestEditor target before running
    [switch]$Build,
    # list matching tests instead of running them
    [switch]$List,
    # run with a real RHI instead of -nullrhi (only needed for rendering tests)
    [switch]$Rhi,
    # stream the full engine log to the console (it is always written to Saved\Logs)
    [switch]$ShowLog,
    # engine install root
    [string]$EnginePath = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $PSScriptRoot
$uproject = Join-Path $projectDir "SOLTest.uproject"
$editorCmd = Join-Path $EnginePath "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$buildBat = Join-Path $EnginePath "Engine\Build\BatchFiles\Build.bat"
$reportDir = Join-Path $projectDir "Saved\AutomationReports"

if (-not (Test-Path $editorCmd)) { Write-Error "UnrealEditor-Cmd.exe not found at $editorCmd"; exit 2 }

# refuse to run while the editor has this project open (locked DLLs, shared config)
$running = Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" |
    Where-Object { $_.CommandLine -like "*SOLTest.uproject*" }
if ($running) {
    Write-Error "The Unreal Editor has SOLTest open (PID $($running[0].ProcessId)). Close it first."
    exit 2
}

# optional build step
if ($Build) {
    Write-Host "Building SOLTestEditor..."
    & $buildBat SOLTestEditor Win64 Development "-Project=$uproject" -WaitMutex
    if ($LASTEXITCODE -ne 0) { Write-Error "Build failed (exit $LASTEXITCODE)"; exit 2 }
}

if (Test-Path $reportDir) { Remove-Item $reportDir -Recurse -Force }
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null

# list mode or run mode
if ($List) { $cmd = "Automation List; Quit" }
else { $cmd = "Automation RunTests $Filter; Quit" }

$args = @(
    "`"$uproject`"",
    "-ExecCmds=`"$cmd`"",
    "-ReportExportPath=`"$reportDir`"",
    "-unattended", "-nosplash", "-nosound", "-NoLogTimes"
)
if ($ShowLog) { $args += "-stdout"; $args += "-FullStdOutLogOutput" }
if (-not $Rhi) { $args += "-nullrhi" }
$args += "-TestExit=`"Automation Test Queue Empty`""

Write-Host "Running: $cmd"
$sw = [Diagnostics.Stopwatch]::StartNew()
if ($ShowLog) { & $editorCmd @args } else { & $editorCmd @args | Out-Null }
$engineExit = $LASTEXITCODE
$sw.Stop()

if ($List) { exit 0 }

# parse the exported report
$indexFile = Join-Path $reportDir "index.json"
if (-not (Test-Path $indexFile)) {
    Write-Host "No report produced at $indexFile (engine exit $engineExit). Check Saved\Logs."
    exit 2
}
$report = (Get-Content $indexFile -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
$total = @($report.tests).Count
if ($total -eq 0) {
    Write-Host "No tests matched filter '$Filter'."
    exit 2
}

Write-Host ""
Write-Host ("Tests: {0}  Passed: {1}  Passed with warnings: {2}  Failed: {3}  Not run: {4}  ({5:N1}s)" -f `
    $total, $report.succeeded, $report.succeededWithWarnings, $report.failed, $report.notRun, $sw.Elapsed.TotalSeconds)
foreach ($t in $report.tests | Where-Object { $_.state -ne "Success" }) {
    Write-Host ("  {0}: {1}" -f $t.state, $t.fullTestPath)
    foreach ($e in @($t.entries) | Where-Object { $_.event.type -eq "Error" }) {
        Write-Host ("      {0}" -f $e.event.message)
    }
}

if ($report.failed -gt 0 -or $report.notRun -gt 0) { exit 1 }
exit 0
