<#
.SYNOPSIS
    Create the project-local Python environment (.venv) for the measurement
    harness, using the pinned versions in Scripts/analysis/requirements.txt.
#>
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)

if (-not (Test-Path '.venv/Scripts/python.exe')) {
    python -m venv .venv
    if ($LASTEXITCODE -ne 0) { throw 'Failed to create .venv (is Python 3.11+ on PATH?)' }
}

& .venv/Scripts/python.exe -m pip install --upgrade pip
& .venv/Scripts/python.exe -m pip install -r Scripts/analysis/requirements.txt
if ($LASTEXITCODE -ne 0) { throw 'pip install failed' }
