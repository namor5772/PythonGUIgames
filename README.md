# PythonGUIgames

A collection of Python GUI games built with **Tkinter**:

- **MyTetris** — an accurate, guideline-faithful clone of the classic game.
- **MyPocketTanks** — a turn-based artillery duel on destructible terrain,
  in the spirit of Pocket Tanks / Scorched Earth.

Everything here is **pure standard library** — Tkinter, `wave`, `json`,
`winsound`/`afplay`, etc. all ship with CPython, so there's nothing to `pip
install`. The game logic is platform-independent; only three things differ per
OS, and each degrades gracefully:

| Concern | Windows | macOS |
| --- | --- | --- |
| Sound playback | `winsound` (in-memory WAVs) | `afplay` (temp WAVs) |
| Saved data | `%APPDATA%\<game>\` | `~/<game>/` |
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
python3 MyTetris.py                 # play Tetris
python3 MyPocketTanks.py            # play the artillery duel
python3 MyTetris.py --selftest      # headless logic self-test (no window)
python3 MyPocketTanks.py --selftest
```

**Windows (PowerShell):**

```powershell
py -m venv .venv                    # one-time: create the (git-ignored) venv
.venv\Scripts\Activate.ps1          # activate it
.venv\Scripts\python.exe MyTetris.py                 # play Tetris
.venv\Scripts\python.exe MyPocketTanks.py            # play the artillery duel
.venv\Scripts\python.exe MyTetris.py --selftest      # headless self-tests
.venv\Scripts\python.exe MyPocketTanks.py --selftest
```

The `.venv` keeps the project isolated for if dependencies are ever added; since
there are none today, plain `python`/`python3` works just as well. To leave the
venv, run `deactivate`.

`--selftest` drives every difficulty for thousands of simulated frames and
asserts that nothing throws — handy for catching regressions without opening a
window.

## Controls (MyTetris)

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

## MyPocketTanks

A turn-based **artillery duel** in the spirit of Pocket Tanks / Scorched Earth,
written from scratch for Tkinter. Two tanks trade shots across procedurally
generated, **fully destructible terrain**. There are no health bars — every
point of damage you deal is a point on the scoreboard, and whoever has the
most points after all volleys **wins**. (Damaging yourself scores for your
opponent!)

- **Two match styles** — the classic **draft** (players alternate picking
  **10 weapons each** from a randomized pool of 20 cards, so every match
  plays differently), or a **one-weapon match**: choose a single weapon that
  *both* tanks fire for a settable **1–20 rounds** — set the round count on
  the menu, then click the weapon on the pick screen (your last pick is
  highlighted in gold).
- **20 distinct weapons** — simple shells and big blasts; multi-shot spreads
  (Triple Shot, Buckshot, Cluster Pod, Pentabomb — a 5-way MIRV); terrain
  tools (Dirt Ball, Excavator, Dirt Slinger, Drill Bit, Tremor); and exotics
  like the downhill-seeking **Steamroller**, ground-skipping **Skimmer**,
  bouncing and hopping bombs, downhill-flowing **Firestorm** napalm, the
  enemy-seeking **Magno Shot**, the orbital **Sky Laser**, and the colossal
  **Kiloton**.
- **Wind** changes every turn and bends every shot; an arrow above the field
  shows direction and strength.
- **Tank movement** — drive with A/D, limited by a match-long **fuel budget**
  and slopes too steep to climb.
- **1-player vs. an aiming AI** (Easy / Normal / Hard — it genuinely simulates
  trajectories, with difficulty controlling its aim error) or **2-player
  hotseat**.
- **Destructible terrain** — craters, trenches, dirt piles, and collapsing
  ground, painted per-pixel; tanks settle when the ground under them is blown
  away.
- **Synthesized sound** and a **control panel** where everything (angle, power,
  movement, weapon select, FIRE) is clickable with the mouse too.

### Controls (MyPocketTanks)

| Key | Action | Key | Action |
| --- | --- | --- | --- |
| ← / → | Turret angle (Shift = ×5) | ↑ / ↓ | Power (Shift = ×5) |
| End / Home | Angle up / down (Shift = ×5) | A / D | Drive tank (uses fuel) |
| Tab / `[` `]` | Cycle weapon | Space / Enter | **FIRE** |
| M | Mute / unmute | Esc | Back to menu (asks first) |
| R | Rematch (after game over) | | |

Config (window position, last mode, AI level, match style, one-weapon round
count and last weapon) persists in `%APPDATA%\MyPocketTanks\config.json`
(macOS/Linux: `~/MyPocketTanks/`), and `--selftest` fires every weapon
headlessly plus plays full AI-vs-AI matches at every difficulty, including
one-weapon matches.

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

**Prefer the funny icon?** Generate **"The Troll Piece"** as a multi-resolution
`.ico` and point the shortcut at that instead:

```powershell
.venv\Scripts\python.exe make_troll_icon.py
.\create_shortcut.ps1 -Icon mytetris_troll.ico
```

This writes **`mytetris_troll.ico`** — 16/32/48 px BMP frames plus a 256 px PNG
frame, so it stays sharp from the taskbar to Explorer's extra-large view. It's
the same gag described under **macOS** below (and reuses that scene), brought to
Windows.

**MyPocketTanks shortcut:**

```powershell
.venv\Scripts\python.exe make_pockettanks_icon.py
.\create_shortcut.ps1 -Script MyPocketTanks.py -Icon mypockettanks.ico -Name "MyPocketTanks"
```

This writes **`mypockettanks.ico`** (a tiny artillery duel — a red tank lobbing
a shell into an explosion under a starry sky, drawn directly as ICO bytes) and
creates **`MyPocketTanks.lnk`** on your Desktop.

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
| `MyPocketTanks.py` | Artillery duel — weapon draft or one-weapon matches (settable 1–20 rounds), 20 weapons, destructible terrain, wind, fuel-limited movement, AI or hotseat, sound — plus a headless `--selftest`. |
| `make_tetris_icon.py` | **Windows icon** — generates `mytetris.ico` by writing the ICO/BMP bytes directly (no Pillow). |
| `make_pockettanks_icon.py` | **Windows icon** — generates `mypockettanks.ico` (tank, shell arc, explosion, starry sky); reuses `make_tetris_icon.build_ico()`. No Pillow. |
| `make_troll_icon.py` | **Windows funny icon** — generates `mytetris_troll.ico` (*The Troll Piece*) as a multi-resolution ICO (256 px PNG + 48/32/16 px BMP), reusing the macOS scene. No Pillow. |
| `make_tetris_icon_mac.py` | **macOS icon** — generates `mytetris.png` (*The Troll Piece*) by writing the PNG bytes directly (zlib + chunks, no Pillow). |
| `create_shortcut.ps1` | **Windows** — creates a Desktop `.lnk` for any app (parameterized: `-Script`, `-Icon`, `-Name`). |
| `create_shortcut.command` | **macOS** — builds `mytetris.icns` and a clickable `.app` on the Desktop (optional args: name, target script). |

## License

See [`LICENSE`](LICENSE).
