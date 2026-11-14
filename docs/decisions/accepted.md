# Accepted Decisions Register — v0.1
Date: 2026-10-20. Status: documentation draft for owner review; entries below reflect the owner's supplied accepted decisions, not approval of new implementation details.

| ID | Area | Accepted decision |
|---|---|---|
| A-001 | Mission | New native x86-64 OS; not Linux, emulator or compatibility layer |
| A-002 | UX | Rapid boot directly into interactive BASIC; no conventional desktop |
| A-003 | Boot | UEFI-based startup; do not depend indefinitely on UEFI boot services |
| A-004 | Testing | QEMU on Forge first; dedicated laptop for physical validation |
| A-005 | Protection | Kernel ring 0, BASIC user code ring 3; isolate kernel and program state |
| A-006 | Scheduling | Timer-driven preemptive multitasking with Background, Normal, Interactive, Time-sensitive priorities |
| A-007 | IPC | One kernel Message Manager; direct single-consumer process inboxes plus named fan-out subscriptions for future events (ADR-007) |
| A-008 | Events | Polling and event-driven handlers share messaging; callbacks run at safe interpreter points without unbounded recursion |
| A-009 | Workspaces | Nine independent BASIC virtual workspaces; tasks continue in background |
| A-010 (partially superseded by M8) | Keyboard | F1–F9 workspace; F10 system screen; F12 workspace indicator. Ctrl+Alt+Delete remains a reserved privileged supervisor chord. |
| A-011 | Display | Foreground workspace alone controls physical display; background virtual buffers retained |
| A-012 | Audio | Foreground workspace audible; background playback continues muted |
| A-013 | Supervisor | Reserved-resource task control, suspend/resume/kill/shutdown/restart; not dependent on responsive BASIC |
| A-014 | BASIC | Modern structured BASIC without required line numbers, with SUB/FUNCTION/local variables |
| A-015 | Compatibility | No full C64 machine code, VIC-II or SID compatibility |
| A-016 | Editor | Screen-position command entry; EDIT upper source/lower immediate console, Tab focus, EXIT returns |
| A-017 | Editor UX | Active pane distinct by color and explicit cue; customizable themes; Turbo Pascal visual inspiration only |
| A-018 | Graphics | Hardware-independent logical resolution and documented safe low-level graphics interface |
| A-019 | Audio APIs | Simultaneous music and sound effects from BASIC |
| A-020 | Hardware | Modular support for video, sound, input, storage and network devices |
| A-021 | Storage | Directories and power-loss-resistant boot/storage design; no unsaved-data survival guarantee |
| A-022 | Network | Wired TCP/IP first; BASIC client/server network application capability; Wi-Fi later |
| A-023 | Governance | Documentation and approval before code; AI implementation; PR review; no unauthorized merge/deploy |

| A-024 | IPC architecture | One shared kernel queue/delivery engine for direct messages, request/reply and published events; no competing IPC engines (ADR-007) |\n| A-025 | Ownership | One authoritative owner for each stateful subsystem, accessed through documented interfaces; direct internal calls allowed where appropriate (ADR-007) |\n\n| A-026 | Boot compatibility | Versioned SpiffFS format; no silent incompatible migrations; verify safe A/B rollback (ADR-008) |\n| A-027 | File concurrency | Exclusive update locks, atomic transactions and stale-update conflict detection (ADR-008) |\n| A-028 | Workspace focus | One foreground application per workspace; Workspace Manager controls focus and composition (ADR-008) |\n| A-029 | Supervisor safety | Reserved CPU/memory/input/display recovery path independent of BASIC responsiveness (ADR-008) |\n\n## Clarifications requiring approval
The four priorities are conceptual, not an approved algorithm. Workspace count does not establish a task-count limit. Virtual PEEK/POKE addressing is a recommendation, not a finalized mapping. C as implementation language remains PROPOSED. Bounded queue/drop-oldest policy remains PROPOSED. Turbo Pascal menu organization is OPEN. The preliminary development sequence is not a dependency-resolved plan.

## Change control
Add an ADR with motivation, options, consequences, validation and owner approval before replacing any accepted decision. Record source and approval date for subsequent entries.

## Owner approvals — 2026-10-26 (ADR-011)

