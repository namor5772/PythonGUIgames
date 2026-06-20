"""Generate mytetris.ico — a Tetris-themed icon for the MyTetris shortcut.

Draws a tiny playfield: a purple T-piece falling toward a nearly-complete,
multi-colored stack, in the classic tetromino colors. Pure standard library —
it writes the 32x32 ICO/BMP byte format directly, so no Pillow is required.
"""

import struct

SIZE = 32    # icon is 32x32 pixels

GRID = 6     # logical playfield cells per side
CELL = 5     # pixels per logical cell (6 * 5 = 30)
OFF = 1      # 1px margin so the 30px scene is centered in 32px
FILL = 4     # filled pixels per cell (leaves a 1px grid gap)


def _bgra(r, g, b, a=255):
    return (b, g, r, a)


TRANSPARENT = (0, 0, 0, 0)
PANEL = _bgra(18, 18, 28)        # dark background tile

# Classic tetromino colors (same palette as MyTetris.py).
PIECE = {
    "I": _bgra(0x19, 0xD3, 0xDA),
    "J": _bgra(0x3F, 0x63, 0xE0),
    "L": _bgra(0xFF, 0x9F, 0x1A),
    "O": _bgra(0xFF, 0xD9, 0x1A),
    "S": _bgra(0x2E, 0xCC, 0x55),
    "T": _bgra(0xA6, 0x4D, 0xDB),
    "Z": _bgra(0xEF, 0x44, 0x44),
}

# The scene: (col, row) -> piece letter. Row 0 is the top.
SCENE = {
    # Purple T-piece falling toward the gap.
    (2, 0): "T", (1, 1): "T", (2, 1): "T", (3, 1): "T",
    # Stack row 4 — green on the left, red on the right, gap in the middle.
    (0, 4): "S", (1, 4): "S", (4, 4): "Z", (5, 4): "Z",
    # Stack row 5 (bottom) — a full, colorful line.
    (0, 5): "J", (1, 5): "J", (2, 5): "I", (3, 5): "I", (4, 5): "O", (5, 5): "O",
}


def _adjust(bgra, factor):
    b, g, r, a = bgra
    return (max(0, min(255, int(b * factor))),
            max(0, min(255, int(g * factor))),
            max(0, min(255, int(r * factor))), a)


def _draw_cell(grid, col, row, color):
    px = OFF + col * CELL
    py = OFF + row * CELL
    light = _adjust(color, 1.4)
    dark = _adjust(color, 0.65)
    for yy in range(FILL):
        for xx in range(FILL):
            grid[py + yy][px + xx] = color
    for k in range(FILL):
        grid[py][px + k] = light            # top highlight
        grid[py + k][px] = light            # left highlight
        grid[py + FILL - 1][px + k] = dark  # bottom shadow
        grid[py + k][px + FILL - 1] = dark  # right shadow


def build_scene():
    grid = [[PANEL for _ in range(SIZE)] for _ in range(SIZE)]
    for (col, row), piece in SCENE.items():
        _draw_cell(grid, col, row, PIECE[piece])
    # Round the four corners (2px) so it reads as an app tile.
    for cx, cy in [(0, 0), (1, 0), (0, 1), (31, 0), (30, 0), (31, 1),
                   (0, 31), (0, 30), (1, 31), (31, 31), (30, 31), (31, 30)]:
        grid[cy][cx] = TRANSPARENT
    return grid


def build_ico(grid):
    """Serialize a 32x32 grid of (B, G, R, A) pixels into ICO file bytes."""
    # XOR (color) data: BGRA, bottom-up rows.
    xor = bytearray()
    for y in range(SIZE - 1, -1, -1):
        for x in range(SIZE):
            xor += bytes(grid[y][x])

    # AND (mask) data: 1 bpp, bottom-up, rows padded to 4 bytes. All zero
    # = "use the alpha channel", i.e. fully driven by the BGRA alpha above.
    row_bytes = ((SIZE + 31) // 32) * 4
    and_mask = bytes(row_bytes * SIZE)

    # BITMAPINFOHEADER — height is doubled to cover XOR + AND.
    header = struct.pack(
        "<IiiHHIIiiII",
        40, SIZE, SIZE * 2, 1, 32, 0, 0, 0, 0, 0, 0,
    )
    image = header + xor + and_mask

    # ICONDIR + one ICONDIRENTRY, then the image.
    icondir = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack(
        "<BBBBHHII",
        SIZE, SIZE, 0, 0, 1, 32, len(image), 6 + 16,
    )
    return icondir + entry + image


def main():
    ico = build_ico(build_scene())
    with open("mytetris.ico", "wb") as f:
        f.write(ico)
    print(f"Wrote mytetris.ico ({len(ico)} bytes)")


if __name__ == "__main__":
    main()
