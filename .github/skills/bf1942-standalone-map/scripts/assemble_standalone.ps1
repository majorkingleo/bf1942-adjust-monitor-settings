# assemble_standalone.ps1 — Phase 1.5: build the standalone game root.
#
# Minimal-base policy (i): the custom mod carries all content, so the base mod
# ships only what the engine needs to boot and localise:
#   Mods\BF1942\init.con, Mod.dll, DrvMgt.dll, lexiconAll.dat (1.9 MB - FH's own
#   lexiconall.dat is only a 0.08 MB partial override), contentCrc32.con,
#   bfdist.vlu, 00000000.016/.256, Settings\ (profile + server templates) and
#   Archives\bf1942\Game.rfa. The multi-GB base archives (texture, sound, ...) are
#   NOT shipped; everything the map references was merged into the custom mod.
# Use -IncludeBaseArchives for the fallback policy (a) that also copies them.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File assemble_standalone.ps1 `
#       -Origin "origin" -BuildDir "work\build" -OutRoot "bf_1942_kwg_mod"

param(
    [string]$Origin = 'origin',
    [string]$BuildDir = 'work\build',
    [string]$OutRoot = 'bf_1942_kwg_mod',
    [string]$ModName = 'bf_1942_kwg_mod',
    # One or more level names, comma-separated. Each must have a matching
    # bf1942\levels\<Name>.rfa in -BuildDir. A level's DISPLAY TITLE is derived
    # from its name ('_' and '-' become spaces), e.g. Battle_of_KWG_11 ->
    # "Battle of KWG 11" - so never put a literal space in the name itself.
    [string[]]$MapName = 'Battle_Of_Pavlov-1942',
    [string]$CompatSource = 'E:\bf_tk_mod\bf',
    [switch]$IncludeBaseArchives
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Origin)) { throw "Origin not found: $Origin" }
if (-not (Test-Path $BuildDir)) { throw "Build dir not found: $BuildDir (run repack_standalone.ps1 first)" }
$originAbs = (Resolve-Path $Origin).Path
$buildAbs = (Resolve-Path $BuildDir).Path
$outAbs = Join-Path (Get-Location) $OutRoot

# `-File` delivers a comma list as ONE string, so split it here
$mapNames = @()
foreach ($m in $MapName) { $mapNames += @($m -split ',' | Where-Object { $_.Trim() }) }
if (-not $mapNames.Count) { throw 'At least one -MapName is required' }
$firstMap = $mapNames[0]

# --- helper -----------------------------------------------------------------
$script:copied = 0
$script:skipped = @()
function Copy-One([string]$srcRel, [string]$dstRel) {
    $src = Join-Path $originAbs $srcRel
    if (-not (Test-Path $src)) { $script:skipped += $srcRel; return }
    $dst = Join-Path $outAbs $dstRel
    $dir = Split-Path $dst -Parent
    if ($dir -and -not (Test-Path $dir)) { [void](New-Item -ItemType Directory -Force -Path $dir) }
    Copy-Item -LiteralPath $src -Destination $dst -Force
    $script:copied++
}
function Copy-Tree([string]$srcRel, [string]$dstRel) {
    $src = Join-Path $originAbs $srcRel
    if (-not (Test-Path $src)) { $script:skipped += $srcRel; return }
    $dst = Join-Path $outAbs $dstRel
    if (-not (Test-Path $dst)) { [void](New-Item -ItemType Directory -Force -Path $dst) }
    Copy-Item -Path (Join-Path $src '*') -Destination $dst -Recurse -Force
    $script:copied++
}

Write-Host '=== 1. engine files (game root) ==='
# BF1942.exe launches the game; BF1942_origin.exe is the launcher variant;
# the rest are the DLLs/exes the retail build expects to find beside it.
$engineFiles = @(
    'BF1942.exe', 'BF1942_origin.exe', 'BF1942_w32ded.exe', 'DedicatedServer.exe',
    'bfcprt.dll', 'binkw32.dll', 'GDFBinary_de_DE.dll', 'mfc70.dll', 'msvcr70.dll',
    'BlackScreen.exe', 'fpupdate.exe', 'bfdist.vlu', 'lexiconDS.dat', 'BF1942.par'
)
foreach ($f in $engineFiles) { Copy-One $f $f }
Copy-Tree 'Core' 'Core'

