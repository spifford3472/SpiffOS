#!/usr/bin/env python3
"""Check every pixel of a QEMU P6 screendump against the boot pattern."""
from pathlib import Path
import sys


def main():
    """Read one evidence file, fail on a malformed image or unexpected pixel row."""
    if len(sys.argv) != 2:
        raise SystemExit("usage: check-framebuffer.py SCREENSHOT.ppm")
    # QEMU screendump emits this fixed P6 header followed by RGB byte triples.
    magic, dimensions, maximum, pixels = Path(sys.argv[1]).read_bytes().split(b"\n", 3)
    if magic != b"P6" or maximum != b"255":
        raise SystemExit("FAIL: unsupported QEMU PPM header")
    width, height = map(int, dimensions.split())
    if width <= 32 or height <= 32 or len(pixels) != width * height * 3:
        raise SystemExit("FAIL: invalid screenshot size")
    border = bytes((160, 160, 255))
    interior = width - 32
    red_columns = interior // 3
    green_columns = interior * 2 // 3 - red_columns
    blue_columns = interior - red_columns - green_columns
    border_row = border * width
    pattern_row = (border * 16 + bytes((255, 0, 0)) * red_columns +
                   bytes((0, 255, 0)) * green_columns +
                   bytes((0, 0, 255)) * blue_columns + border * 16)
    for y in range(height):
        expected = border_row if y < 16 or y >= height - 16 else pattern_row
        actual = pixels[y * width * 3:(y + 1) * width * 3]
        if actual != expected:
            raise SystemExit("FAIL: unexpected screenshot pixels in row " + str(y))
    print(f"PASS: every framebuffer pixel verified ({width}x{height})")


if __name__ == "__main__":
    main()
