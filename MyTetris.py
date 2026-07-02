"""MyTetris.py — an accurate Tkinter clone of the classic game TETRIS.

Guideline mechanics:
  * 10 x 20 playfield, 7 tetrominoes with their standard colors
  * SRS (Super Rotation System) rotation with wall kicks
  * 7-bag randomizer, DAS (Delayed Auto Shift), lock delay
  * Soft drop, hard drop, ghost piece, hold, next-3 preview queue
  * Line-clear scoring, leveling, and increasing gravity
  * T-spin detection (Full / Mini) with bonus scoring
  * Back-to-back bonus (1.5x) for chained Tetrises / T-spins

Extras:
  * Start menu with selectable difficulty (Easy / Normal / Hard)
  * Adjustable, persisted "speed ramp" — how fast gravity climbs per level
  * Synthesized sound effects (stdlib; Windows winsound / macOS afplay)
  * High-score persistence per difficulty (JSON in %APPDATA%\\MyTetris)
  * Pause -> "return to menu?" confirmation

Controls:
  Left / Right .... move          Up or X ......... rotate clockwise
  Down ............ soft drop      Z or Ctrl ....... rotate counter-clockwise
  Space ........... hard drop      C or Shift ...... hold piece
  P ............... pause          M ............... mute
  Esc ............. menu (asks)    R ............... restart / retry

Run:  python MyTetris.py           Self-test (headless logic):  python MyTetris.py --selftest
"""

import atexit
import io
import json
import math
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import wave
import tkinter as tk

# ----------------------------------------------------------------------------
# Board / rendering constants
# ----------------------------------------------------------------------------
COLS, ROWS = 10, 20
CELL = 30
PCELL = 26          # hold/legacy preview cell
PNEXT = 18          # next-queue / hold preview cell
FRAME_MS = 16       # ~60 FPS fixed timestep

DAS_DELAY = 150
DAS_REPEAT = 40
SOFT_DROP_INTERVAL = 30
MAX_LOCK_RESETS = 15
TOAST_FRAMES = 80   # how long a "TETRIS" / "T-SPIN" banner lingers

BG = "#0b0b12"
BG_CELL = "#15151f"
GRID_LINE = "#22222e"
PANEL_CELL = "#15151f"
TEXT = "#e6e6ec"
SUBTEXT = "#9a9ab0"
GOLD = "#ffd91a"

COLORS = {
    "I": "#19d3da", "J": "#3f63e0", "L": "#ff9f1a", "O": "#ffd91a",
    "S": "#2ecc55", "T": "#a64ddb", "Z": "#ef4444",
}

# Difficulty presets. Per-difficulty knobs:
#   start_level  - the level the game begins at
#   gravity_mult - flat multiplier on fall time (>1 = slower, <1 = faster)
#   lock_delay   - ms a grounded piece waits before it locks
#   score_mult   - score multiplier for the mode
#   blurb        - one-line description shown in the start menu
# How fast gravity ramps up per level is a separate, player-adjustable setting
# (see SPEED_STEP_* below and the "SPEED RAMP" row on the start menu).
DIFFICULTIES = {
    "Easy":   {"start_level": 1, "gravity_mult": 1.6, "lock_delay": 700,
               "score_mult": 1.0, "blurb": "Slower fall, gentle start"},
    "Normal": {"start_level": 1, "gravity_mult": 1.0, "lock_delay": 500,
               "score_mult": 1.0, "blurb": "Standard fall speed"},
    "Hard":   {"start_level": 5, "gravity_mult": 0.55, "lock_delay": 350,
               "score_mult": 1.25, "blurb": "Starts at level 5, x1.25"},
}
DIFFICULTY_NAMES = list(DIFFICULTIES)

# Player-adjustable gravity ramp — the start-menu "SPEED RAMP" row, persisted in
# config.json and shared by every difficulty. It scales how far each level moves
# along the classic guideline curve: 1.0 = standard Tetris ramp, and smaller
# values mean a gentler per-level speed-up and much longer games.
SPEED_STEP_MIN = 0.20
SPEED_STEP_MAX = 1.00
SPEED_STEP_DEFAULT = 0.50
SPEED_STEP_INCREMENT = 0.05

BASE = {
    "I": [[0, 0, 0, 0], [1, 1, 1, 1], [0, 0, 0, 0], [0, 0, 0, 0]],
    "J": [[1, 0, 0], [1, 1, 1], [0, 0, 0]],
    "L": [[0, 0, 1], [1, 1, 1], [0, 0, 0]],
    "O": [[1, 1], [1, 1]],
    "S": [[0, 1, 1], [1, 1, 0], [0, 0, 0]],
    "T": [[0, 1, 0], [1, 1, 1], [0, 0, 0]],
    "Z": [[1, 1, 0], [0, 1, 1], [0, 0, 0]],
}


def _rotate_cw(matrix):
    n = len(matrix)
    return [[matrix[n - 1 - j][i] for j in range(n)] for i in range(n)]


def _cells(matrix):
    return [(c, r) for r, row in enumerate(matrix)
            for c, val in enumerate(row) if val]


PIECE_CELLS = {}
for _name, _matrix in BASE.items():
    _states, _cur = [], _matrix
    for _ in range(4):
        _states.append(_cells(_cur))
        _cur = _rotate_cw(_cur)
    PIECE_CELLS[_name] = _states

