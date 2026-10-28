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
| A-010 | Keyboard | Ctrl+Alt+1–9 workspace; Ctrl+Alt+0 overview; Ctrl+Alt+Delete privileged supervisor |
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
