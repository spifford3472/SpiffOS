#!/usr/bin/env python3
"""Exercise real QEMU startup, pointer policy, keyboard, locking and cleanup.

Run from a built checkout. Only the test's display is replaced with 'none';
actual desktop interactions remain manual. Own and reap only our runner/child.
"""
from pathlib import Path
import json
import os
import runpy
import signal
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
RUNNER = ROOT / 'scripts/qemu.py'


def qmp(path, name, arguments=None):
    """Send one command on a private Unix QMP connection; raise on failure."""
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(3)
        connection.connect(str(path))
        with connection.makefile('rwb') as stream:
            assert 'QMP' in json.loads(stream.readline())
            result = None
            for command, args in [('qmp_capabilities', None), (name, arguments)]:
                request = {'execute': command, 'id': command}
                if args is not None:
                    request['arguments'] = args
                stream.write((json.dumps(request)+'\n').encode()); stream.flush()
                deadline = time.monotonic()+3
                while time.monotonic() < deadline:
                    reply = json.loads(stream.readline())
                    if reply.get('id') == command:
                        assert 'error' not in reply, reply
                        result = reply['return']
                        break
                else:
                    raise RuntimeError('QMP response timed out')
            return result


def exercise(action):
    """Run real launcher; close or interrupt it, then verify child/temp cleanup."""
    before = set(BUILD.glob('ovmf-*'))
    log = BUILD / ('launcher-'+action+'.log')
    harness = ("import runpy,sys; "
               "m=runpy.run_path(sys.argv[1]); "
               "m['main'].__globals__['GTK_DISPLAY']='none'; "
               "sys.argv=[sys.argv[1],'run']; "
               "raise SystemExit(m['main']())")
    child_pid = None
    with log.open('wb') as output:
        runner = subprocess.Popen([sys.executable, '-c', harness, str(RUNNER)],
                                  cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic()+30
            while time.monotonic() < deadline:
                assert runner.poll() is None, log.read_text()
                if log.read_text().count('POINTER:') == 2:
                    break
                time.sleep(.05)
            else:
                raise RuntimeError('Launcher did not complete guest boot')
            private = set(BUILD.glob('ovmf-*'))-before
            assert len(private) == 1, private
            directory = private.pop()
            path = directory/'qmp.sock'
            assert directory.stat().st_mode & 0o077 == 0
            assert (directory/'OVMF_VARS.fd').is_file()
            mice = qmp(path, 'query-mice')
            assert any(m['current'] and m['absolute'] for m in mice), mice
            assert b'SPIFFOS_BOOT_OK' in (BUILD/'run-serial.log').read_bytes().splitlines()
            # Linux exposes exactly the child process started by this runner.
            children = Path(f'/proc/{runner.pid}/task/{runner.pid}/children').read_text().split()
            assert len(children) == 1, children
            child_pid = int(children[0])
            arguments = Path(f'/proc/{child_pid}/cmdline').read_bytes().split(b'\0')
            assert b'none' in arguments and b'virtio-tablet-pci,id=host-pointer' in arguments
            assert b'-net' in arguments and b'-gdb' not in arguments
            blocked = subprocess.run([sys.executable, str(RUNNER), 'run'],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
            assert blocked.returncode != 0 and b'BlockingIOError' in blocked.stdout
            assert runner.poll() is None
            if action == 'close':
                # Same QEMU shutdown/runner cleanup as a frontend close event;
                # this does not claim a desktop close-button interaction.
                runpy.run_path(str(ROOT/'scripts/test-keyboard.py'))['run_keyboard_test'](
                    path, BUILD/'run-serial.log', BUILD)
                qmp(path, 'quit')
                assert runner.wait(timeout=5) == 0
            else:
                runner.send_signal(signal.SIGTERM)
                assert runner.wait(timeout=5) != 0
            assert not Path(f'/proc/{child_pid}').exists(), 'QEMU child leaked'
            assert not directory.exists(), 'Private firmware/socket leaked'
        finally:
            if runner.poll() is None:
                runner.terminate()
                runner.wait(timeout=5)
    print('PASS launcher '+action+': boot, absolute pointer, lock, child/temp cleanup')


def main():
    """Validate supported GTK parsing without a desktop, then real headless runs."""
    display = runpy.run_path(str(RUNNER))['GTK_DISPLAY']
    result = subprocess.run(['qemu-system-x86_64', '-machine', 'q35', '-net', 'none',
                             '-S', '-display', display], env={**os.environ, 'DISPLAY': '',
                             'WAYLAND_DISPLAY': '', 'GDK_BACKEND': 'x11'},
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
    assert result.returncode != 0 and b'gtk initialization failed' in result.stdout, result.stdout
    print('PASS installed QEMU accepted GTK settings; desktop deliberately absent')
    exercise('close')
    exercise('signal')


if __name__ == '__main__':
    main()
