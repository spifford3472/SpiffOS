#!/usr/bin/env python3
"""Run only an ISO and private OVMF variables; test, interactive run, or GDB stub."""
from pathlib import Path
import argparse
import fcntl
import json
import socket
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
MARKER = b"SPIFFOS_BOOT_OK"
# Explicit settings avoid a default backend or accidental hover grab.
GTK_DISPLAY = ("gtk,full-screen=off,grab-on-hover=off,show-cursor=on,"
               "window-close=on,zoom-to-fit=on,show-menubar=on,gl=off")


def interrupted(signum, frame):
    """Convert termination into normal cleanup of this runner's own QEMU child."""
    raise KeyboardInterrupt


def capture_framebuffer(socket_path, output_path):
    """Ask this QEMU instance for its rendered screen; raise on QMP errors.

    Uses a private Unix socket and writes a PPM evidence file. QMP messages may
    include asynchronous events, so wait for matching response IDs. No external
    connection or guest memory access; socket resources close on all paths.
    """
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(3)
        connection.connect(str(socket_path))
        with connection.makefile("rwb") as stream:
            greeting = json.loads(stream.readline())
            if "QMP" not in greeting:
                raise RuntimeError("Missing QMP greeting")
            for request in [
                {"execute": "qmp_capabilities", "id": 1},
                {"execute": "screendump", "arguments": {"filename": str(output_path)}, "id": 2}
            ]:
                stream.write((json.dumps(request) + "\n").encode())
                stream.flush()
                while True:
                    response = json.loads(stream.readline())
                    if response.get("id") == request["id"]:
                        if "error" in response:
                            raise RuntimeError("QMP screenshot error: " + str(response["error"]))
                        break


