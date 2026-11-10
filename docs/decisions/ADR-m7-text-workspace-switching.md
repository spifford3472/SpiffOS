# M7 — Text workspace switching (implementation design, 2026-11-10)

Status: owner-authorized M7 implementation in progress; final verification pending.

## Context

A-009/A-010/A-011 already authorize nine workspaces, Ctrl+Alt+1..9 and one visible foreground. M6 retains session and nine workspace identity roots, but `memory_service_check` currently assumes active ID is always 1. The boot editor has 65 lines of 4096 bytes each and cannot fit inside a 4096-byte identity root. The current framebuffer console is a viewport and does not retain scrolled pixels or the logo independently.

## Decision and ownership

Use the existing memory service as the authority for active ID and workspace reset generation. Add a checked active-ID setter (1..9) which validates the complete service before changing the session, and make `memory_service_check` accept any valid active ID. The workspace manager owns nine editor instances, console state and sufficient virtual screen backing. Each editor retains logical lines, cursor row/column/offset, insert mode and visibility; the foreground alone paints the physical framebuffer. Do not copy live `struct screen_editor` objects without rebinding internal line pointers to their owned storage. A workspace reset must clear only its owner allocations and rebuild its own editor/display state, never session or other workspaces. Never mutate the active ID until destination preparation succeeds.

## Memory budget and failure policy

Text alone: 65 * 4096 = 266,240 bytes/workspace; nine = 2,396,160 bytes (2.285 MiB). Metadata, console state, screen snapshots and allocator overhead are additional. A memory-service root is metadata only, not an editor buffer. M7 uses nine bounded BSS editor instances and nine fixed 1280×800×4-byte pixel snapshots (36,864,000 bytes = 35.16 MiB), plus 2,396,160 bytes of editor line storage and metadata. The existing M4 one-page heap cannot contain editors or framebuffer images, so the service remains the authoritative identity/reset-generation owner while M7's fixed manager owns diagnostic display/editor storage. This is a deliberate temporary exception to heap-backed per-workspace ownership, not an implicit claim that reset releases RAM. The snapshot bound rejects larger display modes before modifying active state; the boot editor must remain operational on larger modes in a follow-up. QEMU currently uses 1280×800. Snapshot copying is a temporary accuracy-first solution until a bounded text-cell display backing replaces it. On insufficient backing, leave the current editor, active ID, and displayed pixels intact. Never silently fall back to shared text storage.

## Input and rendering

Recognize Ctrl+Alt+top-row digits 1..9 as named workspace events in the translated set-1 decoder before character conversion. Consume the chord without inserting digits. Keep existing punctuation, keypad, Insert and Delete behavior; Ctrl+Alt+0 overview remains deferred. Restore cursor overlay with the existing overwrite block and insert double-underscore behavior. For accurate display restoration, retain or reconstruct all visible characters including boot output, scrolling and cursor overlay, rather than treating editor lines as a complete framebuffer snapshot.

## Alternatives rejected

Dynamic editor allocations from the one-page M4 heap are impossible without extending physical-page backing and its validation. BSS editors and bounded pixel snapshots were therefore chosen for this diagnostic milestone, with the known cost and limitation above. A later text-cell backing will reduce the memory footprint. Storing editors inside 4 KiB roots remains impossible.

## Validation gates

1. Two-workspace hosted tests: content, cursor, mode, display and transactional allocation failure.
2. Expand to nine: valid/invalid active IDs, all workspace states, isolated reset and generation increments.
3. Existing hosted regression suite, real QEMU PS/2 modifier and framebuffer capture tests, reset isolation and cursor overlay comparison.
4. Clean build from scratch, Makefile dependency validation and owner checkout preservation.

No BASIC execution, process scheduler, workspace overview, physical-disk installation or unrelated infrastructure changes in M7.
