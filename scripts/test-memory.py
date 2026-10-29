#!/usr/bin/env python3
"""Recompute M1 inventory independently from real guest metadata at three sizes."""
from pathlib import Path
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PAGE = 4096


def verify_serial(serial, ram_mib):
    """Check complete guest evidence and recompute all counters with Python ints."""
    lines = serial.splitlines()
    assert lines.count(b"SPIFFOS_MEMORY_OK") == 1
    assert b"SPIFFOS_MEMORY_UNAVAILABLE" not in lines
    assert b"SPIFFOS_BOOT_OK" in lines
    assert b"SPIFFOS_KEYBOARD_READY" in lines
    regions = [tuple(map(int, line[14:].split(b","))) for line in lines
               if line.startswith(b"MEMORY_REGION:")]
    summaries = [tuple(map(int, line[15:].split(b","))) for line in lines
                 if line.startswith(b"MEMORY_SUMMARY:")]
    assert 0 < len(regions) <= 512 and len(summaries) == 1
    usable_bytes = frames = 0
    for index, (base, length, kind) in enumerate(regions):
        assert 0 <= base <= 2**64 - 1 and 0 <= length <= 2**64 - 1
        assert base + length <= 2**64 - 1
        if length == 0:
            continue
        for other_base, other_length, other_kind in regions[index+1:]:
            if other_length and (kind == 0 or other_kind == 0):
                assert base + length <= other_base or other_base + other_length <= base
        if kind == 0:
            usable_bytes += length
            first = max(1, (base + PAGE - 1) // PAGE)
            last = (base + length) // PAGE
            frames += max(0, last - first)
    expected = (usable_bytes, frames, frames * PAGE, usable_bytes - frames * PAGE)
    assert summaries[0] == expected, (summaries[0], expected)
    assert 0 < frames * PAGE <= usable_bytes < ram_mib * 1024 * 1024
    return dict(ram_mib=ram_mib, regions=len(regions), usable_bytes=usable_bytes,
                candidate_frames=frames, candidate_bytes=frames * PAGE,
                excluded_usable_bytes=expected[3])


def main():
    """Run isolated normal guests sequentially; qemu.py retains locking/cleanup."""
    build = ROOT / "build"
    results = []
    for ram_mib in (128, 256, 512):
        with (build / f"memory-{ram_mib}-run.log").open("wb") as log:
            subprocess.run([sys.executable, str(ROOT / "scripts/qemu.py"),
                            "test", "--memory-mib", str(ram_mib), "--timeout", "30"],
                           cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=40)
        serial = (build / "serial.log").read_bytes()
        (build / f"memory-{ram_mib}-serial.log").write_bytes(serial)
        result = verify_serial(serial, ram_mib)
        if results:
            assert result["candidate_frames"] > results[-1]["candidate_frames"]
        results.append(result)
        print(f"PASS memory QEMU {ram_mib}MiB: {result['candidate_frames']} candidate "
              f"frames, {result['regions']} regions, exact serial counters")
    (build / "memory-inventory-results.json").write_text(
        json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
