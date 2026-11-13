# ADR — Boot-time prewarming of nine diagnostic workspaces

Status: **PROPOSED IMPLEMENTATION FOR REVIEW** — 2026-11-13.
Authority: owner requested eliminating the one-time F-key workspace activation
delay by moving initialization to boot. This is a separate change from the
approved M10-D work, and does not authorize merge or deployment.

References: AGENTS.md, docs/decisions/accepted.md (A-009/A-010/A-011),
docs/decisions/open.md, docs/architecture/system-overview.md,
docs/decisions/ADR-m7-text-workspace-switching.md,
docs/DEVELOPERS_GUIDE.md.

## Current behavior and cause

Workspace 1's editor and framebuffer are ready before keyboard entry.
The other eight workspaces are **lazy**: their first F2–F9 activation
draws the boot logo, initializes the console and screen editor, and
allocates page-backed 1280x800 snapshots. The current workspace must
also capture its framebuffer the first time it is left. These first-use
operations explain the visible delay; subsequent switches reuse snapshots.

## Decision and constraints

Introduce `workspace_manager_prewarm()` called once from the boot
keyboard-initialization routine, **before** `SPIFFOS_KEYBOARD_READY`.
Capture the exact current Workspace 1 pixels, initialize each of
Workspaces 2–9 on the existing framebuffer and capture their initial
pixels to individually owned snapshot pages, then restore Workspace 1
exactly and leave it active. All workspace editors and snapshots are now
ready by the time keyboard interaction is enabled.

This intentionally trades startup work and ordinary RAM for avoiding
one-time editor/page initialization during the first F-key switch.
At 1280x800 32-bit pixels, each full screen is 4,096,000 bytes,
and nine snapshots together occupy 36,864,000 bytes
(approximately 35.16 MiB), plus metadata. There is no extra 35 MiB
*per* snapshot. Existing page ownership and limit checks remain
unchanged, and this does **not** add tasks or a preemptive editor.

Prewarm is an optimization, not a required boot feature. If allocation
or initialization fails, restore the original Workspace 1 pixels and
retain existing lazy initialization for unprepared workspaces. If
framebuffer mode is unsupported, retain the historical main
workspace. Do not claim instant switches: capture/restore of millions
of framebuffer pixels still costs time even after all backing is warm.

## Alternatives and consequences

- Continue lazy initialization: minimal boot time/memory until use,
  but the owner observes first-use delays. Retained as fallback.
- Preallocate pages only: moves the allocation but leaves the first
  boot-logo/editor paint delay. Rejected for desired UX.
- Construct each initial snapshot in a private software framebuffer:
  avoids visible rendering during preload but requires additional
  framebuffer abstraction and temporary backing; deferred in favor
  of the smaller reviewable boot-only change.
- Store a single shared immutable initial screen and copy on first
  modification: potentially reduces resident memory, but requires
  copy-on-write/dirty-region semantics and more invasive ownership
  design. Consider later if memory usage or boot speed is unacceptable.
- Reinitialize all editors upon every switch: unacceptable as it
  destroys per-workspace input and increases latency.

## Tests and release gates

- Hosted workspace-manager test: exact pixels of Workspace 1 unchanged
  after preload; all nine ready/snapshots; switching 1 -> 9 -> 1
  returns identical pixels; forced allocation failure retains legacy
  lazy switch behavior.
- Real QEMU 128/256/512 MiB: `SPIFFOS_WORKSPACES_PREWARMED` marker
  precedes `SPIFFOS_KEYBOARD_READY`, all boot markers present.
- Existing `make HOST_CC=gcc test-memory test-keyboard`: QEMU
  memory ownership and framebuffer-pixel tests; F1–F9, F10, F12,
  editor logical lines, reflow, scrolling, special keys; must all pass.
- Owner interactive acceptance should compare first-switch perceived
  latency and boot responsiveness on Forge; measure if it remains
  noticeable before any merge/deployment.

## Actual executed review branch evidence

Code branch `feat/workspace-boot-prewarm-20261113`, isolated Forge
checkout `/tmp/spiffos-m10c-review-fOoa6o`; no changes to
`/home/geoff/projects/spiffos-qemu-main` or owner ISO.
- Hosted `make HOST_CC=gcc test-workspace-manager-memory`: PASS.
- Built normal ISO and QEMU tested at 128, 256, 512 MiB: all
  emitted `SPIFFOS_WORKSPACES_PREWARMED` then
  `SPIFFOS_KEYBOARD_READY`, with boot completed.
- `make HOST_CC=gcc test-memory test-keyboard`: PASS, all memory
  and framebuffer 3-size checks, all nine function-key workspaces,
  F10/F12 (F12 1.009 s), exact screenshots, 406-byte editor
  test and text/keyboard regression checks.
- Local QWEN `qwen3-coder:30b` performed a narrow design-risk
  review. It identified partial allocation/restore as worth
  testing. Its per-workspace 35MiB estimate and assumed concurrency
  during prewarm were rejected: allocation is ~35MiB **total**, and
  boot workspace initialization is single-CPU with no scheduler
  switching. The coordinator implemented and validated the change.

## Rollback / deployment

No installation occurred. Code rollback is simply dropping the
separate branch/PR; the accepted M10-A/B/C owner checkout/ISO remains
unchanged. An eventual approved release must preserve the original
owner ISO, verify exact source SHA, run the regression suite, deploy
only to Forge's QEMU ISO, and obtain interactive owner acceptance.
No physical disk, OPNsense or LAN changes are in scope.
