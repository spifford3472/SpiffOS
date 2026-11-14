#!/usr/bin/env python3
"""D5.5c isolated real IRQ0 tick-service monitor with non-yielding D4 workers.

No live editor preemption, no owner ISO changes, no disk/network access.
References: AGENTS.md and ADR-m10-d55c-task-frame-budget.md.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def build(src, env, flags):
    """Rebuild all guest objects with diagnostic flags in temporary source."""
    run = subprocess.run(["make", "-B", f"BOOT_TEST_FLAGS={flags}", "iso"],
                         cwd=src, env=env, capture_output=True, timeout=120)
    if run.returncode:
        raise RuntimeError("D55c guest build: " +
                           (run.stdout+run.stderr).decode(errors="replace")[-4000:])

def boot(src, mib, marker):
    """Require an actual serial marker from guest QEMU execution."""
    run = subprocess.run([sys.executable, "scripts/qemu.py", "test",
                          "--memory-mib", str(mib),
                          "--wait-marker", marker, "--timeout", "20"],
                         cwd=src, capture_output=True, timeout=27)
    data = (src/"build/serial.log").read_bytes()
    if run.returncode:
        raise RuntimeError(f"D55c QEMU {mib}MiB: " + repr(data[-1600:]))
    return data

def metric(data, name):
    """Read only actual guest-reported measurements, never inferred values."""
    match = re.search(rb"(?m)^"+name.encode()+rb":([0-9]+)\r?$", data)
    if not match:
        raise RuntimeError(f"missing {name}: {data[-1000:]!r}")
    return int(match.group(1))

def main():
    """Check actual service completions and intentionally missed deadline."""
    with tempfile.TemporaryDirectory(prefix="spiffos-m10-d55c-") as tmp:
        src=Path(tmp)/"source"
        shutil.copytree(ROOT,src,symlinks=True,
                        ignore=shutil.ignore_patterns(".git","build","__pycache__"))
        env=os.environ.copy()
        env.update({"GIT_CONFIG_COUNT":"1",
                    "GIT_CONFIG_KEY_0":"safe.directory",
                    "GIT_CONFIG_VALUE_0":str((ROOT/".local-dev/limine").resolve())})
        flags="-DSPIFFOS_M10_D4_COMPLETE_TEST -DSPIFFOS_M10_D55C_TICK_TEST"
        build(src,env,flags)
        for mib in (128,256,512):
            data=boot(src,mib,"SPIFFOS_KEYBOARD_READY")
            markers=[b"SPIFFOS_IRQ0_ENABLED",b"SPIFFOS_BOOT_OK",
                     b"SPIFFOS_M10_D55C_BUDGET_OK",b"SPIFFOS_KEYBOARD_READY"]
            positions=[data.find(x) for x in markers]
            if any(x<0 for x in positions) or positions!=sorted(positions) or (
                b"SPIFFOS_M10_D4_COMPLETE_ERROR" in data or
                b"SPIFFOS_EXCEPTION:" in data):
                raise RuntimeError(f"D55c {mib}MiB invalid boot {data[-1800:]!r}")
            completed=metric(data,"D55C_VERIFIED_SUPERVISOR_SERVICE_COMPLETIONS")
            gap=metric(data,"D55C_MAX_OBSERVED_UNSERVICED_TICKS")
            ticks=metric(data,"D4_TOTAL_IRQ_TICKS")
            if completed<12 or gap>8 or ticks<48:
                raise RuntimeError(f"D55c {mib}MiB invalid {completed=} {gap=} {ticks=}")
            print(f"PASS D5.5c {mib}MiB real IRQ0: nonyielding workers, "
                  f"verified supervisor services={completed}, max unserviced "
                  f"gap={gap} ticks, total IRQs={ticks}",flush=True)
        build(src,env,flags+" -DSPIFFOS_M10_D55C_BAD_BUDGET_TEST")
        for mib in (128,256,512):
            data=boot(src,mib,"SPIFFOS_M10_D4_COMPLETE_ERROR:D4_QUOTA_OR_RECOVERY")
            if (b"SPIFFOS_M10_D55C_BUDGET_OK" in data or
                    b"SPIFFOS_KEYBOARD_READY" in data or
                    b"SPIFFOS_EXCEPTION:" in data):
                raise RuntimeError(f"D55c negative {mib}MiB accepted {data[-1600:]!r}")
            print(f"PASS D5.5c negative {mib}MiB: missed service deadline latched",flush=True)

if __name__=="__main__":
    main()
