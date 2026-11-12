#!/usr/bin/env python3
"""Prove that a poisoned kernel RSP reaches the double-fault IST in real QEMU.

Only a disposable copied source tree compiles the deliberate test fault. The
normal ISO, owner checkout, and physical devices remain unchanged.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Build, boot, and require one #DF panic before any normal boot marker."""
    with tempfile.TemporaryDirectory(prefix="spiffos-df-") as temp:
        source = Path(temp) / "source"
        shutil.copytree(ROOT, source, symlinks=True,
                        ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "safe.directory",
            "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve()),
        })
        result = subprocess.run(
            ["make", "BOOT_TEST_FLAGS=-DSPIFFOS_DOUBLE_FAULT_TEST", "iso"],
            cwd=source, env=env, capture_output=True, timeout=90)
        if result.returncode:
            raise RuntimeError("test ISO failed: " +
                               (result.stdout + result.stderr).decode(errors="replace")[-3000:])
        guest = subprocess.run(
            [sys.executable, "scripts/qemu.py", "test", "--memory-mib", "128",
             "--timeout", "15"],
            cwd=source, capture_output=True, timeout=25)
        serial = (source / "build/serial.log").read_bytes()
        markers = [b"SPIFFOS_IDT_READY", b"SPIFFOS_DOUBLE_FAULT_TEST_TRIGGER",
                   b"SPIFFOS_EXCEPTION:08,0000000000000000",
                   b"SPIFFOS_EXCEPTION_HALT"]
        assert guest.returncode == 1, (guest.returncode, guest.stdout[-200:])
        positions = [serial.find(marker) for marker in markers]
        assert all(p >= 0 and serial.count(m) == 1 for p, m in zip(positions, markers)), serial
        assert positions == sorted(positions), serial
        assert b"SPIFFOS_BOOT_OK" not in serial, serial
        print("PASS QEMU #DF: poisoned RSP uses IST1, exact vector/error, fatal halt")


if __name__ == "__main__":
    main()
