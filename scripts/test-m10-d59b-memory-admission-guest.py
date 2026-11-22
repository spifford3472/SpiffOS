#!/usr/bin/env python3
"""Real boot/owned-SUP/retirement memory-domain lifecycle, isolated QEMU.
No accepted image, physical disk, networking or host configuration changes.
Reference: ADR-m10-d59b-runtime-memory-wiring.md and AGENTS.md.
"""
from pathlib import Path
import importlib.util
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("d59b_page_guest", ROOT / "scripts/test-m10-d59b-page-claim-guest.py")
page = importlib.util.module_from_spec(spec)
spec.loader.exec_module(page)


def admission_samples(serial, mib):
    """Retain parent page/service/frame/IRQ checks and require safe retirement."""
    page.page_samples(serial, mib)
    if b"SPIFFOS_M10_D59B_MEMORY_BINDING_OK" not in serial:
        raise RuntimeError(f"missing complete memory lifecycle: {serial[-1800:]!r}")
    direct = [page.service.guest.metric(serial, f"D59B_WORKER{i}_DIRECT_CYCLES") for i in (0, 1)]
    frames = [page.service.guest.metric(serial, f"D59B_WORKER{i}_FRAME_CYCLES") for i in (0, 1)]
    # A final completed guarded cycle can be preempted before its read-only
    # refusal probe. It owns NO resource across that gap; require real progress.
    if min(direct) == 0 or any(f - d not in (0, 1) for f, d in zip(frames, direct)):
        raise RuntimeError(f"incomplete direct caller coverage: {direct=} {frames=}")
    print(f"PASS D59B memory lifecycle QEMU {mib}MiB: bind/off-stack retirement, direct callers={direct}", flush=True)


def main():
    """Use actual assembly IRQ depth, CPU identity and task-owned stack samples."""
    page.service.guest.main(" -DSPIFFOS_M10_D59B_SERVICE_TEST -DSPIFFOS_M10_D59B_PAGE_CLAIM_TEST -DSPIFFOS_M10_D59B_MEMORY_WIRING_TEST", admission_samples)
    print("SPIFFOS_M10_D59B_MEMORY_ADMISSION_GUEST_OK", flush=True)


if __name__ == "__main__":
    main()
