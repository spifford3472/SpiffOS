# Open Decisions Register — v0.1
Date: 2026-10-20. All items OPEN unless explicitly marked PROPOSED or DEFERRED. No implementation authorization implied.

| ID | Priority | Decision | Dependencies / acceptance question |
|---|---|---|---|
| O-001 | P0 | Loader strategy, kernel image, binary separation | UEFI exit, fast boot, recovery, debuggability |
| O-002 | P0 | C toolchain, freestanding runtime, assembly ABI | Reproducible QEMU builds; minimal dependencies; language PROPOSED |
| O-003 | P0 | Paging layout, process address spaces, allocators | Ring-3 isolation, fault handling, limits |
| O-004 | P0 | Interrupt/syscall ABI, scheduler timer/quantum | Preemption and supervisor responsiveness |
| O-005 | P0 | Kernel vs user-space driver/service boundaries | Complexity, fault containment, protected APIs |
| O-006 | P0 | Workspace foreground/composition API | ADR-008 settles one foreground app per workspace; surface/focus details open |
| O-007 | P0 | Supervisor reserve sizing and takeover implementation | ADR-008 requires protected CPU/memory/input/display resources |
| O-008 | P0 | SpiffFS on-disk format, compatibility matrix and migration protocol | ADR-003/004/008 establish A/B and crash-consistency policy |
| O-009 | P0 | Virtual PEEK/POKE graphics/sound address contract | Educational low-level access without violating isolation |
| O-010 | P1 | Unified Message Manager quotas, overflow, cancellation, event naming and handler semantics | ADR-007 settles one engine and both routing modes; details remain open |
| O-011 | P1 | BASIC grammar, typing, scoping, errors and modules | Parser and conformance tests |
| O-012 | P1 | Interpreter safe points, event-handler blocking/reentrancy | Event-driven safety and responsiveness |
| O-013 | P1 | Input focus vs broadcast keyboard subscriptions | Multiple subscribers without unintended keystrokes |
| O-014 | P1 | Logical resolution, text modes, sprites and frame sync | Virtual buffers and device drivers |
| O-015 | P1 | Sound voices, mixer, background timing | Music/effects with foreground-only output |
| O-016 | P1 | Hardware inventory, minimum CPU/RAM, USB and driver ABI | Laptop and QEMU targets |
| O-017 | P1 | IPv4 stack, DHCP, TCP/UDP/DNS, sockets and async events | Ethernet, timers, buffering, security |
| O-018 | P1 | BASIC file commands and persistence/error contracts | Chosen filesystem semantics |
| O-019 | P2 | Editor navigation, undo, source management and output overflow | Unnumbered BASIC and two-pane editor |
| O-020 | P2 | Turbo Pascal menu research and adapted keyboard scheme | No menu design approved |
| O-021 | P2 | Workspace overview, default programs, resource limits | Task ownership and virtual screen |
| O-022 | P2 | Driver recovery, watchdog, diagnostic boot | Supervisor and hardware failure cases |
| O-023 | P2 | Release policy, compatibility and native applications | ABI and language stability |
| O-024 | DEFERRED | Wi-Fi, IPv6, optional Python/MicroPython, multi-monitor, legacy BIOS | Reassess after foundational releases |

| O-025 | P0 | File transaction and conflict-token APIs | ADR-008 requires locks, atomic commits and stale-write detection |\n\n## Potential tensions (not contradictions resolved by fiat)
- UEFI fast boot vs hardware discovery and recovery: define bounded initialization and diagnostics.
- Ring-3 isolation vs raw PEEK/POKE: choose virtual device registers rather than physical kernel addresses, subject to approval.
- Multiple keyboard subscribers vs focus: focused application input is default; privileged broadcasts require explicit authorization under ADR-007.
- High-priority tasks vs supervisor: reserve scheduler/input/display paths and define preemption rules.
- Background audio advancing vs muted hardware output: separate logical playback timeline from physical mixer.
- Nine workspaces vs multiple tasks: explicitly define workspace/task relationship.
- Documentation-first sequencing vs early QEMU experiments: exploratory research must not become implementation before design gate.

## Approval gates
G1: architecture and decision inventory; G2: boot/kernel/memory/scheduling interfaces; G3: BASIC/IPC/workspace/editor contracts; G4: graphics/audio/storage/network/driver contracts; G5: authorization for code. Owner explicitly approves each gate. Review this register at every PR.

