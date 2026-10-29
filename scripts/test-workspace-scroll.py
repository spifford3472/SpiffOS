#!/usr/bin/env python3
"""Test forced workspace scrolling in a clean temporary build, without replacing the normal ISO."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Build the test-only startup variant, verify its screenshot, retain evidence."""
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="spiffos-scroll-case-", dir="/tmp") as directory:
        check = Path(directory)
        for name in ["Makefile", "linker.ld", "limine.conf"]:
            shutil.copy2(ROOT / name, check / name)
        for name in ["include", "src/kernel", "scripts", "tests", "assets/branding"]:
            shutil.copytree(ROOT / name, check / name)
        (check / ".local-dev").mkdir()
        (check / ".local-dev/limine").symlink_to(ROOT / ".local-dev/limine", target_is_directory=True)
        command = ["make", "test-powerup", "BOOT_TEST_FLAGS=-DSPIFFOS_SCROLL_TEST",
                   "POWERUP_CHECK_FLAGS=--powerup --console --workspace --scrolled"]
        log = build / "workspace-scroll-validation.log"
        with log.open("wb") as output:
            result = subprocess.run(command, cwd=check, stdout=output, stderr=subprocess.STDOUT)
        print(log.read_text(), end="")
        if result.returncode != 0:
            raise SystemExit(result.returncode)
        shutil.copyfile(check / "build/framebuffer.ppm", build / "workspace-scrolled.ppm")
        shutil.copyfile(check / "build/serial.log", build / "workspace-scrolled-serial.log")


if __name__ == "__main__":
    main()
