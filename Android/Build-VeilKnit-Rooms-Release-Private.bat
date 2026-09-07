@echo off
setlocal EnableExtensions
cd /d "%~dp0"
call gradlew.bat --no-daemon clean assembleRelease
exit /b %errorlevel%
