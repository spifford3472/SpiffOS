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
