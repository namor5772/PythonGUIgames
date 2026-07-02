"""Generate mypockettanks.png — the macOS icon master for MyPocketTanks.

The same tiny artillery duel as mypockettanks.ico (red tank on a grassy hill
lobbing a shell into an explosion under a starry night sky), but rendered
natively at 1024x1024 instead of upscaled from 32px pixel art.

Pure standard library: reuses the software rasterizer and hand-rolled PNG
writer from make_tetris_icon_mac.py. macOS `sips`/`iconutil` then turn this
master PNG into `mypockettanks.icns` (see create_shortcut.command).
"""

import math

from make_tetris_icon_mac import (H, W, blend, fill_circle, fill_rect,
                                  new_canvas, thick_path, write_png)

S = W // 32          # one "pixel" of the 32px ico at this resolution
M, RAD = 36, 170     # tile margin / corner radius (match mytetris.png)

# Same palette as make_pockettanks_icon.py (RGB, not BGRA).
SKY_TOP = (11, 13, 30)
SKY_BOT = (34, 44, 74)
STAR = (220, 220, 245)
GRASS = (63, 174, 79)
GRASS_DARK = (47, 133, 60)
DIRT = (124, 86, 48)
DIRT_DARK = (88, 60, 34)
TANK = (239, 68, 68)
TANK_DARK = (150, 40, 40)
BARREL = (200, 200, 214)
TRAIL = (255, 217, 26)
BOOM_OUT = (255, 85, 34)
BOOM_IN = (255, 239, 122)


def terrain_y(u):
    """Surface height in 32px-ico units (u = x / S), same hills as the ico."""
    return max(14.0, min(26.0, 21 - 4 * math.sin(u * 0.18 + 0.6)
                         - 2 * math.sin(u * 0.45)))


def build_scene():
    buf = new_canvas()

    # 1) Night-sky gradient over the whole tile (corners get masked at the end).
    for y in range(M, H - M):
        t = (y - M) / (H - 2 * M)
        col = tuple(int(SKY_TOP[k] + (SKY_BOT[k] - SKY_TOP[k]) * t)
                    for k in range(3))
        fill_rect(buf, M, y, W - M, y + 1, col)

    # 2) Stars — the ico's six, plus a faint halo so they read at desktop size.
    for sx, sy in [(3, 2), (9, 5), (14, 1), (20, 4), (27, 2), (30, 7)]:
        cx, cy = (sx + 0.5) * S, (sy + 0.5) * S
        fill_circle(buf, cx, cy, S * 0.42, (*STAR, 70))
        fill_circle(buf, cx, cy, S * 0.20, STAR)

    # 3) Terrain: per-column heightmap (grass lip, dirt body, darker bedrock).
    for x in range(M, W - M):
        sy = int(terrain_y((x + 0.5) / S) * S)
        fill_rect(buf, x, sy, x + 1, min(sy + S, H - M), GRASS)
        fill_rect(buf, x, sy + S, x + 1, min(sy + 2 * S, H - M), GRASS_DARK)
        fill_rect(buf, x, sy + 2 * S, x + 1, min(sy + 7 * S, H - M), DIRT)
        fill_rect(buf, x, sy + 7 * S, x + 1, H - M, DIRT_DARK)

    # 4) The red tank on the left hill: treads with wheels, hull, dome, barrel.
    tcx = 7.5 * S
    gy = terrain_y(7.5) * S
    fill_circle(buf, tcx, gy - 2.6 * S, 1.35 * S, TANK)               # dome
    thick_path(buf, [(tcx, gy - 2.9 * S), (10.5 * S, 11.5 * S)],
               0.55 * S, BARREL)                                      # barrel
    fill_rect(buf, tcx - 3.5 * S, gy - 2.1 * S, tcx + 3.5 * S,
              gy - 0.9 * S, TANK)                                     # hull
    thick_path(buf, [(tcx - 2.9 * S, gy - 0.55 * S),
                     (tcx + 2.9 * S, gy - 0.55 * S)], 1.1 * S,
               TANK_DARK)                                             # treads
    for i in range(5):
        fill_circle(buf, tcx + (i - 2) * 1.3 * S, gy - 0.55 * S,
                    0.28 * S, (60, 16, 16))                           # wheels

    # 5) Dotted shell arc: golden tracer dots along the ico's trajectory.
    pts = [(12.5, 12.5), (15.5, 9.5), (18.5, 8.5), (21.5, 9.5), (23.8, 11.2)]
    for i in range(len(pts) - 1):
        (x0, y0), (x1, y1) = pts[i], pts[i + 1]
        for t in (0.0, 0.5):
            cx, cy = (x0 + (x1 - x0) * t) * S, (y0 + (y1 - y0) * t) * S
            fill_circle(buf, cx, cy, 0.42 * S, (*TRAIL, 80))          # glow
            fill_circle(buf, cx, cy, 0.24 * S, TRAIL)

    # 6) The explosion on the right slope: hot core, fire ball, sparks.
    ex, ey = 26.5 * S, (terrain_y(26.5) - 1.5) * S
    fill_circle(buf, ex, ey, 3.8 * S, (*BOOM_OUT, 70))                # halo
    fill_circle(buf, ex, ey, 3.0 * S, BOOM_OUT)
    fill_circle(buf, ex, ey, 1.7 * S, BOOM_IN)
    fill_circle(buf, ex, ey, 0.7 * S, (255, 255, 235))
    for k in range(8):                                                # sparks
        a = k * math.pi / 4 + 0.4
        fill_circle(buf, ex + 3.6 * S * math.cos(a),
                    ey + 3.6 * S * math.sin(a), 0.30 * S, BOOM_OUT)

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
    write_png(buf, "mypockettanks.png")
    print(f"Wrote mypockettanks.png ({W}x{H})")


if __name__ == "__main__":
    main()
