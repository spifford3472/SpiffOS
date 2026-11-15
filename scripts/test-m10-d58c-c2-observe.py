#!/usr/bin/env python3
"""C2a genuine IRQ0 observation of priority donation + real BLOCKED waiter.

The diagnostic uses a fixed cooperative D5.4 bootstrap to resume a high
waiter's original saved RET continuation. It does NOT perform IRQ handoff;
genuine IRQ0 frame observation is not the same as IRQ task switching.
The source/ISO/OVMF images are disposable; QEMU -net none, no disks.
References: AGENTS.md, ADR-m10-d58c-c2-blocked-irq-observation.md.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ("-DSPIFFOS_M10_D54_GUEST_TEST "
         "-DSPIFFOS_M10_D58B_BLOCK_HANDOFF_TEST "
         "-DSPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST")
REQUIRED = (
    b"SPIFFOS_IRQ0_ENABLED",
    b"SPIFFOS_BOOT_OK",
    b"SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_OK",
    b"SPIFFOS_M10_D54_GUEST_OK",
    b"SPIFFOS_KEYBOARD_READY",
)


def main():
    """Reject absent hardware IRQs, wrong order or incomplete worker wake."""
    with tempfile.TemporaryDirectory(prefix="spiffos-d58c-c2-") as temporary:
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
            ["make", "-B", f"BOOT_TEST_FLAGS={FLAGS}", "iso"],
            cwd=source, env=env, capture_output=True, timeout=180)
        if build.returncode:
            raise RuntimeError(
                "C2a flagged QEMU build failed:\n" +
                (build.stdout+build.stderr).decode(errors="replace")[-6500:])

        for mib in (128, 256, 512):
            run = subprocess.run(
                [sys.executable, "scripts/qemu.py", "test",
                 "--memory-mib", str(mib), "--wait-marker",
                 "SPIFFOS_KEYBOARD_READY", "--timeout", "24"],
                cwd=source, capture_output=True, timeout=31)
            serial = (source / "build/serial.log").read_bytes()
            positions = [serial.find(marker) for marker in REQUIRED]
            if (run.returncode or any(pos < 0 for pos in positions) or
                    positions != sorted(positions) or
                    b"SPIFFOS_M10_D54_GUEST_ERROR" in serial or
                    b"SPIFFOS_EXCEPTION:" in serial):
                raise RuntimeError(
                    f"C2a QEMU {mib}MiB failed: {serial[-2000:]!r}; " +
                    (run.stdout+run.stderr).decode(errors="replace")[-900:])
            found = re.search(
                rb"(?m)^D58C_C2_DONATED_IRQ_FRAMES:([0-9]+)\r?$", serial)
            if not found or int(found.group(1)) < 3:
                raise RuntimeError(
                    f"C2a {mib}MiB missing real IRQ donor evidence: "
                    f"{serial[-900:]!r}")
            print(
                f"PASS C2a QEMU {mib}MiB: "
                f"{int(found.group(1))} genuine IRQ0 donated-owner "
                "frame observations, high waiter BLOCKED, medium READY, "
                "real IF-off blocked-stack detach, lock transfer/wake, "
                "saved continuation and editor restored",
                flush=True,
            )


if __name__ == "__main__":
    main()
