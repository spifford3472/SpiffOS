#!/usr/bin/env python3
"""Real retained M6 transactions under authentic D5.9-B preemption.

The parent frame/context harness retains all admission and negative assertions.
Only isolated ISO/OVMF/QEMU -net none; no accepted ISO or physical devices.
Reference: ADR-m10-d59b-service-transactions.md.
"""
from pathlib import Path
import importlib.util
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("d59b_context_guest", ROOT / "scripts/test-m10-d59b-context-guest.py")
guest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guest)


def service_samples(serial, mib):
    """Require both workers actually complete real retained-service cycles."""
    if b"SPIFFOS_M10_D59B_SERVICE_IRQ_OK" not in serial:
        raise RuntimeError(f"missing M6 transaction proof: {serial[-1800:]!r}")
    counts = [guest.metric(serial, f"D59B_WORKER{i}_SERVICE_CYCLES") for i in (0, 1)]
    frames = [guest.metric(serial, f"D59B_WORKER{i}_FRAME_CYCLES") for i in (0, 1)]
    if min(counts) == 0 or counts != frames:
        raise RuntimeError(f"incomplete service cycles: {counts=} {frames=}")
    print(f"PASS D59B retained service QEMU {mib}MiB: {counts}, owners/OOM/roots/payload restored", flush=True)


def main():
    """Keep every parent frame/IF/IRQ/supervisor test, adding M6 operations."""
    guest.main(" -DSPIFFOS_M10_D59B_SERVICE_TEST", service_samples)
    print("SPIFFOS_M10_D59B_SERVICE_GUEST_OK", flush=True)


if __name__ == "__main__":
    main()
