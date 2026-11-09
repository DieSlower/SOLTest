# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Regenerates the player guide and the map glossary crops (SDD 33 section 9, D39), in three steps:
#   1. the crop run: the game with -SOLWriteGlossaryCrops at 1920x1080 (windowed), which pins a map scene (sim date,
#      ship, camera, a route, a destination pick), takes a shot per scene and cuts the five map glossary crops into
#      Docs\Images\33-glossary-crop-*.png (a crop that matches its image within the capture noise keeps the old
#      file), then quits;
#   2. the import: Tools\UI\import_glossary_crops.py (headless UnrealEditor-Cmd) imports the crops as the Help page's
#      textures under Content\SOL\UI\Glossary (an unchanged crop is skipped, so nothing is re-saved);
#   3. the guide run: the game with -SOLWritePlayerGuide at 1920x1080 (windowed), which opens Help in the main menu,
#      captures each Help tab's pages (and the Keyboard tab with a key hovered), writes Docs\PLAYER_GUIDE.md and puts
#      the images in Docs\Images (a shot that matches its image within the capture noise keeps the old file), then
#      quits. It runs last so its Help shots show the fresh crop textures.
# Both game runs use the smoke settings slot (default keys) and give the player's GameUserSettings.ini back. Their PNGs
# are recompressed losslessly when Python with Pillow is installed (the crops before the import). A failed crop run or
# import stops there (the old guide is kept).
#   .\Tools\WritePlayerGuide.ps1            the crops, the guide and its images, the crop textures
#   .\Tools\WritePlayerGuide.ps1 -Build     build SOLTestEditor first
#   .\Tools\WritePlayerGuide.ps1 -NoCrops   the guide only (no crop run, no import)
# Exit codes: 0 written, 1 a run failed (that run's old files kept), 2 could not run.
# Logs: Saved\Logs\GlossaryCrops.log, Saved\Logs\PlayerGuide.log, Saved\Logs\GlossaryCropsImport.log.
[CmdletBinding()]
param(
    # build the SOLTestEditor target before running
    [switch]$Build,
    # skip the crop run and the crop import (the guide run only)
    [switch]$NoCrops,
    # seconds to wait for each run before giving up (it is left running, its PID printed)
    [int]$TimeoutSeconds = 300,
    # engine install root
    [string]$EnginePath = "C:\Program Files\Epic Games\UE_5.8"
)

$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $PSScriptRoot
$uproject = Join-Path $projectDir "SOLTest.uproject"
$editor = Join-Path $EnginePath "Engine\Binaries\Win64\UnrealEditor.exe"
$editorCmd = Join-Path $EnginePath "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$buildBat = Join-Path $EnginePath "Engine\Build\BatchFiles\Build.bat"
$logDir = Join-Path $projectDir "Saved\Logs"
$importScript = Join-Path $PSScriptRoot "UI\import_glossary_crops.py"
$importLogName = "GlossaryCropsImport.log"

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

