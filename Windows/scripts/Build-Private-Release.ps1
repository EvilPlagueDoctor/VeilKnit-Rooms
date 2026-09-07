$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $Root
cmake --preset vs2022-x64 -DVKROOMS_PRIVACY_RELEASE=ON
cmake --build --preset vs2022-release
ctest --preset vs2022-release
$Dist = Join-Path $Root "dist\Release"
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
Copy-Item "$Root\out\build\vs2022-x64\Release\VeilKnitRooms.exe" $Dist -Force
Write-Host "Privacy-hardened Windows release: $Dist" -ForegroundColor Green
