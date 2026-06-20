"""MyTetris.py — an accurate Tkinter clone of the classic game TETRIS.

Implements the modern "guideline" mechanics that make Tetris feel right:
  * 10 x 20 playfield, 7 tetrominoes with their standard colors
  * SRS (Super Rotation System) rotation with wall kicks
  * 7-bag randomizer (every 7 spawns yields all 7 pieces, shuffled)
  * DAS (Delayed Auto Shift) for smooth held left/right movement
  * Soft drop, hard drop, ghost piece, hold, lock delay
  * Line-clear scoring, leveling, and increasing gravity

Controls:
  Left / Right .... move          Up or X ......... rotate clockwise
  Down ............ soft drop      Z or Ctrl ....... rotate counter-clockwise
  Space ........... hard drop      C or Shift ...... hold piece
  P ............... pause          R ............... restart

Run:  python MyTetris.py           Self-test (headless logic):  python MyTetris.py --selftest
"""

import random
import sys
import tkinter as tk

# ----------------------------------------------------------------------------
# Board / rendering constants
# ----------------------------------------------------------------------------
COLS, ROWS = 10, 20          # standard Tetris playfield
CELL = 30                    # pixel size of one board cell
PCELL = 26                   # pixel size of one preview cell (next / hold)
FRAME_MS = 16                # ~60 FPS fixed timestep

# Timing (milliseconds)
DAS_DELAY = 150              # delay before auto-shift kicks in when holding L/R
DAS_REPEAT = 40             # auto-shift repeat interval
SOFT_DROP_INTERVAL = 30      # fall interval while soft-dropping
LOCK_DELAY = 500             # grace time on the ground before a piece locks
MAX_LOCK_RESETS = 15         # cap on lock-delay resets (prevents infinite spin)

# Palette
BG = "#0b0b12"
BG_CELL = "#15151f"
GRID_LINE = "#22222e"
PANEL_CELL = "#15151f"
TEXT = "#e6e6ec"
SUBTEXT = "#9a9ab0"

# Standard tetromino colors.
COLORS = {
    "I": "#19d3da",   # cyan
    "J": "#3f63e0",   # blue
    "L": "#ff9f1a",   # orange
    "O": "#ffd91a",   # yellow
    "S": "#2ecc55",   # green
    "T": "#a64ddb",   # purple
    "Z": "#ef4444",   # red
}

# Spawn-state matrices (SRS). Other rotation states are 90-degree rotations.
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
    """Rotate a square matrix 90 degrees clockwise."""
    n = len(matrix)
    return [[matrix[n - 1 - j][i] for j in range(n)] for i in range(n)]


def _cells(matrix):
    """List (col, row) offsets of every filled square in a matrix."""
    return [(c, r) for r, row in enumerate(matrix)
            for c, val in enumerate(row) if val]


# Precompute the 4 rotation states (as cell-offset lists) for every piece.
PIECE_CELLS = {}
for _name, _matrix in BASE.items():
    _states, _cur = [], _matrix
    for _ in range(4):
        _states.append(_cells(_cur))
        _cur = _rotate_cw(_cur)
    PIECE_CELLS[_name] = _states

# SRS wall-kick tables. Keys are (from_state, to_state); values are the (dx, dy)
# offsets to try in order (dy is positive-down to match screen coordinates).
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

# Lines-cleared -> base score (multiplied by level).
SCORE_TABLE = {1: 100, 2: 300, 3: 500, 4: 800}


def _adjust(hex_color, factor):
    """Lighten (factor > 1) or darken (factor < 1) a #rrggbb color."""
    h = hex_color.lstrip("#")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    r = max(0, min(255, int(r * factor)))
    g = max(0, min(255, int(g * factor)))
    b = max(0, min(255, int(b * factor)))
    return f"#{r:02x}{g:02x}{b:02x}"


