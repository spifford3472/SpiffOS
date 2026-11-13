# ADR M10 — Kernel task management, cooperative switching and timer accounting

Status: OWNER APPROVED DIRECTION, 2026-11-12; implementation unverified.
Scope: M10-A, M10-B and M10-C only. M10-D requires a separate explicit acceptance.
Baseline: main 6b4dffbaab20d6717ccb8ea2d85d84e7c0621a62, M9-3D6.

## Context and authority
The owner approved the 2026-11-12 recommendation for a minimal, single-CPU
task subsystem. The accepted register A-006 requires timer-driven preemption
and four named priority classes eventually; those system requirements are NOT
cancelled. The owner explicitly separated M10-D, which will establish whether
and when that behavior can safely be introduced. This ADR authorizes work on
A through C but NOT merging, deployment, physical boot media or enabling timer
preemption. Read AGENTS.md, both decision registers, system overview,
Developer's Guide and applicable M9 memory/interrupt ADRs before editing.

## Definitions
Task: one kernel-owned, schedulable execution context with independent stack
and lifecycle. Kernel thread: a future optional multiple-context arrangement;
not a separate M10 type. User process: future protected ring-3 address space,
resources and one or more tasks; no user process or ABI exists in M10.
Workspace: existing virtual text/editor state, not a task or process.
Supervisor: privileged future reserved-resource execution capability.
No kernel-mode tasks are memory-isolated from one another.

## M10-A model — hosted, hardware-independent
Use a bounded, caller-owned task table and deterministic FIFO ready queue.
A task ID is an opaque slot plus generation, never an unqualified reusable
slot number. Reject stale IDs and generation wrap; do not silently recycle
identity. No hidden allocations or public ABI.
States: CREATED -> RUNNABLE -> RUNNING -> RUNNABLE (yield);
RUNNING -> BLOCKED -> RUNNABLE (wake); CREATED/RUNNABLE/BLOCKED/
RUNNING -> TERMINATED by defined owner-controlled shutdown paths. Reject
illegal transitions, non-current yield, double terminate, and waking
non-blocked tasks without mutation. Only the dispatcher selects RUNNING;
the running task may relinquish or block. TERMINATED is terminal.
Queue operations must preserve membership exactly once. The empty-ready
case selects a permanent idle/dispatcher context, never a freed task.
Initially one FIFO round-robin queue at Normal priority; other priority
labels may exist as metadata but must not affect scheduling until separately
tested and documented. Never equate priority with privilege.

## M10-B cooperative switching — independent gate
Start with explicit kernel task yield; no switches inside an IRQ handler.
Context save/restore uses a dedicated x86-64 assembly routine, not the M9
IRQ0 stub. A call-based cooperative switch must preserve SysV callee-saved
RBX, RBP, R12–R15 and resumed RSP/RIP plus correct caller-visible RFLAGS,
including IF. The assembler/C boundary must satisfy 16-byte stack
alignment. A first-run trampoline provides a valid return/exit path.
Caller-saved registers are not assumed persistent across an ordinary
function call; tests must validate the actual C ABI contract.
Never enable floating-point/SIMD use without a dedicated save/restore
policy; kernel should remain compiled without red zone.
Future asynchronous preemption requires the entire interrupted GPR state,
RIP/CS/RFLAGS/RSP/SS as applicable, floating-point policy and explicit
interrupt-frame ownership; it cannot reuse the cooperative call ABI
unchanged. CR3 and ring-3 transition are deferred.

Each task owns independent mapped stack backing, proposed 16 KiB minimum,
allocated as separately tracked ordinary kernel frames rather than from
the current one-page M6 service. Mapping and frame ownership, stack
alignment and any feasible guard pages must be verified before B begins.
Task creation failure must roll back every acquired frame, mapping and
descriptor. Stack memory may be reclaimed only after execution switches
to a separate surviving stack. Preserve a static/bootstrap/idle context.
A bounded non-allocating reserve for future supervisor context, descriptor,
stack and scheduling availability cannot be used by ordinary creation.

