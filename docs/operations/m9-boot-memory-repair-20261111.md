# M9 issue #34: 128 MiB boot-memory repair — 2026-11-11

## Defect and root cause
Merged PR #33 fails at QEMU/OVMF 128 and 160 MiB before kernel entry. Captured QMP screenshot shows Limine's high-memory allocator panic while loading the ELF. The baseline kernel BSS was 47,755,264 bytes, including nine 1280x800x4 snapshots (36,864,000 bytes) and 8 MiB of frame-owner slots. Boots at 192, 224 and 256 MiB passed.

## Corrective architecture
Read AGENTS.md, ADR-m9-128m-boot-memory.md, ADR-frame-allocator.md, ADR-memory-services.md and ADR-m7-text-workspace-switching.md.
- Each workspace snapshot is stored in ordinary 4 KiB owned pages, lazily allocated through M2/M3 and validated Limine HHDM.
- A complete set of pages is allocated, zeroed and authorized before snapshot publication. On partial failure the exact workspace frames are released. A rollback failure disables further snapshot allocations.
- The nine workspaces retain exact 32-bit framebuffer pixels, including cursor overlays and F10 return, with the original 1280-pixel snapshot stride.
- M2 owner tokens 0..12 now use uint8_t rather than uint16_t, with a compile-time bound check. The original 4194304 slots and 16 GiB candidate-frame tracking limit are preserved.
- No supervisor reserve is consumed by ordinary snapshots; M6 session authority, keyboard paths and F12 PIT timing are unchanged.

## Verified test evidence from isolated Forge worktree
| Gate | Result |
|---|---|
| Hosted workspace manager (ASan/UBSan), including denied F10 snapshot | PASS |
| Frame allocator, page-access, heap and memory-map sanitizer tests after uint8_t migration | PASS |
| make test-memory | PASS at 128/256/512 MiB with exact framebuffer and serial counters |
| make test-keyboard | PASS existing QMP workspace and editor tests |
| qemu.py test --keyboard --memory-mib 128 --timeout 55 | PASS nine workspaces, F10, F12 fade/restore and editor |
| make test | PASS serial boot marker in real QEMU |
| git diff --check | PASS |

Final kernel BSS is approximately 6.9 MiB, compared with 47.8 MiB on the failing baseline. Tests use pinned Limine and no physical hardware.

## Known limits
Snapshot page backing currently remains allocated until shutdown; a workspace reset invalidates its saved image but does not return frames. Future resource-lifetime work should reclaim pages safely. Physical RAM above 512 MiB has not been tested in this fix (the candidate-frame metadata capacity is preserved at 16 GiB). Larger video modes continue to disallow switching, preserving workspace 1. No BASIC, ring-3 process management, IRQs, network or on-disk format changes.

## Provenance
Local qwen3-coder:30b performed an isolated architecture/interface review. Coordinator implemented the memory change, performed review and ran the recorded tests. No Codex escalation was required.

## Safe deployment and rollback
The owner-maintained Forge checkout and its installed ISO were not modified by this work. A merged artifact must be staged separately with SHA256, while the owner-account installation backs up the current PR #33 ISO before replacement. To roll back, restore the preserved PR #33 ISO and launch QEMU. There are no migrations. No network, OPNsense, physical drives or bootloaders were changed.
