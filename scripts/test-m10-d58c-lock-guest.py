#!/usr/bin/env python3
"""D5.8c C1: genuine lock-owning task interrupted by real x86 IRQ0 in QEMU.

This is not yet a high-priority BLOCKED waiter/donation integration test.
The QEMU source/ISO and variable store are disposable; -net none, no disks.
References: AGENTS.md, ADR-m10-d58c-real-irq-lock-owner.md.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ("-DSPIFFOS_M10_D56_IRQ_TEST -DSPIFFOS_M10_D57_IRQ_TEST "
         "-DSPIFFOS_M10_D58C_LOCK_IRQ_TEST")
REQUIRED = (
    b"SPIFFOS_BOOT_OK",
    b"D58C_LOCKED_SOURCE_IRQS:",
    b"SPIFFOS_M10_D56_IRQ_OK",
    b"SPIFFOS_KEYBOARD_READY",
)


def main():
    """Verify >=3 actual lock-held IRQ0 source frames + real release."""
    with tempfile.TemporaryDirectory(prefix="spiffos-d58c-real-irq-") as tmp:
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
                "D5.8c flagged ISO build failed:\n" +
                (build.stdout + build.stderr).decode(errors="replace")[-6000:])

        for mib in (128, 256, 512):
            run = subprocess.run(
                [sys.executable, "scripts/qemu.py", "test",
                 "--memory-mib", str(mib), "--wait-marker",
                 "SPIFFOS_KEYBOARD_READY", "--timeout", "24"],
                cwd=source, capture_output=True, timeout=29)
            serial = (source / "build/serial.log").read_bytes()
            indexes = [serial.find(marker) for marker in REQUIRED]
            if (run.returncode or any(pos < 0 for pos in indexes) or
                    indexes != sorted(indexes) or
                    b"SPIFFOS_M10_D56_IRQ_ERROR:" in serial or
                    b"SPIFFOS_EXCEPTION:" in serial):
                raise RuntimeError(
                    f"D5.8c C1 QEMU {mib}MiB failed: " +
                    repr(serial[-2100:]) + " " +
                    (run.stdout + run.stderr).decode(errors="replace")[-1000:])

            def metric(name):
                found = re.search(
                    rb"(?m)^" + name.encode() + rb":([0-9]+)\r?$", serial)
                if not found:
                    raise RuntimeError(
                        f"Missing {name} at {mib}MiB: {serial[-700:]!r}")
                return int(found.group(1))

            held = metric("D58C_LOCKED_SOURCE_IRQS")
            acquired = metric("D58C_LOCK_ACQUIRES")
            released = metric("D58C_LOCK_RELEASES")
            worker0 = metric("D56_WORKER0_TICKS")
            worker1 = metric("D56_WORKER1_TICKS")
            gap = metric("D56_MAX_UNSERVICED_IRQ_TICKS")
            if held < 3 or acquired != 1 or released != 1 or (
                    min(worker0, worker1) < 12 or gap > 8):
                raise RuntimeError(
                    f"D5.8c C1 {mib}MiB bad metrics "
                    f"{held=} {acquired=} {released=} "
                    f"{worker0=} {worker1=} {gap=}")
            print(
                f"PASS D5.8c C1 QEMU {mib}MiB: {held} authentic IRQ0 "
                f"source frames while rank-0 owner live; acquire/release "
                f"1/1 off-IRQ; workers={worker0}/{worker1}; "
                f"supervisor max gap={gap} ticks", flush=True,
            )
        # Exercise the existing D5.6 fail-closed handoff gates while the
        # new lock-aware C1 admission is enabled. Never replace the real
        # source frame with a fabricated return frame.
        for label, flag, expected in (
            ("target-frame", "-DSPIFFOS_M10_D56_BAD_TARGET_TEST",
             b"SPIFFOS_M10_D56_IRQ_ERROR:TARGET_FRAME_DENIED"),
            ("critical", "-DSPIFFOS_M10_D56_BAD_CRITICAL_TEST",
             b"SPIFFOS_M10_D56_IRQ_ERROR:CRITICAL_DENIED"),
            ("stale-target", "-DSPIFFOS_M10_D56_BAD_GENERATION_TEST",
             b"SPIFFOS_M10_D56_IRQ_ERROR:STALE_HANDLE_DENIED"),
        ):
            negative = subprocess.run(
                ["make", "-B", f"BOOT_TEST_FLAGS={FLAGS} {flag}", "iso"],
                cwd=source, env=env, capture_output=True, timeout=150)
            if negative.returncode:
                raise RuntimeError(
                    f"D5.8c negative {label} compilation: " +
                    (negative.stdout+negative.stderr).decode(
                        errors="replace")[-4500:])
            for mib in (128, 256, 512):
                run = subprocess.run(
                    [sys.executable, "scripts/qemu.py", "test",
                     "--memory-mib", str(mib), "--wait-marker",
                     expected.decode(), "--timeout", "24"],
                    cwd=source, capture_output=True, timeout=29)
                serial = (source / "build/serial.log").read_bytes()
                if (run.returncode or expected not in serial or
                        b"SPIFFOS_M10_D56_IRQ_OK" in serial or
                        b"SPIFFOS_KEYBOARD_READY" in serial or
                        b"SPIFFOS_EXCEPTION:" in serial):
                    raise RuntimeError(
                        f"D5.8c {label} {mib}MiB fail-closed violated: " +
                        repr(serial[-1600:]))
                print(f"PASS D5.8c C1 negative {label} QEMU "
                      f"{mib}MiB: rejected before unsafe task dispatch",
                      flush=True)


if __name__ == "__main__":
    main()