## M10-C timer interaction — independent gate
Keep PIT0 ~100 Hz, APIC vector 0x30, LAPIC EOI, masked legacy PIC and
spurious vector 0xff unchanged. Existing M9 x86_irq0_entry pushes all
15 GPRs, realigns the stack before calling C, and returns by IRETQ.
The handler may atomically account a tick and set a reschedule-request flag;
it must not allocate, lock, switch stacks, call the cooperative scheduler,
do framebuffer work or change task ownership. M10-C does NOT introduce
asynchronous preemption. Read the flag at an explicitly safe non-IRQ
cooperative boundary and ensure IF is restored to its entry state.
For a single CPU, critical sections preserve the previous IF and restore
that previous state, never blindly STI; no task may yield with such a
section held. SMP and true interrupt-entry scheduling remain future ADRs.
Existing keyboard polling and F12 PIT2 operation must remain functional.

## Interfaces (provisional internal contracts, not an ABI)
Task table init; create descriptor; make runnable; pick next runnable;
mark running; yield; block; wake; terminate; validate invariants.
All return explicit results, do no I/O, preserve caller buffers on
rejection and document ownership. The B switch additionally accepts
validated old/new context storage and must not attempt to free the old stack.
C provides scheduler_note_tick()/scheduler_reschedule_requested() style
mechanisms, with actual naming decided by the code review.

## Tests and acceptance
A: hosted ASan/UBSan, table exhaustion, ID reuse/generation wrap,
empty/single/multiple ready queue, FIFO fairness, repeated yield, all blocked,
illegal transitions, termination while queued, invariant checks after
every mutation, stale-ID rejection, and no mutation on rejected calls.
B: QEMU proof of multiple independent tasks voluntarily yielding, exact
callee-saved register values, stack alignment and sentinel integrity,
correct first-run/exit/restart behavior, error and allocation rollback,
no execution after stack reclamation, safe bootstrap/idle fallback.
C: real-QEMU 100 Hz tick accounting under multi-task CPU load, bounded
IRQ handler, unchanged LAPIC EOI and no timer-driven switch, stable F12,
screen editor and keyboard operation.
Every integrated checkpoint reruns 128/256/512 MiB IRQ0/clock, masked-route
negative, memory/MMIO/PIC/APIC/IDT/TSS, exceptions #UD/#DF, exact framebuffer,
nine workspace and F10/F12, full keyboard, editing/reflow/scrolling tests.
Run QEMU only with isolated OVMF variables; no physical disks or network.
Failure at any gate blocks advancement; never weaken established tests.

## Alternatives considered
Directly preempt in IRQ0: rejected for A–C; current memory/editor code
was written without preemptive reentrancy guarantees.
Large multi-level priority scheduler: deferred until cooperative correctness.
Reuse M9 IRQ stub as switch primitive: rejected, incorrect ownership model.
Use existing retained 4 KiB heap for stacks: rejected, insufficient backing.
One context per workspace: rejected; workspaces are UI data, not processes.
A full Linux-style process API: rejected, unnecessary premature ABI.
Static test stacks: may be used in initial diagnostic only, not as a
substitute for validated frame ownership on release paths.

## Rollback and release process
Keep M9-3D6 known-good ISO and checksum
06856f9311f7894d5b870add2afd0b457309853433a689e4c74f1f11aa0b5a32.
The Forge working ISO observed on 2026-11-12 hashed
758492d3b4aea5587e28514e5882231929e25a90f31c7ce1a38bae8135eb4ba2;
DO NOT overwrite it or assume it matches baseline until provenance is
resolved. Implement in isolated branch/worktree, record exact source SHA,
build and test evidence, checksum candidate separately, install only after
owner explicitly authorizes merge/deployment. Git worktree validation uses
git commands; .git may be a file.

## Deferred work and approval gates
M10-D: time slicing, asynchronous interrupt-frame handoff, full-state
preservation, priority policy and fairness/starvation safeguards. Requires
its own architecture review and explicit owner approval.
Future: user-mode ring 3, CR3 switching, syscall ABI, SIMD save/restore,
SMP locking, protected-process fault isolation and runnable workspace apps.

## Change history
2026-11-12: owner approved M10-A/B/C and explicitly separated M10-D.
Initial baseline audit found the expected source commit, but current
Forge ISO checksum differs from prior acceptance artifact; QWEN executable
was located outside the remote agent PATH. Implementation and tests not
yet claimed.
