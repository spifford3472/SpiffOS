#!/usr/bin/env python3
"""M10-D5.8b isolated IF=0 real x86 BLOCKED-stack handoff proof.

The extra diagnostic flag retains D5.4 ordinary behavior unchanged. A
disposable source/ISO/OVMF copy uses QEMU -net none with no physical drives.
References: AGENTS.md, ADR-m10-d58b-frame-ownership-gate.md.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ("-DSPIFFOS_M10_D54_GUEST_TEST "
         "-DSPIFFOS_M10_D58B_BLOCK_HANDOFF_TEST")
REQUIRED = (
    b"SPIFFOS_IRQ0_ENABLED",
    b"SPIFFOS_BOOT_OK",
    b"SPIFFOS_M10_D54_GUEST_OK",
    b"SPIFFOS_M10_D58B_BLOCK_HANDOFF_OK",
    b"SPIFFOS_KEYBOARD_READY",
)


def main():
    """Require independent actual RSP+IF witnesses for two BLOCKED tasks."""
    with tempfile.TemporaryDirectory(prefix="spiffos-d58b-block-") as tmp:
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
        build = subprocess.run(
            ["make", "-B", f"BOOT_TEST_FLAGS={FLAGS}", "iso"],
            cwd=source, env=env, capture_output=True, timeout=150)
        if build.returncode:
            raise RuntimeError(
                "D5.8b blocked-switch fixture failed to compile: " +
                (build.stdout + build.stderr).decode(errors="replace")[-5200:])

        for mib in (128, 256, 512):
            run = subprocess.run(
                [sys.executable, "scripts/qemu.py", "test",
                 "--memory-mib", str(mib), "--wait-marker",
                 "SPIFFOS_KEYBOARD_READY", "--timeout", "22"],
                cwd=source, capture_output=True, timeout=27)
            serial = (source / "build/serial.log").read_bytes()
            positions = [serial.find(marker) for marker in REQUIRED]
            if (run.returncode or any(pos < 0 for pos in positions) or
                    positions != sorted(positions) or
                    b"SPIFFOS_M10_D54_GUEST_ERROR:" in serial or
                    b"SPIFFOS_EXCEPTION:" in serial):
                raise RuntimeError(
                    f"D5.8b QEMU {mib}MiB invalid blocked transfer: " +
                    repr(serial[-2000:]) + " " +
                    (run.stdout + run.stderr).decode(errors="replace")[-900:])
            print(
                f"PASS D5.8b QEMU {mib}MiB: two real IF=0 blocked "
                "CPU-stack departures to surviving dispatcher; exact "
                "M10-A/D5.3 waiter refs, wake/resume, editor preserved",
                flush=True,
            )


if __name__ == "__main__":
    main()
