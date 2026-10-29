#!/usr/bin/env python3
"""Verify fixed boot-logo provenance and emit a kernel C byte array."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Reject stale/corrupt assets before generating build/boot-logo-data.h."""
    prefix = ROOT / "assets/branding/spiffos-boot-logo"
    info = json.loads(prefix.with_suffix(".json").read_text())
    data = prefix.with_suffix(".rgb").read_bytes()
    if (info["width"], info["height"]) != (400, 160) or len(data) != 400 * 160 * 3:
        raise SystemExit("Invalid boot logo dimensions/byte count")
    source = ROOT / "assets/branding/spiffos-logo.svg"
    if hashlib.sha256(source.read_bytes()).hexdigest() != info["source_sha256"]:
        raise SystemExit("Original SVG changed: regenerate boot logo")
    if hashlib.sha256(data).hexdigest() != info["rgb_sha256"]:
        raise SystemExit("Boot RGB checksum mismatch")
    target = ROOT / "build/boot-logo-data.h"
    target.parent.mkdir(exist_ok=True)
    with target.open("w") as out:
        out.write("/* Generated bitmap bytes. Purpose: embed verified original logo; no runtime decoder. */\n")
        out.write("#define BOOT_LOGO_WIDTH 400u\n#define BOOT_LOGO_HEIGHT 160u\n")
        out.write("static const uint8_t boot_logo_rgb[] = {\n")
        for start in range(0, len(data), 24):
            out.write("    " + ",".join(str(value) for value in data[start:start+24]) + ",\n")
        out.write("};\n")


if __name__ == "__main__":
    main()
