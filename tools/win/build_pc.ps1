# Builds the Windows PC version of rondo-nx.
#   1. Translates the game code from your coverage traces (tracer\traces) into C++ (out\generated).
#   2. Configures and compiles with clang-cl + Ninja (SDL3 is downloaded automatically the first time).
#   3. Writes out\pc\rondo-nx.ini pointing at your game files.
# Every step's full output goes to out\logs\.
#
# Usage (from C:\rondo-nx): powershell -ExecutionPolicy Bypass -File tools\win\build_pc.ps1 [-Clean]

param([switch]$Clean)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$logDir = Join-Path $repo 'out\logs'
$gen = Join-Path $repo 'out\generated'
$build = Join-Path $repo 'out\pc'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

function Step($name, $logName, [scriptblock]$body) {
    $log = Join-Path $logDir $logName
    Write-Host ''
    Write-Host "== $name  (log: out\logs\$logName)"
    $start = Get-Date
    & $body *>&1 | Tee-Object -FilePath $log | Select-Object -Last 3 | ForEach-Object { Write-Host "   $_" }
    if ($LASTEXITCODE -ne 0) {
        Write-Host ''
        Write-Host "FAILED at: $name. Tell Claude; the log is out\logs\$logName"
        exit 1
    }
    Write-Host ("   done in {0:n1} min" -f ((Get-Date) - $start).TotalMinutes)
}

. (Join-Path $PSScriptRoot 'env.ps1')

if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }

$traces = Join-Path $repo 'tracer\traces'
if (-not (Test-Path $traces)) { Write-Host "No traces folder at $traces"; exit 1 }

Step 'Translate game code from traces' 'translate.log' {
    python (Join-Path $repo 'tools\xlat\gen.py') (Join-Path $repo 'third_party\geargrafx\src\huc6280_timing.h') $gen $traces
}

Step 'Configure (CMake + Ninja + clang-cl)' 'configure.log' {
    cmake -S $repo -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl "-DRONDO_GENERATED_DIR=$gen"
}

Step 'Compile' 'compile.log' {
    cmake --build $build
}

# Point the app at the game files in this folder.
$cue = Get-ChildItem -Path $repo -Filter '*.cue' -Recurse -Depth 1 |
    Where-Object { $_.FullName -notmatch 'English' -and $_.FullName -notmatch '\\out\\' } | Select-Object -First 1
$bios = Get-ChildItem -Path $repo -Filter 'syscard3*.pce' -Depth 0 | Select-Object -First 1
$ini = Join-Path $build 'rondo-nx.ini'
if (-not (Test-Path $ini)) {
    @(
        '# rondo-nx settings',
        "cue=$($cue.FullName)",
        "bios=$($bios.FullName)",
        '# 1 = translated code (default), 0 = interpreter only',
        'translated=1',
        'scale=4',
        'fullscreen=0',
        'smooth=0'
    ) | Set-Content -Path $ini -Encoding ascii
}

Write-Host ''
Write-Host "Built: $build\rondo-nx.exe"
Write-Host "Settings: $ini"
