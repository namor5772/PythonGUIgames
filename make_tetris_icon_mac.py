"""Generate a humorous macOS icon for MyTetris — "The Troll Piece".

The gag every Tetris player knows: you've stacked a clean wall with a single
1-wide well, praying for the long cyan I-piece... and the game hands you a fat,
grinning O-piece (a square) that is gleefully too wide to fill it. Note there's
no cyan anywhere in the stack — the I-piece never came.

Pure standard library: renders a 1024x1024 RGBA scene and writes it as a PNG by
hand (zlib + the PNG chunk format), so no Pillow is required. macOS `sips` /
`iconutil` then turn this master PNG into `mytetris.icns` (see
create_shortcut.command).
"""

import math
import struct
import zlib

W = H = 1024  # master resolution (covers the 512x512@2x .icns slot)

# --- palette (same tetromino colors as MyTetris.py / make_tetris_icon.py) -----
PANEL_TOP = (28, 28, 44)      # playfield tile, lighter at the top
PANEL_BOT = (16, 16, 26)      # ...darker at the bottom (subtle gradient)
PIECE = {
    "I": (0x19, 0xD3, 0xDA),
    "J": (0x3F, 0x63, 0xE0),
    "L": (0xFF, 0x9F, 0x1A),
    "O": (0xFF, 0xD9, 0x1A),
    "S": (0x2E, 0xCC, 0x55),
    "T": (0xA6, 0x4D, 0xDB),
    "Z": (0xEF, 0x44, 0x44),
}
BLACK = (20, 18, 24)
WHITE = (250, 250, 255)


# --- tiny software rasterizer (RGBA bytearray, source-over compositing) -------
def new_canvas():
    return bytearray(W * H * 4)  # all zero = fully transparent


def blend(buf, x, y, r, g, b, a):
    if a <= 0 or x < 0 or x >= W or y < 0 or y >= H:
        return
    i = (y * W + x) * 4
    if a >= 255:
        buf[i] = r; buf[i + 1] = g; buf[i + 2] = b; buf[i + 3] = 255
        return
    ba = buf[i + 3]
    if ba == 0:
        buf[i] = r; buf[i + 1] = g; buf[i + 2] = b; buf[i + 3] = a
        return
    af = a / 255.0
    inv = 1.0 - af
    buf[i] = int(r * af + buf[i] * inv)
    buf[i + 1] = int(g * af + buf[i + 1] * inv)
    buf[i + 2] = int(b * af + buf[i + 2] * inv)
    buf[i + 3] = min(255, int(a + ba * inv))


def fill_rect(buf, x0, y0, x1, y1, color):
    r, g, b = color[:3]
    a = color[3] if len(color) > 3 else 255
    x0 = max(0, int(x0)); y0 = max(0, int(y0))
    x1 = min(W, int(x1)); y1 = min(H, int(y1))
    if x1 <= x0 or y1 <= y0:
        return
    if a >= 255:
        row = bytes((r, g, b, 255)) * (x1 - x0)
        for y in range(y0, y1):
            i = (y * W + x0) * 4
            buf[i:i + (x1 - x0) * 4] = row
    else:
        for y in range(y0, y1):
            for x in range(x0, x1):
                blend(buf, x, y, r, g, b, a)


def fill_circle(buf, cx, cy, rad, color):
    r, g, b = color[:3]
    a = color[3] if len(color) > 3 else 255
    for y in range(int(cy - rad - 1), int(cy + rad + 2)):
        for x in range(int(cx - rad - 1), int(cx + rad + 2)):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if d <= rad - 0.5:
                blend(buf, x, y, r, g, b, a)
            elif d < rad + 0.5:                       # 1px anti-aliased rim
                blend(buf, x, y, r, g, b, int(a * (rad + 0.5 - d)))


