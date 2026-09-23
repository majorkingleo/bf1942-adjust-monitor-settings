# find_deps.ps1 — Scan an extracted BF1942 level for its external asset
# references. This tells you exactly which objects / geometry / textures the
# map needs from the shared mod archives, so you can copy only those files.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File find_deps.ps1 <mapDir>
#   powershell -NoProfile -ExecutionPolicy Bypass -File find_deps.ps1 <mapDir> -Resolve <extractedArchivesRoot>
#
#   <mapDir>                extracted level folder, e.g. "...\Battle_Of_Pavlov-1942"
#   -Resolve <root>         optional root of extracted mod archives (contains
#                           StandardMesh\, Objects\, Texture\, Sound\, ...).
#                           When given, each referenced name is looked up there
#                           and the matching file paths are listed at the end.

param(
    [Parameter(Mandatory = $true)][string]$MapDir,
    [string]$Resolve
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $MapDir)) { throw "Map dir not found: $MapDir" }

$conFiles = Get-ChildItem $MapDir -Recurse -File -Filter *.con

$precache = @(); $static = @(); $texPaths = @()
$kits = @(); $skins = @(); $spawnTemplates = @(); $runTargets = @()

foreach ($f in $conFiles) {
    $lines = Get-Content $f.FullName
    foreach ($line in $lines) {
        if ($line -match '^\s*run\s+(\S+)') { $runTargets += $Matches[1] }

        if ($f.Name -eq 'PreCache.con' -and $line -match '^\s*Object\.create\s+(\S+)') {
            $precache += $Matches[1]
        }
        if ($f.Name -eq 'StaticObjects.con' -and $line -match '^\s*Object\.create\s+(\S+)') {
            $static += $Matches[1]
        }
        if ($f.Name -eq 'Init.con') {
            if ($line -match 'textureManager\.alternativePath\s+(\S+)') { $texPaths += $Matches[1] }
            if ($line -match 'game\.setKit\s+\d+\s+\d+\s+(\S+)')   { $kits += $Matches[1] }
            if ($line -match 'game\.setTeamSkin\s+\d+\s+(\S+)')    { $skins += $Matches[1] }
        }
        if ($f.Name -match 'SpawnTemplates\.con' -and $line -match 'setObjectTemplate\s+[12]\s+(\S+)') {
            $spawnTemplates += $Matches[1]
        }
        if ($f.Name -match 'SpawnTemplates\.con' -and $line -match 'ObjectTemplate\.create\s+\S+\s+(\S+)') {
            $spawnTemplates += $Matches[1]
        }
    }
}

function Show([string]$title, [string[]]$items) {
    Write-Host ""
    Write-Host ("=== {0} ({1}) ===" -f $title, @($items | Where-Object { $_ -and $_.Trim() } | Sort-Object -Unique).Count)
    $items | Where-Object { $_ -and $_.Trim() } | Sort-Object -Unique | ForEach-Object { Write-Host ("  {0}" -f $_) }
}

Show 'PreCache objects (weapons/kits/vehicles)' $precache
Show 'Static geometry (StandardMesh names)'      $static
Show 'Texture alternative paths'                 $texPaths
Show 'Kits (game.setKit)'                        $kits
Show 'Skins (game.setTeamSkin)'                  $skins
Show 'Spawner templates'                         $spawnTemplates
Show 'run targets'                               $runTargets

if ($Resolve) {
    Write-Host ""
    Write-Host "=== RESOLVED FILES (from $Resolve) ==="
    $hits = New-Object System.Collections.Generic.HashSet[string]

    # Static geometry -> StandardMesh/<name>.sm / .rs
    foreach ($n in $static) {
        foreach ($ext in '.sm', '.rs', '.con') {
            $p = Join-Path $Resolve ("StandardMesh\" + $n + $ext)
            if (Test-Path $p) { [void]$hits.Add($p) }
        }
    }
    # Texture alternative paths -> whole Texture/<path> folders
    foreach ($p in $texPaths) {
        $dir = Join-Path $Resolve ("Texture\" + $p.Replace('/', '\'))
        if (Test-Path $dir) {
            Get-ChildItem $dir -Recurse -File | ForEach-Object { [void]$hits.Add($_.FullName) }
        }
        else {
            Write-Host ("  MISSING texture path: {0}" -f $p)
        }
    }
    # Object / kit names -> search Objects\ for their template definition
    $objectNames = @($precache) + @($kits) + @($skins) + @($spawnTemplates) |
        Where-Object { $_ -and $_.Trim() } | Sort-Object -Unique
    if (Test-Path (Join-Path $Resolve 'Objects')) {
        Write-Host ("  Scanning Objects\ for {0} template names ..." -f $objectNames.Count)
        $objCon = Get-ChildItem (Join-Path $Resolve 'Objects') -Recurse -File -Filter *.con -ErrorAction SilentlyContinue
        foreach ($f in $objCon) {
            $txt = Get-Content $f.FullName -Raw
            foreach ($n in $objectNames) {
                if ($txt -match ('(?m)^\s*ObjectTemplate\.create\s+\S+\s+' + [regex]::Escape($n) + '\b')) {
                    [void]$hits.Add($f.FullName)
                }
            }
        }
    }

    Write-Host ("  {0} resolved files/dirs:" -f $hits.Count)
    $hits | Sort-Object | ForEach-Object { Write-Host ("    {0}" -f $_) }
}
