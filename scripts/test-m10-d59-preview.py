#!/usr/bin/env python3
"""Exercise the isolated D5.9 preview, safe off-ramp, and fatal worker trap.

Run only disposable QEMU guests with temporary OVMF state and no networking.
The interactive ISO is tested separately from the bounded and fault builds.
"""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "spiffos_d57_guest", ROOT / "scripts/test-m10-d57-guest.py"
)
guest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guest)
RAM_MIB = (128, 256, 512)
SNAPSHOT = re.compile(
    rb"(?m)^D59_PREVIEW:TICKS=(\d+),W1=(\d+),W2=(\d+),"
    rb"W1IRQ=(\d+),W2IRQ=(\d+),FG=(\d+),SW=(\d+),SUP=(\d+),GAP=(\d+)\r?$"
)


def verify_keyboard_preview(source, memory):
    """Check exact key events, restored pixels, and two advancing snapshots."""
    check = subprocess.run(
        [sys.executable, "scripts/qemu.py", "test", "--keyboard",
         "--wait-marker", "SPIFFOS_KEYBOARD_READY", "--timeout", "45",
         "--memory-mib", str(memory)],
        cwd=source, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=105,
    )
    serial = (source / "build/serial.log").read_bytes()
    if check.returncode:
        raise RuntimeError(
            f"preview keyboard {memory} MiB: {check.stdout[-3000:]!r} "
            f"serial={serial[-1300:]!r}"
        )
    snapshots = [tuple(map(int, match)) for match in SNAPSHOT.findall(serial)]
    if len(snapshots) != 2:
        raise RuntimeError(f"expected two coherent F10 samples, got {snapshots}")
    for ticks, w1, w2, w1_irq, w2_irq, foreground, switches, service, gap in snapshots:
        if not (min(w1, w2, w1_irq, w2_irq, foreground, switches, service) > 0
                and w1_irq + w2_irq + foreground <= ticks and switches <= ticks
                and service <= ticks and 0 < gap <= 8):
            raise RuntimeError(f"invalid F10 sample: {snapshots}")
    if any(after <= before for before, after in zip(snapshots[0][:-1], snapshots[1][:-1])):
        raise RuntimeError(f"foreground/worker/supervisor progress stalled: {snapshots}")
    for marker in (b"SPIFFOS_EXCEPTION:", b"SPIFFOS_M10_D56_IRQ_ERROR",
                   b"SPIFFOS_D59_PREVIEW_RETIREMENT_OK"):
        if marker in serial:
            raise RuntimeError(f"running preview unexpectedly reported {marker!r}")
    print(f"PASS D59 preview {memory} MiB: editor, workspace pixels, keys, F10 "
          f"progress {snapshots}, F12 timing", flush=True)


def main():
    """Run all preview profiles at three QEMU memory settings."""
    flags = " ".join([
        "-DSPIFFOS_M10_D56_IRQ_TEST", "-DSPIFFOS_M10_D57_IRQ_TEST",
        "-DSPIFFOS_M10_D59B_CONTEXT_TEST", "-DSPIFFOS_M10_D59B_SERVICE_TEST",
        "-DSPIFFOS_M10_D59B_PAGE_CLAIM_TEST", "-DSPIFFOS_M10_D59B_MEMORY_WIRING_TEST",
        "-DSPIFFOS_M10_D59_PREVIEW",
    ])
    with tempfile.TemporaryDirectory(prefix="spiffos-d59-preview-") as directory:
        source = Path(directory) / "source"
        shutil.copytree(ROOT, source, symlinks=True,
                        ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({"GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "safe.directory",
                    "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve())})
        guest.image(source, env, flags)
        for memory in RAM_MIB:
            verify_keyboard_preview(source, memory)
        subprocess.run([sys.executable, "scripts/test-safe-launcher.py"],
                       cwd=source, check=True, timeout=110)
        guest.image(source, env, flags + " -DSPIFFOS_M10_D59_PREVIEW_BOUNDED_TEST")
        for memory in RAM_MIB:
            output = guest.boot(source, memory, "SPIFFOS_D59_PREVIEW_RETIREMENT_OK")
            if (b"SPIFFOS_D59_PREVIEW_READY" not in output or
                    b"SPIFFOS_M10_D59B_MEMORY_BINDING_OK" not in output or
                    b"SPIFFOS_EXCEPTION:" in output or b"SPIFFOS_M10_D56_IRQ_ERROR" in output):
                raise RuntimeError(f"invalid retirement at {memory} MiB: {output[-1300:]!r}")
            print(f"PASS D59 bounded preview {memory} MiB: exact off-stack retirement", flush=True)
        guest.image(source, env, flags + " -DSPIFFOS_M10_D59_PREVIEW_FAULT_TEST")
        for memory in RAM_MIB:
            output = guest.boot(source, memory, "SPIFFOS_EXCEPTION:06,0000000000000000")
            if (b"SPIFFOS_D59_PREVIEW_READY" not in output or
                    b"SPIFFOS_D59_PREVIEW_RETIREMENT_OK" in output):
                raise RuntimeError(f"worker trap at {memory} MiB: {output[-1300:]!r}")
            print(f"PASS D59 fatal worker #UD {memory} MiB: fail closed", flush=True)
    print("SPIFFOS_M10_D59_PREVIEW_ACCEPTANCE_OK", flush=True)


if __name__ == "__main__":
    main()
