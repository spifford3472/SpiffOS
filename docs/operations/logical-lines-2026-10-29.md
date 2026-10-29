# Stored logical lines and soft wrapping — 2026-10-29

## Authorization and design decisions

The owner authorized direct ChatGPT architecture, implementation, tests and
documentation in small checkpoints. No delegated model or agent was used.
The owner selected default overwrite with Insert toggling insert mode, then
accepted push-down reflow when an edited logical line grows into another row.
See ../decisions/ADR-screen-editor-logical-lines.md.

This checkpoint implements only stored append-at-end input, full-width soft
wrapping, complete Enter submission, cross-wrap Backspace and explicit capacity.
Arrow navigation, arbitrary screen editing, cursor rendering, Insert mode and
push-down reflow remain the next checkpoint and are not claimed here.

Work remains in /home/geoff/projects/spiffos-screen-editor on
feat/safe-qemu-screen-editor. Original /home/geoff/projects/spiffos and its
staged start.S, untracked drafts and running QEMU are preserved. The verified
pre-work snapshot remains /home/geoff/projects/spiffos-prescreen-edit-20261029T062442Z.

## Implementation

logical-line.c/.h owns no allocation. A caller supplies bounded writable storage;
the module retains byte length, appends atomically, deletes only within the
logical line, provides one complete counted immutable view to a synchronous
submission callback, and resets without freeing storage. It never derives text
from framebuffer pixels.

keyboard-console.c now borrows a logical line and the text console. All display
columns are usable. Crossing the final column uses text_console's ordinary wrap,
without storing a newline or invoking submission. Enter calls the callback once
with the complete byte sequence. An exact-width line reuses the already selected
continuation row and does not add a blank physical row. Backspace maps the last
stored byte to its visible cell and can cross a soft-wrap boundary.

text_console now counts physical rows removed by scrolling. The logical input
stores its absolute start row, so the complete prefix remains in memory when it
has scrolled off screen. The 4096-byte diagnostic command buffer is static
kernel storage, avoiding Limine's entry stack. It is an implementation capacity,
not a BASIC language limit or final ABI.

The boot diagnostic submission callback writes SUBMIT: followed by the complete
counted command to serial. It does not parse or execute BASIC. Capacity rejection
returns KEYBOARD_CONSOLE_FULL, preserves all existing bytes and pixels, and the
kernel emits SPIFFOS_INPUT_FULL. Input errors emit SPIFFOS_INPUT_ERROR and halt
instead of submitting partial text.

## Actual validation

make test-keyboard-memory passed under AddressSanitizer and
UndefinedBehaviorSanitizer:

- Pure logical-line boundary, four-byte capacity, complete ordered submission,
  reset and Backspace tests.
- Exact eight-column input and the ninth character on the next row.
- Input spanning three physical rows with no callback before Enter.
- Backspace immediately across the wrap mapping and continued append.
- Complete 24-byte submission exactly once.
- Exact-width Enter without an extra blank physical row.
- Explicit capacity rejection with byte/pixel state unchanged.
- A 48-byte line scrolling beyond a six-row viewport while retaining its
  complete off-screen prefix.
- Framebuffer guards and every padded word unchanged.

make test-keyboard passed with actual QEMU q35/TCG, OVMF and QMP key injection.
The test entered the existing short line, then a 163-character line consisting
of 80 A, 80 B and CDE. It verified there was no new SUBMIT record while the text
soft-wrapped across three rows, deleted E and D with two Backspaces, appended QR,
pressed Enter and captured one exact 163-byte ordered submission. Accepted key
serial order was checked after every injected make/break sequence.

The test retained exact full-frame checks for the initial logo/short typed screen
and the final fully scrolled screen. make test-workspace and
make test-workspace-scroll also passed: strict freestanding Clang/LLD build,
sanitizer framebuffer/console/logo/workspace tests, literal SPIFFOS_BOOT_OK and
complete pixel comparisons. The logo scrolled out and did not reappear.

Evidence retained under build/: logical-line-qemu.log,
logical-line-regression.log, keyboard-serial.log, keyboard-typed.ppm and
keyboard-scrolled.ppm.

## Limits and rollback

One active append-at-end logical line is implemented. No visible cursor, arrows,
Insert toggle, arbitrary blank-cell typing, editing earlier characters, multiple
line records or push-down row insertion yet. PS/2 remains polling-only; USB,
interrupts, BASIC, ring 3 and the final console ABI remain outside this milestone.

Roll back by building commit 353f160 or merged baseline bbb2d4d in an isolated
worktree. Do not reset or clean the owner's editor checkout. No merge, physical
deployment, USB, disk, firmware, network or infrastructure change is included.
