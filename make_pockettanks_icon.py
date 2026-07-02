"""Generate mypockettanks.ico — the icon for the MyPocketTanks shortcut.

Draws a tiny artillery duel: a red tank on a grassy hill lobbing a shell
(dotted arc) into an explosion on the far slope, under a starry night sky.
Pure standard library — reuses make_tetris_icon.build_ico() to serialize the
32x32 BGRA grid into ICO bytes directly, so no Pillow is required.
"""

import math

from make_tetris_icon import SIZE, build_ico


def _bgra(r, g, b, a=255):
    return (b, g, r, a)


TRANSPARENT = (0, 0, 0, 0)

SKY = [_bgra(11, 13, 30), _bgra(20, 26, 52), _bgra(34, 44, 74)]
STARS = [(3, 2), (9, 5), (14, 1), (20, 4), (27, 2), (30, 7)]
STAR = _bgra(220, 220, 245)

GRASS = _bgra(63, 174, 79)
GRASS_DARK = _bgra(47, 133, 60)
DIRT = _bgra(124, 86, 48)
DIRT_DARK = _bgra(88, 60, 34)

TANK = _bgra(239, 68, 68)
TANK_DARK = _bgra(150, 40, 40)
BARREL = _bgra(200, 200, 214)
TRAIL = _bgra(255, 217, 26)
BOOM_OUT = _bgra(255, 85, 34)
BOOM_IN = _bgra(255, 239, 122)


def _heights():
    """Terrain surface y per column: hill left, valley, rising right slope."""
    hs = []
    for x in range(SIZE):
        y = 21 - 4 * math.sin(x * 0.18 + 0.6) - 2 * math.sin(x * 0.45)
        hs.append(max(14, min(26, int(round(y)))))
    return hs


def build_scene():
    hs = _heights()
    grid = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            h = hs[x]
            if y < h:
                row.append(SKY[min(2, y * 3 // SIZE)])
            elif y == h:
                row.append(GRASS)
            elif y == h + 1:
                row.append(GRASS_DARK)
            else:
                row.append(DIRT if y - h < 7 else DIRT_DARK)
        grid.append(row)
    for sx, sy in STARS:
        if sy < hs[sx]:
            grid[sy][sx] = STAR

    # The red tank on the left hill: treads, hull, dome, and a raised barrel.
    tx = 7
    gy = hs[tx]
    for x in range(tx - 3, tx + 4):
        grid[gy - 1][x] = TANK_DARK             # treads
        grid[gy - 2][x] = TANK                  # hull
    for x in range(tx - 1, tx + 2):
        grid[gy - 3][x] = TANK                  # dome
    for i, (dx, dy) in enumerate([(1, -4), (2, -5), (3, -6)]):
        grid[gy + dy][tx + dx] = BARREL         # barrel, aimed up-right

    # Dotted shell arc from the muzzle toward the far slope.
    for x, y in [(12, 12), (15, 9), (18, 8), (21, 9), (23, 11)]:
        grid[y][x] = TRAIL

    # Explosion where the shell lands on the right slope.
    ex, ey = 26, hs[26] - 2
    for y in range(SIZE):
        for x in range(SIZE):
            d = math.hypot(x - ex, y - ey)
            if d <= 1.4:
                grid[y][x] = BOOM_IN
            elif d <= 3.2:
                grid[y][x] = BOOM_OUT

    # Round the four corners (2px) so it reads as an app tile.
    for cx, cy in [(0, 0), (1, 0), (0, 1), (31, 0), (30, 0), (31, 1),
                   (0, 31), (0, 30), (1, 31), (31, 31), (30, 31), (31, 30)]:
        grid[cy][cx] = TRANSPARENT
    return grid


def main():
    ico = build_ico(build_scene())
    with open("mypockettanks.ico", "wb") as f:
        f.write(ico)
    print(f"Wrote mypockettanks.ico ({len(ico)} bytes)")


if __name__ == "__main__":
    main()
