# Creates a Desktop shortcut that launches hello_world.py with no console window.
# Run from the project root:  .\create_shortcut.ps1

$ErrorActionPreference = "Stop"

$projectDir = $PSScriptRoot
$pythonw    = Join-Path $projectDir ".venv\Scripts\pythonw.exe"
$script     = Join-Path $projectDir "hello_world.py"
$icon       = Join-Path $projectDir "hello_world.ico"

# GetFolderPath resolves the real Desktop even when redirected to OneDrive.
$desktop  = [Environment]::GetFolderPath("Desktop")
$linkPath = Join-Path $desktop "Hello World.lnk"

$shell    = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($linkPath)
$shortcut.TargetPath       = $pythonw
$shortcut.Arguments        = "`"$script`""
$shortcut.WorkingDirectory = $projectDir
$shortcut.IconLocation     = $icon
$shortcut.Description       = "Launch the Hello World Tkinter app"
$shortcut.Save()

Write-Output "Created shortcut: $linkPath"
