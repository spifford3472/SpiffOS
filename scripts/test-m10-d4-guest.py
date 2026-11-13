#!/usr/bin/env python3
"""M10-D4 actual IRQ0 priority and supervisor-service QEMU regression.

This test builds only private disposable images from the current Git tree;
it never modifies the installed owner ISO or physical media. Two real
CPU-bound workers remain non-yielding across four priority epochs while
the supervisor's permanent boot stack receives every-fourth dispatch.
References: AGENTS.md and ADR-m10-d4-priority-supervisor.md.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def guest_run(source, mib, marker):
    """Return real QEMU serial evidence after one boot with private OVMF vars."""
    proc = subprocess.run(
        [sys.executable, "scripts/qemu.py", "test", "--memory-mib",
         str(mib), "--wait-marker", marker, "--timeout", "25"],
        cwd=source, capture_output=True, timeout=30)
    serial = (source / "build/serial.log").read_bytes()
    if proc.returncode:
        raise RuntimeError(
            f"D4 QEMU {mib}MiB missing {marker}: " +
            (proc.stdout + proc.stderr).decode(errors="replace")[-1000:] +
            "\nserial=" + repr(serial[-1800:]))
    return serial


def build(source, env, flags):
    """Recompile every object when changing diagnostic flags."""
    proc = subprocess.run(
        ["make", "-B", f"BOOT_TEST_FLAGS={flags}", "iso"],
        cwd=source, env=env, capture_output=True, timeout=120)
    if proc.returncode:
        raise RuntimeError(
            "M10-D4 disposable ISO build failed: " +
            (proc.stdout + proc.stderr).decode(errors="replace")[-3600:])


def main():
    """Prove four priority phases, reservation and fail-closed negatives."""
    with tempfile.TemporaryDirectory(prefix="spiffos-m10-d4-") as temp:
        source = Path(temp) / "source"
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
        base = "-DSPIFFOS_M10_D3_PREEMPT_TEST -DSPIFFOS_M10_D4_PRIORITY_TEST"
        build(source, env, base)
        for mib in (128, 256, 512):
            serial = guest_run(source, mib, "SPIFFOS_KEYBOARD_READY")
            markers = (
                b"SPIFFOS_IRQ0_ENABLED",
                b"SPIFFOS_BOOT_OK",
                b"SPIFFOS_M10_D4_PRIORITY_OK",
                b"SPIFFOS_M10_D3_PREEMPT_OK",
                b"SPIFFOS_WORKSPACES_READY_9",
                b"SPIFFOS_KEYBOARD_READY"
            )
            indexes = [serial.find(m) for m in markers]
            if (any(i < 0 for i in indexes) or
                    indexes != sorted(indexes) or
                    b"SPIFFOS_EXCEPTION:" in serial or
                    b"SPIFFOS_M10_D3_PREEMPT_ERROR" in serial):
                raise RuntimeError(f"D4 {mib}MiB unexpected: {serial[-2000:]!r}")
            print(f"PASS M10-D4 QEMU {mib}MiB: all four priority phases, "
                  "aging, supervisor reserve, owned stacks and keyboard",
                  flush=True)

        build(source, env, base + " -DSPIFFOS_M10_D4_BAD_PRIORITY_TEST")
        for mib in (128, 256, 512):
            serial = guest_run(
                source, mib, "SPIFFOS_M10_D3_PREEMPT_ERROR:PREEMPTION_PROGRESS")
            if (b"SPIFFOS_M10_D4_PRIORITY_OK" in serial or
                    b"SPIFFOS_KEYBOARD_READY" in serial or
                    b"SPIFFOS_EXCEPTION:" in serial):
                raise RuntimeError(f"D4 invalid priority {mib}MiB accepted")
            print(f"PASS M10-D4 negative QEMU {mib}MiB: malformed "
                  "priority rejected before dispatch", flush=True)

        build(source, env, base + " -DSPIFFOS_IRQ0_DISABLED_TEST")
        for mib in (128, 256, 512):
            serial = guest_run(
                source, mib, "SPIFFOS_M10_D3_PREEMPT_ERROR:IRQ0_NOT_ENABLED")
            if (b"SPIFFOS_M10_D4_PRIORITY_OK" in serial or
                    b"SPIFFOS_KEYBOARD_READY" in serial or
                    b"SPIFFOS_EXCEPTION:" in serial):
                raise RuntimeError(f"D4 disabled IRQ {mib}MiB accepted")
            print(f"PASS M10-D4 negative QEMU {mib}MiB: no IRQ0, "
                  "scheduler remains disabled", flush=True)


if __name__ == "__main__":
    main()
