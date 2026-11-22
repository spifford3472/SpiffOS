#!/usr/bin/env python3
"""D5.9-B authentic IRQ/CPU/owned-stack frame transaction evidence.

Disposable QEMU -net none; no accepted ISO, hardware drives or host changes.
Reference: ADR-m10-d59b-authentic-context.md. Hosted negative samples alone
are never accepted as hardware evidence.
"""
from pathlib import Path
import importlib.util
import os
import re
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("d57_guest", ROOT / "scripts/test-m10-d57-guest.py")
guest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guest)


def metric(serial, name):
    """Require a complete decimal metric; a missing sample is a failure."""
    match = re.search(rb"(?m)^" + name.encode() + rb":([0-9]+)\r?$", serial)
    if match is None:
        raise RuntimeError(f"missing {name}: {serial[-1600:]!r}")
    return int(match.group(1))


def main(extra_flags="", extra_check=None):
    """Prove both guarded frame callers execute under real IRQ preemption."""
    flags = "-DSPIFFOS_M10_D56_IRQ_TEST -DSPIFFOS_M10_D57_IRQ_TEST -DSPIFFOS_M10_D59B_CONTEXT_TEST"
    flags += extra_flags
    with tempfile.TemporaryDirectory(prefix="spiffos-d59b-context-") as tmp:
        source = Path(tmp) / "source"
        shutil.copytree(ROOT, source, symlinks=True,
                        ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({"GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "safe.directory",
                    "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve())})
        guest.image(source, env, flags)
        for mib in (128, 256, 512):
            serial = guest.boot(source, mib, "SPIFFOS_KEYBOARD_READY")
            if (b"SPIFFOS_M10_D59B_CONTEXT_IRQ_OK" not in serial or
                    b"SPIFFOS_M10_D56_IRQ_OK" not in serial or
                    b"SPIFFOS_EXCEPTION:" in serial or b"SPIFFOS_M10_D56_IRQ_ERROR" in serial):
                raise RuntimeError(f"D59B invalid positive boot: {serial[-1800:]!r}")
            if extra_check is not None:
                extra_check(serial, mib)
            counts = [metric(serial, f"D59B_WORKER{i}_FRAME_CYCLES") for i in (0, 1)]
            entries = metric(serial, "D59B_AUTHENTIC_IRQ_ENTRIES")
            duration = metric(serial, "D59B_MAX_TRANSACTION_BODY_TSC")
            full_duration = metric(serial, "D59B_MAX_EXCLUSION_TSC")
            gap = metric(serial, "D56_MAX_UNSERVICED_IRQ_TICKS")
            if min(counts) == 0 or entries < 12 or duration == 0 or full_duration < duration or gap > 8:
                raise RuntimeError(f"D59B insufficient progress: {counts=} {entries=} {duration=} {gap=}")
            print(f"PASS D59B QEMU {mib}MiB: frame cycles={counts}, authentic IRQ={entries}, "
                  f"observed max transaction-body TSC={duration}, max full exclusion TSC={full_duration}, supervisor gap={gap}", flush=True)
        guest.image(source, env, flags + " -DSPIFFOS_M10_D59B_BAD_IRQ_TEST")
        for mib in (128, 256, 512):
            serial = guest.boot(source, mib, "SPIFFOS_M10_D56_IRQ_ERROR:D59B_IRQ_DENIED")
            if (b"SPIFFOS_M10_D56_IRQ_OK" in serial or b"SPIFFOS_KEYBOARD_READY" in serial or
                    b"SPIFFOS_EXCEPTION:" in serial):
                raise RuntimeError(f"D59B IRQ negative admitted: {serial[-1800:]!r}")
            print(f"PASS D59B QEMU {mib}MiB: actual IRQ-context admission denied before allocator work", flush=True)
    print("SPIFFOS_M10_D59B_CONTEXT_GUEST_OK", flush=True)


if __name__ == "__main__":
    main()
