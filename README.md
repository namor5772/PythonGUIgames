# PythonGUIgames

A collection of Python GUI apps and games built with Tkinter.

## Requirements

- Python 3.14+ (uses the `py` launcher on Windows)
- Tkinter (included with the standard Python installation)

## Setup

Create and activate the virtual environment:

```powershell
# Create the environment (already done if .venv exists)
py -m venv .venv

# Activate it (PowerShell)
.venv\Scripts\Activate.ps1
```

To deactivate, run `deactivate`.

> The project currently has no third-party dependencies — Tkinter ships with
> Python. The `.venv` keeps this project isolated for when dependencies are added.

## Running

```powershell
# With the venv activated:
python hello_world.py

# Or without activating, using the venv interpreter directly:
.venv\Scripts\python.exe hello_world.py
```

## Desktop shortcut

A double-clickable Desktop shortcut can launch the app with no console window
(it uses the venv's `pythonw.exe`):

```powershell
# Generate the icon (writes hello_world.ico), then create the shortcut:
.venv\Scripts\python.exe make_icon.py
.\create_shortcut.ps1
```

This creates **"Hello World.lnk"** on your Desktop with a custom window icon.
The shortcut points at `.venv\Scripts\pythonw.exe hello_world.py`, so it runs the
GUI silently. Re-run `create_shortcut.ps1` if you move the project folder.

## Scripts

| Script | Description |
| --- | --- |
| `hello_world.py` | Minimal Tkinter window displaying "HELLO WORLD!" centered. |
| `make_icon.py` | Generates `hello_world.ico` (a custom window icon) using only the standard library. |
| `create_shortcut.ps1` | Creates a Desktop shortcut that launches the app without a console window. |