def select_absolute_pointer(socket_path, child, resume=False):
    """Select this VM's tablet before execution or after firmware initialization.

    Borrow child; never touch other processes. Wait at most five seconds for the
    private socket, then use bounded QMP reads. HMP's numeric mouse_set selector
    comes from query-mice, never shell text or external input. Raise on any
    missing tablet, protocol error or failed selection so main reaps its child.
    """
    deadline = time.monotonic() + 5
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(3)
        while True:
            if child.poll() is not None:
                raise RuntimeError("QEMU exited before pointer setup; see run-qemu.log")
            try:
                connection.connect(str(socket_path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() >= deadline:
                    raise RuntimeError("Timed out waiting for private QMP socket")
                time.sleep(0.02)
        with connection.makefile("rwb") as stream:
            if "QMP" not in json.loads(stream.readline()):
                raise RuntimeError("Missing QMP greeting")

            def execute(name, arguments=None):
                """Return one matching QMP reply; ignore asynchronous events."""
                request = {"execute": name, "id": name}
                if arguments is not None:
                    request["arguments"] = arguments
                stream.write((json.dumps(request) + "\n").encode())
                stream.flush()
                # Events must not extend the exchange indefinitely.
                response_deadline = time.monotonic() + 3
                while time.monotonic() < response_deadline:
                    response = json.loads(stream.readline())
                    if response.get("id") == name:
                        if "error" in response:
                            raise RuntimeError("QMP pointer setup: " + str(response["error"]))
                        return response["return"]
                raise RuntimeError("QMP pointer response deadline exceeded")

            execute("qmp_capabilities")
            mice = execute("query-mice")
            tablet = next((m for m in mice if m["absolute"] and
                           m["name"] == "QEMU Virtio Tablet"), None)
            if tablet is None:
                raise RuntimeError("QEMU absolute pointer unavailable")
            execute("human-monitor-command", {
                "command-line": "mouse_set " + str(int(tablet["index"]))})
            if not any(m["index"] == tablet["index"] and m["absolute"] and
                       m["current"] for m in execute("query-mice")):
                raise RuntimeError("QEMU absolute pointer selection failed")
            if resume:
                execute("cont")
    print("POINTER: absolute mode selected; normal focus needs no input grab", flush=True)


def main():
    """Return 0 only for a captured full marker line in test mode; always reap QEMU."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["test", "run", "debug"])
    parser.add_argument("--timeout", type=float, default=20.0, help="test deadline in seconds")
    parser.add_argument("--screenshot", action="store_true", help="test framebuffer marker and capture build/framebuffer.ppm")
    parser.add_argument("--keyboard", action="store_true", help="inject and verify real keyboard events")
    parser.add_argument("--wait-keyboard-ready", action="store_true",
                        help="require serial keyboard readiness without QMP input")
    parser.add_argument("--memory-mib", type=int, default=256,
                        help="guest RAM in MiB (64..4096; diagnostic runner bound)")
    args = parser.parse_args()
    if not 64 <= args.memory_mib <= 4096:
        parser.error("memory-mib must be between 64 and 4096")
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    if args.keyboard and (args.mode != "test" or args.screenshot):
        parser.error("--keyboard requires test mode without --screenshot")
    if args.wait_keyboard_ready and args.mode != "test":
        parser.error("--wait-keyboard-ready requires test mode")
    if args.screenshot and args.mode != "test":
        parser.error("--screenshot requires test mode")
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
                   "-m", str(args.memory_mib) + "M", "-net", "none", "-no-reboot",
                   "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=" + str(code),
                   "-drive", "if=pflash,format=raw,unit=1,file=" + str(variables),
                   "-cdrom", str(build / "spiffos.iso"), "-boot", "d",
                   "-serial", "file:" + str(serial), "-monitor", "none"]
        qmp_socket = Path(directory) / "qmp.sock"
        if args.screenshot or args.keyboard or args.mode == "run":
            command += ["-qmp", "unix:" + str(qmp_socket) + ",server=on,wait=off"]
        if args.mode in ["test", "debug"]:
            command += ["-display", "none"]
        if args.mode == "run":
            command += ["-display", GTK_DISPLAY, "-device",
                        "virtio-tablet-pci,id=host-pointer", "-S"]
        if args.mode == "debug":
            command += ["-S", "-gdb", "tcp:127.0.0.1:1234"]
        print("COMMAND:", " ".join(command), flush=True)
        with (build / (args.mode + "-qemu.log")).open("wb") as diagnostics:
            child = subprocess.Popen(command, cwd=ROOT, stdout=diagnostics, stderr=subprocess.STDOUT)
            success = False
            try:
                if args.mode == "run":
                    print("Windowed GTK: focus to type; Ctrl+Alt+G releases input capture.", flush=True)
                    select_absolute_pointer(qmp_socket, child, resume=True)
                    deadline = time.monotonic() + args.timeout
                    while MARKER not in serial.read_bytes().splitlines():
                        if child.poll() is not None:
                            return child.returncode
                        if time.monotonic() >= deadline:
                            raise RuntimeError("Guest boot deadline exceeded; see run-serial.log")
                        time.sleep(0.1)
                    # Firmware can choose an input device; restore our policy
                    # after it hands control to the native kernel.
                    if child.poll() is None:
                        select_absolute_pointer(qmp_socket, child)
                        child.wait()
                elif args.mode == "debug":
                    child.wait()
                else:
                    deadline = time.monotonic() + args.timeout
                    while time.monotonic() < deadline:
                        lines = serial.read_bytes().splitlines()
                        if MARKER in lines:
                            # Some diagnostics need evidence emitted immediately
                            # after BOOT_OK. Keep QEMU alive until that complete
                            # serial line arrives instead of racing its flush.
                            if (args.wait_keyboard_ready and
                                    b"SPIFFOS_KEYBOARD_READY" not in lines):
                                time.sleep(0.01)
                                continue
                            if args.keyboard:
                                import runpy
                                runpy.run_path(str(ROOT / "scripts/test-keyboard.py"))["run_keyboard_test"](
                                    qmp_socket, serial, build)
                            if args.screenshot:
                                if b"SPIFFOS_FRAMEBUFFER_OK" not in lines:
                                    print("FAIL: framebuffer success marker absent", flush=True)
                                    break
                                capture_framebuffer(qmp_socket, build / "framebuffer.ppm")
                                print("CAPTURED: build/framebuffer.ppm", flush=True)
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
