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

## Desktop shortcuts

A double-clickable Desktop shortcut can launch MyTetris with no console window
(it uses the venv's `pythonw.exe`):

```powershell
# Generate the icon, then create the MyTetris shortcut (defaults):
.venv\Scripts\python.exe make_tetris_icon.py
.\create_shortcut.ps1
```

This creates **"MyTetris.lnk"** on your Desktop with a custom Tetris icon, pointing
at `.venv\Scripts\pythonw.exe MyTetris.py` so the GUI runs silently. Re-run the
script if you move the project folder (the shortcut stores absolute paths).

`create_shortcut.ps1` is parameterized, so the same script can make a shortcut for
any future app: `.\create_shortcut.ps1 -Script MyApp.py -Icon myapp.ico -Name "My App"`.

## MyTetris

An accurate Tkinter clone of the classic game **Tetris**, implementing the modern
"guideline" mechanics:

- 10 x 20 playfield with the 7 standard tetrominoes in their classic colors
- **SRS** rotation (Super Rotation System) with wall kicks
- **7-bag** randomizer, **DAS** (smooth held movement), and **lock delay**
- Ghost piece, next-piece preview, hold, soft/hard drop
- Line-clear scoring, leveling, and increasing gravity

It also includes:

- **Start menu** — pick a difficulty before each game
- **Difficulty levels** — *Easy* (slower), *Normal*, *Hard* (starts at level 5,
  faster fall, 1.25x score)
- **Sound effects** — synthesized at runtime with the standard library (Windows
  `winsound`); toggle with **M**. No audio files are shipped or required.
- **High-score persistence** — top-10 scores per difficulty, saved to
  `%APPDATA%\MyTetris\highscores.json` (user data, kept out of the repo)

```powershell
python MyTetris.py

# Headless logic self-test (no window):
python MyTetris.py --selftest
```

| Key | Action | Key | Action |
| --- | --- | --- | --- |
| ← / → | Move | Space | Hard drop |
| ↓ | Soft drop | C / Shift | Hold |
| ↑ / X | Rotate CW | P | Pause |
| Z / Ctrl | Rotate CCW | M | Mute |
| Enter | Start (menu) | Esc | Back to menu / quit |
| R | Restart / retry | | |

## Scripts

| Script | Description |
| --- | --- |
| `MyTetris.py` | Accurate Tetris clone (SRS, 7-bag, DAS, ghost, hold, scoring, start menu, difficulty, sound, high scores). |
| `make_tetris_icon.py` | Generates `mytetris.ico` (a Tetris-themed icon) using only the standard library. |
| `create_shortcut.ps1` | Creates a Desktop shortcut for any app (parameterized: `-Script`, `-Icon`, `-Name`). |
