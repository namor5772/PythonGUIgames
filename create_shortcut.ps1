# Creates a Desktop shortcut that launches a project script with no console window.
#
# Defaults make a "MyTetris" shortcut:
#   .\create_shortcut.ps1
# Or target any script/icon/name:
#   .\create_shortcut.ps1 -Script MyApp.py -Icon myapp.ico -Name "My App"

param(
    [string]$Script = "MyTetris.py",
    [string]$Icon   = "mytetris.ico",
    [string]$Name   = "MyTetris"
)

$ErrorActionPreference = "Stop"

$projectDir = $PSScriptRoot
$pythonw    = Join-Path $projectDir ".venv\Scripts\pythonw.exe"
$scriptPath = Join-Path $projectDir $Script
$iconPath   = Join-Path $projectDir $Icon

# GetFolderPath resolves the real Desktop even when redirected to OneDrive.
$desktop  = [Environment]::GetFolderPath("Desktop")
$linkPath = Join-Path $desktop "$Name.lnk"

$shell    = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($linkPath)
$shortcut.TargetPath       = $pythonw
$shortcut.Arguments        = "`"$scriptPath`""
$shortcut.WorkingDirectory = $projectDir
$shortcut.IconLocation     = $iconPath
$shortcut.Description       = "Launch $Name"
$shortcut.Save()

Write-Output "Created shortcut: $linkPath"
