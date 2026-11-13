# ADR M10-D3.1 — boot-prepared workspaces and read-only diagnostics

Status: **OWNER APPROVED FOR IMPLEMENTATION**, 2026-11-13.
Scope: diagnostics and workspace preparation before M10-D4, **not** enabling production preemption.
Governing references: `AGENTS.md`, `docs/decisions/accepted.md`, `docs/decisions/open.md`, `docs/architecture/system-overview.md`, `docs/decisions/ADR-m7-text-workspace-switching.md`, `docs/decisions/ADR-m8-function-keys-and-workspace-indicator.md`, `docs/decisions/ADR-m10-d-preemption-proposal.md`, `docs/DEVELOPERS_GUIDE.md`.

## Context and decision

The owner accepted the M10-D3 diagnostic QEMU and requested that all nine
workspaces be initialized on boot, followed by debugging tools. The existing
manager initializes workspace 1 on boot and calls `boot_workspace_reset`
and `screen_editor_initialize` on first visit to 2..9. Both functions
draw visible pixels, so calling them for every workspace during boot
would corrupt workspace 1 unless expensive snapshots were allocated.
At 1280x800x32bpp nine full images cost 35.16 MiB; this is unacceptable
as an unconditional 128MiB boot expense.

**Decision:** prepare each workspace's independent editor/console *logical
state* during boot, while deferring framebuffer snapshot allocation and
first visible rendering until first activation. A first-visit editor may
perform only presentation binding/painting, not separate logical allocation.
Preserve workspace-1 logo and its seeded text. Never copy live
`screen_editor` instances because records contain internal pointers.
On failure retain the active workspace and its bytes without false
ready publication. Keep boot fast and single-CPU diagnostic-only.

Expose a **read-only F10 system monitor** using only validated state:
workspace numbers/readiness, display snapshot status, IRQ0 tick count if
available, a bounded static memory-service summary if an existing
read-only API is safe, and the D3 diagnostic/production mode distinction.
Do not fabricate task counts/CPU percentages or promise generalized
priority enforcement. No unsafe physical memory inspection, editing,
allocation, scheduler mutation or CPU-control commands in this phase.
Preserve F1–F9 editing and 1s F12 overlay.

## Validation

Hosted nine independent editors prepared, unchanged framebuffer before
first workspace visit, lazy image allocation, F10 display and return
pixel-identical, snapshot allocation failure rollback, inactive reset,
cursor and editor ownership, full memory regression at 128/256/512MiB,
keyboard F1–F12 and M10-D1/D2/D3 tests.

## M10-D4 follow-on

Only after the D3.1 QA gate, integrate the tested four priority classes
and aging/reserved supervisor service into the **test-only** IRQ-exit
scheduler. Require adversarial high-priority CPU hogs, bounded service
windows, correct stack ownership, negative IRQ/fault tests and unchanged
normal ISO. D5 remains the separate production preemption decision.

## Change history

2026-11-13: owner accepted D3, suggested nine boot-initialized workspaces
and debugging tools; assistant proposed logical preinit/lazy snapshots
and F10 diagnostics; owner approved D3.1 followed by D4.
