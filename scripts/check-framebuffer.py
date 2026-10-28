#!/usr/bin/env python3
"""Check every pixel of a QEMU P6 screendump against the boot pattern."""
from pathlib import Path
import argparse
import json


def main():
    """Read one evidence file, fail on a malformed image or unexpected pixel row."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshot", type=Path)
    parser.add_argument("--title", action="store_true", help="expect fixed SPIFFOS lettering")
    parser.add_argument("--serial", type=Path, help="also verify captured boot markers")
    parser.add_argument("--console", action="store_true", help="expect the boot console demonstration")
    args = parser.parse_args()
    if args.console and not args.title:
        parser.error("--console requires --title")
    if args.serial:
        lines = args.serial.read_bytes().splitlines()
        markers = [b"SPIFFOS_BOOT_OK", b"SPIFFOS_FRAMEBUFFER_OK"]
        if args.title:
            markers.append(b"SPIFFOS_TEXT_OK")
        if args.console:
            markers.append(b"SPIFFOS_CONSOLE_OK")
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
    if args.console:
        if width < 80 or height < 160:
            raise SystemExit("FAIL: screenshot too small for console")
        columns = (width - 64) // 16
        rows = (height - 144) // 16
        console_grid = [[" "] * columns for _ in range(rows)]
        messages = ("SPIFFOS BOOT CONSOLE\n"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
                    "0123456789 .:?\n"
                    "TEXT ADVANCES THE CURSOR.\n"
                    "NEWLINES AND WRAPPING ARE ACTIVE.")
        cursor_row, cursor_column = 0, 0
        for character in messages:
            if cursor_row >= rows:
                raise SystemExit("FAIL: console demonstration does not fit")
            if character == "\n":
                cursor_row += 1
                cursor_column = 0
            else:
                console_grid[cursor_row][cursor_column] = character
                cursor_column += 1
                if cursor_column == columns:
                    cursor_column = 0
                    cursor_row += 1
        reference_path = Path(__file__).resolve().parents[1] / "tests/fixtures/console-font.json"
        console_font = json.loads(reference_path.read_text())
        paper, ink = bytes((32, 32, 96)), bytes((160, 160, 255))
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
        if args.console and 112 <= y < 112 + rows * 16:
            expected = bytearray(expected)
            character_row = (y - 112) // 16
            glyph_row = ((y - 112) % 16) // 2
            # Compare a screen row against the independent stored font fixture.
            # Each reference bit expands horizontally to two RGB pixels.
            console_row = b"".join(
                (ink if bit == "1" else paper) * 2
                for character in console_grid[character_row]
                for bit in ("0" + format(console_font[character][glyph_row], "05b") + "00"
                            if glyph_row < 7 else "00000000")
            )
            expected[32 * 3:(32 + columns * 16) * 3] = console_row
        actual = pixels[y * width * 3:(y + 1) * width * 3]
        if actual != expected:
            raise SystemExit("FAIL: unexpected screenshot pixels in row " + str(y))
    print(f"PASS: every framebuffer pixel verified ({width}x{height}, title={args.title}, console={args.console})")


if __name__ == "__main__":
    main()
