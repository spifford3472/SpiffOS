# ADR M8 — Function keys, system screen and workspace indicator

Status: owner-authorized implementation 2026-11-10; initial implementation and validation underway.

## Context
M7 used Ctrl+Alt+1..9, which can conflict with desktop hotkeys. The owner
verified F1..F9 work and requested exclusive F-key selection, F10 system
screen, and F12 one-second large centered workspace indicator.

## Decision
- F1..F9 exclusively select the nine user workspaces; remove Ctrl+Alt+1..9.
  Ordinary number keys remain text. Ctrl+Alt+0 overview is superseded by F10.
- F10 opens a dedicated system screen, not a tenth editable user workspace.
  It should show the active user workspace number and minimal read-only system
  status until system controls are separately specified. F1..F9 return to a
  user workspace without altering its contents.
- F12 briefly displays a large centered number 1..9 for the selected user
  workspace, without changing editor contents or cursor, and automatically
  removes the overlay after approximately one second. While the system screen
  is open, F12 reports the last active user workspace number.
- Implement time-based dismissal with a measured monotonic clock, not loop
  iteration counts, because VM and hardware CPU speeds vary. The current
  kernel polls PS/2 with interrupts disabled and has no timer service; a
  validated clock source must precede the overlay implementation.
- Non-destructive overlay: preserve the affected rectangle and restore it
  before handling any input, workspace switch or screen redraw. The timed
  dismissal also occurs without further keyboard input. The indicator fades at approximately 650 ms and 850 ms, and restores exact
  original pixels after approximately 1 second.

## Rejected alternatives
- Reusing Ctrl+Alt chords: host interception and accidental text input.
- Busy-waiting for one second: would freeze typing and wastes CPU.
- Counting keyboard polling iterations: cannot guarantee elapsed time.
- Drawing text into the editor's backing store: corrupts user data.

## Validation required
Hosted key decoder tests (F1..F12 and release codes), QEMU keyboard tests,
framebuffer byte-for-byte restoration after indicator, one-second elapsed
runtime tolerance, input during overlay, workspace switching during overlay,
F10 read-only screen, and existing editor/keypad/graphics regressions.

## Scope and risks
No physical disk, OPNsense or LAN changes. Do not merge or deploy until all
three requested behaviors are implemented and tested in a real QEMU boot.

## Initial timer implementation
Use read-only latched PIT channel 0 (I/O 0x43/0x40), accumulating unsigned
16-bit down-counter deltas to 1,193,182 ticks. This assumes the firmware's
standard PIT periodic setup and continuous polling more frequently than the
54.9 ms wrap period. No PIT reprogramming, IRQ activation or busy wait.
This is a diagnostic timing mechanism; general timer services and hardware
portability remain separate work. QEMU screenshot capture may exceed the
one-second lifetime, so hosted tests verify immediate draw/restore while
QEMU tests verify automatic dismissal and exact restoration.

## Verification and limitations
F10 displays read-only diagnostic system information and active workspace ID;
its content is intentionally minimal pending a separate system-control design.
F12 uses a 128x128 enlarged original console-font glyph, with two blended
fade stages at 0.65 and 0.85 seconds, then exact saved-pixel restoration.
Any key input dismisses the overlay early to prevent stale pixels or lost
editing; a new F12 restarts the timer. PIT is boot diagnostic only, not a
portable timer abstraction. Physical laptop hardware is not tested.
