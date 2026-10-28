#!/usr/bin/env python3
"""Run only an ISO and private OVMF variables; test, interactive run, or GDB stub."""
from pathlib import Path
import argparse
import fcntl
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
MARKER = b"SPIFFOS_BOOT_OK"


def interrupted(signum, frame):
    """Convert termination into normal cleanup of this runner's own QEMU child."""
    raise KeyboardInterrupt


def main():
    """Return 0 only for a captured full marker line in test mode; always reap QEMU."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["test", "run", "debug"])
    parser.add_argument("--timeout", type=float, default=20.0, help="test deadline in seconds")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    # Refuse competing runs sharing logs or the local GDB port.
    lock = (build / "qemu.lock").open("w")
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    code = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
    source_vars = Path("/usr/share/OVMF/OVMF_VARS_4M.fd")
    for required in [code, source_vars, build / "spiffos.iso"]:
        if not required.is_file():
            raise SystemExit("Missing input: " + str(required))
    serial = build / ("serial.log" if args.mode == "test" else args.mode + "-serial.log")
    serial.write_bytes(b"")
    signal.signal(signal.SIGTERM, interrupted)
    with tempfile.TemporaryDirectory(prefix="ovmf-", dir=build) as directory:
        variables = Path(directory) / "OVMF_VARS.fd"
        shutil.copyfile(source_vars, variables)
        command = ["qemu-system-x86_64", "-machine", "q35", "-accel", "tcg",
                   "-m", "256M", "-net", "none", "-no-reboot",
                   "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=" + str(code),
                   "-drive", "if=pflash,format=raw,unit=1,file=" + str(variables),
                   "-cdrom", str(build / "spiffos.iso"), "-boot", "d",
                   "-serial", "file:" + str(serial), "-monitor", "none"]
        if args.mode in ["test", "debug"]:
            command += ["-display", "none"]
        if args.mode == "debug":
            command += ["-S", "-gdb", "tcp:127.0.0.1:1234"]
        print("COMMAND:", " ".join(command), flush=True)
        with (build / (args.mode + "-qemu.log")).open("wb") as diagnostics:
            child = subprocess.Popen(command, cwd=ROOT, stdout=diagnostics, stderr=subprocess.STDOUT)
            success = False
            try:
                if args.mode != "test":
                    child.wait()
                else:
                    deadline = time.monotonic() + args.timeout
                    while time.monotonic() < deadline:
                        if MARKER in serial.read_bytes().splitlines():
                            success = True
                            break
                        if child.poll() is not None:
                            break
                        time.sleep(0.1)
            finally:
                if child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()
        if args.mode == "test":
            print(serial.read_text(errors="replace"), end="")
            print("PASS: exact serial marker captured" if success else "FAIL: serial marker absent")
            return 0 if success else 1
        return child.returncode


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
