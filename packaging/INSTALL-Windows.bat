@echo off
rem =====================================================================
rem  MyPocketTanks - Windows installer.
rem  Finds Python 3 (helping you install it if it's missing) and puts a
rem  "MyPocketTanks" shortcut on your Desktop that runs it with no
rem  console window. Plain text on purpose - feel free to read it.
rem
rem  (This file doubles as the template for the other apps' bundles:
rem  package_games.py rewrites the MyPocketTanks/mypockettanks tokens and
rem  the description text, so keep any app-specific wording inside them.)
rem
rem  Usage: double-click.  (Optional flag /nolaunch skips the "start it
rem  now?" question at the end - used by automated tests.)
rem =====================================================================
setlocal EnableExtensions
title MyPocketTanks installer
echo.
echo   === MyPocketTanks installer ===
echo.

rem ---- 0) Make sure we're running from the extracted folder, not from
rem ----    inside the zip (Explorer runs zipped .bat files from %TEMP%).
if not exist "%~dp0MyPocketTanks.py" goto notextracted

rem ---- 1) Find a Python 3 interpreter: the py launcher, then python ----
set "PYRUN="
py -3 -c "import sys" >nul 2>nul && set "PYRUN=py -3"
if not defined PYRUN (
    python -c "import sys" >nul 2>nul && set "PYRUN=python"
)
if not defined PYRUN goto nopython

rem ---- 2) Sanity checks: version, and Tkinter (python.org includes it) ----
%PYRUN% -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>nul
if errorlevel 1 goto oldpython
%PYRUN% -c "import tkinter" >nul 2>nul
if errorlevel 1 goto notkinter

rem ---- 3) Resolve pythonw.exe (runs the GUI with no console window),
rem ----    falling back to python.exe if it's somehow absent.
set "PYW="
for /f "usebackq delims=" %%i in (`%PYRUN% -c "import os,sys;print(os.path.join(os.path.dirname(sys.executable),'pythonw.exe'))"`) do set "PYW=%%i"
if not exist "%PYW%" (
    for /f "usebackq delims=" %%i in (`%PYRUN% -c "import sys;print(sys.executable)"`) do set "PYW=%%i"
)

rem ---- 4) Create the Desktop shortcut. Paths travel via environment ----
rem ----    variables so spaces/apostrophes in folder names are safe. ----
set "PT_DIR=%~dp0"
set "PT_PYW=%PYW%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$q=[char]34; $d=$env:PT_DIR; $lnk=Join-Path ([Environment]::GetFolderPath('Desktop')) 'MyPocketTanks.lnk'; $s=(New-Object -ComObject WScript.Shell).CreateShortcut($lnk); $s.TargetPath=$env:PT_PYW; $s.Arguments=$q+$d+'MyPocketTanks.py'+$q; $s.WorkingDirectory=$d; $s.IconLocation=$d+'mypockettanks.ico'; $s.Description='MyPocketTanks - turn-based artillery duel'; $s.Save(); Write-Host ('  Created ' + $lnk)"
if errorlevel 1 goto shortcutfail

echo.
echo   Done! A "MyPocketTanks" shortcut is now on your Desktop.
echo   (If you move this folder later, run this installer again.)
echo.
if /i "%~1"=="/nolaunch" exit /b 0
choice /c YN /n /m "  Start MyPocketTanks now [Y/N]? "
if errorlevel 2 exit /b 0
start "" "%PYW%" "%~dp0MyPocketTanks.py"
exit /b 0

:notextracted
echo   MyPocketTanks.py is not next to this installer.
echo   Please extract the WHOLE zip first: right-click the zip, choose
echo   "Extract All...", then run INSTALL-Windows.bat from the extracted
echo   folder.
echo.
pause
exit /b 1

:nopython
echo   Python 3 is not installed yet - MyPocketTanks needs it (free, ~30 MB).
echo.
echo     1. Your browser is opening  https://www.python.org/downloads/
echo     2. Click the big "Download Python 3.x" button and run it.
echo     3. IMPORTANT: tick "Add python.exe to PATH" on its first screen.
echo     4. When it finishes, double-click this installer again.
echo.
start "" "https://www.python.org/downloads/"
pause
exit /b 1

:oldpython
echo   Your Python is too old for MyPocketTanks (3.8 or newer is needed).
echo   Please install the current version from python.org - your browser
echo   is opening the download page - then run this installer again.
echo.
start "" "https://www.python.org/downloads/"
pause
exit /b 1

:notkinter
echo   Python is installed, but its GUI library (Tkinter) is missing.
echo   Easiest fix: install Python from python.org - your browser is
echo   opening the download page; the standard install includes Tkinter -
echo   then run this installer again.
echo.
start "" "https://www.python.org/downloads/"
pause
exit /b 1

:shortcutfail
echo   Could not create the Desktop shortcut (see the error above).
echo   You can still start it by double-clicking MyPocketTanks.py in this
echo   folder, or by running:  %PYRUN% MyPocketTanks.py
echo.
pause
exit /b 1
