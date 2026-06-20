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

## Scripts

| Script | Description |
| --- | --- |
| `hello_world.py` | Minimal Tkinter window displaying "HELLO WORLD!" centered. |