## Status changes — 2026-10-26

- **O-001 partially resolved:** UEFI + Limine approved (ADR-011). Kernel image layout, boot protocol version pinning, recovery and binary boundaries remain OPEN.
- **O-002 partially resolved:** C with minimal assembly approved. Exact compiler/toolchain, freestanding runtime and ABI remain OPEN.
- **O-003 partially resolved:** 64-bit paging, separate user address spaces, RAM-only/no swap, reserved emergency memory and recoverable OOM approved. Exact page-table layout, allocators and reserve sizing remain OPEN.
- **O-005 partially resolved:** modular monolithic kernel and user-space BASIC/EDIT approved. Driver interface details and service placement remain OPEN.
- **O-016 partially resolved:** HP Anvilone UEFI laptop (i5-7200U, 16 GB, Intel HD 620, SATA AHCI, Realtek Ethernet) selected for first physical USB tests; full hardware support requirements remain OPEN.
- **O-019 clarified:** EDIT is an optional user application, not the boot screen; UX details remain deferred.
- **O-015 conflict to resolve:** older foreground-only audible audio contradicts later approved background audio/mixer concept. Review ADR-010 before implementation.
- **Storage policy conflict to resolve:** older deletion retention and file versioning drafts vs later manual-trash preference. Reconcile before SpiffFS implementation.
- **New P0:** exact syscall ABI, kernel/userspace boundary validation, reserve sizing and error contracts.
- **New governance requirement:** AI tasks must read root AGENTS.md and maintain the Developer's Guide and ADRs (ADR-011).

Keep original open items above as historical inventory; these status notes take precedence where explicitly approved.

## Conflict triage — 2026-10-26 review

- **Audio policy resolved:** use ADR-010's continued background playback and shared Audio Manager, consistent with the latest approval. Older foreground-only mute language is superseded. Exact mixer limits remain open.
- **Filesystem recovery policy remains a major subsystem decision:** older retention/versioning specifications conflict with later preference for manually managed trash. Obtain an explicit official choice before changing SpiffFS contracts.
- **Graphics implementation detail:** later approved default logical modes and opt-in collision behavior supersede older proposals where approval is clear; exact APIs/driver implementation still open. Avoid spending initial boot milestones on these details.

**Filesystem policy resolved 2026-10-26:** manual Trash, one current saved version, no automatic retention/purge; see ADR-012. O-008 remains OPEN only for implementation details (on-disk format, crash consistency, recovery, transaction semantics, compatibility and A/B safeguards). Do not treat older automatic versioning/retention policies as current.

## First-boot milestone — 2026-10-28

For the QEMU-only smoke test, the pinned Limine protocol and binary pair,
base revision 5, C entry and template-derived ELF layout are recorded in
[ADR-011](ADR-011-boot-kernel-memory-governance.md) and the
[boot record](../operations/first-boot-2026-10-28.md). The exact serial marker
was captured in two passing QEMU tests, including a fresh build. Future process
VM layout, runtime ABI, allocators, recovery/A-B and BASIC remain open.

## Scoped framebuffer diagnostic — 2026-10-28

The owner authorized a QEMU boot pattern as the next verified step after
`be74fe2`; see ADR-011. O-014 logical resolution, composition, sprites and
BASIC graphics interfaces remain open. This diagnostic settles none of them.

## Fixed boot label scope — 2026-10-28

The owner authorized a fixed bitmap SPIFFOS label after `8f48fa7`; see ADR-011.
General text encoding, console/cursor/scrolling behavior, font selection, BASIC
interfaces and logical graphics policy remain open.

## Boot console scope — 2026-10-28

A bounded kernel diagnostic console is authorized after `25150f0`; see ADR-011.
Scrolling, input, full text encoding, visible cursor, user console interfaces
and BASIC remain open and outside this step.

## Scroll and logo scope — 2026-10-28

Console scrolling and reuse of the existing SVG logo are now authorized;
see ADR-011. Input, visible cursor, scroll history and final user-space
console/display contracts remain open.

## Owner correction: initial logo content — 2026-10-28

Logo scrolls with workspace content and returns only on explicit reset/boot.
No permanent header reservation. See ADR-011; user reset commands and keyboard
input remain open.

## QEMU keyboard milestone — 2026-10-29

