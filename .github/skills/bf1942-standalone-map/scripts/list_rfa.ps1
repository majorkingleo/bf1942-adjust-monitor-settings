# list_rfa.ps1 — List the contents of a Battlefield 1942 .rfa archive
# without unpacking. Pure PowerShell (no external tools).
#
# This only READS the directory table, so it works for both the base game
# archives and the compressed Forgotten Hope archives. To actually extract
# files (including compressed entries), use rfaUnpack.exe:
#   rfaUnpack.exe <archive.rfa> <outputDir>
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File list_rfa.ps1 <input.rfa>
#
# Verified RFA layout (v1):
#   [0..3]  u32 directoryOffset
#   [4..7]  u32 version (=1)
#   [8 .. directoryOffset)   contiguous file data
#   [directoryOffset .. ]    directory
#   directory = numFiles(u32) + entries + 4-byte zero terminator
#   entry = nameLen(u32) + name(nameLen, NO null terminator)
#           + 6 x u32: storedSize, uncompressedSize, dataOffset, 0, 0, flags
#   file data at dataOffset = 16-byte sub-header + payload
#     sub-header: tag(=1), payloadSize, uncompressedSize, reserved(0)
#     if payloadSize < uncompressedSize the payload is LZO1X compressed
#     (handled by rfaUnpack.exe / miniLZO; flags is per-entry, treat as opaque).

param(
    [Parameter(Mandatory = $true)][string]$InputFile,
    # Print one internal path per line instead of a table. Format-Table
    # truncates long paths, so use this when you need exact names (e.g. to
    # assert that an archive does not contain another archive's content).
    [switch]$Names
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $InputFile)) { throw "Input file not found: $InputFile" }

$bytes = [System.IO.File]::ReadAllBytes($InputFile)

function Read-UInt32([byte[]]$buf, [int]$offset) {
    if ($offset -lt 0 -or $offset + 4 -gt $buf.Length) { throw "Read out of range at $offset" }
    return [BitConverter]::ToUInt32($buf, $offset)
}

$dirOff  = Read-UInt32 $bytes 0
$version = Read-UInt32 $bytes 4
$numFiles = Read-UInt32 $bytes $dirOff
$pos = $dirOff + 4

$rows = for ($i = 0; $i -lt $numFiles; $i++) {
    $nameLen = Read-UInt32 $bytes $pos; $pos += 4
    $name = [System.Text.Encoding]::ASCII.GetString($bytes, $pos, $nameLen).TrimEnd([char]0)
    $pos += $nameLen
    $stored    = Read-UInt32 $bytes $pos
    $uncomp    = Read-UInt32 $bytes ($pos + 4)
    $dataOff   = Read-UInt32 $bytes ($pos + 8)
    $pos += 24
    [PSCustomObject]@{
        Name         = $name
        StoredBytes  = $stored
        UncompBytes  = $uncomp
        Compressed   = ($stored -lt $uncomp)
        DataOffset   = $dataOff
    }
}

if ($Names) {
    $rows | Sort-Object Name | ForEach-Object { Write-Output $_.Name }
    return
}

$rows | Sort-Object Name | Format-Table -AutoSize
Write-Host ("Archive: {0}  version={1}  files={2}  (dir at {3}, file len {4})" -f `
    (Split-Path $InputFile -Leaf), $version, $numFiles, $dirOff, $bytes.Length)