# Runs the game once with a writer flag and returns its verdict: 0 written (and its PNGs), 1 failed, 2 could not run.
# The verdict comes from the log (one "<Prefix> DONE" line, no FAIL line) and the exit code (1 on a failure)
function Invoke-WriterRun([string]$Flag, [string]$LogName, [string]$Prefix, [string]$What) {
    $logFile = Join-Path $logDir $LogName
    if (Test-Path $logFile) { Remove-Item $logFile -Force }
    # plain arguments, no console pipe (the game writes its own log)
    $gameArgs = @("`"$uproject`"", "-game", "-windowed", "-ResX=1920", "-ResY=1080", "-$Flag", "-nosplash",
        "-NoLogTimes", "-log=$LogName")
    Write-Host "$What (about a minute)..."
    $process = Start-Process -FilePath $editor -ArgumentList $gameArgs -PassThru
    $null = $process.Handle   # keeps the handle, so ExitCode is readable after the exit
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Write-Host "The game is still running after $TimeoutSeconds s (PID $($process.Id)); it was not stopped."
        return @{ Verdict = 2; Images = @() }
    }
    if (-not (Test-Path $logFile)) {
        Write-Host "No log at $logFile (engine exit $($process.ExitCode))."
        return @{ Verdict = 2; Images = @() }
    }
    $lines = Select-String -Path $logFile -Pattern "$Prefix (WROTE|KEPT|DONE|FAIL)" | ForEach-Object { $_.Line }
    $lines | ForEach-Object { Write-Host $_ }
    if (($lines | Where-Object { $_ -match "$Prefix FAIL" }) -or $process.ExitCode -ne 0) {
        Write-Host "The run failed (engine exit $($process.ExitCode)); see $logFile."
        return @{ Verdict = 1; Images = @() }
    }
    if (-not ($lines | Where-Object { $_ -match "$Prefix DONE" })) {
        Write-Host "The run ended without a DONE line; see $logFile."
        return @{ Verdict = 1; Images = @() }
    }
    $images = @($lines | ForEach-Object { if ($_ -match "$Prefix WROTE (.+\.png) \(") { $Matches[1] } })
    return @{ Verdict = 0; Images = $images }
}

# Lossless recompression (the engine writes RGBA with an opaque alpha at a fast PNG level): RGB, optimized, about 30%
# smaller, saved to a temporary file and swapped in; skipped when Python with Pillow is not installed
function Compress-Images([string[]]$Images) {
    if (-not $Images -or -not (Get-Command python -ErrorAction SilentlyContinue)) { return }
    $hasPillow = "import importlib.util, sys`nsys.exit(0 if importlib.util.find_spec('PIL') else 1)"
    $recompress = "import os, sys`nfrom PIL import Image`nfor p in sys.argv[1:]:`n    im = Image.open(p)`n    " +
        "im = im.convert('RGB') if im.mode == 'RGBA' and im.getchannel('A').getextrema() == (255, 255) else im`n" +
        "    im.save(p + '.tmp', 'PNG', optimize=True)`n    os.replace(p + '.tmp', p)"
    & python -c $hasPillow
    if ($LASTEXITCODE -ne 0) { Write-Host "Pillow not installed: the images are left as captured."; return }
    & python -c $recompress @Images
    if ($LASTEXITCODE -ne 0) { Write-Host "Recompressing the images failed; they are left as captured." }
    foreach ($image in $Images) { Write-Host ("{0}: {1:N0} bytes" -f $image, (Get-Item $image).Length) }
}

# 1. the crop run (a failure keeps the old crops and stops here), its new crops recompressed before the import
if (-not $NoCrops) {
    $crops = Invoke-WriterRun "SOLWriteGlossaryCrops" "GlossaryCrops.log" "GlossaryCrops" `
        "Capturing the map glossary crops"
    if ($crops.Verdict -ne 0) { exit $crops.Verdict }
    Compress-Images $crops.Images

    # 2. the crop import (a failure stops here: the guide's Help shots would show stale textures)
    $importLog = Join-Path $logDir $importLogName
    if (Test-Path $importLog) { Remove-Item $importLog -Force }
    Write-Host "Importing the crop textures..."
    $importArgs = @("`"$uproject`"", "-run=pythonscript", "-script=`"$importScript`"", "-unattended", "-nosplash",
        "-nullrhi", "-NoLogTimes", "-log=$importLogName")
    $process = Start-Process -FilePath $editorCmd -ArgumentList $importArgs -PassThru -WindowStyle Hidden
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Write-Host "The import is still running after $TimeoutSeconds s (PID $($process.Id)); it was not stopped."
        exit 2
    }
    # the verdict comes from the log: a DONE line and no Python error (the commandlet's exit code also counts the
    # project's unrelated load errors, so it is only reported)
    $importLines = @()
    if (Test-Path $importLog) {
        $importLines = Select-String -Path $importLog -Pattern "SOLGlossaryCrops: |LogPython: Error" |
            ForEach-Object { $_.Line }
    }
    $importLines | ForEach-Object { Write-Host $_ }
    if (($importLines | Where-Object { $_ -match "LogPython: Error" }) -or
        -not ($importLines | Where-Object { $_ -match "SOLGlossaryCrops: DONE" })) {
        Write-Host "The crop import failed (engine exit $($process.ExitCode)); see $importLog. The guide was not run."
        exit 1
    }
}

# 3. the guide run (last, so its Help shots show the imported crop textures)
$guide = Invoke-WriterRun "SOLWritePlayerGuide" "PlayerGuide.log" "PlayerGuide" "Writing the player guide"
if ($guide.Verdict -ne 0) { exit $guide.Verdict }
Compress-Images $guide.Images

if ($NoCrops) { Write-Host "Done: commit Docs\PLAYER_GUIDE.md and the Docs\Images\33-help-*.png images." }
else {
    Write-Host ("Done: commit Docs\PLAYER_GUIDE.md, the Docs\Images\33-help-*.png and 33-glossary-crop-*.png images " +
        "and Content\SOL\UI\Glossary.")
}
exit 0
