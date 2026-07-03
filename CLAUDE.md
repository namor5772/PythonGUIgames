# CLAUDE.md

Guidance for working in this repository. See `README.md` for user-facing docs.

## Project

`PythonGUIgames` — a collection of Python **Tkinter** GUI games. The main apps
are `MyTetris.py`, an accurate, guideline-faithful Tetris clone, and
`MyPocketTanks.py`, a turn-based artillery duel (Pocket Tanks / Scorched Earth
style) on destructible terrain. `Sun2Set.py` is a bonus non-game in the same
style: a sunrise/sunset almanac (a year of sun times, graphed and exportable).

**Pure standard library only.** No third-party packages — Tkinter, `winsound`,
`wave`, `json`, etc. all ship with CPython. Keep it dependency-free; don't add a
`requirements.txt` or import anything outside the stdlib without good reason.

## Environment (Windows)

- Python 3.14 via the `py` launcher; the preferred interpreter is the venv:
  `.venv\Scripts\python.exe` (GUI without a console: `.venv\Scripts\pythonw.exe`).
- Recreate the venv with `py -m venv .venv` (it's git-ignored).
- `.vscode/settings.json` points VS Code at the venv and auto-activates it in
  new terminals.

## Run & test

```powershell
.venv\Scripts\python.exe MyTetris.py              # play
.venv\Scripts\python.exe MyTetris.py --selftest   # headless logic check, no window
.venv\Scripts\python.exe MyPocketTanks.py             # same pattern
.venv\Scripts\python.exe MyPocketTanks.py --selftest
.venv\Scripts\python.exe Sun2Set.py                   # same pattern
.venv\Scripts\python.exe Sun2Set.py --selftest
```

`--selftest` drives all difficulties for thousands of frames and asserts no
exceptions. For mechanics that random play rarely triggers (T-spins, B2B,
persistence), write a short throwaway `_*.py` script that **constructs** the exact
state and asserts on it, run it, then delete it. Validate GUI/visuals by launching
with `pythonw.exe` and capturing a screenshot via .NET `CopyFromScreen`, then
`Read` the PNG. Always delete temp scripts/PNGs afterward (none should be committed).

## MyPocketTanks architecture

Same logic/rendering split as MyTetris: `PocketTanks(root=None)` runs fully
headless — `step()` is pure logic driven by `--selftest`; `tick()` = `step()` +
`draw()` when a canvas exists.

- **State machine:** `state in {menu, pick, playing, gameover}` plus a
  `confirm_menu` modal flag; within `playing`, `phase in {aim, flight}`.
- **Two match styles:** `match_type` `"draft"` (alternate-pick 10 from a
  20-card pool) or `"single"` (one chosen weapon arms both tanks for
  `single_rounds` = 1–20 shots each; the pick screen shows all 20 weapons and
  one click starts combat). `self.rounds` is the live per-match shot count —
  branch on it, not the `ROUNDS` constant (that's draft's fixed 10).
- **Terrain is a per-column heightmap** (`terrain[x]` = surface y). Explosions
  carve circles, dirt weapons raise columns; no overhangs by design — that's
  what keeps physics/repaints simple. Rendered into one `tk.PhotoImage`;
  `_repaint_terrain(x0, x1)` repaints only the touched column range (full
  repaints are slow — avoid per-frame).
- **Weapons are data** (`WEAPONS` list) + a `kind` the projectile engine
  dispatches on in `_impact`/`_detonate`. New weapon = new dict + a branch.
- **Scoring is Pocket-Tanks style:** no health; damage dealt = points, self
  damage scores for the opponent. Damage-over-time (napalm) must be budgeted
  (see `_spawn_flames`) or scores explode.
- **AI** picks angle/power by simulating real trajectories
  (`_simulate_shot`), then blurs the answer by difficulty `aim_err`.
- **Persistence:** `%APPDATA%\MyPocketTanks\config.json` (window pos, mode,
  AI level, match style/rounds/last one-weapon pick), gated by the `persist`
  flag like MyTetris.

## Sun2Set architecture

Same logic/GUI split: `Sun2Set(root=None)` is fully headless — parameters are
plain attributes, and `compute()` / `save_text_file()` / `load_text_file()`
never touch Tk (tk Vars are created only inside `_build_gui`).

- **Solar math is pure module functions** (NOAA/Meeus). `sun_events(date, lat,
  lon, tz_hours)` returns minutes after local midnight; declination and the
  equation of time are re-evaluated *at each event's estimated time* (2
  passes) — that refinement is what matches the NOAA reference calculator.
  Zenith 90.833° = refraction + solar disc radius. `--selftest` pins
  WolframAlpha-verified times for London / Sydney / Reykjavik (±120 s) plus
  polar cases; keep those anchors when touching the math.