KICKS_JLSTZ = {
    (0, 1): [(0, 0), (-1, 0), (-1, -1), (0, 2), (-1, 2)],
    (1, 0): [(0, 0), (1, 0), (1, 1), (0, -2), (1, -2)],
    (1, 2): [(0, 0), (1, 0), (1, 1), (0, -2), (1, -2)],
    (2, 1): [(0, 0), (-1, 0), (-1, -1), (0, 2), (-1, 2)],
    (2, 3): [(0, 0), (1, 0), (1, -1), (0, 2), (1, 2)],
    (3, 2): [(0, 0), (-1, 0), (-1, 1), (0, -2), (-1, -2)],
    (3, 0): [(0, 0), (-1, 0), (-1, 1), (0, -2), (-1, -2)],
    (0, 3): [(0, 0), (1, 0), (1, -1), (0, 2), (1, 2)],
}
KICKS_I = {
    (0, 1): [(0, 0), (-2, 0), (1, 0), (-2, 1), (1, -2)],
    (1, 0): [(0, 0), (2, 0), (-1, 0), (2, -1), (-1, 2)],
    (1, 2): [(0, 0), (-1, 0), (2, 0), (-1, -2), (2, 1)],
    (2, 1): [(0, 0), (1, 0), (-2, 0), (1, 2), (-2, -1)],
    (2, 3): [(0, 0), (2, 0), (-1, 0), (2, -1), (-1, 2)],
    (3, 2): [(0, 0), (-2, 0), (1, 0), (-2, 1), (1, -2)],
    (3, 0): [(0, 0), (1, 0), (-2, 0), (1, 2), (-2, -1)],
    (0, 3): [(0, 0), (-1, 0), (2, 0), (-1, -2), (2, 1)],
}

# Front-corner pairs of the T's 3x3 box, by rotation (corner keys A,B,C,D =
# top-left, top-right, bottom-left, bottom-right). "Front" = pointing side.
T_FRONT_CORNERS = {0: ("A", "B"), 1: ("B", "D"), 2: ("C", "D"), 3: ("A", "C")}


def _clear_base(cleared, tspin):
    """Base points (before level/difficulty/B2B multipliers) for a lock."""
    if tspin == "full":
        return {0: 400, 1: 800, 2: 1200, 3: 1600}.get(cleared, 0)
    if tspin == "mini":
        return {0: 100, 1: 200, 2: 400}.get(cleared, 0)
    return {1: 100, 2: 300, 3: 500, 4: 800}.get(cleared, 0)


def _adjust(hex_color, factor):
    h = hex_color.lstrip("#")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    r = max(0, min(255, int(r * factor)))
    g = max(0, min(255, int(g * factor)))
    b = max(0, min(255, int(b * factor)))
    return f"#{r:02x}{g:02x}{b:02x}"


# ----------------------------------------------------------------------------
# Sound: synthesize small WAV buffers and play them per-platform. On Windows we
# play the buffers straight from memory with winsound (no files). macOS has no
# stdlib audio API, so we write each buffer to a temp WAV once and play it with
# the system `afplay` tool. Either way playback runs on a daemon thread so it
# never blocks the game loop.
# ----------------------------------------------------------------------------
SAMPLE_RATE = 22050


def _tone(freq, ms, vol=0.4, wave_type="square"):
    n = int(SAMPLE_RATE * ms / 1000)
    attack = max(1, int(0.004 * SAMPLE_RATE))
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        if wave_type == "square":
            base = 1.0 if math.sin(2 * math.pi * freq * t) >= 0 else -1.0
        else:
            base = math.sin(2 * math.pi * freq * t)
        env = min(1.0, i / attack) * (1.0 - i / n)
        samples.append(int(max(-1.0, min(1.0, base * env * vol)) * 32767))
    return samples


def _seq(*tone_lists):
    out = []
    for t in tone_lists:
        out.extend(t)
    return out


def _wav(samples):
    buf = io.BytesIO()
    w = wave.open(buf, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SAMPLE_RATE)
    w.writeframes(b"".join(struct.pack("<h", s) for s in samples))
    w.close()
    return buf.getvalue()


def _sound_specs():
    """name -> WAV bytes for every effect (shared by both playback backends)."""
    return {
        "blip": _wav(_tone(440, 30, 0.25)),
        "start": _wav(_seq(_tone(523, 60, 0.4), _tone(784, 110, 0.4))),
        "rotate": _wav(_tone(600, 38, 0.3)),
        "hold": _wav(_tone(380, 45, 0.3)),
        "lock": _wav(_tone(200, 55, 0.35, "sine")),
        "harddrop": _wav(_tone(110, 70, 0.5, "sine")),
        "lineclear": _wav(_seq(_tone(523, 55), _tone(659, 55),
                               _tone(784, 70))),
        "tetris": _wav(_seq(_tone(523, 60, 0.5), _tone(659, 60, 0.5),
                            _tone(784, 60, 0.5), _tone(1047, 120, 0.5))),
        "tspin": _wav(_seq(_tone(880, 45, 0.45), _tone(1175, 45, 0.45),
                           _tone(1568, 110, 0.45))),
        "levelup": _wav(_seq(_tone(659, 50), _tone(880, 50),
                             _tone(1175, 100))),
        "gameover": _wav(_seq(_tone(440, 130, 0.4, "sine"),
                              _tone(330, 130, 0.4, "sine"),
                              _tone(247, 130, 0.4, "sine"),
                              _tone(165, 220, 0.4, "sine"))),
    }


