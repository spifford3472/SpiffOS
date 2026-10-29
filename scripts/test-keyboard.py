#!/usr/bin/env python3
"""Real QMP keyboard events and independent full-frame reference checks."""
from pathlib import Path
import json
import socket
import time

ROOT = Path(__file__).resolve().parents[1]


def wait_until(predicate, description, seconds=5):
    """Bound a guest observation wait; raise instead of accepting missing evidence."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise RuntimeError("Timed out: " + description)


def verify_screen(path, text, row, initial):
    """Compare every RGB byte to independent text/font/logo reference data."""
    magic, size, maximum, actual = path.read_bytes().split(b"\n", 3)
    width, height = map(int, size.split())
    assert magic == b"P6" and maximum == b"255"
    assert len(actual) == width * height * 3
    navy, ink = bytes((9, 27, 53)), bytes((160, 160, 255))
    expected = bytearray(navy * width * height)
    if initial:
        logo = (ROOT / "assets/branding/spiffos-boot-logo.rgb").read_bytes()
        left = (width - 400) // 2
        for y in range(160):
            start = ((y + 24) * width + left) * 3
            expected[start:start+1200] = logo[y*1200:(y+1)*1200]
    font = json.loads((ROOT / "tests/fixtures/console-font.json").read_text())
    lines = [(13, "SPIFFOS WORKSPACE"), (14, "OUTPUT SCROLLS THE LOGO.")] if initial else []
    lines.append((row, text))
    for screen_row, line in lines:
        for column, character in enumerate(line):
            for y in range(16):
                bits = ("0" + format(font[character][y//2], "05b") + "00"
                        if y//2 < 7 else "00000000")
                cell = b"".join((ink if bit == "1" else navy) * 2 for bit in bits)
                start = ((screen_row*16+y)*width+column*16)*3
                expected[start:start+48] = cell
    if actual != expected:
        index = next(i for i, (a,b) in enumerate(zip(actual, expected)) if a != b)
        raise RuntimeError(f"Unexpected keyboard screenshot pixel {index//3%width},{index//3//width}")
    print(f"PASS every keyboard screenshot pixel: {path.name} ({width}x{height})")
    return height // 16


def run_keyboard_test(socket_path, serial, build):
    """Drive real emulated keys after readiness; verify serial and two full frames."""
    wait_until(lambda: b"SPIFFOS_KEYBOARD_READY" in serial.read_bytes().splitlines(),
               "keyboard ready")
    expected = []
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(str(socket_path))
        with connection.makefile("rwb") as stream:
            assert "QMP" in json.loads(stream.readline())
            request_id = 0

            def rpc(command, arguments=None):
                """Send one QMP request, skipping events until the matching reply."""
                nonlocal request_id
                request_id += 1
                request = {"execute": command, "id": request_id}
                if arguments is not None:
                    request["arguments"] = arguments
                stream.write((json.dumps(request)+"\n").encode())
                stream.flush()
                while True:
                    response = json.loads(stream.readline())
                    if response.get("id") == request_id:
                        if "error" in response:
                            raise RuntimeError(str(response["error"]))
                        return

            def accepted():
                """Return only accepted-key byte diagnostics in their original order."""
                return [line for line in serial.read_bytes().splitlines() if line.startswith(b"KEY:")]

            def submitted():
                """Return complete diagnostic logical-line submissions in order."""
                return [line[7:] for line in serial.read_bytes().splitlines()
                        if line.startswith(b"SUBMIT:")]

            def key(codes, character=None):
                """Inject key combination, wait for releases and check exact accepted trace."""
                rpc("send-key", {"keys":[{"type":"qcode","data":code} for code in codes],
                                 "hold-time":20})
                if character is not None:
                    expected.append(f"KEY:{ord(character):02X}".encode())
                    wait_until(lambda: len(accepted()) >= len(expected), "accepted key " + repr(character))
                time.sleep(0.04)  # QEMU key-up timer must finish before the next combination.
                assert accepted() == expected, (accepted(), expected)

            rpc("qmp_capabilities")
            key(["backspace"])  # Must not erase boot output.
            key(["ctrl","a"])
            key(["alt","b"])
            key(["up"])
            for character in "spiffox":
                key([character], character.upper())
            key(["backspace"], "\b")
            key(["s"], "S")
            for code, character in [("spc"," "),("1","1"),("2","2"),("3","3"),("dot",".")]:
                key([code], character)
            key(["shift","semicolon"], ":")
            key(["shift","slash"], "?")
            path = build / "keyboard-typed.ppm"
            rpc("screendump", {"filename":str(path)})
            rows = verify_screen(path, "SPIFFOS 123.:?", 15, True)
            key(["ret"], "\n")
            wait_until(lambda: submitted() == [b"SPIFFOS 123.:?"],
                       "first complete logical submission")
            key(["backspace"])  # Cannot cross a submitted line boundary.
            long_text = "A" * 80 + "B" * 80 + "CDE"
            for character in long_text:
                key([character.lower()], character)
            assert submitted() == [b"SPIFFOS 123.:?"]  # Soft wraps never submit.
            key(["backspace"], "\b")
            key(["backspace"], "\b")
            for character in "QR":
                key([character.lower()], character)
            expected_long = (long_text[:-2] + "QR").encode()
            key(["ret"], "\n")
            wait_until(lambda: submitted()[-1:] == [expected_long],
                       "three-row complete logical submission")
            assert submitted()[:2] == [b"SPIFFOS 123.:?", expected_long]
            for _ in range(rows+20):
                key(["ret"], "\n")
            for character in "SCROLL OK.":
                code = "spc" if character == " " else "dot" if character == "." else character.lower()
                key([code], character)
            path = build / "keyboard-scrolled.ppm"
            rpc("screendump", {"filename":str(path)})
            verify_screen(path, "SCROLL OK.", rows-1, False)
            assert b"SPIFFOS_KEYBOARD_BYTE_OK" in serial.read_bytes().splitlines()
            (build / "keyboard-serial.log").write_bytes(serial.read_bytes())
            print(f"PASS real QEMU keyboard input: {len(expected)} accepted bytes, correction, modifiers, Enter and logo scrolling")
