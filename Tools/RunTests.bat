@echo off
rem SOLTest / Copyright (c) 2026 Acid Rain Studios LLC
rem Command-line wrapper for Tools\RunTests.ps1; passes all arguments through.
rem   Tools\RunTests.bat -Filter SOLTest.Kepler -Build
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0RunTests.ps1" %*
exit /b %ERRORLEVEL%