| ID | Area | Accepted decision and rationale |
|---|---|---|
| A-030 | Bootloader | UEFI + Limine initially; avoids custom bootloader work before first kernel; QEMU first, Anvilone USB later. Legacy BIOS and signed Secure Boot deferred. |
| A-031 | Kernel language | Modular monolithic C kernel, minimal assembly; simplest comprehensible driver/module organization. |
| A-032 | User boundary | Ring 0 kernel, ring 3 applications; BASIC interpreter and optional EDIT editor are independent user programs. |
| A-033 | Memory | 64-bit paging, process address spaces, RAM-only/no swap, protected kernel/supervisor reserve; recoverable OOM and no automatic OOM process killing. |
| A-034 | Source readability | Verbose file/function contracts and explanatory inline comments, written for developers returning to C. |
| A-035 | Agent governance | Root AGENTS.md must be explicitly read at the beginning of each AI coding task; Developer's Guide maintained with implementation. |
| A-036 | Hardware validation | QEMU primary test environment; HP Anvilone UEFI laptop first physical target using USB, preserving Mint. |

These entries record explicit owner approval; see [ADR-011](ADR-011-boot-kernel-memory-governance.md) for context, alternatives, consequences and limitations. **Documentation approval does not grant implementation, merge or physical deployment authorization.**

### Historical conflicts to reconcile
- A-016 and older overview text describe a permanently open editor-like environment. The later approved model is BASIC READY first, optional EDIT launched as a separate program. A-016 is superseded in this respect by A-032; detailed editor UX remains deferred.
- A-012 states background sound is muted. Later approved background playback/mixer behavior conflicts with that policy. Mark A-012 **superseded in principle**, pending review of ADR-010 and precise mixer requirements; do not implement the old mute policy by default.
- Historical storage retention/versioning proposals may differ from later manual-trash discussions. Do not assume either is final without owner review.

## Decision precedence policy — 2026-10-26

**A-037 (accepted):** The latest explicitly owner-approved decision supersedes conflicting older decisions. Retain older rationale and mark it superseded; update relevant architecture documents. For material conflicts in major subsystems, review alternatives and obtain an explicit official direction before implementation. Suggestions are not approvals; minor UX choices can be deferred.

**Audio reconciliation:** A-012's foreground-only audible-output rule is superseded. ADR-010 already documents background music continuing across workspace switches, a single system-wide mixer, and per-workspace volume/mute. The later approval aligns with ADR-010. Global voice limits and exact audio resource budgets remain open.

**A-038 (accepted 2026-10-26):** SpiffFS keeps only the current saved file version; deleted files go to manually managed Trash until explicitly emptied. No automatic file version history, Trash expiration or silent purge. Crash consistency and power-loss-resistant boot remain mandatory and independent of Trash. See [ADR-012](ADR-012-spifffs-trash-policy.md). Earlier automatic retention/versioning requirements are superseded where conflicting.

## Scoped first-boot authorization — 2026-10-28

The owner explicitly authorized direct ChatGPT-guided recovery, VS Code setup,
and a QEMU-only first boot on `feat/first-qemu-boot`, without Qwen or a separate
Codex coding workflow. This exception applies only to this boot issue; the
historical model policy and unrelated governance are retained.

Use the existing official Limine C template at
`40dcbdf9602a0255b1fd20e148c6343476df17c3` as the reference. Pin the protocol to
`3a0526b700e356f0eac1b71a77697b3fd1c707a3` (base revision 6), and Limine binaries
to `7a9013f305de0bea1f6310e76e7baba30499fef0` (v10.8.5-binary).
For this smoke-test milestone, use the template's higher-half ELF layout and
C `kmain` entry on Limine's supplied stack. The recovered `start.S` remains
preserved but is excluded from the build: its fixed physical stack is unsafe.
This does not settle future memory layout, syscall ABI or runtime architecture.

Validation requires a strict freestanding Clang build, retained Limine request
markers, a UEFI ISO booted with OVMF and a writable local variables copy, and
captured serial output containing the exact line `SPIFFOS_BOOT_OK`. Compilation
alone is insufficient. BASIC, ring 3, interrupts, allocators, storage recovery,
A/B slots and physical hardware validation remain unimplemented.

No merge, push, physical disk, USB, or infrastructure configuration changes are
authorized. Recovery and exact validation evidence are recorded in
`docs/operations/first-boot-2026-10-28.md`.

