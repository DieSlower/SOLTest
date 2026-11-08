@echo off
rem SOLTest / Copyright (c) 2026 Acid Rain Studios LLC
rem Regenerates Docs\PLAYER_GUIDE.md and its images (Tools\WritePlayerGuide.ps1); passes all arguments through.
rem   Tools\WritePlayerGuide.bat           regenerate (close the editor first)
rem   Tools\WritePlayerGuide.bat -Build    build SOLTestEditor first
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0WritePlayerGuide.ps1" %*
exit /b %ERRORLEVEL%
