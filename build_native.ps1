# Builds a native app (MyTetris.cpp / MyPocketTanks.cpp / Sun2Set.cpp) with
# the Visual Studio C++ toolchain into build\<App>.exe — standalone: static
# CRT, only core Windows DLLs, icon embedded.
#
#   .\build_native.ps1                     # default: MyTetris
#   .\build_native.ps1 -App MyPocketTanks

param(
    [ValidateSet("MyTetris", "MyPocketTanks", "Sun2Set")]
    [string]$App = "MyTetris"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

# Locate the newest Visual Studio with the C++ tools via vswhere.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "Visual Studio not found (no vswhere.exe). Install VS Community with the C++ workload."
}
$vs = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vs) {
    throw "No Visual Studio installation with the C++ tools was found."
}
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"

$build = Join-Path $root "build"
New-Item -ItemType Directory -Force $build | Out-Null

# Optional icon resource (id 1 — the window class loads MAKEINTRESOURCE(1)).
$res = ""
$icon = Join-Path $root "$($App.ToLower()).ico"
if (Test-Path $icon) {
    $rc = Join-Path $build "$App.rc"
    "1 ICON `"$($icon.Replace('\', '\\'))`"" | Set-Content -Encoding ascii $rc
    $res = "build\$App.res"
}

# /MT = static CRT, so the exe runs without the VC++ redistributable.
# /utf-8 = the sources are UTF-8; without it MSVC reads non-ASCII string
# literals (em dashes, ellipses) as ANSI and window titles turn to mojibake.
$compile = "cl /nologo /EHsc /O2 /MT /std:c++17 /W3 /utf-8 " +
           "/D_CRT_SECURE_NO_WARNINGS $App.cpp /Fo:build\ " +
           "/Fe:build\$App.exe $res /link /SUBSYSTEM:WINDOWS " +
           "user32.lib gdi32.lib winmm.lib msimg32.lib comdlg32.lib shell32.lib"
$steps = if ($res) {
    "rc /nologo /fo $res build\$App.rc && $compile"
} else {
    $compile
}

cmd /c "`"$vcvars`" >nul && cd /d `"$root`" && $steps"
if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
Write-Host "Built $build\$App.exe"
