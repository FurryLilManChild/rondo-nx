# Builds the Switch version (rondo-nx.nro) with your installed devkitPro, then makes an SD card folder.
#   1. Translates the game code from tracer\traces (same as the PC build).
#   2. Compiles with devkitA64 through devkitPro's own MSYS2 make.
#   3. Copies the .nro, syscard3.pce and the game files into out\switch\sd\switch\rondo-nx\.
# Logs go to out\logs\.
#
# Usage (from C:\rondo-nx): powershell -ExecutionPolicy Bypass -File tools\win\build_switch.ps1 [-Clean] [-NoGameCopy] [-WithJapanese]
#   The English disc (english\...) goes in game\; -WithJapanese also copies the Japanese disc to game-jp\.

param([switch]$Clean, [switch]$NoGameCopy, [switch]$WithJapanese, [int]$Jobs = 0)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$logDir = Join-Path $repo 'out\logs'
$gen = Join-Path $repo 'out\generated'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
if ($Jobs -le 0) { $Jobs = [Math]::Max(2, [Environment]::ProcessorCount) }

$dkp = if ($env:DEVKITPRO_WIN) { $env:DEVKITPRO_WIN } else { 'C:\devkitPro' }
$bash = Join-Path $dkp 'msys2\usr\bin\bash.exe'
if (-not (Test-Path $bash)) { Write-Host "devkitPro's MSYS2 not found at $bash (set DEVKITPRO_WIN if it lives elsewhere)"; exit 1 }

function Step($name, $logName, [scriptblock]$body) {
    $log = Join-Path $logDir $logName
    Write-Host ''
    Write-Host "== $name  (log: out\logs\$logName)"
    $start = Get-Date
    if (Test-Path $log) { Remove-Item $log }
    & $body *>&1 | ForEach-Object { "$_" | Out-File -FilePath $log -Append -Encoding utf8; "$_" } | Select-Object -Last 3 | ForEach-Object { Write-Host "   $_" }
    if ($LASTEXITCODE -ne 0) {
        Write-Host ''
        Write-Host "FAILED at: $name. Tell Claude; the log is out\logs\$logName"
        exit 1
    }
    Write-Host ("   done in {0:n1} min" -f ((Get-Date) - $start).TotalMinutes)
}

function To-Msys($p) { $p = $p -replace '\\', '/'; if ($p -match '^([A-Za-z]):(.*)$') { return '/' + $matches[1].ToLower() + $matches[2] }; return $p }

Step 'Translate game code from traces' 'translate.log' {
    python (Join-Path $repo 'tools\xlat\gen.py') (Join-Path $repo 'third_party\geargrafx\src\huc6280_timing.h') $gen (Join-Path $repo 'tracer\traces')
}

$sw = To-Msys (Join-Path $repo 'platforms\switch')
$genM = To-Msys $gen
if ($Clean) {
    & $bash -lc "cd '$sw' && make clean" | Out-Null
}
Step 'Compile for Switch (devkitA64)' 'switch_compile.log' {
    $env:MSYSTEM = 'MSYS'
    & $bash -lc "export DEVKITPRO=/opt/devkitpro; export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitA64/bin:`$PATH; cd '$sw' && make -j$Jobs GEN='$genM'"
}

$nro = Join-Path $repo 'platforms\switch\rondo-nx.nro'
$sd = Join-Path $repo 'out\switch\sd\switch\rondo-nx'
New-Item -ItemType Directory -Force -Path $sd | Out-Null
Copy-Item $nro $sd -Force
Copy-Item (Join-Path $repo 'syscard3.pce') $sd -Force
if (-not $NoGameCopy) {
    $en = Get-ChildItem -Path (Join-Path $repo 'english') -Filter '*.cue' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $en) { Write-Host "No English disc under english\ (extract the English v1.03 .7z there)."; exit 1 }
    $gameDst = Join-Path $sd 'game'
    New-Item -ItemType Directory -Force -Path $gameDst | Out-Null
    Write-Host ''
    Write-Host "== Copying the English disc (about 500 MB) to $gameDst"
    Copy-Item (Join-Path $en.DirectoryName '*') $gameDst -Force
    if ($WithJapanese) {
        $jp = Get-ChildItem -Path $repo -Directory | Where-Object { $_.Name -like 'Akumajou Dracula X - Chi no Rondo (Japan)*' } | Select-Object -First 1
        $jpDst = Join-Path $sd 'game-jp'
        New-Item -ItemType Directory -Force -Path $jpDst | Out-Null
        Write-Host "== Copying the Japanese disc to $jpDst"
        Copy-Item (Join-Path $jp.FullName '*') $jpDst -Force
    }
}

Write-Host ''
Write-Host "Built: $nro"
Write-Host "SD card folder ready: $sd"
Write-Host "Copy the 'switch' folder inside out\switch\sd\ to the root of your SD card (it merges with the existing switch folder)."
