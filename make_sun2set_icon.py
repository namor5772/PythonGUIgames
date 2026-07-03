"""Generate sun2set.ico — the icon for the Sun2Set shortcut.

A sunset over the sea: dusk-gradient sky, the sun half-dipped below the
horizon with a golden reflection shimmering down the water, and a dotted
arc tracing the sun's day-path across the sky (the almanac's whole subject).
Pure standard library — reuses make_tetris_icon.build_ico() to serialize the
32x32 BGRA grid into ICO bytes directly, so no Pillow is required.
"""

import math

from make_tetris_icon import SIZE, build_ico


def _bgra(r, g, b, a=255):
    return (b, g, r, a)


TRANSPARENT = (0, 0, 0, 0)

HORIZON = 20                 # first sea row; the sun rests on this line

# Sky gradient stops, top of tile down to the horizon (indigo dusk to gold).
SKY_STOPS = [(26, 22, 64), (74, 36, 92), (178, 66, 60),
             (255, 140, 48), (255, 196, 92)]
SEA_TOP, SEA_BOT = (18, 26, 52), (8, 12, 26)

STAR = _bgra(226, 224, 246)
STARS = [(5, 2), (12, 1), (26, 3)]

# Dots along the sun's day-path across the sky (sunrise left, sunset right).
ARC = [(7, 11), (12, 8), (16, 7), (20, 8), (25, 11)]
ARC_DOT = _bgra(255, 232, 160)

SUN_CORE = _bgra(255, 250, 214)
SUN_BODY = _bgra(255, 214, 64)
SUN_RIM = _bgra(255, 158, 42)
GLITTER = _bgra(255, 206, 100)
WAVE = _bgra(46, 62, 104)


def _lerp(c0, c1, t):
    return tuple(int(round(c0[k] + (c1[k] - c0[k]) * t)) for k in range(3))


def _sky(y):
    t = y / (HORIZON - 1) * (len(SKY_STOPS) - 1)
    i = min(len(SKY_STOPS) - 2, int(t))
    return _bgra(*_lerp(SKY_STOPS[i], SKY_STOPS[i + 1], t - i))


def build_scene():
    grid = []
    for y in range(SIZE):
        if y < HORIZON:
            grid.append([_sky(y)] * SIZE)
        else:
            t = (y - HORIZON) / (SIZE - 1 - HORIZON)
            grid.append([_bgra(*_lerp(SEA_TOP, SEA_BOT, t))] * SIZE)

    for sx, sy in STARS:
        grid[sy][sx] = STAR
    for ax, ay in ARC:
        grid[ay][ax] = ARC_DOT

    # The setting sun: a half-disc resting on the horizon line.
    cx = 16
    for y in range(HORIZON - 6, HORIZON + 1):
        for x in range(cx - 6, cx + 7):
            d = math.hypot(x - cx, y - HORIZON)
            if d <= 2.3:
                grid[y][x] = SUN_CORE
            elif d <= 4.3:
                grid[y][x] = SUN_BODY
            elif d <= 5.4:
                grid[y][x] = SUN_RIM

    # Sun glitter along the waterline, then a shimmering reflection column
    # (dashes on alternating rows, narrowing with depth).
    for x in range(cx - 6, cx + 7):
        if grid[HORIZON][x] not in (SUN_CORE, SUN_BODY, SUN_RIM):
            grid[HORIZON][x] = GLITTER
    for i, y in enumerate(range(HORIZON + 1, SIZE - 2, 2)):
        half = max(0, 3 - i)
        col = _bgra(*_lerp((255, 186, 78), (186, 108, 44), i / 4))
        for x in range(cx - half, cx + half + 1):
            grid[y][x] = col

    # Faint wave streaks so the water isn't dead flat.
    for y, x0, x1 in [(22, 4, 8), (24, 23, 27), (27, 6, 9)]:
        for x in range(x0, x1 + 1):
            grid[y][x] = WAVE

    # Round the four corners (2px) so it reads as an app tile.
    for px, py in [(0, 0), (1, 0), (0, 1), (31, 0), (30, 0), (31, 1),
                   (0, 31), (0, 30), (1, 31), (31, 31), (30, 31), (31, 30)]:
        grid[py][px] = TRANSPARENT
    return grid


def main():
    ico = build_ico(build_scene())
    with open("sun2set.ico", "wb") as f:
        f.write(ico)
    print(f"Wrote sun2set.ico ({len(ico)} bytes)")


if __name__ == "__main__":
    main()
