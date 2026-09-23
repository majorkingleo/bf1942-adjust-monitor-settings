# resolve_keep_list.ps1 — Walk a BF1942 level's dependency closure and emit
# per-archive keep-lists that rfaUnpack.exe -l can consume directly.
#
# Why: the shared FH archives are 190-707 MB each, but a single map uses a small
# slice of them. This script resolves names -> files using the archive index
# (bfmod_tools\all_packages.txt) plus the already-extracted object .con content,
# so only the referenced files ever get unpacked.
#
# Reference forms handled (verified against FH content):
#   ObjectTemplate.create  <type> <name>      defines template <name> in this file
#   ObjectTemplate.addTemplate <name>         edge to another template
#   ObjectTemplate.geometry <gname>           edge to a geometry template
#   GeometryTemplate.create <type> <gname>    + GeometryTemplate.file <mesh>
#                                             + GeometryTemplate.setSkin <path>
#   run <path>                                include, relative to the object folder
#   textureManager.alternativePath <path>     whole texture folder
#   StaticObjects.con / PreCache.con Object.create <name>
#   game.setKit / game.setTeamSkin / setObjectTemplate <name>
#
# Objects are treated as self-contained units: if any file of an object folder is
# needed, the whole folder is kept (.con, .inc, .ssc, .rs next to it), because
# includes, network info and sound scripts are resolved relative to that folder.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File resolve_keep_list.ps1 `
#       -MapDir "work\map\bf1942\levels\Battle_Of_Pavlov-1942" `
#       -ModRoot "work\FH" -OutDir "work\keep"

param(
    [Parameter(Mandatory = $true)][string]$MapDir,
    [string]$ModRoot = 'work\FH',
    [string]$VanillaObjectRoot = 'work\vanilla',
    [string]$OutDir = 'work\keep',
    [string]$Index,
    [switch]$ExcludeVanilla,
    [switch]$SkipSound,
    [switch]$IncludeMenu
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $MapDir)) { throw "Map dir not found: $MapDir" }
if (-not $Index) { $Index = Join-Path $PSScriptRoot '..\..\..\..\bfmod_tools\all_packages.txt' }
if (-not (Test-Path $Index)) { throw "Index not found: $Index" }
if (-not (Test-Path $OutDir)) { [void](New-Item -ItemType Directory -Force -Path $OutDir) }

# ---------------------------------------------------------------------------
# 1. Load the archive index
# ---------------------------------------------------------------------------
Write-Host 'Loading archive index ...'
$lineRe = [regex]'^(?<archive>.*?):\s+\./(?<path>.+)$'
$byPath = @{}    # lower internal path -> array of archives that contain it
$exactPath = @{} # lower internal path -> exact spelling used inside the archive
$byStem = @{}    # lower file stem   -> array of entries
$entries = New-Object System.Collections.Generic.List[object]

# The shared FH archives hold the reusable assets. A single-map standalone must
# NOT pull from other level archives (every FH level ships its own StandardMesh/
# Texture overlays) nor from unrelated mods (DesertCombat ships its own copies of
# vanilla-looking names).
$sharedFh = '*/FH/Archives/*'
$isSharedFh = { param($a) $a -like $sharedFh -and $a -notlike '*/levels/*' }

# Policy (b): FH does not ship every vanilla asset it uses (ammobox, cabledrm_m1,
# eubed_m1, ...) and reaches the base game at runtime through the mod path. To
# make the standalone self-contained those files are pulled from the VANILLA
# archives and merged into the custom mod, so fall back to them for meshes that
# FH does not have. XPack1/XPack2 are official expansions; DC/DCX are not.
$isVanilla = { param($a) $a -match '^bf/Mods/(bf1942|XPack1|XPack2)/Archives/' }

