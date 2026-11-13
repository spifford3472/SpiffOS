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


## First integrated test evidence and discovered starvation edge

D4 hosted all-four-band test at 20,000 deterministic dispatch decisions
with alternating blocked/woken readiness passed using GCC's address
and undefined-behavior sanitizers, marker
`SPIFFOS_M10_D4_HOST_PRIORITY_CHURN_OK`. The live IRQ test-only
dispatcher preserves D3's owned-stack and trusted IRQ-exit frame design,
maps boot supervisor to slot 15, worker A to Time-sensitive (slot 0)
and worker B to Background (slot 1). It proves actual supervisor
reservation, two non-yielding worker slices, maximum background
ready-wait, and distinct blocked/woken transitions in QEMU. Normal
kernel code remains excluded by compile flag; D3 unchanged when
its own diagnostic flag is used.

The first live readiness stress failed its completion gate with
`PREEMPTION_PROGRESS`: Background was repeatedly blocked for
8 of each 32 dispatches and reset its aging score to zero on each
block. A worker runnable only for a short recurring interval could
never accumulate enough credit to overcome the 24-point
base-priority gap, despite eventually waking. The coordinator
corrected the D4 *test scheduler* to **preserve already-earned
ready-time aging credit across short blocked intervals while
pausing the age clock when blocked**. This permits eventual service
without awarding credits for time spent blocked. No change was
made to D1's immutable pure selection interface. The hosted
readiness-churn model was updated to enforce the same policy.
This is an explicit fairness contract for future scheduler work.

Local QWEN performed a narrow no-code architecture review. It
proposed modulo age counters to 'avoid overflow'; rejected because
wraparound defeats monotonic fairness. Existing saturated 64-bit
age counters are retained. No Codex escalation was necessary.

## Remaining D4 requirements

This is a **test-only D4 checkpoint**, not completion of generalized
preemptive task scheduling. QEMU proves Background, Time-sensitive
and supervisor Normal under real timer IRQ; Interactive is covered
in the deterministic hosted policy tests. Full four-class live
execution, an actual kernel task block/wakeup API, CPU budget/latency
quantification, supervisor recovery under faults, priority inversion
controls and editor isolation under generalized preemption remain
OPEN. Do not enable D4 scheduling in the normal ISO yet.


## Full current D4 checkpoint acceptance — 2026-11-13

At exact code commit `c425fa6462d351bd008fcae850af621dc64d757a`,
on isolated Forge review checkout:

```sh
make HOST_CC=gcc test-m10-d4-acceptance
```

**PASS**, runtime approximately 214.55 seconds, log
`/tmp/spiffos-d4-full-regressions.log`, final markers
`SPIFFOS_M10_D2_ACCEPTANCE_OK`,
`SPIFFOS_M10_D3_ACCEPTANCE_OK`, and
`SPIFFOS_M10_D4_CHECKPOINT_OK`.

This includes D4 20,000-decision all-four-band hosted sanitizer tests,
three 128/256/512MiB real-IRQ priority/supervisor positives,
three IRQ-disabled negatives, three stack-canary corruption negatives,
D3 non-yielding handoff positives/negatives, D2 complete IRQ trap-frame
positives/negatives, D1 age policy, M10-A/B/C, M9 memory/framebuffer,
F1–F9/F10/F12 keyboard/editor and timed IRQ0, #UD/#DF, APIC/MMIO
positive/negative regression tests.

After that exact code commit, GitHub comparison confirmed **only**
`docs/DEVELOPERS_GUIDE.md` and this ADR changed on the branch
(no C/ASM/Makefile/test changes). A separate ordinary unflagged ISO
was built from both D3.1 and D4 using the same pinned Limine source.
Their debug-stripped kernel executable SHA-256 was identically
`82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`.
Thus the D4 experiment is excluded from the regular kernel runtime,
while the D3.1 F10/boot preparation changes intentionally remain.
No merge, physical media or owner ISO deployment has occurred.
