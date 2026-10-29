# ADR — Safe interactive QEMU launcher

Status: ACCEPTED objective by owner, 2026-10-29; implementation details below.
Read AGENTS.md, accepted/open, system overview, ADR-011, ADR-003 and keyboard ADR.

## Context and decision
The owner recovered desktop control using Ctrl+Alt+G. Avoid implicit frontend
selection and accidental relative-mouse capture. Normal run explicitly uses GTK,
windowed, software rendering, visible host pointer, zoom to fit, window-close on,
and grab-on-hover off. Keyboard focus passes keys without an exclusive grab.

GTK 8.2.2 still captures on relative-pointer clicks, even with hover grabbing off.
A launcher-only virtio-tablet-pci supplies an absolute pointer. Before guest CPU
execution, a private Unix QMP connection selects that pointer using query-mice
and the bounded HMP mouse_set command, then verifies current+absolute. No guest
mouse driver, USB changes or mouse feature is implemented. If setup fails, stop
only this runner's child and fail; do not continue with accidental capture.
Start paused and continue only after pointer selection. Firmware may affect input
selection during boot; validation must check it again after SPIFFOS_BOOT_OK.

Keep firmware CODE read-only, use private VARS, disable guest networking, retain
per-checkout locking and reap only the child started by this runner. QMP is a
private Unix socket; debug remains headless and bound to 127.0.0.1. Test/debug
commands retain their existing behavior. Closing GTK exits QEMU, allowing the
runner's existing finally block and temporary-directory context to clean up.

## Alternatives and consequences
Only disabling hover grab is insufficient (GTK click path still grabs).
An ordinary USB tablet depends on guest/firmware activation; explicitly selecting
an emulated PCI tablet avoids adding USB support. Custom QEMU patches, VNC and a
new viewer would add dependencies. SDL is installed but offers no documented
simple no-click-grab flag. GTK has the needed resizing and close settings.
The launcher gains a small QMP startup handshake. It is not a guest input ABI.
An explicit user request to Grab Input or go fullscreen remains possible.
Ctrl+Alt+G toggles Grab Input in GTK; normal focus differs from exclusive capture.

## Evidence and validation
Installed QEMU 8.2.2, GTK/SDL backends and installed system manual inspected.
Reference implementation: QEMU v8.2.2 ui/gtk.c (gd_button_event, gd_menu_grab_input,
gd_menu_init), ui/input.c (qemu_input_is_absolute), hw/input/virtio-input-hid.c.
A real headless QMP probe selected QEMU Virtio Tablet and verified current+absolute
without a guest driver. Record subsequent build, real guest and cleanup checks
in docs/operations/safe-launcher-2026-10-29.md. Desktop focus, resizing, grab release
and clicking close require owner observation unless exercised explicitly.

Rollback: build bbb2d4d in a separate worktree. Preserve current history/index.
No owner emulator is closed and no physical media is touched.
