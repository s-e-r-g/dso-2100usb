#!/usr/bin/env python3
"""Writes the small arrow images of the application stylesheet to src/gui/icons/.

Plain PNGs: Qt's SVG plugin would be one more DLL to ship for two triangles.
"""

import pathlib
import struct
import zlib

WIDTH, HEIGHT = 10, 6
COLOR = (0xAA, 0xB1, 0xBA)
OVERSAMPLE = 8


def coverage(x, y):
    """Share of pixel (x, y) inside a triangle pointing down."""
    hits = 0
    for sy in range(OVERSAMPLE):
        for sx in range(OVERSAMPLE):
            px = x + (sx + 0.5) / OVERSAMPLE
            py = y + (sy + 0.5) / OVERSAMPLE
            half = (WIDTH / 2) * (1 - py / HEIGHT)
            hits += abs(px - WIDTH / 2) <= half
    return hits / OVERSAMPLE**2


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))


def png(flip):
    rows = b""
    for y in range(HEIGHT):
        row = b"\0"  # no filter
        for x in range(WIDTH):
            alpha = coverage(x, HEIGHT - 1 - y if flip else y)
            row += bytes(COLOR) + bytes([round(alpha * 255)])
        rows += row
    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)  # 8 bit RGBA
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def main():
    out = pathlib.Path(__file__).resolve().parent.parent / "src" / "gui" / "icons"
    out.mkdir(exist_ok=True)
    (out / "arrow-down.png").write_bytes(png(False))
    (out / "arrow-up.png").write_bytes(png(True))


if __name__ == "__main__":
    main()
