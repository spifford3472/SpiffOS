#!/usr/bin/env python3
"""M10-C isolated QEMU proof: IRQ accounting and non-IRQ reschedule request.

Builds the special diagnostic ISO in a disposable checkout; never overwrites
the owner's existing ISO, boot media or Forge checkout.
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
    b"SPIFFOS_KEYBOARD_READY",
    b"SPIFFOS_M10_C_REQUEST_OK",
)


def main():
    """Require ordered IRQ activation, keyboard safe point and request signal."""
    with tempfile.TemporaryDirectory(prefix="spiffos-m10-c-") as temporary:
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
            ["make", "BOOT_TEST_FLAGS=-DSPIFFOS_M10_C_REQUEST_TEST", "iso"],
            cwd=source, env=env, capture_output=True, timeout=120)
        if build.returncode:
            raise RuntimeError(
                "M10-C isolated ISO build failed: " +
                (build.stdout + build.stderr).decode(errors="replace")[-3500:])
        for mib in (128, 256, 512):
            run = subprocess.run(
                [sys.executable, "scripts/qemu.py", "test",
                 "--memory-mib", str(mib),
                 "--wait-marker", "SPIFFOS_M10_C_REQUEST_OK",
                 "--timeout", "18"],
                cwd=source, capture_output=True, timeout=23)
            serial = (source / "build/serial.log").read_bytes()
            if run.returncode:
                raise RuntimeError(
                    f"M10-C guest {mib}MiB failed: " +
                    (run.stdout + run.stderr).decode(errors="replace")[-2000:])
            positions = [serial.find(marker) for marker in MARKERS]
            if (any(position < 0 for position in positions) or
                    positions != sorted(positions) or
                    b"SPIFFOS_M10_C_REQUEST_ERROR" in serial or
                    b"SPIFFOS_EXCEPTION:" in serial or
                    b"SPIFFOS_MEMORY_SERVICE_ERROR" in serial):
                raise RuntimeError(
                    f"M10-C QEMU {mib}MiB serial failure: " +
                    repr(serial[-1800:]))
            print(f"PASS M10-C QEMU {mib}MiB: IRQ0 accounted, "
                  "request consumed at keyboard safe point, no preemption")


if __name__ == "__main__":
    main()