### Validated protocol compatibility correction — 2026-10-28

The first QEMU attempt reached `kmain` but rejected requested base revision 6.
GDB at `kmain` read the official base tag as `{magic, 5, 6}`: the pinned Limine
v10.8.5 binary loaded revision 5 and left the unsupported request unchanged.
The newer template requests revision 6. Keep both dependency commit pins and
request base revision **5**, explicitly validated with this binary. This
supersedes the initial revision-6 choice above for this smoke-test milestone.
The current upstream specification deprecates revisions below 6; upgrading the
bootloader/protocol pair is future work. No newer-revision state is assumed.

## Framebuffer smoke-test authorization — 2026-10-28

After reviewing first-boot commit `be74fe2`, the owner authorized the next small
step: request a Limine framebuffer, validate its format and draw a color pattern
in QEMU. Direct ChatGPT-guided work continues for this step; no Qwen or separate
Codex coding workflow is invoked. This does not replace unrelated governance.

Use only the first provided framebuffer, with 32-bit RGB pixels and three
non-overlapping 8-bit color fields. Honor reported pitch (bytes per row), check
alignment and arithmetic bounds, and leave padding bytes untouched. Draw red,
green and blue bars inside a 16-pixel purple border. If framebuffer metadata is
missing/unsupported, report it on serial and preserve the serial boot path.
The existing SPIFFOS_BOOT_OK marker remains; SPIFFOS_FRAMEBUFFER_OK means the
pattern writes completed. Screenshot verification is required for visible output.

This temporary kernel boot diagnostic does not decide logical display modes,
Graphics Manager/Workspace Manager interfaces, font rendering or BASIC APIs
from ADR-009. Native video drivers, multiple displays and user graphics remain
open. Validate strict compilation, padded-row/guard tests, serial regression and
an actual QEMU framebuffer capture. No physical devices, push or merge.

## Fixed bitmap boot label — owner authorization 2026-10-28

The owner authorized the next small step after framebuffer commit `8f48fa7`:
render the fixed word SPIFFOS using bitmap glyphs. Continue direct ChatGPT-guided
work for this step without delegated coding models; unrelated governance stays
unchanged. This is a boot diagnostic, not a finalized graphics or text ABI.

Use original 8x8 glyph bitmaps, scaled 4x, white on an opaque black 224x32-pixel
panel centered horizontally at y=48, over the existing pattern. Require at least
256x96 pixels to preserve the 16-pixel border; reject smaller/unsupported buffers
without title writes. Honor framebuffer pitch and existing format validation.
Emit SPIFFOS_TEXT_OK only after title drawing succeeds; retain existing serial
markers and the missing-framebuffer path. No cursor, scrolling, keyboard or BASIC.

Validation: strict kernel build, padded-row/guard tests, exact-fit and undersized
buffer cases, screenshot comparison including the entire label, serial marker
and visual inspection of an actual QEMU capture. Keep original staging/drafts,
verify a pre-edit snapshot, and leave code uncommitted for review. No push,
merge, physical-device or infrastructure changes. See the step's implementation
and validation record in docs/operations/text-2026-10-28.md.

## Small boot text console — owner authorization 2026-10-28

After saving fixed-label milestone `25150f0`, the owner authorized printing
different characters and advancing a logical cursor. Continue direct
ChatGPT-guided work without delegated coding models. Unrelated governance
remains unchanged. This is a kernel boot diagnostic, not a stable console ABI.

Use caller-owned state and borrowed Limine mappings, with no heap. Original
5x7 uppercase/digit glyphs occupy 8x8 cells at scale 2 (16x16 screen pixels).
Support space, period, colon and question mark; unsupported bytes render '?'.
Newline sets column zero and advances the row. Printable characters advance
and wrap immediately after the final column. A cursor row equal to the row
count means full: later input returns false without writes. String output may
be partial on error. No scrolling, visible/blinking cursor, input or BASIC.

Keep the bars and fixed title; show demonstration text in a dark-blue panel
at x=32,y=112, using whole 16x16 cells and a 32-pixel right/bottom margin.
Rectangle/cell operations must validate layout and bounds before writing.
Emit SPIFFOS_CONSOLE_OK only after the demonstration prints completely.
Retain prior markers and serial diagnostics if screen output is unavailable.

