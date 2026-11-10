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


def verify_screen(path, text, row, initial, cursor, extra_lines=()):
    """Compare every RGB byte to independent text/font/logo/cursor reference data."""
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
    lines.extend(extra_lines)
    for screen_row, line in lines:
        for column, character in enumerate(line):
            for y in range(16):
                bits = ("0" + format(font[character][y//2], "05b") + "00"
                        if y//2 < 7 else "00000000")
                cell = b"".join((ink if bit == "1" else navy) * 2 for bit in bits)
                start = ((screen_row*16+y)*width+column*16)*3
                expected[start:start+48] = cell
    cursor_row, cursor_column, cursor_mode = cursor
    cursor_y = cursor_row * 16
    y_range = range(cursor_y, cursor_y + 16)
    if cursor_mode == "insert":
        y_range = range(cursor_y + 14, cursor_y + 16)
    for y in y_range:
        for x in range(cursor_column * 16, cursor_column * 16 + 16):
            offset = (y * width + x) * 3
            expected[offset:offset+3] = bytes(255-value for value in expected[offset:offset+3])
    if actual != expected:
        index = next(i for i, (a,b) in enumerate(zip(actual, expected)) if a != b)
        raise RuntimeError(f"Unexpected keyboard screenshot pixel {index//3%width},{index//3//width}")
    print(f"PASS every keyboard screenshot pixel: {path.name} ({width}x{height})")
    return height // 16


def run_keyboard_test(socket_path, serial, build):
    """Drive real emulated keys; verify named events, submissions and full frames."""
    wait_until(lambda: b"SPIFFOS_KEYBOARD_READY" in serial.read_bytes().splitlines(),
               "keyboard ready")
    expected = []
    expected_events = []
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
                """Return accepted text-key diagnostics in their original order."""
                return [line for line in serial.read_bytes().splitlines()
                        if line.startswith(b"KEY:")]

            def events():
                """Return named editor make events; break codes must add nothing."""
                return [line for line in serial.read_bytes().splitlines()
                        if line.startswith(b"EVENT:")]

            def submitted():
                """Return complete diagnostic logical-line submissions in order."""
                return [line[7:] for line in serial.read_bytes().splitlines()
                        if line.startswith(b"SUBMIT:")]

            def key(codes, character=None, named=None):
                """Inject one key combination and verify exact text/event traces."""
                rpc("send-key", {"keys":[{"type":"qcode","data":code} for code in codes],
                                 "hold-time":20})
                if character is not None:
                    expected.append(f"KEY:{ord(character):02X}".encode())
                    wait_until(lambda: len(accepted()) >= len(expected),
                               "accepted key " + repr(character))
                if named is not None:
                    expected_events.append(("EVENT:" + named).encode())
                    wait_until(lambda: len(events()) >= len(expected_events),
                               "named event " + named)
                time.sleep(0.04)
                assert accepted() == expected, (accepted(), expected)
                assert events() == expected_events, (events(), expected_events)

            rpc("qmp_capabilities")
            # M7: real modifier chord must preserve both independent pixel
            # images, editor state, and existing boot workspace unchanged.
            initial_path = build / "m7-initial.ppm"
            rpc("screendump", {"filename":str(initial_path)})
            key(["ctrl", "alt", "2"], named="WORKSPACE_OK")
            second_path = build / "m7-second.ppm"
            rpc("screendump", {"filename":str(second_path)})
            assert initial_path.read_bytes() != second_path.read_bytes()
            key(["z"], "Z")
            key(["ctrl", "alt", "1"], named="WORKSPACE_OK")
            restored_path = build / "m7-restored.ppm"
            rpc("screendump", {"filename":str(restored_path)})
            assert initial_path.read_bytes() == restored_path.read_bytes(), "workspace 1 framebuffer changed"
            key(["ctrl", "alt", "2"], named="WORKSPACE_OK")
            second_restored = build / "m7-second-restored.ppm"
            rpc("screendump", {"filename":str(second_restored)})
            assert second_path.read_bytes() != second_restored.read_bytes(), "workspace 2 text lost"
            key(["ctrl", "alt", "1"], named="WORKSPACE_OK")
            print("PASS M7 real QEMU Ctrl+Alt+1/2, framebuffer restore and independent text")
            for workspace_id in range(3, 10):
                key(["ctrl", "alt", str(workspace_id)], named="WORKSPACE_OK")
                key(["ctrl", "alt", "1"], named="WORKSPACE_OK")
                restored_nine = build / f"m7-return-{workspace_id}.ppm"
                rpc("screendump", {"filename":str(restored_nine)})
                assert initial_path.read_bytes() == restored_nine.read_bytes(), (
                    f"workspace {workspace_id} corrupted workspace 1")
            print("PASS M7 all nine workspace chords and exact workspace 1 restoration")
            key(["backspace"])
            key(["ctrl","a"])
            key(["alt","b"])
            key(["up"], named="UP")
            key(["down"], named="DOWN")
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
            rows = verify_screen(path, "SPIFFOS 123.:?", 15, True,
                                 (15, 14, "overwrite"))

            key(["left"], named="LEFT")
            key(["left"], named="LEFT")
            key(["insert"], named="INSERT")
            path = build / "keyboard-insert.ppm"
            rpc("screendump", {"filename":str(path)})
            verify_screen(path, "SPIFFOS 123.:?", 15, True,
                          (15, 12, "insert"))
            key(["x"], "X")
            key(["insert"], named="INSERT")
            key(["y"], "Y")
            first_text = b"SPIFFOS 123.XY?"
            key(["ret"], "\n")
            wait_until(lambda: submitted() == [first_text],
                       "first complete edited logical submission")

            key(["backspace"])
            long_text = "A" * 80 + "B" * 80 + "CDE"
            for character in long_text:
                key([character.lower()], character)
            assert submitted() == [first_text]
            key(["backspace"], "\b")
            key(["backspace"], "\b")
            for character in "QR":
                key([character.lower()], character)
            expected_long = (long_text[:-2] + "QR").encode()
            key(["ret"], "\n")
            wait_until(lambda: submitted()[-1:] == [expected_long],
                       "three-row complete logical submission")
            assert submitted()[:2] == [first_text, expected_long]
            for _ in range(rows+20):
                key(["ret"], "\n")
            for character in "SCROLL OK.":
                code = "spc" if character == " " else "dot" if character == "." else character.lower()
                key([code], character)
            path = build / "keyboard-scrolled.ppm"
            rpc("screendump", {"filename":str(path)})
            verify_screen(path, "SCROLL OK.", rows-1, False,
                          (rows-1, 10, "overwrite"))
            # Edit an earlier visible line near the bottom. Grow from one row
            # to two, prove a following line moves intact, then shrink it again.
            for _ in range(5):
                key(["up"], named="UP")
            for _ in range(10):
                key(["left"], named="LEFT")
            for _ in range(79):
                key(["a"], "A")
            key(["ret"], "\n")
            for _ in range(4):
                key(["b"], "B")
            key(["up"], named="UP")
            for _ in range(75):
                key(["right"], named="RIGHT")
            key(["insert"], named="INSERT")
            key(["x"], "X")
            key(["y"], "Y")
            path = build / "keyboard-reflow.ppm"
            rpc("screendump", {"filename":str(path)})
            verify_screen(path, "A" * 79 + "X", rows-6, False,
                          (rows-5, 1, "insert"),
                          [(rows-5, "Y"), (rows-4, "BBBB")])
            key(["backspace"], "\b")
            path = build / "keyboard-shrink.ppm"
            rpc("screendump", {"filename":str(path)})
            verify_screen(path, "A" * 79 + "X", rows-6, False,
                          (rows-5, 0, "insert"), [(rows-5, "BBBB")])
            key(["right"], named="RIGHT")
            key(["insert"], named="INSERT")
            key(["c"], "C")
            key(["ret"], "\n")
            wait_until(lambda: submitted()[-1:] == [b"BCBB"],
                       "preserved following line submitted after reflow")
            # Verify every US punctuation glyph and keypad byte in a fresh row,
            # retaining previous reflow content in independent full-frame checks.
            punctuation = ""
            for code, character in [('minus', '-'), ('equal', '='), ('bracket_left', '['), ('bracket_right', ']'), ('semicolon', ';'), ('apostrophe', "'"), ('grave_accent', '`'), ('backslash', '\\'), ('comma', ','), ('dot', '.'), ('slash', '/')]:
                key([code], character)
                punctuation += character
            for code, character in [('1', '!'), ('2', '@'), ('3', '#'), ('4', '$'), ('5', '%'), ('6', '^'), ('7', '&'), ('8', '*'), ('9', '('), ('0', ')'), ('minus', '_'), ('equal', '+'), ('bracket_left', '{'), ('bracket_right', '}'), ('semicolon', ':'), ('apostrophe', '"'), ('grave_accent', '~'), ('backslash', '|'), ('comma', '<'), ('dot', '>'), ('slash', '?')]:
                key(["shift", code], character)
                punctuation += character
            keypad = "0123456789.+-*/"
            codes = ["kp_0","kp_1","kp_2","kp_3","kp_4","kp_5","kp_6","kp_7","kp_8","kp_9",
                     "kp_decimal","kp_add","kp_subtract","kp_multiply","kp_divide"]
            for code, character in zip(codes, keypad):
                key([code], character)
            text = punctuation + keypad
            row = rows - 4
            previous = [(rows-6, "A" * 79 + "X"), (rows-5, "BCBB")]
            path = build / "keyboard-symbols.ppm"
            rpc("screendump", {"filename":str(path)})
            verify_screen(path, text, row, False, (row,len(text),"overwrite"), previous)
            key(["left"], named="LEFT")
            key(["left"], named="LEFT")
            offset = len(text) - 2
            key(["delete"], named="DELETE")
            text = text[:offset] + text[offset+1:]
            path = build / "keyboard-delete.ppm"
            rpc("screendump", {"filename":str(path)})
            verify_screen(path, text, row, False, (row,offset,"overwrite"), previous)
            key(["num_lock"])
            key(["kp_4"], named="LEFT")
            offset -= 1
            key(["kp_decimal"], named="DELETE")
            text = text[:offset] + text[offset+1:]
            key(["shift","kp_1"], "1")
            text = text[:offset] + "1" + text[offset+1:]
            key(["num_lock"])
            key(["kp_enter"], "\n")
            wait_until(lambda: submitted()[-1:] == [text.encode()], "punctuation/keypad/Delete submission")
            assert b"SPIFFOS_KEYBOARD_BYTE_OK" in serial.read_bytes().splitlines()
            (build / "keyboard-serial.log").write_bytes(serial.read_bytes())
            print(f"PASS real QEMU screen editor: {len(expected)} text bytes, "
                  f"{len(expected_events)} named events, reflow, submission and scrolling")
