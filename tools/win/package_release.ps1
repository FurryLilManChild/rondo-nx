# Makes the Switch release zip for rondo-nx. No game files and no System Card go in it.
#   1. Relinks platforms\switch\rondo-nx.nro with the version from the Makefile. It does NOT re-translate the game
#      code: it reuses out\generated from your last build_switch.ps1 run, so the program is the one you tested.
#   2. Builds out\release\rondo-nx-v<version>-switch.zip with switch/rondo-nx/ inside: the .nro, a readme,
#      notes saying where the player's own files go, and the GPL licence.
#   3. Checks that no disc, System Card or save file slipped into the zip, and prints its SHA-256.
#
# Usage (from C:\rondo-nx): powershell -ExecutionPolicy Bypass -File tools\win\package_release.ps1

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sw = Join-Path $repo 'platforms\switch'
$gen = Join-Path $repo 'out\generated'
$logDir = Join-Path $repo 'out\logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

$mk = Get-Content (Join-Path $sw 'Makefile') | Where-Object { $_ -match '^APP_VERSION\s*:=\s*(\S+)' } | Select-Object -First 1
if (-not $mk) { Write-Host 'APP_VERSION not found in platforms\switch\Makefile.'; exit 1 }
$version = ($mk -replace '^APP_VERSION\s*:=\s*', '').Trim()
Write-Host "rondo-nx $version"

if (-not (Test-Path (Join-Path $gen 'xlat_table.inc'))) {
    Write-Host 'out\generated is missing: run tools\win\build_switch.ps1 -NoGameCopy once first.'
    exit 1
}

$dkp = if ($env:DEVKITPRO_WIN) { $env:DEVKITPRO_WIN } else { 'C:\devkitPro' }
$bash = Join-Path $dkp 'msys2\usr\bin\bash.exe'
if (-not (Test-Path $bash)) { Write-Host "devkitPro's MSYS2 not found at $bash (set DEVKITPRO_WIN if it lives elsewhere)"; exit 1 }
function To-Msys($p) { $p = $p -replace '\\', '/'; if ($p -match '^([A-Za-z]):(.*)$') { return '/' + $matches[1].ToLower() + $matches[2] }; return $p }

Write-Host ''
Write-Host '== Step 1 of 3: relink the .nro with the release version  (log: out\logs\release_link.log)'
$env:MSYSTEM = 'MSYS'
$swM = To-Msys $sw; $genM = To-Msys $gen
& $bash -lc "export DEVKITPRO=/opt/devkitpro; export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitA64/bin:`$PATH; cd '$swM' && make -j4 GEN='$genM'" *>&1 | Out-File (Join-Path $logDir 'release_link.log') -Encoding utf8
if ($LASTEXITCODE -ne 0) { Write-Host 'FAILED. Tell Claude; the log is out\logs\release_link.log'; exit 1 }
$nro = Join-Path $sw 'rondo-nx.nro'
Write-Host ("   done: {0:n1} MB" -f ((Get-Item $nro).Length / 1MB))

Write-Host ''
Write-Host '== Step 2 of 3: assemble the zip'
$rel = Join-Path $repo 'out\release'
$stage = Join-Path $rel 'stage'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$app = Join-Path $stage 'switch\rondo-nx'
New-Item -ItemType Directory -Force -Path (Join-Path $app 'game') | Out-Null
Copy-Item $nro $app
Copy-Item (Join-Path $repo 'LICENSE') (Join-Path $app 'LICENSE.txt')

@"
rondo-nx $version (test release): Castlevania: Rondo of Blood (PC Engine CD) for the Nintendo Switch
https://github.com/FurryLilManChild/rondo-nx
Unofficial and fan-made; no affiliation with Konami or Nintendo. No game files and no System Card are included.

Install
1. Extract this zip to the root of your SD card (it makes switch/rondo-nx/).
2. Copy your own System Card 3.0 BIOS to switch/rondo-nx/syscard3.pce (that exact name).
3. Copy your English disc (the v1.03 fan translation, as a .cue and all its .bin tracks) into switch/rondo-nx/game/.
   Optional: the Japanese original in switch/rondo-nx/game-jp/ adds a Japanese option in the menu.
4. Hold R while opening any game to get the homebrew menu with full memory, then start rondo-nx.

Controls: Y attack, B jump, X subweapon, + Run (start/pause), - Select. ZL + ZR together (or clicking the right
stick) opens the menu: reset, 5 save-state slots, controller remapping, language, volume, aspect ratio, cheats.

Saves are in switch/rondo-nx/saves/ (a backup copy is made once a day).

Please report problems on the GitHub page with where in the game it happened and what you saw.

License: GPL-3.0 (LICENSE.txt), because it builds on the Geargrafx emulator core. Source code is at the link above.
"@ | Set-Content -Path (Join-Path $app 'README.txt') -Encoding ASCII

@"
Put your own English Castlevania: Rondo of Blood disc here: the v1.03 fan translation of the
Japanese PC Engine Super CD-ROM2 disc, as a .cue file plus all of its .bin tracks.

Any file names work; the port uses the first .cue it finds in this folder.
The System Card goes one folder up, as switch/rondo-nx/syscard3.pce.

No game files are included with rondo-nx.
"@ | Set-Content -Path (Join-Path $app 'game\PUT_YOUR_DISC_HERE.txt') -Encoding ASCII

$zip = Join-Path $rel "rondo-nx-v$version-switch.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
# ZipFile (not Compress-Archive): Windows PowerShell 5's Compress-Archive can write backslashes in the paths, which
# breaks the folders when the zip is extracted on a Switch, Mac or Linux.
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)

Write-Host ''
Write-Host '== Step 3 of 3: check the zip'
$z = [System.IO.Compression.ZipFile]::OpenRead($zip)
$names = $z.Entries | ForEach-Object { $_.FullName }
$z.Dispose()
$names | ForEach-Object { Write-Host "   $_" }
$bad = $names | Where-Object { $_ -match '\.(pce|cue|bin|iso|chd|sav|ini)$' }
if ($bad) { Write-Host "STOP: game, BIOS or save files are in the zip: $bad"; Remove-Item $zip -Force; exit 1 }
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
Remove-Item $stage -Recurse -Force

Write-Host ''
Write-Host "Ready: $zip"
Write-Host ("Size: {0:n1} MB   SHA-256: {1}" -f ((Get-Item $zip).Length / 1MB), $hash)
Write-Host "Release notes to paste: docs\release\rondo-nx-$version.md"
