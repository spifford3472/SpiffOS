#!/usr/bin/env python3
"""Check every pixel of a QEMU P6 screendump against the boot pattern."""
from pathlib import Path
import argparse
import json
import hashlib


def main():
    """Read one evidence file, fail on a malformed image or unexpected pixel row."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshot", type=Path)
    parser.add_argument("--title", action="store_true", help="expect fixed SPIFFOS lettering")
    parser.add_argument("--serial", type=Path, help="also verify captured boot markers")
    parser.add_argument("--console", action="store_true", help="expect the boot console demonstration")
    parser.add_argument("--powerup", action="store_true", help="expect original logo and forced scrolling")
    parser.add_argument("--workspace", action="store_true", help="expect full-screen initial logo content")
    parser.add_argument("--scrolled", action="store_true", help="expect logo gone after forced scrolling")
    args = parser.parse_args()
    if args.workspace and not args.powerup:
        parser.error("--workspace requires --powerup")
    if args.scrolled and not args.workspace:
        parser.error("--scrolled requires --workspace")
    if args.console and not (args.title or args.powerup):
        parser.error("--console requires --title or --powerup")
    if args.powerup and not args.console:
        parser.error("--powerup requires --console")
    if args.serial:
        lines = args.serial.read_bytes().splitlines()
        markers = [b"SPIFFOS_BOOT_OK", b"SPIFFOS_FRAMEBUFFER_OK"]
        if args.title:
            markers.append(b"SPIFFOS_TEXT_OK")
        if args.console:
            markers.append(b"SPIFFOS_CONSOLE_OK")
        if args.powerup:
            markers.extend([b"SPIFFOS_TEXT_OK", b"SPIFFOS_LOGO_OK"])
            if not args.workspace or args.scrolled:
                markers.append(b"SPIFFOS_SCROLL_OK")
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
    if args.powerup:
        if width < 432 or height < 264:
            raise SystemExit("FAIL: screenshot too small for power-up layout")
        asset = Path(__file__).resolve().parents[1] / "assets/branding/spiffos-boot-logo"
        logo = asset.with_suffix(".rgb").read_bytes()
        provenance = json.loads(asset.with_suffix(".json").read_text())
        if len(logo) != 400*160*3 or hashlib.sha256(logo).hexdigest() != provenance["rgb_sha256"]:
            raise SystemExit("FAIL: corrupt reference logo asset")
    if args.console:
        console_top = 0 if args.workspace else (216 if args.powerup else 112)
        if width < 80 or height < console_top + 48:
            raise SystemExit("FAIL: screenshot too small for console")
        columns = (width - (0 if args.workspace else 64)) // 16
        rows = (height - console_top - (0 if args.workspace else 32)) // 16
        console_grid = [[None if args.workspace and not args.scrolled else " "] * columns for _ in range(rows)]
        messages = ("SPIFFOS BOOT CONSOLE\n"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
                    "0123456789 .:?\n"
                    "TEXT ADVANCES THE CURSOR.\n"
                    "NEWLINES AND WRAPPING ARE ACTIVE.")
        if args.powerup:
            # The forced scroll leaves the final rows-1 numbered lines and label.
            messages = "\n".join([f"LINE {i}" for i in range(3, rows+2)] + ["SCROLLING OK."])
        if args.workspace and not args.scrolled:
            messages = "SPIFFOS WORKSPACE\nOUTPUT SCROLLS THE LOGO."
        cursor_row, cursor_column = (13 if args.workspace and not args.scrolled else 0), 0
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
        paper, ink = (bytes((9, 27, 53)) if args.workspace else bytes((32, 32, 96))), bytes((160, 160, 255))
    for y in range(height):
        expected = border_row if y < 16 or y >= height - 16 else pattern_row
        if args.powerup:
            navy = bytes((9, 27, 53))
            expected = (navy * width if args.workspace else
                        (border_row if y < 16 or y >= height - 16 else border*16 + navy*(width-32) + border*16))
            if 24 <= y < 184 and not (args.workspace and args.scrolled):
                expected = bytearray(expected)
                left = (width - 400) // 2
                expected[left*3:(left+400)*3] = logo[(y-24)*400*3:(y-23)*400*3]
        if args.title and 48 <= y < 80:
            expected = bytearray(expected)
            title_row = b"".join(
                (bytes((255, 255, 255)) if bit == "#" else bytes((0, 0, 0))) * 4
                for letter in "SPIFFOS" for bit in font[letter][(y - 48) // 4]
            )
            left = (width - 224) // 2
            expected[left * 3:(left + 224) * 3] = title_row
        if args.console and console_top <= y < console_top + rows * 16:
            expected = bytearray(expected)
            character_row = (y - console_top) // 16
            glyph_row = ((y - console_top) % 16) // 2
            # Compare a screen row against the independent stored font fixture.
            # Each reference bit expands horizontally to two RGB pixels.
            if args.workspace:
                for column, character in enumerate(console_grid[character_row]):
                    if character is None:
                        continue  # initial logo pixels remain ordinary untouched contents
                    bits = ("0" + format(console_font[character][glyph_row], "05b") + "00"
                            if glyph_row < 7 else "00000000")
                    cell = b"".join((ink if bit == "1" else paper) * 2 for bit in bits)
                    expected[column*16*3:(column+1)*16*3] = cell
            else:
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
    print(f"PASS: every framebuffer pixel verified ({width}x{height}, title={args.title}, console={args.console}, powerup={args.powerup}, workspace={args.workspace}, scrolled={args.scrolled})")


if __name__ == "__main__":
    main()
