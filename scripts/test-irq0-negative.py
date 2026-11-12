#!/usr/bin/env python3
"""Build a disposable QEMU variant which deliberately never unmasks IRQ0."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Prove the clock stays unavailable with IF=0 and normal boot works."""
    with tempfile.TemporaryDirectory(prefix="spiffos-irq0-disabled-") as temporary:
        source = Path(temporary) / "source"
        shutil.copytree(ROOT, source, symlinks=True,
                        ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "safe.directory",
            "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve()),
        })
        build = subprocess.run(
            ["make", "BOOT_TEST_FLAGS=-DSPIFFOS_IRQ0_DISABLED_TEST", "iso"],
            cwd=source, env=env, capture_output=True, timeout=100)
        if build.returncode:
            raise RuntimeError("disabled ISO failed: " +
                               (build.stdout+build.stderr).decode(errors="replace")[-3000:])
        run = subprocess.run(
            [sys.executable, "scripts/qemu.py", "test", "--memory-mib",
             "128", "--timeout", "15"],
            cwd=source, capture_output=True, timeout=25)
        assert run.returncode == 0, run.stdout[-1000:]
        evidence = (source / "build/serial.log").read_bytes()
        expected = (
            b"SPIFFOS_APIC_REGISTERS_OK",
            b"SPIFFOS_IRQ0_DISABLED_TEST",
            b"SPIFFOS_IRQ0_DISABLED_CLOCK_UNAVAILABLE",
            b"SPIFFOS_BOOT_OK",
        )
        pos = [evidence.find(marker) for marker in expected]
        assert all(p >= 0 for p in pos) and pos == sorted(pos), evidence
        assert b"SPIFFOS_IRQ0_ENABLED" not in evidence
        assert b"SPIFFOS_IRQ0_TICK_100" not in evidence
        assert b"SPIFFOS_EXCEPTION:" not in evidence
        print("PASS QEMU disabled IRQ0: IF=0, clock unavailable, normal boot")


if __name__ == "__main__":
    main()
