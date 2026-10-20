# ADR-002 — Memory, workspace media, and native execution

Status: ACCEPTED direction; detailed interfaces PROPOSED / OPEN
Date: 2026-10-20

## Decision
- Each Ring 3 process has its own protected x86-64 virtual address space.
- BASIC offers dynamic allocation with configurable process limits; allocation failures must be recoverable.
- PEEK/POKE access a documented SpiffOS logical map, not arbitrary physical RAM or kernel addresses. This is a new map, not the C64 memory map.
- Each workspace has an independent virtual graphics and sound environment. Background graphics/audio timelines continue, but only the active workspace normally reaches physical display/speakers.
- Cross-workspace output is mediated and permission-controlled; notification rights differ from full graphics/audio access, with a future trusted-program policy.
- Support native sprites conceptually through BASIC APIs and controlled logical registers; exact v1 features and command syntax remain open.
- Preserve a future SYS-style capability to run native x86-64 machine code written from BASIC into authorized memory, strictly in Ring 3.

## Native-code safety
Execution must not require writable-and-executable memory simultaneously. A protected writable-to-executable transition, validated entry region, documented ABI, and explicit error handling are required. Native code in the interpreter process can still corrupt that process; separate-process execution remains an open stronger-isolation option.

## Open design questions
- Exact logical memory addresses, register layout, handle model, and executable-region syntax.
- Ownership/compositing of graphics surfaces when several BASIC processes share a workspace.
- Sprite count, sizing, collision, scaling, animation, and v1 scope.
- Page size, supervisor reserve, swap policy, quotas, and OOM behavior.
- SYS argument/return convention and whether native code runs in-process or a helper process.