Owner authorized direct architecture/implementation, merge and local QEMU deployment.
See docs/decisions/ADR-qemu-keyboard-input.md (repository-relative) for scope,
rationale and validation. PS/2 polling is a temporary kernel diagnostic; USB,
interrupts, full character input, shortcuts, BASIC and the final input ABI remain open.
Prior no-merge restrictions are superseded for this milestone only.

## Safe launcher and screen editing — owner authorization 2026-10-29

The owner authorized direct ChatGPT architecture, implementation, local builds,
tests and documentation for safer QEMU launching and stored logical input with
screen editing. No delegated model/agent is used. This scoped exception supersedes
the historical Qwen-first requirement only for these objectives. PR #13's merge
and deployment authorization does not extend here: prepare tested PRs for review;
no merge, physical disk, USB, firmware, network or unrelated service changes.
Preserve the original checkout/index and unrelated work. See
[launcher decision](../decisions/ADR-safe-qemu-launcher.md). The owner has now
settled overwrite/Insert and push-down reflow; remaining work follows verified
implementation checkpoints.

### Logical-line screen editor design — 2026-10-29

Owner chose default overwrite, the Insert key to toggle insert mode, and
push-down reflow when a growing logical line needs another physical row. The
visible cursor is a full-cell block in overwrite mode and a two-pixel-thick
underscore at the bottom of the cell in insert mode.
[Screen-editor design](../decisions/ADR-screen-editor-logical-lines.md) records
the rules. The first implementation checkpoint now stores a complete bounded
logical line, uses the full display width, soft-wraps without submission, submits
only on Enter and Backspaces across wraps; see
[validation](../operations/logical-lines-2026-10-29.md). The next checkpoint
implements arrow navigation, the block/underscore cursor, Insert/overwrite,
multiple stored screen lines, blank-cell padding and push-down/shrink reflow.
See [screen-editor validation](../operations/screen-editor-2026-10-29.md) for
actual evidence and limitations. General scrollback, asynchronous output,
BASIC execution and the final user-space console ABI remain open.

## Memory foundation — owner authorization 2026-10-29

The owner authorized memory foundations and direct work/Codex fallback after
Qwen timeouts, with very small Qwen jobs. See
[the memory ADR](../decisions/ADR-memory-foundation.md) for OS/session/workspace
ownership, M1 read-only inventory scope and validation. Shared RAM ownership,
protected recovery resources, no swap and recoverable OOM remain the direction.
Allocator/reserve sizing, quotas, process mappings and session layout remain
OPEN beyond this inventory checkpoint. M1 now implements read-only inventory;
see [actual memory validation](../operations/memory-inventory-2026-10-29.md).
No allocator, heap, reserve allocation or process isolation is claimed by M1.

## M2 allocation and timed merge/deployment authority — 2026-10-29

The owner authorized moving memory foundations forward and local QEMU merges/
deployments as needed until 2026-10-30 03:00 America/New_York (07:00 UTC).
See [M2 design](../decisions/ADR-frame-allocator.md): one ownership slot per
candidate frame, OS/session/workspace groups, explicit allocation/free errors
and a protected 64-page diagnostic reserve. Final reserve sizing, process
subownership, heap/mappings and concurrency remain open. This authorization
does not include physical devices or infrastructure; no merge after expiry.

## Memory services night work — owner authorization 2026-10-29

The owner approved checked page access/zeroing, a small kernel heap and owner-
scoped cleanup, using a persistent three-hour Forge engineering job. See
[M3–M5 design](../decisions/ADR-memory-services.md). Internal diagnostics do
not settle ring-3 mappings, syscalls, quotas, SMP or final reserve sizing.
Merge/local deployment authority expires 2026-10-30 03:00 Eastern; concrete
results must pass review and tests. The worker pushes its feature branch only.

## Memory checkpoint completion and renewed authority — 2026-10-30
The owner renewed local QEMU merge/deployment authorization through 14:00
America/New_York on 2026-10-30 (18:00 UTC), superseding the earlier 03:00 expiry
for subsequent reviewed checkpoints. Physical/infrastructure deployment remains
outside scope. M4 now implements a caller-owned bounded heap with 16-byte
alignment, zero-before-publication, split/coalesce and explicit failures. M5
provides workspace-only cleanup and a separate session cleanup operation.
The current boot probe uses and releases one ordinary page; it is not yet a
persistent multi-page service or nine running workspaces. Ring-3 mappings,
SMP/IRQ safety, quotas and stale-handle protection remain open. See the
[memory services decision](../decisions/ADR-memory-services.md) and
[actual validation record](../operations/memory-services-night-20261029.md).

