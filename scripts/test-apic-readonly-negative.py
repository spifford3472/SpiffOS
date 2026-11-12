#!/usr/bin/env python3
"""Fail-closed APIC cache gate with a disposable QEMU-only negative ISO."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Force cache policy rejection, forbid register reads, keep normal boot."""
    with tempfile.TemporaryDirectory(prefix="spiffos-apic-readonly-negative-") as tmp:
        guest_source = Path(tmp) / "source"
        shutil.copytree(ROOT, guest_source, symlinks=True,
            ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({"GIT_CONFIG_COUNT": "1",
                    "GIT_CONFIG_KEY_0": "safe.directory",
                    "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve())})
        build = subprocess.run(["make", "BOOT_TEST_FLAGS=-DSPIFFOS_APIC_READONLY_BLOCK_TEST",
                                "iso"], cwd=guest_source, env=env,
                               capture_output=True, timeout=100)
        if build.returncode:
            raise RuntimeError("negative build failed: " +
                               (build.stdout + build.stderr).decode(errors="replace")[-3000:])
        run = subprocess.run([sys.executable, "scripts/qemu.py", "test",
                              "--memory-mib", "128", "--timeout", "15"],
                             cwd=guest_source, capture_output=True, timeout=25)
        assert run.returncode == 0, run.stdout[-400:]
        serial = (guest_source / "build/serial.log").read_bytes()
        required = (b"SPIFFOS_MMIO_MAPPED_OK", b"SPIFFOS_APIC_CACHE_UNVERIFIED",
                    b"SPIFFOS_APIC_REGISTERS_SKIPPED", b"SPIFFOS_BOOT_OK")
        positions = [serial.find(marker) for marker in required]
        assert all(position >= 0 and serial.count(marker) == 1
                   for marker, position in zip(required, positions)), serial
        assert positions == sorted(positions), serial
        assert b"SPIFFOS_APIC_REGISTERS_OK" not in serial
        assert b"SPIFFOS_APIC_REGISTERS_UNAVAILABLE" not in serial
        assert b"SPIFFOS_EXCEPTION:" not in serial
        print("PASS QEMU APIC cache rejection: register reads skipped, boot intact")


if __name__ == "__main__":
    main()
