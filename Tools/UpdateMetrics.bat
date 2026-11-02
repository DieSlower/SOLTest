@echo off
rem SOLTest / Copyright (c) 2026 Acid Rain Studios LLC
rem Refreshes Docs\PROJECT_METRICS.md and the README effort summary from the Claude Code session logs.
rem   Tools\UpdateMetrics.bat            rewrite the docs
rem   Tools\UpdateMetrics.bat --check    print the totals only
python "%~dp0Metrics\update_metrics.py" %*
exit /b %ERRORLEVEL%