# dgVoodoo2 D3D8->D3D11/12 wrapper. BF1942 renders through D3D8, which modern
# Windows/drivers no longer provide: without these two files the client shows a
# black screen and exits immediately. The source install does not carry them, the
# working install does, and its dgVoodoo.conf is hand-tuned - copy both verbatim.
$wrapper = @('D3D8.dll', 'dgVoodoo.conf')
foreach ($f in $wrapper) {
    $src = Join-Path $originAbs $f
    if (-not (Test-Path $src)) { $src = Join-Path $CompatSource $f }
    if (Test-Path $src) {
        Copy-Item -LiteralPath $src -Destination (Join-Path $outAbs $f) -Force
        $script:copied++
        Write-Host ("  compat wrapper: {0}" -f $f)
    }
    else { $script:skipped += ("{0} (compat wrapper; pass -CompatSource <working install>)" -f $f) }
}

Write-Host ("  {0} file(s)/tree(s) copied" -f $script:copied)

Write-Host ''
Write-Host '=== 2. Mods\BF1942 (minimal base) ==='
$baseFiles = @(
    'init.con', 'Mod.dll', 'DrvMgt.dll', 'lexiconAll.dat', 'contentCrc32.con',
    'bfdist.vlu', '00000000.016', '00000000.256', 'SECDRV.SYS'
)
foreach ($f in $baseFiles) { Copy-One ("Mods\bf1942\$f") ("Mods\BF1942\$f") }
Copy-Tree 'Mods\bf1942\Settings' 'Mods\BF1942\Settings'

# The vanilla Mods\BF1942\init.con (copied verbatim) names music/*.bik for the
# menu, loading, win/lose and debrief screens. Ship the files it points at - the
# menu will not start without them.
Copy-Tree 'Mods\bf1942\Music' 'Mods\BF1942\Music'
Copy-Tree 'Mods\bf1942\Movies' 'Mods\BF1942\Movies'

# the engine needs a game.rfa in the base mod path; the vanilla one is 0.3 MB.
Copy-One 'Mods\bf1942\Archives\bf1942\Game.rfa' 'Mods\BF1942\Archives\bf1942\Game.rfa'

# UI / support archives: these are NOT map content and must stay, because the
# mod's own copies are overlays on top of them. Dropping them leaves the client
# with no fonts and an incomplete menu - the create-game map list then renders
# empty. They are tiny (~10 MB) and cost nothing next to the content archives.
$baseSupport = @('Font.rfa', 'shaders.rfa', 'menu.rfa', 'ai.rfa', 'aiMeshes.rfa')
foreach ($f in $baseSupport) {
    Copy-One ("Mods\bf1942\Archives\$f") ("Mods\BF1942\Archives\$f")
}

