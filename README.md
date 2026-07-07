# PythonGUIgames

A collection of Python GUI games built with **Tkinter**:

- **MyTetris** — an accurate, guideline-faithful clone of the classic game.
- **MyPocketTanks** — a turn-based artillery duel on destructible terrain,
  in the spirit of Pocket Tanks / Scorched Earth.
- **Sun2Set** — a bonus *non-game* in the same style: a sunrise / sunset
  almanac that computes, graphs, saves and reloads a year of sun times for
  any location on Earth.

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

> **No sound on macOS?** Check the system mute first: `afplay` reports success
> even when the output device is muted, so the game can't tell the difference.
> Both games print a startup warning to stderr when they detect this; unmute
> with F10 or `osascript -e 'set volume without output muted'`.

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
python3 Sun2Set.py                  # the sunrise/sunset almanac
python3 MyTetris.py --selftest      # headless logic self-test (no window)
python3 MyPocketTanks.py --selftest
python3 Sun2Set.py --selftest
```

**Windows (PowerShell):**

```powershell
py -m venv .venv                    # one-time: create the (git-ignored) venv
.venv\Scripts\Activate.ps1          # activate it
.venv\Scripts\python.exe MyTetris.py                 # play Tetris
.venv\Scripts\python.exe MyPocketTanks.py            # play the artillery duel
.venv\Scripts\python.exe Sun2Set.py                  # the sunrise/sunset almanac
.venv\Scripts\python.exe MyTetris.py --selftest      # headless self-tests
.venv\Scripts\python.exe MyPocketTanks.py --selftest
.venv\Scripts\python.exe Sun2Set.py --selftest
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
| Tab / Shift-Tab | Walk the buttons (menus & dialogs) | Enter | Press the focused button |
| ← → / Home / End | Previous / next button | ↑ ↓ / PgUp / PgDn | Button above / below |

On the **start menu** every control is a real button — pick a difficulty,
nudge the speed ramp with the **−/+** buttons (or **`[`** / **`]`**), and
START — reachable with **Tab / Shift-Tab or the arrow keys** (a light-blue
halo shows the focus), pressed with **Enter**, or simply clicked. The same
goes for the pause, confirm and game-over dialogs; during play the arrows
are piece controls as ever.

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

- **Start menu** — choose a difficulty before each game from a button list
  (keyboard-navigable with Tab/arrows + Enter, or clickable); **Enter** with
  nothing focused still starts straight away.
- **Difficulty levels:**
  - *Easy* — slower base fall, gentle start.
  - *Normal* — standard base fall speed.
  - *Hard* — starts at **level 5**, faster base fall, and a **×1.25** score
    multiplier.
- **Adjustable speed ramp** — how quickly gravity speeds up as you level is a
  separate setting you control right on the **start menu**: the **−/+**
  buttons (or **`[`** and **`]`**) lower / raise the **SPEED** value (range
  `0.20`–`1.00`). `1.0`
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

### The apps in native C++ (no Python needed)

`MyTetris.cpp`, `MyPocketTanks.cpp` and `Sun2Set.cpp` are from-scratch
**C++17 ports with identical functionality** to their Python twins — same
mechanics (SRS / 7-bag / T-spins; 20 weapons / destructible terrain /
trajectory-simulating AI; NOAA solar math / embedded WMM2025 / DST rules /
hover-readout graph), same screens, same synthesized sounds, the same
`--selftest`s **including every pinned reference value** (the WMM test
vectors to ±0.01°, the WolframAlpha solar times, the table round-trips),
and the same `%APPDATA%` JSON files, so the Python and native versions
**share saved settings, high scores and config**. In the repo's
stdlib-only spirit they use no third-party libraries: each is one file — a
platform-free core plus a Win32 + GDI backend on Windows and a Cocoa
backend on macOS (the same file compiles as Objective-C++ there).

**Windows** (needs Visual Studio with the C++ workload — any edition):

```powershell
.\build_native.ps1                        # -> build\MyTetris.exe
.\build_native.ps1 -App MyPocketTanks     # -> build\MyPocketTanks.exe
.\build_native.ps1 -App Sun2Set           # -> build\Sun2Set.exe
build\MyTetris.exe --selftest             # same headless checks as the .py
```

The exes are fully standalone (0.4–0.6 MB): static CRT, only core Windows
DLLs, icon embedded — copy them to any Windows machine and run; no Python,
no runtime installs.

**macOS** (needs the Xcode Command Line Tools: `xcode-select --install`):

