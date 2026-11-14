#!/usr/bin/env python3
"""M10-D4 complete: four real priority workers plus supervisor in isolated QEMU.

No physical disks or owner ISO are modified; this source is copied into a
temporary tree with pinned Limine and uses QEMU -net none and private OVMF.
All tested image creation is isolated. References: AGENTS.md and
ADR-m10-d4-priority-supervisor-proof.md.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def image(source, env, flags):
    """Build an entirely disposable flagged ISO, erroring on compilation."""
    proc = subprocess.run(
        ["make", "-B", f"BOOT_TEST_FLAGS={flags}", "iso"],
        cwd=source, env=env, capture_output=True, timeout=120)
    if proc.returncode:
        raise RuntimeError(
            "D4 full guest build failed: " +
            (proc.stdout + proc.stderr).decode(errors="replace")[-4500:])


def boot(source, mib, marker):
    """Observe serial milestone reached by a real PIT0/APIC QEMU guest."""
    proc = subprocess.run(
        [sys.executable, "scripts/qemu.py", "test",
         "--memory-mib", str(mib), "--wait-marker", marker,
         "--timeout", "20"], cwd=source, capture_output=True, timeout=27)
    data = (source / "build/serial.log").read_bytes()
    if proc.returncode:
        raise RuntimeError(f"D4 full QEMU {mib}MiB missing {marker}: " +
                           repr(data[-1600:]) + "\n" +
                           (proc.stdout + proc.stderr).decode(
                               errors="replace")[-700:])
    return data


def main():
    """Run all memory sizes, positive and disabled-IRQ failure cases."""
    with tempfile.TemporaryDirectory(prefix="spiffos-d4-complete-") as temporary:
        src = Path(temporary) / "source"
        shutil.copytree(
            ROOT, src, symlinks=True,
            ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "safe.directory",
            "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve())
        })
        image(src, env, "-DSPIFFOS_M10_D4_COMPLETE_TEST")
        for mib in (128, 256, 512):
            data = boot(src, mib, "SPIFFOS_KEYBOARD_READY")
            markers = [b"SPIFFOS_IRQ0_ENABLED", b"SPIFFOS_BOOT_OK",
                       b"SPIFFOS_M10_D4_COMPLETE_OK", b"SPIFFOS_KEYBOARD_READY"]
            positions = [data.find(marker) for marker in markers]
            if (any(p < 0 for p in positions) or
                    positions != sorted(positions) or
                    b"SPIFFOS_EXCEPTION:" in data or
                    b"SPIFFOS_M10_D4_COMPLETE_ERROR" in data):
                raise RuntimeError(
                    f"D4 full QEMU {mib}MiB invalid boot: {data[-1900:]!r}")
            print(f"PASS M10-D4 complete QEMU {mib}MiB: four priority "
                  "workers, real block/wake and supervisor checked-frame "
                  "recovery, owned-stack release, keyboard ready", flush=True)
        image(src, env, "-DSPIFFOS_M10_D4_COMPLETE_TEST "
                        "-DSPIFFOS_IRQ0_DISABLED_TEST")
        for mib in (128, 256, 512):
            data = boot(src, mib, "SPIFFOS_M10_D4_COMPLETE_ERROR:IRQ0_NOT_ENABLED")
            if (b"SPIFFOS_M10_D4_COMPLETE_OK" in data or
                    b"SPIFFOS_KEYBOARD_READY" in data or
                    b"SPIFFOS_EXCEPTION:" in data):
                raise RuntimeError(f"D4 disabled IRQ0 did not fail closed {mib}")
            print(f"PASS M10-D4 complete negative QEMU {mib}MiB: "
                  "disabled IRQ0 rejects preemption", flush=True)

        # Damage a saved worker frame only in a separate diagnostic ISO.
        # The IRQ-exit selector must refuse that frame and restore the
        # surviving supervisor, not return through the corrupted CS.
        image(src, env, "-DSPIFFOS_M10_D4_COMPLETE_TEST "
                        "-DSPIFFOS_M10_D4_BAD_FRAME_TEST")
        for mib in (128, 256, 512):
            data = boot(src, mib,
                        "SPIFFOS_M10_D4_COMPLETE_ERROR:D4_QUOTA_OR_RECOVERY")
            if (b"SPIFFOS_M10_D4_COMPLETE_OK" in data or
                    b"SPIFFOS_KEYBOARD_READY" in data or
                    b"SPIFFOS_EXCEPTION:" in data):
                raise RuntimeError(f"D4 bad frame not contained at {mib}MiB")
            print(f"PASS M10-D4 complete frame-negative QEMU {mib}MiB: "
                  "invalid interrupted worker frame rejected", flush=True)

        image(src, env, "-DSPIFFOS_M10_D4_COMPLETE_TEST "
                        "-DSPIFFOS_M10_D4_BAD_CANARY_TEST")
        for mib in (128, 256, 512):
            data = boot(src, mib,
                        "SPIFFOS_M10_D4_COMPLETE_ERROR:D4_QUOTA_OR_RECOVERY")
            if (b"SPIFFOS_M10_D4_COMPLETE_OK" in data or
                    b"SPIFFOS_KEYBOARD_READY" in data or
                    b"SPIFFOS_EXCEPTION:" in data):
                raise RuntimeError(f"D4 corrupted guard accepted at {mib}MiB")
            print(f"PASS M10-D4 complete guard-negative QEMU {mib}MiB: "
                  "corrupt mapped-stack canary rejected", flush=True)


if __name__ == "__main__":
    main()
