# SpiffOS — Mandatory AI Agent Instructions

Read this entire file at the start of **every** AI development task, even if the task appears trivial. Then read `docs/decisions/accepted.md`, `docs/decisions/open.md`, `docs/architecture/system-overview.md`, and any relevant ADR or subsystem reference **before editing**. In every coding prompt explicitly instruct the agent to read these files; do not assume an agent tool discovers this file automatically.

## Mission
Build a small, understandable, native x86-64 operating system that boots directly to a BASIC READY prompt. Not Linux, not an emulator. Prefer clarity, fast boot, modularity and resilience over cleverness. BASIC and EDIT are user-space programs, not kernel internals.

## Decision authority
- The owner approves architecture and implementation authorization. A discussion, draft, example, or PR does not silently authorize implementation, merge, deployment or destructive disk changes.
- **Documentation first.** Before an architecture-changing implementation, update the accepted/open registers, relevant ADRs, architecture reference, and validation expectations in a documentation PR or an explicitly approved docs-first phase.
- Preserve the *why*: an ADR records context, considered alternatives, decision, trade-offs, consequences, validation and approval date. Never erase history; supersede outdated entries with links.
- If documents disagree, identify the conflict explicitly and seek owner resolution rather than guessing. Current approval is more authoritative than a stale draft, but must be recorded before implementation.
- No manual coding required from the owner: AI agents prepare changes on branches and PRs. Never claim a test ran or a merge occurred unless verified.

## Mandatory source-code style
- Every C and assembly source file begins with a clear header: purpose, responsibilities, dependencies, invariants, relevant architecture/ADR references.
- Every function has a header: purpose, arguments and units, return values, error behavior, side effects, ownership/lifetime, concurrency and interrupt-context constraints when relevant.
- Explain **why** non-obvious lines work: pointers, pointer arithmetic, allocation/free, ownership, bit masks, hardware registers, page tables, memory ordering, interrupts, assembly, ABI assumptions and undefined-behavior hazards. Write for a competent programmer returning to C after decades.
- Use descriptive names, named constants, explicit units, and minimal cleverness. Explain invariants; do not add noisy comments that merely repeat the code.
- Keep comments accurate when code changes. Explain C concepts in `docs/DEVELOPERS_GUIDE.md` and link to that guide when useful.
- Keep interfaces small and documented. No unexplained magic numbers, hidden allocations, or hardware-specific behavior outside driver boundaries.

## Required PR checklist
1. Read and name the applicable decision documents and ADRs.
2. State what changed and why, and distinguish approved behavior from proposal.
3. Update documentation, examples, function headers and inline comments.
4. Record build/test commands, actual results, limitations and untested paths.
5. Verify that QEMU/USB testing does not overwrite Anvilone's Linux Mint disk.
6. Request owner review before merge or physical deployment unless explicitly authorized.

## Current foundational decisions (2026-10-26)
- UEFI + Limine for initial boot; QEMU first, HP Anvilone via dedicated USB later. Secure Boot currently enabled on Anvilone; disable only when preparing unsigned physical test media. Signing and legacy BIOS deferred.
- Modular monolithic C kernel with minimal x86-64 assembly; ring 0 kernel, ring 3 user programs; BASIC interpreter and EDIT separate user applications.
- 64-bit virtual memory/paging, per-process address spaces, RAM-only (no swap), reserved kernel/supervisor resources; return recoverable out-of-memory errors, do not automatically kill user programs.
- Small, stable, versioned interfaces are the goal; exact syscall ABI, paging layout, allocator and driver ABI are **not yet approved**.
- Current status is design/documentation; do not represent planned features as implemented.