## M6 retained memory service — 2026-10-30
Owner authorized continuing the next section: retain ordinary heap backing and
heap-allocated session/nine-workspace identity/lifetime records after boot.
See [M6 ADR](../decisions/ADR-persistent-memory-service.md). This is a bounded
one-page service, not a complete workspace manager or multi-page growth policy.
Local merge/deployment authority expires today at 14:00 Eastern (18:00 UTC).

## M6 stopped core checkpoint — 2026-10-30
Owner requested stop, commit and merge. M6 now supplies a pure service library
and focused sanitizer tests; kernel linkage, retained boot backing and M6
guest/late-retention evidence remain open. See ADR-persistent-memory-service
and operations/persistent-memory-service-20261030.md. This specific merge
instruction supersedes the expired time window only for this checkpoint.

## M6 boot integration — 2026-11-08
Owner authorized implementation, test, merge and local QEMU deployment. The
service now retains one ordinary M3-owned page and ten protected state roots.
A workspace reset is checked against another workspace's live client payload.
State/backing are revalidated after console and editor initialization. Capacity
growth, workspace UI, process isolation and concurrent allocation remain open.
See operations/m6-boot-retention-20261108.md for actual evidence/deployment limits.

Keyboard US punctuation, Delete and keypad completion is being implemented per
ADR-keyboard-completeness.md. Lowercase, Unicode, alternative layouts, lock LEDs
and keypad Home/End/PageUp/PageDown remain deferred.

## M7 workspace checkpoint — 2026-11-10

O-006 is partially scoped for diagnostic text workspace switching; future application composition remains OPEN. The approved checkpoint design is [M7 ADR](ADR-m7-text-workspace-switching.md).

- Verify F1..F9 workspace selection in the owner's interactive GTK/QEMU window; QMP synthetic events alone cannot validate desktop shortcut interception.

- M8 follow-up: replace boot-only PIT channel-0 sampling with a general monotonic timer driver before physical-hardware portability; QEMU diagnostic timing uses the existing firmware PIT setup without reprogramming interrupts.

- M8.1: owner reported F12 too short; verify PIT mode 3 tick semantics and enforce measured QEMU overlay duration, plus physical hardware follow-up.

## M9 follow-up decisions — 2026-11-11
Final interrupt controller/IRQ activation sequence, timer source portability, ring-3 syscall ABI, driver ABI stabilization and scheduling policy remain OPEN. M9 may provide internal testable scaffolding but must not silently lock these public interfaces.

- M9-34: Consider dynamically allocated owner metadata beyond the preserved 16 GiB diagnostic capacity; any future expanded owner enum must fit the current 8-bit slot representation or adopt a versioned wider scheme.

## M9-2 exception follow-ups — 2026-11-11
- A guarded, dedicated IST/TSS fault stack and double-fault containment remain
  open. The initial IDT uses the Limine-provided kernel stack.
- Do not enable STI, configure the IRQ controller, assign user exception policy
  or freeze an interrupt ABI until separate design and QEMU tests.

## M9-3A exception-stack follow-up (2026-11-11)

Guard-page mappings for the emergency stack, SMP/per-CPU TSS lifecycle, generic exception-stack handling, PIC/APIC selection and IRQ ABI remain open. Phase A only establishes a static boot-CPU double-fault IST; see [ADR-m9-3-exception-stacks](ADR-m9-3-exception-stacks.md).

## M9-3B portability follow-up

ACPI MADT, APIC selection, PIC absence detection, IRQ routing on physical hardware, and SMP interrupt-controller ownership remain open. No hardware IRQ is enabled in Phase B.

## M9-3C timer follow-up

Clock source beyond QEMU PIT0, physical calibration, sleep/deadlines, hardware timer migration, and wall-clock date remain open. The monotonic core has no active source before Phase D.

## APIC routing prerequisites (2026-11-11)
ACPI table lifetime, validated physical-memory translations, UC MMIO mappings, CPU LAPIC ID matching, non-QEMU GSI overrides, multiple IOAPICs and failure policy require explicit tests before STI. No assumption of fixed APIC address or route is accepted. See ADR-m9-3-apic-routing.md.

## M9-3D3 MMIO mapping (2026-11-11)