class SoundManager:
    def __init__(self, enable=True):
        self.muted = False
        self.enabled = False
        self._ws = None         # winsound module (Windows backend)
        self._afplay = None     # path to afplay (macOS backend)
        self._cache = {}        # name -> WAV bytes (Windows: played from memory)
        self._files = {}        # name -> temp WAV path (macOS: played by afplay)
        if not enable:
            return
        if sys.platform == "win32":
            try:
                import winsound
                self._ws = winsound
            except Exception:
                return
        elif sys.platform == "darwin" and os.path.exists("/usr/bin/afplay"):
            self._afplay = "/usr/bin/afplay"
        else:
            return                                  # no supported backend
        self.enabled = True
        self._build()
        if self._afplay:
            # afplay exits 0 even into an OS-muted output, which once looked
            # exactly like broken sound — surface the mute instead of hiding it.
            threading.Thread(target=self._warn_if_os_muted, daemon=True).start()

    def _build(self):
        self._cache = _sound_specs()
        if self._afplay:
            # macOS: afplay needs files, so materialize each WAV once into a
            # private temp dir and replay those paths. Cleaned up at exit.
            tmpdir = tempfile.mkdtemp(prefix="mytetris-snd-")
            for name, data in self._cache.items():
                path = os.path.join(tmpdir, name + ".wav")
                with open(path, "wb") as f:
                    f.write(data)
                self._files[name] = path
            atexit.register(shutil.rmtree, tmpdir, ignore_errors=True)

    def play(self, name):
        if not self.enabled or self.muted:
            return
        if self._ws is not None:
            data = self._cache.get(name)
            if data is not None:
                # winsound forbids SND_MEMORY | SND_ASYNC ("Cannot play
                # asynchronously from memory"), so play the in-memory WAV
                # *synchronously* on a daemon thread to keep audio off the loop.
                threading.Thread(target=self._play_winsound, args=(data,),
                                 daemon=True).start()
        elif self._afplay is not None:
            path = self._files.get(name)
            if path is not None:
                threading.Thread(target=self._play_afplay, args=(path,),
                                 daemon=True).start()

    def _play_winsound(self, data):
        try:
            self._ws.PlaySound(data, self._ws.SND_MEMORY)
        except Exception:
            pass

    def _play_afplay(self, path):
        try:
            subprocess.run([self._afplay, path],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception:
            pass

    def _warn_if_os_muted(self):
        """Warn on stderr when macOS output is muted, so silence isn't a mystery."""
        try:
            r = subprocess.run(
                ["/usr/bin/osascript", "-e",
                 "output muted of (get volume settings)"],
                capture_output=True, text=True, timeout=5)
            if r.stdout.strip() == "true":
                print("MyTetris: sound is on, but the macOS output device is "
                      "muted — unmute to hear anything (F10, or: osascript "
                      "-e 'set volume without output muted')", file=sys.stderr)
        except Exception:
            pass                        # best-effort diagnostic; never fatal

    def toggle_mute(self):
        self.muted = not self.muted
        if self.muted and self._ws is not None:
            try:
                self._ws.PlaySound(None, self._ws.SND_PURGE)  # cut Windows audio
            except Exception:
                pass


# ----------------------------------------------------------------------------
# High-score persistence (JSON in the user's app-data directory).
# ----------------------------------------------------------------------------
def _scores_path():
    base = os.environ.get("APPDATA") or os.path.expanduser("~")
    return os.path.join(base, "MyTetris", "highscores.json")


def load_scores():
    try:
        with open(_scores_path(), "r", encoding="utf-8") as f:
            data = json.load(f)
        out = {}
        for name in DIFFICULTY_NAMES:
            entries = data.get(name, [])
            out[name] = [e for e in entries
                         if isinstance(e, dict) and "score" in e][:10]
        return out
    except Exception:
        return {name: [] for name in DIFFICULTY_NAMES}


def save_scores(scores):
    try:
        path = _scores_path()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(scores, f, indent=2)
    except Exception:
        pass


def _config_path():
    base = os.environ.get("APPDATA") or os.path.expanduser("~")
    return os.path.join(base, "MyTetris", "config.json")


def load_config():
    try:
        with open(_config_path(), "r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def save_config(config):
    try:
        path = _config_path()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=2)
    except Exception:
        pass


class TetrisGame:
    def __init__(self, root, enable_sound=True, persist=True):
        self.root = root
        self.persist = persist
        self.sound = SoundManager(enable=enable_sound)
        self.scores = load_scores()
        self.config = load_config()
        self.speed_step = self._load_speed_step()
        self.difficulty = "Normal"
        self.diff = DIFFICULTIES[self.difficulty]
        self.board = [[None] * COLS for _ in range(ROWS)]
        self.queue = []
        self.hold_piece = None
        self.piece = None
        self.score = self.lines = 0
        self.level = 1
        self.new_best = False
        self.b2b = False
        self.confirm_menu = False
        self.last_action_was_rotate = False
        self.last_kick_index = 0
        self._toast_text = ""
        self._toast_frames = 0
        self.held = set()
        self.state = "menu"
        self._build_ui()
        self._restore_window_position()
        self.loop_id = self.root.after(FRAME_MS, self.tick)

    # ----- piece geometry --------------------------------------------------
    @staticmethod
    def cells_of(piece, rotation):
        return PIECE_CELLS[piece][rotation % 4]

    def current_cells(self):
        return self.cells_of(self.piece, self.rot)

    def valid_at(self, piece, rotation, px, py):
        for cx, cy in self.cells_of(piece, rotation):
            x, y = px + cx, py + cy
            if x < 0 or x >= COLS or y >= ROWS:
                return False
            if y >= 0 and self.board[y][x] is not None:
                return False
        return True

    def grounded(self):
        return not self.valid_at(self.piece, self.rot, self.px, self.py + 1)

    # ----- spawning / 7-bag ------------------------------------------------
    def _fill_queue(self):
        while len(self.queue) < 5:
            if not self.bag:
                self.bag = list("IJLOSTZ")
                random.shuffle(self.bag)
            self.queue.append(self.bag.pop())

    def set_current(self, piece):
        self.piece = piece
        self.rot = 0
        self.px = 4 if piece == "O" else 3
        self.py = 0
        self.fall_charge = 0
        self.lock_counter = 0
        self.lock_resets = 0
        self.last_action_was_rotate = False
        self.last_kick_index = 0
        if not self.valid_at(self.piece, self.rot, self.px, self.py):
            self.state = "gameover"

    def spawn_from_queue(self):
        piece = self.queue.pop(0)
        self._fill_queue()
        self.set_current(piece)

    # ----- locking, T-spin, line clears, scoring ---------------------------
    def _lock(self, sound_name):
        self.sound.play(sound_name)
        self.lock_piece()

    def _corner_filled(self, x, y):
        """A corner counts as filled if it's a wall/floor/ceiling or a block."""
        if x < 0 or x >= COLS or y < 0 or y >= ROWS:
            return True
        return self.board[y][x] is not None

    def _detect_tspin(self):
        """Return 'full', 'mini', or None for the piece about to lock."""
        if self.piece != "T" or not self.last_action_was_rotate:
            return None
        corners = {
            "A": self._corner_filled(self.px + 0, self.py + 0),
            "B": self._corner_filled(self.px + 2, self.py + 0),
            "C": self._corner_filled(self.px + 0, self.py + 2),
            "D": self._corner_filled(self.px + 2, self.py + 2),
        }
        if sum(corners.values()) < 3:
            return None
        f1, f2 = T_FRONT_CORNERS[self.rot]
        full = corners[f1] and corners[f2]
        if self.last_kick_index >= 4:   # the big "TST"-style kick is always full
            full = True
        return "full" if full else "mini"

    def lock_piece(self):
        tspin = self._detect_tspin()
        topout = False
        for cx, cy in self.current_cells():
            x, y = self.px + cx, self.py + cy
            if y < 0:
                topout = True
                continue
            if 0 <= x < COLS and 0 <= y < ROWS:
                self.board[y][x] = COLORS[self.piece]
        cleared = self._remove_full_rows()
        self._apply_score(cleared, tspin)
        self.can_hold = True
        if topout:
            self.state = "gameover"
            return
        self.spawn_from_queue()

    def _remove_full_rows(self):
        kept = [row for row in self.board if any(c is None for c in row)]
        cleared = ROWS - len(kept)
        for _ in range(cleared):
            kept.insert(0, [None] * COLS)
        self.board = kept
        return cleared

    def _apply_score(self, cleared, tspin):
        base = _clear_base(cleared, tspin)
        b2b_bonus = 1.0
        b2b_applied = False
        if cleared > 0:
            difficult = (cleared == 4) or (tspin is not None)
            if difficult:
                if self.b2b:
                    b2b_bonus = 1.5
                    b2b_applied = True
                self.b2b = True
            else:
                self.b2b = False
        self.score += int(base * self.level * self.diff["score_mult"] * b2b_bonus)

        leveled = False
        if cleared:
            self.lines += cleared
            old_level = self.level
            self.level = self.diff["start_level"] + self.lines // 10
            leveled = self.level > old_level

        label = self._clear_label(cleared, tspin, b2b_applied)
        if label:
            self._toast_text = label
            self._toast_frames = TOAST_FRAMES

        if leveled:
            self.sound.play("levelup")
        elif tspin and cleared > 0:
            self.sound.play("tetris")
        elif tspin:
            self.sound.play("tspin")
        elif cleared >= 4:
            self.sound.play("tetris")
        elif cleared > 0:
            self.sound.play("lineclear")
        else:
            self.sound.play("lock")

    @staticmethod
    def _clear_label(cleared, tspin, b2b):
        names = {1: "SINGLE", 2: "DOUBLE", 3: "TRIPLE", 4: "TETRIS"}
        if tspin == "full":
            text = "T-SPIN" + (" " + names[cleared] if cleared else "")
        elif tspin == "mini":
            text = "T-SPIN MINI" + (" " + names[cleared] if cleared else "")
        elif cleared == 4:
            text = "TETRIS"
        else:
            text = ""
        if text and b2b:
            text = "B2B " + text
        return text

    # ----- movement / rotation ---------------------------------------------
    def try_move(self, dx, dy):
        if not self.valid_at(self.piece, self.rot, self.px + dx, self.py + dy):
            return False
        self.px += dx
        self.py += dy
        self.last_action_was_rotate = False
        if dy > 0:
            self.lock_counter = 0
            self.lock_resets = 0
        elif dx != 0 and self.grounded():
            self._reset_lock_on_action()
        return True

    def rotate(self, direction):
        if self.state != "playing" or self.piece == "O":
            return False
        new_rot = (self.rot + direction) % 4
        table = KICKS_I if self.piece == "I" else KICKS_JLSTZ
        for i, (dx, dy) in enumerate(table[(self.rot, new_rot)]):
            if self.valid_at(self.piece, new_rot, self.px + dx, self.py + dy):
                self.rot = new_rot
                self.px += dx
                self.py += dy
                self.last_action_was_rotate = True
                self.last_kick_index = i
                if self.grounded():
                    self._reset_lock_on_action()
                self.sound.play("rotate")
                return True
        return False

    def _reset_lock_on_action(self):
        if self.lock_resets < MAX_LOCK_RESETS:
            self.lock_counter = 0
            self.lock_resets += 1

    def hard_drop(self):
        dist = 0
        while self.try_move(0, 1):
            dist += 1
        self.score += 2 * dist
        self._lock("harddrop")

    def hold(self):
        if not self.can_hold or self.state != "playing":
            return
        current = self.piece
        if self.hold_piece is None:
            self.hold_piece = current
            self.spawn_from_queue()
        else:
            incoming = self.hold_piece
            self.hold_piece = current
            self.set_current(incoming)
        self.can_hold = False
        self.sound.play("hold")

    # ----- per-frame update -------------------------------------------------
    def gravity_interval(self):
        # Classic guideline gravity: time-per-cell = base^(L-1) seconds, with
        # base = 0.8 - (L-1)*0.007. self.speed_step (the player's "SPEED RAMP"
        # setting) stretches the ramp by scaling how far each real level advances
        # along that curve, so a smaller value means a gentler speed-up and
        # longer games (1.0 = standard Tetris).
        eff = 1 + (self.level - 1) * self.speed_step   # effective level on curve
        base = 0.8 - (eff - 1) * 0.007
        if base <= 0:
            base = 0.01
        ms = (base ** (eff - 1)) * 1000 * self.diff["gravity_mult"]
        return max(ms, FRAME_MS)

    def update(self, dt):
        self._update_horizontal(dt)
        self._update_gravity(dt)
        if self.state != "playing":
            return
        if self.grounded():
            self.lock_counter += dt
            if self.lock_counter >= self.diff["lock_delay"]:
                self._lock("lock")
        else:
            self.lock_counter = 0

    def _update_horizontal(self, dt):
        left, right = "Left" in self.held, "Right" in self.held
        if left and right:
            direction = self.last_dir
        elif left:
            direction = -1
        elif right:
            direction = 1
        else:
            direction = 0

        if direction == 0:
            self.das_dir = 0
            self.das_charge = 0
            self.das_active = False
            return
        if direction != self.das_dir:
            self.das_dir = direction
            self.das_charge = 0
            self.das_active = False
            return
        self.das_charge += dt
        if not self.das_active:
            if self.das_charge >= DAS_DELAY:
                self.das_active = True
                self.das_charge = 0
                self.try_move(direction, 0)
        else:
            while self.das_charge >= DAS_REPEAT:
                self.das_charge -= DAS_REPEAT
                if not self.try_move(direction, 0):
                    break

    def _update_gravity(self, dt):
        soft = "Down" in self.held
        interval = max(SOFT_DROP_INTERVAL if soft else self.gravity_interval(), 1)
        self.fall_charge += dt
        while self.fall_charge >= interval:
            self.fall_charge -= interval
            if self.try_move(0, 1):
                if soft:
                    self.score += 1
            else:
                self.fall_charge = 0
                break

    def tick(self):
        if self.state == "playing":
            self.update(FRAME_MS)
            if self._toast_frames > 0:
                self._toast_frames -= 1
        if self.state == "gameover" and not self._gameover_handled:
            self._gameover_handled = True
            self.sound.play("gameover")
            self._record_score()
        self.render()
        self.loop_id = self.root.after(FRAME_MS, self.tick)

    # ----- scores -----------------------------------------------------------
    def _best(self, difficulty):
        entries = self.scores.get(difficulty, [])
        return entries[0]["score"] if entries else 0

    def _record_score(self):
        entry = {"score": self.score, "lines": self.lines, "level": self.level}
        entries = self.scores.setdefault(self.difficulty, [])
        entries.append(entry)
        entries.sort(key=lambda e: e["score"], reverse=True)
        del entries[10:]
        self.new_best = self.score > 0 and entries[0] is entry
        if self.persist:
            save_scores(self.scores)

    # ----- window position persistence -------------------------------------
    def _restore_window_position(self):
        win = self.config.get("window")
        if not isinstance(win, dict):
            return
        try:
            x, y = int(win["x"]), int(win["y"])
        except (KeyError, ValueError, TypeError):
            return
        self.root.update_idletasks()
        sw = self.root.winfo_screenwidth()
        sh = self.root.winfo_screenheight()
        w = self.root.winfo_reqwidth() or 1
        h = self.root.winfo_reqheight() or 1
        # Clamp so the whole window stays on the primary screen (and grabbable).
        x = max(0, min(x, max(0, sw - w)))
        y = max(0, min(y, max(0, sh - h)))
        self.root.geometry(f"+{x}+{y}")

    def _save_window_position(self):
        if not self.persist:
            return
        # Parse the position out of geometry() (not winfo_x/y) so restoring it
        # verbatim doesn't make the window creep by the title-bar height.
        try:
            m = re.match(r"(\d+)x(\d+)([+-]\d+)([+-]\d+)", self.root.geometry())
            if m:
                self.config["window"] = {"x": int(m.group(3)), "y": int(m.group(4))}
                save_config(self.config)
        except Exception:
            pass

    # ----- speed-ramp setting persistence ----------------------------------
    def _load_speed_step(self):
        try:
            v = round(float(self.config.get("speed_step")), 2)
        except (TypeError, ValueError):
            return SPEED_STEP_DEFAULT
        return min(SPEED_STEP_MAX, max(SPEED_STEP_MIN, v))

    def _save_speed_step(self):
        if not self.persist:
            return
        self.config["speed_step"] = self.speed_step
        save_config(self.config)

    def _on_close(self):
        self._save_window_position()
        self.root.destroy()

    # ----- input ------------------------------------------------------------
    def on_key_press(self, event):
        key = event.keysym
        if key in self.held:
            return
        self.held.add(key)
        self._handle_press(key, event.char)

    def on_key_release(self, event):
        self.held.discard(event.keysym)

    def _handle_press(self, key, char=""):
        if key in ("m", "M"):
            self.sound.toggle_mute()
            return
        if self.state == "menu":
            self._menu_key(key, char)
            return
        if self.state == "gameover":
            if key in ("Return", "KP_Enter"):
                self.state = "menu"
            elif key in ("r", "R"):
                self.start_game(self.difficulty)
            return
        # playing / paused (with optional confirm-menu modal)
        if self.confirm_menu:
            if key in ("y", "Y", "Return", "KP_Enter"):
                self.confirm_menu = False
                self.state = "menu"
            elif key in ("n", "N", "Escape"):
                self.confirm_menu = False
                self.state = "paused"
            return
        if key in ("p", "P"):
            if self.state == "playing":
                self.state = "paused"
            elif self.state == "paused":
                self.state = "playing"
            return
        if key == "Escape":
            self.confirm_menu = True       # ask before abandoning the game
            self.state = "paused"
            return
        if key in ("r", "R"):
            self.start_game(self.difficulty)
            return
        if self.state != "playing":
            return
        if key == "Left":
            self.last_dir = -1
            self.try_move(-1, 0)
        elif key == "Right":
            self.last_dir = 1
            self.try_move(1, 0)
        elif key == "Down":
            if self.try_move(0, 1):
                self.score += 1
        elif key in ("Up", "x", "X"):
            self.rotate(1)
        elif key in ("z", "Z", "Control_L", "Control_R"):
            self.rotate(-1)
        elif key == "space":
            self.hard_drop()
        elif key in ("c", "C", "Shift_L", "Shift_R"):
            self.hold()

    def _menu_key(self, key, char=""):
        idx = DIFFICULTY_NAMES.index(self.difficulty)
        # Speed keys match the typed character too, not just the keysym: on macOS
        # Tk doesn't always report "bracketleft"/"bracketright" for [ and ].
        if key in ("Up", "Left"):
            self.difficulty = DIFFICULTY_NAMES[(idx - 1) % len(DIFFICULTY_NAMES)]
            self.sound.play("blip")
        elif key in ("Down", "Right"):
            self.difficulty = DIFFICULTY_NAMES[(idx + 1) % len(DIFFICULTY_NAMES)]
            self.sound.play("blip")
        elif key in ("bracketleft", "minus", "KP_Subtract") or char in ("[", "-"):
            self._adjust_speed_step(-SPEED_STEP_INCREMENT)   # gentler / longer
        elif key in ("bracketright", "equal", "plus", "KP_Add") or char in ("]", "=", "+"):
            self._adjust_speed_step(+SPEED_STEP_INCREMENT)   # steeper / classic
        elif key in ("Return", "KP_Enter", "space"):
            self.start_game(self.difficulty)
        elif key == "Escape":
            self._on_close()

    def _adjust_speed_step(self, delta):
        new = round(min(SPEED_STEP_MAX,
                        max(SPEED_STEP_MIN, self.speed_step + delta)), 2)
        if new == self.speed_step:
            return                          # already at a limit — nothing to do
        self.speed_step = new
        self.sound.play("blip")
        self._save_speed_step()             # persist the choice immediately

    # ----- lifecycle --------------------------------------------------------
    def start_game(self, difficulty):
        self.difficulty = difficulty
        self.diff = DIFFICULTIES[difficulty]
        self.board = [[None] * COLS for _ in range(ROWS)]
        self.score = 0
        self.lines = 0
        self.level = self.diff["start_level"]
        self.hold_piece = None
        self.can_hold = True
        self.bag = []
        self.queue = []
        self._fill_queue()
        self.held = set()
        self.last_dir = 0
        self.das_dir = 0
        self.das_charge = 0
        self.das_active = False
        self.fall_charge = 0
        self.lock_counter = 0
        self.lock_resets = 0
        self.last_action_was_rotate = False
        self.last_kick_index = 0
        self.b2b = False
        self.confirm_menu = False
        self.new_best = False
        self._toast_text = ""
        self._toast_frames = 0
        self._gameover_handled = False
        self.state = "playing"
        self.spawn_from_queue()
        self.sound.play("start")

    # ----- UI construction --------------------------------------------------
    def _build_ui(self):
        self.root.title("MyTetris")
        self.root.configure(bg=BG)
        self.root.resizable(False, False)

        main = tk.Frame(self.root, bg=BG, padx=14, pady=14)
        main.pack()

        self.canvas = tk.Canvas(
            main, width=COLS * CELL, height=ROWS * CELL,
            bg=BG_CELL, highlightthickness=2, highlightbackground="#3a3a55")
        self.canvas.grid(row=0, column=0)

        side = tk.Frame(main, bg=BG, padx=14)
        side.grid(row=0, column=1, sticky="n")

        self.score_var = tk.StringVar(value="0")
        self.level_var = tk.StringVar(value="1")
        self.lines_var = tk.StringVar(value="0")
        self.diff_var = tk.StringVar(value=self.difficulty)
        self.best_var = tk.StringVar(value="0")

        tk.Label(side, text="NEXT", bg=BG, fg=SUBTEXT,
                 font=("Consolas", 12, "bold")).pack(anchor="w")
        self.next_canvas = tk.Canvas(
            side, width=4 * PNEXT, height=9 * PNEXT,
            bg=PANEL_CELL, highlightthickness=1, highlightbackground="#33334a")
        self.next_canvas.pack(pady=(2, 8))

        tk.Label(side, text="HOLD", bg=BG, fg=SUBTEXT,
                 font=("Consolas", 12, "bold")).pack(anchor="w")
        self.hold_canvas = tk.Canvas(
            side, width=4 * PNEXT, height=3 * PNEXT,
            bg=PANEL_CELL, highlightthickness=1, highlightbackground="#33334a")
        self.hold_canvas.pack(pady=(2, 8))

        self._stat_row(side, "SCORE", self.score_var)
        self._stat_row(side, "BEST", self.best_var)
        self._stat_row(side, "LEVEL", self.level_var)
        self._stat_row(side, "LINES", self.lines_var)
        self._stat_row(side, "DIFF", self.diff_var)

        tk.Label(side, text="M Mute   Esc Menu", bg=BG, fg=SUBTEXT,
                 justify="left", font=("Consolas", 9)).pack(anchor="w",
                                                            pady=(8, 0))

        self.root.bind("<KeyPress>", self.on_key_press)
        self.root.bind("<KeyRelease>", self.on_key_release)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.focus_set()

    def _stat_row(self, parent, name, var):
        row = tk.Frame(parent, bg=BG)
        row.pack(anchor="w", fill="x", pady=1)
        tk.Label(row, text=name, bg=BG, fg=SUBTEXT, width=6, anchor="w",
                 font=("Consolas", 10, "bold")).pack(side="left")
        tk.Label(row, textvariable=var, bg=BG, fg=TEXT, anchor="w",
                 font=("Consolas", 14, "bold")).pack(side="left")

    # ----- rendering --------------------------------------------------------
    def draw_block(self, canvas, px, py, size, color):
        canvas.create_rectangle(px + 0.5, py + 0.5, px + size - 0.5,
                                py + size - 0.5, fill=color, outline="#0a0a0f")
        hi = _adjust(color, 1.4)
        canvas.create_line(px + 1.5, py + 1.5, px + size - 2, py + 1.5, fill=hi)
        canvas.create_line(px + 1.5, py + 1.5, px + 1.5, py + size - 2, fill=hi)

    def draw_ghost(self, canvas, px, py, size, color):
        canvas.create_rectangle(px + 1, py + 1, px + size - 1, py + size - 1,
                                fill=color, stipple="gray25", outline="")
        canvas.create_rectangle(px + 1.5, py + 1.5, px + size - 1.5,
                                py + size - 1.5, outline=_adjust(color, 1.2),
                                width=2)

    def render(self):
        c = self.canvas
        c.delete("all")
        if self.state == "menu":
            self._render_menu()
            self._render_hud()
            return
        for x in range(COLS + 1):
            c.create_line(x * CELL, 0, x * CELL, ROWS * CELL, fill=GRID_LINE)
        for y in range(ROWS + 1):
            c.create_line(0, y * CELL, COLS * CELL, y * CELL, fill=GRID_LINE)
        for y in range(ROWS):
            for x in range(COLS):
                if self.board[y][x]:
                    self.draw_block(c, x * CELL, y * CELL, CELL, self.board[y][x])
        if self.state in ("playing", "paused"):
            color = COLORS[self.piece]
            ghost_y = self.py
            while self.valid_at(self.piece, self.rot, self.px, ghost_y + 1):
                ghost_y += 1
            for cx, cy in self.current_cells():
                if ghost_y + cy >= 0:
                    self.draw_ghost(c, (self.px + cx) * CELL,
                                    (ghost_y + cy) * CELL, CELL, color)
            for cx, cy in self.current_cells():
                if self.py + cy >= 0:
                    self.draw_block(c, (self.px + cx) * CELL,
                                    (self.py + cy) * CELL, CELL, color)
            if self._toast_frames > 0:
                c.create_text(COLS * CELL / 2, 3 * CELL, text=self._toast_text,
                              fill=GOLD, font=("Consolas", 16, "bold"))
        if self.state == "paused":
            if self.confirm_menu:
                self._draw_overlay([("RETURN TO MENU?", 22, "#ffffff"),
                                    ("This game will be lost", 12, SUBTEXT),
                                    ("Y  Yes        N  No", 13, "#cfcfe0")])
            else:
                self._draw_overlay([("PAUSED", 26, "#ffffff"),
                                    ("P  Resume", 13, "#cfcfe0"),
                                    ("Esc  Menu    R  Retry", 11, SUBTEXT)])
        elif self.state == "gameover":
            lines = [("GAME OVER", 26, "#ffffff")]
            if self.new_best:
                lines.append(("★ NEW HIGH SCORE ★", 14, GOLD))
            lines.append((f"Score  {self.score}", 14, TEXT))
            lines.append((f"Best  {self._best(self.difficulty)}", 12, SUBTEXT))
            lines.append(("ENTER  Menu    R  Retry", 12, "#cfcfe0"))
            self._draw_overlay(lines)
        if self.sound.muted or not self.sound.enabled:
            label = "MUTED" if self.sound.enabled else "NO SOUND"
            c.create_text(COLS * CELL - 6, 10, text=label, fill=SUBTEXT,
                          font=("Consolas", 8), anchor="e")
        self._render_hud()

    def _draw_overlay(self, lines):
        c = self.canvas
        w, h = COLS * CELL, ROWS * CELL
        c.create_rectangle(0, 0, w, h, fill="#000000", stipple="gray50",
                           outline="")
        gap = 14
        total = sum(sz for _, sz, _ in lines) + gap * (len(lines) - 1)
        y = h / 2 - total / 2
        for text, sz, color in lines:
            y += sz / 2
            c.create_text(w / 2, y, text=text, fill=color,
                          font=("Consolas", sz, "bold"))
            y += sz / 2 + gap

    def _render_menu(self):
        c = self.canvas
        w, h = COLS * CELL, ROWS * CELL
        c.create_rectangle(0, 0, w, h, fill=BG_CELL, outline="")
        c.create_text(w / 2, 66, text="MYTETRIS", fill="#ffffff",
                      font=("Consolas", 34, "bold"))
        c.create_text(w / 2, 100, text="a classic clone", fill=SUBTEXT,
                      font=("Consolas", 11))
        c.create_text(w / 2, 162, text="DIFFICULTY", fill=SUBTEXT,
                      font=("Consolas", 12, "bold"))
        c.create_text(w / 2, 194, text=f"◄  {self.difficulty.upper()}  ►",
                      fill=COLORS["T"], font=("Consolas", 20, "bold"))
        c.create_text(w / 2, 222, text=self.diff["blurb"], fill=SUBTEXT,
                      font=("Consolas", 9))
        c.create_text(w / 2, 248,
                      text=f"SPEED RAMP   [ {self.speed_step:.2f} ]",
                      fill=COLORS["I"], font=("Consolas", 13, "bold"))
        c.create_text(w / 2, 272, text=f"BEST  {self._best(self.difficulty)}",
                      fill=GOLD, font=("Consolas", 16, "bold"))
        c.create_text(w / 2, 308, text="TOP SCORES", fill=SUBTEXT,
                      font=("Consolas", 11, "bold"))
        entries = self.scores.get(self.difficulty, [])[:5]
        y = 332
        if entries:
            for i, e in enumerate(entries):
                c.create_text(w / 2, y,
                              text=f"{i + 1}. {e['score']:>6}  Lv{e['level']} "
                                   f" {e['lines']}L",
                              fill=TEXT, font=("Consolas", 11))
                y += 22
        else:
            c.create_text(w / 2, y, text="— none yet —", fill=SUBTEXT,
                          font=("Consolas", 10))
        c.create_text(w / 2, h - 104, text="ENTER  Start", fill="#ffffff",
                      font=("Consolas", 14, "bold"))
        c.create_text(w / 2, h - 76, text="↑↓ Difficulty      [ ] Speed",
                      fill=SUBTEXT, font=("Consolas", 10))
        c.create_text(w / 2, h - 54, text="M  Mute       Esc  Quit",
                      fill=SUBTEXT, font=("Consolas", 10))
        if self.sound.muted or not self.sound.enabled:
            label = "MUTED" if self.sound.enabled else "NO SOUND"
            c.create_text(w - 6, 12, text=label, fill=SUBTEXT,
                          font=("Consolas", 8), anchor="e")

    def _render_hud(self):
        self.score_var.set(str(self.score))
        self.level_var.set(str(self.level))
        self.lines_var.set(str(self.lines))
        self.diff_var.set(self.difficulty)
        self.best_var.set(str(self._best(self.difficulty)))
        self.next_canvas.delete("all")
        for i in range(min(3, len(self.queue))):
            self._blit_piece(self.next_canvas, self.queue[i], 0,
                             i * 3 * PNEXT, PNEXT)
        self.hold_canvas.delete("all")
        if self.hold_piece:
            self._blit_piece(self.hold_canvas, self.hold_piece, 0, 0, PNEXT)

    def _blit_piece(self, canvas, piece, base_x, base_y, cell):
        """Draw `piece` centered in a 4-wide x 3-tall cell box at (base_x, base_y)."""
        cells = self.cells_of(piece, 0)
        cols = [c for c, _ in cells]
        rows = [r for _, r in cells]
        off_x = (4 - (max(cols) - min(cols) + 1)) / 2 - min(cols)
        off_y = (3 - (max(rows) - min(rows) + 1)) / 2 - min(rows)
        for cx, cy in cells:
            self.draw_block(canvas, base_x + (cx + off_x) * cell,
                            base_y + (cy + off_y) * cell, cell, COLORS[piece])


def _selftest():
    """Headless logic exercise across all difficulties; assert no exceptions."""
    root = tk.Tk()
    root.withdraw()
    game = TetrisGame(root, enable_sound=False, persist=False)
    game.render()
    for difficulty in DIFFICULTY_NAMES:
        game.start_game(difficulty)
        for i in range(1500):
            game.update(FRAME_MS)
            if i % 7 == 0:
                game.rotate(1)
            if i % 11 == 0:
                game.try_move(-1, 0)
            if i % 13 == 0:
                game.try_move(1, 0)
            if i % 17 == 0:
                game.rotate(-1)
            if i % 23 == 0:
                game.hold()
            if i % 19 == 0:
                game.hard_drop()
            if game.state == "gameover":
                game.tick()
                game.start_game(difficulty)
            game.render()
    root.destroy()
    print("selftest OK: all difficulties, no exceptions; "
          f"last run score={game.score}, lines={game.lines}, level={game.level}")


def main():
    if "--selftest" in sys.argv:
        _selftest()
        return
    root = tk.Tk()
    TetrisGame(root)
    try:
        root.mainloop()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
