<#
.SYNOPSIS
    Run the C++ test suite (CTest/Catch2) and the Python measurement tests.
    Build first with ./Scripts/build.ps1 using the same -Config / -Core.
.EXAMPLE
    ./Scripts/test.ps1
    ./Scripts/test.ps1 -Config Release
    ./Scripts/test.ps1 -Core
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Config = 'Debug',
    [switch] $Core
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)

$testPreset = ($(if ($Core) { 'core-' } else { '' }) + $Config.ToLower())
$failed = $false

Write-Host "== C++ tests ($testPreset) ==" -ForegroundColor Cyan
ctest --preset $testPreset
if ($LASTEXITCODE -ne 0) { $failed = $true }

Write-Host '== Python measurement tests ==' -ForegroundColor Cyan
$python = '.venv/Scripts/python.exe'
if (-not (Test-Path $python)) {
    Write-Warning 'Python venv missing: run ./Scripts/setup-python.ps1 (measurement tests skipped)'
    $failed = $true
} else {
    $buildDir = if ($Core) { 'vs2022-core' } else { 'vs2022' }
    $env:AUREL_RENDER = (Resolve-Path "build/$buildDir/Tools/Render/$Config/aurel_render.exe" -ErrorAction SilentlyContinue)
    & $python -m pytest Scripts/analysis -q
    if ($LASTEXITCODE -ne 0) { $failed = $true }
}

if ($failed) { throw 'Tests FAILED' }
Write-Host 'All tests passed.' -ForegroundColor Green
