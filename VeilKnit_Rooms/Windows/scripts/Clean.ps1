$Root = Split-Path -Parent $PSScriptRoot
Remove-Item "$Root\out" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "$Root\dist" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "$Root\daemon\target" -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "Build outputs removed." -ForegroundColor Green
