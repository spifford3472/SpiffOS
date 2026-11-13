#!/usr/bin/env python3
"""D4 second gate: four simultaneous priorities on four owned QEMU stacks.

Build and run disposable diagnostic ISOs only. The normal owner SpiffOS ISO
must never be overwritten or linked with D4 flags. Real PIT0 IRQ0 delivers
the full saved frame and the D1 aging selector reserves every fourth
dispatch for the persistent supervisor boot stack.
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
         "-DSPIFFOS_M10_D4_FOUR_CLASS_TEST")


def build(source, env, extra=""):
    """Rebuild from scratch so stale test flags cannot leak across guests."""
    run = subprocess.run(
        ["make", "-B", f"BOOT_TEST_FLAGS={FLAGS} {extra}".rstrip(), "iso"],
        cwd=source, env=env, capture_output=True, timeout=120)
    if run.returncode:
        raise RuntimeError(
            "D4 four-class ISO build failed: " +
            (run.stdout + run.stderr).decode(errors="replace")[-3600:])


def boot(source, mib, marker):
    """Run a QEMU guest with its own OVMF vars and return serial evidence."""
    run = subprocess.run(
        [sys.executable, "scripts/qemu.py", "test", "--memory-mib",
         str(mib), "--wait-marker", marker, "--timeout", "26"],
        cwd=source, capture_output=True, timeout=32)
    serial = (source / "build/serial.log").read_bytes()
    if run.returncode:
        raise RuntimeError(
            f"QEMU {mib}MiB did not reach {marker}: " +
            (run.stdout + run.stderr).decode(errors="replace")[-1400:] +
            "\nserial=" + repr(serial[-2000:]))
    return serial


def main():
    """Verify simultaneous scheduling, four independent failure modes."""
    with tempfile.TemporaryDirectory(prefix="spiffos-m10-d4-four-") as temporary:
        source = Path(temporary) / "source"
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
            indices = [serial.find(m) for m in markers]
            if (any(i < 0 for i in indices) or
                    indices != sorted(indices) or
                    b"SPIFFOS_EXCEPTION:" in serial or
                    b"SPIFFOS_M10_D3_PREEMPT_ERROR" in serial):
                raise RuntimeError(f"D4 four-class QEMU {mib} invalid: {serial[-1800:]!r}")
            print(f"PASS M10-D4 four-class QEMU {mib}MiB: four simultaneous "
                  "CPU hogs, distinct priority bands, supervisor quota, "
                  "four owned stacks reclaimed, keyboard ready", flush=True)

        cases = (
            ("-DSPIFFOS_M10_D4_BAD_PRIORITY_TEST",
             "SPIFFOS_M10_D3_PREEMPT_ERROR:PREEMPTION_PROGRESS",
             "invalid-priority"),
            ("-DSPIFFOS_IRQ0_DISABLED_TEST",
             "SPIFFOS_M10_D3_PREEMPT_ERROR:IRQ0_NOT_ENABLED",
             "IRQ0-disabled"),
            ("-DSPIFFOS_M10_D3_BAD_CANARY_TEST",
             "SPIFFOS_M10_D3_PREEMPT_ERROR:PREEMPTION_PROGRESS",
             "corrupt-canary"),
            ("-DSPIFFOS_M10_D4_FAIL_STACK_INDEX_TEST",
             "SPIFFOS_M10_D3_PREEMPT_ERROR:ALLOCATE_STACKS",
             "third-stack-allocation-rollback")
        )
        for extra, marker, label in cases:
            build(source, env, extra)
            for mib in (128, 256, 512):
                serial = boot(source, mib, marker)
                if (b"SPIFFOS_M10_D4_PRIORITY_OK" in serial or
                        b"SPIFFOS_KEYBOARD_READY" in serial or
                        b"SPIFFOS_EXCEPTION:" in serial):
                    raise RuntimeError(
                        f"D4 {label} {mib}MiB did not fail closed")
                print(f"PASS M10-D4 four-class negative QEMU {mib}MiB: "
                      f"{label} rejected", flush=True)


if __name__ == "__main__":
    main()
