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
- **Keyboard focus:** `self.buttons` (the clickable hit boxes) is rebuilt by
  every `draw()`, so focus tracks the button's *action string*
  (`focus_action`), never an index. Tab/Shift-Tab walk that list in draw
  order (`_focus_move`), Enter presses (`_focus_activate`, falling through
  to Enter's legacy meaning — fire/start/rematch — when nothing is
  focused), and `_button`/`_draw_pick` render a `FOCUS` halo. Handled at
  the top of `_on_key` (returning "break" to stop Tk's own Tab traversal)
  so it works in every state including the confirm modal; screen
  transitions clear `focus_action` — except the one-weapon pick screen,
  which enters with the last-picked (gold-outlined) card pre-focused so
  Enter alone replays the previous choice. Weapon cycling is `[`/`]` only
  — Tab was repurposed; don't rebind it.
- **Arrow-cluster navigation:** ←/Home = Shift-Tab, →/End = Tab, and
  ↑/PgUp / ↓/PgDn move *spatially* (`_focus_spatial`: nearest row above/
  below by vertical gap then horizontal offset, wrapping at the edges —
  that's what makes the 5×4 card grid move column-wise; same-row buttons
  are left/right's job, so vertical moves on a one-row screen are no-ops).
  Gated by an `aiming` check: during a human aim turn the arrow keys stay
  angle/power controls and only Tab navigates. This replaced the menu's
  old hidden Left/Right-cycles-AI-level shortcut (AI buttons are directly
  navigable now); `1`/`2` mode shortcuts survive.
- **Scoring is Pocket-Tanks style:** no health; damage dealt = points, self
  damage scores for the opponent. Damage-over-time (napalm) must be budgeted
  (see `_spawn_flames`) or scores explode.
- **AI** picks angle/power by simulating real trajectories
  (`_simulate_shot`), then blurs the answer by difficulty `aim_err`.
- **Tank driving is optional:** the menu MOVING toggle (`move_enabled`,
  persisted, default on) gates `move_tank` for humans and AI alike; the
  per-match `FUEL_MAX` budget is what keeps repositioning from dominating.
  The AI's plan may carry a `move_to` spot — chosen only from
  `_drive_range` (walks px-by-px under `move_tank`'s own fuel/slope/enemy
  rules) and rationed to half the remaining fuel per turn — when its best
  shot misses by more than the level's `move_err` (blocked by terrain) or
  as an occasional short scoot. The angle/power solution is searched *at*
  the destination; `_ai_act` drives 1 px/frame before swinging the turret.
- **Persistence:** `%APPDATA%\MyPocketTanks\config.json` (window pos, mode,
  AI level, match style/rounds/last one-weapon pick, moving toggle), gated
  by the `persist` flag like MyTetris.

## Sun2Set architecture

Same logic/GUI split: `Sun2Set(root=None)` is fully headless — parameters are
plain attributes, and `compute()` / `save_text_file()` / `load_text_file()`
never touch Tk (tk Vars are created only inside `_build_gui`).

- **Solar math is pure module functions** (NOAA/Meeus). `sun_events(date, lat,
  lon, tz_hours)` returns minutes after local midnight; declination and the
  equation of time are re-evaluated *at each event's estimated time* (2
  passes) — that refinement is what matches the NOAA reference calculator.
  Zenith 90.833° = refraction + solar disc radius. It also returns
  `rise_az`/`set_az` — the sun's azimuth at each event (deg clockwise from
  true north, E=90), converged in the same per-event iteration the skyline
  code uses — and `rise_dur`/`set_dur`, the disc's horizon-crossing time:
  a second solve with the center one `SUN_DIAMETER` (32′) higher, i.e. the
  lower limb on the *same* line, so refraction cancels out of the
  difference. Durations are `None` on grazing days (disc peeks over but
  never fully clears — real near the polar circles, where the first full
  crossing after the polar night takes ~40 min). `--selftest` pins
  WolframAlpha-verified times (±120 s) *and* azimuths (±2°) for London /
  Sydney / Reykjavik plus polar cases, and cross-checks durations against
  the independent slant formula 15°/h · cos φ · sin *az* (±8% — azimuth
  drift during long crossings is real) plus pinned values; keep those
  anchors when touching the math.
