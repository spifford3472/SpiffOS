#!/usr/bin/env python3
"""D5.5c real M10-A task generation bound to a CPU IRQ0-saved frame.

Builds only disposable QEMU ISOs, never touches Forge accepted image,
physical disks or network. AGENTS.md and ADR-m10-d55c-task-frame-budget.md.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def build(src,env,flags):
    """Force all objects to use the exact test-only build flags."""
    run=subprocess.run(["make","-B",f"BOOT_TEST_FLAGS={flags}","iso"],
                       cwd=src,env=env,capture_output=True,timeout=120)
    if run.returncode:
        raise RuntimeError("D55c frame build: "+
            (run.stdout+run.stderr).decode(errors="replace")[-4000:])

def guest(src,mib,marker):
    """Require the real guest serial milestone on actual QEMU IRQ0."""
    run=subprocess.run([sys.executable,"scripts/qemu.py","test",
                        "--memory-mib",str(mib),"--wait-marker",marker,
                        "--timeout","18"],cwd=src,capture_output=True,timeout=23)
    data=(src/"build/serial.log").read_bytes()
    if run.returncode:
        raise RuntimeError(f"D55c M10-A frame guest {mib}MiB: "+repr(data[-1800:]))
    return data

def main():
    """Positive actual owned IRQ frame and wrong generation rejection."""
    with tempfile.TemporaryDirectory(prefix="spiffos-m10-d55c-frame-") as tmp:
        src=Path(tmp)/"source"
        shutil.copytree(ROOT,src,symlinks=True,
            ignore=shutil.ignore_patterns(".git","build","__pycache__"))
        env=os.environ.copy()
        env.update({"GIT_CONFIG_COUNT":"1",
                    "GIT_CONFIG_KEY_0":"safe.directory",
                    "GIT_CONFIG_VALUE_0":str((ROOT/".local-dev/limine").resolve())})
        flag="-DSPIFFOS_M10_D55C_FRAME_GUEST_TEST"
        build(src,env,flag)
        for mib in (128,256,512):
            data=guest(src,mib,"SPIFFOS_KEYBOARD_READY")
            required=[b"SPIFFOS_IRQ0_ENABLED",b"SPIFFOS_BOOT_OK",
                      b"SPIFFOS_M10_D55C_FRAME_OK",b"SPIFFOS_KEYBOARD_READY"]
            positions=[data.find(m) for m in required]
            if any(i<0 for i in positions) or positions!=sorted(positions) or (
                    b"SPIFFOS_M10_D55C_FRAME_ERROR" in data or
                    b"SPIFFOS_EXCEPTION:" in data):
                raise RuntimeError(f"D55c {mib}MiB invalid proof: {data[-1600:]!r}")
            print(f"PASS D5.5c {mib}MiB: real IRQ frame bound to exact RUNNING "
                  "M10-A generation, owned stack reaped, editor restored",flush=True)
        build(src,env,flag+" -DSPIFFOS_M10_D55C_BAD_GENERATION_TEST")
        for mib in (128,256,512):
            data=guest(src,mib,"SPIFFOS_M10_D55C_FRAME_ERROR:FRAME_REJECTED")
            if (b"SPIFFOS_M10_D55C_FRAME_OK" in data or
                    b"SPIFFOS_KEYBOARD_READY" in data or
                    b"SPIFFOS_EXCEPTION:" in data):
                raise RuntimeError(f"D55c stale generation not rejected {mib}MiB")
            print(f"PASS D5.5c negative {mib}MiB: forged IRQ task generation "
                  "rejected without changing CPU frame or entering editor",flush=True)

if __name__=="__main__":
    main()
