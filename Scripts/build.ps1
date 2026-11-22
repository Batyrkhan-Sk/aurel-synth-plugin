<#
.SYNOPSIS
    Configure and build AUREL.
.EXAMPLE
    ./Scripts/build.ps1                  # full build (JUCE + tests + tools), Debug
    ./Scripts/build.ps1 -Config Release
    ./Scripts/build.ps1 -Core            # DSP core + tests only, no JUCE (fast)
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Config = 'Debug',
    [switch] $Core
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)

if (-not $Core -and -not (Test-Path 'external/JUCE/CMakeLists.txt')) {
    git submodule update --init --depth 1
    if ($LASTEXITCODE -ne 0) { throw 'JUCE submodule checkout failed' }
}

$configurePreset = if ($Core) { 'vs2022-core' } else { 'vs2022' }
$buildPreset = ($(if ($Core) { 'core-' } else { '' }) + $Config.ToLower())

cmake --preset $configurePreset
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($configurePreset)" }

cmake --build --preset $buildPreset
if ($LASTEXITCODE -ne 0) { throw "Build failed ($buildPreset)" }