- **Geomagnetism is embedded, not fetched:** the official WMM2025 `.COF`
  coefficient file sits verbatim in `_WMM_COF` (public domain, NOAA/BGS —
  degree-12 spherical harmonics + linear secular variation).
  `_wmm_declination` follows the WMM technical report, including the WGS84
  geodetic→geocentric latitude step (skipping it fails the official
  vectors); `magnetic_declination(lat, lon, date)` is lru_cached per day,
  `true_to_magnetic`/`magnetic_to_true` wrap it, and `compute_rows` applies
  it per row: RiseMag/SetMag = the true azimuth minus *that row's own
  date's* declination. `--selftest` pins 16 official WMM2025 test vectors
  (±0.01°) plus NOAA online-calculator declinations (Sydney / Binda /
  London / Reykjavik, 2026-07-04); the NOAA API used for those anchors is
  `ngdc.noaa.gov/geomag-web/calculators/calculateDeclination?...&key=zNEw7`.
  Model refresh circa 2030: paste the next `.COF` over the string and
  update the selftest vectors in the same change.
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
  Data rows have 11 columns — RiseDur/SetDur (crossing time as MM:SS whole
  seconds, spilling to H:MM:SS if a polar-threshold crossing tops an hour;
  '--:--' on polar *and* grazing days; `parse_dur` also reads the brief
  HH:MM:SS interlude format) plus RiseAz/RiseMag and SetAz/SetMag all sit
  beside their events, azimuths stored rounded to 0.1° so `round(az,1)` →
  `%6.1f` → `float` is lossless ('---' on polar days); 9-column
  pre-duration, 7-column pre-magnetic and 5-column pre-azimuth files still
  parse (missing columns None, also selftest-enforced).
  Load also restores the *settings* by inverting the header's descriptive
  lines (`parse_tz_desc`, `parse_horizon_desc`), so a loaded file
  regenerates itself (selftest-enforced) — if you reword the `tz_desc` /
  `hz_desc` strings `compute()` emits, update those inverse parsers and the
  selftest in the same change.
- **Polar days** carry rise/set `None` end-to-end; graph code treats `None`
  as a segment break (curves flush; the daylight band emits one polygon per
  contiguous run). Never plot `None` as 0.
- **Gotcha:** `Canvas.tkraise()` raises canvas *items*, not the widget — the
  GRAPH/TABLE tab switcher raises wrapper Frames instead.
- **Gotcha:** `FONT` resolves per platform (Consolas is Windows-only; a
  family the OS lacks makes Tk substitute the *proportional* system font —
  ragged table columns, taller panel rows). Aqua widgets are taller than
  their Windows twins too, so don't pin the left panel's height
  (`pack_propagate(False)` clipped the status text at the bottom on macOS):
  the panel sizes naturally and the window grows to fit, while the status
  label sits in a fixed six-line `status_box` measured from the live font —
  messages can neither be clipped nor resize the window. Status paths are
  `~`-abbreviated (`_display_path`) so they fit that reserve.
- **Gotcha:** coordinates pasted from the web contain a Unicode minus
  (U+2212), degree marks and no-break/zero-width spaces that `float()`
  rejects while looking identical on screen (real bug report: Binda NSW's
  Wikipedia latitude). `parse_angle` + `_clean_numeric` normalize these and
  accept hemisphere letters and DMS — don't regress lat/lon parsing to a
  bare `float()`. The offset/skyline parsers share `_clean_numeric`.
- **Gotcha:** a Tk file dialog with no `initialdir` opens in the last
  folder used by *any* dialog of the same executable — Windows keys that
  memory to `pythonw.exe`, so it is shared across every Tkinter app on the
  machine (a foreign project's folder leaked into Sun2Set's Save dialog
  this way). Always pass `initialdir`; Sun2Set uses the persisted
  `file_dir` (last save/load folder), falling back to the real Documents
  folder read from the `User Shell Folders` registry key (Documents is
  often OneDrive-redirected — never guess `~/Documents`).