if ($IncludeBaseArchives) {
    Write-Host '  -IncludeBaseArchives: copying the vanilla root archives too'
    foreach ($f in Get-ChildItem (Join-Path $originAbs 'Mods\bf1942\Archives') -File) {
        Copy-One ("Mods\bf1942\Archives\" + $f.Name) ("Mods\BF1942\Archives\" + $f.Name)
    }
}

Write-Host ''
Write-Host '=== 3. Mods\<modname> (custom content) ==='
$modRoot = Join-Path $outAbs ("Mods\" + $ModName)
if (-not (Test-Path $modRoot)) { [void](New-Item -ItemType Directory -Force -Path $modRoot) }

# init.con — modelled on Mods\FH\init.con. The six music lines MATTER: the mod
# ships FH's menu.rfa as an overlay, and without them the menu comes up silent.
# The paths resolve through any mod path (`Mods\BF1942\Music` ships the .bik files),
# and `movies\background.bik` is what the menu uses for its background animation.
$initCon = @"
game.setCustomGameName $ModName
game.addModPath Mods/$ModName/
game.addModPath Mods/BF1942/
game.setCustomGameVersion 1.0
game.customGameFlushArchives 0
game.setCustomGameUrl ""

Game.setMenuMusicFilename "music/slaughter4.bik"
Game.setLoadMusicFilename "music/vehicle4.bik"
Game.setWinMusicFilename "music/vehicle3.bik"
Game.setLoseMusicFilename "music/menu.bik"
Game.setCampaignLoseMusicFilename "music/theme2.bik"
Game.setDebriefingMusicFilename "music/briefing.bik"
"@
Set-Content -LiteralPath (Join-Path $modRoot 'init.con') -Value $initCon -Encoding ASCII
Write-Host '  init.con written'

# FH's lexiconall.dat is a partial overlay on the base game's lexiconAll.dat
Copy-One 'Mods\FH\lexiconall.dat' ("Mods\$ModName\lexiconall.dat")
Copy-One 'Mods\FH\serverInfo.dds' ("Mods\$ModName\serverInfo.dds")

# MapList.con: the file the game writes for the ACTIVE mod when hosting. The copy
# inherited from the source install is stale - it names the wrong mod and a map
# that does not ship (game.addLevel <level> <gamemode> <mod>) - so write correct
# entries, one per level the mod ships. NOTE the engine REGENERATES this file from
# each level's Menu\init.con `game.setMapId`, so the durable fix for a wrong mod
# name lives there (see overrides\), not in this file.
$mapListLines = @()
foreach ($mn in $mapNames) { $mapListLines += "game.addLevel $mn GPM_CQ $ModName" }
$mapListLines += "game.setCurrentLevel $firstMap GPM_CQ $ModName"
$mapList = ($mapListLines -join "`r`n") + "`r`n"
$modSettings = Join-Path $modRoot 'Settings'
if (-not (Test-Path $modSettings)) { [void](New-Item -ItemType Directory -Force -Path $modSettings) }
Set-Content -LiteralPath (Join-Path $modSettings 'MapList.con') -Value $mapList -Encoding ASCII
Set-Content -LiteralPath (Join-Path $outAbs 'Mods\BF1942\Settings\MapList.con') -Value $mapList -Encoding ASCII
Write-Host '  MapList.con written for base + custom mod (level/mod corrected)'

# the repacked archive set (includes bf1942\game.rfa and bf1942\levels\<Map>.rfa)
$archDst = Join-Path $modRoot 'Archives'
if (-not (Test-Path $archDst)) { [void](New-Item -ItemType Directory -Force -Path $archDst) }
Copy-Item -Path (Join-Path $buildAbs '*') -Destination $archDst -Recurse -Force

# launchers: the +game argument is the MOD FOLDER NAME. Launching with "+game FH"
# mounts an unrelated (empty) mod folder and yields an empty map list, which is
# an easy mistake to make - so the correct command is generated here.
$clientBat = @"
@echo off
setlocal
rem Launch the standalone mod (client). +game must be the MOD FOLDER NAME.
rem Everything is echoed and the window stays open, because the client writes no
rem stdout or log of its own: without this a failure is just a window that
rem flashes and disappears.
cd /d "%~dp0"
echo Working directory: %CD%
if not exist BF1942.exe ( echo ERROR: BF1942.exe not found here. & pause & exit /b 1 )
if not exist D3D8.dll ( echo WARNING: D3D8.dll missing - the client will show a black screen on modern Windows. )
if exist BF1942.pid ( echo Removing leftover BF1942.pid & del /q BF1942.pid 2>nul )
echo Starting: BF1942.exe +game $ModName
echo.
BF1942.exe +game $ModName
echo.
echo BF1942 has exited, errorlevel=%ERRORLEVEL%
pause
"@
Set-Content -LiteralPath (Join-Path $outAbs 'run-client.bat') -Value $clientBat -Encoding ASCII

$clientMapBat = @"
@echo off
setlocal
rem Same as run-client.bat but jumps straight into the map, skipping the menus -
rem useful to tell a menu problem apart from a content problem.
cd /d "%~dp0"
echo Working directory: %CD%
if exist BF1942.pid ( del /q BF1942.pid 2>nul )
BF1942.exe +game $ModName +restart 1 +map $firstMap
echo.
echo BF1942 has exited, errorlevel=%ERRORLEVEL%
pause
"@
Set-Content -LiteralPath (Join-Path $outAbs 'run-client-map.bat') -Value $clientMapBat -Encoding ASCII

$serverBat = @"
@echo off
rem Dedicated server. Run from a REAL console window: with redirected stdio the
rem server exits immediately with "couldn't change console flags" (a stock
rem BF1942 install does the same, so it is not a mod problem).
cd /d "%~dp0"
BF1942_w32ded.exe +game $ModName +restart 1 +map $firstMap
pause
"@
Set-Content -LiteralPath (Join-Path $outAbs 'run-server.bat') -Value $serverBat -Encoding ASCII
Write-Host '  run-client.bat / run-server.bat written'

Write-Host ''
Write-Host '=== result tree ==='
Get-ChildItem $outAbs -Directory | ForEach-Object {
    $f = @(Get-ChildItem $_.FullName -Recurse -File -ErrorAction SilentlyContinue)
    '{0,-22} {1,6} file(s) {2,9:N1} MB' -f $_.Name, $f.Count, (($f | Measure-Object Length -Sum).Sum / 1MB)
}
Get-ChildItem $outAbs -File | Measure-Object Length -Sum | ForEach-Object {
    'engine root files      {0,6} file(s) {1,9:N1} MB' -f $_.Count, ($_.Sum / 1MB)
}
$total = @(Get-ChildItem $outAbs -Recurse -File)
'{0,-22} {1,6} file(s) {2,9:N1} MB' -f 'TOTAL', $total.Count, (($total | Measure-Object Length -Sum).Sum / 1MB)

Write-Host ''
Write-Host '=== checks ==='
$expected = @(
    ("Mods\$ModName\init.con"),
    ("Mods\$ModName\lexiconall.dat"),
    ("Mods\$ModName\Archives\objects.rfa"),
    ("Mods\$ModName\Archives\standardMesh.rfa"),
    ("Mods\$ModName\Archives\texture.rfa"),
    ("Mods\$ModName\Archives\sound.rfa"),
    ("Mods\$ModName\Archives\menu.rfa"),
    ("Mods\$ModName\Archives\bf1942\game.rfa"),
    'Mods\BF1942\init.con',
    'Mods\BF1942\lexiconAll.dat',
    'Mods\BF1942\Mod.dll',
    'Mods\BF1942\DrvMgt.dll',
    'Mods\BF1942\contentCrc32.con',
    'Mods\BF1942\Music\Menu.bik',
    'D3D8.dll',
    'dgVoodoo.conf',
    'BF1942.exe',
    'BF1942_w32ded.exe'
)
# every shipped level gets its own archive
$expected += @($mapNames | ForEach-Object { "Mods\$ModName\Archives\bf1942\levels\$_.rfa" })
$bad = 0
foreach ($e in $expected) {
    $ok = Test-Path (Join-Path $outAbs $e)
    if (-not $ok) { $bad++ }
    Write-Host ("  {0}  {1}" -f $(if ($ok) { 'ok     ' } else { 'MISSING' }), $e)
}
if ($script:skipped.Count) {
    Write-Host ''
    Write-Host '=== not found in origin (skipped) ==='
    $script:skipped | Sort-Object -Unique | ForEach-Object { Write-Host ("  {0}" -f $_) }
}
Write-Host ''
Write-Host ("Launch: {0}\BF1942.exe +game {1}" -f $OutRoot, $ModName)
if ($bad) { Write-Host ("{0} expected path(s) MISSING" -f $bad) } else { Write-Host 'all expected paths present' }
