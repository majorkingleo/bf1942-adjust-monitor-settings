# repack_standalone.ps1 — Phase 1.4: pack the staged trees into the .rfa set the
# standalone mod ships.
#
# Input is the staging tree produced by extract_keep_lists.ps1 (plus the level
# extracted with rfaUnpack). Each staged folder is packed with the base folder
# name its archive expects, so internal paths round-trip:
#
#   work\FH\texture\...        + base "texture"    -> texture.rfa:        texture/...
#   work\FH\standardmesh\...   + base "standardmesh" -> standardMesh.rfa: standardmesh/...
#   work\FH\bf1942\game\...    + base "bf1942"     -> bf1942/game.rfa:    bf1942/game/...
#   work\map\bf1942\levels\... + base "bf1942"     -> bf1942/levels/<Map>.rfa
#
# Output mirrors the game's Archives layout under -OutDir, ready to copy into
# Mods\<modname>\Archives\.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File repack_standalone.ps1 `
#       -StageRoot "work\FH" -MapDir "work\map\bf1942\levels\Battle_Of_Pavlov-1942" `
#       -OutDir "work\build"

param(
    [string]$StageRoot = 'work\FH',
    # One or more extracted level folders, comma-separated. Each becomes its own
    # bf1942\levels\<Name>.rfa, so the mod can ship several levels at once.
    [Parameter(Mandatory = $true)][string[]]$MapDir,
    [string]$OutDir = 'work\build',
    [string]$Tool,
    [switch]$NoCompress
)

$ErrorActionPreference = 'Stop'

# `-File` delivers a comma list as ONE string, so split it here
$mapDirs = @()
foreach ($m in $MapDir) { $mapDirs += @($m -split ',' | Where-Object { $_.Trim() }) }
if (-not $mapDirs.Count) { throw 'At least one -MapDir is required' }
foreach ($m in $mapDirs) { if (-not (Test-Path $m)) { throw "Map dir not found: $m" } }
if (-not (Test-Path $OutDir)) { [void](New-Item -ItemType Directory -Force -Path $OutDir) }