```bash
./build_native.command                    # -> build/MyTetris(.app)
./build_native.command MyPocketTanks      # -> build/MyPocketTanks(.app)
./build_native.command Sun2Set            # -> build/Sun2Set(.app)
./build/MyTetris --selftest
```

The `.app`s are self-contained (native binary + icon, only system
frameworks) — copy them to Applications or the Desktop and double-click.

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
  highlighted in gold and starts keyboard-focused, so Enter replays it).
- **20 distinct weapons** — simple shells and big blasts; multi-shot spreads
  (Triple Shot, Buckshot, Cluster Pod, Pentabomb — a 5-way MIRV); terrain
  tools (Dirt Ball, Excavator, Dirt Slinger, Drill Bit, Tremor); and exotics
  like the downhill-seeking **Steamroller**, ground-skipping **Skimmer**,
  bouncing and hopping bombs, downhill-flowing **Firestorm** napalm, the
  enemy-seeking **Magno Shot**, the orbital **Sky Laser**, and the colossal
  **Kiloton**.
- **Wind** changes every turn and bends every shot; an arrow above the field
  shows direction and strength.
- **Tank movement** (optional — the MOVING toggle on the menu) — drive with
  A/D, limited by a match-long **fuel budget** and slopes too steep to climb.
  When it's on, the **AI drives too**: it relocates when a hill blocks its
  best shot (watch for *"computer is driving..."*) and scoots now and then
  to stay unpredictable, all on the same fuel budget.
- **1-player vs. an aiming AI** (Easy / Normal / Hard — it genuinely simulates
  trajectories, with difficulty controlling its aim error) or **2-player
  hotseat**.
- **Destructible terrain** — craters, trenches, dirt piles, and collapsing
  ground, painted per-pixel; tanks settle when the ground under them is blown
  away.
- **Synthesized sound** and a **control panel** where everything (angle, power,
  movement, weapon select, FIRE) is clickable with the mouse too.
- **Fully mouse-free** — every button on every screen (menu, weapon cards,
  aim panel, modals) is reachable with **Tab / Shift-Tab** or the **arrow
  keys** (←/→/Home/End step through, ↑/↓/PgUp/PgDn jump to the button
  above/below — the weapon grid moves column-wise) and pressed with
  **Enter**; a light-blue halo shows the focus. During your aim turn the
  arrows keep doing angle/power, so use Tab there.

### Controls (MyPocketTanks)

| Key | Action | Key | Action |
| --- | --- | --- | --- |
| ← / → | Turret angle (Shift = ×5) | ↑ / ↓ | Power (Shift = ×5) |
| End / Home | Angle up / down (Shift = ×5) | A / D | Drive tank (uses fuel; needs MOVING on) |
| `[` `]` | Cycle weapon | Space / Enter | **FIRE** |
| Tab / Shift-Tab | Walk the buttons (any screen) | Enter | Press the focused button |
| ← → / Home / End | Previous / next button (menus, cards & dialogs) | ↑ ↓ / PgUp / PgDn | Button above / below (grids move column-wise) |
| M | Mute / unmute | Esc | Back to menu (asks first) |
| R | Rematch (after game over) | | |

Config (window position, last mode, AI level, match style, one-weapon round
count and last weapon, moving on/off) persists in
`%APPDATA%\MyPocketTanks\config.json`
(macOS/Linux: `~/MyPocketTanks/`), and `--selftest` fires every weapon
headlessly plus plays full AI-vs-AI matches at every difficulty, including
one-weapon matches and the keyboard-focus model.

## Sun2Set

A bonus **non-game** app in the same stdlib-only Tkinter style: a **sunrise /
sunset almanac**. Type a latitude / longitude — decimals (`-34.2196`),
hemisphere letters (`34.2196 S`), degrees-minutes-seconds
(`34°13'10.5" S`), or text pasted straight from Wikipedia / Google Maps
(Unicode minus signs, degree marks and hidden spaces are all handled) —
choose how the time zone should be handled, and it computes **sunrise,
sunset — each with its azimuth (true *and* magnetic) and how long the disc
takes to cross the horizon — and day length for today and every day for a
year ahead** (366 rows by default; 1–1500 settable, any start date).

- **Accurate** — NOAA solar-position equations (Jean Meeus), with the solar
  declination and the equation of time re-evaluated *at each event's own
  time*; results are typically within a minute or two of published almanac
  values (times *and* azimuths spot-verified against WolframAlpha for
  Sydney, London and Reykjavik). Sunrise/sunset use the standard zenith of
  **90.833°** (atmospheric refraction + the solar disc radius).
