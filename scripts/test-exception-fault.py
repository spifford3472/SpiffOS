#!/usr/bin/env python3
"""Prove M9 CPU #UD handling in an isolated QEMU ISO; never fault normal build."""

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    """Build a separate deliberate-UD2 ISO and require a fatal, nonreturning trace.

    No mutation of ROOT build artifacts or local desktop state. The guest must
    never reach BOOT_OK, and a serial failure or QEMU crash fails the test.
    The one expected nonzero runner exit denotes the intentionally halted guest.
    """
    with tempfile.TemporaryDirectory(prefix="spiffos-exception-") as temporary:
        isolated = Path(temporary) / "source"
        shutil.copytree(
            ROOT, isolated, symlinks=True,
            ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        # The pinned dependency belongs to the owner account; this process-
        # local exception does not alter Git's global trust configuration.
        build_env = os.environ.copy()
        build_env.update({
            "GIT_CONFIG_COUNT": "1",
            "GIT_CONFIG_KEY_0": "safe.directory",
            "GIT_CONFIG_VALUE_0": str((ROOT / ".local-dev/limine").resolve()),
        })
        with (Path(temporary) / "build.log").open("wb") as build_log:
            result = subprocess.run(
                ["make", "BOOT_TEST_FLAGS=-DSPIFFOS_EXCEPTION_TEST", "iso"],
                cwd=isolated, env=build_env,
                stdout=build_log, stderr=subprocess.STDOUT,
                check=False, timeout=90)
        if result.returncode != 0:
            raise RuntimeError("Fault variant build failed:\n" +
                               (Path(temporary) / "build.log").read_text()[-3000:])
        with (Path(temporary) / "qemu.log").open("wb") as run_log:
            runner = subprocess.run(
                [sys.executable, "scripts/qemu.py", "test", "--memory-mib", "128",
                 "--timeout", "15"], cwd=isolated, stdout=run_log,
                stderr=subprocess.STDOUT, timeout=25, check=False)
        evidence = (isolated / "build/serial.log").read_bytes().splitlines()
        needed = (
            b"SPIFFOS_IDT_READY",
            b"SPIFFOS_EXCEPTION_TEST_TRIGGER",
            b"SPIFFOS_EXCEPTION:06,0000000000000000",
            b"SPIFFOS_EXCEPTION_HALT")
        assert runner.returncode == 1, "Unexpected QEMU test-runner status"
        # OVMF writes ANSI clear-screen bytes without a trailing newline;
        # the first kernel marker may share that last firmware serial line.
        occurrences = [
            [i for i, line in enumerate(evidence) if line.endswith(marker)]
            for marker in needed
        ]
        assert all(len(indexes) == 1 for indexes in occurrences), evidence
        assert b"SPIFFOS_BOOT_OK" not in evidence
        assert b"SPIFFOS_KEYBOARD_READY" not in evidence
        indices = [indexes[0] for indexes in occurrences]
        assert indices == sorted(indices) and len(set(indices)) == len(indices)
        print("PASS QEMU #UD: exact vector, normalized error, fatal halt, no boot")


if __name__ == "__main__":
    main()
