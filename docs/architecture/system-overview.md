# SpiffOS System Architecture — Baseline v0.1
Status: PROPOSED DOCUMENT; architecture decisions below retain their individually recorded statuses. Date: 2026-10-20.

## Mission and boundaries
SpiffOS is a new native x86-64 operating system inspired by the simplicity and immediacy of early personal computers. It is neither an emulator, Linux distribution, nor compatibility layer. Motto: **Simple enough to understand. Powerful enough to create.**

## Accepted system model
UEFI boots a standalone SpiffOS kernel into an interactive, unnumbered structured BASIC environment. No conventional desktop. QEMU on Forge precedes physical laptop validation. The kernel runs in ring 0; BASIC applications in ring 3, isolated from kernel memory and one another. A timer-driven preemptive scheduler supports Background, Normal, Interactive, and Time-sensitive priorities; priority never grants privilege.

Nine independent virtual BASIC workspaces retain program, cursor, screen, and execution state. Ctrl+Alt+1–9 select workspaces, Ctrl+Alt+0 shows overview, Ctrl+Alt+Delete enters a privileged supervisor rather than ordinarily rebooting. Only the foreground workspace owns physical display and audible output; background playback advances silently.

A unified kernel Message Manager provides process inboxes, direct single-recipient messages, correlated request/reply and named event subscriptions with per-subscriber fan-out; subscriptions receive future events only, with polling and safe-point event handlers. Queue limits, overflow policy and exact API syntax remain open; payload families, immutable delivery, kernel permissions and safety requirements are recorded in ADR-006/007. BASIC supports named SUB/FUNCTION, local variables, structured control flow, files, graphics, sound, networking, processes and messages. Interactive screen entry works away from a fixed bottom prompt; EDIT opens a two-pane source/command environment, Tab switches panes, EXIT leaves editor. Pane focus must be visibly explicit, with a Turbo Pascal-inspired but unapproved menu design.

Logical graphics resolution is independent of physical hardware. Documented virtual PEEK/POKE device access must not compromise ring-3 isolation. Sound supports simultaneous music/effects. Hardware uses modular drivers. Storage supports folders and power-loss resilience without promising survival of unsaved writes. Wired TCP/IP (IPv4, TCP, UDP, DNS, client/server APIs) is a first-class capability; Wi-Fi is later.

## Proposed component boundaries (not approved interfaces)
UEFI loader → kernel startup/interrupts/memory/scheduler/syscalls → task and message services → BASIC runtime and supervisory UI. Drivers for display/input/storage/audio/Ethernet serve virtual workspace devices. Filesystem and network services expose controlled APIs. This diagram is conceptual; whether services/drivers execute in kernel or user space is OPEN.

ADR-008 adds a compatibility contract: versioned SpiffFS rollback checks, exclusive locks plus atomic transactions and stale-write detection, one foreground application per workspace, and reserved emergency Supervisor CPU/memory/input/display paths.\n\n## Requirements and verification
R-BOOT: boot directly to BASIC; QEMU boot-time test.
R-ISO: ring-3 cannot write ring-0 or other process memory; fault injection.
R-SCHED: preemption and four priorities; CPU contention tests.
R-SUP: Ctrl+Alt+Delete reachable during runaway BASIC; stress tests.
R-IPC: subscription-time fan-out and bounded delivery; queue tests.
R-WS: nine retained virtual screens and background task progress; switching tests.
R-AUDIO: background muted with advancing timeline; transition tests.
R-STORAGE: sudden power loss must not routinely prevent boot; crash simulation.
R-NET: BASIC network client/server use cases; integration tests.
R-DOC: every BASIC command specifies syntax, parameters, behavior, errors and examples; documentation lint/review.

## Dependency and risk notes
A ring-3 BASIC prompt requires boot, paging, interrupts, user-mode transition and syscall boundaries. Workspace isolation requires process ownership and virtual display/input. Event callbacks require interpreter safe points. PEEK/POKE requires explicit virtual address semantics. Emergency supervisor must reserve execution/input/display resources. Power-loss-safe filesystem design must precede persistent writable storage. Networking depends on drivers, timers, memory buffers and scheduling.

