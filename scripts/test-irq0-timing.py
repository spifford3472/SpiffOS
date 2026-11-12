#!/usr/bin/env python3
"""Prove real QEMU PIT0 ticks via independent wall-clock sampling.

Build a disposable source/ISO; never alter the owner's ISO or run QEMU
against a physical disk. Only the test variant prints tick milestones.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Require ordered 100/200 tick milestones around one second apart."""
    with tempfile.TemporaryDirectory(prefix="spiffos-irq0-timing-") as temp:
        source = Path(temp) / "source"
        shutil.copytree(ROOT, source, symlinks=True,
                        ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "safe.directory",
            "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve()),
        })
        build = subprocess.run(
            ["make", "BOOT_TEST_FLAGS=-DSPIFFOS_IRQ0_TIMING_TEST", "iso"],
            cwd=source, env=env, capture_output=True, timeout=100)
        if build.returncode:
            raise RuntimeError("timing ISO build failed: " +
                               (build.stdout + build.stderr).decode(errors="replace")[-3000:])
        for mib in (128, 256, 512):
            # The QEMU runner writes one shared disposable guest serial path;
            # delete the prior test's markers before starting the next guest.
            serial_path = source / "build/serial.log"
            serial_path.unlink(missing_ok=True)
            process = subprocess.Popen(
                [sys.executable, "scripts/qemu.py", "test",
                 "--memory-mib", str(mib),
                 "--wait-marker", "SPIFFOS_IRQ0_TICK_200",
                 "--timeout", "16"], cwd=source,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            first = None
            second = None
            deadline = time.monotonic() + 19
            serial_path = source / "build/serial.log"
            try:
                while time.monotonic() < deadline:
                    if serial_path.exists():
                        evidence = serial_path.read_bytes()
                        now = time.monotonic()
                        if first is None and b"SPIFFOS_IRQ0_TICK_100\r\n" in evidence:
                            first = now
                        if second is None and b"SPIFFOS_IRQ0_TICK_200\r\n" in evidence:
                            second = now
                        if second is not None:
                            break
                    if process.poll() is not None:
                        break
                    time.sleep(0.01)
                output, _ = process.communicate(timeout=5)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
            assert process.returncode == 0, output[-1200:]
            evidence = serial_path.read_bytes()
            required = (
                b"SPIFFOS_IRQ0_PREFLIGHT_OK",
                b"SPIFFOS_IRQ0_ROUTE_READY",
                b"SPIFFOS_IRQ0_ENABLED",
                b"SPIFFOS_BOOT_OK",
                b"SPIFFOS_IRQ0_TICK_100",
                b"SPIFFOS_IRQ0_TICK_200",
            )
            positions = [evidence.find(marker) for marker in required]
            assert all(x >= 0 for x in positions) and positions == sorted(positions), evidence
            assert all(evidence.count(marker) == 1 for marker in required), evidence
            assert first is not None and second is not None, evidence
            delta = second - first
            assert 0.75 <= delta <= 1.35, f"bad 100-tick elapsed wall time: {delta:.3f}s"
            assert b"SPIFFOS_EXCEPTION:" not in evidence, evidence
            print(f"PASS real QEMU IRQ0 {mib}MiB: +100 ticks in {delta:.3f}s")


if __name__ == "__main__":
    main()
