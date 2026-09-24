<#
.SYNOPSIS
	Installs rfaPack.exe and rfaUnpack.exe into the consuming repository, keeping the 2004
	originals next to them as *.orig.exe.

.DESCRIPTION
	Four things happen, and the last two are the ones that are usually forgotten:

	  1. the binaries are rebuilt, so what gets installed matches the source;
	  2. they are STRIPPED. The build carries debug info (-g) that nobody wants in a mod
	     repository: 31 MB becomes 2 MB per binary, with no behaviour change;
	  3. the 2004 originals are backed up on the first run, and the installed files are verified
	     by hash against what was staged - a copy that half-succeeded must not look fine;
	  4. the hash table in the consumer's skill is brought up to date, so that skill cannot go
	     on describing binaries that are no longer there.

	The consumer's own behaviour test (`Test-RfaTools.ps1` in its `rfa-pack-unpack` skill) is not
	run from here - use the "verify deployed rfa tools" task, or run it directly. It is the only
	thing that proves the swap still behaves, not just that the files arrived.

.EXAMPLE
	powershell -NoProfile -ExecutionPolicy Bypass -File tools\deploy_rfa_tools.ps1

.EXAMPLE
	# only re-copy the current build, do not rebuild
	powershell -NoProfile -ExecutionPolicy Bypass -File tools\deploy_rfa_tools.ps1 -NoBuild
#>
[CmdletBinding()]
param(
	[string] $Target    = 'E:\progs\bf_pablov_mod\bin',
	[string] $SkillPath = '',
	[switch] $NoBuild,
	[switch] $NoStrip
)

$ErrorActionPreference = 'Stop'

$root   = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bash   = 'C:\cygwin64\bin\bash.exe'
$strip  = 'C:\cygwin64\bin\x86_64-w64-mingw32-strip.exe'
$names  = @( 'rfaPack.exe', 'rfaUnpack.exe' )

function Sha ( $path ) { (Get-FileHash $path -Algorithm SHA256).Hash.ToLower() }

