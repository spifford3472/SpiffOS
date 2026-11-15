#!/usr/bin/env python3
"""Callback-free M3 claim/fixed zero/rollback under actual D5.7 IRQ scheduling.
Temporary source/ISO and QEMU -net none only; accepted image stays untouched.
Reference: ADR-m10-d59b-runtime-memory-wiring.md and AGENTS.md.
"""
from pathlib import Path
import importlib.util
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("d59b_service_guest", ROOT / "scripts/test-m10-d59b-service-guest.py")
service = importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)


def page_samples(serial, mib):
    """Require actual full-page fixed-leaf and rollback checks on both workers."""
    service.service_samples(serial, mib)
    if b"SPIFFOS_M10_D59B_PAGE_IRQ_OK" not in serial:
        raise RuntimeError(f"missing fixed page proof: {serial[-1800:]!r}")
    pages = [service.guest.metric(serial, f"D59B_WORKER{i}_PAGE_CYCLES") for i in (0, 1)]
    frames = [service.guest.metric(serial, f"D59B_WORKER{i}_FRAME_CYCLES") for i in (0, 1)]
    if min(pages) == 0 or pages != frames:
        raise RuntimeError(f"incomplete page cycles: {pages=} {frames=}")
    print(f"PASS D59B fixed page QEMU {mib}MiB: {pages}, full zero/payload/rollback/accounting", flush=True)


def main():
    """Retain ALL parent IRQ/IF/frame/service tests and add real page work."""
    service.guest.main(" -DSPIFFOS_M10_D59B_SERVICE_TEST -DSPIFFOS_M10_D59B_PAGE_CLAIM_TEST", page_samples)
    print("SPIFFOS_M10_D59B_PAGE_CLAIM_GUEST_OK", flush=True)


if __name__ == "__main__":
    main()
