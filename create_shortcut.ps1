# Creates a Desktop shortcut for a project app, in one of two flavors:
#
#   * Python (default) — launches the .py with the venv's pythonw.exe (no console).
#   * Native (-Native) — launches the compiled build\<App>.exe directly, using the
#                        icon embedded in that exe. Fully self-contained: no Python,
#                        no venv, no loose .ico needed. Build it first with
#                        build_native.ps1.
#
# Defaults make a "MyTetris" shortcut:
#   .\create_shortcut.ps1                     # Python -> MyTetris.lnk
#   .\create_shortcut.ps1 -Native             # Native -> "MyTetris (Native).lnk"
# Or target any script/icon/name:
#   .\create_shortcut.ps1 -Script MyApp.py -Icon myapp.ico -Name "My App"
#   .\create_shortcut.ps1 -Native -Script Sun2Set.py   # -> "Sun2Set (Native).lnk"
#
# In -Native mode the exe is build\<basename of -Script>.exe, and when -Name is
# omitted the shortcut is named "<App> (Native)" so it sits beside the Python one
# instead of overwriting it.

param(
    [string]$Script = "MyTetris.py",
    [string]$Icon   = "mytetris.ico",
    [string]$Name   = "MyTetris",
    [switch]$Native
)

$ErrorActionPreference = "Stop"

$projectDir = $PSScriptRoot

# GetFolderPath resolves the real Desktop even when redirected to OneDrive.
$desktop = [Environment]::GetFolderPath("Desktop")

$shell = New-Object -ComObject WScript.Shell

if ($Native) {
    # Launch the standalone native build directly (no interpreter).
    $app = [IO.Path]::GetFileNameWithoutExtension($Script)
    $exe = Join-Path $projectDir "build\$app.exe"
    if (-not (Test-Path $exe)) {
        throw "Native build not found: $exe  (run .\build_native.ps1 -App $app first)"
    }
    # Default the name to "<App> (Native)" so it sits beside the Python shortcut
    # rather than overwriting it.
    if (-not $PSBoundParameters.ContainsKey("Name")) { $Name = "$app (Native)" }

    $linkPath = Join-Path $desktop "$Name.lnk"
    $shortcut = $shell.CreateShortcut($linkPath)
    $shortcut.TargetPath       = $exe
    $shortcut.WorkingDirectory = Split-Path $exe        # the build folder
    $shortcut.IconLocation     = "$exe,0"               # the exe's embedded icon
    $shortcut.Description       = "Launch $Name (native build)"
    $shortcut.Save()
} else {
    # Launch the Python script with the venv's pythonw.exe (no console window).
    $pythonw    = Join-Path $projectDir ".venv\Scripts\pythonw.exe"
    $scriptPath = Join-Path $projectDir $Script
    $iconPath   = Join-Path $projectDir $Icon

    $linkPath = Join-Path $desktop "$Name.lnk"
    $shortcut = $shell.CreateShortcut($linkPath)
    $shortcut.TargetPath       = $pythonw
    $shortcut.Arguments        = "`"$scriptPath`""
    $shortcut.WorkingDirectory = $projectDir
    $shortcut.IconLocation     = $iconPath
    $shortcut.Description       = "Launch $Name"
    $shortcut.Save()
}

Write-Output "Created shortcut: $linkPath"
