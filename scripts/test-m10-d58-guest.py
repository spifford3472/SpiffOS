#!/usr/bin/env python3
"""M10-D5.8a real IRQ0 diagnostic observation and supervisor drain test.

Build flags exclusively isolated from normal EDIT/BASIC kernel; disposable
QEMU with -net none, no physical disks. See ADR-m10-d58-irq-mutex-integration.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ("-DSPIFFOS_M10_D56_IRQ_TEST -DSPIFFOS_M10_D57_IRQ_TEST "
         "-DSPIFFOS_M10_D58_NOTIFY_TEST")

def main():
    """Prove queue enqueue/drain with real timer IRQ0 on all RAM sizes."""
    with tempfile.TemporaryDirectory(prefix="spiffos-d58-irq-") as tmp:
        src = Path(tmp)/"source"
        shutil.copytree(ROOT, src, symlinks=True,
                        ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        env = os.environ.copy()
        env.update({"GIT_CONFIG_COUNT":"1", "GIT_CONFIG_KEY_0":"safe.directory",
                    "GIT_CONFIG_VALUE_0":str((ROOT/".local-dev/limine").resolve())})
        built = subprocess.run(["make", "-B", f"BOOT_TEST_FLAGS={FLAGS}", "iso"],
                               cwd=src, env=env, capture_output=True, timeout=150)
        if built.returncode:
            raise RuntimeError("D58 build failed: "+
                               (built.stdout+built.stderr).decode(errors="replace")[-5000:])
        for mib in (128,256,512):
            run = subprocess.run([sys.executable,"scripts/qemu.py","test",
                                  "--memory-mib",str(mib),"--wait-marker",
                                  "SPIFFOS_KEYBOARD_READY","--timeout","24"],
                                 cwd=src,capture_output=True,timeout=30)
            serial = (src/"build/serial.log").read_bytes()
            if run.returncode or b"SPIFFOS_M10_D56_IRQ_ERROR" in serial:
                raise RuntimeError(f"D58 QEMU {mib}MiB: "+repr(serial[-1800:]))
            required=[b"SPIFFOS_BOOT_OK",b"D58_IRQ_NOTICES_DRAINED:",
                      b"SPIFFOS_M10_D56_IRQ_OK",b"SPIFFOS_KEYBOARD_READY"]
            positions=[serial.find(x) for x in required]
            if any(x<0 for x in positions) or positions!=sorted(positions):
                raise RuntimeError(f"D58 missing/out-of-order QEMU {mib}: "+repr(serial[-1600:]))
            def value(name):
                m=re.search(rb"(?m)^"+name.encode()+rb":([0-9]+)\r?$",serial)
                if m is None:
                    raise RuntimeError(f"D58 missing metric {name}: "+repr(serial[-1500:]))
                return int(m.group(1))
            drained=value("D58_IRQ_NOTICES_DRAINED")
            workers=(value("D56_WORKER0_TICKS"),value("D56_WORKER1_TICKS"))
            gap=value("D56_MAX_UNSERVICED_IRQ_TICKS")
            if drained<120 or min(workers)<12 or gap>8:
                raise RuntimeError(f"D58 QEMU {mib} invalid {drained=} {workers=} {gap=}")
            print(f"PASS D58 QEMU {mib}MiB: {drained} genuine IRQ notices "
                  f"drained off-IRQ by supervisor, workers={workers}, "
                  f"max unserviced ticks={gap}",flush=True)

if __name__ == "__main__":
    main()