# Cygwin wants /cygdrive/c/... instead of C:\..., and forward slashes.
function To-CygwinPath ( $path ) {
	$full = (Resolve-Path -LiteralPath $path).Path
	return '/cygdrive/' + $full.Substring(0, 1).ToLower() + $full.Substring(2).Replace( '\', '/' )
}

# 2 052 096, with spaces as separators - the way the table in the consumer's skill spells it.
function Format-Size ( $bytes ) {
	return ([string]::Format( [cultureinfo]::InvariantCulture, '{0:N0}', $bytes ) -replace ',', ' ')
}

<#
	Zero the two PE header fields that carry no information for an executable.

	`strip` stamps TimeDateStamp with the CURRENT time (measured: two runs a minute apart differ,
	two runs in the same second do not), so the same source would deploy different bytes every
	time and the consumer's repository would show a binary change that means nothing. Measured
	otherwise: the stripped file differs from the unstripped one in exactly these fields.

	TimeDateStamp is informational - a normal link with `--no-insert-timestamp` zeroes it too.
	CheckSum is not verified for user-mode images and a plain `ld` link leaves it at 0; bfd fills
	it in when rewriting, which is why it has to go as well. Clearing both makes the deployed
	bytes a function of the source alone, which is what makes the hash worth recording.

	PE layout: at 0x3C sits e_lfanew; the COFF header starts 4 bytes later, and its
	TimeDateStamp is 4 bytes in (so e_lfanew + 8). The optional header follows 20 bytes after the
	COFF header, and its CheckSum is 64 bytes into that (e_lfanew + 24 + 64).
#>
function Clear-PeVolatileFields ( $path ) {
	$bytes = [IO.File]::ReadAllBytes( $path )
	$pe    = [BitConverter]::ToInt32( $bytes, 0x3C )

	if( $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A ) { throw "not a PE file: $path" }

	$zero = [byte[]] @( 0, 0, 0, 0 )
	[Array]::Copy( $zero, 0, $bytes, $pe + 8, 4 )
	[Array]::Copy( $zero, 0, $bytes, $pe + 24 + 64, 4 )
	[IO.File]::WriteAllBytes( $path, $bytes )
}

if( -not (Test-Path -LiteralPath $Target) ) {
	throw "target directory does not exist: $Target (refusing to create it, the path may be wrong)"
}

if( -not $SkillPath ) {
	$SkillPath = Join-Path (Split-Path $Target -Parent) '.github\skills\rfa-pack-unpack\SKILL.md'
}

# ------------------------------------------------------------------ 1. build
if( $NoBuild ) {
	'build       skipped (-NoBuild)'
} else {
	'build       make -j$(nproc)'
	& $bash -lc ( "cd '" + (To-CygwinPath $root) + "' && make -j`$(nproc)" ) | Out-Null

	if( $LASTEXITCODE -ne 0 ) { throw "the build failed (make exited $LASTEXITCODE)" }
}

foreach( $name in $names ) {
	if( -not (Test-Path -LiteralPath (Join-Path $root $name)) ) {
		throw "not built: $(Join-Path $root $name)"
	}
}

# ------------------------------------------------------------------ 2. strip
$stage = Join-Path $env:TEMP 'rfa-deploy'
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

foreach( $name in $names ) {
	Copy-Item (Join-Path $root $name) (Join-Path $stage $name) -Force
}

$before = @{}
foreach( $name in $names ) { $before[$name] = (Get-Item (Join-Path $stage $name)).Length }

if( $NoStrip ) {
	"stage       skipped stripping (-NoStrip)"
} else {
	foreach( $name in $names ) {
		& $strip (Join-Path $stage $name)

		if( $LASTEXITCODE -ne 0 ) { throw "strip failed on $name" }
	}

	foreach( $name in $names ) { Clear-PeVolatileFields (Join-Path $stage $name) }
	"stage       PE time/checksum fields cleared, so the bytes depend on the source only"
}

foreach( $name in $names ) {
	$after = (Get-Item (Join-Path $stage $name)).Length
	"stage       $name  $($before[$name]) B -> $after B"
}

# ------------------------------------------------- 3. back up, install, verify
foreach( $name in $names ) {
	$destination = Join-Path $Target $name
	$backup      = Join-Path $Target ( $name -replace '\.exe$', '.orig.exe' )

	if( Test-Path -LiteralPath $backup ) {
		"backup      $(Split-Path $backup -Leaf) already there, left alone"
	} elseif( Test-Path -LiteralPath $destination ) {
		Copy-Item $destination $backup
		"backup      $(Split-Path $backup -Leaf)  $(Format-Size (Get-Item $backup).Length) B  $(Sha $backup)"
	} else {
		"backup      no $name in the target, so nothing to back up"
	}

	Copy-Item (Join-Path $stage $name) $destination -Force

	$staged  = Sha (Join-Path $stage $name)
	$landed  = Sha $destination

	if( $staged -ne $landed ) {
		throw "$name does not match after copying: staged $staged, deployed $landed"
	}

	"deployed    $name  $(Format-Size (Get-Item $destination).Length) B  $landed"
}

# ------------------------------------------- 4. keep the consumer's table honest
$deployed = @{}
foreach( $name in $names ) {
	$deployed[ $name ] = @{
		Path = Join-Path $Target $name
		Size = (Get-Item (Join-Path $Target $name)).Length
		Hash = Sha (Join-Path $Target $name)
	}
}

foreach( $name in @( 'rfaPack.orig.exe', 'rfaUnpack.orig.exe' ) ) {
	$path = Join-Path $Target $name

	if( Test-Path -LiteralPath $path ) {
		$deployed[ $name ] = @{
			Path = $path
			Size = (Get-Item $path).Length
			Hash = Sha $path
		}
	}
}

if( -not (Test-Path -LiteralPath $SkillPath) ) {
	"skill       not found, so the hash table was left alone: $SkillPath"
} else {
	# Rewrite only the size and hash cells of the rows that name a file, keeping every other
	# cell - and the spacing - exactly as it was.
	$lines   = [IO.File]::ReadAllText( $SkillPath ) -split "`n"
	$updated = 0
	$missing = @()

	foreach( $name in $deployed.Keys ) {
		$needle = '| `bin\' + $name + '`'
		$found  = $false

		for( $i = 0; $i -lt $lines.Count; ++$i ) {
			if( -not $lines[$i].StartsWith( $needle ) ) { continue }

			$cells = $lines[$i].Split( '|' )

			if( $cells.Count -lt 6 ) { continue }

			$sizeCell = ' ' + (Format-Size $deployed[$name].Size) + ' B '
			$hashCell = ' `' + $deployed[$name].Hash + '` '

			if( $cells[2] -eq $sizeCell -and $cells[3] -eq $hashCell ) {
				$found = $true
				break
			}

			$cells[2] = $sizeCell
			$cells[3] = $hashCell
			$lines[$i] = $cells -join '|'
			$updated++
			$found = $true
			break
		}

		if( -not $found ) { $missing += $name }
	}

	if( $updated -gt 0 ) {
		[IO.File]::WriteAllText( $SkillPath, ( $lines -join "`n" ), [Text.UTF8Encoding]::new($false) )
		"skill       $updated hash row(s) updated in $SkillPath"
		"            commit it in the target repository, or the next reader sees the old hashes"
	} else {
		"skill       hash table already current"
	}

	foreach( $name in $missing ) {
		"WARNING     no table row found for $name - update $SkillPath by hand"
	}
}

''
"done. next: run the 'verify deployed rfa tools' task, which cross-checks behaviour."
