# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Runs the UE Automation tests headless and prints a pass/fail summary.
#   .\Tools\RunTests.ps1                      run everything under "SOLTest"
#   .\Tools\RunTests.ps1 -Filter SOLTest.Kepler
#   .\Tools\RunTests.ps1 -Build               build SOLTestEditor first
#   .\Tools\RunTests.ps1 -List                list the tests matching -Filter
# Exit codes: 0 all passed, 1 test failures, 2 could not run / no results,
#             3 run stalled or timed out (the process this script started was killed).
# Results: Saved\AutomationReports\index.json (also the per-run log, Saved\Logs\RunTests-*.log).
# Works the same from Windows PowerShell 5.1 and PowerShell 7.
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
    # echo the engine log to the console as it is written (it is always written to Saved\Logs)
    [switch]$ShowLog,
    # engine install root
    [string]$EnginePath = "C:\Program Files\Epic Games\UE_5.8",
    # kill the run and exit 3 if it takes longer than this many minutes
    [int]$TimeoutMinutes = 25
)

$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $PSScriptRoot
$uproject = Join-Path $projectDir "SOLTest.uproject"
$editorCmd = Join-Path $EnginePath "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$buildBat = Join-Path $EnginePath "Engine\Build\BatchFiles\Build.bat"
$reportDir = Join-Path $projectDir "Saved\AutomationReports"
$logDir = Join-Path $projectDir "Saved\Logs"
$logFile = Join-Path $logDir ("RunTests-{0}.log" -f (Get-Date -Format "yyyyMMdd-HHmmss"))

# stall detector limits (seconds)
$NoLogLimit = 120
$CmdLineCheckLimit = 90
$StartupToTestsLimit = 180
$NoGrowthLimit = 300

# prints a message to stderr and exits with the given code
function Stop-Run([string]$Message, [int]$Code) {
    [Console]::Error.WriteLine($Message)
    exit $Code
}

# wraps a whole argument in quotes when it holds whitespace or a semicolon, so Windows splits it as one argument
function ConvertTo-QuotedArg([string]$Value) {
    if ($Value -match '[\s;]') { return '"' + $Value + '"' }
    return $Value
}

# kills only the process this script started, with its children
function Stop-OwnProcess($Proc) {
    if ($Proc -and -not $Proc.HasExited) { & taskkill.exe /T /F /PID $Proc.Id | Out-Null }
}

# returns the log text appended since the last call (shared read, so the engine keeps writing)
function Read-LogTail([string]$Path, [ref]$Pos) {
    if (-not (Test-Path $Path)) { return "" }
    $fs = $null
    try {
        $fs = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        if ($fs.Length -le $Pos.Value) { return "" }
        $null = $fs.Seek($Pos.Value, [IO.SeekOrigin]::Begin)
        $buf = New-Object byte[] ($fs.Length - $Pos.Value)
        $n = $fs.Read($buf, 0, $buf.Length)
        $Pos.Value += $n
        return [Text.Encoding]::UTF8.GetString($buf, 0, $n)
    }
    catch { return "" }
    finally { if ($fs) { $fs.Dispose() } }
}

if (-not (Test-Path $editorCmd)) { Stop-Run "UnrealEditor-Cmd.exe not found at $editorCmd" 2 }

# refuse to run while any other Unreal editor or Build.bat is active (locked DLLs, shared config, mutexes)
$busy = @(Get-CimInstance Win32_Process | Where-Object {
        $_.ProcessId -ne $PID -and (
            $_.Name -like "UnrealEditor*" -or $_.Name -eq "UnrealBuildTool.exe" -or
            ($_.Name -in @("cmd.exe", "powershell.exe", "pwsh.exe") -and $_.CommandLine -like "*Build.bat*"))
    })
if ($busy.Count -gt 0) {
    Stop-Run ("Another Unreal process is running ({0} PID {1}). Close it or wait for it, then retry." -f $busy[0].Name, $busy[0].ProcessId) 2
}

# optional build step
if ($Build) {
    Write-Host "Building SOLTestEditor..."
    & $buildBat SOLTestEditor Win64 Development "-Project=$uproject" -WaitMutex
    if ($LASTEXITCODE -ne 0) { Stop-Run "Build failed (exit $LASTEXITCODE)" 2 }
}

