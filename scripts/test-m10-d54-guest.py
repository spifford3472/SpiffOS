#!/usr/bin/env python3
"""M10-D5.4 first QEMU context-switch proof in a disposable ISO/source tree.

Never rewrites the owner's working ISO or local checkout. Requires the
already-pinned local Limine dependency and the normal QEMU/OVMF toolchain.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = (
    b"SPIFFOS_IRQ0_ENABLED",
    b"SPIFFOS_BOOT_OK",
    b"SPIFFOS_M10_D54_GUEST_OK",
    b"SPIFFOS_KEYBOARD_READY",
)


def main():
    """Require stack switch, ownership cleanup and editor restoration at 3 sizes."""
    with tempfile.TemporaryDirectory(prefix="spiffos-m10-d54-") as temporary:
        source = Path(temporary) / "source"
        shutil.copytree(
            ROOT, source, symlinks=True,
            ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        environment = os.environ.copy()
        environment.update({
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "safe.directory",
            "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve()),
        })
        build = subprocess.run(
            ["make", "BOOT_TEST_FLAGS=-DSPIFFOS_M10_D54_GUEST_TEST", "iso"],
            cwd=source, env=environment, capture_output=True, timeout=120)
        if build.returncode:
            raise RuntimeError(
                "M10-D5.4 isolated ISO build failed: " +
                (build.stdout + build.stderr).decode(errors="replace")[-3500:])
        for memory_mib in (128, 256, 512):
            run = subprocess.run(
                [sys.executable, "scripts/qemu.py", "test",
                 "--memory-mib", str(memory_mib),
                 "--wait-marker", "SPIFFOS_KEYBOARD_READY",
                 "--timeout", "18"],
                cwd=source, capture_output=True, timeout=23)
            serial = (source / "build/serial.log").read_bytes()
            if run.returncode:
                raise RuntimeError(
                    f"M10-D5.4 QEMU {memory_mib}MiB failed: " +
                    (run.stdout + run.stderr).decode(errors="replace")[-2000:])
            positions = [serial.find(marker) for marker in REQUIRED]
            if (any(position < 0 for position in positions) or
                    positions != sorted(positions) or
                    b"SPIFFOS_M10_D54_GUEST_ERROR" in serial or
                    b"SPIFFOS_EXCEPTION:" in serial or
                    b"SPIFFOS_MEMORY_SERVICE_ERROR" in serial):
                raise RuntimeError(
                    f"M10-D5.4 QEMU {memory_mib}MiB serial failure: " +
                    repr(serial[-1800:]))
            print(f"PASS M10-D5.4 QEMU {memory_mib}MiB: three-stack multiwaiter context, owned-stack "
                  "cleanup, restored editor and existing IRQ0")


if __name__ == "__main__":
    main()
