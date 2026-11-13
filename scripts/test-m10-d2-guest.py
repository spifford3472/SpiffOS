#!/usr/bin/env python3
"""M10-D2 real-IRQ full-frame preservation proof in disposable QEMU source.

This test compiles a test-only IRQ observation hook, then proves multiple
real 100Hz PIT0 interrupts preserve all 15 GPRs and long-mode hardware
RIP/CS/RFLAGS/RSP/SS while a non-yielding assembly sentinel runs. It never
modifies the installed owner ISO, Linux disk, or network configuration.
References: AGENTS.md and ADR-m10-d-preemption-proposal.md.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MARKERS = (
    b"SPIFFOS_IRQ0_ENABLED",
    b"SPIFFOS_BOOT_OK",
    b"SPIFFOS_M10_D2_FRAME_OK",
    b"SPIFFOS_KEYBOARD_READY",
)


def main():
    """Build/boot a private D2 diagnostic ISO, never an owner artifact."""
    with tempfile.TemporaryDirectory(prefix="spiffos-m10-d2-") as temporary:
        source = Path(temporary) / "source"
        shutil.copytree(
            ROOT, source, symlinks=True,
            ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "safe.directory",
            "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve()),
        })
        build = subprocess.run(
            ["make", "BOOT_TEST_FLAGS=-DSPIFFOS_M10_D2_IRQ_FRAME_TEST", "iso"],
            cwd=source, env=env, capture_output=True, timeout=120)
        if build.returncode:
            raise RuntimeError(
                "M10-D2 isolated ISO compilation failed: " +
                (build.stdout + build.stderr).decode(errors="replace")[-4500:])
        for mib in (128, 256, 512):
            run = subprocess.run(
                [sys.executable, "scripts/qemu.py", "test",
                 "--memory-mib", str(mib),
                 "--wait-marker", "SPIFFOS_M10_D2_FRAME_OK",
                 "--timeout", "18"],
                cwd=source, capture_output=True, timeout=23)
            serial = (source / "build/serial.log").read_bytes()
            if run.returncode:
                raise RuntimeError(
                    f"M10-D2 QEMU {mib} MiB failure: " +
                    (run.stdout + run.stderr).decode(errors="replace")[-2200:] +
                    "\nserial tail: " + repr(serial[-1400:]))
            positions = [serial.find(marker) for marker in MARKERS[:3]]
            if (any(position < 0 for position in positions) or
                    positions != sorted(positions) or
                    b"SPIFFOS_M10_D2_FRAME_ERROR" in serial or
                    b"SPIFFOS_EXCEPTION:" in serial):
                raise RuntimeError(
                    f"M10-D2 {mib} MiB error: " + repr(serial[-2000:]))
            print(
                f"PASS M10-D2 QEMU {mib}MiB: real IRQ0 frame, all GPRs, "
                "saved RIP/CS/RFLAGS/RSP/SS and IRETQ", flush=True)


if __name__ == "__main__":
    main()
