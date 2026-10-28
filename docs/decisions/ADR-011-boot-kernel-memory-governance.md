# ADR-011 — Initial boot, kernel, memory and documentation governance

Status: **ACCEPTED** by project owner in conversation, 2026-10-26. Implementation is **not** authorized by this record alone.

## Context
SpiffOS aims for native x86-64, fast BASIC startup and code a single returning C programmer can understand. The initial physical test laptop is HP Anvilone (17-bs0xx), UEFI-booted Linux Mint, Intel i5-7200U, 16 GB RAM, Intel HD 620, SATA disk. Its Secure Boot is enabled. QEMU on Forge is the primary debugging environment.

## Decision 1 — Boot
Use **Limine with UEFI** for the initial kernel, rather than writing a custom bootloader or prioritizing legacy BIOS. Develop/test in QEMU first; later boot Anvilone from dedicated USB without modifying its Mint partitions. For unsigned physical testing, disable Secure Boot when needed; consider signing later. Keep the kernel sufficiently loader-independent to allow a different loader in the future.

**Alternatives:** custom UEFI loader (more work before first kernel), GRUB (possible, not selected), legacy BIOS first (unnecessary for Anvilone), signed Secure Boot from day one (additional setup).
**Consequences:** dependency on Limine in early releases; boot protocol and kernel image contracts require documentation; no claim of Secure Boot compatibility.

## Decision 2 — Kernel
Use a **modular monolithic kernel**, primarily C with minimal, well-documented x86-64 assembly. Ring 0 hosts kernel/driver services; ring 3 hosts protected user programs. The BASIC interpreter and optional EDIT editor are separate user-space applications, not kernel components. Drivers must be isolated by documented, evolvable interfaces; exact ABI and module loading policy remain open.

**Alternatives:** microkernel (more IPC and startup complexity), monolithic unstructured kernel (harder to extend), BASIC embedded in kernel (unnecessary coupling).
**Consequences:** kernel drivers can affect whole-system stability; test and comment carefully.

## Decision 3 — Memory
Use **64-bit virtual memory and paging from the beginning**, separate process address spaces, RAM only with **no swap**, and a protected emergency reserve for kernel/supervisor responsiveness. User allocation failure returns a recoverable error; do not automatically kill programs merely for exhausting memory. Workspaces draw from available RAM, subject to later policy.

**Alternatives:** no process isolation (simpler initially but unsafe), swap (complexity and storage writes), automatic out-of-memory kills (violates interactive recovery goal).
**Consequences:** early paging/allocator complexity; allocator algorithms, reserve sizing, address layout, quotas and syscall contract remain OPEN. Reserve must be validated under stress.

## Decision 4 — Documentation and AI coding
Keep **documentation first**. Root `AGENTS.md` is mandatory reading at the start of each AI coding task; prompts must explicitly request this. Provide `docs/DEVELOPERS_GUIDE.md` for programmers returning to C. Require comprehensive file/function headers and explanatory inline comments for pointer, memory, bit, hardware and assembly logic. Maintain decisions with reasons and history, not just outcomes. PR review and owner approval are required before merges or deployment absent separate authorization.

**Alternatives:** rely on chat history or terse code comments (risk of losing context and inaccessible source).
**Consequences:** additional documentation/review work; easier future maintenance.

## Reconciliation with older documents
- The accepted/open registers and system overview predate these approvals. Where they say C or bootloader is merely proposed, this ADR supersedes that status.
- Earlier editor descriptions that imply an always-open IDE are superseded: **boot to a BASIC READY prompt; EDIT is optional and separately launched**.
- Earlier documents that say background audio is muted conflict with the more recent user-approved background music/mixer behavior. Flag for focused reconciliation; do not silently alter old audio ADRs without reviewing them.
- Older storage retention/versioning drafts conflict with later discussion of manually managed trash. Storage policy remains unresolved until reviewed.
- Prior conceptual ring 1/2 language is not a commitment to x86 hardware privilege rings 1/2; implementation targets ring 0 and ring 3.

## Validation and next decisions
Demonstrate QEMU UEFI boot, serial debug, kernel entry and protected user memory; later USB boot on Anvilone. Next architecture topics: syscall ABI, page-table layout, physical/virtual allocators, emergency reserve and driver contracts. No physical disk repartitioning is authorized.

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