foreach ($line in [System.IO.File]::ReadLines((Resolve-Path $Index).Path)) {
    $m = $lineRe.Match($line)
    if (-not $m.Success) { continue }
    $p = $m.Groups['path'].Value
    $leaf = $p.Substring($p.LastIndexOf('/') + 1)
    $dot = $leaf.LastIndexOf('.')
    $e = [PSCustomObject]@{
        Archive = $m.Groups['archive'].Value
        Path    = $p
        File    = $leaf
        Stem    = if ($dot -gt 0) { $leaf.Substring(0, $dot) } else { $leaf }
    }
    $entries.Add($e)
    $pk = $p.ToLowerInvariant()
    if (-not $byPath.ContainsKey($pk)) { $byPath[$pk] = New-Object System.Collections.Generic.List[string] }
    $byPath[$pk].Add($e.Archive)
    if (-not $exactPath.ContainsKey($pk)) { $exactPath[$pk] = $p }
    $k = $e.Stem.ToLowerInvariant()
    if (-not $byStem.ContainsKey($k)) { $byStem[$k] = New-Object System.Collections.Generic.List[object] }
    $byStem[$k].Add($e)
}
Write-Host ("  {0} entries indexed" -f $entries.Count)

# Pick the owning archive for an internal path. The same internal path exists in
# several archives (objects.rfa in FH, bf1942, DC, ...); favour the shared FH
# archive so extracted FH content is never attributed to another mod.
function Resolve-ArchiveFor([string]$internalPath, [bool]$preferVanilla = $false) {
    $pk = $internalPath.ToLowerInvariant()
    if (-not $byPath.ContainsKey($pk)) { return $null }
    $cands = $byPath[$pk]
    # files staged from the vanilla extraction root must be attributed back to
    # their vanilla archive, not to the FH archive that shares the same path
    if ($preferVanilla) {
        foreach ($c in $cands) { if (& $isVanilla $c) { return $c } }
    }
    foreach ($c in $cands) { if (& $isSharedFh $c) { return $c } }
    foreach ($c in $cands) { if (& $isVanilla $c) { return $c } }
    return $cands[0]
}

# ---------------------------------------------------------------------------
# 2. Seed names from the level
# ---------------------------------------------------------------------------
$precache = @(); $static = @(); $texPaths = @(); $kits = @(); $skins = @(); $spawn = @()

foreach ($f in Get-ChildItem $MapDir -Recurse -File -Filter *.con) {
    foreach ($line in [System.IO.File]::ReadLines($f.FullName)) {
        if ($f.Name -eq 'PreCache.con' -and $line -match '^\s*Object\.create\s+(\S+)') { $precache += $Matches[1] }
        if ($f.Name -eq 'StaticObjects.con' -and $line -match '^\s*Object\.create\s+(\S+)') { $static += $Matches[1] }
        if ($f.Name -eq 'Init.con') {
            if ($line -match 'textureManager\.alternativePath\s+(\S+)') { $texPaths += $Matches[1] }
            if ($line -match 'game\.setKit\s+\d+\s+\d+\s+(\S+)') { $kits += $Matches[1] }
            if ($line -match 'game\.setTeamSkin\s+\d+\s+(\S+)') { $skins += $Matches[1] }
        }
        if ($f.Name -match 'SpawnTemplates\.con' -and $line -match '(?:setObjectTemplate\s+[12]\s+|ObjectTemplate\.create\s+\S+\s+)(\S+)') { $spawn += $Matches[1] }
    }
}

$seeds = @($precache + $kits + $skins + $spawn) | Where-Object { $_ } | Sort-Object -Unique
Write-Host ("Seeds: {0} templates, {1} static geometry, {2} texture paths" -f $seeds.Count, $static.Count, $texPaths.Count)

# ---------------------------------------------------------------------------
# 3. Index the object .con files we already have on disk
# ---------------------------------------------------------------------------
$templateDef = @{}   # lower template name -> file it is defined in
$tmplGeom = @{}      # lower template name -> geometry names
$tmplAdd = @{}       # lower template name -> added template names
$geomMesh = @{}      # lower geometry name -> mesh file
$geomSkins = @{}     # lower geometry name -> skin paths
$geomType = @{}      # lower geometry name -> StandardMesh | AnimatedMesh | ...
$units = @{}         # file -> unit folder (directory of the defining .con)

