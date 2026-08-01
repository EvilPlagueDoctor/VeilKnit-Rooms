@echo off
setlocal EnableExtensions

rem Build only the C++ VeilKnit Rooms desktop application.
rem Place this file in the root VeilKnit-Rooms folder.

pushd "%~dp0" || exit /b 1

echo ========================================
echo   VeilKnit Rooms - C++ Build
echo ========================================
echo.

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake was not found in PATH.
    echo Open "Developer Command Prompt for VS 2022" or install the
    echo Visual Studio C++ CMake tools.
    popd
    exit /b 1
)

if not exist "CMakePresets.json" (
    echo ERROR: CMakePresets.json was not found.
    echo This batch file must be placed in the VeilKnit-Rooms root folder.
    popd
    exit /b 1
)

echo [1/3] Configuring Visual Studio 2022 x64 project...
cmake --preset vs2022-x64
if errorlevel 1 goto failed

echo.
echo [2/3] Building VeilKnitRooms.exe in Release mode...
cmake --build --preset vs2022-release --target VeilKnitRooms
if errorlevel 1 goto failed

echo.
echo [3/3] Copying executable to dist\Release...
if not exist "dist\Release" mkdir "dist\Release"

set "APP_EXE=out\build\vs2022-x64\Release\VeilKnitRooms.exe"
if not exist "%APP_EXE%" (
    echo ERROR: The build completed, but this executable was not found:
    echo        %APP_EXE%
    goto failed
)

copy /Y "%APP_EXE%" "dist\Release\VeilKnitRooms.exe" >nul
if errorlevel 1 goto failed

echo.
echo Build successful.
echo Executable:
echo   %CD%\dist\Release\VeilKnitRooms.exe
echo.
popd
exit /b 0

:failed
echo.
echo BUILD FAILED.
popd
exit /b 1
