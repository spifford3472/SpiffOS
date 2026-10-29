# Screen-wide cursor, editing and push-down reflow — 2026-10-29

## Authorization and decision

The owner continued the accepted screen-editor checkpoint and specified the
cursor shapes: a full-cell block in overwrite mode and a two-pixel-thick
underscore at the bottom of the cell in insert mode. Default overwrite, Insert
toggle and push-down reflow were already accepted. The decision registers and
ADR were updated and committed before source changes as 9e88478.

This is direct ChatGPT-guided work under the scoped safe-launcher/screen-editing
exception. Work is isolated in /home/geoff/projects/spiffos-screen-editor-next
on feat/screen-editor-cursor-reflow. The original /home/geoff/projects/spiffos
index, staged start.S, unrelated drafts and existing emulator were not changed.

## Implementation

The translated set-1 decoder now emits named Left, Right, Up, Down and Insert
make events. E0 break events do not move or toggle; Ctrl/Alt chords remain
filtered. The editor consumes abstract actions rather than magic ASCII bytes.

logical-line supports atomic overwrite, insert, space padding and delete-before
at counted offsets. Capacity failure preserves the old bytes and length.
screen-editor owns up to 65 static logical-line records of 4096 bytes each and
supports at most 160 columns by 64 visible rows. The state lives in kernel BSS,
not Limine's entry stack. Screen pixels are never the text authority.

A line owns its visible physical rows. Growing through a row boundary copies
following framebuffer rows downward from bottom to top and shifts their line
identities together. Shrinking pulls following rows upward. Rows pushed fully
out of view are reclaimed. Whole-viewport bottom scrolling moves the initial
logo pixels and line coordinates together; no edit path redraws the logo.

The two existing boot text lines are registered as already-rendered logical
records without changing pixels. Submitted and newly created lines remain
separate records. Enter submits the complete counted selected line once and
inserts a new blank row after it. Typing in an unmanaged blank row creates a
record and pads intervening cells with spaces.

Cursor drawing uses reversible framebuffer inversion. Hiding and restoring the
same rectangle preserves underlying text or logo pixels exactly. Overwrite
inverts all 16x16 pixels. Insert inverts only the bottom two pixel rows. A full
buffer adds a distinct top band/notch while retaining the active mode shape.
The cursor is steady; blinking/timers remain outside this polling diagnostic.

## Actual validation

make test-keyboard-memory passed under AddressSanitizer and
UndefinedBehaviorSanitizer:

- E0 arrow/Insert make events, ignored breaks and Ctrl chord filtering.
- Atomic overwrite, insert, padding, deletion and full-capacity rejection.
- Exact overwrite block and two-pixel insert underscore pixels.
- Earlier-line insertion across a row boundary pushing a later line down intact.
- Backspace shrinking that line and pulling the later line back up.
- Editing the retained later record, blank-cell padding, and submitting seeded
  older output as one complete counted line.
- Full 4096-byte scrolling line, capacity rejection without mutation, complete
  retained-prefix submission, and Backspace clamped at its top-left fragment.
- Exact-width Enter without an extra blank row and exact pixel restoration
  when the cursor visits earlier screen content.
- Framebuffer row padding and surrounding guard words unchanged.

make test-keyboard passed with QEMU q35/TCG, OVMF and real QMP key injection.
It verified 100 named events, make/break behavior, earlier insert/overwrite
editing, exact SUBMIT:SPIFFOS 123.XY?, the prior 163-byte three-row submission,
scrolling and 356 accepted text bytes. keyboard-typed.ppm,
keyboard-insert.ppm, keyboard-scrolled.ppm, keyboard-reflow.ppm and
keyboard-shrink.ppm each matched every expected 1280x800 pixel. The reflow
captures prove that growing/shrinking an earlier line moves a following BBBB
line intact; editing and submitting that moved record produced BCBB.
Representative block, underscore and reflow captures were visually inspected.
The combined validation log is build/checkpoint-validation.log; launcher
results are retained in build/checkpoint-launcher.log.

make test-workspace passed the normal full-frame boot check with the visible
overwrite cursor. make test-workspace-scroll passed the isolated forced-scroll
variant with no input cursor, preserving its historical exact frame.
python3 scripts/test-safe-launcher.py passed real guest boot, absolute pointer
selection, injected editor tests, competing-run lock rejection, QMP close and
SIGTERM child/private-resource cleanup. Manual GTK resize/focus/mouse movement,
Ctrl+Alt+G and close-button observation remain pending as before.

## Limits and rollback

This remains a boot-kernel diagnostic: PS/2 polling, uppercase subset, no BASIC,
ring 3, interrupts, asynchronous output integration, general scrollback or final
console ABI. The fixed row/line capacities are implementation bounds, not BASIC
language limits. A selected long line retains its stored prefix while visible
fragments scroll.

The owner explicitly authorized merge and deployment after review. PR #16
tested head df5f966f3d0be182ad4d166b61441611461f18a8 merged as
e63b04222d04060753949b849f1dfd2e3001f60e on 2026-10-29.
The clean /home/geoff/projects/spiffos-qemu-main checkout fast-forwarded to
that merge and rebuilt the local QEMU ISO at build/spiffos.iso.

Post-merge make test-keyboard test-workspace test-workspace-scroll and
python3 scripts/test-safe-launcher.py both completed with exit 0 from the
merged source. This included host sanitizers, real QEMU key injection, all five
full-frame cursor/reflow captures, normal and forced-scroll boot regression,
absolute pointer selection, lock rejection and child/private-resource cleanup.
Logs: build/pr16-postmerge-validation.log and build/pr16-postmerge-launcher.log.

Deployed ISO SHA-256:
59c9ecf7d7fd0a777a7e604c3f5b171bcb92bdf5c58361977017abae907196f3
Kernel ELF SHA-256:
ce8cf71da88ce8d21a89acd94c82e27edbe7108d6ae93ad422ef09c7a0a57704

Deployment is the rebuilt local QEMU development image. Manual GTK observations
remain pending as documented above. Original staged start.S and unrelated
drafts were rechecked and remain preserved. No USB, physical disk, laptop,
firmware, network or infrastructure changes were made.
Roll back by building 8d489b8 in an isolated worktree; do not reset or clean
the owner checkout.