Validate guarded padded RAM, wrapping/newlines/full-panel behavior, unsupported
characters, strict kernel build, actual QEMU capture and serial success. Verify
a backup before source changes, preserve current staging/drafts, and leave the
new step uncommitted for review. No push, merge or physical-device changes.
See docs/operations/console-2026-10-28.md for actual results and limitations.

## Scrolling and existing power-up logo — owner authorization 2026-10-28

The owner authorized scrolling after checkpoint `711168c` and explicitly asked
that the existing SpiffOS logo be used for the power-up screen. Direct
ChatGPT-guided work continues; no delegated coding models. Preserve unrelated
governance, staging and drafts; verify backup, test each change separately,
and leave new work uncommitted for review. No push, merge or physical devices.

Scrolling supersedes the bounded-console full-error behavior: row == rows now
means a pending scroll. Before consuming the next byte, move the console
viewport up one 16-pixel character row, clear the bottom row and put the cursor
on that row. Keep the final character visible until subsequent input needs
space. Existing immediate wrapping/newline rules stay: a newline following a
wrap creates an additional blank line. No history, input or visible cursor.

Use only the original assets/branding/spiffos-logo.svg, retaining it unchanged.
Produce a 400x160 RGB bitmap with recorded provenance and checksums; ordinary
builds embed that fixed bitmap without image/font libraries in the kernel.
Center the logo at y=24, outside the console. Use a navy power-up background
inside the existing purple border and move the console top to y=216. Retain
prior framebuffer/text diagnostics but replace the temporary fixed title in
the normal power-up presentation. Do not introduce a splash-screen delay.

Validate overlapping scroll copies, clear/guard/padding behavior, one-row
consoles, repeated wraps/newlines, image bounds and truncated buffers, faithful
logo conversion, strict build and actual QEMU captures. Print enough numbered
lines to force scrolling and verify final visible rows plus untouched logo.
Record results in docs/operations/scroll-logo-2026-10-28.md. This is still a
kernel diagnostic; BASIC, ring 3, keyboard and physical hardware remain later.

## Logo is initial workspace content — owner correction 2026-10-28

The owner explicitly corrected the previous fixed-header treatment: once the
logo scrolls off the workspace, it must not return until workspace reset or
power-on. This supersedes the fixed logo/header and permanently reserved top
area in the earlier scrolling-logo section; retain that section as history.

The whole drawable interior inside the 16-pixel border becomes one console
viewport. Original logo appears initially at y=24, and text initially starts
on row 12 of the full grid (y=208). No header rows remain reserved. Scrolling
moves logo and text together; exposed rows use the common navy workspace
background. All character cells, including former logo rows, can hold text.

An explicit boot_workspace_reset() initializes the full viewport, draws the
original logo and places the cursor below it. Scrolling and cursor positioning
never call logo rendering. This is a single boot-workspace helper, not the nine
workspace manager or a user input command. A reset key/command remains pending.
Normal boot shows the initial logo and sample text without forcing it away.
A separate compile-time test variant forces scrolling; no delays are added.

Validate initial screen, partial movement, complete logo removal, repeated
scrolls without reappearance, usable former header cells and restoration only
by explicit reset. Preserve all current uncommitted work, staging and drafts.
Record evidence in docs/operations/workspace-logo-2026-10-28.md. No push, merge,
physical-device changes or delegated coding models.

### Owner refinement: uniform full-screen background — 2026-10-28

The owner requested one consistent screen background. Use RGB(9,27,53)
throughout, remove the purple border, and let the console use the whole
cell-aligned display (origin 0,0). The initial cursor is row 13 (y=208).
For the boot bitmap only, flatten the SVG's backing rectangle to this same
navy; preserve the original source SVG and foreground artwork unchanged.
This supersedes the prior border/inset and gradient-backed boot presentation.

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

## PR #14 merge and local QEMU deployment — 2026-10-29

The owner explicitly authorized merge and deployment after reviewing the tested
launcher and stored logical-line checkpoint. PR #14 exact head
`9e40e5af09836855508aa39df615ca4c955d719d` merged as
`4a06c61f86a5004cb532fdde5096d684e77b265b`. Deployment means the rebuilt
QEMU development ISO in the separate merged-main checkout. Post-merge strict
build, host sanitizer tests, actual QEMU input/full-frame checks, forced-scroll
workspace test and safe-launcher cleanup test all passed. Manual GTK resizing,
focus, Ctrl+Alt+G and close-button observation remain pending and are not claimed.
No physical disk, USB, firmware, network or infrastructure change was authorized
or performed.

