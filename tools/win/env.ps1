# Sets up the build environment in the current PowerShell session: the Visual Studio C++ libraries and Windows SDK
# (through vcvars64.bat), then LLVM clang, CMake and Ninja first on the PATH.
# Dot-source it: . tools\win\env.ps1
# Overrides: RONDO_LLVM (folder holding bin\clang-cl.exe), RONDO_VSDIR (Visual Studio or Build Tools folder).
# Only this PowerShell session is changed.

$ErrorActionPreference = 'Stop'

# Unix-style tool folders (devkitPro's MSYS2, MSYS2, Cygwin) carry their own cmake and make; keep them out.
$script:RondoUnixPathPattern = '\\(devkitPro\\msys2|msys64|msys2|cygwin(64)?)(\\|$)'

function Find-VsDir {
    if ($env:RONDO_VSDIR) { return $env:RONDO_VSDIR }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: are the Visual Studio 2022 Build Tools installed?" }
    $dir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $dir) { throw "No Visual Studio install with the C++ tools (VCTools workload) was found." }
    return $dir
}

function Find-Llvm {
    $candidates = @()
    if ($env:RONDO_LLVM) { $candidates += $env:RONDO_LLVM }
    $candidates += (Join-Path $env:ProgramFiles 'LLVM')
    if (${env:ProgramFiles(x86)}) { $candidates += (Join-Path ${env:ProgramFiles(x86)} 'LLVM') }
    $candidates += (Join-Path $env:LOCALAPPDATA 'Programs\LLVM')
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c 'bin\clang-cl.exe')) { return $c }
    }
    throw ("clang-cl.exe not found. Looked in: " + ($candidates -join ', '))
}

if (-not $env:RONDO_ENV_READY) {
    $env:PATH = (@($env:PATH -split ';' | Where-Object { $_ -and ($_ -notmatch $script:RondoUnixPathPattern) }) -join ';')

    $vsDir = Find-VsDir
    $vcvars = Join-Path $vsDir 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found under $vsDir" }
    $cmdExe = if ($env:ComSpec) { $env:ComSpec } else { Join-Path $env:SystemRoot 'System32\cmd.exe' }
    & $cmdExe /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
    }

    $llvm = Find-Llvm
    $extra = @((Join-Path $llvm 'bin'))
    foreach ($p in @((Join-Path $env:ProgramFiles 'CMake\bin'), (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links'))) {
        if (Test-Path $p) { $extra += $p }
    }
    $env:PATH = ($extra -join ';') + ';' + $env:PATH
    $env:RONDO_ENV_READY = '1'
}
