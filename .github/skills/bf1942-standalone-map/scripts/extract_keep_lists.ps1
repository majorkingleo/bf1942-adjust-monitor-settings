# extract_keep_lists.ps1 — Apply every keep-list from resolve_keep_list.ps1:
# extract exactly those files from their source archives into ONE staging root.
#
# Policy (b) note: vanilla keep-lists (bf1942 / XPack1 / XPack2) are extracted
# into the SAME staging root as the FH ones. Because both archives use the same
# internal top folders (standardmesh/, texture/, animations/), the files merge
# by internal path, and repacking the staging folder later produces a single
# archive that contains FH + the vanilla assets it borrows - which is what makes
# the standalone self-contained without shipping the whole base game.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File extract_keep_lists.ps1 `
#       -KeepDir "work\keep" -GameRoot "origin" -StageRoot "work\FH"

param(
    [Parameter(Mandatory = $true)][string]$KeepDir,
    [Parameter(Mandatory = $true)][string]$GameRoot,
    [string]$StageRoot = 'work\FH',
    [string]$Tool
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $KeepDir)) { throw "Keep dir not found: $KeepDir" }
if (-not (Test-Path $StageRoot)) { [void](New-Item -ItemType Directory -Force -Path $StageRoot) }
$StageRoot = (Resolve-Path $StageRoot).Path

if (-not $Tool) {
    $Tool = @(
        (Join-Path $PSScriptRoot '..\..\..\..\bin\rfaUnpack.exe'),
        'E:\bf_tk_mod\tk_mod\bin\rfaUnpack.exe'
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Tool) { throw 'rfaUnpack.exe not found; pass -Tool <path>' }
$Tool = (Resolve-Path $Tool).Path

$manifestPath = Join-Path $KeepDir '_manifest.tsv'
if (-not (Test-Path $manifestPath)) { throw "Manifest not found: $manifestPath (re-run resolve_keep_list.ps1)" }

$rows = @()
foreach ($line in Get-Content $manifestPath) {
    if (-not $line.Trim()) { continue }
    $parts = $line -split "`t"
    if ($parts.Count -lt 2) { continue }
    $rows += [PSCustomObject]@{ ListFile = $parts[0]; Archive = $parts[1] }
}

Write-Host ("{0} keep-list(s) to apply" -f $rows.Count)
$grandTotal = 0
$missing = @()

foreach ($r in $rows) {
    $lst = Join-Path $KeepDir $r.ListFile
    if (-not (Test-Path $lst)) { $missing += $r.ListFile; continue }

    # "bf/Mods/FH/Archives/objects.rfa" -> "<GameRoot>\Mods\FH\Archives\objects.rfa"
    $rel = ($r.Archive -replace '^bf/', '') -replace '/', '\'
    $rfa = Join-Path $GameRoot $rel
    if (-not (Test-Path $rfa)) { $missing += ("{0}  (archive missing: {1})" -f $r.ListFile, $rfa); continue }

    $want = @(Get-Content $lst | Where-Object { $_.Trim() })
    $before = @(Get-ChildItem -LiteralPath $StageRoot -Recurse -File -ErrorAction SilentlyContinue).Count

    & $Tool $rfa $StageRoot "-l$lst" | Out-Null

    $after = @(Get-ChildItem -LiteralPath $StageRoot -Recurse -File -ErrorAction SilentlyContinue).Count
    $added = $after - $before
    $grandTotal += $added

    $flag = if ($added -lt $want.Count) { '  <-- SHORT' } else { '' }
    Write-Host ("  {0,6}/{1,-6} {2}{3}" -f $added, $want.Count, $rel, $flag)
}

Write-Host ''
Write-Host ("Extracted {0} file(s) into {1}" -f $grandTotal, $StageRoot)

# ---------------------------------------------------------------------------
# Repair pass: rfaUnpack -l splits list entries on whitespace, so entries whose
# name contains a space are never written (verified: "green_T .dds",
# "stnwall french1_s.dds"). Extract those by 0-based file-table index instead,
# which is parsed straight from the archive directory table.
# ---------------------------------------------------------------------------
function Get-RfaEntryNames([string]$path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $dirOff = [BitConverter]::ToUInt32($bytes, 0)
    $num = [BitConverter]::ToUInt32($bytes, $dirOff)
    $pos = $dirOff + 4
    $names = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $num; $i++) {
        $len = [BitConverter]::ToUInt32($bytes, $pos); $pos += 4
        $names.Add([System.Text.Encoding]::ASCII.GetString($bytes, $pos, $len).TrimEnd([char]0))
        $pos += $len + 24
    }
    return $names
}

$repaired = 0
foreach ($r in $rows) {
    $lst = Join-Path $KeepDir $r.ListFile
    if (-not (Test-Path $lst)) { continue }
    $rel = ($r.Archive -replace '^bf/', '') -replace '/', '\'
    $rfa = Join-Path $GameRoot $rel
    if (-not (Test-Path $rfa)) { continue }

    $todo = @(Get-Content $lst | Where-Object { $_.Trim() } | Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $StageRoot ($_ -replace '/', '\')))
        })
    if (-not $todo) { continue }

    $names = Get-RfaEntryNames $rfa
    $idx = @{}
    for ($i = 0; $i -lt $names.Count; $i++) { $idx[$names[$i].ToLowerInvariant()] = $i }

    foreach ($p in $todo) {
        $key = $p.ToLowerInvariant()
        if (-not $idx.ContainsKey($key)) {
            Write-Host ("  cannot locate in archive: {0}" -f $p)
            continue
        }
        & $Tool $rfa $StageRoot ("-i" + $idx[$key]) | Out-Null
        if (Test-Path -LiteralPath (Join-Path $StageRoot ($p -replace '/', '\'))) { $repaired++ }
        else { Write-Host ("  still missing after index extract: {0}" -f $p) }
    }
}
if ($repaired) {
    Write-Host ("Repaired {0} whitespace-named file(s) via index extraction" -f $repaired)
}

if ($missing.Count) {
    Write-Host ''
    Write-Host '=== problems ==='
    $missing | ForEach-Object { Write-Host ("  {0}" -f $_) }
}
