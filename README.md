# PythonGUIgames

A collection of Python GUI games built with **Tkinter**. The flagship is
**MyTetris**, an accurate, guideline-faithful clone of the classic game.

Everything here is **pure standard library** — Tkinter, `wave`, `json`,
`winsound`/`afplay`, etc. all ship with CPython, so there's nothing to `pip
install`. The game logic is platform-independent; only three things differ per
OS, and each degrades gracefully:

| Concern | Windows | macOS |
| --- | --- | --- |
| Sound playback | `winsound` (in-memory WAVs) | `afplay` (temp WAVs) |
| Saved data | `%APPDATA%\MyTetris\` | `~/MyTetris/` |
| Desktop shortcut | `create_shortcut.ps1` → `.lnk` | `create_shortcut.command` → `.app` |

On any other platform the game still runs; sound just stays off.

## Requirements

- **Python 3.14+** with **Tkinter** (both bundled with a standard CPython
  install — on Windows via [python.org](https://www.python.org/), on macOS via
  the [Homebrew](https://brew.sh/) `python` formula or python.org).
- No third-party packages. There is intentionally **no `requirements.txt`**.

Check your setup:

```bash
python3 -c "import tkinter; print('tkinter', tkinter.TkVersion)"
```

## Running

**macOS / Linux:**

```bash
python3 MyTetris.py                 # play
python3 MyTetris.py --selftest      # headless logic self-test (no window)
```

**Windows (PowerShell):**

```powershell
py -m venv .venv                    # one-time: create the (git-ignored) venv
.venv\Scripts\Activate.ps1          # activate it
.venv\Scripts\python.exe MyTetris.py            # play
.venv\Scripts\python.exe MyTetris.py --selftest # headless self-test
```

The `.venv` keeps the project isolated for if dependencies are ever added; since
there are none today, plain `python`/`python3` works just as well. To leave the
venv, run `deactivate`.

`--selftest` drives every difficulty for thousands of simulated frames and
asserts that nothing throws — handy for catching regressions without opening a
window.

## Controls

| Key | Action | Key | Action |
| --- | --- | --- | --- |
| ← / → | Move | Space | Hard drop |
| ↓ | Soft drop | C / Shift | Hold |
| ↑ / X | Rotate clockwise | P | Pause |
| Z / Ctrl | Rotate counter-clockwise | M | Mute / unmute |
| Enter | Start (from menu) | Esc | Back to menu (asks first) / quit |
| R | Restart / retry | | |

On the **start menu**: ↑ ↓ (or ← →) change the difficulty, **`[`** / **`]`**
lower / raise the speed ramp, and **Enter** starts.

## MyTetris

An accurate Tkinter clone of **Tetris** implementing the modern *guideline*
mechanics:

- **10 × 20 playfield** with the 7 standard tetrominoes (I, J, L, O, S, T, Z) in
  their classic colors.
- **SRS** rotation (Super Rotation System) with the standard wall-kick tables, so
  rotations near walls and floors behave exactly as guideline players expect.
- **7-bag randomizer** — each of the 7 pieces appears once per bag, so you never
  get long droughts or floods of a single piece.
- **DAS** (Delayed Auto-Shift) for smooth held movement, plus **lock delay** with
  reset, so a piece doesn't lock the instant it lands.
- **Ghost piece** (landing preview), a **next-3 preview queue**, **hold**, and
  both **soft** and **hard** drop.
- **Line-clear scoring**, **leveling**, and increasing **gravity** as you climb.
- **T-spin** detection (Full and Mini) with bonus scoring and on-screen banners.
- **Back-to-back** bonus (×1.5) for chaining Tetrises and T-spins without an
  intervening normal line clear.

### Game flow & quality-of-life

- **Start menu** — choose a difficulty before each game; ← / → change the
  selection, **Enter** starts.
- **Difficulty levels:**
  - *Easy* — slower base fall, gentle start.
  - *Normal* — standard base fall speed.
  - *Hard* — starts at **level 5**, faster base fall, and a **×1.25** score
    multiplier.
- **Adjustable speed ramp** — how quickly gravity speeds up as you level is a
  separate setting you control right on the **start menu**: press **`[`** and
  **`]`** to lower or raise the **SPEED RAMP** value (range `0.20`–`1.00`). `1.0`
  is the classic Tetris ramp; **lower means a gentler per-level speed-up and much
  longer games** (default `0.50`). It applies to every difficulty and is **saved
  to `config.json`**, so your choice is restored automatically next time.
- **Sound effects** — every effect (move blip, rotate, hold, lock, hard drop,
  line clear, Tetris, T-spin, level-up, game over, start jingle) is **synthesized
  at runtime** as a small WAV — no audio files are shipped or committed. Playback
  is per-platform: Windows plays the buffers straight from memory with `winsound`;
  macOS writes each WAV once to a temp directory and plays it with the system
  `afplay` (the temp files are cleaned up on exit). Toggle with **M**; the HUD
  shows `MUTED` when muted, or `NO SOUND` only if no backend is available.
- **High-score persistence** — the **top-10 scores per difficulty** are saved as
  JSON (`highscores.json`) and survive restarts.
- **Window-position memory** — the window reopens where you last closed it
  (`config.json`).
- **Pause → "return to menu?" confirmation** — pausing and choosing to quit asks
  first, so you never lose a game by accident.

### Saved data

User data lives outside the repo and is **never committed**:

| File | Windows | macOS / Linux |
| --- | --- | --- |
| High scores | `%APPDATA%\MyTetris\highscores.json` | `~/MyTetris/highscores.json` |
| Window position **&** speed ramp | `%APPDATA%\MyTetris\config.json` | `~/MyTetris/config.json` |

Both files load defensively — if they're missing or corrupt the game just starts
fresh. Delete them to reset your scores, or the speed ramp and window position.

## Desktop shortcuts

Create a double-clickable launcher so you don't need a terminal.

### Windows

```powershell
# Generate the icon, then create the MyTetris shortcut (defaults):
.venv\Scripts\python.exe make_tetris_icon.py
.\create_shortcut.ps1
```

This writes **`mytetris.ico`** (a tidy falling-T playfield scene drawn directly as
ICO bytes — no Pillow) and creates **`MyTetris.lnk`** on your Desktop, pointing at
`.venv\Scripts\pythonw.exe MyTetris.py` so the GUI runs with no console window.

`create_shortcut.ps1` is parameterized, so it can make a shortcut for any app:

```powershell
.\create_shortcut.ps1 -Script MyApp.py -Icon myapp.ico -Name "My App"
```

### macOS

```bash
# Generate the icon, then build the MyTetris.app shortcut (defaults):
python3 make_tetris_icon_mac.py
./create_shortcut.command
```

This writes **`mytetris.png`**, packs it into a multi-resolution **`mytetris.icns`**
with the system `sips` + `iconutil` tools, and assembles a clickable
**`MyTetris.app`** bundle on your Desktop whose launcher runs the game with
`python3`.

The icon is **"The Troll Piece"** 🟨 — a fat, smugly grinning O-piece plummeting
straight toward the one 1-wide well you've spent twenty turns praying to fill with
the long cyan I-piece... which, of course, never came (note there's no cyan
anywhere in the stack). Every Tetris player feels this.

`create_shortcut.command` takes the same optional name / target script:

```bash
./create_shortcut.command "My App" MyApp.py
```

> Both shortcut builders bake in **absolute paths**, so re-run the relevant script
> if you move the project folder. macOS sometimes caches the old Finder/Dock icon;
> if the new one doesn't appear immediately, it refreshes on next login.

## Scripts

| Script | Description |
| --- | --- |
| `MyTetris.py` | The game — accurate Tetris clone (SRS, 7-bag, DAS, ghost, hold, next-3, T-spins, back-to-back, start menu, difficulty, sound, high scores) plus a headless `--selftest`. |
| `make_tetris_icon.py` | **Windows icon** — generates `mytetris.ico` by writing the ICO/BMP bytes directly (no Pillow). |
| `make_tetris_icon_mac.py` | **macOS icon** — generates `mytetris.png` (*The Troll Piece*) by writing the PNG bytes directly (zlib + chunks, no Pillow). |
| `create_shortcut.ps1` | **Windows** — creates a Desktop `.lnk` for any app (parameterized: `-Script`, `-Icon`, `-Name`). |
| `create_shortcut.command` | **macOS** — builds `mytetris.icns` and a clickable `.app` on the Desktop (optional args: name, target script). |

## License

See [`LICENSE`](LICENSE).
