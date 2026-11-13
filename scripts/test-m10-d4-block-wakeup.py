#!/usr/bin/env python3
"""M10-D4 synthetic READY/block/wakeup test under real PIT0 IRQ preemption.

Four independent non-yielding worker stacks stay mapped throughout
temporary block windows. Blocked tasks MUST stop running, and re-enabled
workers must receive service within a bounded 64 IRQ ticks. All images
are disposable and cannot change the owner's installed SpiffOS.
References: AGENTS.md and ADR-m10-d4-priority-supervisor.md.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ("-DSPIFFOS_M10_D3_PREEMPT_TEST "
         "-DSPIFFOS_M10_D4_PRIORITY_TEST "
         "-DSPIFFOS_M10_D4_FOUR_CLASS_TEST "
         "-DSPIFFOS_M10_D4_BLOCK_WAKE_TEST")


def build(source, env, extra=""):
    """Rebuild all objects with exact diagnostic flags, no stale artifacts."""
    run = subprocess.run(
        ["make", "-B", f"BOOT_TEST_FLAGS={FLAGS} {extra}".rstrip(), "iso"],
        cwd=source, env=env, capture_output=True, timeout=120)
    if run.returncode:
        raise RuntimeError(
            "M10-D4 block/wakeup ISO build failed: " +
            (run.stdout + run.stderr).decode(errors="replace")[-3600:])


def boot(source, mib, marker):
    """Run real QEMU with fresh firmware vars and return serial state."""
    run = subprocess.run(
        [sys.executable, "scripts/qemu.py", "test", "--memory-mib",
         str(mib), "--wait-marker", marker, "--timeout", "26"],
        cwd=source, capture_output=True, timeout=32)
    serial = (source / "build/serial.log").read_bytes()
    if run.returncode:
        raise RuntimeError(
            f"M10-D4 QEMU {mib}MiB did not reach {marker}: " +
            (run.stdout + run.stderr).decode(errors="replace")[-1400:] +
            "\nserial=" + repr(serial[-1700:]))
    return serial


def main():
    """Prove blocked workers stop, wake promptly, and reject corrupted state."""
    with tempfile.TemporaryDirectory(prefix="spiffos-m10-d4-wake-") as tmp:
        source = Path(tmp) / "source"
        shutil.copytree(
            ROOT, source, symlinks=True,
            ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "safe.directory",
            "GIT_CONFIG_VALUE_0":
                str((ROOT / ".local-dev/limine").resolve())
        })
        build(source, env)
        for mib in (128, 256, 512):
            serial = boot(source, mib, "SPIFFOS_KEYBOARD_READY")
            markers = (
                b"SPIFFOS_IRQ0_ENABLED",
                b"SPIFFOS_BOOT_OK",
                b"SPIFFOS_M10_D4_PRIORITY_OK",
                b"SPIFFOS_M10_D3_PREEMPT_OK",
                b"SPIFFOS_WORKSPACES_READY_9",
                b"SPIFFOS_KEYBOARD_READY"
            )
            positions = [serial.find(m) for m in markers]
            if (any(p < 0 for p in positions) or
                    positions != sorted(positions) or
                    b"SPIFFOS_EXCEPTION:" in serial or
                    b"SPIFFOS_M10_D3_PREEMPT_ERROR" in serial):
                raise RuntimeError(f"D4 wake {mib}MiB bad: {serial[-1800:]!r}")
            print(f"PASS D4 wakeup QEMU {mib}MiB: blocked workers "
                  "stop progressing, resume before deadline, "
                  "four owned stacks reclaimed", flush=True)

        negatives = (
            ("-DSPIFFOS_M10_D4_NEVER_WAKE_TEST", "never-wake"),
            ("-DSPIFFOS_M10_D4_BLOCK_PROGRESS_CORRUPT_TEST",
             "illegal-blocked-progress")
        )
        for flags, label in negatives:
            build(source, env, flags)
            for mib in (128, 256, 512):
                serial = boot(
                    source, mib,
                    "SPIFFOS_M10_D3_PREEMPT_ERROR:PREEMPTION_PROGRESS")
                if (b"SPIFFOS_M10_D4_PRIORITY_OK" in serial or
                        b"SPIFFOS_KEYBOARD_READY" in serial or
                        b"SPIFFOS_EXCEPTION:" in serial):
                    raise RuntimeError(
                        f"D4 {label} {mib}MiB did not fail closed")
                print(f"PASS D4 wakeup negative QEMU {mib}MiB: "
                      f"{label} rejected", flush=True)


if __name__ == "__main__":
    main()