def rounded_rect(buf, x0, y0, x1, y1, rad, color):
    fill_rect(buf, x0 + rad, y0, x1 - rad, y1, color)
    fill_rect(buf, x0, y0 + rad, x0 + rad, y1 - rad, color)
    fill_rect(buf, x1 - rad, y0 + rad, x1, y1 - rad, color)
    for cx, cy in ((x0 + rad, y0 + rad), (x1 - rad, y0 + rad),
                   (x0 + rad, y1 - rad), (x1 - rad, y1 - rad)):
        fill_circle(buf, cx, cy, rad, color)


def thick_path(buf, pts, thickness, color):
    """Stroke a smooth curve by stamping overlapping discs along it."""
    rad = thickness / 2.0
    for i in range(len(pts) - 1):
        (x0, y0), (x1, y1) = pts[i], pts[i + 1]
        steps = int(max(2, math.hypot(x1 - x0, y1 - y0)))
        for s in range(steps + 1):
            t = s / steps
            fill_circle(buf, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, rad, color)


def _adjust(color, factor):
    return tuple(max(0, min(255, int(c * factor))) for c in color[:3])


def beveled_cell(buf, px, py, size, color, radius=None):
    """A chunky 3D tetromino block, like the ones the game draws."""
    light = _adjust(color, 1.45)
    dark = _adjust(color, 0.58)
    bw = max(3, size // 9)             # bevel width
    radius = size // 6 if radius is None else radius
    rounded_rect(buf, px, py, px + size, py + size, radius, color)
    fill_rect(buf, px + radius, py, px + size - radius, py + bw, light)      # top
    fill_rect(buf, px, py + radius, px + bw, py + size - radius, light)      # left
    fill_rect(buf, px + radius, py + size - bw, px + size - radius, py + size, dark)
    fill_rect(buf, px + size - bw, py + radius, px + size, py + size - radius, dark)


# --- the scene ----------------------------------------------------------------
def build_scene():
    buf = new_canvas()

    # 1) Rounded playfield tile with a faint top-to-bottom gradient. Draw the
    #    smooth (anti-aliased) rounded silhouette in the base color first, then
    #    lay the gradient row by row, insetting along the corner arc so the
    #    rounded corners show through and the sides still reach the edge.
    M, RAD = 36, 170
    rounded_rect(buf, M, M, W - M, H - M, RAD, (*PANEL_BOT, 255))
    for y in range(M, H - M):
        t = (y - M) / (H - 2 * M)
        col = tuple(int(PANEL_TOP[k] + (PANEL_BOT[k] - PANEL_TOP[k]) * t)
                    for k in range(3))
        dy = 0                                         # distance into a corner band
        if y < M + RAD:
            dy = RAD - (y - M)
        elif y > H - M - RAD:
            dy = y - (H - M - RAD)
        inset = int(RAD - math.sqrt(max(0, RAD * RAD - dy * dy))) + 1 if dy else 0
        fill_rect(buf, M + inset, y, W - M - inset, y + 1, (*col, 255))

    # 2) The stack at the bottom: 7 columns, a deep 1-wide WELL dead center.
    #    Deliberately NO cyan "I" — the long piece you needed never arrived.
    cols, cell = 7, 120
    field_w = cols * cell
    x0 = (W - field_w) // 2
    floor = H - 64
    WELL = 3                                            # the empty center well
    heights = [3, 4, 3, 0, 3, 4, 3]                     # blocks per column
    fills = ["J", "L", "S", None, "Z", "T", "L"]
    for c in range(cols):
        if c == WELL:
            continue
        for r in range(heights[c]):
            px = x0 + c * cell
            py = floor - (r + 1) * cell
            beveled_cell(buf, px + 5, py + 5, cell - 10, PIECE[fills[c]])

    # subtle "this is the well, give me an I-piece" shading down the empty column
    wx = x0 + WELL * cell
    fill_rect(buf, wx + 8, floor - 4 * cell, wx + cell - 8, floor, (0, 0, 0, 55))

    # 3) The troll: a fat grinning O-piece (2x2) falling square over the well.
    o_cell = 200
    ocx = x0 + WELL * cell + cell // 2                  # == W // 2, dead center
    otop = 168
    ox = ocx - o_cell                                   # left edge of the 2x2
    for (cc, rr) in ((0, 0), (1, 0), (0, 1), (1, 1)):
        beveled_cell(buf, ox + cc * o_cell + 6, otop + rr * o_cell + 6,
                     o_cell - 12, PIECE["O"], radius=28)

    # motion streaks above it (it's plummeting toward the wrong spot)
    for k, dx in enumerate((-92, 0, 92)):
        sx = ocx + dx
        fill_rect(buf, sx - 6, otop - 116 - (k % 2) * 26, sx + 6, otop - 34,
                  (255, 255, 255, 55))

    draw_troll_face(buf, ox, otop, o_cell * 2)
    return buf


def draw_troll_face(buf, fx, fy, fsize):
    """A smug, raised-eyebrow grin spanning the 2x2 O-piece."""
    cx = fx + fsize / 2
    eye_y = fy + fsize * 0.42
    dx = fsize * 0.24
    eye_r = fsize * 0.135

    for side, ex in ((-1, cx - dx), (1, cx + dx)):
        # white of the eye
        fill_circle(buf, ex, eye_y, eye_r, WHITE)
        # smug half-lidded look: cover the top of each eye with an O-yellow lid
        lid = _adjust(PIECE["O"], 1.0)
        fill_rect(buf, ex - eye_r - 2, eye_y - eye_r - 2,
                  ex + eye_r + 2, eye_y - eye_r * 0.18, (*lid, 255))
        thick_path(buf, [(ex - eye_r, eye_y - eye_r * 0.18),
                         (ex + eye_r, eye_y - eye_r * 0.18)],
                   fsize * 0.026, BLACK)               # the eyelid line
        # pupil, looking sideways toward the well (down-left) — shifty
        fill_circle(buf, ex + side * eye_r * 0.0 - eye_r * 0.30,
                    eye_y + eye_r * 0.28, eye_r * 0.5, BLACK)
        fill_circle(buf, ex - eye_r * 0.42, eye_y + eye_r * 0.12,
                    eye_r * 0.16, WHITE)               # catch-light

    # one cocked eyebrow (left) — raised and angled = pure smugness
    thick_path(buf, [(cx - dx - eye_r, eye_y - eye_r * 1.55),
                     (cx - dx + eye_r * 0.7, eye_y - eye_r * 1.95)],
               fsize * 0.05, BLACK)
    # the other brow, flatter
    thick_path(buf, [(cx + dx - eye_r * 0.7, eye_y - eye_r * 1.5),
                     (cx + dx + eye_r, eye_y - eye_r * 1.42)],
               fsize * 0.05, BLACK)

    # the sly asymmetric grin: flat-ish on the left, curling up on the right
    my = fy + fsize * 0.70
    mw = fsize * 0.30
    grin = [(cx - mw, my - fsize * 0.02),
            (cx - mw * 0.4, my + fsize * 0.06),
            (cx + mw * 0.3, my + fsize * 0.075),
            (cx + mw * 0.85, my + fsize * 0.0),
            (cx + mw * 1.02, my - fsize * 0.075)]      # curls upward at the end
    thick_path(buf, grin, fsize * 0.045, BLACK)

    # a tiny cheeky tongue poking out the low-left of the grin
    fill_circle(buf, cx - mw * 0.45, my + fsize * 0.085, fsize * 0.05,
                PIECE["Z"])
    fill_rect(buf, cx - mw * 0.45 - fsize * 0.05, my + fsize * 0.05,
              cx - mw * 0.45 + fsize * 0.05, my + fsize * 0.085, PIECE["Z"])


# --- PNG writer (pure stdlib) -------------------------------------------------
def write_png(buf, path):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = bytearray()
    for y in range(H):
        raw.append(0)                                  # filter type 0 per row
        raw += buf[y * W * 4:(y + 1) * W * 4]
    ihdr = struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)  # 8-bit RGBA
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main():
    buf = build_scene()
    write_png(buf, "mytetris.png")
    print(f"Wrote mytetris.png ({W}x{H})")


if __name__ == "__main__":
    main()
