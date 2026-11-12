#!/usr/bin/env python3
"""Check QEMU APIC ID/version read-only proof at 128/256/512 MiB.

The tested kernel uses private UC/NX M9-3D4 mappings, verifies HHDM aliases
absent, then reads LAPIC ID/IOAPIC ID/version without changing IRQ routing.
"""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = (
    b"SPIFFOS_ACPI_MADT_OK",
    b"SPIFFOS_APIC_CPU_MATCH",
    b"SPIFFOS_MMIO_MAPPED_OK",
    b"SPIFFOS_ALIAS_LAPIC_ABSENT",
    b"SPIFFOS_ALIAS_IOAPIC_ABSENT",
    b"SPIFFOS_MTRR_ENABLED",
    b"SPIFFOS_APIC_CACHE_SAFE",
    b"SPIFFOS_APIC_REGISTERS_OK",
    b"SPIFFOS_MEMORY_RETAINED_OK",
    b"SPIFFOS_BOOT_OK",
)
FORBIDDEN = (
    b"SPIFFOS_APIC_CACHE_UNVERIFIED",
    b"SPIFFOS_APIC_REGISTERS_UNAVAILABLE",
    b"SPIFFOS_APIC_REGISTERS_SKIPPED",
    b"SPIFFOS_MMIO_MAPPING_UNAVAILABLE",
    b"SPIFFOS_EXCEPTION:",
)


def main():
    """Verify three independent real QEMU guests and marker ordering."""
    for memory_mib in (128, 256, 512):
        run = subprocess.run(
            [sys.executable, "scripts/qemu.py", "test", "--memory-mib",
             str(memory_mib)], cwd=ROOT, capture_output=True, timeout=25)
        assert run.returncode == 0, run.stdout[-800:]
        serial = (ROOT / "build/serial.log").read_bytes()
        positions = [serial.find(marker) for marker in REQUIRED]
        assert all(p >= 0 and serial.count(marker) == 1
                   for marker, p in zip(REQUIRED, positions)), serial
        assert positions == sorted(positions), serial
        assert all(marker not in serial for marker in FORBIDDEN), serial
        print(f"PASS QEMU APIC read-only {memory_mib}MiB: cache/alias safe, LAPIC and IOAPIC verified")


if __name__ == "__main__":
    main()
