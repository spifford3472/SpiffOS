# Proposed M11 first slice — Ring 3 transition and retirement in QEMU

Status: approved for isolated QEMU implementation and testing, 2026-11-22.
Based on the D5.9 preview integration at 0dd4c5c. A-032 and A-033 establish Ring 3 applications and
separate address spaces. O-003 and O-004 leave the detailed contracts open.

## Existing boundary

The M10-A task table owns lifecycle transitions. D5.2b and D5.9-B prove
generation-bound stack ownership and permit retirement from the surviving
dispatcher after switching off the task stack. The preview's IRQ selector
and D5.5c validation expect saved Ring 0 selectors and an interrupted RSP
inside the same owned kernel stack as the CPU frame. A Ring 3 transition
instead puts the CPU frame on a protected kernel entry stack and saves a
user RSP separately. Keep the preview validator strict.

The existing TSS installs an IST1 stack for double faults. The diagnostic
needs an explicitly installed RSP0 that points to its protected entry
stack. The existing IDT gates have DPL 0, and the common exception path
halts. The existing page walker serves supervisor mappings and MMIO
preflight. It cannot certify effective user R/W/X permissions.

## Proposed scope and alternatives

Use a separate build profile on the preview source base. Run a single
fixed diagnostic before the preview scheduler takes live IRQ ownership.
Use a separate four-level page-table root with a private user branch,
supervisor-only kernel mappings, an RX diagnostic page, an RW/NX user
stack and an unmapped guard. Check effective permissions through every
paging level and refuse unsupported boot conditions. Keep the bootstrap
root and its user permissions intact. Make the working set small enough
to verify ownership, failure rollback and restoration deterministically.
Treat sample placement as fixture data. Public virtual addresses, loading
formats, quotas and application ABI stay open.

Use RSP0 for the CPL change through a single private DPL-3 interrupt gate.
The gate accepts zero user arguments and supplies one completion event.
It validates the active task generation and CPU-saved origin on its kernel
stack before abandoning user execution. Using an intentional ordinary
exception as the success signal would conflate completion and containment.
Using SYSCALL would require additional entry-state and stack setup before
this proof could verify either. Keep IST1 for double faults. Qwen suggested
IST2 for the diagnostic. RSP0 directly exercises the normal privilege
transition, while IST2 would bypass it for the selected gate.

Keep this initial fixed-payload run serialized on one CPU with maskable
interrupts disabled and bound the guest with a host-side timeout. Resume
the surviving dispatcher, restore and verify the bootstrap CR3, switch off
the protected entry stack and retire the exact task generation using the
existing ownership paths. Return ownership of input and display to the
preview. Classify only positively identified expected test faults as
diagnostic termination. Unexpected faults retain the kernel fatal path.
Retain any backing whose ownership or quiescence is uncertain. The host
timeout supplies a test bound only. Ordinary programs and normal IRQ
preemption remain future work.

## Required observations

- Hosted checks verify effective page permissions, private-root ownership,
  rejection of invalid CPU-frame provenance or stale generations, and
  rollback or retention on partial setup failure.
- At 128, 256 and 512 MiB QEMU records real CPL 3 execution, the kernel
  stack change, and restoration of the bootstrap root. Verify that user
  code cannot access a known supervisor-only page, write the code page,
  execute its stack or cross its stack guard. Exercise the expected task
  fault path separately from clean completion.
- Verify one-time retirement off the task's entry stack, independent
  supervisor service, and ordinary editor input after the diagnostic.
  Confirm the existing preview profile, launcher and unflagged baseline
  through their normal entry points and independent CI.
- Stage a separate checksummed ISO only after acceptance. Roll back by
  closing the diagnostic guest and using the verified D5.9 preview or D4.

## Owner approval

Geoff approved the QEMU-only diagnostic and its private completion and
fault policy on 2026-11-22. The public syscall ABI, production fault policy and
normal IRQ scheduling across privilege levels require later decisions.

References: AGENTS.md, accepted.md, open.md, ADR-011, the D5.9 preview and
runtime memory wiring ADRs, and docs/architecture/system-overview.md.

## Hosted page-permission validation — 2026-11-23

The diagnostic has a read-only permission checker separate from the MMIO
walker. It verifies the selected top-level user branch and checks effective
user, write and execute permissions for 4 KiB leaves. The hosted fixtures cover
RX, RW/NX, presence versus unreadable tables, ancestor restrictions and an
unchanged result on failure. `make HOST_CC=gcc test-m11-page-permissions` passed
on Forge under ASan/UBSan, and the source compiled freestanding with Clang.
The unchanged M10-D5.9 baseline passed full Forge preview acceptance at 128,
256 and 512 MiB. Hardware entry and off-stack retirement remain pending.

Qwen's first permission request timed out, its next checked the wrong address
regions, and its split retry inspected the child table for root permissions
and did not correctly aggregate hardware permission bits. The lead replaced
only the failed read-only permission helpers and verified the focused cases.
