# M10-D4 — Test-only four-band IRQ scheduling and supervisor service

Status: **owner-authorized implementation in progress**, 2026-11-13;
not approved for general production preemption or physical deployment.

Governing references: `AGENTS.md`, `docs/decisions/accepted.md`,
`docs/decisions/open.md`, `docs/architecture/system-overview.md`,
`docs/decisions/ADR-m10-task-scheduler-foundation.md`,
`docs/decisions/ADR-m10-d-preemption-proposal.md`,
`docs/decisions/ADR-m10-d31-workspaces-debug-hub.md`,
`docs/operations/m10-d3-irq-handoff-20261113.md`,
and `docs/DEVELOPERS_GUIDE.md`.

## Decision and safety

Integrate the existing, independently hosted/sanitizer-tested
`kernel_priority_choose` pure priority selector only in a new
`SPIFFOS_M10_D4_PRIORITY_TEST` diagnostic build. Keep default IRQ0
callback and normal keyboard editor unchanged. Three test contexts:
boot supervisor with permanently surviving stack (reserved slot 15),
one Background worker (slot 0), and one Time-sensitive CPU-bound
worker (slot 1). Both workers never voluntarily yield. Pure selection
runs only after the bounded IRQ0 accounting and exactly one LAPIC EOI,
with IF cleared; no allocation/free, logging, rendering or locks
in IRQ context.

Every fourth dispatch while boot is runnable is reserved for supervisor
service; otherwise choose highest saturating `age+priority*8` score.
A dispatched task resets waiting age; others accumulate, saturated.
The boot supervisor is an actual registered runnable context, not an
imagined extra CPU. Only the boot context may clean up worker stacks
after all workers are no longer active. Task block/wakeup needs a
separate state transition proof; do not claim real blocking from
reassigning a static synthetic snapshot.

## Test gates

- QEMU 128/256/512MiB: background and time-sensitive workers both
  gain sustained IRQ-driven CPU service, boot supervisor runs at least
  each fourth dispatch, no invalid frame, timer monotonic, stack
  canaries and return-to-keyboard pass.
- Hosted D1 policy test proves starvation protection with 16 ready
  candidates and supervisor reservation; D4 guest measures its own
  time-slices and rejects missing service.
- Negative IRQ-disabled, corrupted worker guard and readiness/
  dispatch validation fail closed. Preserve D3/D2/M10-A–C/M9
  existing regression suite and normal default ISO isolation.
- Do not merge/deploy D4 diagnostic or enable normal-editor
  preemption until a separate full end-to-end review.
