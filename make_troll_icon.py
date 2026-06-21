"""Generate mytetris_troll.ico — the funny "Troll Piece" icon, for Windows.

The Windows sibling of make_tetris_icon_mac.py. It reuses that module's
1024x1024 "Troll Piece" scene (the fat, smug, grinning O-piece plummeting
toward the 1-wide well you were praying would finally get its I-piece) and
packs it into a multi-resolution Windows .ico so it stays crisp everywhere:
a PNG-compressed 256x256 (Explorer's large/extra-large views and the shortcut
thumbnail) plus classic BMP 48/32/16 entries (medium icons, taskbar, title
bar). Pure standard library — no Pillow; we write the ICO/BMP/PNG bytes by
hand, same as the other two icon generators.
"""

import struct
import zlib

import make_tetris_icon_mac as mac

# BMP entries for the small sizes + a PNG entry for 256 (the modern ICO format
# allows per-entry PNG, which keeps the big slot small and sharp).
BMP_SIZES = (16, 32, 48)
PNG_SIZE = 256


def downsample(src, sw, sh, dw, dh):
    """Box-average an RGBA bytearray from (sw x sh) down to (dw x dh).

    Averages in *premultiplied* alpha so the anti-aliased rounded corners and
    block edges don't darken against the transparent background.
    """
    dst = bytearray(dw * dh * 4)
    for dy in range(dh):
        sy0 = dy * sh // dh
        sy1 = max(sy0 + 1, (dy + 1) * sh // dh)
        for dx in range(dw):
            sx0 = dx * sw // dw
            sx1 = max(sx0 + 1, (dx + 1) * sw // dw)
            r = g = b = asum = 0
            n = 0
            for sy in range(sy0, sy1):
                base = (sy * sw + sx0) * 4
                for _ in range(sx0, sx1):
                    a = src[base + 3]
                    r += src[base] * a
                    g += src[base + 1] * a
                    b += src[base + 2] * a
                    asum += a
                    n += 1
                    base += 4
            j = (dy * dw + dx) * 4
            if asum:                                   # un-premultiply
                dst[j] = min(255, r // asum)
                dst[j + 1] = min(255, g // asum)
                dst[j + 2] = min(255, b // asum)
                dst[j + 3] = asum // n
            # else: leave fully transparent
    return dst


def bmp_entry(rgba, size):
    """A classic BITMAPINFOHEADER icon image: BGRA (bottom-up) + AND mask."""
    xor = bytearray()
    for y in range(size - 1, -1, -1):
        row = (y * size) * 4
        for x in range(size):
            i = row + x * 4
            xor += bytes((rgba[i + 2], rgba[i + 1], rgba[i], rgba[i + 3]))
    # 1-bpp AND mask, rows padded to 4 bytes, all zero = "use the alpha above".
    row_bytes = ((size + 31) // 32) * 4
    and_mask = bytes(row_bytes * size)
    header = struct.pack(
        "<IiiHHIIiiII",
        40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0,
    )
    return header + xor + and_mask


def png_entry(rgba, size):
    """A PNG-compressed icon image (allowed for ICO entries since Vista)."""
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = bytearray()
    for y in range(size):
        raw.append(0)                                  # filter type 0 per row
        raw += rgba[y * size * 4:(y + 1) * size * 4]
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8-bit RGBA
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def build_ico(images):
    """Pack [(size, data), ...] into ICO bytes (ICONDIR + entries + blobs)."""
    icondir = struct.pack("<HHH", 0, 1, len(images))   # reserved, type=icon, count
    offset = 6 + 16 * len(images)
    entries = bytearray()
    blobs = bytearray()
    for size, data in images:
        dim = 0 if size >= 256 else size               # 0 in the byte means 256
        entries += struct.pack(
            "<BBBBHHII",
            dim, dim, 0, 0, 1, 32, len(data), offset,
        )
        offset += len(data)
        blobs += data
    return bytes(icondir + entries + blobs)


def main():
    scene = mac.build_scene()                          # 1024x1024 RGBA
    sw, sh = mac.W, mac.H
    images = []
    for s in BMP_SIZES:
        images.append((s, bmp_entry(downsample(scene, sw, sh, s, s), s)))
    images.append((PNG_SIZE, png_entry(downsample(scene, sw, sh, PNG_SIZE, PNG_SIZE), PNG_SIZE)))
    ico = build_ico(images)
    with open("mytetris_troll.ico", "wb") as f:
        f.write(ico)
    sizes = ", ".join(str(s) for s in (*BMP_SIZES, PNG_SIZE))
    print(f"Wrote mytetris_troll.ico ({len(ico)} bytes; sizes: {sizes})")


if __name__ == "__main__":
    main()
