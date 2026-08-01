@echo off
setlocal
cd /d "%~dp0"
echo == Cleaning generated Android build files ==
call gradlew.bat clean
if exist ".gradle" rmdir /s /q ".gradle"
if exist ".kotlin" rmdir /s /q ".kotlin"
if exist "app\build" rmdir /s /q "app\build"
echo Source tree restored to project files only.
endlocal
