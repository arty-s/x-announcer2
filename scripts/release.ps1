# Builds a SkunkCrafts update module for X-Announcer 2.
#
#   powershell -ExecutionPolicy Bypass -File scripts\release.ps1 [-Deploy] [-Version v2.0.0]
#
# SkunkcraftsUpdater.exe lives in the X-Plane 12 ROOT (not in plugins). It walks
# the installation, finds every folder holding a skunkcrafts_updater.cfg, reads
# `module` from it, and compares the files it lists against two manifests served
# from that URL: a whitelist of path|CRC32 and a sizes list of path|bytes. Both
# use forward slashes and paths relative to the addon folder. Verified against
# live modules (SGES, ToLiss, AEP) rather than taken from documentation.
#
# The one rule that matters here: config.ini, Sound_packs and report_last.txt are
# the USER'S, and an updater that overwrites them is the installer that wiped the
# plugin folder all over again. They are shipped in the ignore list and are never
# part of the whitelist.
#
# ASCII only in this file on purpose - PowerShell 5.1 reads UTF-8 without BOM as
# cp1251 and a Cyrillic comment breaks the parser.
[CmdletBinding()]
param(
    [switch]$Deploy,
    [string]$Version = "",
    [string]$ModuleUrl = "https://xvatrus.ru/xannouncer/update",
    [string]$VpsTarget = "root@83.217.201.120",
    [string]$VpsPath = "/etc/x-vatrus/www/xannouncer/update",
    [string]$SshKey = "$env:USERPROFILE\.ssh\vatrus_vps"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$staged = Join-Path $root "build\x_announcer2"
$out = Join-Path $root "build\update"

# Files that belong to the person running the plugin. The updater is told to
# leave them alone; they are also never hashed, so a changed config.ini can
# never read as "your install is out of date".
#
# skunkcrafts_updater.cfg is in here too, and for a sharper reason: that file is
# how the updater knows which version this install already has. If the updater
# were allowed to overwrite it from the module, it would write the NEW version
# number into an install that has not been updated yet, and then agree with
# itself that nothing needs downloading. The module publishes the same lines as
# skunkcrafts_updater_config.txt, which is the channel meant for that.
$userOwned = @("config.ini", "report_last.txt", "Sound_packs", "skunkcrafts_updater.cfg")

if ($Version -eq "") {
    $header = Get-Content (Join-Path $root "src\plugin\version.h") -Raw
    if ($header -match 'kPluginVersion\s*=\s*"([^"]+)"') {
        $Version = "v" + $Matches[1]
    } else {
        throw "cannot read kPluginVersion from src/plugin/version.h"
    }
}

Write-Host "Building $Version..."
& powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") | Out-Null
if ($LASTEXITCODE -ne 0) { throw "build failed" }
if (-not (Test-Path $staged)) { throw "no build output at $staged" }

# CRC32, the same polynomial zlib uses - checked against a file from a live
# SkunkCrafts module: 663 bytes, zlib.crc32 = 2605887540, which is what that
# module's own whitelist carries.
# Everything is held in Int64 and masked back to 32 bits at each step: PowerShell
# 5.1 has no unsigned arithmetic worth the name, and a UInt32 that goes through
# -bxor comes back signed, which turns the last step into "cannot convert -1".
# Decimal, not hex: PowerShell 5.1 parses 0xFFFFFFFF and 0xEDB88320 as Int32
# by bit pattern, so they arrive as -1 and -306674912 and every checksum
# comes out wrong (and negative, which is how this was caught).
$mask = [int64]4294967295
$crcTable = New-Object int64[] 256
for ($i = 0; $i -lt 256; $i++) {
    $c = [int64]$i
    for ($k = 0; $k -lt 8; $k++) {
        if ($c -band 1) {
            $c = ((([int64]3988292384) -bxor ($c -shr 1)) -band $mask)
        } else {
            $c = (($c -shr 1) -band $mask)
        }
    }
    $crcTable[$i] = $c
}
function Get-Crc32([string]$path) {
    $mask = [int64]4294967295
    $crc = $mask
    $stream = [System.IO.File]::OpenRead($path)
    try {
        $buffer = New-Object byte[] 65536
        while (($read = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            for ($i = 0; $i -lt $read; $i++) {
                $index = ($crc -bxor [int64]$buffer[$i]) -band 0xFF
                $crc = (($crcTable[$index] -bxor ($crc -shr 8)) -band $mask)
            }
        }
    } finally {
        $stream.Close()
    }
    return (($crc -bxor $mask) -band $mask)
}

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force -Path $out | Out-Null

$whitelist = New-Object System.Collections.Generic.List[string]
$sizes = New-Object System.Collections.Generic.List[string]
$copied = 0
$totalBytes = 0

Get-ChildItem -Path $staged -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($staged.Length + 1).Replace("\", "/")
    $head = $relative.Split("/")[0]
    if ($userOwned -contains $head -or $userOwned -contains $relative) {
        Write-Host "  skipping user file $relative"
        return
    }
    $target = Join-Path $out $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item $_.FullName $target
    $whitelist.Add("$relative|" + (Get-Crc32 $_.FullName))
    $sizes.Add("$relative|" + $_.Length)
    $script:copied++
    $script:totalBytes += $_.Length
}

# The cfg the user's install carries, and the copy the module serves so that a
# later release can move the module URL without anybody editing a file by hand.
$cfg = @(
    "name|X-Announcer 2",
    "version|$Version",
    "module|$ModuleUrl",
    "zone|custom",
    "locked|false",
    "disabled|false"
)

# UTF8 without BOM: the updater reads these as plain text, and a BOM ends up in
# the first path of the first line.
#
# The names are not a guess and not copied from another module: they are the
# only ones SkunkcraftsUpdater.exe v3.2d actually carries. Pulled out of the
# binary after it refused the first attempt with "Remote configuration file is
# missing important information" - the module had been given
# skunkcrafts_updater_config.txt and skunkcrafts_updater_ignorelist.txt, and
# NEITHER string exists anywhere in the updater. It asks the module for
# skunkcrafts_updater.cfg - the same file name the install carries - and for
# skunkcrafts_updater_ignore.txt. (SGES publishes the _config.txt spelling as
# well; it is not what this version reads.)
$enc = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines((Join-Path $out "skunkcrafts_updater_whitelist.txt"), $whitelist, $enc)
[System.IO.File]::WriteAllLines((Join-Path $out "skunkcrafts_updater_sizeslist.txt"), $sizes, $enc)
[System.IO.File]::WriteAllLines((Join-Path $out "skunkcrafts_updater_ignore.txt"), $userOwned, $enc)
[System.IO.File]::WriteAllLines((Join-Path $out "skunkcrafts_updater_blacklist.txt"), @(), $enc)
[System.IO.File]::WriteAllLines((Join-Path $out "skunkcrafts_updater.cfg"), $cfg, $enc)
# Into the staged folder as well, so that build.ps1 -Install puts it beside the
# plugin: an install without this file is invisible to the updater.
[System.IO.File]::WriteAllLines((Join-Path $staged "skunkcrafts_updater.cfg"), $cfg, $enc)
[System.IO.File]::WriteAllLines((Join-Path $root "build\skunkcrafts_updater.cfg"), $cfg, $enc)

Write-Host ""
Write-Host "Module ready: $out"
Write-Host ("  {0} files, {1:N0} bytes, version {2}" -f $copied, $totalBytes, $Version)
Write-Host "  cfg for the install: build\skunkcrafts_updater.cfg"

if ($Deploy) {
    Write-Host ""
    Write-Host "Deploying to $VpsTarget`:$VpsPath"
    & ssh -i $SshKey $VpsTarget "mkdir -p '$VpsPath'"
    if ($LASTEXITCODE -ne 0) { throw "cannot reach the VPS" }
    & scp -i $SshKey -r "$out\*" "$VpsTarget`:$VpsPath/"
    if ($LASTEXITCODE -ne 0) { throw "upload failed" }
    & ssh -i $SshKey $VpsTarget "chmod -R a+rX '$VpsPath'"
    Write-Host "Deployed. Module URL: $ModuleUrl"
}
