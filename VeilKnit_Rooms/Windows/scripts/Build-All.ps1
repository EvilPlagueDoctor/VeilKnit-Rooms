param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

Write-Host "== VeilKnit Rooms app-only build ==" -ForegroundColor Cyan
Write-Host "The standalone VeilKnit daemon is built and run separately." -ForegroundColor DarkGray

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake was not found. Install the C++ CMake tools through Visual Studio Installer."
}

Write-Host "Configuring Visual Studio 2022 project..." -ForegroundColor Cyan
cmake --preset vs2022-x64

$BuildPreset = if ($Configuration -eq "Release") { "vs2022-release" } else { "vs2022-debug" }
Write-Host "Building desktop app ($Configuration)..." -ForegroundColor Cyan
cmake --build --preset $BuildPreset

if (-not $SkipTests) {
    Write-Host "Running core tests..." -ForegroundColor Cyan
    ctest --preset $(if ($Configuration -eq "Release") { "vs2022-release" } else { "vs2022-debug" })
}

$Dist = Join-Path $Root "dist\$Configuration"
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$AppExe = Join-Path $Root "out\build\vs2022-x64\$Configuration\VeilKnitRooms.exe"
if (-not (Test-Path $AppExe)) { throw "App executable was not produced at $AppExe" }
Copy-Item $AppExe $Dist -Force

Copy-Item "$Root\README.md" $Dist -Force
Copy-Item "$Root\docs\USER_GUIDE.md" $Dist -Force
Copy-Item "$Root\docs\CURRENT_LIMITATIONS.md" $Dist -Force

Write-Host "Rooms app assembled in: $Dist" -ForegroundColor Green
Write-Host "Start the standalone VeilKnit daemon before launching VeilKnitRooms.exe." -ForegroundColor Yellow
