#!/usr/bin/env python3
"""Package a UEFI-only ISO using pinned local Limine files; no downloads/devices."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PIN = "7a9013f305de0bea1f6310e76e7baba30499fef0"


def main():
    """Verify committed dependency bytes, then build the ISO; errors exit nonzero."""
    dep = ROOT / ".local-dev/limine"
    head = subprocess.check_output(["git", "-C", str(dep), "rev-parse", "HEAD"], text=True).strip()
    if head != PIN:
        raise SystemExit("Limine binary checkout does not match pinned commit: " + PIN)
    temporary = tempfile.TemporaryDirectory(prefix="iso-", dir=ROOT / "build")
    stage = Path(temporary.name)
    for name, dest in [("BOOTX64.EFI", "EFI/BOOT/BOOTX64.EFI"),
                       ("limine-uefi-cd.bin", "boot/limine/limine-uefi-cd.bin")]:
        official = subprocess.check_output(["git", "-C", str(dep), "show", PIN + ":" + name])
        if official != (dep / name).read_bytes():
            raise SystemExit("Modified Limine binary: " + name)
        target = stage / dest
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(official)
    (stage / "boot/spiffos-kernel.elf").write_bytes((ROOT / "build/spiffos-kernel.elf").read_bytes())
    (stage / "limine.conf").write_bytes((ROOT / "limine.conf").read_bytes())
    subprocess.run(["xorriso", "-as", "mkisofs", "-R", "-r", "-J",
                    "--efi-boot", "boot/limine/limine-uefi-cd.bin",
                    "-efi-boot-part", "--efi-boot-image", "--protective-msdos-label",
                    str(stage), "-o", str(ROOT / "build/spiffos.iso")], check=True)
    temporary.cleanup()


if __name__ == "__main__":
    main()