if (-not $Tool) {
    $Tool = @(
        (Join-Path $PSScriptRoot '..\..\..\..\bin\rfaPack.exe'),
        'E:\bf_tk_mod\tk_mod\bin\rfaPack.exe'
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Tool) { throw 'rfaPack.exe not found; pass -Tool <path>' }
$Tool = (Resolve-Path $Tool).Path

$lister = Join-Path $PSScriptRoot 'list_rfa.ps1'

# rfaPack can only pack a WHOLE folder and has no exclude switch, so when a spec
# needs a subfolder only (or must drop a subtree) we pack a hardlink mirror of
# just the wanted files. Hardlinks keep this cheap and use no extra disk.
function New-Mirror([string]$Src, [string]$Dst, [string[]]$Include, [string[]]$Exclude) {
    if (Test-Path $Dst) { Remove-Item -LiteralPath $Dst -Recurse -Force }
    [void](New-Item -ItemType Directory -Force -Path $Dst)
    foreach ($f in Get-ChildItem -LiteralPath $Src -Recurse -File -ErrorAction SilentlyContinue) {
        $rel = $f.FullName.Substring($Src.Length).TrimStart('\').Replace('\', '/')
        if ($Include -and -not (@($Include | Where-Object { $rel -like $_ }).Count)) { continue }
        if ($Exclude -and (@($Exclude | Where-Object { $rel -like $_ }).Count)) { continue }
        $dest = Join-Path $Dst $rel.Replace('/', '\')
        $destDir = Split-Path $dest -Parent
        if (-not (Test-Path $destDir)) { [void](New-Item -ItemType Directory -Force -Path $destDir) }
        try { New-Item -ItemType HardLink -Path $dest -Target $f.FullName -Force | Out-Null }
        catch { Copy-Item -LiteralPath $f.FullName -Destination $dest -Force }
    }
}

# source folder, base folder name inside the archive, output .rfa
$specs = @(
    [PSCustomObject]@{ Key = 'objects'; Source = (Join-Path $StageRoot 'objects'); Base = 'objects'; Out = 'objects.rfa' }
    [PSCustomObject]@{ Key = 'standardmesh'; Source = (Join-Path $StageRoot 'standardmesh'); Base = 'standardmesh'; Out = 'standardMesh.rfa' }
    [PSCustomObject]@{ Key = 'texture'; Source = (Join-Path $StageRoot 'texture'); Base = 'texture'; Out = 'texture.rfa' }
    [PSCustomObject]@{ Key = 'sound'; Source = (Join-Path $StageRoot 'sound'); Base = 'sound'; Out = 'sound.rfa' }
    [PSCustomObject]@{ Key = 'animations'; Source = (Join-Path $StageRoot 'animations'); Base = 'animations'; Out = 'animations.rfa' }
    [PSCustomObject]@{ Key = 'treemesh'; Source = (Join-Path $StageRoot 'treemesh'); Base = 'treemesh'; Out = 'treemesh.rfa' }
    [PSCustomObject]@{ Key = 'aimeshes'; Source = (Join-Path $StageRoot 'aimeshes'); Base = 'aimeshes'; Out = 'aimeshes.rfa' }
    [PSCustomObject]@{ Key = 'menu'; Source = (Join-Path $StageRoot 'menu'); Base = 'menu'; Out = 'menu.rfa' }
    # The staged "bf1942" folder also holds a stray levels\<Map>\ subtree (level
    # files that belong to the LEVEL archive). Packing the whole folder embeds a
    # PARTIAL level inside game.rfa, and the engine then exits cleanly at startup
    # (exit 0x00000000) with no log. game.rfa must contain game/** ONLY.
    [PSCustomObject]@{ Key = 'game'; Source = (Join-Path $StageRoot 'bf1942'); Base = 'bf1942'; Out = 'bf1942\game.rfa'; Include = @('game/*'); Forbidden = @('bf1942/levels/*') }
)

# One archive per level. A -MapDir is the extracted level folder
# (...\bf1942\levels\<Map>); the archive is built from a MIRROR of just that
# level, because all levels usually share one parent folder and packing the
# parent would put every level into every archive.
foreach ($md in $mapDirs) {
    $mn = Split-Path $md -Leaf
    $mp = Split-Path (Split-Path $md -Parent) -Parent   # ...\bf1942\levels -> ...\bf1942
    $specs += [PSCustomObject]@{
        # NOTE: no ':' in Key - it is used as a mirror folder name
        Key     = ('map_' + $mn)
        Source  = $mp
        Base    = 'bf1942'
        # paths are relative to Source, which is the folder ABOVE 'levels', so the
        # pattern must include the 'levels/' prefix or nothing matches
        Include = @("levels/$mn/*")
        Out     = ('bf1942\levels\{0}.rfa' -f $mn)
    }
}

$flags = @()
if (-not $NoCompress) { $flags += '-Compress' }

$results = @()
foreach ($s in $specs) {
    if (-not (Test-Path $s.Source)) {
        Write-Host ("SKIP  {0,-13} (no staging folder {1})" -f $s.Key, $s.Source)
        continue
    }
    $src = (Resolve-Path $s.Source).Path
    $files = @(Get-ChildItem -LiteralPath $src -Recurse -File -ErrorAction SilentlyContinue)
    if ($s.Include -or $s.Exclude) {
        $files = @($files | Where-Object {
            $rel = $_.FullName.Substring($src.Length).TrimStart('\').Replace('\', '/')
            $keep = $true
            if ($s.Include) { $keep = [bool](@($s.Include | Where-Object { $rel -like $_ }).Count) }
            if ($keep -and $s.Exclude) { $keep = -not (@($s.Exclude | Where-Object { $rel -like $_ }).Count) }
            $keep
        })
    }
    if ($files.Count -eq 0) {
        Write-Host ("SKIP  {0,-13} (no staged files match)" -f $s.Key)
        continue
    }

    $target = Join-Path $OutDir $s.Out
    $targetDir = Split-Path $target -Parent
    if (-not (Test-Path $targetDir)) { [void](New-Item -ItemType Directory -Force -Path $targetDir) }
    if (Test-Path $target) { Remove-Item -LiteralPath $target -Force }

    $packSrc = $src
    $mirror = $null
    if ($s.Include -or $s.Exclude) {
        $mirror = Join-Path $OutDir ('_mirror\' + $s.Key)
        New-Mirror $src $mirror $s.Include $s.Exclude
        $packSrc = $mirror
    }
    & $Tool $packSrc $s.Base $target @flags | Out-Null

    if (-not (Test-Path $target)) {
        Write-Host ("FAIL  {0,-13} rfaPack produced no archive" -f $s.Key)
        continue
    }

    # round-trip check: entry count in the new archive vs files staged
    $entries = $null
    $names = @()
    if (Test-Path $lister) {
        $names = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $lister $target -Names 2>&1 |
            Where-Object { $_ -and $_ -is [string] })
        $entries = $names.Count
    }

    # guard: never let one archive swallow paths that belong to another one
    $violations = @()
    if ($s.Forbidden) {
        foreach ($pat in @($s.Forbidden)) {
            if (-not $pat) { continue }
            $rx = [regex]::Escape($pat).Replace('\*', '.*')
            $c = @($names | Where-Object { $_ -match $rx }).Count
            if ($c -gt 0) { $violations += ('{0} x {1}' -f $c, $pat) }
        }
    }

    # guard: rfaPack writes "<base>/<path relative to SOURCE>", so getting the
    # source folder one level wrong yields a doubled prefix (bf1942/bf1942/game/...)
    # and the engine silently ignores the whole archive. Every entry must start
    # with the base folder name exactly once.
    $badPrefix = @($names | Where-Object { $_ -notlike ($s.Base.Replace('\','/') + '/*') })
    if ($badPrefix.Count) {
        $violations += ('{0} x wrong prefix (expected {1}/, e.g. {2})' -f $badPrefix.Count, $s.Base, $badPrefix[0])
    }

    $mb = [math]::Round((Get-Item $target).Length / 1MB, 2)
    $ok = if ($violations.Count) { 'FORBIDDEN' }
          elseif ($null -eq $entries) { '?' }
          elseif ($entries -eq $files.Count) { 'ok' }
          else { 'MISMATCH' }
    Write-Host ("PACK  {0,-13} {1,6} file(s) -> {2,6} entr(ies)  {3,7} MB  {4}" -f $s.Key, $files.Count, $entries, $mb, $ok)
    if ($violations.Count) { Write-Host ('      ^ FORBIDDEN path(s): ' + ($violations -join ', ')) }

    if ($mirror -and (Test-Path $mirror)) { Remove-Item -LiteralPath $mirror -Recurse -Force -ErrorAction SilentlyContinue }
    $results += [PSCustomObject]@{ Key = $s.Key; Staged = $files.Count; Entries = $entries; MB = $mb; Out = $s.Out; Status = $ok }
}

Write-Host ''
Write-Host '=== produced archives ==='
$results | ForEach-Object { Write-Host ("  {0,7} MB  {1}" -f $_.MB, (Join-Path $OutDir $_.Out)) }
$totalMb = [math]::Round((($results | Measure-Object MB -Sum).Sum), 2)
Write-Host ("  TOTAL {0} MB across {1} archive(s)" -f $totalMb, $results.Count)

$bad = @($results | Where-Object { $_.Status -ne 'ok' })
if ($bad.Count) {
    Write-Host ''
    Write-Host '=== needs attention ==='
    $bad | ForEach-Object { Write-Host ("  {0}: staged {1}, archive entries {2}" -f $_.Key, $_.Staged, $_.Entries) }
}