- **DST is resolved per-day, not per-datetime:** `system_offset_minutes(date)`
  asks the OS for the offset at local *noon* (transitions happen ~2–3 AM, so
  noon's offset is in force at both sunrise and sunset). Both events use the
  same offset, which keeps `day == set - rise` physically true across
  changeovers; the 1-hour wall-clock steps visible in the curves are correct.
- **Manual DST rules** (fixed mode) encode how the laws are written: a rule
  is `(ordinal, weekday, month)` with ordinal −1 = "last"; `dst_active` is
  start-date *inclusive*, end-date *exclusive* (exactly what noon sampling
  of a real zone yields), and start-month > end-month wraps New Year
  (southern hemisphere). The selftest proves Sydney's rules (+10:00 std,
  +11:00 from 1st Sun Oct to 1st Sun Apr) reproduce system mode row-for-row.
- **Skyline (raised horizon):** `parse_horizon` yields `[]` (flat), one
  point (uniform hills) or an az:alt profile; `horizon_alt` interpolates
  linearly with wrap-around. The event condition swaps the fixed 90.833°
  zenith for `_zenith_for_horizon(h)` = 90° − h + Bennett refraction(h) +
  16′ solar radius (a fixed 34′ would overshoot raised horizons by minutes),
  and the per-event iteration also converges the azimuth → skyline lookup.
  Single-crossing model — it does not simulate multiple rises through
  notches in a jagged ridge. Sun-never-clears-the-ridge days reuse the
  polar-night path ('--:--:--', 0 h).
- **The text file is the interchange format:** `build_table_text` /
  `parse_table_text` round-trip exactly (selftest-enforced) — Load rebuilds
  the graph from the file alone. Header lines are `# Key : value` (unknown
  keys ignored). Keep the file plain ASCII so it opens cleanly anywhere.
- **Polar days** carry rise/set `None` end-to-end; graph code treats `None`
  as a segment break (curves flush; the daylight band emits one polygon per
  contiguous run). Never plot `None` as 0.
- **Gotcha:** `Canvas.tkraise()` raises canvas *items*, not the widget — the
  GRAPH/TABLE tab switcher raises wrapper Frames instead.
- **Persistence:** `%APPDATA%\Sun2Set\config.json` (window pos + last params)
  and an autosave of the latest table to `%APPDATA%\Sun2Set\sun2set_latest.txt`
  on every Calculate — both gated by the `persist` flag like the games.

## MyTetris architecture

One file, one class (`TetrisGame`). The key design choice is a **clean split
between game logic and Tkinter rendering/input** — that's what makes the logic
testable headlessly. Preserve it.

- **State machine:** `state in {menu, playing, paused, gameover}`, plus a
  `confirm_menu` flag layered over `paused` for the "return to menu?" modal.
  Adding behavior usually means a new branch, not a new top-level state.
- **Fixed-timestep loop:** `tick()` runs every `FRAME_MS` (~60 FPS) and advances
  gravity, DAS, and lock delay by a fixed `dt`. Don't use wall-clock timing.
- **Rotation = SRS:** pieces are matrices rotated 90°, then `KICKS_*` wall-kick
  tables are tried in order. T-spin detection needs `last_action_was_rotate` and
  `last_kick_index`, which is why every move/rotate maintains them.