$conFiles = @()
# the whole level tree (not just Objects\): spawner templates live in Conquest\,
# and level-local objects/effects elsewhere under the map folder
foreach ($root in @((Join-Path $ModRoot 'Objects'), (Join-Path $ModRoot 'bf1942'), $MapDir)) {
    if (Test-Path $root) { $conFiles += Get-ChildItem $root -Recurse -File -Filter *.con }
}
# vanilla templates (GermanSoldier, gear, effects) that FH references but does
# not ship - required so the fallback knows what geometry/sounds they pull in
$vanillaObjectDir = Join-Path $VanillaObjectRoot 'Objects'
if ((Test-Path $vanillaObjectDir) -and -not $ExcludeVanilla) {
    $conFiles += Get-ChildItem $vanillaObjectDir -Recurse -File -Filter *.con
}
Write-Host ("Scanning {0} object .con files ..." -f $conFiles.Count)

foreach ($f in $conFiles) {
    $unit = $f.DirectoryName
    $cur = $null; $gcur = $null
    foreach ($line in [System.IO.File]::ReadLines($f.FullName)) {
        if ($line -match '^\s*ObjectTemplate\.create\s+\S+\s+(\S+)') {
            $cur = $Matches[1].ToLowerInvariant()
            $templateDef[$cur] = $f.FullName
            $units[$f.FullName] = $unit
            if (-not $tmplGeom.ContainsKey($cur)) { $tmplGeom[$cur] = New-Object System.Collections.Generic.List[string] }
            if (-not $tmplAdd.ContainsKey($cur)) { $tmplAdd[$cur] = New-Object System.Collections.Generic.List[string] }
            continue
        }
        if ($cur) {
            if ($line -match '^\s*ObjectTemplate\.geometry\s+(\S+)') { $tmplGeom[$cur].Add($Matches[1]) }
            if ($line -match '^\s*ObjectTemplate\.addTemplate\s+(\S+)') { $tmplAdd[$cur].Add($Matches[1]) }
        }
        if ($line -match '^\s*GeometryTemplate\.create\s+(\S+)\s+(\S+)') {
            $gcur = $Matches[2].ToLowerInvariant()
            $geomType[$gcur] = $Matches[1]
            continue
        }
        if ($gcur) {
            if ($line -match '^\s*GeometryTemplate\.file\s+(\S+)') { $geomMesh[$gcur] = $Matches[1] }
            if ($line -match '^\s*GeometryTemplate\.setSkin\s+(\S+)') {
                if (-not $geomSkins.ContainsKey($gcur)) { $geomSkins[$gcur] = New-Object System.Collections.Generic.List[string] }
                $geomSkins[$gcur].Add($Matches[1])
            }
        }
    }
}
Write-Host ("  {0} templates, {1} geometry templates" -f $templateDef.Count, $geomMesh.Count)

# ---------------------------------------------------------------------------
# 4. Transitive closure over template names
# ---------------------------------------------------------------------------
$wantTemplates = New-Object System.Collections.Generic.HashSet[string]
$wantGeometry = New-Object System.Collections.Generic.HashSet[string]
$queue = New-Object System.Collections.Generic.Queue[string]
$unknownTemplates = New-Object System.Collections.Generic.HashSet[string]

foreach ($s in $seeds) {
    $k = $s.ToLowerInvariant()
    if ($templateDef.ContainsKey($k)) { [void]$queue.Enqueue($k) }
    elseif ($byStem.ContainsKey($k)) { [void]$wantGeometry.Add($k) }   # static geometry name
    else { [void]$unknownTemplates.Add($s) }
}
foreach ($s in $static) {
    # a StaticObjects.con "Object.create <name>" is either an OBJECT template
    # (killer objects, depots, ...) or a raw geometry/mesh name - check both,
    # otherwise template-based static objects get dropped from the closure.
    $k = $s.ToLowerInvariant()
    if ($templateDef.ContainsKey($k)) { [void]$queue.Enqueue($k) }
    elseif ($byStem.ContainsKey($k)) { [void]$wantGeometry.Add($k) }
    else { [void]$unknownTemplates.Add($s) }
}

