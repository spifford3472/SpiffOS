# M10-D3.1 — Boot-prepared workspaces and read-only diagnostics

Status: **OWNER APPROVED IMPLEMENTATION, 2026-11-13.** D3 test-only preemption remains the deployed baseline; D4 implementation follows this checkpoint after its tests pass. No merge or installation approval implied beyond the user-authorized development sequence.

References: `AGENTS.md`, `docs/decisions/accepted.md`, `docs/decisions/open.md`, `docs/architecture/system-overview.md`, `docs/decisions/ADR-m7-text-workspace-switching.md`, `docs/decisions/ADR-m8-function-keys-and-workspace-indicator.md`, `docs/decisions/ADR-m10-task-scheduler-foundation.md`, `docs/decisions/ADR-m10-d-preemption-proposal.md`, `docs/DEVELOPERS_GUIDE.md`.

## Decision and reasons

Initialize the **logical identities and per-workspace state** for all nine workspaces at boot. Do **not** allocate eight full inactive framebuffer snapshots, paint hidden workspaces on the physical framebuffer, or mark an editor usable until its own editor initialization succeeds. Preserve the existing workspace-1 image and exact F12 overlay. Treat 'prepared' and 'active/initialized' as different states. Full inactive editors are expensive and their existing initialize function paints the real framebuffer, so do not call it from boot for hidden workspaces without a separate nonpainting API.

The F10 screen evolves into a **read-only bounded kernel monitor**, surfacing actual verified values only: workspace identity/preparation/ready/snapshot counters and IRQ0 tick or clock observations (and possibly bounded service information via safe read-only APIs). **Never invent active tasks, CPU percentages, hardware frequencies or free RAM numbers.** D3's diagnostic tasks have already terminated by keyboard entry; D1 policy is not linked into normal release. Display should explicitly distinguish 'not available' measurements from zero.

## Tests and failure policy

- Hosted workspace tests: all identities prepared at boot without allocating snapshots; exact framebuffer checksum unchanged; first switch initializes the destination without corruption; invalid IDs/denied snapshots preserve current state; existing reset isolation preserved.
- Hosted diagnostics: truthful source values and bounded text rendering; F10 must preserve exact editor framebuffer and return via F1–F9; invalid/missing timer evidence must render 'unavailable', not fake uptime.
- QEMU 128/256/512 MiB: keyboard/editor, F1–F12, framebuffer, memory and IRQ0 tests; measure any boot-time regression; full M10-D3 acceptance gate. Do not change the protected owner ISO without a release decision.

## D4 boundary

Next, integrate the four-priority D1 policy into the **test-only D3 IRQ-return dispatcher** and demonstrate supervisor servicing under CPU hogs, starvation bounds under churn, blocked/woken tasks, interrupted RFLAGS/IF correctness, bounded IRQ service and safe editor isolation. D4 does not implicitly authorize general production preemption.
