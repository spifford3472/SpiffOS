# D5.9-B authentic execution-context adapter — isolated implementation contract

Status: owner-authorized development, 2026-11-15; not runtime acceptance.

## Context and decision
The owner's autonomous M10-D5.9/M11 handoff authorizes this docs-first isolated
memory-safety implementation. Parent PR #90 at
8f77e47d7c5282d60d7890a7636b9a76e3972f60 passed independent CI run #107.
Read AGENTS.md, accepted/open registers, system-overview.md, DEVELOPERS_GUIDE.md,
ADR-m10-d59b-allocator-exclusion-contract.md and D5.7 before changing this path.

Use the existing D5.7 guard as the only owner/depth/cookie/fault authority.
A small adapter samples real x86 flags, disables IRQs before inspecting shared
state, validates initial boot CPU CPUID/APIC identity, genuine IRQ-entry depth,
authoritative M10-A RUNNING generation and D5.2b binding against live RSP.
Only successful outermost exit restores original IF. All invalid ownership or
corrupt state latches failure and retains IF=0; no forced unlock or task switch.
A hosted-only backend permits negative tests; it must not exist as an injectable
production backend. It is not hardware provenance evidence.

IRQ0 depth is recorded at actual x86 entry/exit only in the new separately
flagged build. No allocator, cleanup, logging or extra EOI is added to IRQ.
Unflagged assembly must retain identical executable bytes.

## Alternatives and consequences
A second standalone hosted gate as a kernel lock duplicates D5.7 and is rejected.
Blind CLI around all memory operations hides potentially long IRQ-off spans and
is rejected. Initial measurements cover selected short frame transactions;
heap zeroing, service resets, mapping rollback and stack lifetime still need
separate review. This adapter has no bootstrap/no-task bypass. Boot startup and
supervisor termination retain their existing exclusive nonpreemptive contracts;
task allocation tests occur only on an independently registered RUNNING stack.

## Validation gates
Strict C11 hosted GCC ASan/UBSan and Clang compilation; original IF on/off;
nested LIFO; stale generation, wrong CPU, live RSP outside stack, IRQ context,
corrupt guard depth, invalid cookie, terminated current task, no reset after
fault. Rejections never perform an allocator operation or restore IF.
Real IRQ0 QEMU evidence at 128/256/512 MiB must show independently preempted
workers, protected frame success/release and accounting, no IRQ allocation,
latency measurement and continued editor boot. Source inventory, all inherited
regressions, default stripped ELF compatibility and exact-head CI are required.
Until those pass the adapter is an unaccepted isolated candidate.

## Rollback and exclusions
Discard the feature branch; preserve accepted D4 ISO
0f2dbca116bd991cd44ad7b477042a8bf13caae67711819290b30b8b5bdb9cf6.
No merge, accepted ISO replacement, unrestricted ordinary preemption, host package,
privilege, physical disk, infrastructure or firewall changes.

Change history: 2026-11-15 recorded owner scope before runtime edits.