while ($queue.Count -gt 0) {
    $t = $queue.Dequeue()
    if (-not $wantTemplates.Add($t)) { continue }
    if ($tmplAdd.ContainsKey($t)) {
        foreach ($a in $tmplAdd[$t]) {
            $ak = $a.ToLowerInvariant()
            if ($templateDef.ContainsKey($ak)) { [void]$queue.Enqueue($ak) }
            elseif ($byStem.ContainsKey($ak)) { [void]$wantGeometry.Add($ak) }
            else { [void]$unknownTemplates.Add($a) }
        }
    }
    if ($tmplGeom.ContainsKey($t)) {
        foreach ($g in $tmplGeom[$t]) { [void]$wantGeometry.Add($g.ToLowerInvariant()) }
    }
}
Write-Host ("Closure: {0} templates, {1} geometry names, {2} unresolved names" -f $wantTemplates.Count, $wantGeometry.Count, $unknownTemplates.Count)

# ---------------------------------------------------------------------------
# 5. Build the keep set as (archive, internal path) pairs
# ---------------------------------------------------------------------------
$keep = New-Object System.Collections.Generic.HashSet[string]   # "archive|path"
$unresolved = New-Object System.Collections.Generic.HashSet[string]
$vanillaHits = New-Object System.Collections.Generic.HashSet[string]   # names filled from vanilla

function Add-IndexEntry([object]$e) {
    if ($e) { [void]$keep.Add(($e.Archive + '|' + $e.Path)) }
}

function Exact-Path([string]$lowerPath) {
    # rfaUnpack -l compares names literally, so always emit the archive's casing
    if ($exactPath.ContainsKey($lowerPath)) { return $exactPath[$lowerPath] }
    return $lowerPath
}

function Add-Mesh([string]$meshStem) {
    # a mesh name maps to <stem>.sm (visual) + <stem>.rs (collision). Prefer the
    # FH shared archives; fall back to vanilla only when FH has no such mesh
    # (policy b: those files are merged into the custom mod).
    $k = $meshStem.ToLowerInvariant()
    if (-not $byStem.ContainsKey($k)) { [void]$unresolved.Add($meshStem); return }
    $found = $false
    foreach ($e in $byStem[$k]) {
        if (& $isSharedFh $e.Archive) { Add-IndexEntry $e; $found = $true }
    }
    if (-not $found -and -not $ExcludeVanilla) {
        foreach ($e in $byStem[$k]) {
            if (& $isVanilla $e.Archive) { Add-IndexEntry $e; $found = $true }
        }
        if ($found) { [void]$vanillaHits.Add($meshStem) }
    }
    if (-not $found) { [void]$unresolved.Add($meshStem) }
}

