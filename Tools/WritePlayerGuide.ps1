# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Regenerates the player guide: runs the game with -SOLWritePlayerGuide at 1920x1080 (windowed), which opens Help in the
# main menu, captures one image per Help tab (and the Keyboard tab with a key hovered), writes Docs\PLAYER_GUIDE.md and
# puts the images in Docs\Images, then quits (SDD 33 section 9). The run uses the smoke settings slot (default keys)
# and gives the player's GameUserSettings.ini back.
#   .\Tools\WritePlayerGuide.ps1            regenerate the guide and its images
#   .\Tools\WritePlayerGuide.ps1 -Build     build SOLTestEditor first
# Exit codes: 0 written, 1 the run failed (the old guide and images kept), 2 could not run.
# Log: Saved\Logs\PlayerGuide.log.
[CmdletBinding()]
param(
    # build the SOLTestEditor target before running
    [switch]$Build,
    # seconds to wait for the game before giving up (it is left running, its PID printed)
    [int]$TimeoutSeconds = 300,
    # engine install root
    [string]$EnginePath = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $PSScriptRoot
$uproject = Join-Path $projectDir "SOLTest.uproject"
$editor = Join-Path $EnginePath "Engine\Binaries\Win64\UnrealEditor.exe"
$buildBat = Join-Path $EnginePath "Engine\Build\BatchFiles\Build.bat"
$logName = "PlayerGuide.log"
$logFile = Join-Path $projectDir "Saved\Logs\$logName"

if (-not (Test-Path $editor)) { Write-Error "UnrealEditor.exe not found at $editor"; exit 2 }

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

# the game run: plain arguments, no console pipe (the game writes its own log)
if (Test-Path $logFile) { Remove-Item $logFile -Force }
$gameArgs = @("`"$uproject`"", "-game", "-windowed", "-ResX=1920", "-ResY=1080", "-SOLWritePlayerGuide",
    "-nosplash", "-NoLogTimes", "-log=$logName")
Write-Host "Writing the player guide (about a minute)..."
$process = Start-Process -FilePath $editor -ArgumentList $gameArgs -PassThru
$null = $process.Handle   # keeps the handle, so ExitCode is readable after the exit
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    Write-Host "The game is still running after $TimeoutSeconds s (PID $($process.Id)); it was not stopped."
    exit 2
}

# the verdict comes from the log (one DONE line, no FAIL line) and the exit code (1 on a failure)
if (-not (Test-Path $logFile)) { Write-Host "No log at $logFile (engine exit $($process.ExitCode))."; exit 2 }
$lines = Select-String -Path $logFile -Pattern "PlayerGuide (WROTE|DONE|FAIL)" | ForEach-Object { $_.Line }
$lines | ForEach-Object { Write-Host $_ }
if (($lines | Where-Object { $_ -match "PlayerGuide FAIL" }) -or $process.ExitCode -ne 0) {
    Write-Host "The run failed (engine exit $($process.ExitCode)); see $logFile."
    exit 1
}
if (-not ($lines | Where-Object { $_ -match "PlayerGuide DONE" })) {
    Write-Host "The run ended without a DONE line; see $logFile."
    exit 1
}

# lossless recompression (the engine writes RGBA with an opaque alpha at a fast PNG level): RGB, optimized, about 30%
# smaller, saved to a temporary file and swapped in; skipped when Python with Pillow is not installed
$images = $lines | ForEach-Object { if ($_ -match "PlayerGuide WROTE (.+\.png) \(") { $Matches[1] } }
$hasPillow = "import importlib.util, sys`nsys.exit(0 if importlib.util.find_spec('PIL') else 1)"
$recompress = "import os, sys`nfrom PIL import Image`nfor p in sys.argv[1:]:`n    im = Image.open(p)`n    " +
    "im = im.convert('RGB') if im.mode == 'RGBA' and im.getchannel('A').getextrema() == (255, 255) else im`n" +
    "    im.save(p + '.tmp', 'PNG', optimize=True)`n    os.replace(p + '.tmp', p)"
if ($images -and (Get-Command python -ErrorAction SilentlyContinue)) {
    & python -c $hasPillow
    if ($LASTEXITCODE -eq 0) {
        & python -c $recompress @images
        if ($LASTEXITCODE -ne 0) { Write-Host "Recompressing the images failed; they are left as captured." }
        foreach ($image in $images) { Write-Host ("{0}: {1:N0} bytes" -f $image, (Get-Item $image).Length) }
    }
    else { Write-Host "Pillow not installed: the images are left as captured." }
}
Write-Host "Done: commit Docs\PLAYER_GUIDE.md and the Docs\Images\33-help-*.png images."
exit 0
