#!/usr/bin/env python3
"""Verify actual QEMU APIC MMIO PTE setup at every supported boot size.

This is a read-only check of serial proof emitted by the guest's own CR3
walker. It does not read APIC registers or enable interrupt delivery.
"""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = (
    b"SPIFFOS_ACPI_MADT_OK",
    b"SPIFFOS_APIC_CPU_MATCH",
    b"SPIFFOS_MMIO_POLICY_OK",
    b"SPIFFOS_MMIO_ROOT_READY",
    b"SPIFFOS_MMIO_MAPPED_OK",
    b"SPIFFOS_MEMORY_RETAINED_OK",
    b"SPIFFOS_BOOT_OK",
)
FORBIDDEN = (
    b"SPIFFOS_MMIO_MAPPING_UNAVAILABLE",
    b"SPIFFOS_MMIO_ROLLBACK_FATAL",
    b"SPIFFOS_EXCEPTION",
)


def main():
    """Boot three independent QEMU guests and validate required marker order."""
    for memory_mib in (128, 256, 512):
        guest = subprocess.run(
            [sys.executable, "scripts/qemu.py", "test", "--memory-mib",
             str(memory_mib)],
            cwd=ROOT, capture_output=True, timeout=25
        )
        assert guest.returncode == 0, guest.stdout[-800:]
        serial = (ROOT / "build/serial.log").read_bytes()
        offsets = [serial.find(marker) for marker in REQUIRED]
        assert all(offset >= 0 and serial.count(marker) == 1
                   for offset, marker in zip(offsets, REQUIRED)), serial
        assert offsets == sorted(offsets), serial
        assert all(marker not in serial for marker in FORBIDDEN), serial
        print(f"PASS QEMU MMIO {memory_mib}MiB: checked UC/NX pages and normal boot")


if __name__ == "__main__":
    main()