class TetrisGame:
    """Holds all game state, the update logic, input handling, and rendering."""

    def __init__(self, root):
        self.root = root
        self._build_ui()
        self.reset_game()
        # Start the fixed-timestep loop.
        self.loop_id = self.root.after(FRAME_MS, self.tick)

    # ----- piece geometry helpers ------------------------------------------
    @staticmethod
    def cells_of(piece, rotation):
        return PIECE_CELLS[piece][rotation % 4]

    def current_cells(self):
        return self.cells_of(self.piece, self.rot)

    def valid_at(self, piece, rotation, px, py):
        """Can `piece` in `rotation` occupy board position (px, py)?"""
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
        """Make `piece` the active falling piece at its spawn position."""
        self.piece = piece
        self.rot = 0
        self.px = 4 if piece == "O" else 3
        self.py = 0
        self.fall_charge = 0
        self.lock_counter = 0
        self.lock_resets = 0
        if not self.valid_at(self.piece, self.rot, self.px, self.py):
            self.state = "gameover"   # spawn blocked => top out

    def spawn_from_queue(self):
        piece = self.queue.pop(0)
        self._fill_queue()
        self.set_current(piece)

    # ----- locking & line clears -------------------------------------------
    def lock_piece(self):
        topout = False
        for cx, cy in self.current_cells():
            x, y = self.px + cx, self.py + cy
            if y < 0:
                topout = True
                continue
            if 0 <= x < COLS and 0 <= y < ROWS:
                self.board[y][x] = COLORS[self.piece]
        self.clear_lines()
        self.can_hold = True
        if topout:
            self.state = "gameover"
            return
        self.spawn_from_queue()

    def clear_lines(self):
        kept = [row for row in self.board if any(c is None for c in row)]
        cleared = ROWS - len(kept)
        if cleared:
            for _ in range(cleared):
                kept.insert(0, [None] * COLS)
            self.board = kept
            self.lines += cleared
            self.level = self.lines // 10 + 1
            self.score += SCORE_TABLE.get(cleared, 0) * self.level

    # ----- movement / rotation ---------------------------------------------
    def try_move(self, dx, dy):
        if not self.valid_at(self.piece, self.rot, self.px + dx, self.py + dy):
            return False
        self.px += dx
        self.py += dy
        if dy > 0:                       # fell a row => fresh lock window
            self.lock_counter = 0
            self.lock_resets = 0
        elif dx != 0 and self.grounded():  # shifted on the ground => reset lock
            self._reset_lock_on_action()
        return True

    def rotate(self, direction):
        """direction: +1 clockwise, -1 counter-clockwise."""
        if self.state != "playing" or self.piece == "O":
            return False
        new_rot = (self.rot + direction) % 4
        table = KICKS_I if self.piece == "I" else KICKS_JLSTZ
        for dx, dy in table[(self.rot, new_rot)]:
            if self.valid_at(self.piece, new_rot, self.px + dx, self.py + dy):
                self.rot = new_rot
                self.px += dx
                self.py += dy
                if self.grounded():
                    self._reset_lock_on_action()
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
        self.lock_piece()

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

    # ----- per-frame update -------------------------------------------------
    def gravity_interval(self):
        """Milliseconds per cell of natural fall, from the guideline curve."""
        level = self.level
        base = 0.8 - (level - 1) * 0.007
        if base <= 0:
            base = 0.01
        return max((base ** (level - 1)) * 1000, FRAME_MS)

    def update(self, dt):
        self._update_horizontal(dt)
        self._update_gravity(dt)
        if self.state != "playing":
            return
        if self.grounded():
            self.lock_counter += dt
            if self.lock_counter >= LOCK_DELAY:
                self.lock_piece()
        else:
            self.lock_counter = 0

    def _update_horizontal(self, dt):
        left = "Left" in self.held
        right = "Right" in self.held
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
            # The initial step already happened on key-press; just start charging.
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
        interval = SOFT_DROP_INTERVAL if soft else self.gravity_interval()
        interval = max(interval, 1)
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
        self.render()
        self.loop_id = self.root.after(FRAME_MS, self.tick)

    # ----- input ------------------------------------------------------------
    def on_key_press(self, event):
        key = event.keysym
        if key in self.held:
            return  # ignore the OS auto-repeat; DAS drives continuous movement
        self.held.add(key)
        self._handle_press(key)

    def on_key_release(self, event):
        self.held.discard(event.keysym)

    def _handle_press(self, key):
        if key in ("p", "P"):
            if self.state == "playing":
                self.state = "paused"
            elif self.state == "paused":
                self.state = "playing"
            return
        if key in ("r", "R"):
            self.reset_game()
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

    # ----- lifecycle --------------------------------------------------------
    def reset_game(self):
        self.board = [[None] * COLS for _ in range(ROWS)]
        self.score = 0
        self.lines = 0
        self.level = 1
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
        self.state = "playing"
        self.spawn_from_queue()

    # ----- UI construction --------------------------------------------------
    def _build_ui(self):
        self.root.title("MyTetris")
        self.root.configure(bg=BG)
        self.root.resizable(False, False)

        main = tk.Frame(self.root, bg=BG, padx=14, pady=14)
        main.pack()

        self.canvas = tk.Canvas(
            main, width=COLS * CELL, height=ROWS * CELL,
            bg=BG_CELL, highlightthickness=2, highlightbackground="#3a3a55",
        )
        self.canvas.grid(row=0, column=0, rowspan=1)

        side = tk.Frame(main, bg=BG, padx=14)
        side.grid(row=0, column=1, sticky="n")

        self.score_var = tk.StringVar(value="0")
        self.level_var = tk.StringVar(value="1")
        self.lines_var = tk.StringVar(value="0")

        tk.Label(side, text="NEXT", bg=BG, fg=SUBTEXT,
                 font=("Consolas", 12, "bold")).pack(anchor="w")
        self.next_canvas = tk.Canvas(
            side, width=4 * PCELL, height=4 * PCELL,
            bg=PANEL_CELL, highlightthickness=1, highlightbackground="#33334a")
        self.next_canvas.pack(pady=(2, 12))

        tk.Label(side, text="HOLD", bg=BG, fg=SUBTEXT,
                 font=("Consolas", 12, "bold")).pack(anchor="w")
        self.hold_canvas = tk.Canvas(
            side, width=4 * PCELL, height=4 * PCELL,
            bg=PANEL_CELL, highlightthickness=1, highlightbackground="#33334a")
        self.hold_canvas.pack(pady=(2, 12))

        self._stat(side, "SCORE", self.score_var)
        self._stat(side, "LEVEL", self.level_var)
        self._stat(side, "LINES", self.lines_var)

        controls = (
            "CONTROLS\n"
            "← →  Move\n"
            "↑ / X  Rotate CW\n"
            "Z  Rotate CCW\n"
            "↓  Soft drop\n"
            "Space  Hard drop\n"
            "C  Hold\n"
            "P  Pause   R  Restart"
        )
        tk.Label(side, text=controls, bg=BG, fg=SUBTEXT, justify="left",
                 font=("Consolas", 9)).pack(anchor="w", pady=(12, 0))

        # Key bindings.
        self.root.bind("<KeyPress>", self.on_key_press)
        self.root.bind("<KeyRelease>", self.on_key_release)
        self.root.focus_set()

    def _stat(self, parent, name, var):
        tk.Label(parent, text=name, bg=BG, fg=SUBTEXT,
                 font=("Consolas", 11, "bold")).pack(anchor="w")
        tk.Label(parent, textvariable=var, bg=BG, fg=TEXT,
                 font=("Consolas", 18, "bold")).pack(anchor="w", pady=(0, 8))

    # ----- rendering --------------------------------------------------------
    def draw_block(self, canvas, px, py, size, color):
        canvas.create_rectangle(px + 0.5, py + 0.5, px + size - 0.5,
                                py + size - 0.5, fill=color, outline="#0a0a0f")
        hi = _adjust(color, 1.4)
        canvas.create_line(px + 1.5, py + 1.5, px + size - 2, py + 1.5, fill=hi)
        canvas.create_line(px + 1.5, py + 1.5, px + 1.5, py + size - 2, fill=hi)

    def draw_ghost(self, canvas, px, py, size, color):
        # Faint fill plus a bright 2px outline so the landing spot reads clearly.
        canvas.create_rectangle(px + 1, py + 1, px + size - 1, py + size - 1,
                                fill=color, stipple="gray25", outline="")
        canvas.create_rectangle(px + 1.5, py + 1.5, px + size - 1.5,
                                py + size - 1.5, outline=_adjust(color, 1.2),
                                width=2)

    def render(self):
        c = self.canvas
        c.delete("all")
        # Grid lines.
        for x in range(COLS + 1):
            c.create_line(x * CELL, 0, x * CELL, ROWS * CELL, fill=GRID_LINE)
        for y in range(ROWS + 1):
            c.create_line(0, y * CELL, COLS * CELL, y * CELL, fill=GRID_LINE)
        # Locked blocks.
        for y in range(ROWS):
            for x in range(COLS):
                color = self.board[y][x]
                if color:
                    self.draw_block(c, x * CELL, y * CELL, CELL, color)
        # Active piece + ghost.
        if self.state in ("playing", "paused"):
            ghost_y = self.py
            while self.valid_at(self.piece, self.rot, self.px, ghost_y + 1):
                ghost_y += 1
            color = COLORS[self.piece]
            for cx, cy in self.current_cells():
                gx, gy = self.px + cx, ghost_y + cy
                if gy >= 0:
                    self.draw_ghost(c, gx * CELL, gy * CELL, CELL, color)
            for cx, cy in self.current_cells():
                x, y = self.px + cx, self.py + cy
                if y >= 0:
                    self.draw_block(c, x * CELL, y * CELL, CELL, color)
        # Overlays.
        if self.state == "paused":
            self._overlay("PAUSED", "Press P to resume")
        elif self.state == "gameover":
            self._overlay("GAME OVER", "Press R to restart")
        self._render_hud()

    def _overlay(self, title, subtitle):
        c = self.canvas
        w, h = COLS * CELL, ROWS * CELL
        c.create_rectangle(0, 0, w, h, fill="#000000", stipple="gray50",
                           outline="")
        c.create_text(w / 2, h / 2 - 18, text=title, fill="#ffffff",
                      font=("Consolas", 26, "bold"))
        c.create_text(w / 2, h / 2 + 22, text=subtitle, fill="#cfcfe0",
                      font=("Consolas", 12))

    def _render_hud(self):
        self.score_var.set(str(self.score))
        self.level_var.set(str(self.level))
        self.lines_var.set(str(self.lines))
        self._draw_preview(self.next_canvas, self.queue[0] if self.queue else None)
        self._draw_preview(self.hold_canvas, self.hold_piece)

    def _draw_preview(self, canvas, piece):
        canvas.delete("all")
        if not piece:
            return
        cells = self.cells_of(piece, 0)
        cols = [c for c, _ in cells]
        rows = [r for _, r in cells]
        wb = max(cols) - min(cols) + 1
        hb = max(rows) - min(rows) + 1
        off_x = (4 - wb) / 2 - min(cols)
        off_y = (4 - hb) / 2 - min(rows)
        for cx, cy in cells:
            self.draw_block(canvas, (cx + off_x) * PCELL,
                            (cy + off_y) * PCELL, PCELL, COLORS[piece])


def _selftest():
    """Headless logic exercise: drive the game hard and assert it never throws."""
    root = tk.Tk()
    root.withdraw()
    game = TetrisGame(root)
    game.render()
    for i in range(4000):
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
            game.reset_game()
        game.render()
    root.destroy()
    print("selftest OK: 4000 frames, no exceptions; "
          f"reached score={game.score}, lines={game.lines}, level={game.level}")


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
