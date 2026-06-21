# CLAUDE.md

Guidance for working in this repository. See `README.md` for user-facing docs.

## Project

`PythonGUIgames` — a collection of Python **Tkinter** GUI games. The main app is
`MyTetris.py`, an accurate, guideline-faithful Tetris clone.

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
```

`--selftest` drives all difficulties for thousands of frames and asserts no
exceptions. For mechanics that random play rarely triggers (T-spins, B2B,
persistence), write a short throwaway `_*.py` script that **constructs** the exact
state and asserts on it, run it, then delete it. Validate GUI/visuals by launching
with `pythonw.exe` and capturing a screenshot via .NET `CopyFromScreen`, then
`Read` the PNG. Always delete temp scripts/PNGs afterward (none should be committed).

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
- **Persistence** lives in `%APPDATA%\MyTetris\`: `highscores.json` (top-10 per
  difficulty) and `config.json` (window position). Both load defensively
  (missing/corrupt → empty) and are gated by the `persist` flag so tests with
  `persist=False` never write real files. User data — never committed.

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
| `make_tetris_icon.py` | Generates `mytetris.ico` by writing the ICO/BMP bytes directly (no Pillow). |
| `make_tetris_icon_mac.py` | Generates `mytetris.png` (the macOS "Troll Piece" icon) by writing the PNG bytes directly (zlib + chunks, no Pillow). |
| `create_shortcut.ps1` | **Windows** Desktop `.lnk`; parameterized `-Script` / `-Icon` / `-Name`, defaults to MyTetris. |
| `create_shortcut.command` | **macOS**: `sips`+`iconutil` build `mytetris.icns`, then assemble a clickable `.app` on the Desktop. Bakes in the found `python3` (Finder gives apps a minimal PATH) and absolute project paths. |
