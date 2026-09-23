# apply_overrides.ps1 — copy hand-authored overrides over the staging tree
# before repacking.
#
# Extracted vendor content is not always what the standalone mod should ship.
# Example: FH's bf1942/game/CampaignMapList.con lists ~30 campaign maps, so the
# menu shows a long list of greyed-out levels for a one-map build.
#
# Override files mirror the staging layout, i.e.
#   overrides\bf1942\game\CampaignMapList.con
# is copied to <StageRoot>\bf1942\game\CampaignMapList.con
#
# The override folder lives at the repo root (NOT under work\, which is
# gitignored) so hand-authored content is versioned with the build.
#
# Run this AFTER extract_keep_lists.ps1 and BEFORE repack_standalone.ps1.
#
# The level tree is staged OUTSIDE the main staging root (work\map), so pass
# every staging root that should receive overrides.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File apply_overrides.ps1 `
#       -StageRoot "work\FH","work\map" -OverrideDir "overrides"

param(
    [Parameter(Mandatory = $true)][string[]]$StageRoot,
    [string]$OverrideDir = 'overrides'
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $OverrideDir)) {
    Write-Host ("No override dir ({0}) - nothing to do" -f $OverrideDir)
    exit 0
}

$files = @(Get-ChildItem -LiteralPath $OverrideDir -Recurse -File)
Write-Host ("{0} override file(s) from {1}" -f $files.Count, $OverrideDir)

# `-File` delivers a comma-separated list as ONE string, so split it here
$roots = @()
foreach ($r in $StageRoot) { $roots += @($r -split ',' | Where-Object { $_.Trim() }) }

foreach ($root in $roots) {
    if (-not (Test-Path $root)) { throw "Stage root not found: $root" }
    $stageAbs = (Resolve-Path $root).Path
    $applied = 0
    foreach ($f in $files) {
        $rel = $f.FullName.Substring((Resolve-Path $OverrideDir).Path.Length).TrimStart('\')
        $dest = Join-Path $stageAbs $rel
        # only apply an override where the target already exists (i.e. this root
        # is the one that stages that content)
        if (-not (Test-Path $dest)) { continue }
        Copy-Item -LiteralPath $f.FullName -Destination $dest -Force
        Write-Host ("  {0,-14} {1}" -f (Split-Path $root -Leaf), $rel)
        $applied++
    }
    if (-not $applied) { Write-Host ("  {0,-14} (no matching paths)" -f (Split-Path $root -Leaf)) }
}
