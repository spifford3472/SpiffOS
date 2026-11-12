#!/usr/bin/env python3
"""Prove a failed APIC page-table allocation leaves no live MMIO mapping.

The deliberate fault is built ONLY in a disposable source/ISO copy. The
normal SpiffOS ISO, owner checkout, physical disks and network are untouched.
"""

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Inject failure on the second page-table frame; require clean boot.

    The first frame must be returned, the CR3 root must remain unmodified,
    and the existing M6 retained-memory service must continue to validate.
    The successful normal MMIO mapping marker must never appear.
    """
    with tempfile.TemporaryDirectory(prefix="spiffos-mmio-fault-") as temp:
        source = Path(temp) / "source"
        shutil.copytree(
            ROOT, source, symlinks=True,
            ignore=shutil.ignore_patterns(".git", "build", "__pycache__")
        )
        env = os.environ.copy()
        env.update({
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "safe.directory",
            "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve()),
        })
        result = subprocess.run(
            ["make", "BOOT_TEST_FLAGS=-DSPIFFOS_MMIO_FAULT_SECOND_ALLOCATION",
             "iso"],
            cwd=source, env=env, capture_output=True, timeout=90
        )
        if result.returncode:
            raise RuntimeError(
                "MMIO fault ISO build failed: " +
                (result.stdout + result.stderr).decode(errors="replace")[-3000:]
            )
        guest = subprocess.run(
            [sys.executable, "scripts/qemu.py", "test", "--memory-mib", "128",
             "--timeout", "15"],
            cwd=source, capture_output=True, timeout=25
        )
        assert guest.returncode == 0, guest.stdout[-500:]
        serial = (source / "build/serial.log").read_bytes()
        required = (
            b"SPIFFOS_MMIO_POLICY_OK",
            b"SPIFFOS_MMIO_ROOT_READY",
            b"SPIFFOS_MMIO_MAPPING_UNAVAILABLE",
            b"SPIFFOS_MEMORY_RETAINED_OK",
            b"SPIFFOS_BOOT_OK",
        )
        positions = [serial.find(marker) for marker in required]
        assert all(pos >= 0 and serial.count(marker) == 1
                   for pos, marker in zip(positions, required)), serial
        assert positions == sorted(positions), serial
        assert b"SPIFFOS_MMIO_MAPPED_OK" not in serial, serial
        assert b"SPIFFOS_MMIO_ROLLBACK_FATAL" not in serial, serial
        assert b"SPIFFOS_EXCEPTION" not in serial, serial
        print("PASS QEMU MMIO failure injection: no mapping, rollback, normal boot")


if __name__ == "__main__":
    main()