## PR #16 merge and local QEMU deployment — 2026-10-29

After reviewing the cursor/screen-editing checkpoint, the owner explicitly
authorized merge and deployment. Tested head
df5f966f3d0be182ad4d166b61441611461f18a8 merged through PR #16 as
e63b04222d04060753949b849f1dfd2e3001f60e. The separate merged-main checkout
was fast-forwarded and its local QEMU ISO rebuilt. Post-merge host sanitizer,
real QEMU input, five full-frame cursor/reflow captures, workspace/forced-scroll
and safe-launcher tests all passed. Manual GTK observations remain pending.
See [deployment evidence](../operations/screen-editor-2026-10-29.md) for
commands, image hashes, limitations and rollback. Deployment does not extend
to physical hardware, USB, firmware, network or infrastructure.

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

M2 implementation and verified limits are recorded in
[frame allocator validation](../operations/frame-allocator-2026-10-29.md).

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

## Keyboard completeness — owner report 2026-11-09
Implement US punctuation, forward Delete and numeric keypad input while retaining
uppercase text and existing cursor/reflow semantics. See ADR-keyboard-completeness.md.
Prepared for review; no new merge or physical deployment authority inferred.

## M7 workspace switching — owner authorization 2026-11-10

The owner authorized implementation, testing, merge and deployment of Ctrl+Alt+1..9 independent text workspaces, subject to passing validation. Design and constraints: [M7 ADR](ADR-m7-text-workspace-switching.md). This does not approve BASIC execution or physical disk installation.

- M7 host keyboard mitigation (2026-11-10): accept F1..F9 as alternative workspace selectors because interactive Forge Ctrl+Alt+digits are intercepted before reaching the guest; preserve Ctrl+Alt+digits for compatible hosts. See ADR-m7-text-workspace-switching.md.

- M8 function-key direction (2026-11-10): F1..F9 exclusively select user workspaces, F10 opens a dedicated system screen, F12 temporarily shows a centered workspace ID; supersedes A-010 Ctrl+Alt workspace shortcuts and Ctrl+Alt+0 overview. See ADR-m8-function-keys-and-workspace-indicator.md.

## M9 incremental kernel scope — owner authorization 2026-11-11
The owner authorized implementation, tests, review, merge and safe Forge QEMU ISO deployment under the M9 prompt. ADR-m9-kernel-foundation.md records the staged implementation boundaries and explicitly preserves prior stable interfaces. Approval does not settle final syscall, timer or driver ABIs, or permit physical media changes.

## M9 issue #34 staged boot-memory remediation — 2026-11-11
The owner authorized continuing issue #34 through implementation, testing and safe local deployment after confirming M8 behavior. ADR-m9-128m-boot-memory.md records the internal lazy workspace-paging choice, the safety invariants and compact 8-bit frame-owner metadata preserving the prior 16 GiB candidate-frame cap. The long-term dynamic memory and driver ABIs remain open.

## M9-2 staged x86-64 exception-only IDT — 2026-11-11
The owner authorized the next M9 checkpoint after confirming post-boot snapshot
workspace operation. Implement a static exception-only IDT, preserve IRQ-off
polling and PIT F12, and require direct QEMU fault-injection proof before merge.
ADR-m9-exception-idt.md records the internal scope; no IRQ controller or
task/scheduling ABI is approved.

## M9-3A owner-authorized kernel exception stack (2026-11-11)

The M9-3 phase authorizes a boot-CPU TSS, IST and double-fault emergency stack with separate verification before interrupts are enabled. Internal implementation choice and limits are recorded in [ADR-m9-3-exception-stacks](ADR-m9-3-exception-stacks.md). This does not approve an external hardware interrupt ABI or physical deployment.

## M9-3B controller checkpoint (2026-11-11)

Owner authorization for M9-3 includes choosing the simplest safe interrupt-controller foundation. QEMU uses a fully masked dual 8259 PIC remapped to vectors 32–47; IF remains zero. APIC and physical hardware topology remain deferred. See [ADR-m9-3-pic-foundation](ADR-m9-3-pic-foundation.md).

## M9-3C monotonic internal timer (2026-11-11)