The read-only QEMU CR3 probe could not establish a verified virtual mapping for both ACPI-discovered APIC device pages. This is an availability result (it can also mean an intermediate table was not accessible through the restricted mapper), **not proof the devices are absent**. Requires a separate dedicated kernel MMIO virtual-address reservation, bounded page-table ownership/mutation and rollback, UC/PAT/MTRR cache policy, and host + QEMU negative tests before LAPIC/IOAPIC device access or STI. See ADR-m9-3-apic-routing.md.

## M9-3D4 mapping implementation gates — 2026-11-12
Owner approved a dedicated two-page APIC MMIO aperture; page-table root/ancestor physical ownership, safe 4/5-level virtual reservation, verified effective UC cache type (IA32_PAT/MTRR), NX and fault-safe rollback still require real validation. These are test gates, **not** permission to access MMIO prematurely. Hardware IRQ0 remains blocked pending successful mapped LAPIC/IOAPIC tests. See ADR-m9-3d4-mmio-mapping.md.

## M9-3D5 next gate — APIC register access (2026-11-12)
M9-3D4's private PTEs validate in QEMU with no MMIO reads. Before any LAPIC ID or IOAPIC version read, verify memory-type alias hazards and effective MTRR/PAT UC behavior; then use a bounded read-only hardware-probe adapter and negative tests. APIC controller writes, IRQ0 redirection/EOI, scheduler and STI remain blocked until separate validation. See ADR-m9-3d4-mmio-mapping.md and operations/m9-3d4-mmio-mapping-20261112.md.

## M9-3D5 read-only register proof and M9-3D6 IRQ0 — 2026-11-12
APIC register identity and I/O APIC version/GSI routing extent must be confirmed from M9-3D4's validated kernel-private mapping, with a conservative effective cache/alias gate. IOREGSEL selection is a narrowly bounded hardware write only for a register read; it must not touch configuration. Hardware IRQ0 routing, LAPIC EOI, PIT0 unmask and STI remain OPEN and unapproved as operational until their own tests. See ADR-m9-3d5-apic-readonly.md.

## M9-3D6 controlled IRQ0 readiness (2026-11-12)
With QEMU read-only register identities, IOAPIC version and MADT IRQ0 GSI validated under M9-3D5, active redirection configuration, LAPIC LVT/EOI, 100Hz PIT0 delivery, live tick/negative-storm tests, fallback and physical portability still remain OPEN. Preserve PIC masked, IF=0, keyboard polling and F12 PIT2 until separately validated. See ADR-m9-3d5-apic-readonly.md.

## M9-3D6 controlled IRQ implementation — 2026-11-12
Owner authorized the *tested* first single-boot-CPU APIC IRQ0, vector 0x30 and PIT0 timer path. Until actual QEMU positive/negative tick-delivery tests pass, IF/IRQ0 availability is not a completed operational feature. Do not generalize this QEMU path to physical laptops, multiple IOAPICs/SMP, scheduler, ring3 or PS/2 device IRQ. See ADR-m9-3d6-irq0-delivery.md.

## M9-3D6 post-IRQ validation follow-ups (2026-11-12)
The controlled boot-CPU QEMU IRQ0 path has real 100/200-tick evidence; physical-chipset support, several IOAPICs or CPUs, NMI/SMI coordination, pending-IRQ shutdown, sustained storm/error handling beyond the bounded timing regression, recalibration and any public user/kernel IRQ ABI remain OPEN. Do not generalize from QEMU q35 success. See ADR-m9-3d6-irq0-delivery.md.


## M10 open implementation gates — 2026-11-12

The owner approved the A–C architecture and requires separate M10-D acceptance. Before M10-B, prove mapped stack backing ownership, cleanup from a surviving stack, initial 16 KiB stack sizing, and assembly ABI/interrupt-state preservation. Before M10-C, prove the cooperative context switch in QEMU; no switching in IRQ0. Four priority classes and actual preemptive scheduling remain M10-D review items, while ring-3 user-process ABI, SMP, FPU/SIMD and CR3 switching are deferred. Resolve the discrepancy between the accepted M9-3D6 ISO SHA256 (06856f9311f7894d5b870add2afd0b457309853433a689e4c74f1f11aa0b5a32) and the Forge current ISO (758492d3b4aea5587e28514e5882231929e25a90f31c7ce1a38bae8135eb4ba2) before deployment. See [M10 ADR](ADR-m10-task-scheduler-foundation.md).

## M10-D preemption approval gate — 2026-11-13

