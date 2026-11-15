#!/usr/bin/env python3
"""Real IRQ0 C2b failure injection: reject before unsafe IRETQ handoff.

Faults are introduced only in a separately flagged disposable QEMU guest
or as a stale M10-A target-generation witness. No physical frames or locks
are repaired in IRQ, no unknown worker stacks are released on fault.
References: AGENTS.md, ADR-m10-d58c-c2b-real-irq-bridge.md.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = "-DSPIFFOS_M10_D58C_C2B_REAL_IRQ_TEST"
FAULTS = (
    ("source", "-DSPIFFOS_M10_D58C_C2B_BAD_SOURCE_TEST",
     "UNSAFE_IRQ_SOURCE"),
    ("graph", "-DSPIFFOS_M10_D58C_C2B_BAD_GRAPH_TEST",
     "UNSAFE_IRQ_SOURCE"),
    ("guard", "-DSPIFFOS_M10_D58C_C2B_BAD_GUARD_TEST",
     "IRQ_GUARD_DENIED"),
    ("target", "-DSPIFFOS_M10_D58C_C2B_BAD_TARGET_TEST",
     "BAD_TARGET_FRAME_DENIED"),
    ("generation", "-DSPIFFOS_M10_D58C_C2B_BAD_GENERATION_TEST",
     "STALE_TARGET_DENIED"),
)


def main():
    """Reject each separately compiled bad source or target on real IRQ."""
    with tempfile.TemporaryDirectory(prefix="spiffos-c2b-negative-") as tmp:
        source = Path(tmp) / "source"
        shutil.copytree(
            ROOT, source, symlinks=True,
            ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "safe.directory",
            "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve()),
        })
        for name, flag, reason in FAULTS:
            build = subprocess.run(
                ["make", "-B", f"BOOT_TEST_FLAGS={BASE} {flag}", "iso"],
                cwd=source, env=env, capture_output=True, timeout=180)
            if build.returncode:
                raise RuntimeError(
                    f"C2b negative {name} build failed: " +
                    (build.stdout+build.stderr).decode(errors="replace")[-5000:])
            marker=f"SPIFFOS_M10_D58C_C2B_IRQ_ERROR:{reason}"
            for mib in (128, 256, 512):
                run = subprocess.run(
                    [sys.executable, "scripts/qemu.py", "test",
                     "--memory-mib", str(mib), "--wait-marker", marker,
                     "--timeout", "24"],
                    cwd=source, capture_output=True, timeout=30)
                serial = (source / "build/serial.log").read_bytes()
                if (run.returncode or marker.encode() not in serial or
                        b"SPIFFOS_M10_D58C_C2B_IRQ_OK" in serial or
                        b"SPIFFOS_KEYBOARD_READY" in serial or
                        b"SPIFFOS_EXCEPTION:" in serial):
                    raise RuntimeError(
                        f"C2b negative {name} {mib}MiB failed closed: "
                        f"{serial[-1700:]!r} " +
                        (run.stdout+run.stderr).decode(errors="replace")[-600:])
                print(
                    f"PASS C2b real IRQ negative {name} QEMU {mib}MiB: "
                    f"{reason}, no unsafe IRETQ and no editor boot",
                    flush=True,
                )


if __name__ == "__main__":
    main()