- **Azimuths, true *and* magnetic** — the `RiseAz` / `SetAz` columns give
  the sun's bearing at each event, in degrees clockwise from **true north**
  (N=0°, E=90°), i.e. where on your horizon the sun actually appears and
  disappears — watch the rise point sweep from NE in June to SE in December
  (Sydney) and cross exactly 90° at the equinoxes. Right beside them,
  `RiseMag` / `SetMag` give **the number a magnetic compass actually
  reads**: the app embeds the official **World Magnetic Model (WMM2025,
  NOAA/BGS)** and applies your location's magnetic declination for *each
  row's own date* — no hand arithmetic, and the values keep themselves
  current as the magnetic pole drifts (the file header states the
  declination used, e.g. `12.3 deg E` around Sydney). Module helpers
  `magnetic_declination(lat, lon, date)`, `true_to_magnetic(bearing, …)`
  and `magnetic_to_true(bearing, …)` do one-off conversions in a Python
  shell. (Compass *deviation* from nearby metal is yours to manage.)
- **Crossing durations** — the `RiseDur` / `SetDur` columns time the event
  itself: from the upper limb's first gleam to the lower limb lifting clear
  (reversed at sunset). The disc must climb its own 32′ diameter — both
  endpoints put a limb on the *same* horizon line, so refraction cancels —
  which takes ~2–3 minutes at mid-latitudes (Earth's fastest, ~2 min 8 s,
  is an equatorial equinox; Reykjavik's midwinter crossing runs ~11 min).
  On grazing polar-circle days the sun rises without the disc ever fully
  clearing the horizon; the duration then shows `--:--` (when the sun
  first returns to Longyearbyen in February, the first complete crossing
  takes ~40 minutes).
- **Time zones done honestly** — either your **system zone with DST applied
  per day** from the OS rules (the sunrise/sunset curves show the 1-hour steps
  at each changeover while day length stays smooth), or any **fixed UTC
  offset** (`10`, `-3.5`, `9:30`…) — optionally with **manual daylight-saving
  rules**: tick *with daylight saving*, set the DST offset and the start/end
  rules the way the laws are written (`1st Sun of Oct` → `1st Sun of Apr`),
  and fixed mode follows any location's local law — reproducing the system
  zone's output exactly if you mirror its rules. Every row records the UTC
  offset it used, and the file header states the rules.
- **Polar-safe** — days when the sun never rises or never sets show
  `--:--:--` (with `---` azimuths and `--:--` durations) and 0 h / 24 h
  daylight (try Longyearbyen: `78.2232`, `15.6267`).
- **Valley / hills aware** — an optional **skyline** raises the horizon: a
  single number for a uniform ridge (`5`), or an `az:alt` profile
  (`60:2, 90:6, 240:8`; azimuth N=0°, E=90°, linearly interpolated with
  wrap-around) when the hills differ by direction. Sunrise/sunset fire when
  the sun's upper edge clears *that* line — atmospheric refraction is
  re-evaluated at the hill altitude (Bennett's formula), the event's own
  azimuth picks which hill applies, and winter days when the sun never
  clears the ridge show `--:--:--`. The skyline is recorded in the file
  header with the other assumptions.
- **Graph** — sunrise and sunset curves over a shaded daylight band, a
  day-length curve on the same 24 h axis, month gridlines, and a **hover
  crosshair** that reads out the exact times, azimuths and crossing
  durations for any day (true bearings on the first line, the horizon
  crossings and the magnetic compass pair beneath).
- **Text table** — the same data as a commented text file whose header states
  every assumption (location, latitude, longitude, time-zone handling,
  algorithm, units); view it on the **TABLE** tab, **SAVE AS…** anywhere, and
  **LOAD…** a saved file later to re-display its graph. Loading also
  **restores every assumption from the header** — coordinates, time-zone /
  DST settings, skyline and range go back into the form — so a saved file
  regenerates itself with one CALCULATE on any machine (files saved before
  the duration columns existed — or the magnetic ones, or the azimuths —
  still load). Every calculation is
  also autosaved to `%APPDATA%\Sun2Set\sun2set_latest.txt` (macOS/Linux:
  `~/Sun2Set/`). The Save/Load dialogs open in your Documents folder the
  first time, then remember the folder you last used (persisted).
- **Rounded controls, two looks** — every button is a rounded-corner widget
  (same style as MyPocketTanks), and a **THEME** toggle flips between the
  dark house style and a light theme, keeping everything you've typed; the
  choice persists in `config.json`.

The whole session persists in `%APPDATA%\Sun2Set\config.json`: the window
position, theme, active tab and **every input field exactly as typed** —
even edits you never calculated — so the app reopens just as you left it.
`--selftest` checks the solar math against reference almanac times and
azimuths, the crossing durations against an independent slant-rate formula
(15°/h · cos φ · sin *azimuth*, plus grazing polar days), the magnetic
model against the official NOAA/BGS WMM2025 test vectors (plus NOAA's
online calculator for Sydney, Binda, London and Reykjavik), polar cases,
the manual-DST rule engine (including that it reproduces the system zone
exactly), the skyline model (delays, profiles, ridge-blocked days),
window-position parsing, table round-trips (including pre-duration,
pre-azimuth and pre-magnetic legacy files) and save/load — all headlessly.

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

**Sun2Set shortcut:**

```powershell
.venv\Scripts\python.exe make_sun2set_icon.py
.\create_shortcut.ps1 -Script Sun2Set.py -Icon sun2set.ico -Name "Sun2Set"
```

This writes **`sun2set.ico`** (a sunset over the sea — the sun half-dipped
below the horizon with a golden reflection down the water, beneath the dotted
arc of its day-path across a dusk sky) and creates **`Sun2Set.lnk`** on your
Desktop.

`create_shortcut.ps1` is parameterized, so it can make a shortcut for any app:

```powershell
.\create_shortcut.ps1 -Script MyApp.py -Icon myapp.ico -Name "My App"
```

**Native `.exe` shortcut (no Python needed):** once you've compiled the standalone
executables (see the *native C++* section below), add `-Native` to point the
shortcut at `build\<App>.exe` directly, using the icon embedded in the exe:

```powershell
.\build_native.ps1 -App MyPocketTanks        # build it first -> build\MyPocketTanks.exe
.\create_shortcut.ps1 -Native -Script MyPocketTanks.py
```

These are auto-named **"<App> (Native)"** (e.g. `MyPocketTanks (Native).lnk`) so
they sit beside the Python shortcuts instead of overwriting them. A `.lnk` stores
an absolute path, so re-run this if you move the `build\` folder.

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

**MyPocketTanks shortcut:**

```bash
python3 make_pockettanks_icon_mac.py
./create_shortcut.command MyPocketTanks MyPocketTanks.py mypockettanks.png
```

This writes **`mypockettanks.png`** (the same artillery-duel scene as the Windows
icon, rendered natively at 1024 px), packs it into **`mypockettanks.icns`**, and
creates **`MyPocketTanks.app`** on your Desktop.

**Sun2Set shortcut:**

```bash
python3 make_sun2set_icon_mac.py
./create_shortcut.command Sun2Set Sun2Set.py sun2set.png
```

This writes **`sun2set.png`** (the same sunset-over-the-sea scene as the
Windows icon, rendered natively at 1024 px), packs it into **`sun2set.icns`**,
and creates **`Sun2Set.app`** on your Desktop.

`create_shortcut.command` takes the same optional name / target script / icon:

```bash
./create_shortcut.command "My App" MyApp.py myapp.png
```

> Both shortcut builders bake in **absolute paths**, so re-run the relevant script
> if you move the project folder. macOS sometimes caches the old Finder/Dock icon;
> if the new one doesn't appear immediately, it refreshes on next login.

## Sharing the apps with friends

Each app packs into a single zip that anyone on Windows or macOS can
install without knowing anything about Python:

```powershell
.venv\Scripts\python.exe package_games.py            # all three zips
.venv\Scripts\python.exe package_games.py MyTetris   # or just one
```

This writes `dist\MyPocketTanks.zip`, `dist\MyTetris.zip` and
`dist\Sun2Set.zip` (~60–76 KB each). Every zip contains the app, its icons,
the license, a per-app plain-text `README.txt` install-and-play guide, and
two one-click installers (one templated source pair in `packaging/`,
customized per app at zip time):

- **`INSTALL-Windows.bat`** — finds an installed Python 3 (opening the
  python.org download page with instructions if there isn't one), checks
  Tkinter, then creates a Desktop shortcut that runs the app with
  `pythonw.exe` so no console window appears.
- **`Install-macOS.command`** — same idea: finds a `python3` that has
  Tkinter, builds the `.icns` with the system `sips`/`iconutil`, and
  assembles a double-clickable `.app` on the Desktop. It ships LF-only with
  its executable bit stored in the zip, so it survives extraction on macOS.

Your friend just extracts the zip somewhere permanent and double-clicks the
installer for their OS — `README.txt` inside walks them through the
SmartScreen / Gatekeeper warnings and the (free) Python install if needed.

> **Don't email the zips as attachments** — Gmail (and most mail providers)
> block archives containing `.bat` files, even nested or renamed. Share a
> link instead: upload to Google Drive / OneDrive and set access to
> *Anyone with the link*, or attach the zip to a GitHub release.

## Scripts

| Script | Description |
| --- | --- |
| `MyTetris.py` | The game — accurate Tetris clone (SRS, 7-bag, DAS, ghost, hold, next-3, T-spins, back-to-back, start menu, difficulty, sound, high scores) plus a headless `--selftest`. |
| `MyTetris.cpp` | The same game as native C++17 (no third-party libraries): platform-free core + Win32/GDI and Cocoa backends in one file, sharing the .py's saved files — plus the same `--selftest`. |
| `MyPocketTanks.cpp` | The artillery duel as native C++17, same pattern: terrain pixel buffer, all 20 weapons, the driving AI, and the .py's config — plus the same `--selftest`. |
| `Sun2Set.cpp` | The almanac as native C++17: NOAA solar math, embedded WMM2025, DST engine, text-entry form, hover-readout graph, native file dialogs, themes — plus the same `--selftest` with all its pinned reference values. |
| `build_native.ps1` | **Windows** — compiles a native app (`-App MyTetris`/`MyPocketTanks`/`Sun2Set`) with Visual Studio's `cl` (found via vswhere) into a standalone `build\<App>.exe`, icon embedded. |
| `build_native.command` | **macOS** — compiles a native app with `clang++` (Objective-C++) and wraps a self-contained `build/<App>.app` with the icon. |
| `MyPocketTanks.py` | Artillery duel — weapon draft or one-weapon matches (settable 1–20 rounds), 20 weapons, destructible terrain, wind, optional fuel-limited movement (the AI drives too), AI or hotseat, sound — plus a headless `--selftest`. |
| `Sun2Set.py` | Bonus non-game — sunrise/sunset almanac: a year of sunrise, sunset and day length for any location (NOAA equations), azimuths in true *and* magnetic bearings (embedded WMM2025), horizon-crossing durations, DST-aware or fixed-offset, graph with hover readout, assumptions-headed text file with Save/Load — plus a headless `--selftest`. |
| `make_tetris_icon.py` | **Windows icon** — generates `mytetris.ico` by writing the ICO/BMP bytes directly (no Pillow). |
| `make_pockettanks_icon.py` | **Windows icon** — generates `mypockettanks.ico` (tank, shell arc, explosion, starry sky); reuses `make_tetris_icon.build_ico()`. No Pillow. |
| `make_sun2set_icon.py` | **Windows icon** — generates `sun2set.ico` (sunset over the sea, with the sun's dotted day-path arc); reuses `make_tetris_icon.build_ico()`. No Pillow. |
| `make_troll_icon.py` | **Windows funny icon** — generates `mytetris_troll.ico` (*The Troll Piece*) as a multi-resolution ICO (256 px PNG + 48/32/16 px BMP), reusing the macOS scene. No Pillow. |
| `make_tetris_icon_mac.py` | **macOS icon** — generates `mytetris.png` (*The Troll Piece*) by writing the PNG bytes directly (zlib + chunks, no Pillow). |
| `make_pockettanks_icon_mac.py` | **macOS icon** — generates `mypockettanks.png` (the artillery duel at 1024 px), reusing `make_tetris_icon_mac.py`'s rasterizer + PNG writer. No Pillow. |
| `make_sun2set_icon_mac.py` | **macOS icon** — generates `sun2set.png` (the sunset scene at 1024 px), reusing `make_tetris_icon_mac.py`'s rasterizer + PNG writer. No Pillow. |
| `create_shortcut.ps1` | **Windows** — creates a Desktop `.lnk` for any app (parameterized: `-Script`, `-Icon`, `-Name`); add `-Native` to launch the compiled `build\<App>.exe` instead of Python (embedded icon, auto-named "<App> (Native)"). |
| `create_shortcut.command` | **macOS** — builds an `.icns` and a clickable `.app` on the Desktop (optional args: name, target script, icon PNG; defaults to MyTetris). |
| `package_games.py` | Builds `dist/<Name>.zip` friend-shareable bundles (all three apps, or the ones named on the command line): app + icons + license + per-app install guide + the one-click Windows/macOS installers templated from `packaging/`. |

## License

See [`LICENSE`](LICENSE).
