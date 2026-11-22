<#
.SYNOPSIS
    Validate the built AUREL VST3 with Tracktion pluginval.
.DESCRIPTION
    Downloads pluginval (pinned version + SHA-256) into build/tools on first
    use, then runs it at the requested strictness over all six target sample
    rates and a range of block sizes. Exit code is non-zero on failure.
.EXAMPLE
    ./Scripts/pluginval.ps1                       # Release build, strictness 10
    ./Scripts/pluginval.ps1 -Config Debug
    ./Scripts/pluginval.ps1 -SkipGui              # no editor windows
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Config = 'Release',
    [ValidateRange(1, 10)] [int] $Strictness = 10,
    [switch] $SkipGui
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)

$version = '1.0.4'
$sha256 = 'C08E61CE3B96DB41636F8EC7E76F4C7E2C13EBDAC7FA1B5A1F52B4F32EC715AB'
$toolDir = "build/tools/pluginval-$version"
$exe = "$toolDir/bin/pluginval.exe"

if (-not (Test-Path $exe)) {
    New-Item -ItemType Directory -Force $toolDir | Out-Null
    $zip = "$toolDir/pluginval_Windows.zip"
    $url = "https://github.com/Tracktion/pluginval/releases/download/v$version/pluginval_Windows.zip"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    $actual = (Get-FileHash $zip -Algorithm SHA256).Hash
    if ($actual -ne $sha256) {
        Remove-Item $zip
        throw "pluginval download hash mismatch (got $actual)"
    }
    Expand-Archive $zip -DestinationPath "$toolDir/bin" -Force
}

$plugin = "build/vs2022/Source/Plugin/AUREL_artefacts/$Config/VST3/AUREL.vst3"
if (-not (Test-Path $plugin)) { throw "Plugin not built: $plugin (run ./Scripts/build.ps1 -Config $Config)" }

$reportDir = "build/reports/pluginval-$Config"
New-Item -ItemType Directory -Force $reportDir | Out-Null

$pvArgs = @(
    '--strictness-level', $Strictness,
    '--sample-rates', '44100,48000,88200,96000,176400,192000',
    '--block-sizes', '32,64,128,256,512,1024,2048',
    '--random-seed', '1234',
    '--timeout-ms', '600000',
    '--output-dir', (Resolve-Path $reportDir).Path
)
if ($SkipGui) { $pvArgs += '--skip-gui-tests' }
$pvArgs += @('--validate', (Resolve-Path $plugin).Path)

# pluginval is a GUI-subsystem executable: `& $exe` would return before it
# finishes, so wait on the process explicitly and capture its console output.
$log = Join-Path (Resolve-Path $reportDir).Path 'pluginval-console.log'
$quoted = $pvArgs | ForEach-Object { if ("$_" -match '\s') { '"' + $_ + '"' } else { "$_" } }
$proc = Start-Process -FilePath (Resolve-Path $exe).Path -ArgumentList $quoted `
    -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log
$code = $proc.ExitCode

Get-Content $log | Select-String -Pattern '^Starting tests|FAILED|!!!|ALL TESTS PASSED|SUCCESS' |
    ForEach-Object { Write-Host $_.Line }
if ($code -ne 0) { throw "pluginval FAILED (exit $code). Full log: $log" }
Write-Host "pluginval passed at strictness $Strictness ($Config)." -ForegroundColor Green
