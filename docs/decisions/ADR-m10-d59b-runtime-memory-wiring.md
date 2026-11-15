# D5.9-B runtime memory admission and fixed page initialization

Status: owner-authorized isolated implementation, 2026-11-15. Parent #92 at
c0cc335edce315252591dad51781bd9b76b87976 passed full Forge acceptance and
independent exact-head CI #109. No merge, deployment or ordinary preemption.

Read AGENTS.md, accepted/open registers, system overview, developer guide,
D5.9 production audit, allocator exclusion contract, authentic-context and
service-transaction ADRs, M2/M3/M4/M6 APIs, D5.2b resource/reaper contracts.

## Decision and scope

Move from probes to admission at existing frame/heap/service entry points,
under only SPIFFOS_M10_D59B_MEMORY_WIRING_TEST. One existing implementation per
allocator/service remains authoritative; thin wrappers protect the same bodies.
D5.7 remains the only task-generation/cookie guard. Existing unflagged code and
accepted D4 image must remain byte-identical after debug stripping.

The adapter has explicit lifecycle modes. Bootstrap/retirement has one verified
boot CPU, no independently scheduled task and off-IRQ structured C irq-save
scopes. It does not invent a synthetic M10-A task identity or another task lock.
Runtime binds once, IF-off from the surviving boot stack, to the already-created
reserved supervisor/current M10-A table and D5.2b registry. Runtime scopes use
actual CPU/IF/IRQ/RSP/generation and the SAME D5.7 LIFO cookies. Only the verified
off-stack, no-RUNNING-task dispatcher may unbind after IRQ handoff is disarmed;
the IRQ selector also requires the binding before any task handoff. No rebind,
guard reset, forced switch, forced orphan unlock or active-stack reclamation.

Unsupported lifecycle/report/snapshot/MMIO boot APIs reject runtime use before
callbacks or mutations. D5.9-C/D will separately admit their actual owners;
this is not permission to hold IRQs off across a multi-page workspace snapshot.
Service/heap pointer lifetimes retain their documented caller contracts; root
reset/retirement is boot/quiescent-only until its concurrent lifetime protocol
is proved. A void lifecycle violation must stop safely rather than pretend it
succeeded and allow backing release.

## Page initialization without an arbitrary protected callback

The existing M3 generic API accepts a caller-supplied backend. D5.9's contract
forbids arbitrary callbacks inside protected runtime transactions. Keep the
legacy generic backend path boot-only. Introduce a small callback-free M3 claim
primitive sharing existing M2 allocation/M3 authorization/rollback, preserving
output alias checks. The kernel bridge invokes only the fixed, audited 4096-byte
kernel zeroing leaf by a direct call, then publishes the page after success.
No caller-supplied function pointer is executed while the task guard is held.
The claim cannot escape its outer protected transaction uninitialized.

Known boot helpers route through that fixed leaf in the flagged build. Failure
unwinds exact ownership with untouched public output; uncertain rollback faults
and retains storage, never manufacturing successful recovery.

## Stack and context protection

Use the existing D5.2b stack bindings plus the validated HHDM offset to recognize
physical backing. Do not add a second ownership registry. Runtime stack-frame
release is forbidden. Retirement requires exact TERMINATED generation, zero
registered references, surviving RSP outside all bindings and no quarantine.
A FREE physical slot still covered by a retained/quarantined binding is not an
allocation candidate. Preserve uncertain backing metadata on release failure.

Unexpected scope-exit corruption is nonreturning and IF-off: publication/error
contracts cannot be preserved by returning into a potentially corrupted caller.
No automatic cookie recovery or blind STI. Report the fault without allocating.

## Evidence and remaining gates

Observational QEMU tests of the unchanged heap used 1023 active metadata records
and the 1MiB maximum allocation. Across 128/256/512MiB, sampled allocation/zeroing
spans were about 1.58–1.65ms and bulk session cleanup about 0.064–0.067ms using a
nominal PIT/TSC ratio. These are observations, not exhaustive physical bounds.
The initial 4MiB static benchmark arena failed 128MiB loader startup; the fixture
was reduced to the largest allocation plus metadata clients and 4096 spare
bytes. This changes no accepted kernel or memory policy.

Require strict hosted GCC ASan/UBSan, native/freestanding Clang, real IRQ0 at all
three sizes, direct-entry rejection and saved-IF tests, protected stack release
negatives, page success/map/zero rollback and output/accounting checks, source
inventory, full inherited regression and exact-head CI. Runtime admission is
initially limited to the tested single-CPU QEMU memory profile (<=512MiB frame
tracking). No physical deadline, SMP, Ring 3 or unrestricted caller certification.

Rollback: discard isolated branch; parent #92 and owner D4 remain. No physical
disk, network, OPNsense, host package/privilege or other-project changes.

Change history: 2026-11-15 docs-first integration contract and measured limits.
