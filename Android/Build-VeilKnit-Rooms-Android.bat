@echo off
setlocal
cd /d "%~dp0"
echo == Building VeilKnit Rooms Android ==
call gradlew.bat :app:assembleDebug
if errorlevel 1 exit /b %errorlevel%
echo.
echo APK:
echo   app\build\outputs\apk\debug\VeilKnitRooms-debug.apk
endlocal