## Preliminary stages (not approved schedule)
Architecture approval; loader/kernel; protection/interrupts; scheduler; processes/workspaces; BASIC; screen/editor; IPC; graphics/audio; storage; drivers; networking; physical validation. Reconcile dependencies before approving a delivery schedule.

## Governance
Documentation first; code second. All implementation by AI agents, with owner approval of architecture and merges/deployments. Feature branches, PRs, validation, ADRs, and change history required. This baseline is for review and does not authorize implementation.

## Approved 2026-10-26 architectural update (ADR-011)

This section supersedes older *proposed* language where it conflicts with the owner's newer approvals. Initial boot uses **UEFI + Limine**, QEMU first and HP Anvilone via dedicated USB later; Anvilone's Secure Boot is currently enabled, so unsigned USB tests require disabling it at test time. The kernel is **modular monolithic C**, minimal x86-64 assembly, with ring 0 kernel and ring 3 user processes. BASIC and optional EDIT are separate applications. **The default boot UX is a BASIC READY prompt, not an always-open IDE.** Memory uses 64-bit paging, separate user address spaces, no swap and a protected emergency reserve, with recoverable out-of-memory errors rather than automatic process killing. Exact syscall ABI, allocators and driver interfaces are still open.

Source code must meet the explanation and function-header requirements in [AGENTS.md](../../AGENTS.md) and [Developer's Guide](../DEVELOPERS_GUIDE.md). See [ADR-011](../decisions/ADR-011-boot-kernel-memory-governance.md) for alternatives, rationale and consequences. Background-audio and filesystem-retention conflicts in earlier drafts are flagged for reconciliation, not silently settled by this update.

## Decision precedence and resolved audio conflict (2026-10-26)

The latest explicitly approved owner decision prevails over older conflicting drafts; major subsystem conflicts require an explicit owner decision and updated ADR. Earlier foreground-only audio wording is superseded: background workspace playback may remain audible through the single system-wide Audio Manager, with per-workspace mute and volume, consistent with ADR-010. Older always-open editor wording is superseded by the BASIC READY prompt and separately launched EDIT application. Filesystem retention/recovery differences remain OPEN pending explicit review.

## Approved SpiffFS user-facing policy (2026-10-26)

SpiffFS stores one current saved version per file; deleting moves a file to user-managed Trash until explicitly emptied. There is no automatic saved-version history or Trash expiration/purge. This supersedes older retention/versioning drafts. Crash-consistent updates, power-loss-resistant boot and recovery are independent mandatory requirements; implementation mechanisms remain open. See [ADR-012](../decisions/ADR-012-spifffs-trash-policy.md).

## Verified first-boot scope — 2026-10-28

A minimal native x86-64 kernel now boots through pinned Limine in QEMU/OVMF
and prints `SPIFFOS_BOOT_OK` on COM1 before halting. This is a smoke test, not
the BASIC READY environment or a completed architecture. See the
[recovery and validation record](../operations/first-boot-2026-10-28.md) for
protocol compatibility, workspace configuration, evidence and limitations.

## Framebuffer diagnostic scope — 2026-10-28

The next authorized boot-only diagnostic draws a pattern directly into the
first compatible Limine framebuffer. It uses firmware-selected physical
dimensions and does not establish a logical graphics mode or workspace API.
Validation status is recorded in `docs/operations/framebuffer-2026-10-28.md`.

## Fixed boot label scope — 2026-10-28

The next authorized diagnostic adds bitmap SPIFFOS lettering over the boot
pattern. This uses the existing borrowed Limine framebuffer and does not
establish a console or a user-space graphics interface. See
[the text validation record](../operations/text-2026-10-28.md).

## Boot console diagnostic — 2026-10-28

The next authorized step adds a small caller-owned text console over the
existing framebuffer. Only kernel startup uses it; process/workspace console
interfaces remain open. See [console validation](../operations/console-2026-10-28.md).

## Scrolling power-up presentation — 2026-10-28

The next authorized diagnostic scrolls only its console viewport and embeds a
bitmap derived from the original branding SVG above it. The logo stays outside
the scrolling area, with no added boot delay. See
[validation](../operations/scroll-logo-2026-10-28.md).

## Owner correction: reclaimable logo area — 2026-10-28

The earlier fixed-logo header is superseded: the entire drawable interior is
one scrolling text workspace. The logo is initial content and returns only
on workspace reset or boot. See [validation](../operations/workspace-logo-2026-10-28.md).

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

M2 now implements internal ownership accounting and a reversible boot probe.
The ordinary and protected reserve pools have explicit errors and independently
checked counters. Returned physical addresses are not dereferenced; heap,
process mappings and actual workspace memory services remain future work.
See [actual M2 validation](../operations/frame-allocator-2026-10-29.md).

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

The polling input diagnostic now prepares US punctuation, Delete-at-cursor and
NumLock keypad input. It retains uppercase alphabet behavior and existing editor
records/reflow. See decisions/ADR-keyboard-completeness.md for limits.

## M7 diagnostic text workspaces

The owner-authorized M7 checkpoint is specified in [ADR-m7-text-workspace-switching](../decisions/ADR-m7-text-workspace-switching.md). Nine editor text workspaces are diagnostic precursors to the future ring-3 BASIC workspace manager; this does not imply a process scheduler or BASIC implementation.

M7 keyboard: F1..F9 are alternate workspace selectors alongside Ctrl+Alt+top-row 1..9. Host desktop interception of the Ctrl+Alt chord was observed in the Forge interactive session.

M8 implemented: F1..F9 exclusive workspace keys, F10 read-only system screen, F12 timed 128x128 non-destructive workspace indicator with two fade stages and exact restoration (ADR-m8-function-keys-and-workspace-indicator.md). Uses boot-only PIT channel-2 mode-2 polling; no general timer driver yet.

## M9 low-memory boot repair (2026-11-11)
The nine diagnostic workspace snapshots now borrow ordinary M2/M3-owned 4KiB
pages after kernel entry rather than occupying about 35 MiB of boot ELF BSS.
Full backing is prepared before screen capture; failure cannot change the
active workspace or visible pixels. Owner token slots are uint8_t (values
0..12) with unchanged 16 GiB candidate frame tracking capacity, reducing
loader-time metadata by half. This is an internal implementation, not ring-3
paging or a stable user ABI. See ADR-m9-128m-boot-memory.md and
operations/m9-boot-memory-repair-20261111.md.

## M9-2 interrupt foundations (scope, 2026-11-11)
The first x86-64 interrupt layer is a static exception-only IDT loaded after
serial initialization. Fatal CPU faults report a bounded serial diagnostic
and halt; hardware IRQs and timer preemption remain disabled. The existing
PS/2 polling and PIT-channel-2 F12 timing remain unchanged. See
ADR-m9-exception-idt.md for risk limits and validation.

## M9-3A bootstrap fault isolation (2026-11-11)

The Phase A candidate initializes a static boot-CPU TSS through a private GDT that retains the loader descriptor layout; exception vector 8 uses a dedicated IST1 emergency stack. It does not yet provide guard pages, per-CPU isolation, IRQ delivery or scheduling. See [ADR-m9-3-exception-stacks](../decisions/ADR-m9-3-exception-stacks.md). Verification results and release state are recorded independently.

## M9-3B interrupt-controller baseline

The boot CPU remaps the QEMU-compatible dual 8259 PIC to vectors 32–47 with every IRQ masked and IF=0. CPU exception vectors 0–31 are unchanged. No IRQ handlers or interrupt timer are enabled until later validated checkpoints. This is an internal boot foundation, not an approved universal interrupt-controller ABI.

## M9-3C monotonic clock foundation

A static 64-bit saturating tick counter is initialized with an intended 100 Hz rate but returns unavailable until a real IRQ0 tick arrives. Hosted arithmetic tests verify conversion and overflow. PIT channel 2 F12 overlay is unchanged; Phase D controls the first actual IRQ enable. See ADR-m9-3-monotonic-clock.md.

## Selected M9-3D APIC-first design (not yet implemented)
The owner selected ACPI RSDP/XSDT/MADT discovery of LAPIC and IOAPIC topology before enabling only PIT0 IRQ0 through a dedicated vector. Existing PIC remains fully masked; a validated MMIO mapping and safe EOI/masks are prerequisites. The last verified mainline checkpoint remains M9-3C; see ADR-m9-3-apic-routing.md.

## APIC MMIO readiness gate (read-only, 2026-11-11)

The Phase D3 exploratory page-table walker verifies only whether the existing CR3 translation and caller-validated RAM page-table pointers can prove a mapping from HHDM+ACPI controller physical addresses. QEMU reported mapping **unverified**. No new page mapping, APIC MMIO access, device interrupt, or scheduler was added; M9-3C remains the last IRQ-disabled operational behavior.

## M9-3D4 approved kernel MMIO mapping foundation — 2026-11-12
The next internal kernel service reserves two supervisor-only, read/write, NX, verified-UC virtual pages for the discovered LAPIC and IOAPIC physical windows. It must use existing M2/M3 ownership for page-table RAM, validate every active level before attaching an entry, and transactionally roll back any failed allocation/link. Existing HHDM and loader mappings must remain untouched. All tests remain QEMU-only and IF=0 until separate IRQ routing is validated. See ADR-m9-3d4-mmio-mapping.md.

## M9-3D4 mapping implementation verified in QEMU — 2026-11-12
The earlier M9-3D3 `APIC_MAPPING_UNVERIFIED` observation applies only to the original HHDM/CR3 probe and is superseded as a *mapping capability* limitation by the new, isolated M9-3D4 kernel aperture. The boot CPU now owns 3–4 additional ordinary kernel page-table frames, attaches exactly two supervisor RW/NX/UC-index-3 mappings, and re-walks the active CR3. Tests pass at 128/256/512 MiB, and a forced second-allocation failure rolls back without modifying the root. No LAPIC/IOAPIC register access, IRQ delivery or scheduler was introduced. See ADR-m9-3d4-mmio-mapping.md.

## Approved M9-3D5 read-only APIC probe — 2026-11-12
After private LAPIC/IOAPIC 4KiB UC/NX mappings have passed M9-3D4, M9-3D5 separately verifies effective MMIO memory type, alias safety, MADT APIC identities, LAPIC boot-CPU ID and IOAPIC ID/version/GSI range. Any uncertainty fails closed and retains masked PIC and IF=0. Only I/O APIC read register selection is permitted as a bounded index-latch write; interrupt controller redirection, LAPIC EOI, PIT IRQ0 and STI remain unchanged. See ADR-m9-3d5-apic-readonly.md.

## M9-3D5 read-only APIC hardware proof — 2026-11-12
A read-only x86 PTE alias walker, MTRR/PAT policy check and bounded LAPIC/IOAPIC identity/version probe now run after the isolated M9-3D4 mapping proof. In QEMU 128/256/512 MiB, both known HHDM aliases are absent, cache gate is safe, MADT/CPUID/device identities match and IRQ0's GSI fits the IOAPIC hardware range. The IOAPIC version read uses a selector-latch write and restores it; interrupt configuration is unmodified. All IRQs remain disabled. See ADR-m9-3d5-apic-readonly.md.

## M9-3D6 authorized IRQ0 integration
The first controlled timer interrupt is an internal QEMU-only boot-CPU path: MADT and xAPIC/memory-type proof (M9-3D5), one IOAPIC GSI route for PIT0 to vector 0x30, LAPIC EOI, and kernel_timer 100Hz monotonic ticks. Exceptions and IST1 remain distinct, PIC completely masked and polled keyboard/COM1 plus PIT channel2 F12 preserved. The IF enable is a release-critical test gate, not a generic IRQ ABI. See ADR-m9-3d6-irq0-delivery.md.

## M9-3D6 first validated interrupt timer (QEMU)
The first LAPIC/IOAPIC-delivered PIT0 interrupt now has bounded test-only 100/200-tick evidence, with a nominal 100Hz monotonic software clock via kernel_timer and IRQ0 vector 0x30/LAPIC EOI. The path is boot-CPU-only, source GSI from MADT and actual IOAPIC version, with full existing M9-3D5 cache and ID checks. PIC sources remain masked, keyboard/COM1 polled and F12 PIT2 overlay independent. No scheduler or ring-3 BASIC application is yet present. See ADR-m9-3d6-irq0-delivery.md.

## M10-D3.1 planned boot preparation and system diagnostics — 2026-11-13

All nine workspace identities/metadata are prepared at boot; the physical framebuffer and per-workspace snapshots remain lazily painted/allocated for memory efficiency. An F10 read-only monitor can report only verified diagnostic values. See [ADR-m10-d31-workspaces-debug-hub.md](../decisions/ADR-m10-d31-workspaces-debug-hub.md). D4 priority/supervisor integration remains a separate test milestone.


## M10-D4 diagnostic architecture boundary — 2026-11-13

M10-D4 completion is a distinct QEMU-only boot CPU diagnostic with
four independently stacked ring-0 workers (Background, Normal,
Interactive, Time-sensitive), preemptive IRQ-exit full-frame handoff
and an always-ready supervisor. It incorporates a fixed task-state
owner and bounded single-waiter priority inheritance. A controlled
saved-frame corruption is rejected before recovery. Normal editor
execution remains nonpreemptive; full process scheduling belongs to
M10-D5. See `../decisions/ADR-m10-d4-priority-supervisor-proof.md`.


## M10-D5.2a task cleanup boundary (development only)

A bounded kernel-task-reaper adapter supplements the authoritative M10-A/D5.1 task table without replacing D4's diagnostic controller. Only the permanent dispatcher or supervisor may invoke memory-owner release after verifying task termination, stack range, saved context bounds and an actually observed caller RSP outside that stack. A successful release precedes `kernel_task_reap` and generation-tagged descriptor reuse. The first real-QEMU integration is the **test-only M10-B cooperative switch fixture**. The normal editor remains unpreempted; resource graph cleanup and true production reaper queues are deferred. See `../decisions/ADR-m10-d5-execution-aware-reaper.md`.


## M10-D5.2b bounded ownership interlock (development only)

One static, generation-keyed record per authoritative kernel task slot tracks stack identity, verified saved RSP, and bounded synchronization references. The dispatcher/supervisor refuses to reclaim tasks with remaining references, uses the registered stack binding for the D5.2a off-stack owner release, and quarantines any memory-owner release failure without retry. This is an isolated adapter for the M10-B diagnostic, not a parallel scheduler and not yet integrated into the normal SpiffOS kernel. See `../decisions/ADR-m10-d5-resource-ownership.md`.


## M10-D5.2c supervisor reaper and synchronization reference proof (isolated)

The authoritative M10-A task table and D5.2b generation-keyed resource registry now have a bounded cleanup FIFO that processes only on the external dispatcher or reserved supervisor stack, never in IRQ context. Registered outstanding locks/waiters retain a terminated task in queue; safe candidates retire using their exact registered stack owner; uncertain failures quarantine the task and leave backing unavailable for reuse without retry. A separate one-owner/one-waiter helper proves reference accounting and transfer but does not implement blocking, wake, priority inheritance or production mutex APIs. The M10-B QEMU diagnostic exercises the queue after switching to the boot stack; the normal editor remains unchanged. See `../decisions/ADR-m10-d5-supervisor-cleanup.md`.


## M10-D5.2d authoritative mutex blocking/waking (isolated test-only)

An internal one-owner/one-waiter mutex adapter now operates directly on the sole M10-A lifecycle table, not the separate D4 diagnostic controller. A RUNNING contender is registered as waiting and changes to BLOCKED (removed from CPU current ownership and ready FIFO); the owner transfers registered lock ownership and wakes that exact generation-tagged waiter into RUNNABLE when it releases. The new M10-B **test-only** QEMU fixture demonstrates actual cooperative context-switch and resumption across this one-owner/one-waiter lock handoff on independent stacks. The mutex's policy API itself does not switch stacks, preempt the editor or enable generalized production synchronization. See `../decisions/ADR-m10-d5-mutex-block-wake.md`.


## M10-D5.3 bounded multiwaiter policy and resource accounting (isolated)

A fixed sidecar models four ranked mutexes and up to fifteen ordinary generation-tagged task profiles, always referencing the sole M10-A task table and D5.2b ledger. Lock contention transitions the actual current M10-A task to BLOCKED, retains its registered reference and permits up to fourteen waiters per lock. Unlock transfers ownership to the highest donated effective-priority waiter (FIFO tie), wakes that exact identity, and leaves other waiters BLOCKED. Strictly increasing ranks prohibit lock-order cycles; bounded fixed-point donation handles permitted chains. Dead owner locks are retained as orphans and cannot silently reclaim their backing. This is an isolated hosted synchronization policy; existing M10-B cooperative QEMU handoff proves only the preceding one-waiter mutex, and normal SpiffOS remains FIFO/nonpreemptive. See `../decisions/ADR-m10-d53-bounded-synchronization.md`.


## M10-D5.4 single-authority priority selection and multiwaiter execution proof (isolated)

The M10-A `kernel_task_dispatch_selected` validates and transitions an exact generation-tagged RUNNABLE task using the *existing* lifecycle queue; this is the only task-state mutator. The D5.4 selection adapter reads the M10-A ready list and D5.3 registered lock graph to compute effective priorities and bounded ready ages, chooses a candidate using the already-tested D1 priority policy, then asks M10-A to dispatch it. A runnable supervisor in reserved slot 15 is guaranteed a selection opportunity at least every four dispatches (not a measured runtime deadline). The test-only three-stack QEMU fixture verifies a low owner legitimately blocked on an external event, two separately stacked higher-priority mutex waiters, owner wake with donation, successive lock transfer and actual saved-context resumption; a surviving boot dispatcher reclaims task stacks. No default editor preemption or normal-kernel linkage. See `../decisions/ADR-m10-d54-priority-dispatch.md`.


## M10-D5.5 nonmutating execution safety gate (diagnostic only)

D5.5 adds a test-only freestanding safety-admission module independent of both D4's diagnostic controller and M10-A's authoritative task manager. It consumes **caller-supplied evidence** of execution origin, IRQ depth, IF flag, preempt/critical nesting, exact generation-tagged running task, observed stack pointer and registered stack ownership. It rejects an IRQ switch with unresolved ownership or waiter refs, non-IRQ synchronization attempted during IRQ, and cleanup not on an independent dispatcher/supervisor stack. It never switches CPU contexts or frees resources. A bounded monotonic supervisor monitor latches an unrecoverable diagnostic failure if no verified service completion occurs within the configured interval, if time rolls back or if the clock disappears. A dispatch opportunity is not service completion, and the policy does not establish a hard realtime guarantee. The module is **not linked into the normal SpiffOS editor**; a trusted x86 evidence collection adapter and real interrupt-entry integration remain open. See `../decisions/ADR-m10-d55-preemption-safety.md`.


## M10-D5.5b real x86 IRQ frame observation (isolated)

A new test-only `SPIFFOS_M10_D55B_IRQ_EVIDENCE_TEST` flag attaches an additional read-only C hook to the existing `x86-irq0.S` saved-frame pointer after its normal tick/EOI. Reusing the independently compiled D2 assembly sentinel verifies actual CPU-saved 20-qword long-mode frame, canonical RIP/RSP under four-level QEMU paging, matching pre-interrupt stack/CS/SS/RFLAGS, 15 GPR preservation and live interrupt-callback IF=0. A wrong *expected* RSP is rejected without mutating the real frame. The boot test checks the result after returning to boot stack; the editor and ordinary IRQ0 path remain unchanged. Not yet an authoritative current-task or non-yielding supervisor CPU budget proof. See `../decisions/ADR-m10-d55b-x86-evidence.md`.


## M10-D5.5c exact M10-A task frame verification + diagnostic hardware tick service

A bounded, read-only `m10-d55c-frame-owner.[ch]` validator tests that an immutable 20-qword IRQ frame lies wholly within the registered D5.2b stack of the **current** exact-generation M10-A RUNNING ordinary task and that saved RSP/RIP/segments/flags and interrupt/critical evidence satisfy safe constraints. The validator never changes a task state or independently authenticates the source of a caller-provided frame; a separate test-only x86 IRQ0 hook now supplies an authentic hardware frame while **one real M10-A-owned worker** is RUNNING, with exact handle/stack validation and safe off-stack reclamation. The stale-generation negative is refused before editor entry. A hardware-tick supervisor monitor latches time rollback or an actual missed service deadline, and resets only when supervisor work truly completes. An isolated flag on the earlier D4 **separate diagnostic** full-IRQ non-yielding-worker test checks service completion versus IRQ ticks; this is NOT a general M10-A IRQ dispatch integration, CPU-time hard realtime guarantee, or ordinary editor preemption. See `../decisions/ADR-m10-d55c-task-frame-budget.md`.
