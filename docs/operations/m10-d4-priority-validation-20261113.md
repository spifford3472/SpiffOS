# M10-D4 first real-priority IRQ integration — test ledger

Status: **first D4 gate implemented, isolated and full-regression-tested;
D4 overall remains OPEN.** No merge, release ISO installation, physical
media or network changes have been authorized for these new branches.
M10-D3 remains the accepted deployed Forge baseline.

## Implementation

At compile-time `SPIFFOS_M10_D3_PREEMPT_TEST` plus
`SPIFFOS_M10_D4_PRIORITY_TEST`, the D3 real PIT0 IRQ-exit selector
reuses the pure D1 hosted priority chooser to select a validated saved
20-word IRETQ frame. The permanent boot supervisor is policy slot
15, while workers A/B use slots 0/1. Every fourth dispatch is reserved
for the supervisor if runnable. All three tasks remain ready, aging
increases one per unselected dispatch (saturating), and the single
boot CPU runs with IRQ IF=0 in the bounded callback, after the existing
C tick handler and LAPIC EOI. **No page allocation, logging, locks,
stack free or cooperative switch happens from IRQ context.**

Two separately-owned 16 KiB non-yielding CPU hogs are real IRQ-
preempted through four successive 80-tick priority epochs:
Background, Normal, Interactive and Time-sensitive on worker A,
competing with permanently Time-sensitive worker B. The test
requires every priority epoch to give worker A CPU opportunities,
max wait age <64, >=80 reserved supervisor selections across the
320+ tick run, both worker progress counters, valid stacks and
canaries, clean boot-stack return and backing release. The existing
cross-context counter was corrected to count actual change of
task owner only (self-resumes are not transfers). This is a
**time-phased four-class test**, not simultaneously active four-class
scheduler proof.

## Evidence

QEMU tests were performed on isolated Forge checkout, never the
owner source/ISO:

- First `make test-m10-d4-guest`: PASS positive 128/256/512MiB,
  malformed-priority rejection 128/256/512MiB, and IRQ0-unavailable
  fail-closed 128/256/512MiB (nine QEMU guests).
- First `make HOST_CC=gcc test-m10-d4-acceptance`: PASS,
  `SPIFFOS_M10_D4_ACCEPTANCE_OK`, ~202.81s.
- Final **exact tested SHA**
  `b011edb2c842a60c12e673cc757b20bb05186c6e`:
  `make -B HOST_CC=gcc iso` PASS with diagnostic/priority symbols
  absent from normal default ELF, followed by complete
  `make HOST_CC=gcc test-m10-d4-acceptance` PASS
  in ~195.76s, final markers
  `SPIFFOS_M10_D2_ACCEPTANCE_OK`,
  `SPIFFOS_M10_D3_ACCEPTANCE_OK`,
  `SPIFFOS_M10_D4_ACCEPTANCE_OK` and
  `FINAL_D4_FULL_SUITE_PASS`.
  Full log `/tmp/spiffos-m10-d4-final-acceptance.log`.
  Includes D1 sanitizer/aging, D2 full frame and corruption
  detection, D3 real preemption with corrupted canary negatives,
  nine prepared workspaces and F10 diagnostics, keyboard/F1–F12,
  M10-A/B/C, memory/framebuffer at 128/256/512MiB,
  hardware IRQ0 timing/IRQ-disabled negative, #UD/#DF,
  APIC cache + negative, MMIO UC/NX + rollback negative.
  D4 test-only code is not linked into the normal ISO.
- Production owner checkout SHA
  `28bbcca650957a0a031611f8e0a16dfe6958d0b3`, installed
  normal ISO SHA-256
  `4fcda82804c72fb4697175b811865755deb3caa4605797bce7a1d7c521eda596`,
  unchanged after isolated tests.

## Peer review and limitations

Local QWEN `qwen3-coder:30b` reviewed the narrowly scoped IRQ policy
integration without editing files. It flagged the explicit
`(enum kernel_priority_class)4` injection, which exists *only*
under `SPIFFOS_M10_D4_BAD_PRIORITY_TEST` to prove invalid values are
rejected; this is not production C arithmetic. It also questioned
phase index bounds: `d3_ticks==0` is rejected before subtraction,
and all phases are clamped to the final valid index. Supervisor
slot 15 is explicitly required by the existing D1 selector and is
not a conflict. No Codex escalation was required.

## Open D4 gates

**Do not close D4 yet.** Still needed:
1. All four classes running simultaneously on independently owned
   stacks, rather than one worker changing priority between epochs.
2. Blocked/wakeup transitions with correct owner state and no
   lost runnability or unfair dispatch.
3. Supervisor reserved CPU service under heavier resource pressure,
   with measured worst-case latency/quantum bounds and recovery
   testing, including malformed frame/stack failures.
4. Exact postmerge checksum and guarded owner installation, if
   separately approved, followed by manual interactive QEMU
   acceptance. D5 generalized editor/process preemption is not
   approved or implemented here.

## Rollback

This is an isolated branch and test fixtures; rollback means
discarding the temporary checkout/tests. Keep installed M10-D3 ISO
and its preserved M10-A/B/C and M9 backups intact. No owner or
infrastructure permissions were weakened.
