#!/usr/bin/env python3
"""QEMU-only IRQ0 positive and disabled-source negative integration tests.

No physical devices, network, production ISO or owner checkout are touched.
The negative test rebuilds in a temporary source copy with a compile-time
disabled IRQ0; it must fail the exact same real-QEMU cadence gate.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def invoke_timer_test(root, timeout_seconds):
    """Return QEMU runner process result; always use a finite deadline."""
    return subprocess.run(
        [sys.executable, "scripts/qemu.py", "test", "--irq-timer",
         "--memory-mib", "128", "--timeout", str(timeout_seconds)],
        cwd=root, capture_output=True, timeout=timeout_seconds + 10,
        check=False)


def main():
    """Prove timer progress in live guest, then prove masked IRQ fails closed."""
    positive = invoke_timer_test(ROOT, 12)
    if positive.returncode:
        raise AssertionError("IRQ0 normal QEMU failed:\n" +
                             (positive.stdout + positive.stderr).decode(errors="replace")[-3000:])
    print("PASS IRQ0 positive: 100/200 tick real-QEMU cadence")

    with tempfile.TemporaryDirectory(prefix="spiffos-irq0-masked-") as temp:
        isolated = Path(temp) / "source"
        shutil.copytree(ROOT, isolated, symlinks=True,
                        ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "safe.directory",
            "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve())
        })
        built = subprocess.run(
            ["make", "BOOT_TEST_FLAGS=-DSPIFFOS_IRQ0_MASKED_TEST", "iso"],
            cwd=isolated, env=env, capture_output=True, timeout=90,
            check=False)
        if built.returncode:
            raise RuntimeError("masked variant build failed: " +
                               (built.stdout+built.stderr).decode(errors="replace")[-3000:])
        negative = invoke_timer_test(isolated, 5)
        serial = (isolated / "build/serial.log").read_bytes()
        assert negative.returncode == 1, negative.stdout[-1500:]
        assert b"SPIFFOS_IRQ0_MASKED_TEST" in serial
        assert b"SPIFFOS_IRQ0_TICKS_100" not in serial
        assert b"SPIFFOS_IRQ0_TICKS_200" not in serial
        assert b"SPIFFOS_BOOT_OK" in serial
        print("PASS IRQ0 negative: masked source times out without false clock success")


if __name__ == "__main__":
    main()
