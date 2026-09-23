# extract_rfa.ps1 — Extract a Battlefield 1942 .rfa archive using the proven
# community tool rfaUnpack.exe (which handles BF1942's custom compression).
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File extract_rfa.ps1 <input.rfa> <outputDir>
#   powershell -NoProfile -ExecutionPolicy Bypass -File extract_rfa.ps1 <input.rfa> <outputDir> -Tool "C:\path\rfaUnpack.exe"
#
# Default tool path: E:\bf_tk_mod\tk_mod\bin\rfaUnpack.exe
#
# For a pure-PowerShell way to LIST contents without extracting, use list_rfa.ps1.

param(
    [Parameter(Mandatory = $true)][string]$InputFile,
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [string]$Tool = 'E:\bf_tk_mod\tk_mod\bin\rfaUnpack.exe'
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $InputFile)) { throw "Input file not found: $InputFile" }
if (-not (Test-Path $Tool))      { throw "rfaUnpack.exe not found at: $Tool" }

if (-not (Test-Path $OutputDir)) { [void](New-Item -ItemType Directory -Force -Path $OutputDir) }

& $Tool $InputFile $OutputDir
Write-Host ("Extracted '{0}' to '{1}'" -f (Split-Path $InputFile -Leaf), $OutputDir)
