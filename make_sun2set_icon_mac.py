"""Generate sun2set.png — the macOS icon master for Sun2Set.

The same sunset-over-the-sea scene as sun2set.ico (dusk-gradient sky, the
sun half-dipped below the horizon, a golden reflection, and the sun's dotted
day-path arc), but rendered natively at 1024x1024 instead of upscaled from
32px pixel art.

Pure standard library: reuses the software rasterizer and hand-rolled PNG
writer from make_tetris_icon_mac.py. macOS `sips`/`iconutil` then turn this
master PNG into `sun2set.icns` (see create_shortcut.command).
"""

import math

from make_tetris_icon_mac import H, W, fill_circle, fill_rect, new_canvas, write_png

S = W // 32          # one "pixel" of the 32px ico at this resolution
M, RAD = 36, 170     # tile margin / corner radius (match mytetris.png)

HORIZON = 20 * S     # the waterline, same proportion as the ico

# Same palette as make_sun2set_icon.py (RGB, not BGRA).
SKY_STOPS = [(26, 22, 64), (74, 36, 92), (178, 66, 60),
             (255, 140, 48), (255, 196, 92)]
SEA_TOP, SEA_BOT = (18, 26, 52), (8, 12, 26)
STAR = (226, 224, 246)
ARC_DOT = (255, 232, 160)
SUN_CORE = (255, 250, 214)
SUN_BODY = (255, 214, 64)
SUN_RIM = (255, 158, 42)
GLITTER = (255, 206, 100)
WAVE = (66, 88, 138)


def _lerp(c0, c1, t):
    return tuple(int(round(c0[k] + (c1[k] - c0[k]) * t)) for k in range(3))


def build_scene():
    buf = new_canvas()

    # 1) Sky: indigo dusk at the top blending down to gold at the horizon.
    for y in range(M, HORIZON):
        t = (y - M) / (HORIZON - 1 - M) * (len(SKY_STOPS) - 1)
        i = min(len(SKY_STOPS) - 2, int(t))
        fill_rect(buf, M, y, W - M, y + 1,
                  _lerp(SKY_STOPS[i], SKY_STOPS[i + 1], t - i))

    # 2) Stars in the dark upper sky, with a faint halo so they read small.
    for sx, sy in [(5, 2), (12, 1), (26, 3)]:
        cx, cy = (sx + 0.5) * S, (sy + 0.5) * S
        fill_circle(buf, cx, cy, 0.42 * S, (*STAR, 70))
        fill_circle(buf, cx, cy, 0.20 * S, STAR)

    # 3) The sun's dotted day-path arc (sunrise left, sunset right).
    for ax, ay in [(7, 11), (12, 8), (16, 7), (20, 8), (25, 11)]:
        cx, cy = (ax + 0.5) * S, (ay + 0.5) * S
        fill_circle(buf, cx, cy, 0.45 * S, (*ARC_DOT, 80))
        fill_circle(buf, cx, cy, 0.26 * S, ARC_DOT)

    # 4) The setting sun — drawn as a full glowing disc; the sea rows drawn
    #    next paint over its lower half, leaving it dipped in the water.
    scx = W / 2
    fill_circle(buf, scx, HORIZON, 7.0 * S, (*SUN_RIM, 46))      # outer glow
    fill_circle(buf, scx, HORIZON, 6.0 * S, (*SUN_RIM, 70))
    fill_circle(buf, scx, HORIZON, 5.3 * S, SUN_RIM)
    fill_circle(buf, scx, HORIZON, 4.3 * S, SUN_BODY)
    fill_circle(buf, scx, HORIZON, 2.4 * S, SUN_CORE)

    # 5) The sea: a dark gradient over the sun's lower half...
    for y in range(HORIZON, H - M):
        t = (y - HORIZON) / (H - M - 1 - HORIZON)
        fill_rect(buf, M, y, W - M, y + 1, _lerp(SEA_TOP, SEA_BOT, t))

    # ...with sun glitter on the waterline and a shimmering reflection
    # column (dashes narrowing and dimming with depth).
    fill_rect(buf, scx - 6.5 * S, HORIZON, scx + 6.5 * S,
              HORIZON + 0.4 * S, (*GLITTER, 190))
    for i in range(6):
        y = HORIZON + (0.9 + 1.35 * i) * S
        half = max(0.55, 3.4 - 0.55 * i) * S
        col = _lerp((255, 186, 78), (186, 108, 44), i / 5)
        fill_rect(buf, scx - half, y, scx + half, y + 0.5 * S, (*col, 210))

    # 6) Faint wave streaks so the water isn't dead flat.
    for wx, wy, wl in [(4, 22.2, 4.5), (23, 24.2, 4.5), (6, 27.2, 3.5)]:
        fill_rect(buf, wx * S, wy * S, (wx + wl) * S, (wy + 0.35) * S,
                  (*WAVE, 160))

    round_corners(buf)
    return buf


def round_corners(buf):
    """Clip the square scene to the same rounded tile as mytetris.png."""
    for oy in (M, H - M - RAD):
        for ox in (M, W - M - RAD):
            ccx = ox + (RAD if ox == M else 0)          # arc center of this
            ccy = oy + (RAD if oy == M else 0)          # corner's quadrant
            for y in range(oy, oy + RAD):
                for x in range(ox, ox + RAD):
                    d = math.hypot(x + 0.5 - ccx, y + 0.5 - ccy)
                    if d > RAD + 0.5:
                        i = (y * W + x) * 4
                        buf[i:i + 4] = b"\x00\x00\x00\x00"
                    elif d > RAD - 0.5:                 # 1px anti-aliased rim
                        i = (y * W + x) * 4
                        buf[i + 3] = int(buf[i + 3] * (RAD + 0.5 - d))


def main():
    buf = build_scene()
    write_png(buf, "sun2set.png")
    print(f"Wrote sun2set.png ({W}x{H})")


if __name__ == "__main__":
    main()