The owner accepted the M10-A/B/C release and requested that development of M10-D begin. Design work has started in [the proposed M10-D ADR](ADR-m10-d-preemption-proposal.md). The owner approved the staged architecture and isolated D1–D5 implementation on 2026-11-13. Issue #58 remains OPEN for implementation and test acceptance; release merge/deployment still require separate authorization. Required implementation evidence includes: complete interrupt-frame/flags/RSP ownership, single-CPU non-reentrant IRQ dispatch, per-task stack lifetime, four-priority starvation safeguards, and supervisor reserved-service budget must be validated before any production preemption switch is enabled. QEMU-only tests must retain legacy IRQ0/keyboard/editor behavior. No M10-D merge or deployment is currently authorized.


## M10-D3 implementation/release boundary — 2026-11-13

The owner subsequently approved M10-D3 implementation, merge and Forge
QEMU deployment **only after passing release validation**. This is a
scoped exception to the earlier no-merge text for the separate
test-only real IRQ handoff: the default keyboard/editor remains
nonpreemptive. M10-D4 full priority scheduling and guaranteed supervisor
CPU service, M10-D5 generalized production preemption/ABI, ring 3,
physical devices, network and other interrupt routes all remain OPEN.
See `ADR-m10-d-preemption-proposal.md` and
`../operations/m10-d3-irq-handoff-20261113.md`.

## M10-D3.1 debug visibility and D4 open gates — 2026-11-13

The owner approved D3.1 then D4 development. Workspace metadata may be prepared at boot but inactive editor painting remains lazy; any nonpainting editor initializer and F10 diagnostics must be tested. Real four-priority preemptive scheduling, anti-starvation and supervisor recovery under CPU load remain M10-D4 acceptance gates. See [ADR-m10-d31-workspaces-debug-hub.md](ADR-m10-d31-workspaces-debug-hub.md).


## M10-D4 diagnostic acceptance versus D5 production — 2026-11-13

The owner authorized completing the D4 test-only milestone.
Four worker priorities, real task blocking and supervisor wake,
bounded boot-supervisor dispatch quota, single-waiter priority
inheritance and fail-closed task-frame recovery are implemented
in the isolated D4 completion branch and require the full
integration acceptance suite. The **production** task execution API,
ring3 user task isolation, SMP, multiple waiters/nested locks,
FPU/SIMD context, CR3 switching, exception containment and guaranteed
hardware realtime CPU budgets remain M10-D5/open architecture
requirements; diagnostic code must not be silently enabled in the
normal kernel. See `ADR-m10-d4-priority-supervisor-proof.md`.


## M10-D5.1 production task reclamation checkpoint — 2026-11-13

The owner authorized resolving the Forge QWEN access blocker and beginning the first D5.1 implementation, without extending merge or deployment authority. The existing bounded M10-A generation-tagged task table remains the sole candidate production lifecycle manager; the D4 diagnostic controller stays separate. Logical task termination now leaves descriptors unreclaimed until an explicit reaper confirmation. **This first implementation is metadata-only:** it does not establish a production reaper, detach/verify a physical execution stack, or enable normal-kernel IRQ preemption. Before activation, verify all saved contexts, waiter references, mutex owners and stack backing are retired by a surviving dispatcher, with QEMU tests. Ring 3, SIMD, task quotas and production lock graphs remain OPEN. See `ADR-m10-d5-production-task-model.md`.


## M10-D5.2a execution-aware cleanup remaining gates — 2026-11-13

The isolated reaper adapter checks a terminated generation-tagged task, owner stack bounds, saved RSP and real current boot-stack RSP, then releases owner-indexed backing before descriptor reuse. The M10-B diagnostic is its first real execution integration. **Production reaper integration remains OPEN** pending verified wait/lock ownership retirement, bounded cleanup queue, all-or-nothing memory-owner release semantics, authentic CPU context and separate supervisor authority controls. No full production scheduler/editor preemption is enabled. See `ADR-m10-d5-execution-aware-reaper.md`.


## M10-D5.2b open production integration gates — 2026-11-13

Resource registry and lifecycle tests may run in isolation, but existing kernel synchronization objects do not yet register their waiters and owners with it. Real mutex/wait registration, verified atomic stack-owner release or quarantine recovery, bounded supervisor reaper queue, and task/driver reference lifecycle remain OPEN before allowing general production task termination. No production editor preemption is authorized. See `ADR-m10-d5-resource-ownership.md`.
