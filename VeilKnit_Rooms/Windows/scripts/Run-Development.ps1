param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$App = Join-Path $Root "out\build\vs2022-x64\$Configuration\VeilKnitRooms.exe"

if (-not (Test-Path $App)) {
    throw "Build the Rooms app first with scripts\Build-All.ps1 -Configuration $Configuration"
}

Write-Host "The standalone VeilKnit daemon must already be running and logged in." -ForegroundColor Yellow
Write-Host "Starting VeilKnit Rooms..." -ForegroundColor Cyan
Start-Process $App