- **Theming:** every GUI color comes from `THEMES[self.theme]` via `self.T`
  — no color literals in widget/graph code (the graph aliases it as `th`
  because `T` is the plot's top margin there). The THEME toggle rebuilds the
  GUI (destroy root children → `_build_gui`) while preserving raw form
  state; `_pos_applied` stops the rebuild re-applying the saved window
  position. Buttons are `RoundButton` canvases (smoothed-polygon rounded
  rects, same trick as MyPocketTanks); the DST rule dropdowns are
  `RoundMenuButton`s that pop a `tk.Menu` — plain tk widgets can't round
  their corners.
- **Persistence:** `%APPDATA%\Sun2Set\config.json` stores the window
  position (via `_wm_position` — a naive `split('+')` mangles negative
  multi-monitor coordinates), theme, active tab, the last-validated params
  *and* a raw `form` snapshot of every entry exactly as typed; the snapshot
  is restored verbatim on launch (it wins over the validated fallbacks), so
  even never-calculated edits survive restarts. Plus an autosave of the
  latest table to `%APPDATA%\Sun2Set\sun2set_latest.txt` on every
  Calculate. All gated by the `persist` flag like the games; test with an
  isolated `APPDATA` env var so the real config stays safe.

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
- **Keyboard focus / buttons:** same model as MyPocketTanks (kept in sync by
  hand — the games stay single-file, so the ~90-line focus engine is
  duplicated, not imported): `self.buttons` hit boxes are rebuilt by every
  `render()`, focus tracks the *action string*, Tab/Shift-Tab +
  ←→/Home/End walk in draw order, ↑↓/PgUp/PgDn move spatially
  (`_focus_spatial`), Enter presses via `_do_action`, buttons are also
  clickable. The arrow cluster navigates only in `menu`/`paused`/`gameover`
  — during `playing` the arrows are piece controls. The menu's difficulty
  spinner became a button list; selecting via `_set_difficulty` also updates
  `self.diff` (the old spinner left it stale, so the menu blurb lagged one
  pick behind).
- **Persistence** lives in `%APPDATA%\MyTetris\` (macOS/Linux: `~/MyTetris/`):
  `highscores.json` (top-10 per difficulty) and `config.json` (window position
  **and** the player's `speed_step` speed-ramp setting — one shared dict, so
  each saver merges into `self.config` rather than overwriting the file). Both
  load defensively (missing/corrupt → empty) and are gated by the `persist` flag
  so tests with `persist=False` never write real files. User data — never
  committed.

## Native C++ ports (MyTetris.cpp, MyPocketTanks.cpp, Sun2Set.cpp)

C++17 ports with **identical functionality** to their .py twins — no
third-party libraries (the C++ analog of the stdlib-only rule: OS APIs
only). If you change mechanics/math in one version, mirror it in the
other; behavioral parity — including `--selftest` with every pinned
reference value — is the point.

- **Structure:** a platform-free core whose render emits draw *commands*
  (a reified Tk canvas: rects, ovals, polygons, round-rects, text with
  anchors) plus button hit boxes; the Win32/GDI and Cocoa backends just
  rasterize the scene. MyPocketTanks' terrain is additionally a pixel
  buffer the core repaints per column range (its Tk PhotoImage, reified)
  and backends blit via a `Terrain` command. Everything — every screen —
  stays headlessly testable with the same selftest assertions as the .py.
- **Same files on disk:** they read/write the Python versions'
  `%APPDATA%` JSON via a small built-in parser/writer (defensive like the
  Python loaders). Interop verified both directions, including
  MyPocketTanks' `win_pos` geometry-tail string.
- **Build:** `build_native.ps1 [-App X]` / `build_native.command [X]` —
  vswhere + `cl /MT` (static CRT → standalone exe, core Windows DLLs
  only), icon embedded as resource id 1 which the window class loads;
  mac: `clang++ -x objective-c++`, wrapped into a self-contained
  `build/<App>.app` via sips/iconutil. Outputs in `build/` (git-ignored).
- **Sun2Set specifics:** the whole Tk widget tree is reified as
  immediate-mode scene widgets (entries with caret editing + Ctrl/Cmd+V
  paste, radios, checkbox, dropdown popups, a wheel-scrolled table view);
  the WMM2025 `.COF` block is spliced verbatim from Sun2Set.py's
  `_WMM_COF` string into the `[[WMM_COF_DATA]]` raw literal by a small
  script (re-splice if the model is ever refreshed); per-day system-zone
  offsets come from `TzSpecificLocalTimeToSystemTime` at local noon
  (Windows) / `mktime`+`tm_gmtoff` (macOS); file dialogs are
  `GetOpen/SaveFileNameW` and `NSOpen/SavePanel`.
- **Gotcha (cost a real bug):** `JParser` keeps pointers into the parsed
  string — passing a temporary (`JParser p(ss.str())`) is
  use-after-free UB that *worked* in one build and silently returned
  empty configs in the next. Materialize the string first. When a C++
  port behaves nondeterministically across rebuilds, suspect lifetime UB
  before anything else.
- **Gotcha (cost a real bug):** widget state that stores an *index*
  (Sun2Set's month dropdown) must convert back to the domain value
  (`month = index + 1`) everywhere it's read — the miss showed up as DST
  months silently decrementing one step per app restart.
- **Gotcha:** MSVC needs `/utf-8` (set in build_native.ps1) or non-ASCII
  characters typed raw in string literals (em dashes, ellipses) become
  mojibake in window/dialog titles; the `\xNN`-escaped UTF-8 in the
  games' literals never depended on it.
- **Gotcha:** GDI does no font fallback (Tk does): Consolas lacks ◀▶
  U+25C0/25B6, so the C++ panels use the DOS-era pointers ◄► U+25C4/25BA
  that Consolas (and Menlo) do carry — boxes in a screenshot mean a
  missing glyph, not a rendering bug.
- **Gotchas (Windows):** `windows.h` defines `far`/`near` and an `RGB`
  macro — the core compiles before that include, but don't reuse those
  names (`farthest`; `#undef RGB` sits after the include). Win32
  `PlaySound` *can* do `SND_MEMORY|SND_ASYNC` (unlike winsound) because
  the WAV buffers live for the program's lifetime. The GUI-subsystem
  selftest prints fine when stdout is redirected (the CRT inherits the
  pipe) and only calls `AttachConsole` when run bare; MyPocketTanks also
  has a `--dumpconfig` diagnostic. MyTetris suppresses OS key
  auto-repeat (Python tracks a held-set); MyPocketTanks deliberately
  does NOT (Tk auto-repeat is what makes held angle/power keys work).
- **Gotchas (macOS):** Cocoa modifiers (Shift/Ctrl) arrive via
  `flagsChanged:`, not `keyDown:` (MyTetris needs that; MyPocketTanks
  only reads the shift flag). The terrain pixel buffer needs the
  double-flip CTM dance to draw upright in a flipped NSView.
- **Test pattern:** drive GUI states by PostMessage'ing WM_KEYDOWN /
  WM_LBUTTONDOWN and capture via `PrintWindow(hwnd, dc, 2)` — works even
  with the display asleep (CopyFromScreen returns black then).

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

## Friend packages

`package_games.py` zips each app + icons + license + its
`packaging/README-<Name>.txt` guide + the two installers into
`dist/<Name>.zip` (git-ignored) — all three apps by default, or the names
given on the command line.

- **One installer source pair serves all apps.** `INSTALL-Windows.bat` /
  `Install-macOS.command` are written (and E2E-verified) for MyPocketTanks;
  the other apps' copies are derived at zip time by token substitution:
  `MyPocketTanks` → name (also rewrites `<Name>.py` — every app is
  `<Name>.py`), `mypockettanks` → icon base, and the shortcut-description
  tagline (`GAMES` dict). A leftover-token check fails the build if
  app-specific wording bypasses the tokens — route new game-specific text
  through them (or `$NAME` in the .command).
- The installers target the **system** Python (`py -3`/`python` on Windows,
  a tkinter-capable `python3` on macOS) — a friend's machine has no venv.
  Both guide a python.org install when nothing suitable is found, and both
  refuse to run from inside an unextracted zip.
- Platform details are enforced **at zip time**, not trusted from the
  working tree: the `.command` is normalized to LF (a CR after
  `#!/bin/bash` breaks it) and stored with the exec bit via a unix `ZipInfo`
  (`create_system=3`) so macOS restores `chmod +x`; the `.bat` is CRLF
  (cmd.exe mis-parses labels in LF-only batch files); friend-facing text
  must stay plain ASCII (enforced — cmd's OEM codepage garbles the rest).
  `.gitattributes` pins the same endings for checkouts.
- To test the Windows path end-to-end: extract a zip to a temp dir and run
  `INSTALL-Windows.bat /nolaunch` — but **back up the real Desktop
  `<Name>.lnk` first**; the installer overwrites it (that's its job).
  Selftest/launch the extracted copy with the *system* Python, not the venv.
- **Don't email the zips:** Gmail blocks archives containing `.bat` (scans
  inside, nesting/renaming doesn't evade it). Share a Drive/OneDrive link
  set to "Anyone with the link", or a GitHub release.

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
| `MyTetris.cpp` | The native C++ port of MyTetris (see *Native C++ ports*); same `--selftest`. |
| `MyPocketTanks.cpp` | The native C++ port of MyPocketTanks; same `--selftest` plus a `--dumpconfig` diagnostic. |
| `Sun2Set.cpp` | The native C++ port of Sun2Set (WMM block spliced from the .py — see *Native C++ ports*); same `--selftest`. |
| `build_native.ps1` | **Windows** — builds `build\<App>.exe` for any of the native ports (MSVC via vswhere, static CRT, embedded icon). |
| `build_native.command` | **macOS** — builds `build/<App>` + self-contained `build/<App>.app` (clang++ as Objective-C++). |
| `MyPocketTanks.py` | The artillery duel (and its `--selftest`). |
| `Sun2Set.py` | The sunrise/sunset almanac (and its `--selftest`). |
| `make_tetris_icon.py` | Generates `mytetris.ico` (tidy falling-T scene) by writing the ICO/BMP bytes directly (no Pillow). |
| `make_pockettanks_icon.py` | Generates `mypockettanks.ico` (tank + shell arc + explosion scene). Reuses `make_tetris_icon.build_ico()`; no Pillow. |
| `make_sun2set_icon.py` | Generates `sun2set.ico` (sunset over the sea + the sun's dotted day-path arc). Reuses `make_tetris_icon.build_ico()`; no Pillow. |
| `make_troll_icon.py` | Generates `mytetris_troll.ico` — the funny "Troll Piece" as a multi-resolution Windows ICO (256px PNG + 48/32/16 BMP). Reuses `make_tetris_icon_mac.build_scene()` and downsamples; no Pillow. |
| `make_tetris_icon_mac.py` | Generates `mytetris.png` (the macOS "Troll Piece" icon) by writing the PNG bytes directly (zlib + chunks, no Pillow). |
| `make_pockettanks_icon_mac.py` | Generates `mypockettanks.png` (macOS, 1024px) — the same artillery-duel scene as the ICO, rendered natively; reuses `make_tetris_icon_mac`'s rasterizer/PNG writer. |
| `make_sun2set_icon_mac.py` | Generates `sun2set.png` (macOS, 1024px) — the same sunset scene as the ICO, rendered natively; reuses `make_tetris_icon_mac`'s rasterizer/PNG writer. |
| `create_shortcut.ps1` | **Windows** Desktop `.lnk`; parameterized `-Script` / `-Icon` / `-Name`, defaults to MyTetris. `-Native` targets the compiled `build\<App>.exe` (embedded icon, no Python), auto-named "<App> (Native)" so it sits beside the Python shortcut. |
| `create_shortcut.command` | **macOS**: `sips`+`iconutil` build the `.icns`, then assemble a clickable `.app` on the Desktop. Args: name, script, icon PNG (defaults = MyTetris). Bakes in the found `python3` (Finder gives apps a minimal PATH) and absolute project paths. `--native <App>` instead points the launcher at the compiled `build/<App>` binary (no Python), auto-named "<App> (Native)" beside the Python shortcut. |
| `package_games.py` | Builds `dist/<Name>.zip` friend-shareable bundles for all three apps (see *Friend packages* above); templated installer sources live in `packaging/`. |
