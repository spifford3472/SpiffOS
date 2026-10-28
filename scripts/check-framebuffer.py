#!/usr/bin/env python3
"""Check every pixel of a QEMU P6 screendump against the boot pattern."""
from pathlib import Path
import argparse


def main():
    """Read one evidence file, fail on a malformed image or unexpected pixel row."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshot", type=Path)
    parser.add_argument("--title", action="store_true", help="expect fixed SPIFFOS lettering")
    parser.add_argument("--serial", type=Path, help="also verify captured boot markers")
    args = parser.parse_args()
    if args.serial:
        lines = args.serial.read_bytes().splitlines()
        markers = [b"SPIFFOS_BOOT_OK", b"SPIFFOS_FRAMEBUFFER_OK"]
        if args.title:
            markers.append(b"SPIFFOS_TEXT_OK")
        if any(marker not in lines for marker in markers):
            raise SystemExit("FAIL: required serial marker absent")
    # QEMU screendump emits this fixed P6 header followed by RGB byte triples.
    magic, dimensions, maximum, pixels = args.screenshot.read_bytes().split(b"\n", 3)
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
    if args.title and (width < 256 or height < 96):
        raise SystemExit("FAIL: screenshot too small for title")
    # An independent readable reference for the five fixed-label glyphs.
    font = {
        "S": ["..####..", ".##..##.", ".##.....", "..####..", ".....##.", ".##..##.", "..####..", "........"],
        "P": [".#####..", ".##..##.", ".##..##.", ".#####..", ".##.....", ".##.....", ".##.....", "........"],
        "I": ["..####..", "...##...", "...##...", "...##...", "...##...", "...##...", "..####..", "........"],
        "F": [".######.", ".##.....", ".##.....", ".#####..", ".##.....", ".##.....", ".##.....", "........"],
        "O": ["..####..", ".##..##.", ".##..##.", ".##..##.", ".##..##.", ".##..##.", "..####..", "........"]
    }
    for y in range(height):
        expected = border_row if y < 16 or y >= height - 16 else pattern_row
        if args.title and 48 <= y < 80:
            expected = bytearray(expected)
            title_row = b"".join(
                (bytes((255, 255, 255)) if bit == "#" else bytes((0, 0, 0))) * 4
                for letter in "SPIFFOS" for bit in font[letter][(y - 48) // 4]
            )
            left = (width - 224) // 2
            expected[left * 3:(left + 224) * 3] = title_row
        actual = pixels[y * width * 3:(y + 1) * width * 3]
        if actual != expected:
            raise SystemExit("FAIL: unexpected screenshot pixels in row " + str(y))
    print(f"PASS: every framebuffer pixel verified ({width}x{height}, title={args.title})")


if __name__ == "__main__":
    main()
