# ADR — QEMU boot keyboard input
Status: ACCEPTED for this diagnostic by owner request, 2026-10-29.
Read with AGENTS.md, accepted/open registers, ADR-011, system overview and ADR-003.

## Context and authorization
The owner asked ChatGPT to architect/build the proposed keyboard steps, merge
and deploy the result, then review it together. Direct ChatGPT work continues
without Qwen or a delegated coding model. This scoped approval supersedes older
no-merge/no-deployment statements for this milestone only. Deployment means a
locally built QEMU ISO on Forge; no USB, physical disk or infrastructure changes.
Keep the separately staged start.S and unrelated drafts out of all commits.

## Decision and alternatives
Use QEMU q35's i8042/PS2 keyboard with bounded polling, interrupts disabled.
Interrupt-driven input would require an IDT and interrupt-controller milestone;
USB HID requires enumeration and host-controller drivers. Neither is hidden in
this teaching step. Polling consumes host CPU; this is temporary, not the final
input/scheduling architecture. Missing hardware fails closed to serial diagnostics.

Split transport (controller configuration, byte/status reads), pure scan-code
decoder, and bounded console line editing. The transport takes injected I/O
callbacks so host tests never execute privileged IN/OUT. Initialization disables
mouse and controller IRQs, explicitly selects keyboard set 2 then controller
translation to set 1, checking ACKs with bounded retries. Never write the
controller output port (reset/A20). Drop mouse/error bytes and reset partial
decoder state. E0/E1 sequences and modifier keys must not leak as text.
US layout subset: uppercase A-Z, digits, space, period, colon, question mark,
Enter and Backspace. Shift selects colon/question mark; unsupported chords
(including Ctrl/Alt shortcuts) are ignored. No claim of full ASCII/localization.

Use caller-owned fixed state, no heap. Each editable line fits one display row,
up to 63 characters and at most columns-1, so Backspace can erase only its
current unsubmitted line. Enter advances to the next row and resets this length;
it does not execute BASIC. The line limit ignores further printable input until
Backspace/Enter. Serial logs accepted input as hexadecimal byte diagnostics.
The logo remains ordinary initial pixels and scrolls out with Enter/output;
no input path redraws it. No visible/blinking cursor or history in this step.

## Validation
Host sanitizer tests cover make/break, repeats, Shift releases, Ctrl/Alt,
E0/E1, ACK/error bytes, transport timeout/ACK/retry/status filters, line bounds,
Backspace at start and line boundary, Enter and guarded framebuffer memory.
Real QEMU QMP key events must prove letters, correction, Enter and enough newlines
to remove the logo. Check serial bytes and exact full-frame screenshots against
independent expected strings/font data. Boot marker still required. Run previous
workspace/logo/scroll tests and strict freestanding builds. Test absent controller
via injected host I/O; physical keyboard, USB, interrupts and ring 3 untested.

## Integration and rollback
First save screen checkpoint ba40dd8. Commit keyboard scope separately, publish a
reviewable PR including the boot chain, inspect final diff/checks, merge only the
tested head, and fast-forward the local main in an isolated worktree. Build and
test the merged source before calling local QEMU deployment complete. Keep the
owner checkout/staging intact. Roll back locally by building ba40dd8 in another
worktree; no reset/clean or history rewrite.
