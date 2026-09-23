# unpack-rfa.ps1 — Extract files from a Battlefield 1942 .rfa archive.
#
# Wraps rfaUnpack.exe (the only tool that handles BF1942's custom compression)
# and smooths over its sharp edges:
#   * creates the output directory (rfaUnpack errors out if it does not exist)
#   * turns -Only <internal paths> into the working -l list-file form, because
#     the tool's own -f <name> switch is broken and always reports "Name Not found"
#   * reports how many files were actually written, so silently skipped entries
#     (e.g. bad -Only paths) are not mistaken for success
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File unpack-rfa.ps1 `
#       -Archive <a.rfa> -OutDir <dir>
#
#   # only some files (full internal paths, comma-separated in ONE argument:
#   # the `powershell -File` parser does not split comma lists into arrays)
#   ... -Only "bf1942/levels/Battle_Of_Pavlov-1942/Init.con,bf1942/levels/Battle_Of_Pavlov-1942/Conquest.con"
#
#   # only some indices (0-based file table), comma-separated
#   ... -Index "0,5"
#
#   # explicit tool location
#   ... -Tool "E:\bf_tk_mod\tk_mod\bin\rfaUnpack.exe"
#
# NOTE: -OutDir is a PARENT directory. rfaUnpack preserves the archive's internal
# paths including the top folder, so extracting texture.rfa into work\FH yields
# work\FH\texture\... — matching the rfaPack.exe round trip.

param(
    [Parameter(Mandatory = $true)][string]$Archive,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string[]]$Only,
    [string]$Index,
    [string]$Tool,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

# --- normalise the selection arguments -------------------------------------
# `powershell -File script.ps1 -Only "a,b"` hands over ONE string containing the
# comma (the -File argument parser does not build arrays), while a live-session
# call `& script.ps1 -Only a,b` produces a real array. Flatten both forms.
$onlyList = @()
if ($Only) {
    $onlyList = @(
        $Only |
            ForEach-Object { $_ -split '[,;\r\n]' } |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ -ne '' }
    )
}

$indexList = @()
if ($Index) {
    $indexList = @(
        $Index -split '[,;\s]+' |
            Where-Object { $_ -ne '' } |
            ForEach-Object { [int]$_ }
    )
}

# --- resolve the tool: -Tool, else the in-repo bin\, else the legacy checkout ---
if (-not $Tool) {
    $Tool = @(
        (Join-Path $PSScriptRoot '..\..\..\..\bin\rfaUnpack.exe'),
        'E:\bf_tk_mod\tk_mod\bin\rfaUnpack.exe'
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Tool) { throw 'rfaUnpack.exe not found; pass -Tool <path>' }
$Tool = (Resolve-Path $Tool).Path

if (-not (Test-Path $Archive)) { throw "Archive not found: $Archive" }
$Archive = (Resolve-Path $Archive).Path

if ($onlyList.Count -gt 0 -and $indexList.Count -gt 0) { throw 'Use either -Only or -Index, not both.' }

# --- rfaUnpack will NOT create the output directory for us ---
if (-not (Test-Path $OutDir)) { [void](New-Item -ItemType Directory -Force -Path $OutDir) }
$OutDir = (Resolve-Path $OutDir).Path

function Get-FileCount {
    @(Get-ChildItem -LiteralPath $OutDir -Recurse -File -ErrorAction SilentlyContinue).Count
}

$before = Get-FileCount

if ($onlyList.Count -gt 0) {
    $listFile = Join-Path ([System.IO.Path]::GetTempPath()) ('rfa-{0}.lst' -f [guid]::NewGuid().ToString('N'))
    try {
        # -l requires full internal paths with forward slashes
        ($onlyList -replace '\\', '/') | Set-Content -LiteralPath $listFile -Encoding ASCII
        & $Tool $Archive $OutDir "-l$listFile"
    }
    finally {
        Remove-Item -LiteralPath $listFile -Force -ErrorAction SilentlyContinue
    }
}
elseif ($indexList.Count -gt 0) {
    foreach ($i in $indexList) { & $Tool $Archive $OutDir "-i$i" }
}
else {
    & $Tool $Archive $OutDir
}

$after = Get-FileCount
$added = $after - $before

if (-not $Quiet) {
    Write-Host ('rfaUnpack: {0} -> {1}' -f (Split-Path $Archive -Leaf), $OutDir)
    Write-Host ("  {0} file(s) written, {1} file(s) in target now" -f $added, $after)
}

if (($onlyList.Count -gt 0 -or $indexList.Count -gt 0) -and $added -eq 0) {
    Write-Warning 'No files were written. Check that -Only entries are FULL internal paths (e.g. bf1942/levels/<Map>/Init.con) — basenames are skipped silently.'
}
