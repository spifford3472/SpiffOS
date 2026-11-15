#!/usr/bin/env python3
"""C2b physical four-stack IRQ/RET context and donation integration.

Only disposable QEMU with -net none and no disk, no production editor
change or owner ISO overwrite. References: AGENTS.md and C2b real IRQ ADR.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAGS = "-DSPIFFOS_M10_D58C_C2B_REAL_IRQ_TEST"
MARKERS = (
    b"SPIFFOS_IRQ0_ENABLED",
    b"SPIFFOS_BOOT_OK",
    b"SPIFFOS_M10_D58C_C2B_IRQ_OK",
    b"SPIFFOS_KEYBOARD_READY",
)


def main():
    """Prove actual M10-A handoffs and safe RET waiter wake at 3 sizes."""
    with tempfile.TemporaryDirectory(prefix="spiffos-c2b-real-") as tmp:
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
            cwd=source, env=env, capture_output=True, timeout=180)
        if build.returncode:
            raise RuntimeError(
                "C2b flagged ISO did not build:\n" +
                (build.stdout+build.stderr).decode(errors="replace")[-7000:])

        for mib in (128, 256, 512):
            run = subprocess.run(
                [sys.executable, "scripts/qemu.py", "test",
                 "--memory-mib", str(mib), "--wait-marker",
                 "SPIFFOS_KEYBOARD_READY", "--timeout", "28"],
                cwd=source, capture_output=True, timeout=35)
            serial = (source / "build/serial.log").read_bytes()
            found = [serial.find(marker) for marker in MARKERS]
            if (run.returncode or any(index < 0 for index in found) or
                    found != sorted(found) or
                    b"SPIFFOS_M10_D58C_C2B_IRQ_ERROR:" in serial or
                    b"SPIFFOS_EXCEPTION:" in serial):
                raise RuntimeError(
                    f"C2b {mib}MiB guest rejected: {serial[-2000:]!r}; " +
                    (run.stdout+run.stderr).decode(errors="replace")[-800:])

            def metric(label):
                match = re.search(
                    rb"(?m)^" + label.encode() + rb":([0-9]+)\r?$", serial)
                if not match:
                    raise RuntimeError(
                        f"C2b {mib}MiB missing {label}: {serial[-1400:]!r}")
                return int(match.group(1))

            low = metric("C2B_IRQ_LOW_SLICES")
            medium = metric("C2B_IRQ_MEDIUM_SLICES")
            supervisor = metric("C2B_IRQ_SUPERVISOR_SLICES")
            donated = metric("C2B_MEDIUM_TO_DONATED_LOW")
            switches = metric("C2B_IRQ_SWITCHES")
            service = metric("C2B_COMPLETED_SUPERVISOR_SERVICE")
            gap = metric("C2B_MAX_SUPERVISOR_GAP_TICKS")
            medium_cpu = metric("C2B_MEDIUM_CPU_ITERATIONS")
            supervisor_cpu = metric("C2B_SUPERVISOR_CPU_ITERATIONS")
            if (min(low, medium, supervisor, donated, service) < 12 or
                    switches < 12 or gap > 8 or
                    medium_cpu == 0 or supervisor_cpu == 0):
                raise RuntimeError(
                    f"C2b {mib}MiB insufficient verified execution "
                    f"{low=} {medium=} {supervisor=} {donated=} "
                    f"{service=} {gap=} {switches=} "
                    f"{medium_cpu=} {supervisor_cpu=}")
            print(
                f"PASS C2b real QEMU {mib}MiB: IRQ slices low={low}, "
                f"medium={medium}, supervisor={supervisor}; "
                f"{donated} medium-to-donated-owner actual IRQ handoffs; "
                f"{service} completed supervisor services (max gap {gap}); "
                "high BLOCKED RET-only then real owner-release wake, "
                "cooperative continuation resumed and editor restored",
                flush=True,
            )


if __name__ == "__main__":
    main()
