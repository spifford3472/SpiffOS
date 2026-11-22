# M10-D5.8c C2a — real IRQ0 donation observation with a genuine BLOCKED waiter

Status: test-only isolated proof. Date: 2026-11-14. Base: PR #85 exact head 76999915118a0e72b4f7684f739db596005407b7, GitHub Actions run #85 SUCCESS. This is NOT full C2b IRQ0 priority task switching during contention.

## Architecture and integration

Governing: AGENTS.md, accepted.md, open.md, system-overview.md, DEVELOPERS_GUIDE.md, ADR-m10-d53-bounded-synchronization.md, ADR-m10-d54-priority-dispatch.md, ADR-m10-d56-irq-handoff.md, ADR-m10-d57-preemption-and-priority.md, ADR-m10-d58b-frame-ownership-gate.md, ADR-m10-d58c-real-irq-lock-owner.md and ADR-m10-d58c-c2-blocked-irq-observation.md.

A separate SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_TEST flag extends only the existing D5.4 three-worker QEMU cooperative fixture (with the D5.8b IF-off blocked switch flag also enabled). The background owner acquires D5.3 rank 0 on its exact M10-A RUNNING stack, temporarily BLOCKS waiting for an external event and physically surrenders its stack under IF=0. High-priority waiter executes on its own genuine CPU stack, becomes BLOCKED on rank 0 through d53_acquire and transfers to bootstrap under IF=0, retaining exact-generation D5.2b waiter reference. Medium-priority worker runs and yields so it is genuinely READY as a CPU competitor. After boot wakes the owner from its external-event wait, real D5.4 selection consumes D5.3 donation to choose the low-priority owner over the medium READY worker.

Before releasing, the owner spins under IF=1 long enough for at least three hardware IRQ0 observations. A dedicated read-only call from the established x86-irq0.S R12 frame pointer (after regular IRQ handler/EOI) verifies live IF=0, exact M10-A owner RUNNING, high BLOCKED, medium RUNNABLE, full D5.3 graph and rank-0 matching waiter generation, D5.8b owner ledger and C1 actual 20-qword frame geometry/stack/segments/flags and D5.3 effective TIME_SENSITIVE donation. Only the private diagnostic counters/fault latch change in IRQ. No M10-A task or lock mutation, second EOI or task handoff occurs inside IRQ.

Owner then releases rank 0 from its own off-IRQ IF=0 safe point, transferring lock ownership to the high waiter and moving it to M10-A RUNNABLE. Boot dispatcher subsequently chooses high and resumes its actual old six-register cooperative continuation on its preserved physical stack, releases lock, and exits. Independent medium and owner continuations exit and the surviving boot dispatcher checks reference counts and reaps all three backing stacks. Normal keyboard/editor remain available.

## Test commands and evidence

- make HOST_CC=gcc test-m10-d58c-c2-observe: PASS 128/256/512 MiB genuine QEMU, each exactly three authenticated hardware IRQ0 observations with the donated owner RUNNING, high BLOCKED, medium READY, no exception or D54 failure; marker SPIFFOS_M10_D58C_C2_IRQ_OBSERVE_OK and normal SPIFFOS_KEYBOARD_READY. Log /tmp/spiffos-d58c-c2-observe-6.log.
- make HOST_CC=gcc test-m10-d58c-c2-acceptance: new highest acceptance target; includes C2a and all older D2-D5.8c tests. Record final status from /tmp/spiffos-d58c-c2-full-exit.txt before claiming pass.
- Separate exact-head GitHub CI and normal debug-stripped ELF byte identity must pass before treating C2a as independently accepted.

## QWEN provenance and correction record

Local QWEN qwen3-coder:30b was used first on an isolated exact-state/IF predicate (26.71 seconds), which returned a valid minimal five-input conjunction but omitted full real IRQ frame, generation/ledger and graph proofs. Coordinator reviewed and expanded into the actual hardware-backed IRQ observer and QEMU fixture. No Codex used.

Initial new fixture build failed strict Clang -Werror due misplaced preprocessor directive and unused saved flags; corrected without modifying normal builds. First guest reached D54 failure TASK_HANDOFF. Diagnostic precision identified the worker's donation observer failure; the worker was using the optional D54 clock callback, which may return unavailable in boot, rather than the independent actual IRQ count. Removed that clock prerequisite, replaced with a bounded CPU spin plus QEMU harness timeout; the real hardware IRQ frame count became the sole completion witness. Re-ran QEMU successfully at all sizes. This does not relax state, frame or provenance checks. On a failure after true lock ownership or waiter registration, a C2a-only failure path does not free referenced stacks or force lock cleanup; it halts with ownership intact.

## Safety, exclusions, rollback

C2a is not C2b. D5.4 selects the donated owner cooperatively, then IRQ0 only observes genuine donation while the owner runs; no IRQ0 scheduler switches during contention, no medium task CPU preemption while owner runs, and no new unified RET/IRETQ context ABI. Before claiming complete real IRQ mutex preemption and wake, C2b must build separately reviewable and independently validated context transitions plus actual M10-A IRQ switching during contention and fault-injection failure containment. Do not fabricate IRETQ frames or force-release locks. Discard isolated branch/worktree to roll back; owner D4 ISO, normal EDIT/BASIC and physical hardware not touched.

Change history: 2026-11-14 C2a implementation, review, early failed test corrections and passing positive QEMU checkpoint.