# 5a. geometry names -> meshes (via Geometries.con when known, else by name)
foreach ($g in $wantGeometry) {
    $name = $g.ToLowerInvariant()
    if ($geomMesh.ContainsKey($name)) {
        Add-Mesh $geomMesh[$name]
        if ($geomSkins.ContainsKey($name)) {
            foreach ($s in $geomSkins[$name]) {
                $sk = $s.Replace('\', '/').ToLowerInvariant()
                $owner = Resolve-ArchiveFor $sk
                if ($owner) { [void]$keep.Add(($owner + '|' + (Exact-Path $sk))) }
                else { [void]$unresolved.Add($s) }
            }
        }
    }
    else { Add-Mesh $name }
}

# 5b. object units -> every file in the folder that owns the template
$unitDirs = New-Object System.Collections.Generic.HashSet[string]
foreach ($t in $wantTemplates) {
    if ($templateDef.ContainsKey($t)) { [void]$unitDirs.Add($units[$templateDef[$t]]) }
}
Write-Host ("Keeping {0} object folder(s) in full" -f $unitDirs.Count)

$mapRoot = (Resolve-Path $MapDir).Path
$vanillaRootAbs = if (Test-Path $VanillaObjectRoot) { (Resolve-Path $VanillaObjectRoot).Path } else { $null }
foreach ($dir in $unitDirs) {
    foreach ($f in Get-ChildItem -LiteralPath $dir -Recurse -File) {
        # internal path = path relative to the extraction root, minus the top folder
        $rel = $null; $preferVanilla = $false
        if ($f.FullName.StartsWith($mapRoot, [StringComparison]::OrdinalIgnoreCase)) {
            $rel = 'bf1942/levels/' + (Split-Path $MapDir -Leaf) + '/' + $f.FullName.Substring($mapRoot.Length + 1)
        }
        elseif ($vanillaRootAbs -and $f.FullName.StartsWith($vanillaRootAbs, [StringComparison]::OrdinalIgnoreCase)) {
            $rel = $f.FullName.Substring($vanillaRootAbs.Length + 1); $preferVanilla = $true
        }
        else {
            $modRootAbs = (Resolve-Path $ModRoot).Path
            if ($f.FullName.StartsWith($modRootAbs, [StringComparison]::OrdinalIgnoreCase)) {
                $rel = $f.FullName.Substring($modRootAbs.Length + 1)
            }
        }
        if (-not $rel) { continue }
        $rel = $rel.Replace('\', '/')
        $owner = Resolve-ArchiveFor $rel $preferVanilla
        if ($owner) { [void]$keep.Add(($owner + '|' + (Exact-Path $rel.ToLowerInvariant()))) }
        else { [void]$unresolved.Add($rel) }
    }
}

# 5c. texture alternative paths -> whole folders in the texture archive
$texPrefixes = @()
foreach ($t in $texPaths) {
    # alternativePath values already include the Texture/ root (e.g.
    # "Texture/Levels/Pavlov"), and match the archive's folder structure
    # case-insensitively - so just append a separator, do NOT re-add "texture/".
    $texPrefixes += ($t.Trim('"').Replace('\', '/').TrimEnd('/') + '/').ToLowerInvariant()
}
$texPrefixes = @($texPrefixes | Sort-Object -Unique)
$texPerPrefix = @{}
foreach ($p in $texPrefixes) { $texPerPrefix[$p] = 0 }

foreach ($e in $entries) {
    $low = $e.Path.ToLowerInvariant()
    foreach ($p in $texPrefixes) {
        if ($low.StartsWith($p)) {
            if (& $isSharedFh $e.Archive) { Add-IndexEntry $e; $texPerPrefix[$p]++ }
            break
        }
    }
}

$textureHits = 0
foreach ($p in $texPrefixes) {
    $c = $texPerPrefix[$p]
    $textureHits += $c
    Write-Host ("  {0,5}  {1}" -f $c, $p)
    if ($c -eq 0) { [void]$unresolved.Add("texture path: $p") }
}
Write-Host ("Texture folder entries kept: {0} (from {1} path(s))" -f $textureHits, $texPrefixes.Count)

# 5d. sounds: object .ssc scripts reference samples as
#     load @ROOT/Sound/@RTD/VEFTRTYAW2.wav
# where @ROOT is the sound archive root and @RTD is the sample-rate folder
# (11khz / 22khz / 44kHz) chosen by the in-game sound setting. Keep the sample
# for EVERY rate, otherwise the game goes silent at other quality settings.
# Only .ssc files inside kept object folders are read, so unrelated objects do
# not drag their whole sound set in.
$soundHits = 0
if (-not $SkipSound) {
    $wavNames = New-Object System.Collections.Generic.HashSet[string]
    foreach ($dir in $unitDirs) {
        foreach ($f in Get-ChildItem -LiteralPath $dir -Recurse -File -Filter *.ssc -ErrorAction SilentlyContinue) {
            foreach ($line in [System.IO.File]::ReadLines($f.FullName)) {
                foreach ($m in [regex]::Matches($line, '(?i)\bload\s+\S*?([^/\\\s]+\.(?:wav|ogg))')) {
                    [void]$wavNames.Add($m.Groups[1].Value.ToLowerInvariant())
                }
            }
        }
    }
    Write-Host ("Sound samples referenced by .ssc scripts: {0}" -f $wavNames.Count)

    foreach ($w in $wavNames) {
        $dot = $w.LastIndexOf('.')
        $stem = $w.Substring(0, $dot)
        if (-not $byStem.ContainsKey($stem)) { [void]$unresolved.Add("sound: $w"); continue }
        $cands = @($byStem[$stem] | Where-Object { $_.File.ToLowerInvariant() -eq $w })
        $picked = @($cands | Where-Object { & $isSharedFh $_.Archive })
        if (-not $picked -and -not $ExcludeVanilla) {
            $picked = @($cands | Where-Object { & $isVanilla $_.Archive })
            if ($picked) { [void]$vanillaHits.Add($w) }
        }
        if (-not $picked) { [void]$unresolved.Add("sound: $w"); continue }
        foreach ($e in $picked) { Add-IndexEntry $e; $soundHits++ }
    }
    Write-Host ("Sound sample entries kept: {0}" -f $soundHits)
}

# 5e. optional: the whole FH menu overlay (HUD art, kit icons, fonts). It is an
# overlay on the base game's menu.rfa, so shipping it in full is the safe choice
# and only costs ~28 MB unpacked / 1132 entries.
if ($IncludeMenu) {
    $menuHits = 0
    foreach ($e in $entries) {
        if ($e.Archive -like '*/FH/Archives/menu.rfa') { Add-IndexEntry $e; $menuHits++ }
    }
    Write-Host ("Menu overlay entries kept: {0}" -f $menuHits)
}

# ---------------------------------------------------------------------------
# 6. Emit per-archive keep-lists
# ---------------------------------------------------------------------------
$grouped = @{}
foreach ($k in $keep) {
    $parts = $k -split '\|', 2
    $a = $parts[0]; $p = $parts[1]
    if (-not $grouped.ContainsKey($a)) { $grouped[$a] = New-Object System.Collections.Generic.List[string] }
    $grouped[$a].Add($p)
}

$manifest = New-Object System.Collections.Generic.List[string]

Write-Host ''
Write-Host ("Vanilla fallback used for {0} name(s)" -f $vanillaHits.Count)
Write-Host '=== keep-list per archive ==='
foreach ($a in ($grouped.Keys | Sort-Object { -$grouped[$_].Count })) {
    $safe = ($a -replace '[\\/:*?"<>|]', '_')
    $outFile = Join-Path $OutDir ($safe + '.lst')
    ($grouped[$a] | Sort-Object) | Set-Content -LiteralPath $outFile -Encoding ASCII
    # the file name is lossy (StandardMesh_001.rfa already contains '_'), so the
    # authoritative mapping lives in the manifest read by extract_keep_lists.ps1
    $manifest.Add(("{0}`t{1}" -f ($safe + '.lst'), $a))
    Write-Host ("  {0,7}  {1}" -f $grouped[$a].Count, $a)
    Write-Host ("           -> {0}" -f $outFile)
}
$manifest | Set-Content -LiteralPath (Join-Path $OutDir '_manifest.tsv') -Encoding ASCII
Write-Host ''
Write-Host ("TOTAL: {0} files across {1} archive(s)" -f $keep.Count, $grouped.Keys.Count)

# names found neither as a template nor as a mesh stem must not be silently
# dropped from the report - they are candidates for a missing asset
foreach ($u in $unknownTemplates) { [void]$unresolved.Add("(no template, no mesh) $u") }

if ($unresolved.Count -gt 0) {
    $uFile = Join-Path $OutDir 'UNRESOLVED.txt'
    ($unresolved | Sort-Object) | Set-Content -LiteralPath $uFile -Encoding ASCII
    Write-Host ''
    Write-Host ("{0} name(s) could NOT be resolved -> {1}" -f $unresolved.Count, $uFile)
    Write-Host '  (map-internal assets, or names living in an archive outside the index)'
}
