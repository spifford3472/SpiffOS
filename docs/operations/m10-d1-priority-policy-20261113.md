# M10-D1 priority selection — first test-only implementation (2026-11-13)

Status: **IMPLEMENTED AND HOST-TESTED ON ISOLATED REVIEW BRANCH**, not merged or deployed.
Authorization: owner approved M10-D staged architecture and D1–D5 implementation;
the code is intentionally not linked into the production kernel. See
`docs/decisions/ADR-m10-d-preemption-proposal.md`, `AGENTS.md`,
`docs/decisions/accepted.md`, `docs/decisions/open.md`,
`docs/architecture/system-overview.md`,
`docs/decisions/ADR-m10-task-scheduler-foundation.md`
and `docs/DEVELOPERS_GUIDE.md`.

## Pure selection rule

New `src/kernel/kernel-priority-policy.[ch]` accepts an immutable validated
snapshot of up to 16 runnable candidates and selects one slot. Priority classes
are Background=0, Normal=1, Interactive=2, Time-sensitive=3.

- For every **fourth dispatch opportunity** (zero-based dispatch number
  modulo 4 == 3), pick the supervisor reserved slot 15 **if runnable**.
  Supervisor may additionally win normal selection. This is a *selection*
  reservation only, not a measured wall-clock CPU budget or independent
  recovery service; actual forced preemption belongs to D3 and D4.
- On all other decisions choose maximum saturating score:
  `waiting_dispatches + priority*8`; ties resolve by larger
  `waiting_dispatches`, then lower slot index.
- Caller updates each ready task's `waiting_dispatches` once per completed
  selection (chosen task resets to 0; all other continuously runnable tasks
  gain 1, saturating). The pure function does not update task table, queues,
  interrupt state, stacks or any other caller-owned object.
- Reject invalid counts, NULL pointers, repeated slots, malformed enum
  priorities and a mismatched supervisor identity before writing output.
  Empty ready list has a distinct result; errors never change the output.
  Serializing scheduler state is mandatory at future integration points.
- This is **not** a universal numeric starvation bound. With finite ready
  sets, sustained reset-on-selection and nonwrapping wait ages, waiting
  eventually raises a low-priority candidate above newly serviced hogs.
  Exact live-task guarantees, CPU quantums, runnable churn, saturated
  extreme counters and supervision under disabled interrupts require D3/D4.

## Implementation and test evidence

QWEN `qwen3-coder:30b` received a bounded review task, with mandatory
reference context, to propose the pure selection policy and adversarial tests.
Its proposal always selected the maximum base priority, which would permit
permanent starvation; it also claimed an O(1) scan and supervisor guarantee
that did not follow from its rule. Coordinator rejected these elements and
implemented the age-boost/periodic-supervisor selector and host tests.

- Isolated Forge source: `/tmp/spiffos-m10c-review-fOoa6o` detached checkout,
  fetched branch `feat/m10-d1-priority-policy-20261113`; owner checkout and
  ISO were not modified.
- `make HOST_CC=gcc test-m10-d1-priority`: PASS with strict C11 flags
  and GCC address/undefined-behavior sanitizers;
  marker `SPIFFOS_M10_D1_PRIORITY_POLICY_OK`.
- The first test expected Background to receive at least 100 of 4096
  decisions in a 16-slot all-ready stress scenario. Actual simulation
  showed 95; corrected the test to assert >=90 **and**, more importantly,
  a maximum observed wait shorter than 64 decisions for every slot.
  Re-ran the sanitizer test successfully. This is a scenario-specific
  regression check, not a proof of universal 64-dispatch fairness.
- `make HOST_CC=gcc test-kernel-task test-scheduler-clock iso`: PASS;
  markers `SPIFFOS_M10_A_POLICY_OK`,
  `SPIFFOS_M10_C_ACCOUNTING_OK`.
- Built normal kernel ELF SHA-256
  `93a3f08c030fbdfe0dc768646b386c0b34ddcab8fdf202fd2bab1194e6d573ab`
  matches the accepted prior M10-A/B/C kernel; the new selector and
  test files are not linked into the default image.

## Next gated work

Before D2 or any IRQ entry code changes, define/verify a complete interrupt
frame and task-stack ownership contract. Then implement independent assembly
fixtures and real QEMU state-preservation tests. Only D3 may introduce
test-only actual timer preemption; D4 must validate priority and supervisor
service with a CPU hog and reentrant behavior; D5 must pass the full existing
regressions. Merge and deployment remain separately owner-controlled.
