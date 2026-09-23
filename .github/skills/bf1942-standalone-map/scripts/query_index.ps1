# query_index.ps1 — Query bfmod_tools\all_packages.txt, a global inventory of
# every archive in the Battlefield 1942 install and the files inside them.
#
# The index is a flat list of "<archive>: ./<internal path>" lines. Use it to
# find WHICH archive contains a needed asset, so you can extract just that file
# with `rfaUnpack.exe -l` instead of unpacking hundreds of MB of texture.rfa /
# sound.rfa.
#
# Usage:
#   # where does a given file live?
#   ... -Name "pavlov_house_M1.sm"
#
#   # wildcard search, restricted to one archive family
#   ... -Name "*.sm" -Archive "*/FH/Archives/standardmesh.rfa"
#
#   # everything under a folder, with per-archive counts
#   ... -PathLike "*/Levels/Pavlov/*" -Summary
#
#   # resolve a keep-list (one internal path per line) to its owning archive
#   ... -CheckList work\keep.txt
#
# Notes:
#   * Internal paths are relative to the archive root and use '/'.
#   * Matching is case-insensitive wildcard matching.
#   * A -Name without a dot also matches files by stem (extension ignored).

param(
    [string[]]$Name,
    [string]$PathLike,
    [string]$Archive,
    [string]$CheckList,
    [int]$Limit = 100,
    [switch]$Summary,
    [string]$Index
)

$ErrorActionPreference = 'Stop'

# --- locate the index: default is <repo root>\bfmod_tools\all_packages.txt ---
if (-not $Index) {
    $Index = Join-Path $PSScriptRoot '..\..\..\..\bfmod_tools\all_packages.txt'
}
if (-not (Test-Path $Index)) { throw "Index file not found: $Index (pass -Index <path>)" }
$Index = (Resolve-Path $Index).Path

$lineRe = [regex]'^(?<archive>.*?):\s+\./(?<path>.+)$'

function Read-Index {
    $entries = New-Object System.Collections.Generic.List[object]
    foreach ($line in [System.IO.File]::ReadLines($Index)) {
        $m = $lineRe.Match($line)
        if (-not $m.Success) { continue }
        $p = $m.Groups['path'].Value
        $leaf = $p.Substring($p.LastIndexOf('/') + 1)
        $dot = $leaf.LastIndexOf('.')
        $entries.Add([PSCustomObject]@{
                Archive = $m.Groups['archive'].Value
                Path    = $p
                File    = $leaf
                Stem    = if ($dot -gt 0) { $leaf.Substring(0, $dot) } else { $leaf }
            })
    }
    return $entries
}

$all = Read-Index

# ---------------------------------------------------------------------------
# Mode 1: resolve a keep-list file -> owning archive per entry
# ---------------------------------------------------------------------------
if ($CheckList) {
    if (-not (Test-Path $CheckList)) { throw "Keep-list not found: $CheckList" }

    # path (lowercase) -> archives that contain it
    $byPath = @{}
    foreach ($e in $all) {
        $k = $e.Path.ToLowerInvariant()
        if (-not $byPath.ContainsKey($k)) { $byPath[$k] = New-Object System.Collections.Generic.List[string] }
        $byPath[$k].Add($e.Archive)
    }

    $wanted = @(
        Get-Content $CheckList |
            ForEach-Object { $_.Trim().TrimStart('./').Replace('\', '/') } |
            Where-Object { $_ -ne '' -and -not $_.StartsWith('#') } |
            Sort-Object -Unique
    )

    $rows = foreach ($w in $wanted) {
        $key = $w.ToLowerInvariant()
        if ($byPath.ContainsKey($key)) {
            [PSCustomObject]@{ Path = $w; Found = $true; Archives = ($byPath[$key] -join ', ') }
        }
        else {
            [PSCustomObject]@{ Path = $w; Found = $false; Archives = '<MISSING from index>' }
        }
    }

    $missing = @($rows | Where-Object { -not $_.Found })
    Write-Host ("Resolved {0}/{1} path(s); {2} missing" -f ($wanted.Count - $missing.Count), $wanted.Count, $missing.Count)

    Write-Host ''
    Write-Host '=== per-archive extraction load ==='
    $rows | Where-Object { $_.Found } |
        Group-Object Archives | Sort-Object Count -Descending |
        ForEach-Object { Write-Host ("  {0,7}  {1}" -f $_.Count, $_.Name) }

    if ($missing) {
        Write-Host ''
        Write-Host '=== NOT FOUND (typo, or asset really absent) ==='
        $missing | ForEach-Object { Write-Host ("  {0}" -f $_.Path) }
    }
    return
}

# ---------------------------------------------------------------------------
# Mode 2: search by name / path / archive
# ---------------------------------------------------------------------------
$sel = $all
if ($Archive) { $sel = $sel | Where-Object { $_.Archive -like $Archive } }
if ($PathLike) { $sel = $sel | Where-Object { $_.Path -like $PathLike } }
if ($Name) {
    $sel = $sel | Where-Object {
        $hit = $false
        foreach ($n in $Name) {
            if ($_.File -like $n) { $hit = $true; break }
            if ($n -notmatch '\.' -and $_.Stem -like $n) { $hit = $true; break }
        }
        $hit
    }
}

$matches = @($sel)

if ($Summary) {
    Write-Host ("{0} matching entry/entries" -f $matches.Count)
    Write-Host ''
    $matches | Group-Object Archive | Sort-Object Count -Descending |
        ForEach-Object { Write-Host ("  {0,7}  {1}" -f $_.Count, $_.Name) }
    return
}

Write-Host ("{0} matching entry/entries (showing up to {1})" -f $matches.Count, $Limit)
$matches | Select-Object -First $Limit | ForEach-Object {
    Write-Host ("  {0}`n      -> {1}" -f $_.Path, $_.Archive)
}
if ($matches.Count -gt $Limit) {
    Write-Host ("  ... {0} more (raise -Limit or narrow the pattern)" -f ($matches.Count - $Limit))
}