M9-3 Phase C establishes an internal saturating 64-bit tick service with explicit uninitialized/unavailable state, 10 ms intended resolution and no wall-clock dependence. PIT0 IRQ0 becomes its source only after Phase D validation; PIT2 F12 is unchanged. See [ADR-m9-3-monotonic-clock](ADR-m9-3-monotonic-clock.md).

## M9-3D approved APIC-first direction (2026-11-11)
The owner explicitly selected ACPI/MADT-based LAPIC + IOAPIC discovery and routing for first controlled PIT0 IRQ0 delivery, with legacy PIC fallback. Draft PIC IRQ0 delivery failed in q35; it is superseded as the primary route, not merged. See [ADR-m9-3-apic-routing](ADR-m9-3-apic-routing.md). The approval is subject to read-only discovery, staged MMIO/IRQ tests, and no physical hardware changes.

## M9-3D4 owner approval — 2026-11-12
The owner approved the dedicated kernel-owned, uncached, two-page APIC MMIO virtual mapping foundation in Issue #45, following completed M9-3D1/D2/D3. See [ADR-m9-3d4-mmio-mapping](ADR-m9-3d4-mmio-mapping.md). This is limited to tightly controlled page-table ownership, rollback and QEMU validation; it does not authorize unsafe direct physical dereferencing, arbitrary device MMIO or unverified interrupt enabling.

## M9-3D5 owner authorization — 2026-11-12
Owner approved read-only APIC register and cache/alias validation and authorized merge/safe Forge QEMU deployment **only after** regression gates; no IRQ0 routing, controller configuration, physical laptop changes or STI. The IOAPIC version read requires a bounded write to its register-index selector (not redirection or mask configuration); this must be reported transparently and cannot be treated as a read with zero MMIO writes. See [ADR-m9-3d5-apic-readonly](ADR-m9-3d5-apic-readonly.md).

## M9-3D6 owner approval — 2026-11-12
Owner authorized implementation, validation-gated merge and safe QEMU-only deployment of controlled boot-CPU APIC IRQ0 and PIT0 ~100-Hz monotonic ticks under [ADR-m9-3d6-irq0-delivery](ADR-m9-3d6-irq0-delivery.md) and Issue #51. Combined user interactive acceptance of M9-3D5 (#48) and M9-3D6 (#51) is permitted after all automated regression and QEMU hardware tests pass. Physical installation, new public timer/IRQ ABI, keyboard IRQ and unsupported ACPI routes remain unapproved.


## M10 task scheduler — owner decision 2026-11-12

**A-039 (approved direction, scope-limited):** Implement M10-A task metadata/state/round-robin hosted tests, M10-B cooperative kernel context switching, and M10-C 100 Hz timer accounting/reschedule requests in that order. Preserve existing working kernel/keyboard/workspace features and enforce independent testing gates. One kernel execution context per task and a single-CPU scheduler are the initial design; no public process ABI is implied. See [M10 ADR](ADR-m10-task-scheduler-foundation.md).

**A-040 (explicit separate acceptance):** M10-D hardware timer-driven preemption is NOT authorized by M10-A/B/C approval and requires a separate architectural and acceptance decision. A-006's eventual preemptive/four-priority design remains an accepted long-term requirement, not completed M10-C behavior. Merge, deploy, physical tests and network changes are not granted by this architecture approval.


## M10-D architecture and D3 release authorization — 2026-11-13

The owner explicitly approved the M10-D staged design and isolated implementation of D1–D5. After separately accepting the exact-source D2 evening suite, the owner specifically authorized proceeding with **M10-D3, merging and deploying when ready**. This supersedes the older no-merge gate in the M10-D proposed ADR **for a fully tested M10-D3 QEMU-only release only**. The default kernel must keep preemption disabled outside the explicit test build; the physical disk, USB, network and general-purpose preemption remain outside scope. Require all three QEMU memory sizes, corrupt-frame/IRQ-disabled negatives, full M9/M10 regression tests, precise source/ISO checksums, preserved owner rollback and documented change history before considering release complete. D4 prioritization and D5 production-preemption acceptance remain separate milestones.

## M10-D3.1 workspace initialization and diagnostics approval — 2026-11-13

