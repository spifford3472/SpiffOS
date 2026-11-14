#!/usr/bin/env python3
"""D5.6 real authoritative M10-A IRQ0 handoff diagnostic on disposable QEMU.

Only boot-owned temporary ISO/OVMF, QEMU -net none, no physical disks.
References: AGENTS.md and ADR-m10-d56-irq-handoff.md.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def image(src,env,flags):
    """Build fresh private ISO with exact diagnostic-only C/assembly flags."""
    proc=subprocess.run(["make","-B",f"BOOT_TEST_FLAGS={flags}","iso"],
        cwd=src,env=env,capture_output=True,timeout=150)
    if proc.returncode:
        raise RuntimeError("D56 test-only compilation: "+
            (proc.stdout+proc.stderr).decode(errors="replace")[-5200:])

def boot(src,mib,marker):
    """Verify a guest actually reached the requested serial milestone."""
    run=subprocess.run([sys.executable,"scripts/qemu.py","test",
                        "--memory-mib",str(mib),"--wait-marker",marker,
                        "--timeout","24"],cwd=src,capture_output=True,
                       timeout=30)
    data=(src/"build/serial.log").read_bytes()
    if run.returncode:
        raise RuntimeError(f"D56 QEMU {mib}MiB missing {marker}: "+
            repr(data[-1800:])+" "+(run.stdout+run.stderr).decode(
                errors="replace")[-900:])
    return data

def main():
    """Success and two fail-closed wrong-frame/critical exclusion builds."""
    with tempfile.TemporaryDirectory(prefix="spiffos-d56-irq-") as tmp:
        src=Path(tmp)/"source"
        shutil.copytree(ROOT,src,symlinks=True,
            ignore=shutil.ignore_patterns(".git","build","__pycache__"))
        env=os.environ.copy()
        env.update({"GIT_CONFIG_COUNT":"1",
                    "GIT_CONFIG_KEY_0":"safe.directory",
                    "GIT_CONFIG_VALUE_0":str((ROOT/".local-dev/limine").resolve())})
        base="-DSPIFFOS_M10_D56_IRQ_TEST"
        image(src,env,base)
        for mib in (128,256,512):
            serial=boot(src,mib,"SPIFFOS_KEYBOARD_READY")
            marks=[b"SPIFFOS_IRQ0_ENABLED",b"SPIFFOS_BOOT_OK",
                   b"SPIFFOS_M10_D56_IRQ_OK",b"SPIFFOS_KEYBOARD_READY"]
            positions=[serial.find(x) for x in marks]
            if (any(x<0 for x in positions) or positions!=sorted(positions) or
                    b"SPIFFOS_M10_D56_IRQ_ERROR" in serial or
                    b"SPIFFOS_EXCEPTION:" in serial):
                raise RuntimeError(f"D56 QEMU {mib}MiB wrong boot {serial[-1800:]!r}")
            print(f"PASS D56 QEMU {mib}MiB: two real nonyielding M10-A "
                  "IRQ-preempted workers, supervisor completion, "
                  "off-stack reclaim, editor restored",flush=True)
        for name,flags,expected in (
            ("frame",base+" -DSPIFFOS_M10_D56_BAD_TARGET_TEST",
             "SPIFFOS_M10_D56_IRQ_ERROR:TARGET_FRAME_DENIED"),
            ("critical",base+" -DSPIFFOS_M10_D56_BAD_CRITICAL_TEST",
             "SPIFFOS_M10_D56_IRQ_ERROR:CRITICAL_DENIED"),
        ):
            image(src,env,flags)
            for mib in (128,256,512):
                serial=boot(src,mib,expected)
                if (b"SPIFFOS_M10_D56_IRQ_OK" in serial or
                    b"SPIFFOS_KEYBOARD_READY" in serial or
                    b"SPIFFOS_EXCEPTION:" in serial):
                    raise RuntimeError(f"D56 {name} negative accepted: {serial[-1400:]!r}")
                print(f"PASS D56 negative {name} QEMU {mib}MiB: "
                      "rejected before unsafe task dispatch",flush=True)

if __name__=="__main__":
    main()