- **Persistence** lives in `%APPDATA%\MyTetris\` (macOS/Linux: `~/MyTetris/`):
  `highscores.json` (top-10 per difficulty) and `config.json` (window position
  **and** the player's `speed_step` speed-ramp setting — one shared dict, so
  each saver merges into `self.config` rather than overwriting the file). Both
  load defensively (missing/corrupt → empty) and are gated by the `persist` flag
  so tests with `persist=False` never write real files. User data — never
  committed.

## Gotchas discovered here (don't re-learn these the hard way)

- **`winsound.PlaySound(data, SND_MEMORY | SND_ASYNC)` raises** "Cannot play
  asynchronously from memory" — the combo is forbidden. We play in-memory WAVs
  *synchronously on a daemon thread* instead. Don't reintroduce `SND_ASYNC` with
  `SND_MEMORY`.
- **Sound is per-platform.** `SoundManager` synthesizes the same WAV bytes
  (`_sound_specs()`) on every OS, then picks a backend by `sys.platform`: Windows
  uses `winsound` from memory; macOS has no stdlib audio API, so it writes each
  WAV once to a temp dir and plays it with the system `/usr/bin/afplay` (cleaned
  up via `atexit`). `afplay` is an OS tool, not a package, so it stays within the
  stdlib-only spirit (same as `sips`/`iconutil` for the icon). Other platforms
  fall back to silent (`enabled=False`). Keep the WAV synthesis backend-agnostic.
- **"No sound" on macOS may be the OS mute, not the code.** `afplay` exits 0
  even when the default output device is muted (`osascript -e 'get volume
  settings'` → `output muted:true`), so playback "works" inaudibly — exactly
  what a broken backend would look like. Both `SoundManager`s now check this at
  startup (daemon thread, `/usr/bin/osascript`) and warn on stderr. Rule out
  the mute before touching the sound code.
- **Window-position creep:** save/restore the position from `root.geometry()`
  (the wm position), not `winfo_x/y` (the content position) — mixing them makes
  the window drift down-right by the title-bar height every launch.
- **Don't let `except Exception: pass` hide bugs during development.** The sound
  failure was invisible for three commits because the error was swallowed. When
  something "silently doesn't work," temporarily surface the exception first.

## Conventions

- Match the existing style: stdlib only, `Consolas` font, the `COLORS` palette,
  educational-but-concise comments.
- Update `README.md` (and this file) when adding features or scripts.
- Commit/push only when asked. Commit messages end with the `Co-Authored-By:`
  trailer. Prefer committing each fix as soon as it's verified, so unrelated
  changes don't pile into one commit.
- **PowerShell commit gotcha:** here-strings with embedded quotes get mangled
  when piped to `git commit -m`. Write the message to a temp file and use
  `git commit -F <file>`.
- Use `git add -A` (not an explicit file list) when a change includes deletions,
  so removals are staged too.

## Scripts

| Script | Purpose |
| --- | --- |
| `MyTetris.py` | The game (and its `--selftest`). |
| `MyPocketTanks.py` | The artillery duel (and its `--selftest`). |
| `Sun2Set.py` | The sunrise/sunset almanac (and its `--selftest`). |
| `make_tetris_icon.py` | Generates `mytetris.ico` (tidy falling-T scene) by writing the ICO/BMP bytes directly (no Pillow). |
| `make_pockettanks_icon.py` | Generates `mypockettanks.ico` (tank + shell arc + explosion scene). Reuses `make_tetris_icon.build_ico()`; no Pillow. |
| `make_sun2set_icon.py` | Generates `sun2set.ico` (sunset over the sea + the sun's dotted day-path arc). Reuses `make_tetris_icon.build_ico()`; no Pillow. |
| `make_troll_icon.py` | Generates `mytetris_troll.ico` — the funny "Troll Piece" as a multi-resolution Windows ICO (256px PNG + 48/32/16 BMP). Reuses `make_tetris_icon_mac.build_scene()` and downsamples; no Pillow. |
| `make_tetris_icon_mac.py` | Generates `mytetris.png` (the macOS "Troll Piece" icon) by writing the PNG bytes directly (zlib + chunks, no Pillow). |
| `make_pockettanks_icon_mac.py` | Generates `mypockettanks.png` (macOS, 1024px) — the same artillery-duel scene as the ICO, rendered natively; reuses `make_tetris_icon_mac`'s rasterizer/PNG writer. |
| `make_sun2set_icon_mac.py` | Generates `sun2set.png` (macOS, 1024px) — the same sunset scene as the ICO, rendered natively; reuses `make_tetris_icon_mac`'s rasterizer/PNG writer. |
| `create_shortcut.ps1` | **Windows** Desktop `.lnk`; parameterized `-Script` / `-Icon` / `-Name`, defaults to MyTetris. |
| `create_shortcut.command` | **macOS**: `sips`+`iconutil` build the `.icns`, then assemble a clickable `.app` on the Desktop. Args: name, script, icon PNG (defaults = MyTetris). Bakes in the found `python3` (Finder gives apps a minimal PATH) and absolute project paths. |