After accepting D3, the owner approved boot-time logical preparation of all nine workspaces and read-only debugging tools before proceeding to D4. Preserve lazy framebuffer snapshots and the working keyboard/editor and F1–F12 features. See [ADR-m10-d31-workspaces-debug-hub.md](ADR-m10-d31-workspaces-debug-hub.md). This is implementation approval, not automatic release approval.


## M10-D4 completion implementation authorization — 2026-11-13

The owner explicitly requested completing D4 after the passing D4
checkpoint. The approved **implementation** scope is a test-only
four-priority ring-0 IRQ scheduler with genuine worker block/wake
state, measured supervisor dispatch bounds, one-waiter priority
inheritance and independently validated task-frame quarantine/recovery.
This authorizes D4 coding and QEMU regression work, not an automatic
merge, physical install or production-editor preemption. See
`ADR-m10-d4-priority-supervisor-proof.md` and
`../operations/m10-d4-completion-20261113.md`.


## M10-D5.2a isolated execution-reaper proof — owner direction 2026-11-13

The owner elected to proceed with an execution-aware cleanup boundary after D5.1. This authorizes **isolated implementation/testing only** of a supervisor/dispatcher-only reaper adapter and M10-B real-QEMU proof. It does not grant automatic merge, deployment, Ring 3 support, general IRQ scheduler activation or physical hardware boot. Keep the prior D5.1 descriptor lifecycle, D4 diagnostic isolation and supervisor reserve intact. See [ADR-m10-d5-execution-aware-reaper](ADR-m10-d5-execution-aware-reaper.md).


## M10-D5.2b owner direction — isolated resource ownership, 2026-11-13

The owner chose the next D5.2b checkpoint after D5.2a. Implement a bounded generation-tagged task resource registry and fail-closed cleanup gate, with tested lock/wait reference interlocks and quarantine after memory-owner failure. This is **development/testing authorization only**, stacked on PRs #71/#72; it does not authorize merge, deployment, general editor preemption, new process ABI or physical disk changes. See [D5.2b ADR](ADR-m10-d5-resource-ownership.md).


## M10-D5.2c owner direction — bounded supervisor cleanup proof (2026-11-13)

Owner selected isolated implementation of the next M10-D5 cleanup checkpoint. Add a fixed-capacity supervisor-only FIFO of terminated generation-tagged tasks and verify a one-owner/one-waiter synchronization reference proof against the D5.2b registry. Keep D4 diagnostics separate; production mutex/wait integration and general editor preemption are explicitly **not** approved. No merge, accepted ISO deployment or physical media is authorized. See [D5.2c ADR](ADR-m10-d5-supervisor-cleanup.md).


## M10-D5.2d owner direction — authoritative mutex blocking and waking (2026-11-13)

The owner elected D5.2d isolated implementation of a small one-owner/one-waiter mutex that connects the M10-A lifecycle's actual RUNNING→BLOCKED→RUNNABLE state changes to D5.2b's generation-tagged lock/wait reference registry. No second scheduler, IRQ mutex operations, generic lock ABI, normal-editor preemption, merge or deployment is authorized. The cooperative dispatcher must still perform a real CPU context switch after a blocked task returns. See [D5.2d ADR](ADR-m10-d5-mutex-block-wake.md).


## M10-D5.3 owner direction — bounded multiwaiter synchronization (2026-11-13)

Owner approved continuing after D5.2d with isolated implementation/testing of bounded ranked locks, multiple waiter registration, transitive priority donation, explicit cancellation and fail-closed orphan detection. The M10-A task table remains the sole lifecycle/dispatch authority; a D5.3 sidecar may calculate effective priority but may **not** turn on general editor IRQ preemption or become another scheduler. No merge, deployment, physical disk or public ABI authorized. See [D5.3 ADR](ADR-m10-d53-bounded-synchronization.md).


## M10-D5.4 owner direction — authoritative priority dispatch and real multiwaiter QEMU (2026-11-13)

The owner elected to implement D5.4 after D5.3: effective-priority selection must feed the **single authoritative M10-A task dispatcher**, preserve the supervisor's reserved slot and bounded dispatch opportunities, and prove genuine multiwaiter CPU-context handoffs in disposable QEMU. This authorizes isolated development/testing only, not normal-editor IRQ preemption, merge, accepted ISO replacement or physical deployment. No parallel lifecycle scheduler; D4 remains diagnostic. See [D5.4 ADR](ADR-m10-d54-priority-dispatch.md).
