"""Generate hello_world.ico — a small 'window' icon for the Hello World app.

Pure standard library: builds a 32x32 32-bit ICO by writing the ICO + BMP
byte format directly, so no Pillow / third-party dependency is required.
"""

import struct

SIZE = 32

# Colors as (B, G, R, A) — note BMP/ICO store blue first.
TRANSPARENT = (0, 0, 0, 0)
TITLE_BAR = (215, 120, 40, 255)    # blue title bar (RGB 40,120,215)
WINDOW_BODY = (245, 245, 245, 255)  # off-white window body
CLOSE_DOT = (60, 60, 220, 255)      # red close button (RGB 220,60,60)


def build_pixels():
    """Return a top-down SIZE x SIZE grid of (B, G, R, A) pixels."""
    grid = [[TRANSPARENT for _ in range(SIZE)] for _ in range(SIZE)]

    margin = 3              # transparent border around the window
    lo, hi = margin, SIZE - margin - 1
    title_bottom = margin + 6  # height of the title bar

    for y in range(SIZE):
        for x in range(SIZE):
            if x < lo or x > hi or y < lo or y > hi:
                continue  # leave transparent border
            # Round the corners by skipping the 4 corner pixels.
            if (x, y) in {(lo, lo), (hi, lo), (lo, hi), (hi, hi)}:
                continue
            if y <= title_bottom:
                grid[y][x] = TITLE_BAR
            else:
                grid[y][x] = WINDOW_BODY

    # A small red "close" dot in the top-right of the title bar.
    for dy in range(2):
        for dx in range(2):
            grid[lo + 2 + dy][hi - 2 + dx] = CLOSE_DOT

    return grid


def build_ico(grid):
    """Serialize the pixel grid into ICO file bytes."""
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
    ico = build_ico(build_pixels())
    with open("hello_world.ico", "wb") as f:
        f.write(ico)
    print(f"Wrote hello_world.ico ({len(ico)} bytes)")


if __name__ == "__main__":
    main()
