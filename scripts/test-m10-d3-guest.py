#!/usr/bin/env python3
"""M10-D3 isolated QEMU proof of real PIT0-driven CPU-hog task handoff.

Build only throwaway source trees; never modify the installed Forge ISO.
Positive: two non-yielding owned stacks alternate under real IRQ0 and the
original boot stack resumes, frees backing and restores keyboard/editor.
Negative: disabled IRQ0 must fail closed rather than claim preemption.
See AGENTS.md and docs/operations/m10-d3-irq-handoff-20261113.md.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def guest_run(source, mib, marker):
    """Run QEMU with fresh OVMF vars; return all serial evidence or fail."""
    run = subprocess.run(
        [sys.executable, "scripts/qemu.py", "test",
         "--memory-mib", str(mib), "--wait-marker", marker,
         "--timeout", "20"],
        cwd=source, capture_output=True, timeout=26)
    serial = (source / "build/serial.log").read_bytes()
    if run.returncode:
        raise RuntimeError(
            f"QEMU {mib}MiB did not reach {marker}: " +
            (run.stdout + run.stderr).decode(errors="replace")[-1600:] +
            "\nserial: " + repr(serial[-2200:]))
    return serial


def build_guest(source, env, flags):
    """Build special diagnostic ISO from isolated copy, never owner checkout."""
    make = subprocess.run(
        ["make", "-B", f"BOOT_TEST_FLAGS={flags}", "iso"],
        cwd=source, env=env, capture_output=True, timeout=120)
    if make.returncode:
        raise RuntimeError(
            "D3 test-only ISO build failed: " +
            (make.stdout + make.stderr).decode(errors="replace")[-3500:])


def main():
    """Prove 3-size real timer handoff and disabled-IRQ fail-closed route."""
    with tempfile.TemporaryDirectory(prefix="spiffos-m10-d3-") as temporary:
        source = Path(temporary) / "source"
        shutil.copytree(
            ROOT, source, symlinks=True,
            ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "safe.directory",
            "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve())
        })
        build_guest(source, env, "-DSPIFFOS_M10_D3_PREEMPT_TEST")
        for mib in (128, 256, 512):
            serial = guest_run(source, mib, "SPIFFOS_KEYBOARD_READY")
            markers = [
                b"SPIFFOS_IRQ0_ENABLED",
                b"SPIFFOS_BOOT_OK",
                b"SPIFFOS_M10_D3_PREEMPT_OK",
                b"SPIFFOS_KEYBOARD_READY"
            ]
            at = [serial.find(marker) for marker in markers]
            if (any(p < 0 for p in at) or at != sorted(at) or
                b"SPIFFOS_M10_D3_PREEMPT_ERROR" in serial or
                b"SPIFFOS_EXCEPTION:" in serial or
                b"SPIFFOS_MEMORY_SERVICE_ERROR" in serial):
                raise RuntimeError(
                    f"D3 QEMU {mib}MiB invalid evidence: {serial[-2200:]!r}")
            print(f"PASS M10-D3 QEMU {mib}MiB: two non-yielding owned-stack "
                  "CPU hogs preempted by IRQ0, boot stack resumed, "
                  "backing released, keyboard ready", flush=True)

        build_guest(
            source, env, "-DSPIFFOS_M10_D3_PREEMPT_TEST "
            "-DSPIFFOS_IRQ0_DISABLED_TEST")
        for mib in (128, 256, 512):
            serial = guest_run(
                source, mib, "SPIFFOS_M10_D3_PREEMPT_ERROR:IRQ0_NOT_ENABLED")
            if (b"SPIFFOS_M10_D3_PREEMPT_OK" in serial or
                b"SPIFFOS_KEYBOARD_READY" in serial or
                b"SPIFFOS_EXCEPTION:" in serial):
                raise RuntimeError(
                    f"D3 disabled IRQ0 {mib}MiB did not fail closed")
            print(f"PASS M10-D3 negative QEMU {mib}MiB: IRQ0 "
                  "unavailable, no preemption enabled", flush=True)


if __name__ == "__main__":
    main()