if (Test-Path $reportDir) { Remove-Item $reportDir -Recurse -Force }
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
Get-ChildItem $logDir -Filter "RunTests-*.log" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

# list mode or run mode
if ($List) { $cmd = "Automation List; Quit" }
else { $cmd = "Automation RunTests $Filter; Quit" }

# no literal quotes inside values: each argument is quoted whole, so UE sees one clean argument
$argList = @(
    (ConvertTo-QuotedArg $uproject),
    (ConvertTo-QuotedArg "-ExecCmds=$cmd"),
    (ConvertTo-QuotedArg "-ReportExportPath=$reportDir"),
    (ConvertTo-QuotedArg "-abslog=$logFile"),
    "-unattended", "-nosplash", "-nosound", "-NoLogTimes",
    (ConvertTo-QuotedArg "-TestExit=Automation Test Queue Empty")
)
if (-not $Rhi) { $argList += "-nullrhi" }
$argString = $argList -join " "

Write-Host "Running: $cmd"
$sw = [Diagnostics.Stopwatch]::StartNew()
$proc = Start-Process -FilePath $editorCmd -ArgumentList $argString -WindowStyle Hidden -PassThru
$null = $proc.Handle   # keeps the handle open so ExitCode is readable under Windows PowerShell 5.1

$pos = [long]0
$partial = ""
$lastGrow = $sw.Elapsed.TotalSeconds
$cmdLineSeen = $false
$startupAt = $null
$testsAt = $null
$stall = $null
$timeoutSec = $TimeoutMinutes * 60

# poll until the engine exits, the timeout passes, or a stall detector fires
while (-not $proc.WaitForExit(2000)) {
    $now = $sw.Elapsed.TotalSeconds
    $text = Read-LogTail $logFile ([ref]$pos)
    if ($text.Length -gt 0) {
        $lastGrow = $now
        if ($ShowLog) { Write-Host -NoNewline $text }
        # scan only complete lines; the unfinished tail waits for the next read
        $lines = @(($partial + $text) -split "`r?`n")
        $partial = $lines[$lines.Count - 1]
        for ($i = 0; $i -lt $lines.Count - 1; $i++) {
            $line = $lines[$i]
            if ($line -match "Command Line:") {
                $cmdLineSeen = $true
                if ($line.Contains('=""')) { $stall = "stall detector: malformed command line (empty quoted value) in the log: $line" }
            }
            if ($null -eq $startupAt -and $line -match "Total Editor Startup Time") { $startupAt = $now }
            if ($null -eq $testsAt -and $line -match "Cmd: Automation (RunTests|List)") { $testsAt = $now }
        }
    }
    if ($stall) { break }
    if ($pos -eq 0 -and $now -gt $NoLogLimit) { $stall = "stall detector: no log output within $NoLogLimit s"; break }
    if (-not $cmdLineSeen -and $pos -gt 0 -and $now -gt $CmdLineCheckLimit) { $stall = "stall detector: no 'Command Line:' log line within $CmdLineCheckLimit s"; break }
    if ($null -ne $startupAt -and $null -eq $testsAt -and ($now - $startupAt) -gt $StartupToTestsLimit) {
        $stall = "stall detector: 'Cmd: Automation' not seen within $StartupToTestsLimit s after editor startup (command not executed)"; break
    }
    if ($null -ne $testsAt -and ($now - $lastGrow) -gt $NoGrowthLimit) { $stall = "stall detector: log did not grow for $NoGrowthLimit s after tests started"; break }
    if ($now -gt $timeoutSec) { $stall = "timeout: run exceeded $TimeoutMinutes minutes"; break }
}
if ($stall) {
    Stop-OwnProcess $proc
    Write-Host $stall
    Write-Host "Killed PID $($proc.Id). Log: $logFile"
    exit 3
}
$proc.WaitForExit()
$engineExit = $proc.ExitCode
if ($ShowLog) { Write-Host -NoNewline (Read-LogTail $logFile ([ref]$pos)) }
$sw.Stop()

if ($List) { exit 0 }

# parse the exported report
$indexFile = Join-Path $reportDir "index.json"
if (-not (Test-Path $indexFile)) {
    Write-Host "No report produced at $indexFile (engine exit $engineExit). Check $logFile."
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
