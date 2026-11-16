#!/usr/bin/env python3
"""Run real admitted memory tests, one process per sticky-domain scenario.
Only this runner's child-process core limit is changed; no host configuration.
Reference: ADR-m10-d59b-runtime-memory-wiring.md and AGENTS.md.
"""
from pathlib import Path
import resource
import signal
import subprocess
ROOT = Path(__file__).resolve().parents[1]


def main():
    """Require recoverable refusals, partial-release quarantine and fatal void misuse."""
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    binary = ROOT / "build/m10-d59b-memory-direct-host-test"
    for case in ("happy", "partial-release", "irq", "cpu", "rsp"):
        subprocess.run([str(binary), case], check=True)
    result = subprocess.run([str(binary), "void-retire"])
    if result.returncode != -signal.SIGABRT:
        raise RuntimeError(f"unsafe void runtime lifecycle returned: {result.returncode}")
    print("SPIFFOS_M10_D59B_MEMORY_DIRECT_NEGATIVES_OK", flush=True)


if __name__ == "__main__":
    main()
